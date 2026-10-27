#include "buzzer_cue.h"

namespace {
// Tuned for a listener with age-related hearing loss, which takes the high
// frequencies first. That fights the transducer: a piezo is loudest near its
// resonance, well above 2 kHz. The compromise keeps every tone between 500 Hz
// and 1.8 kHz and buys back detectability three other ways: no step shorter
// than kMinAudibleMs, every alert warbles between two pitches rather than
// holding one (a changing tone is far easier to pick out than a steady one),
// and the cues that matter repeat. The failure cue is the lowest and slowest,
// so it can never be mistaken for a confirmation.
constexpr ToneStep kPress[] = {{1500, 300}};
constexpr ToneStep kPressOffline[] = {{700, 300}};
constexpr ToneStep kEmergencyArmed[] = {{1200, 130}, {1700, 130}, {1200, 130},
                                        {1700, 130}, {1200, 130}, {1700, 130}};
constexpr ToneStep kLightSent[] = {{1400, 260}, {0, 130}, {1400, 260}};
constexpr ToneStep kConfirmed[] = {{1000, 220}, {1300, 220}, {1700, 300}, {0, 200},
                                   {1000, 220}, {1300, 220}, {1700, 300}};
constexpr ToneStep kFailed[] = {{500, 400}, {0, 200}, {500, 400}};
constexpr ToneStep kEmergencyWaiting[] = {{1300, 160}, {1800, 160}, {1300, 160}, {1800, 160}};

template <size_t N>
const ToneStep* pick(const ToneStep (&steps)[N], size_t& count) {
  count = N;
  return steps;
}
}  // namespace

const ToneStep* cueSteps(Cue c, size_t& count) {
  switch (c) {
    case Cue::Press: return pick(kPress, count);
    case Cue::PressOffline: return pick(kPressOffline, count);
    case Cue::EmergencyArmed: return pick(kEmergencyArmed, count);
    case Cue::LightSent: return pick(kLightSent, count);
    case Cue::Confirmed: return pick(kConfirmed, count);
    case Cue::Failed: return pick(kFailed, count);
    case Cue::EmergencyWaiting: return pick(kEmergencyWaiting, count);
    case Cue::None: break;
  }
  count = 0;
  return nullptr;
}

void CuePlayer::start(Cue c, uint32_t nowMs) {
  steps_ = cueSteps(c, count_);
  if (count_ == 0) steps_ = nullptr;
  index_ = 0;
  stepStartMs_ = nowMs;
}

uint16_t CuePlayer::freqAt(uint32_t nowMs) {
  while (steps_ != nullptr) {
    const ToneStep step = steps_[index_];
    if (nowMs - stepStartMs_ < step.ms) return step.freqHz;
    // Advance by the nominal duration, not to nowMs: a late tick must not
    // stretch the step that follows it.
    stepStartMs_ += step.ms;
    if (++index_ >= count_) {
      steps_ = nullptr;
      count_ = 0;
      index_ = 0;
      return 0;
    }
  }
  return 0;
}

CueDecision CueScheduler::decide(const CueInputs& in, uint32_t nowMs) {
  if (in.reminder == Cue::None) {
    reminderActive_ = false;
  } else if (!reminderActive_) {
    reminderActive_ = true;
    reminderSinceMs_ = nowMs;
  }

  CueDecision d;
  bool news = false;
  if (in.holdFired || in.pressEdge) {
    if (!in.playerIdle && news_ != Cue::None) replay_ = news_;
    d.start = in.holdFired ? Cue::EmergencyArmed : in.canSend ? Cue::Press : Cue::PressOffline;
  } else if (!in.playerIdle) {
    // let the current cue finish
  } else if (replay_ != Cue::None) {
    d.start = replay_;
    replay_ = Cue::None;
    news = true;
  } else if (in.pending != Cue::None) {
    d.start = in.pending;
    d.pendingTaken = true;
    news = true;
  } else if (reminderActive_) {
    const uint32_t interval = nowMs - reminderSinceMs_ < fastForMs_ ? fastMs_ : slowMs_;
    if (nowMs - lastCueMs_ >= interval) d.start = in.reminder;
  }
  if (d.start != Cue::None) {
    lastCueMs_ = nowMs;
    news_ = news ? d.start : Cue::None;
  }
  return d;
}
