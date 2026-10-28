#pragma once
#include <stddef.h>
#include <stdint.h>
#include "alert_types.h"
#include "led_pattern.h"
#include "persisted_state.h"
#include "pushover_protocol.h"

struct MachineConfig {
  uint32_t lightUpgradeWindowMs, confirmedWindowS, pollIntervalMs,
           transientRetryMs, rejectedRetryMs, lightSentShowMs;
};

enum class CommandKind : uint8_t { None, SendEmergency, PollReceipt, SendLight, SendAckNotice };

struct Command {
  CommandKind kind = CommandKind::None;
  uint32_t pressEpoch = 0;             // SendLight: wall time of the press, 0 = unknown
  char receipt[kReceiptLen] = {};      // PollReceipt
  char device[kDeviceLen] = {};        // SendAckNotice: "" = anonymous
};

// Decides what to send when, retries, and what the LED shows. The caller runs
// requests strictly sequentially: nextCommand(), execute, feed the matching
// on*() result, persist when takeDirty(). All millisecond times are
// wraparound-safe (only elapsed = now - start is compared).
class AlertMachine {
 public:
  explicit AlertMachine(const MachineConfig&);
  // nowEpoch 0 = wall time unknown. A saved Confirmed state then resumes with
  // a full window from nowMs (gestures read as usual) until
  // onWallClock() can place the real window; NVS is left untouched meanwhile,
  // unless that full window runs out first (tick() then ends it, which is safe:
  // at least as long has passed since the acknowledgement).
  void restore(const PersistedState&, uint32_t nowMs, uint32_t nowEpoch);
  // Call regularly with the current wall time (0 = unknown). Resolves a
  // Confirmed window restored without wall time: keeps only its real remaining
  // time, or ends it if it is over. A no-op otherwise.
  void onWallClock(uint32_t nowMs, uint32_t nowEpoch);
  // pressMs/pressEpoch: when the button was pressed (stamped by the button
  // task); nowMs: when the gesture is processed (may be later if the caller
  // was busy). Window decisions use the press time, timers use nowMs.
  // pressEpoch 0 = wall time unknown at the press.
  void onGesture(Gesture, uint32_t nowMs, uint32_t pressMs, uint32_t pressEpoch);
  Command nextCommand(uint32_t nowMs);     // None if nothing is due yet
  // Call every loop iteration: ends timed state that is over, so a stale
  // timestamp never aliases after a millis() wrap (nextCommand() only runs
  // while online). Persisted changes mark the state dirty.
  void tick(uint32_t nowMs);
  void onEmergencySent(const SendResponse&, uint32_t nowMs);
  void onReceipt(const ReceiptStatus&, uint32_t nowMs, uint32_t nowEpoch);
  void onLightSent(HttpOutcome, uint32_t nowMs);
  void onAckNoticeSent(HttpOutcome, uint32_t nowMs);
  Mode mode() const;
  LedPattern led(uint32_t nowMs, bool online) const;
  // led() now and from each known later deadline (upgrade window end, "sent"
  // end, confirmed end), assuming online and the config error stay as they are.
  LedPlan ledPlan(uint32_t nowMs, bool online) const;
  // Pushover rejected the last request; cleared by the next accepted one.
  bool configError() const { return configError_; }
  // An emergency is being sent or awaits acknowledgement.
  bool emergencyLive() const {
    return mode_ == Mode::EmergencyPending || mode_ == Mode::EmergencyAwaitingAck;
  }
  // Counts the emergencies the patient started (a Hold, or the upgrade rule),
  // so the buzzer can tell a new emergency from the same one going on. A
  // re-send after Pushover gave up, or rejected the receipt, keeps the number.
  // Wraps at 256; only a change matters.
  uint8_t emergencySerial() const { return emergencySerial_; }
  // An emergency is waiting to be sent and its last attempt failed (no
  // internet behind the Wi-Fi, Pushover down, TLS error...): nobody has it yet.
  bool emergencySendFailing() const {
    return mode_ == Mode::EmergencyPending && emergencySendFailed_;
  }
  // The latest request of any kind (light, emergency, receipt, ack notice)
  // failed; cleared by the next one that succeeds. Forgotten on restore.
  bool lastRequestFailed() const { return lastRequestFailed_; }
  bool takeDirty();                       // true once after any change to the persisted fields
  PersistedState persisted() const;

 private:
  // Due when (now - startMs) >= delayMs.
  struct Timer {
    uint32_t startMs = 0;
    uint32_t delayMs = 0;
    void set(uint32_t nowMs, uint32_t delay) { startMs = nowMs; delayMs = delay; }
    bool due(uint32_t nowMs) const { return (uint32_t)(nowMs - startMs) >= delayMs; }
  };
  static constexpr size_t kLightQueueCap = 4;
  // How long the first light press is remembered after its window: a second
  // press made inside the window can be processed much later (a Tap is only
  // emitted at release, and the loop may be blocked in a request), and must
  // still upgrade. Far below the 49.7-day millis() wrap.
  static constexpr uint32_t kLightPressMemoryMs = 3600000;

  void triggerEmergency(uint32_t nowMs);
  void expireTimedState(uint32_t nowMs);
  bool confirmedExpired(uint32_t nowMs) const;
  bool upgradeWindowOpen(uint32_t nowMs) const;
  bool lightShownAt(uint32_t nowMs) const;
  void retryAfter(Timer& t, HttpOutcome failure, uint32_t nowMs);
  void commit(const PersistedState& before);

  MachineConfig cfg_;
  Mode mode_ = Mode::Idle;
  char receipt_[kReceiptLen] = {};
  uint32_t confirmedAtEpoch_ = 0;
  uint32_t confirmedAtMs_ = 0;
  bool confirmUnresolved_ = false;  // Confirmed restored without wall time
  bool dirty_ = false;
  bool configError_ = false;
  bool emergencySendFailed_ = false;  // last SendEmergency for this emergency failed
  bool lastRequestFailed_ = false;
  uint8_t emergencySerial_ = 0;

  Timer emergencyTimer_;  // send in EmergencyPending, poll in EmergencyAwaitingAck

  bool ackPending_ = false;
  char ackDevice_[kDeviceLen] = {};
  Timer ackTimer_;

  uint32_t lightQueue_[kLightQueueCap] = {};  // pressEpoch of each request, FIFO
  size_t lightCount_ = 0;
  Timer lightTimer_;
  bool hasLastLightPress_ = false;
  uint32_t lastLightPressMs_ = 0;
  bool lightShown_ = false;
  uint32_t lightShownAtMs_ = 0;
};
