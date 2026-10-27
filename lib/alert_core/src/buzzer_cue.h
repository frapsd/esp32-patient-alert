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
  bool canSend = true;     // online and no configuration error
  Cue pending = Cue::None;   // one-shot news from the loop task
  Cue reminder = Cue::None;  // repeated while an emergency is live, None otherwise
  bool playerIdle = true;    // nothing is sounding
};

struct CueDecision {
  Cue start = Cue::None;      // cue to start now, None to leave the buzzer alone
  bool pendingTaken = false;  // inputs.pending was consumed
};

// Picks the cue to start on each io tick. Pure, like CuePlayer.
// Priority: what the patient just did cuts in at once, because they need to hear
// that the button reacted. News from the loop task waits for silence instead of
// cutting a cue short, and news that a press cut short plays again from the
// start once the buzzer is free. The reminder comes last, only after
// a quiet interval: fastMs while the emergency is young, slowMs once it has
// lasted fastForMs, so a night-long emergency does not chirp every few seconds.
class CueScheduler {
 public:
  CueScheduler(uint32_t fastMs, uint32_t fastForMs, uint32_t slowMs)
      : fastMs_(fastMs), fastForMs_(fastForMs), slowMs_(slowMs) {}
  CueDecision decide(const CueInputs& in, uint32_t nowMs);

 private:
  uint32_t fastMs_;
  uint32_t fastForMs_;
  uint32_t slowMs_;
  uint32_t lastCueMs_ = 0;
  Cue news_ = Cue::None;      // news cue last started, None once anything else starts
  Cue replay_ = Cue::None;    // news cut short by the patient, to play again
  bool reminderActive_ = false;
  uint32_t reminderSinceMs_ = 0;
};
