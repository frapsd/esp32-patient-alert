#pragma once
#include <cstddef>
#include <cstdint>

// Audible counterparts to the LED patterns, for a patient who cannot see the
// LED. Cues are one-shot sequences fired on transitions, never a steady tone:
// the device sits by a bed and anything continuous would be unusable at night.
// The repeating ones are the reminders CueScheduler re-fires while an emergency
// is live: EmergencyWaiting, or Failed when it cannot get out.
enum class Cue : uint8_t {
  None,
  Press,             // press accepted; fires before the gesture is known
  PressOffline,      // same, but nothing can be sent yet
  EmergencyArmed,    // hold threshold reached, the patient can let go
  LightSent,         // light request accepted by Pushover
  Confirmed,         // someone took charge
  Failed,            // nothing gets out: configuration error, or an emergency not yet sent
  EmergencyWaiting,  // emergency delivered or being sent, nobody has acknowledged yet
  Muted,             // the patient silenced this emergency's reminders
  // cueBit() packs every cue into a uint16_t: at most 16 values.
};

struct ToneStep {
  uint16_t freqHz;  // 0 = silence
  uint16_t ms;
};

// Shortest audible step. A hard-of-hearing listener needs a tone to last before
// it registers at all, so nothing in the cue tables is briefer than this.
constexpr uint16_t kMinAudibleMs = 130;

// Steps for a cue. count is set to the step count, 0 for Cue::None.
const ToneStep* cueSteps(Cue c, size_t& count);

// Walks a cue's steps against a millis() clock. Pure: no hardware, so the
// native tests cover it. Wraparound-safe (unsigned time math).
class CuePlayer {
 public:
  // Starts c at nowMs, cutting short whatever was playing.
  void start(Cue c, uint32_t nowMs);
  // Frequency to emit at nowMs, 0 when silent. Call every tick: it also
  // advances the sequence.
  uint16_t freqAt(uint32_t nowMs);
  bool idle() const { return steps_ == nullptr; }

 private:
  const ToneStep* steps_ = nullptr;
  size_t count_ = 0;
  size_t index_ = 0;
  uint32_t stepStartMs_ = 0;
};

// What the io task knows on one tick. Everything but the button comes from the
// loop task, which may be stuck in an HTTPS request for seconds.
struct CueInputs {
  bool pressEdge = false;  // debounced press accepted on this tick
  bool holdFired = false;  // Gesture::Hold emitted on this tick
  bool canSend = false;    // an emergency pressed now could get out
  bool emergencyRaised = false;  // the loop raised an emergency without a Hold
  Cue pending = Cue::None;   // one-shot news from the loop task
  Cue reminder = Cue::None;  // repeated while an emergency is live, None otherwise
  uint8_t emergencySerial = 0;  // which emergency the reminder is for (AlertMachine::emergencySerial)
  bool playerIdle = true;    // nothing is sounding
};

struct CueDecision {
  Cue start = Cue::None;      // cue to start now, None to leave the buzzer alone
  bool pendingTaken = false;  // inputs.pending was consumed
  bool dropPending = false;   // an emergency started: queued news is stale
};

// Picks the cue to start on each io tick. Pure, like CuePlayer.
// Priority: a new emergency (a Hold, or one the loop raised) and the patient's
// press cut in at once, because they need to hear that the button reacted.
// News from the loop task waits for silence instead of cutting a cue short,
// and news that a press cut short plays again from the start once the buzzer
// is free, unless an emergency started meanwhile. The reminder comes last,
// only after a quiet interval since the last reminder (presses do not count):
// fastMs while the emergency is young, slowMs once it has lasted fastForMs,
// slowestMs once it has lasted slowForMs, so a night-long emergency does not
// chirp every few seconds.
//
// The patient can mute an emergency's reminders with a Hold, and unmute them
// with another. A Hold mutes only when the emergency is at least muteGraceMs
// old for this task, was already live when the button went down, and no
// acknowledgement is arriving; otherwise it plays "armed" as before (an
// anxious second hold right after the first must not silence it). Muting
// lasts until the emergency ends or a new one starts (emergencySerial
// changes). While muted, presses and news still sound, and an emergency that
// stops getting out says so once.
class CueScheduler {
 public:
  CueScheduler(uint32_t fastMs, uint32_t fastForMs, uint32_t slowMs, uint32_t slowForMs,
               uint32_t slowestMs, uint32_t muteGraceMs)
      : fastMs_(fastMs), fastForMs_(fastForMs), slowMs_(slowMs), slowForMs_(slowForMs),
        slowestMs_(slowestMs), muteGraceMs_(muteGraceMs) {}
  CueDecision decide(const CueInputs& in, uint32_t nowMs);

 private:
  uint32_t fastMs_;
  uint32_t fastForMs_;
  uint32_t slowMs_;
  uint32_t slowForMs_;
  uint32_t slowestMs_;
  uint32_t muteGraceMs_;
  uint32_t lastReminderMs_ = 0;  // last reminder, or the start of the emergency
  Cue news_ = Cue::None;      // news cue last started, None once anything else starts
  Cue replay_ = Cue::None;    // news cut short by the patient, to play again
  bool reminderActive_ = false;
  uint32_t reminderSinceMs_ = 0;  // when this task first saw the emergency
  uint8_t serial_ = 0;            // emergency the reminder is for
  Cue lastReminderIn_ = Cue::None;
  bool muted_ = false;
  bool worseNews_ = false;   // muted and the emergency stopped getting out: say it once
  bool worseToldThisMute_ = false;
  bool confirmedSeen_ = false;  // "confirmed" was posted for this emergency
  bool pressSawEmergency_ = false;  // at the last press edge, this emergency was live
  uint8_t pressSerial_ = 0;
};

// CuePlayer and CueScheduler together, as the io task runs them each tick: the
// player advances first, so the scheduler sees whether it really is idle.
class CueEngine {
 public:
  CueEngine(uint32_t fastMs, uint32_t fastForMs, uint32_t slowMs, uint32_t slowForMs,
            uint32_t slowestMs, uint32_t muteGraceMs)
      : scheduler_(fastMs, fastForMs, slowMs, slowForMs, slowestMs, muteGraceMs) {}
  // in.playerIdle is ignored. d receives the scheduler's decision. Returns the
  // frequency to emit, 0 for silence.
  uint16_t tick(const CueInputs& in, uint32_t nowMs, CueDecision& d);

 private:
  CuePlayer player_;
  CueScheduler scheduler_;
};

// News cues posted by the loop task are kept as a bit set, so one posted while
// the buzzer is busy cannot overwrite another.
uint16_t cueBit(Cue c);
// The most important cue in mask: Confirmed, then Failed, then the rest in
// enum order. None for an empty mask.
Cue firstPending(uint16_t mask);
