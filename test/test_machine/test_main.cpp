#include <unity.h>
#include <string.h>
#include "alert_machine.h"
#include "alert_types.h"
#include "persisted_state.h"
#include "pushover_protocol.h"

void setUp() {}
void tearDown() {}

// Values from Global Constraints: upgrade window 60 000 ms, confirmed window
// 600 s, poll 10 000 ms, transient retry 5 000 ms, rejected retry 300 000 ms,
// LightSent shown 3 000 ms.
static const MachineConfig kCfg = {60000, 600, 10000, 5000, 300000, 3000};
static const uint32_t kEpoch = 1761480000;  // 2025-10-26, a valid wall time

#define ASSERT_KIND(expected, actual) TEST_ASSERT_EQUAL_INT((int)(expected), (int)(actual))
#define ASSERT_MODE(expected, actual) TEST_ASSERT_EQUAL_INT((int)(expected), (int)(actual))
#define ASSERT_LED(expected, actual) TEST_ASSERT_EQUAL_INT((int)(expected), (int)(actual))

static CommandKind drain(AlertMachine& m, uint32_t now) { return m.nextCommand(now).kind; }

static SendResponse sendResult(HttpOutcome o, const char* receipt) {
  SendResponse r;
  r.outcome = o;
  memset(r.receipt, 0, sizeof(r.receipt));
  strncpy(r.receipt, receipt, sizeof(r.receipt) - 1);
  return r;
}

static ReceiptStatus receiptResult(HttpOutcome o, bool acked, bool expired, const char* device) {
  ReceiptStatus r;
  r.outcome = o;
  r.acknowledged = acked;
  r.expired = expired;
  memset(r.ackDevice, 0, sizeof(r.ackDevice));
  strncpy(r.ackDevice, device, sizeof(r.ackDevice) - 1);
  return r;
}

// Hold at t, send Ok with receipt r: machine is EmergencyAwaitingAck, poll due at t + 10 000.
static void toAwaitingAck(AlertMachine& m, uint32_t t, const char* r) {
  m.onGesture(Gesture::Hold, t, t, kEpoch);
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, t));
  m.onEmergencySent(sendResult(HttpOutcome::Ok, r), t);
  ASSERT_MODE(Mode::EmergencyAwaitingAck, m.mode());
}

// From t: emergency, acked at t + 10 000 by `device`. Ack notice is queued, not sent.
static uint32_t toConfirmed(AlertMachine& m, uint32_t t, const char* device) {
  toAwaitingAck(m, t, "r1");
  uint32_t ackAt = t + 10000;
  ASSERT_KIND(CommandKind::PollReceipt, drain(m, ackAt));
  m.onReceipt(receiptResult(HttpOutcome::Ok, true, false, device), ackAt, kEpoch + 10);
  ASSERT_MODE(Mode::Confirmed, m.mode());
  m.takeDirty();
  return ackAt;
}

static void test_tap_queues_light_request() {
  AlertMachine m(kCfg);
  m.onGesture(Gesture::Tap, 1000, 1000, kEpoch);
  Command c = m.nextCommand(1000);
  ASSERT_KIND(CommandKind::SendLight, c.kind);
  TEST_ASSERT_EQUAL_UINT32(kEpoch, c.pressEpoch);
  ASSERT_MODE(Mode::Idle, m.mode());
  m.onLightSent(HttpOutcome::Ok, 1200);
  ASSERT_KIND(CommandKind::None, drain(m, 1200));
  ASSERT_LED(LedPattern::LightSent, m.led(1200, true));
  ASSERT_LED(LedPattern::LightSent, m.led(4199, true));
  ASSERT_LED(LedPattern::Idle, m.led(4200, true));
  TEST_ASSERT_FALSE(m.takeDirty());
}

static void test_hold_triggers_emergency() {
  AlertMachine m(kCfg);
  m.onGesture(Gesture::Hold, 0, 0, kEpoch);
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, 0));
  ASSERT_MODE(Mode::EmergencyPending, m.mode());
  TEST_ASSERT_TRUE(m.takeDirty());
  TEST_ASSERT_FALSE(m.takeDirty());
  ASSERT_LED(LedPattern::EmergencyActive, m.led(0, true));
}

