#include <unity.h>
#include <cstddef>
#include <initializer_list>
#include <vector>
#include "buzzer_cue.h"

void setUp() {}
void tearDown() {}

static constexpr std::initializer_list<Cue> kAllCues = {
    Cue::Press,     Cue::PressOffline, Cue::EmergencyArmed,  Cue::LightSent,
    Cue::Confirmed, Cue::Failed,       Cue::EmergencyWaiting, Cue::Muted};

static void test_none_is_silent() {
  CuePlayer p;
  TEST_ASSERT_TRUE(p.idle());
  p.start(Cue::None, 100);
  TEST_ASSERT_TRUE(p.idle());
  TEST_ASSERT_EQUAL_UINT16(0, p.freqAt(100));
}

static void test_single_step_cue_ends_on_time() {
  CuePlayer p;
  p.start(Cue::Press, 1000);  // 1500 Hz for 300 ms
  TEST_ASSERT_FALSE(p.idle());
  TEST_ASSERT_EQUAL_UINT16(1500, p.freqAt(1000));
  TEST_ASSERT_EQUAL_UINT16(1500, p.freqAt(1299));
  TEST_ASSERT_EQUAL_UINT16(0, p.freqAt(1300));
  TEST_ASSERT_TRUE(p.idle());
}

static void test_multi_step_cue_has_a_silent_gap() {
  CuePlayer p;
  p.start(Cue::LightSent, 0);  // 1400/260, silence/130, 1400/260
  TEST_ASSERT_EQUAL_UINT16(1400, p.freqAt(0));
  TEST_ASSERT_EQUAL_UINT16(1400, p.freqAt(259));
  TEST_ASSERT_EQUAL_UINT16(0, p.freqAt(260));
  TEST_ASSERT_EQUAL_UINT16(0, p.freqAt(389));
  TEST_ASSERT_EQUAL_UINT16(1400, p.freqAt(390));
  TEST_ASSERT_EQUAL_UINT16(1400, p.freqAt(649));
  TEST_ASSERT_EQUAL_UINT16(0, p.freqAt(650));
  TEST_ASSERT_TRUE(p.idle());
}

// A tick that arrives late must not stretch the steps that follow it.
static void test_a_late_tick_does_not_stretch_the_sequence() {
  CuePlayer p;
  p.start(Cue::LightSent, 0);
  TEST_ASSERT_EQUAL_UINT16(0, p.freqAt(300));  // first tick lands mid-gap
  TEST_ASSERT_EQUAL_UINT16(1400, p.freqAt(390));
  TEST_ASSERT_EQUAL_UINT16(0, p.freqAt(650));
}

static void test_confirmed_rises_so_it_cannot_be_mistaken() {
  size_t n = 0;
  const ToneStep* s = cueSteps(Cue::Confirmed, n);
  TEST_ASSERT_TRUE(n >= 3);
  TEST_ASSERT_TRUE(s[0].freqHz < s[1].freqHz);
  TEST_ASSERT_TRUE(s[1].freqHz < s[2].freqHz);
}

static void test_failure_is_lower_than_every_confirmation() {
  size_t nFail = 0, nOk = 0;
  const ToneStep* fail = cueSteps(Cue::Failed, nFail);
  const ToneStep* ok = cueSteps(Cue::LightSent, nOk);
  TEST_ASSERT_TRUE(nFail > 0 && nOk > 0);
  TEST_ASSERT_TRUE(fail[0].freqHz < ok[0].freqHz);
}

// The patient is hard of hearing: a tone too brief to register is no feedback.
static void test_every_step_lasts_long_enough_to_hear() {
  for (Cue c : kAllCues) {
    size_t n = 0;
    const ToneStep* s = cueSteps(c, n);
    TEST_ASSERT_TRUE(n > 0);
    for (size_t i = 0; i < n; i++) TEST_ASSERT_TRUE(s[i].ms >= kMinAudibleMs);
  }
}

