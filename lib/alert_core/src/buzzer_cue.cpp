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
// The only falling cue: "quieter from now on".
constexpr ToneStep kMuted[] = {{1300, 250}, {800, 400}};

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
    case Cue::Muted: return pick(kMuted, count);
    case Cue::None: break;
  }
  count = 0;
  return nullptr;
}

void CuePlayer::start(Cue c, uint32_t nowMs) {
  steps_ = cueSteps(c, count_);
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
  const bool wasActive = reminderActive_;
  // A new serial is a new emergency even without a silent tick in between.
  bool newEmergency = false;
  if (in.reminder == Cue::None) {
    reminderActive_ = false;
  } else if (!reminderActive_ || in.emergencySerial != serial_) {
    newEmergency = true;
    reminderActive_ = true;
    reminderSinceMs_ = nowMs;
    serial_ = in.emergencySerial;
  }
  if (!reminderActive_ || newEmergency) {
    confirmedSeen_ = false;
    muted_ = false;
    worseNews_ = false;
    worseToldThisMute_ = false;
  } else if (muted_ && !worseToldThisMute_ && lastReminderIn_ == Cue::EmergencyWaiting &&
             in.reminder == Cue::Failed) {
    worseNews_ = true;
    worseToldThisMute_ = true;
  }
  lastReminderIn_ = in.reminder;
  if (in.pressEdge) {
    pressSawEmergency_ = reminderActive_;
    pressSerial_ = serial_;
  }

  // An acknowledgement ends this emergency: a Hold now is not a mute. The loop
  // posts "confirmed" before it stops publishing the reminder (an NVS save in
  // between), so once seen it counts until the reminder goes or changes.
  if (reminderActive_ && in.pending == Cue::Confirmed) confirmedSeen_ = true;
  const bool confirming = confirmedSeen_ || replay_ == Cue::Confirmed ||
                          (!in.playerIdle && news_ == Cue::Confirmed);
  const bool toggleMute = in.holdFired && !in.emergencyRaised && reminderActive_ &&
                          pressSawEmergency_ && pressSerial_ == serial_ &&
                          nowMs - reminderSinceMs_ >= muteGraceMs_ && !confirming;

  CueDecision d;
  bool news = false;
  if (toggleMute) {
    muted_ = !muted_;
    worseNews_ = false;
    if (muted_) {
      d.start = Cue::Muted;
      news = true;  // replayed if a press cuts it
    } else {
      worseToldThisMute_ = false;
      d.start = in.reminder;  // back at once, then at the current phase's rhythm
    }
  } else if (in.emergencyRaised || in.holdFired) {
    // A new emergency is the latest word: news it cuts short or that is still
    // queued ("help is coming", "request sent") would contradict it.
    replay_ = Cue::None;
    d.start = Cue::EmergencyArmed;
    d.dropPending = true;
    news = true;  // the proof it started: replayed if a press cuts it
  } else if (in.pressEdge) {
    if (!in.playerIdle && news_ != Cue::None) replay_ = news_;
    d.start = in.canSend ? Cue::Press : Cue::PressOffline;
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
  } else if (worseNews_) {
    d.start = Cue::Failed;
    worseNews_ = false;
    news = true;
  } else if (reminderActive_ && !muted_) {
    const uint32_t age = nowMs - reminderSinceMs_;
    const uint32_t interval = age < fastForMs_ ? fastMs_ : age < slowForMs_ ? slowMs_ : slowestMs_;
    if (nowMs - lastReminderMs_ >= interval) d.start = in.reminder;
  }
  if (d.start != Cue::None) {
    news_ = news ? d.start : Cue::None;
    // Only the reminder itself, or a cue before or announcing the emergency,
    // restarts the interval: presses during an emergency must not hold off the
    // one cue that says whether it is getting out.
    if (d.start == in.reminder || !wasActive || newEmergency || in.emergencyRaised) {
      lastReminderMs_ = nowMs;
    }
  }
  return d;
}

uint16_t CueEngine::tick(const CueInputs& in, uint32_t nowMs, CueDecision& d) {
  player_.freqAt(nowMs);  // advance first: a cue that just ended reads idle
  CueInputs now = in;
  now.playerIdle = player_.idle();
  d = scheduler_.decide(now, nowMs);
  if (d.start != Cue::None) player_.start(d.start, nowMs);
  return player_.freqAt(nowMs);
}

static_assert(static_cast<uint8_t>(Cue::Muted) < 16, "cueBit() packs cues in a uint16_t");

uint16_t cueBit(Cue c) {
  return c == Cue::None ? 0 : static_cast<uint16_t>(1u << static_cast<uint8_t>(c));
}

Cue firstPending(uint16_t mask) {
  if (mask & cueBit(Cue::Confirmed)) return Cue::Confirmed;
  if (mask & cueBit(Cue::Failed)) return Cue::Failed;
  for (uint8_t i = 1; i < 16; i++) {
    if (mask & (1u << i)) return static_cast<Cue>(i);
  }
  return Cue::None;
}