static void test_second_tap_within_60s_upgrades() {
  // First light request still unsent: the upgrade drops it.
  AlertMachine m(kCfg);
  m.onGesture(Gesture::Tap, 0, 0, kEpoch);
  m.onGesture(Gesture::Tap, 59999, 59999, kEpoch + 59);
  ASSERT_MODE(Mode::EmergencyPending, m.mode());
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, 59999));
  m.onEmergencySent(sendResult(HttpOutcome::Ok, "r1"), 59999);
  ASSERT_KIND(CommandKind::None, drain(m, 59999));
  ASSERT_KIND(CommandKind::None, drain(m, 69998));
  ASSERT_KIND(CommandKind::PollReceipt, drain(m, 69999));

  // First light request already delivered: the second press still upgrades.
  AlertMachine m2(kCfg);
  m2.onGesture(Gesture::Tap, 0, 0, kEpoch);
  ASSERT_KIND(CommandKind::SendLight, drain(m2, 0));
  m2.onLightSent(HttpOutcome::Ok, 800);
  m2.onGesture(Gesture::Tap, 59999, 59999, kEpoch + 59);
  ASSERT_KIND(CommandKind::SendEmergency, drain(m2, 59999));
}

static void test_second_tap_after_60s_is_light() {
  AlertMachine m(kCfg);
  m.onGesture(Gesture::Tap, 0, 0, kEpoch);
  Command c = m.nextCommand(0);
  ASSERT_KIND(CommandKind::SendLight, c.kind);
  TEST_ASSERT_EQUAL_UINT32(kEpoch, c.pressEpoch);
  m.onLightSent(HttpOutcome::Ok, 500);
  m.onGesture(Gesture::Tap, 60000, 60000, kEpoch + 60);
  ASSERT_MODE(Mode::Idle, m.mode());
  c = m.nextCommand(60000);
  ASSERT_KIND(CommandKind::SendLight, c.kind);
  TEST_ASSERT_EQUAL_UINT32(kEpoch + 60, c.pressEpoch);
  m.onLightSent(HttpOutcome::Ok, 60500);
  ASSERT_KIND(CommandKind::None, drain(m, 60500));
  TEST_ASSERT_FALSE(m.takeDirty());
}

static void test_mashing_produces_single_emergency() {
  AlertMachine m(kCfg);
  m.onGesture(Gesture::Tap, 0, 0, kEpoch);
  m.onGesture(Gesture::Tap, 500, 500, kEpoch);   // upgrade -> emergency
  m.onGesture(Gesture::Tap, 1000, 1000, kEpoch);  // ignored
  m.onGesture(Gesture::Hold, 1800, 1800, kEpoch); // ignored
  TEST_ASSERT_TRUE(m.takeDirty());
  TEST_ASSERT_FALSE(m.takeDirty());
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, 2000));
  m.onEmergencySent(sendResult(HttpOutcome::Transient, ""), 2000);

  // Gestures while pending neither re-trigger nor pull the retry forward.
  m.onGesture(Gesture::Tap, 2500, 2500, kEpoch);
  m.onGesture(Gesture::Hold, 3000, 3000, kEpoch);
  TEST_ASSERT_FALSE(m.takeDirty());
  ASSERT_KIND(CommandKind::None, drain(m, 6999));
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, 7000));
  m.onEmergencySent(sendResult(HttpOutcome::Ok, "r1"), 7000);
  TEST_ASSERT_TRUE(m.takeDirty());

  // Nothing else ever queued: no second emergency, no light request.
  m.onGesture(Gesture::Tap, 7100, 7100, kEpoch);
  for (uint32_t t = 7000; t < 17000; t += 250) ASSERT_KIND(CommandKind::None, drain(m, t));
  ASSERT_KIND(CommandKind::PollReceipt, drain(m, 17000));
}