// Age-related loss takes the high frequencies first; nothing may sit up there.
static void test_no_tone_is_pitched_out_of_reach() {
  for (Cue c : kAllCues) {
    size_t n = 0;
    const ToneStep* s = cueSteps(c, n);
    for (size_t i = 0; i < n; i++) {
      if (s[i].freqHz == 0) continue;  // silence
      TEST_ASSERT_TRUE(s[i].freqHz >= 500);
      TEST_ASSERT_TRUE(s[i].freqHz <= 1800);
    }
  }
}

// A warbling alert is far easier to pick out than a steady one.
static void test_the_two_alerts_warble() {
  for (Cue c : {Cue::EmergencyArmed, Cue::EmergencyWaiting}) {
    size_t n = 0;
    const ToneStep* s = cueSteps(c, n);
    TEST_ASSERT_TRUE(n >= 2);
    for (size_t i = 1; i < n; i++) TEST_ASSERT_TRUE(s[i].freqHz != s[i - 1].freqHz);
  }
}

static void test_start_preempts_whatever_was_playing() {
  CuePlayer p;
  p.start(Cue::Confirmed, 0);
  TEST_ASSERT_EQUAL_UINT16(1000, p.freqAt(50));
  p.start(Cue::Press, 50);
  TEST_ASSERT_EQUAL_UINT16(1500, p.freqAt(50));
}

static void test_wraparound() {
  CuePlayer p;
  uint32_t t = 0xFFFFFF00u;  // the 300 ms step straddles the millis() wrap
  p.start(Cue::Press, t);
  TEST_ASSERT_EQUAL_UINT16(1500, p.freqAt(t));
  TEST_ASSERT_EQUAL_UINT16(1500, p.freqAt((uint32_t)(t + 299)));
  TEST_ASSERT_EQUAL_UINT16(0, p.freqAt((uint32_t)(t + 300)));
}

// Reminder every 5 s for the first 120 s of an emergency, then every 30 s.
static CueScheduler scheduler() { return CueScheduler(5000, 120000, 30000, 600000, 120000, 30000); }

static CueInputs quiet() {
  CueInputs in;
  in.canSend = true;
  return in;
}

static void test_press_cue_says_whether_it_can_be_sent() {
  CueScheduler s = scheduler();
  CueInputs in = quiet();
  in.pressEdge = true;
  TEST_ASSERT_TRUE(s.decide(in, 1000).start == Cue::Press);
  in.canSend = false;
  TEST_ASSERT_TRUE(s.decide(in, 2000).start == Cue::PressOffline);
}

// The patient's own input never waits: it cuts whatever is sounding.
static void test_patient_input_cuts_in_and_leaves_news_pending() {
  CueScheduler s = scheduler();
  CueInputs in = quiet();
  in.playerIdle = false;
  in.pending = Cue::LightSent;
  in.holdFired = true;
  CueDecision d = s.decide(in, 1000);
  TEST_ASSERT_TRUE(d.start == Cue::EmergencyArmed);
  TEST_ASSERT_FALSE(d.pendingTaken);
}

// News from the loop task waits for silence instead of being dropped.
static void test_news_waits_for_silence() {
  CueScheduler s = scheduler();
  CueInputs in = quiet();
  in.pending = Cue::LightSent;
  in.playerIdle = false;
  CueDecision d = s.decide(in, 1000);
  TEST_ASSERT_TRUE(d.start == Cue::None);
  TEST_ASSERT_FALSE(d.pendingTaken);
  in.playerIdle = true;
  d = s.decide(in, 1300);
  TEST_ASSERT_TRUE(d.start == Cue::LightSent);
  TEST_ASSERT_TRUE(d.pendingTaken);
}

// A press during the "confirmed" melody must not cost the patient that news.
static void test_news_cut_by_a_press_plays_again() {
  CueScheduler s = scheduler();
  CueInputs in = quiet();
  in.pending = Cue::Confirmed;
  TEST_ASSERT_TRUE(s.decide(in, 1000).start == Cue::Confirmed);
  in = quiet();
  in.playerIdle = false;  // the melody is still sounding
  in.pressEdge = true;
  TEST_ASSERT_TRUE(s.decide(in, 1500).start == Cue::Press);
  in = quiet();
  in.playerIdle = false;  // the press beep
  TEST_ASSERT_TRUE(s.decide(in, 1600).start == Cue::None);
  in.playerIdle = true;
  TEST_ASSERT_TRUE(s.decide(in, 1800).start == Cue::Confirmed);
  TEST_ASSERT_TRUE(s.decide(in, 1810).start == Cue::None);  // once only
}

