#include <unity.h>
#include <cstddef>
#include <initializer_list>
#include "buzzer_cue.h"

void setUp() {}
void tearDown() {}

static constexpr std::initializer_list<Cue> kAllCues = {
    Cue::Press,     Cue::PressOffline, Cue::EmergencyArmed,  Cue::LightSent,
    Cue::Confirmed, Cue::Failed,       Cue::EmergencyWaiting};

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
static CueScheduler scheduler() { return CueScheduler(5000, 120000, 30000); }

static CueInputs quiet() { return CueInputs{}; }

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

static void test_reminder_waits_a_quiet_interval_after_any_cue() {
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
  RUN_TEST(test_reminder_waits_a_quiet_interval_after_any_cue);
  RUN_TEST(test_reminder_never_cuts_a_cue);
  RUN_TEST(test_reminder_slows_down_after_two_minutes);
  RUN_TEST(test_reminder_speeds_up_again_for_a_new_emergency);
  RUN_TEST(test_scheduler_wraparound);
  return UNITY_END();
}