static void test_emergency_ack_flow() {
  AlertMachine m(kCfg);
  toAwaitingAck(m, 0, "r1");
  TEST_ASSERT_TRUE(m.takeDirty());
  TEST_ASSERT_EQUAL_STRING("r1", m.persisted().receipt);
  ASSERT_MODE(Mode::EmergencyAwaitingAck, m.persisted().mode);
  ASSERT_KIND(CommandKind::None, drain(m, 9999));
  Command c = m.nextCommand(10000);
  ASSERT_KIND(CommandKind::PollReceipt, c.kind);
  TEST_ASSERT_EQUAL_STRING("r1", c.receipt);

  m.onReceipt(receiptResult(HttpOutcome::Ok, true, false, "pixel-marco"), 10000, kEpoch + 10);
  ASSERT_MODE(Mode::Confirmed, m.mode());
  TEST_ASSERT_TRUE(m.takeDirty());
  ASSERT_MODE(Mode::Confirmed, m.persisted().mode);
  TEST_ASSERT_EQUAL_UINT32(kEpoch + 10, m.persisted().confirmedAtEpoch);
  c = m.nextCommand(10000);
  ASSERT_KIND(CommandKind::SendAckNotice, c.kind);
  TEST_ASSERT_EQUAL_STRING("pixel-marco", c.device);
  m.onAckNoticeSent(HttpOutcome::Ok, 10500);
  ASSERT_KIND(CommandKind::None, drain(m, 10500));
  ASSERT_LED(LedPattern::Confirmed, m.led(10500, true));
  ASSERT_LED(LedPattern::Confirmed, m.led(609999, false));

  // Window over: led() already stops reporting Confirmed, nextCommand transitions.
  ASSERT_LED(LedPattern::Idle, m.led(610000, true));
  ASSERT_MODE(Mode::Confirmed, m.mode());
  ASSERT_KIND(CommandKind::None, drain(m, 609999));
  ASSERT_MODE(Mode::Confirmed, m.mode());
  ASSERT_KIND(CommandKind::None, drain(m, 610000));
  ASSERT_MODE(Mode::Idle, m.mode());
  TEST_ASSERT_TRUE(m.takeDirty());
  ASSERT_MODE(Mode::Idle, m.persisted().mode);
}

static void test_press_during_confirmed_is_emergency() {
  AlertMachine m(kCfg);
  uint32_t t = toConfirmed(m, 0, "pixel-marco");
  ASSERT_KIND(CommandKind::SendAckNotice, drain(m, t));
  m.onAckNoticeSent(HttpOutcome::Ok, t);
  m.onGesture(Gesture::Tap, t + 20000, t + 20000, kEpoch + 30);
  ASSERT_MODE(Mode::EmergencyPending, m.mode());
  TEST_ASSERT_TRUE(m.takeDirty());
  TEST_ASSERT_EQUAL_STRING("", m.persisted().receipt);
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, t + 20000));
  ASSERT_LED(LedPattern::EmergencyActive, m.led(t + 20000, true));
}

static void test_press_during_awaiting_ack_ignored() {
  AlertMachine m(kCfg);
  toAwaitingAck(m, 0, "r1");
  m.takeDirty();
  m.onGesture(Gesture::Tap, 3000, 3000, kEpoch);
  m.onGesture(Gesture::Hold, 4000, 4000, kEpoch);
  m.onGesture(Gesture::Tap, 5000, 5000, kEpoch);
  ASSERT_MODE(Mode::EmergencyAwaitingAck, m.mode());
  TEST_ASSERT_FALSE(m.takeDirty());
  TEST_ASSERT_EQUAL_STRING("r1", m.persisted().receipt);
  ASSERT_KIND(CommandKind::None, drain(m, 9999));
  ASSERT_KIND(CommandKind::PollReceipt, drain(m, 10000));
}

static void test_emergency_survives_repeated_transient_failures() {
  AlertMachine m(kCfg);
  m.onGesture(Gesture::Hold, 0, 0, kEpoch);
  uint32_t t = 0;
  for (int i = 0; i < 5; i++) {
    ASSERT_KIND(CommandKind::SendEmergency, drain(m, t));
    m.onEmergencySent(sendResult(HttpOutcome::Transient, ""), t);
    ASSERT_MODE(Mode::EmergencyPending, m.mode());
    ASSERT_LED(LedPattern::EmergencyActive, m.led(t, false));
    ASSERT_LED(LedPattern::EmergencyActive, m.led(t + 2500, false));
    ASSERT_KIND(CommandKind::None, drain(m, t + 1));
    ASSERT_KIND(CommandKind::None, drain(m, t + 4999));
    t += 5000;
  }
  // Ok with an empty receipt is still a failure.
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, t));
  m.onEmergencySent(sendResult(HttpOutcome::Ok, ""), t);
  ASSERT_MODE(Mode::EmergencyPending, m.mode());
  ASSERT_KIND(CommandKind::None, drain(m, t + 4999));
  t += 5000;
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, t));
  m.onEmergencySent(sendResult(HttpOutcome::Ok, "r1"), t);
  ASSERT_MODE(Mode::EmergencyAwaitingAck, m.mode());
  ASSERT_LED(LedPattern::EmergencyActive, m.led(t, false));
}