// News that already finished is not replayed by a later press.
static void test_finished_news_is_not_replayed() {
  CueScheduler s = scheduler();
  CueInputs in = quiet();
  in.pending = Cue::LightSent;
  TEST_ASSERT_TRUE(s.decide(in, 1000).start == Cue::LightSent);
  in = quiet();  // idle again: the cue ended
  in.pressEdge = true;
  TEST_ASSERT_TRUE(s.decide(in, 5000).start == Cue::Press);
  in = quiet();
  TEST_ASSERT_TRUE(s.decide(in, 5400).start == Cue::None);
}

// Reminders are not news: one cut by a press just comes back on schedule.
static void test_cut_reminder_is_not_replayed() {
  CueScheduler s = scheduler();
  CueInputs in = quiet();
  in.reminder = Cue::EmergencyWaiting;
  TEST_ASSERT_TRUE(s.decide(in, 10000).start == Cue::EmergencyWaiting);
  in.playerIdle = false;
  in.pressEdge = true;
  TEST_ASSERT_TRUE(s.decide(in, 10200).start == Cue::Press);
  in.pressEdge = false;
  in.playerIdle = true;
  TEST_ASSERT_TRUE(s.decide(in, 10600).start == Cue::None);
  TEST_ASSERT_TRUE(s.decide(in, 15200).start == Cue::EmergencyWaiting);
}

static void test_reminder_waits_a_quiet_interval_after_the_armed_cue() {
  CueScheduler s = scheduler();
  CueInputs in = quiet();
  in.holdFired = true;
  TEST_ASSERT_TRUE(s.decide(in, 10000).start == Cue::EmergencyArmed);
  in = quiet();
  in.reminder = Cue::EmergencyWaiting;
  TEST_ASSERT_TRUE(s.decide(in, 10010).start == Cue::None);
  TEST_ASSERT_TRUE(s.decide(in, 14999).start == Cue::None);
  TEST_ASSERT_TRUE(s.decide(in, 15000).start == Cue::EmergencyWaiting);
  TEST_ASSERT_TRUE(s.decide(in, 19999).start == Cue::None);
  TEST_ASSERT_TRUE(s.decide(in, 20000).start == Cue::EmergencyWaiting);
}

static void test_reminder_never_cuts_a_cue() {
  CueScheduler s = scheduler();
  CueInputs in = quiet();
  in.reminder = Cue::EmergencyWaiting;
  in.playerIdle = false;
  TEST_ASSERT_TRUE(s.decide(in, 100000).start == Cue::None);
  in.playerIdle = true;
  TEST_ASSERT_TRUE(s.decide(in, 100010).start == Cue::EmergencyWaiting);
}

// A night-long emergency must not chirp every 5 s for hours.
static void test_reminder_slows_down_after_two_minutes() {
  CueScheduler s = scheduler();
  CueInputs in = quiet();
  in.reminder = Cue::Failed;
  uint32_t start = 50000;
  TEST_ASSERT_TRUE(s.decide(in, start).start == Cue::Failed);  // long silence before
  uint32_t t = start;
  for (; t < start + 120000; t += 5000) s.decide(in, t);
  // Past 120 s: the next one comes 30 s after the last, not 5 s.
  uint32_t last = t - 5000;
  TEST_ASSERT_TRUE(s.decide(in, last + 29999).start == Cue::None);
  TEST_ASSERT_TRUE(s.decide(in, last + 30000).start == Cue::Failed);
}

// A new emergency starts fast again even if the previous one had slowed down.
static void test_reminder_speeds_up_again_for_a_new_emergency() {
  CueScheduler s = scheduler();
  CueInputs in = quiet();
  in.reminder = Cue::EmergencyWaiting;
  for (uint32_t t = 0; t <= 300000; t += 1000) s.decide(in, t);
  in.reminder = Cue::None;
  s.decide(in, 301000);
  in.reminder = Cue::EmergencyWaiting;
  uint32_t fires[2] = {0, 0};
  size_t n = 0;
  for (uint32_t t = 302000; t < 302000 + 60000 && n < 2; t += 1000) {
    if (s.decide(in, t).start != Cue::None) fires[n++] = t;
  }
  TEST_ASSERT_EQUAL_UINT32(2, n);
  TEST_ASSERT_EQUAL_UINT32(5000, fires[1] - fires[0]);
}

