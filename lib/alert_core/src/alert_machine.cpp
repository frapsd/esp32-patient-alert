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

// Ends time-limited state so stale timestamps never alias after a millis wrap.
void AlertMachine::expireTimedState(uint32_t nowMs) {
  if (mode_ == Mode::Confirmed && confirmedExpired(nowMs)) {
    mode_ = Mode::Idle;
    confirmedAtEpoch_ = 0;
  }
  if (hasLastLightPress_ && (uint32_t)(nowMs - lastLightPressMs_) >= cfg_.lightUpgradeWindowMs) {
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
  configError_ = false;
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
      if (nowEpoch == 0 || nowEpoch < s.confirmedAtEpoch) break;
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

void AlertMachine::onGesture(Gesture g, uint32_t nowMs, uint32_t nowEpoch) {
  PersistedState before = persisted();
  expireTimedState(nowMs);
  switch (mode_) {
    case Mode::EmergencyPending:
    case Mode::EmergencyAwaitingAck:
      break;  // already raising an emergency
    case Mode::Confirmed:
      triggerEmergency(nowMs);
      break;
    case Mode::Idle:
      if (g == Gesture::Hold || hasLastLightPress_) {
        triggerEmergency(nowMs);
        break;
      }
      if (lightCount_ < kLightQueueCap) {
        if (lightCount_ == 0) lightTimer_.set(nowMs, 0);
        lightQueue_[lightCount_++] = nowEpoch;
      }
      hasLastLightPress_ = true;  // also when dropped: a second press is still an upgrade
      lastLightPressMs_ = nowMs;
      break;
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
  if (o == HttpOutcome::Ok) {
    configError_ = false;
    ackPending_ = false;
    ackDevice_[0] = '\0';
  } else {
    retryAfter(ackTimer_, o, nowMs);
  }
}

LedPattern AlertMachine::led(uint32_t nowMs, bool online) const {
  if (mode_ == Mode::EmergencyPending || mode_ == Mode::EmergencyAwaitingAck) {
    return LedPattern::EmergencyActive;
  }
  if (mode_ == Mode::Confirmed && !confirmedExpired(nowMs)) return LedPattern::Confirmed;
  if (configError_) return LedPattern::ConfigError;
  if (!online) return LedPattern::Offline;
  if (lightShown_ && (uint32_t)(nowMs - lightShownAtMs_) < cfg_.lightSentShowMs) {
    return LedPattern::LightSent;
  }
  return LedPattern::Idle;
}