static void test_rejected_send_waits_300s_and_shows_config_error() {
  AlertMachine m(kCfg);
  m.onGesture(Gesture::Hold, 0, 0, kEpoch);
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, 0));
  m.onEmergencySent(sendResult(HttpOutcome::Rejected, ""), 0);
  ASSERT_MODE(Mode::EmergencyPending, m.mode());
  ASSERT_LED(LedPattern::EmergencyActive, m.led(0, true));
  ASSERT_LED(LedPattern::EmergencyActive, m.led(1000, false));
  ASSERT_KIND(CommandKind::None, drain(m, 5000));
  ASSERT_KIND(CommandKind::None, drain(m, 299999));
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, 300000));
  m.onEmergencySent(sendResult(HttpOutcome::Rejected, ""), 300000);
  ASSERT_KIND(CommandKind::None, drain(m, 599999));
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, 600000));
  m.onEmergencySent(sendResult(HttpOutcome::Ok, "r1"), 600000);

  // Through to Idle; the Ok cleared the error.
  ASSERT_KIND(CommandKind::PollReceipt, drain(m, 610000));
  m.onReceipt(receiptResult(HttpOutcome::Ok, true, false, ""), 610000, kEpoch);
  ASSERT_KIND(CommandKind::SendAckNotice, drain(m, 610000));
  m.onAckNoticeSent(HttpOutcome::Ok, 610000);
  ASSERT_KIND(CommandKind::None, drain(m, 1210000));
  ASSERT_MODE(Mode::Idle, m.mode());
  ASSERT_LED(LedPattern::Idle, m.led(1210000, true));

  // A rejected light request in Idle shows ConfigError (over Offline too).
  const uint32_t t = 1220000;
  m.onGesture(Gesture::Tap, t, t, kEpoch);
  ASSERT_KIND(CommandKind::SendLight, drain(m, t));
  m.onLightSent(HttpOutcome::Rejected, t);
  ASSERT_LED(LedPattern::ConfigError, m.led(t, true));
  ASSERT_LED(LedPattern::ConfigError, m.led(t, false));
  ASSERT_KIND(CommandKind::None, drain(m, t + 299999));
  ASSERT_KIND(CommandKind::SendLight, drain(m, t + 300000));
  m.onLightSent(HttpOutcome::Ok, t + 300000);
  ASSERT_LED(LedPattern::LightSent, m.led(t + 300000, true));
  ASSERT_LED(LedPattern::Idle, m.led(t + 303000, true));
}

static void test_expired_unacked_receipt_resends_emergency() {
  AlertMachine m(kCfg);
  toAwaitingAck(m, 0, "r1");
  m.takeDirty();
  ASSERT_KIND(CommandKind::PollReceipt, drain(m, 10000));
  m.onReceipt(receiptResult(HttpOutcome::Ok, false, true, ""), 10000, kEpoch);
  ASSERT_MODE(Mode::EmergencyPending, m.mode());
  TEST_ASSERT_TRUE(m.takeDirty());
  TEST_ASSERT_EQUAL_STRING("", m.persisted().receipt);
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, 10000));
  ASSERT_LED(LedPattern::EmergencyActive, m.led(10000, true));
}

static void test_receipt_rejected_resends_after_300s() {
  AlertMachine m(kCfg);
  toAwaitingAck(m, 0, "r1");
  m.takeDirty();
  ASSERT_KIND(CommandKind::PollReceipt, drain(m, 10000));
  m.onReceipt(receiptResult(HttpOutcome::Rejected, false, false, ""), 10000, kEpoch);
  ASSERT_MODE(Mode::EmergencyPending, m.mode());
  TEST_ASSERT_TRUE(m.takeDirty());
  ASSERT_LED(LedPattern::EmergencyActive, m.led(10000, true));
  ASSERT_KIND(CommandKind::None, drain(m, 309999));
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, 310000));
}

static void test_receipt_unresolved_and_transient_polling() {
  AlertMachine m(kCfg);
  toAwaitingAck(m, 0, "r1");
  ASSERT_KIND(CommandKind::PollReceipt, drain(m, 10000));
  m.onReceipt(receiptResult(HttpOutcome::Ok, false, false, ""), 10000, kEpoch);
  ASSERT_KIND(CommandKind::None, drain(m, 19999));
  ASSERT_KIND(CommandKind::PollReceipt, drain(m, 20000));
  m.onReceipt(receiptResult(HttpOutcome::Transient, false, false, ""), 20000, kEpoch);
  ASSERT_MODE(Mode::EmergencyAwaitingAck, m.mode());
  ASSERT_KIND(CommandKind::None, drain(m, 24999));
  ASSERT_KIND(CommandKind::PollReceipt, drain(m, 25000));
  // Acknowledged wins over expired.
  m.onReceipt(receiptResult(HttpOutcome::Ok, true, true, "tab"), 25000, kEpoch);
  ASSERT_MODE(Mode::Confirmed, m.mode());
}