static void test_scheduler_wraparound() {
  CueScheduler s = scheduler();
  CueInputs in = quiet();
  in.pressEdge = true;
  uint32_t t = 0xFFFFF000u;
  TEST_ASSERT_TRUE(s.decide(in, t).start == Cue::Press);
  in = quiet();
  in.reminder = Cue::EmergencyWaiting;
  TEST_ASSERT_TRUE(s.decide(in, (uint32_t)(t + 4999)).start == Cue::None);
  TEST_ASSERT_TRUE(s.decide(in, (uint32_t)(t + 5000)).start == Cue::EmergencyWaiting);
}

// Pressing during an emergency must not push the reminder back: it is the only
// thing that says whether the emergency is getting out.
static void test_presses_do_not_postpone_the_reminder() {
  CueScheduler s = scheduler();
  CueInputs in = quiet();
  in.reminder = Cue::Failed;
  TEST_ASSERT_TRUE(s.decide(in, 10000).start == Cue::Failed);
  for (uint32_t t = 11000; t < 15000; t += 1000) {
    in.pressEdge = true;
    TEST_ASSERT_TRUE(s.decide(in, t).start == Cue::Press);
    in.pressEdge = false;
    TEST_ASSERT_TRUE(s.decide(in, t + 10).start == Cue::None);  // the press beep
  }
  TEST_ASSERT_TRUE(s.decide(in, 15000).start == Cue::Failed);
}

// A Hold announces an emergency: whatever news it cut is no longer the latest
// word and must not play right after the "armed" warble.
static void test_news_cut_by_a_hold_is_not_replayed() {
  CueScheduler s = scheduler();
  CueInputs in = quiet();
  in.pending = Cue::Confirmed;
  TEST_ASSERT_TRUE(s.decide(in, 1000).start == Cue::Confirmed);
  in = quiet();
  in.playerIdle = false;
  in.holdFired = true;
  TEST_ASSERT_TRUE(s.decide(in, 1500).start == Cue::EmergencyArmed);
  in = quiet();
  TEST_ASSERT_TRUE(s.decide(in, 2400).start == Cue::None);
}

// An emergency raised without a Hold (a second tap within 60 s) is announced
// as soon as the loop reports it, cutting whatever is sounding; the reminder
// then waits its interval.
static void test_emergency_without_a_hold_is_announced() {
  CueScheduler s = scheduler();
  CueInputs in = quiet();
  in.pressEdge = true;
  TEST_ASSERT_TRUE(s.decide(in, 1000).start == Cue::Press);
  in = quiet();
  in.playerIdle = false;
  in.reminder = Cue::EmergencyWaiting;
  in.emergencyRaised = true;
  TEST_ASSERT_TRUE(s.decide(in, 1100).start == Cue::EmergencyArmed);
  in.emergencyRaised = false;
  in.playerIdle = true;
  TEST_ASSERT_TRUE(s.decide(in, 2000).start == Cue::None);
  TEST_ASSERT_TRUE(s.decide(in, 6100).start == Cue::EmergencyWaiting);
}

// News a press cut short is dropped once an emergency starts: "help is coming"
// or "request sent" would contradict it.
static void test_emergency_start_drops_news_waiting_to_replay() {
  CueScheduler s = scheduler();
  CueInputs in = quiet();
  in.pending = Cue::LightSent;
  TEST_ASSERT_TRUE(s.decide(in, 1000).start == Cue::LightSent);
  in = quiet();
  in.playerIdle = false;
  in.pressEdge = true;
  TEST_ASSERT_TRUE(s.decide(in, 1100).start == Cue::Press);
  in = quiet();
  in.playerIdle = false;
  in.reminder = Cue::EmergencyWaiting;
  in.emergencyRaised = true;  // the second tap upgraded
  TEST_ASSERT_TRUE(s.decide(in, 1200).start == Cue::EmergencyArmed);
  in.emergencyRaised = false;
  in.playerIdle = true;
  TEST_ASSERT_TRUE(s.decide(in, 2000).start == Cue::None);
}

