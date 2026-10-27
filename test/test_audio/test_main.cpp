#include <unity.h>
#include <string.h>
#include "alert_machine.h"
#include "audio_policy.h"

void setUp() {}
void tearDown() {}

static const MachineConfig kCfg = {60000, 600, 10000, 5000, 300000, 3000};
static const uint32_t kEpoch = 1761480000;

static SendResponse sendResult(HttpOutcome o, const char* receipt) {
  SendResponse r;
  r.outcome = o;
  memset(r.receipt, 0, sizeof(r.receipt));
  strncpy(r.receipt, receipt, sizeof(r.receipt) - 1);
  return r;
}

static void test_idle_press_can_be_sent_only_online() {
  AlertMachine m(kCfg);
  TEST_ASSERT_TRUE(audioState(m, true).canSend);
  TEST_ASSERT_TRUE(audioState(m, true).reminder == Cue::None);
  TEST_ASSERT_FALSE(audioState(m, false).canSend);
}

static void test_fresh_emergency_reminds_that_it_is_waiting() {
  AlertMachine m(kCfg);
  m.onGesture(Gesture::Hold, 0, 0, kEpoch);
  TEST_ASSERT_TRUE(audioState(m, true).reminder == Cue::EmergencyWaiting);
  TEST_ASSERT_TRUE(audioState(m, true).canSend);
  TEST_ASSERT_TRUE(audioState(m, false).reminder == Cue::Failed);
}

// Wi-Fi up, internet down: the send fails. Nothing the patient hears may say
// it is going out, the press beep included.
static void test_failing_emergency_says_so_on_every_cue() {
  AlertMachine m(kCfg);
  m.onGesture(Gesture::Hold, 0, 0, kEpoch);
  m.nextCommand(0);
  m.onEmergencySent(sendResult(HttpOutcome::Transient, ""), 0);
  AudioState a = audioState(m, true);
  TEST_ASSERT_TRUE(a.reminder == Cue::Failed);
  TEST_ASSERT_FALSE(a.canSend);
}

static void test_delivered_emergency_keeps_waiting_reminder() {
  AlertMachine m(kCfg);
  m.onGesture(Gesture::Hold, 0, 0, kEpoch);
  m.nextCommand(0);
  m.onEmergencySent(sendResult(HttpOutcome::Ok, "r1"), 0);
  // Pushover holds it and alerts the phones on its own, even if Wi-Fi drops.
  TEST_ASSERT_TRUE(audioState(m, false).reminder == Cue::EmergencyWaiting);
}

static void test_rejected_configuration_cannot_send() {
  AlertMachine m(kCfg);
  m.onGesture(Gesture::Tap, 0, 0, kEpoch);
  m.nextCommand(0);
  m.onLightSent(HttpOutcome::Rejected, 0);
  TEST_ASSERT_FALSE(audioState(m, true).canSend);
  TEST_ASSERT_TRUE(audioState(m, true).reminder == Cue::None);
}

// Internet down behind the Wi-Fi shows up first as a failing light request:
// the press beep must not say it can get out.
static void test_failing_light_request_cannot_send() {
  AlertMachine m(kCfg);
  m.onGesture(Gesture::Tap, 0, 0, kEpoch);
  m.nextCommand(0);
  m.onLightSent(HttpOutcome::Transient, 0);
  TEST_ASSERT_FALSE(audioState(m, true).canSend);
}

// During a live emergency the press beep agrees with the reminder: Pushover
// already holds this one, so the low "not getting out" beep would be false.
static void test_delivered_emergency_press_beep_agrees_with_reminder() {
  AlertMachine m(kCfg);
  m.onGesture(Gesture::Hold, 0, 0, kEpoch);
  m.nextCommand(0);
  m.onEmergencySent(sendResult(HttpOutcome::Ok, "r1"), 0);
  TEST_ASSERT_TRUE(audioState(m, false).canSend);
}

// Requests were already failing when the patient held: the new emergency must
// not sound like it is getting out before its own first send fails.
static void test_emergency_after_failing_requests_starts_as_failed() {
  AlertMachine m(kCfg);
  m.onGesture(Gesture::Tap, 0, 0, kEpoch);
  m.nextCommand(0);
  m.onLightSent(HttpOutcome::Transient, 0);
  m.onGesture(Gesture::Hold, 70000, 70000, kEpoch + 70);
  AudioState a = audioState(m, true);
  TEST_ASSERT_TRUE(a.reminder == Cue::Failed);
  TEST_ASSERT_FALSE(a.canSend);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_press_can_be_sent_only_online);
  RUN_TEST(test_fresh_emergency_reminds_that_it_is_waiting);
  RUN_TEST(test_failing_emergency_says_so_on_every_cue);
  RUN_TEST(test_delivered_emergency_keeps_waiting_reminder);
  RUN_TEST(test_rejected_configuration_cannot_send);
  RUN_TEST(test_failing_light_request_cannot_send);
  RUN_TEST(test_delivered_emergency_press_beep_agrees_with_reminder);
  RUN_TEST(test_emergency_after_failing_requests_starts_as_failed);
  return UNITY_END();
}