static void test_restore_awaiting_ack_resumes_polling() {
  AlertMachine m(kCfg);
  PersistedState s;
  s.mode = Mode::EmergencyAwaitingAck;
  strcpy(s.receipt, "r9");
  m.restore(s, 123456, 0);
  ASSERT_MODE(Mode::EmergencyAwaitingAck, m.mode());
  TEST_ASSERT_FALSE(m.takeDirty());
  Command c = m.nextCommand(123456);
  ASSERT_KIND(CommandKind::PollReceipt, c.kind);
  TEST_ASSERT_EQUAL_STRING("r9", c.receipt);
  ASSERT_LED(LedPattern::EmergencyActive, m.led(123456, false));
}

static void test_restore_pending_sends_now() {
  AlertMachine m(kCfg);
  PersistedState s;
  s.mode = Mode::EmergencyPending;
  m.restore(s, 777, 0);
  ASSERT_MODE(Mode::EmergencyPending, m.mode());
  TEST_ASSERT_FALSE(m.takeDirty());
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, 777));
  ASSERT_LED(LedPattern::EmergencyActive, m.led(777, false));
}

static void test_restore_awaiting_without_receipt_resends() {
  AlertMachine m(kCfg);
  PersistedState s;
  s.mode = Mode::EmergencyAwaitingAck;
  m.restore(s, 50, kEpoch);
  ASSERT_MODE(Mode::EmergencyPending, m.mode());
  TEST_ASSERT_TRUE(m.takeDirty());
  ASSERT_MODE(Mode::EmergencyPending, m.persisted().mode);
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, 50));
}

static void test_restore_confirmed_within_window_keeps_solid_led_for_remaining_time() {
  PersistedState s;
  s.mode = Mode::Confirmed;
  s.confirmedAtEpoch = kEpoch;

  AlertMachine m(kCfg);
  m.restore(s, 1000, kEpoch + 100);
  ASSERT_MODE(Mode::Confirmed, m.mode());
  TEST_ASSERT_FALSE(m.takeDirty());
  ASSERT_LED(LedPattern::Confirmed, m.led(1000, false));
  ASSERT_LED(LedPattern::Confirmed, m.led(500999, true));
  ASSERT_KIND(CommandKind::None, drain(m, 500999));
  ASSERT_MODE(Mode::Confirmed, m.mode());
  ASSERT_LED(LedPattern::Idle, m.led(501000, true));
  ASSERT_KIND(CommandKind::None, drain(m, 501000));
  ASSERT_MODE(Mode::Idle, m.mode());
  TEST_ASSERT_TRUE(m.takeDirty());

  // Back-dating across the millis wrap still ends the window on time.
  AlertMachine w(kCfg);
  w.restore(s, 100, kEpoch + 100);
  ASSERT_MODE(Mode::Confirmed, w.mode());
  w.onGesture(Gesture::Tap, 499999 + 100, 499999 + 100, kEpoch + 599);
  ASSERT_MODE(Mode::EmergencyPending, w.mode());  // still confirmed -> new emergency
  AlertMachine w2(kCfg);
  w2.restore(s, 100, kEpoch + 100);
  w2.onGesture(Gesture::Tap, 500000 + 100, 500000 + 100, kEpoch + 600);
  ASSERT_MODE(Mode::Idle, w2.mode());  // window over -> plain light request
  ASSERT_KIND(CommandKind::SendLight, drain(w2, 500100));
}