// News still queued when an emergency starts is stale too: "help is coming"
// must not follow "emergency armed".
static void test_emergency_start_drops_queued_news() {
  CueScheduler s = scheduler();
  CueInputs in = quiet();
  in.playerIdle = false;
  in.pending = Cue::Confirmed;
  in.holdFired = true;
  CueDecision d = s.decide(in, 1000);
  TEST_ASSERT_TRUE(d.start == Cue::EmergencyArmed);
  TEST_ASSERT_TRUE(d.dropPending);
  in = quiet();
  in.pressEdge = true;
  TEST_ASSERT_FALSE(s.decide(in, 3000).dropPending);
}

// The "armed" warble is the one proof the emergency started: a press that cuts
// it short gets it played again.
static void test_armed_cue_cut_by_a_press_plays_again() {
  CueScheduler s = scheduler();
  CueInputs in = quiet();
  in.emergencyRaised = true;
  in.reminder = Cue::EmergencyWaiting;
  TEST_ASSERT_TRUE(s.decide(in, 1000).start == Cue::EmergencyArmed);
  in.emergencyRaised = false;
  in.playerIdle = false;
  in.pressEdge = true;
  TEST_ASSERT_TRUE(s.decide(in, 1100).start == Cue::Press);
  in.pressEdge = false;
  in.playerIdle = true;
  TEST_ASSERT_TRUE(s.decide(in, 1400).start == Cue::EmergencyArmed);
}

// The engine sees the player's state after advancing it: a press on the first
// tick after a melody ended must not replay the finished melody.
static void test_engine_does_not_replay_news_that_just_ended() {
  CueEngine e(5000, 120000, 30000, 600000, 120000, 30000);
  CueInputs in = quiet();
  in.pending = Cue::Confirmed;
  CueDecision taken;
  TEST_ASSERT_EQUAL_UINT16(1000, e.tick(in, 0, taken));
  TEST_ASSERT_TRUE(taken.pendingTaken);
  in = quiet();
  TEST_ASSERT_EQUAL_UINT16(1700, e.tick(in, 1500, taken));  // last step, ends at 1680
  in.pressEdge = true;
  TEST_ASSERT_EQUAL_UINT16(1500, e.tick(in, 1690, taken));  // Press
  in.pressEdge = false;
  TEST_ASSERT_EQUAL_UINT16(0, e.tick(in, 1990, taken));
  TEST_ASSERT_EQUAL_UINT16(0, e.tick(in, 2000, taken));  // nothing replayed
}

// Several news cues posted while the buzzer is busy are all kept, the most
// important first.
static void test_pending_news_is_kept_and_ordered() {
  uint16_t mask = 0;
  mask |= cueBit(Cue::LightSent);
  mask |= cueBit(Cue::Confirmed);
  TEST_ASSERT_TRUE(firstPending(mask) == Cue::Confirmed);
  mask &= static_cast<uint16_t>(~cueBit(Cue::Confirmed));
  TEST_ASSERT_TRUE(firstPending(mask) == Cue::LightSent);
  mask &= static_cast<uint16_t>(~cueBit(Cue::LightSent));
  TEST_ASSERT_TRUE(firstPending(mask) == Cue::None);
}

// --- Muting and thinning the emergency reminder ---

static CueInputs live(Cue reminder = Cue::EmergencyWaiting, uint8_t serial = 1) {
  CueInputs in = quiet();
  in.reminder = reminder;
  in.emergencySerial = serial;
  return in;
}

// Ticks every 100 ms over [from, to) with the buzzer idle; returns the start
// time of every cue started.
static std::vector<uint32_t> run(CueScheduler& s, const CueInputs& in, uint32_t from, uint32_t to) {
  std::vector<uint32_t> starts;
  for (uint32_t t = from; t < to; t += 100) {
    if (s.decide(in, t).start != Cue::None) starts.push_back(t);
  }
  return starts;
}

