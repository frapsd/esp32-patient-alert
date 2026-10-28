#include "alert_machine.h"
#include <string.h>

static void copyStr(char* dst, size_t cap, const char* src) {
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

AlertMachine::AlertMachine(const MachineConfig& cfg) : cfg_(cfg) {}

PersistedState AlertMachine::persisted() const {
  PersistedState s;
  s.mode = mode_;
  copyStr(s.receipt, sizeof(s.receipt), receipt_);
  s.confirmedAtEpoch = confirmedAtEpoch_;
  return s;
}

// Marks dirty iff a persisted field differs from `before`.
void AlertMachine::commit(const PersistedState& before) {
  if (before.mode != mode_ || strncmp(before.receipt, receipt_, kReceiptLen) != 0 ||
      before.confirmedAtEpoch != confirmedAtEpoch_) {
    dirty_ = true;
  }
}

bool AlertMachine::takeDirty() {
  bool d = dirty_;
  dirty_ = false;
  return d;
}

Mode AlertMachine::mode() const { return mode_; }

bool AlertMachine::confirmedExpired(uint32_t nowMs) const {
  return (uint32_t)(nowMs - confirmedAtMs_) >= cfg_.confirmedWindowS * 1000u;
}

// A second press now would upgrade to an emergency (press to press).
bool AlertMachine::upgradeWindowOpen(uint32_t nowMs) const {
  return hasLastLightPress_ && (uint32_t)(nowMs - lastLightPressMs_) < cfg_.lightUpgradeWindowMs;
}

bool AlertMachine::lightShownAt(uint32_t nowMs) const {
  return lightShown_ && (uint32_t)(nowMs - lightShownAtMs_) < cfg_.lightSentShowMs;
}

// Ends time-limited state so stale timestamps never alias after a millis wrap.
void AlertMachine::expireTimedState(uint32_t nowMs) {
  if (mode_ == Mode::Confirmed && confirmedExpired(nowMs)) {
    mode_ = Mode::Idle;
    confirmedAtEpoch_ = 0;
    confirmUnresolved_ = false;
  }
  if (hasLastLightPress_ &&
      (uint32_t)(nowMs - lastLightPressMs_) >= cfg_.lightUpgradeWindowMs + kLightPressMemoryMs) {
    hasLastLightPress_ = false;
  }
  if (lightShown_ && (uint32_t)(nowMs - lightShownAtMs_) >= cfg_.lightSentShowMs) {
    lightShown_ = false;
  }
}

void AlertMachine::triggerEmergency(uint32_t nowMs) {
  lightCount_ = 0;
  hasLastLightPress_ = false;
  mode_ = Mode::EmergencyPending;
  receipt_[0] = '\0';
  confirmedAtEpoch_ = 0;
  confirmUnresolved_ = false;
  emergencySendFailed_ = false;
  emergencyTimer_.set(nowMs, 0);
  // A pending ack notice is kept.
}

void AlertMachine::retryAfter(Timer& t, HttpOutcome failure, uint32_t nowMs) {
  if (failure == HttpOutcome::Rejected) {
    configError_ = true;
    t.set(nowMs, cfg_.rejectedRetryMs);
  } else {
    t.set(nowMs, cfg_.transientRetryMs);
  }
}

void AlertMachine::restore(const PersistedState& s, uint32_t nowMs, uint32_t nowEpoch) {
  mode_ = Mode::Idle;
  receipt_[0] = '\0';
  confirmedAtEpoch_ = 0;
  confirmedAtMs_ = 0;
  confirmUnresolved_ = false;
  configError_ = false;
  emergencySendFailed_ = false;
  lastRequestFailed_ = false;
  ackPending_ = false;
  ackDevice_[0] = '\0';
  lightCount_ = 0;
  hasLastLightPress_ = false;
  lightShown_ = false;
  dirty_ = false;

  switch (s.mode) {
    case Mode::EmergencyPending:
      mode_ = Mode::EmergencyPending;
      copyStr(receipt_, sizeof(receipt_), s.receipt);
      emergencyTimer_.set(nowMs, 0);
      break;
    case Mode::EmergencyAwaitingAck:
      copyStr(receipt_, sizeof(receipt_), s.receipt);
      mode_ = receipt_[0] ? Mode::EmergencyAwaitingAck : Mode::EmergencyPending;
      emergencyTimer_.set(nowMs, 0);
      break;
    case Mode::Confirmed: {
      if (nowEpoch == 0) {
        // Wall time not known yet (e.g. just after a power cut): stay confirmed
        // with a full window and let onWallClock() settle it. Keeping the saved
        // epoch makes persisted() equal `s`, so NVS is not rewritten.
        mode_ = Mode::Confirmed;
        confirmedAtEpoch_ = s.confirmedAtEpoch;
        confirmedAtMs_ = nowMs;
        confirmUnresolved_ = true;
        break;
      }
      if (nowEpoch < s.confirmedAtEpoch) break;
      uint32_t elapsedS = nowEpoch - s.confirmedAtEpoch;
      if (elapsedS >= cfg_.confirmedWindowS) break;
      mode_ = Mode::Confirmed;
      confirmedAtEpoch_ = s.confirmedAtEpoch;
      confirmedAtMs_ = nowMs - elapsedS * 1000u;
      break;
    }
    default:
      break;
  }
  commit(s);
}

void AlertMachine::onWallClock(uint32_t nowMs, uint32_t nowEpoch) {
  if (!confirmUnresolved_ || nowEpoch == 0) return;
  confirmUnresolved_ = false;
  if (mode_ != Mode::Confirmed) return;
  PersistedState before = persisted();
  const uint32_t elapsedS = nowEpoch - confirmedAtEpoch_;
  if (nowEpoch < confirmedAtEpoch_ || elapsedS >= cfg_.confirmedWindowS) {
    mode_ = Mode::Idle;
    confirmedAtEpoch_ = 0;
  } else {
    confirmedAtMs_ = nowMs - elapsedS * 1000u;
  }
  commit(before);
}

void AlertMachine::onGesture(Gesture g, uint32_t nowMs, uint32_t pressMs, uint32_t pressEpoch) {
  PersistedState before = persisted();

  // Ages relative to nowMs keep the comparisons wraparound-safe. A press stamped
  // "after" nowMs cannot happen by construction; treat it as pressed now so a
  // wrapped difference never looks like an old press inside a window.
  if ((int32_t)(nowMs - pressMs) < 0) pressMs = nowMs;
  const uint32_t pressAge = nowMs - pressMs;

  // Decide against the state as it was at the press, before expiring it at nowMs.
  // A gesture stamped before the acknowledgement (a Tap at its press, a Hold at
  // its threshold) was made against an emergency that was still
  // unacknowledged, and carried no new information then.
  // The confirmed window itself does not change how a gesture reads: inside it
  // a Tap is still a light request and a Hold still an emergency. Forcing every
  // press to an emergency for the whole window took the choice away from the patient,
  // who cannot see the LED that says the window is open.
  bool pressedBeforeConfirm = false;
  if (mode_ == Mode::Confirmed && !confirmUnresolved_) {
    pressedBeforeConfirm = pressAge > nowMs - confirmedAtMs_;
  }
  bool upgrade = false;
  if (hasLastLightPress_) {
    const uint32_t lightAge = nowMs - lastLightPressMs_;
    upgrade = pressAge <= lightAge && lightAge - pressAge < cfg_.lightUpgradeWindowMs;
  }

  expireTimedState(nowMs);
  if (mode_ == Mode::EmergencyPending || mode_ == Mode::EmergencyAwaitingAck) {
    // already raising an emergency
  } else if (pressedBeforeConfirm) {
    // pressed while the emergency was unacknowledged: ignored
  } else if (g == Gesture::Hold || upgrade) {
    triggerEmergency(nowMs);
    emergencySerial_++;
  } else {
    if (lightCount_ < kLightQueueCap) {
      if (lightCount_ == 0) lightTimer_.set(nowMs, 0);
      lightQueue_[lightCount_++] = pressEpoch;
    }
    hasLastLightPress_ = true;  // also when dropped: a second press is still an upgrade
    lastLightPressMs_ = pressMs;
  }
  commit(before);
}

Command AlertMachine::nextCommand(uint32_t nowMs) {
  PersistedState before = persisted();
  expireTimedState(nowMs);
  commit(before);

  Command c;
  if (mode_ == Mode::EmergencyPending && emergencyTimer_.due(nowMs)) {
    c.kind = CommandKind::SendEmergency;
  } else if (mode_ == Mode::EmergencyAwaitingAck && emergencyTimer_.due(nowMs)) {
    c.kind = CommandKind::PollReceipt;
    copyStr(c.receipt, sizeof(c.receipt), receipt_);
  } else if (ackPending_ && ackTimer_.due(nowMs)) {
    c.kind = CommandKind::SendAckNotice;
    copyStr(c.device, sizeof(c.device), ackDevice_);
  } else if (lightCount_ > 0 && lightTimer_.due(nowMs)) {
    c.kind = CommandKind::SendLight;
    c.pressEpoch = lightQueue_[0];
  }
  return c;
}

void AlertMachine::onEmergencySent(const SendResponse& r, uint32_t nowMs) {
  if (mode_ != Mode::EmergencyPending) return;
  PersistedState before = persisted();
  HttpOutcome o = r.outcome;
  if (o == HttpOutcome::Ok && r.receipt[0] == '\0') o = HttpOutcome::Transient;
  emergencySendFailed_ = o != HttpOutcome::Ok;
  lastRequestFailed_ = o != HttpOutcome::Ok;
  if (o == HttpOutcome::Ok) {
    configError_ = false;
    copyStr(receipt_, sizeof(receipt_), r.receipt);
    mode_ = Mode::EmergencyAwaitingAck;
    emergencyTimer_.set(nowMs, cfg_.pollIntervalMs);
  } else {
    retryAfter(emergencyTimer_, o, nowMs);
  }
  commit(before);
}

void AlertMachine::onReceipt(const ReceiptStatus& r, uint32_t nowMs, uint32_t nowEpoch) {
  if (mode_ != Mode::EmergencyAwaitingAck) return;
  PersistedState before = persisted();
  lastRequestFailed_ = r.outcome != HttpOutcome::Ok;
  switch (r.outcome) {
    case HttpOutcome::Ok:
      configError_ = false;
      if (r.acknowledged) {
        mode_ = Mode::Confirmed;
        receipt_[0] = '\0';
        confirmedAtMs_ = nowMs;
        confirmedAtEpoch_ = nowEpoch;
        ackPending_ = true;
        copyStr(ackDevice_, sizeof(ackDevice_), r.ackDevice);
        ackTimer_.set(nowMs, 0);
      } else if (r.expired) {
        triggerEmergency(nowMs);
      } else {
        emergencyTimer_.set(nowMs, cfg_.pollIntervalMs);
      }
      break;
    case HttpOutcome::Transient:
      emergencyTimer_.set(nowMs, cfg_.transientRetryMs);
      break;
    case HttpOutcome::Rejected:
      triggerEmergency(nowMs);
      retryAfter(emergencyTimer_, HttpOutcome::Rejected, nowMs);
      break;
  }
  commit(before);
}

void AlertMachine::onLightSent(HttpOutcome o, uint32_t nowMs) {
  if (lightCount_ == 0) return;  // queue cleared by an emergency meanwhile
  lastRequestFailed_ = o != HttpOutcome::Ok;
  if (o == HttpOutcome::Ok) {
    configError_ = false;
    for (size_t i = 1; i < lightCount_; i++) lightQueue_[i - 1] = lightQueue_[i];
    lightCount_--;
    lightTimer_.set(nowMs, 0);
    lightShown_ = true;
    lightShownAtMs_ = nowMs;
  } else {
    retryAfter(lightTimer_, o, nowMs);
  }
}

void AlertMachine::onAckNoticeSent(HttpOutcome o, uint32_t nowMs) {
  if (!ackPending_) return;
  lastRequestFailed_ = o != HttpOutcome::Ok;
  if (o == HttpOutcome::Ok) {
    configError_ = false;
    ackPending_ = false;
    ackDevice_[0] = '\0';
  } else {
    retryAfter(ackTimer_, o, nowMs);
  }
}

void AlertMachine::tick(uint32_t nowMs) {
  PersistedState before = persisted();
  expireTimedState(nowMs);
  commit(before);
}

LedPattern AlertMachine::led(uint32_t nowMs, bool online) const {
  if (mode_ == Mode::EmergencyPending || mode_ == Mode::EmergencyAwaitingAck) {
    return LedPattern::EmergencyActive;
  }
  // Above everything else: for these 60 s it is what the patient can act on.
  if (upgradeWindowOpen(nowMs)) return LedPattern::UpgradeWindow;
  if (mode_ == Mode::Confirmed && !confirmedExpired(nowMs)) return LedPattern::Confirmed;
  if (configError_) return LedPattern::ConfigError;
  if (!online) return LedPattern::Offline;
  if (lightShownAt(nowMs)) return LedPattern::LightSent;
  return LedPattern::Idle;
}

LedPlan AlertMachine::ledPlan(uint32_t nowMs, bool online) const {
  // Time left to each deadline that is still ahead, under the same age checks
  // led() uses, so no remaining time can underflow.
  uint32_t left[kLedPlanSteps - 1];
  size_t n = 0;
  if (upgradeWindowOpen(nowMs)) {
    left[n++] = cfg_.lightUpgradeWindowMs - (nowMs - lastLightPressMs_);
  }
  if (lightShownAt(nowMs)) left[n++] = cfg_.lightSentShowMs - (nowMs - lightShownAtMs_);
  if (mode_ == Mode::Confirmed && !confirmedExpired(nowMs)) {
    left[n++] = cfg_.confirmedWindowS * 1000u - (nowMs - confirmedAtMs_);
  }
  for (size_t i = 1; i < n; i++) {  // insertion sort, at most three
    for (size_t j = i; j > 0 && left[j] < left[j - 1]; j--) {
      const uint32_t t = left[j];
      left[j] = left[j - 1];
      left[j - 1] = t;
    }
  }

  LedPlan p;
  p.fromMs = nowMs;
  p.pattern[0] = led(nowMs, online);
  p.steps = 1;
  for (size_t i = 0; i < n; i++) {
    const LedPattern next = led(nowMs + left[i], online);
    if (next == p.pattern[p.steps - 1]) continue;
    p.pattern[p.steps] = next;
    p.offsetMs[p.steps] = left[i];
    p.steps++;
  }
  return p;
}