static void test_restore_confirmed_unknown_time_stays_confirmed_until_clock() {
  PersistedState s;
  s.mode = Mode::Confirmed;
  s.confirmedAtEpoch = kEpoch;
  AlertMachine m(kCfg);
  m.restore(s, 1000, 0);
  ASSERT_MODE(Mode::Confirmed, m.mode());
  TEST_ASSERT_FALSE(m.takeDirty());  // NVS keeps the original confirmedAtEpoch
  TEST_ASSERT_EQUAL_UINT32(kEpoch, m.persisted().confirmedAtEpoch);
  ASSERT_LED(LedPattern::Confirmed, m.led(1000, false));
  ASSERT_KIND(CommandKind::None, drain(m, 1000));
  m.onWallClock(5000, 0);  // still no wall time: nothing changes
  ASSERT_MODE(Mode::Confirmed, m.mode());
  TEST_ASSERT_FALSE(m.takeDirty());
  // Without a clock the full window runs from the restore.
  ASSERT_LED(LedPattern::Confirmed, m.led(600999, true));
  ASSERT_KIND(CommandKind::None, drain(m, 601000));
  ASSERT_MODE(Mode::Idle, m.mode());
  TEST_ASSERT_TRUE(m.takeDirty());
  m.onWallClock(602000, kEpoch + 100);  // late clock after the expiry: no effect
  ASSERT_MODE(Mode::Idle, m.mode());
  TEST_ASSERT_FALSE(m.takeDirty());

  AlertMachine past(kCfg);  // clock behind the stored time
  past.restore(s, 1000, kEpoch - 1);
  ASSERT_MODE(Mode::Idle, past.mode());
  AlertMachine late(kCfg);  // window already over
  late.restore(s, 1000, kEpoch + 600);
  ASSERT_MODE(Mode::Idle, late.mode());

  AlertMachine idle(kCfg);
  idle.restore(PersistedState(), 1000, 0);
  ASSERT_MODE(Mode::Idle, idle.mode());
  TEST_ASSERT_FALSE(idle.takeDirty());
  idle.onWallClock(2000, kEpoch);
  TEST_ASSERT_FALSE(idle.takeDirty());
  ASSERT_KIND(CommandKind::None, drain(idle, 1000));
}

static void test_wall_clock_resolves_confirmed_remaining_window() {
  PersistedState s;
  s.mode = Mode::Confirmed;
  s.confirmedAtEpoch = kEpoch;
  AlertMachine m(kCfg);
  m.restore(s, 1000, 0);
  // Clock arrives at t=21000: 100 s of the window had passed -> 500 s remain.
  m.onWallClock(21000, kEpoch + 100);
  ASSERT_MODE(Mode::Confirmed, m.mode());
  TEST_ASSERT_FALSE(m.takeDirty());
  ASSERT_LED(LedPattern::Confirmed, m.led(520999, true));
  ASSERT_KIND(CommandKind::None, drain(m, 520999));
  ASSERT_MODE(Mode::Confirmed, m.mode());
  ASSERT_KIND(CommandKind::None, drain(m, 521000));
  ASSERT_MODE(Mode::Idle, m.mode());
  TEST_ASSERT_TRUE(m.takeDirty());

  // Resolved once: a later clock reading does not move the window again.
  AlertMachine r(kCfg);
  r.restore(s, 1000, 0);
  r.onWallClock(21000, kEpoch + 100);
  r.onWallClock(22000, kEpoch + 50);
  ASSERT_KIND(CommandKind::None, drain(r, 521000));
  ASSERT_MODE(Mode::Idle, r.mode());
}

static void test_wall_clock_expires_stale_confirmed() {
  PersistedState s;
  s.mode = Mode::Confirmed;
  s.confirmedAtEpoch = kEpoch;
  AlertMachine m(kCfg);  // power was off longer than the window
  m.restore(s, 1000, 0);
  m.onWallClock(21000, kEpoch + 600);
  ASSERT_MODE(Mode::Idle, m.mode());
  TEST_ASSERT_TRUE(m.takeDirty());
  ASSERT_LED(LedPattern::Idle, m.led(21000, true));
  m.onGesture(Gesture::Tap, 22000, 22000, kEpoch + 601);
  ASSERT_MODE(Mode::Idle, m.mode());  // now a plain light request
  ASSERT_KIND(CommandKind::SendLight, drain(m, 22000));

  AlertMachine back(kCfg);  // clock behind the stored time: cannot trust it
  back.restore(s, 1000, 0);
  back.onWallClock(21000, kEpoch - 1);
  ASSERT_MODE(Mode::Idle, back.mode());
  TEST_ASSERT_TRUE(back.takeDirty());
}