static Cue pressAt(CueScheduler& s, CueInputs in, uint32_t t) {
  in.pressEdge = true;
  return s.decide(in, t).start;
}

static CueDecision holdAt(CueScheduler& s, CueInputs in, uint32_t t) {
  in.holdFired = true;
  return s.decide(in, t);
}

// Emergency live from 0, muted by a hold pressed at 40 000 (past the 30 s grace).
static void muteAt42500(CueScheduler& s) {
  run(s, live(), 0, 40000);
  TEST_ASSERT_TRUE(pressAt(s, live(), 40000) == Cue::Press);
  CueDecision d = holdAt(s, live(), 42500);
  TEST_ASSERT_TRUE(d.start == Cue::Muted);
  TEST_ASSERT_FALSE(d.dropPending);
}

static void test_hold_mutes_after_grace() {
  CueScheduler s = scheduler();
  muteAt42500(s);
  TEST_ASSERT_EQUAL(0, run(s, live(), 42600, 200000).size());
}

static void test_second_hold_unmutes() {
  CueScheduler s = scheduler();
  muteAt42500(s);
  TEST_ASSERT_EQUAL(0, run(s, live(), 42600, 50000).size());
  pressAt(s, live(), 50000);
  TEST_ASSERT_TRUE(holdAt(s, live(), 52500).start == Cue::EmergencyWaiting);
  TEST_ASSERT_EQUAL(0, run(s, live(), 52600, 57500).size());
  TEST_ASSERT_TRUE(s.decide(live(), 57500).start == Cue::EmergencyWaiting);
}

// Unmuting does not restart the emergency's age: 300 s in, the rhythm is 30 s.
static void test_unmute_keeps_the_phase() {
  CueScheduler s = scheduler();
  muteAt42500(s);
  run(s, live(), 42600, 297500);
  pressAt(s, live(), 297500);
  TEST_ASSERT_TRUE(holdAt(s, live(), 300000).start == Cue::EmergencyWaiting);
  TEST_ASSERT_EQUAL(0, run(s, live(), 300100, 330000).size());
  TEST_ASSERT_TRUE(s.decide(live(), 330000).start == Cue::EmergencyWaiting);
}

// An anxious second hold right after "armed" must not silence the emergency.
static void test_hold_within_grace_is_armed() {
  CueScheduler s = scheduler();
  run(s, live(), 0, 10000);
  pressAt(s, live(), 10000);
  TEST_ASSERT_TRUE(holdAt(s, live(), 12500).start == Cue::EmergencyArmed);
}

// A hold whose press began before the buzzer knew of the emergency is not a
// mute (grace shortened to 1 s so only the press rule decides).
static void test_hold_pressed_before_emergency_is_armed() {
  CueScheduler s(5000, 120000, 30000, 600000, 120000, 1000);
  TEST_ASSERT_TRUE(pressAt(s, quiet(), 40000) == Cue::Press);
  s.decide(live(), 40500);
  TEST_ASSERT_TRUE(holdAt(s, live(), 42500).start == Cue::EmergencyArmed);

  CueScheduler control(5000, 120000, 30000, 600000, 120000, 1000);
  control.decide(live(), 40500);
  pressAt(control, live(), 41000);
  TEST_ASSERT_TRUE(holdAt(control, live(), 43500).start == Cue::Muted);
}

static void test_hold_while_confirmed_queued_is_armed() {
  CueScheduler s = scheduler();
  run(s, live(), 0, 40000);
  pressAt(s, live(), 40000);
  CueInputs in = live();
  in.pending = Cue::Confirmed;
  CueDecision d = holdAt(s, in, 42500);
  TEST_ASSERT_TRUE(d.start == Cue::EmergencyArmed);
  TEST_ASSERT_TRUE(d.dropPending);
}

static void test_hold_while_confirmed_playing_is_armed() {
  CueScheduler s = scheduler();
  run(s, live(), 0, 40000);
  pressAt(s, live(), 40000);
  CueInputs in = live();
  in.pending = Cue::Confirmed;
  TEST_ASSERT_TRUE(s.decide(in, 41000).start == Cue::Confirmed);
  in = live();
  in.playerIdle = false;
  TEST_ASSERT_TRUE(holdAt(s, in, 42500).start == Cue::EmergencyArmed);
}