static void test_tap_while_confirmed_unresolved_is_emergency() {
  PersistedState s;
  s.mode = Mode::Confirmed;
  s.confirmedAtEpoch = kEpoch;
  AlertMachine m(kCfg);
  m.restore(s, 1000, 0);
  m.onGesture(Gesture::Tap, 3000, 2500, 0);
  ASSERT_MODE(Mode::EmergencyPending, m.mode());
  TEST_ASSERT_TRUE(m.takeDirty());
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, 3000));
  m.onWallClock(4000, kEpoch + 100);  // late clock must not touch the emergency
  ASSERT_MODE(Mode::EmergencyPending, m.mode());
}

static void test_light_queue_capacity_4() {
  AlertMachine m(kCfg);
  uint32_t t = 0;
  for (uint32_t i = 0; i < 6; i++) {
    t = i * 60000;
    m.onGesture(Gesture::Tap, t, t, kEpoch + i);
    ASSERT_MODE(Mode::Idle, m.mode());
    Command c = m.nextCommand(t);
    ASSERT_KIND(CommandKind::SendLight, c.kind);
    TEST_ASSERT_EQUAL_UINT32(kEpoch, c.pressEpoch);  // head stays first
    m.onLightSent(HttpOutcome::Transient, t);
    ASSERT_KIND(CommandKind::None, drain(m, t + 4999));
  }
  t += 5000;
  for (uint32_t i = 0; i < 4; i++) {
    Command c = m.nextCommand(t);
    ASSERT_KIND(CommandKind::SendLight, c.kind);
    TEST_ASSERT_EQUAL_UINT32(kEpoch + i, c.pressEpoch);
    m.onLightSent(HttpOutcome::Ok, t);
  }
  ASSERT_KIND(CommandKind::None, drain(m, t));
  ASSERT_KIND(CommandKind::None, drain(m, t + 400000));
}

static void test_emergency_has_priority_over_pending_light_and_ack_notice() {
  // Pending light (backing off) is dropped by the emergency.
  AlertMachine a(kCfg);
  a.onGesture(Gesture::Tap, 0, 0, kEpoch);
  ASSERT_KIND(CommandKind::SendLight, drain(a, 0));
  a.onLightSent(HttpOutcome::Transient, 0);
  a.onGesture(Gesture::Hold, 1000, 1000, kEpoch);
  ASSERT_KIND(CommandKind::SendEmergency, drain(a, 6000));
  a.onEmergencySent(sendResult(HttpOutcome::Ok, "r1"), 6000);
  ASSERT_KIND(CommandKind::None, drain(a, 15999));

  // Unsent ack notice waits behind a new emergency, and is kept.
  AlertMachine b(kCfg);
  uint32_t t = toConfirmed(b, 0, "pixel-marco");
  b.onGesture(Gesture::Tap, t + 1000, t + 1000, kEpoch);
  ASSERT_KIND(CommandKind::SendEmergency, drain(b, t + 1000));
  b.onEmergencySent(sendResult(HttpOutcome::Ok, "r2"), t + 1000);
  Command c = b.nextCommand(t + 1000);
  ASSERT_KIND(CommandKind::SendAckNotice, c.kind);
  TEST_ASSERT_EQUAL_STRING("pixel-marco", c.device);
  b.onAckNoticeSent(HttpOutcome::Ok, t + 1000);
  // Poll beats a due ack notice too: a second ack while polling.
  c = b.nextCommand(t + 11000);
  ASSERT_KIND(CommandKind::PollReceipt, c.kind);
  TEST_ASSERT_EQUAL_STRING("r2", c.receipt);

  // Ack notice comes before a light request.
  AlertMachine d(kCfg);
  t = toConfirmed(d, 0, "");
  ASSERT_KIND(CommandKind::SendAckNotice, drain(d, t));
  d.onAckNoticeSent(HttpOutcome::Transient, t);
  d.onGesture(Gesture::Tap, t + 600000, t + 600000, kEpoch);  // window over -> light request
  ASSERT_MODE(Mode::Idle, d.mode());
  c = d.nextCommand(t + 600000);
  ASSERT_KIND(CommandKind::SendAckNotice, c.kind);
  TEST_ASSERT_EQUAL_STRING("", c.device);
  d.onAckNoticeSent(HttpOutcome::Ok, t + 600000);
  ASSERT_KIND(CommandKind::SendLight, drain(d, t + 600000));
}

static void test_wraparound_retry_timing() {
  AlertMachine m(kCfg);
  const uint32_t t = 0xFFFFF000u;  // 4096 ms before millis wraps
  m.onGesture(Gesture::Hold, t, t, kEpoch);
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, t));
  m.onEmergencySent(sendResult(HttpOutcome::Transient, ""), t);
  ASSERT_KIND(CommandKind::None, drain(m, t + 4999));  // wrapped, not yet due
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, t + 5000));
}

// The loop drains gestures late while it is blocked in HTTPS: decisions use the
// press time, not the drain time.
static void test_late_drained_second_tap_still_upgrades() {
  AlertMachine m(kCfg);
  m.onGesture(Gesture::Tap, 0, 0, kEpoch);
  m.onGesture(Gesture::Tap, 80000, 50000, kEpoch + 50);  // pressed at 50 s, drained at 80 s
  ASSERT_MODE(Mode::EmergencyPending, m.mode());
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, 80000));

  // A light request records its press epoch, not the drain-time wall clock.
  AlertMachine l(kCfg);
  l.onGesture(Gesture::Tap, 30000, 1000, kEpoch + 1);
  Command c = l.nextCommand(30000);
  ASSERT_KIND(CommandKind::SendLight, c.kind);
  TEST_ASSERT_EQUAL_UINT32(kEpoch + 1, c.pressEpoch);

  // Pressed 60 s after the light press (drained later): plain light request.
  AlertMachine n(kCfg);
  n.onGesture(Gesture::Tap, 0, 0, kEpoch);
  n.onGesture(Gesture::Tap, 90000, 60000, kEpoch + 60);
  ASSERT_MODE(Mode::Idle, n.mode());
}

static void test_press_inside_confirmed_window_drained_after_expiry_is_emergency() {
  AlertMachine m(kCfg);
  uint32_t t = toConfirmed(m, 0, "phone");
  m.onGesture(Gesture::Tap, t + 620000, t + 599000, kEpoch + 609);
  ASSERT_MODE(Mode::EmergencyPending, m.mode());
  ASSERT_KIND(CommandKind::SendEmergency, drain(m, t + 620000));

  // Pressed just after the window closed, drained later: light request.
  AlertMachine n(kCfg);
  t = toConfirmed(n, 0, "phone");
  n.onGesture(Gesture::Tap, t + 620000, t + 600000, kEpoch + 610);
  ASSERT_MODE(Mode::Idle, n.mode());

  // A press stamped "after" now (clock wrap) counts as pressed now: it must not
  // look like it fell inside an expired window.
  AlertMachine w(kCfg);
  t = toConfirmed(w, 0, "phone");
  w.onGesture(Gesture::Tap, t + 620000, t + 620001, kEpoch + 610);
  ASSERT_MODE(Mode::Idle, w.mode());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_tap_queues_light_request);
  RUN_TEST(test_hold_triggers_emergency);
  RUN_TEST(test_second_tap_within_60s_upgrades);
  RUN_TEST(test_second_tap_after_60s_is_light);
  RUN_TEST(test_mashing_produces_single_emergency);
  RUN_TEST(test_emergency_ack_flow);
  RUN_TEST(test_press_during_confirmed_is_emergency);
  RUN_TEST(test_press_during_awaiting_ack_ignored);
  RUN_TEST(test_emergency_survives_repeated_transient_failures);
  RUN_TEST(test_rejected_send_waits_300s_and_shows_config_error);
  RUN_TEST(test_expired_unacked_receipt_resends_emergency);
  RUN_TEST(test_receipt_rejected_resends_after_300s);
  RUN_TEST(test_receipt_unresolved_and_transient_polling);
  RUN_TEST(test_restore_awaiting_ack_resumes_polling);
  RUN_TEST(test_restore_pending_sends_now);
  RUN_TEST(test_restore_awaiting_without_receipt_resends);
  RUN_TEST(test_restore_confirmed_within_window_keeps_solid_led_for_remaining_time);
  RUN_TEST(test_restore_confirmed_unknown_time_stays_confirmed_until_clock);
  RUN_TEST(test_wall_clock_resolves_confirmed_remaining_window);
  RUN_TEST(test_wall_clock_expires_stale_confirmed);
  RUN_TEST(test_tap_while_confirmed_unresolved_is_emergency);
  RUN_TEST(test_light_queue_capacity_4);
  RUN_TEST(test_emergency_has_priority_over_pending_light_and_ack_notice);
  RUN_TEST(test_wraparound_retry_timing);
  RUN_TEST(test_late_drained_second_tap_still_upgrades);
  RUN_TEST(test_press_inside_confirmed_window_drained_after_expiry_is_emergency);
  return UNITY_END();
}