// A new emergency is never inherited muted, even without a silent tick between.
// The confirmation cut short by the press that starts the hold, waiting to be
// replayed: the emergency is over, so the hold is not a mute.
static void test_hold_while_confirmed_waits_for_replay_is_armed() {
  CueScheduler s = scheduler();
  run(s, live(), 0, 40000);
  CueInputs in = live();
  in.pending = Cue::Confirmed;
  TEST_ASSERT_TRUE(s.decide(in, 41000).start == Cue::Confirmed);
  in = live();
  in.playerIdle = false;  // the melody is sounding
  TEST_ASSERT_TRUE(pressAt(s, in, 41100) == Cue::Press);
  TEST_ASSERT_TRUE(holdAt(s, in, 43600).start == Cue::EmergencyArmed);
}

static void test_new_serial_clears_mute() {
  CueScheduler s = scheduler();
  muteAt42500(s);
  run(s, live(), 42600, 100000);
  TEST_ASSERT_TRUE(s.decide(live(Cue::EmergencyWaiting, 2), 100000).start == Cue::EmergencyWaiting);
  TEST_ASSERT_EQUAL(0, run(s, live(Cue::EmergencyWaiting, 2), 100100, 105000).size());
  TEST_ASSERT_TRUE(s.decide(live(Cue::EmergencyWaiting, 2), 105000).start == Cue::EmergencyWaiting);
}

static void test_end_of_emergency_clears_mute() {
  CueScheduler s = scheduler();
  muteAt42500(s);
  s.decide(quiet(), 50000);
  TEST_ASSERT_TRUE(run(s, live(), 50100, 60000).size() > 0);
}

// Muted, the emergency stops getting out: said once, and only once per mute.
static void test_muted_worse_news_once() {
  CueScheduler s = scheduler();
  muteAt42500(s);
  TEST_ASSERT_TRUE(s.decide(live(Cue::Failed), 50000).start == Cue::Failed);
  TEST_ASSERT_EQUAL(0, run(s, live(Cue::Failed), 50100, 60000).size());
  TEST_ASSERT_EQUAL(0, run(s, live(), 60000, 70000).size());  // the connection flaps
  TEST_ASSERT_EQUAL(0, run(s, live(Cue::Failed), 70000, 200000).size());
}

static void test_muted_better_is_silent() {
  CueScheduler s = scheduler();
  run(s, live(Cue::Failed), 0, 40000);
  pressAt(s, live(Cue::Failed), 40000);
  TEST_ASSERT_TRUE(holdAt(s, live(Cue::Failed), 42500).start == Cue::Muted);
  TEST_ASSERT_EQUAL(0, run(s, live(), 42600, 200000).size());
}

static void test_muted_still_beeps_and_hears_news() {
  CueScheduler s = scheduler();
  muteAt42500(s);
  TEST_ASSERT_TRUE(pressAt(s, live(), 50000) == Cue::Press);
  CueInputs in = live();
  in.pending = Cue::Confirmed;
  TEST_ASSERT_TRUE(s.decide(in, 51000).start == Cue::Confirmed);
}

static void test_muted_cue_cut_by_a_press_plays_again() {
  CueScheduler s = scheduler();
  muteAt42500(s);
  CueInputs in = live();
  in.playerIdle = false;  // "muted" still sounding
  TEST_ASSERT_TRUE(pressAt(s, in, 42600) == Cue::Press);
  TEST_ASSERT_TRUE(s.decide(live(), 43000).start == Cue::Muted);
}

static void test_reminder_thins_in_three_phases() {
  CueScheduler s = scheduler();
  const std::vector<uint32_t> t = run(s, live(), 0, 800000);
  auto next = [&](uint32_t after) {
    for (uint32_t x : t) {
      if (x > after) return x;
    }
    return 0u;
  };
  TEST_ASSERT_EQUAL_UINT32(5000, next(0));
  TEST_ASSERT_EQUAL_UINT32(115000, next(110000));
  TEST_ASSERT_EQUAL_UINT32(145000, next(115000));
  TEST_ASSERT_EQUAL_UINT32(595000, next(565000));
  TEST_ASSERT_EQUAL_UINT32(715000, next(595000));
}

static void test_first_pending_includes_muted() {
  uint16_t mask = cueBit(Cue::Muted) | cueBit(Cue::LightSent);
  TEST_ASSERT_TRUE(firstPending(mask) == Cue::LightSent);
  mask &= static_cast<uint16_t>(~cueBit(Cue::LightSent));
  TEST_ASSERT_TRUE(firstPending(mask) == Cue::Muted);
}

static void test_muted_cue_falls() {
  size_t n = 0;
  const ToneStep* st = cueSteps(Cue::Muted, n);
  TEST_ASSERT_EQUAL(2, n);
  TEST_ASSERT_TRUE(st[0].freqHz > st[1].freqHz);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_none_is_silent);
  RUN_TEST(test_single_step_cue_ends_on_time);
  RUN_TEST(test_multi_step_cue_has_a_silent_gap);
  RUN_TEST(test_a_late_tick_does_not_stretch_the_sequence);
  RUN_TEST(test_confirmed_rises_so_it_cannot_be_mistaken);
  RUN_TEST(test_failure_is_lower_than_every_confirmation);
  RUN_TEST(test_every_step_lasts_long_enough_to_hear);
  RUN_TEST(test_no_tone_is_pitched_out_of_reach);
  RUN_TEST(test_the_two_alerts_warble);
  RUN_TEST(test_start_preempts_whatever_was_playing);
  RUN_TEST(test_wraparound);
  RUN_TEST(test_press_cue_says_whether_it_can_be_sent);
  RUN_TEST(test_patient_input_cuts_in_and_leaves_news_pending);
  RUN_TEST(test_news_waits_for_silence);
  RUN_TEST(test_news_cut_by_a_press_plays_again);
  RUN_TEST(test_finished_news_is_not_replayed);
  RUN_TEST(test_cut_reminder_is_not_replayed);
  RUN_TEST(test_reminder_waits_a_quiet_interval_after_the_armed_cue);
  RUN_TEST(test_reminder_never_cuts_a_cue);
  RUN_TEST(test_reminder_slows_down_after_two_minutes);
  RUN_TEST(test_reminder_speeds_up_again_for_a_new_emergency);
  RUN_TEST(test_scheduler_wraparound);
  RUN_TEST(test_presses_do_not_postpone_the_reminder);
  RUN_TEST(test_news_cut_by_a_hold_is_not_replayed);
  RUN_TEST(test_emergency_without_a_hold_is_announced);
  RUN_TEST(test_emergency_start_drops_news_waiting_to_replay);
  RUN_TEST(test_engine_does_not_replay_news_that_just_ended);
  RUN_TEST(test_emergency_start_drops_queued_news);
  RUN_TEST(test_armed_cue_cut_by_a_press_plays_again);
  RUN_TEST(test_pending_news_is_kept_and_ordered);
  RUN_TEST(test_hold_mutes_after_grace);
  RUN_TEST(test_second_hold_unmutes);
  RUN_TEST(test_unmute_keeps_the_phase);
  RUN_TEST(test_hold_within_grace_is_armed);
  RUN_TEST(test_hold_pressed_before_emergency_is_armed);
  RUN_TEST(test_hold_while_confirmed_queued_is_armed);
  RUN_TEST(test_hold_while_confirmed_playing_is_armed);
  RUN_TEST(test_hold_while_confirmed_waits_for_replay_is_armed);
  RUN_TEST(test_new_serial_clears_mute);
  RUN_TEST(test_end_of_emergency_clears_mute);
  RUN_TEST(test_muted_worse_news_once);
  RUN_TEST(test_muted_better_is_silent);
  RUN_TEST(test_muted_still_beeps_and_hears_news);
  RUN_TEST(test_muted_cue_cut_by_a_press_plays_again);
  RUN_TEST(test_reminder_thins_in_three_phases);
  RUN_TEST(test_first_pending_includes_muted);
  RUN_TEST(test_muted_cue_falls);
  return UNITY_END();
}
