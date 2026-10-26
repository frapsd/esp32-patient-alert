#include <unity.h>
#include <vector>
#include "alert_types.h"
#include "button_interpreter.h"

void setUp() {}
void tearDown() {}

struct Ev { Gesture g; uint32_t t; };

// Feeds samples every 10 ms in [fromMs, toMs) (unsigned arithmetic, wrap-safe).
static std::vector<Ev> feed(ButtonInterpreter& b, bool pressed, uint32_t fromMs, uint32_t toMs) {
  std::vector<Ev> out;
  for (uint32_t t = fromMs; (uint32_t)(t - fromMs) < (uint32_t)(toMs - fromMs); t += 10) {
    if (auto g = b.update(pressed, t)) out.push_back({*g, t});
  }
  return out;
}

static void append(std::vector<Ev>& a, const std::vector<Ev>& b) { a.insert(a.end(), b.begin(), b.end()); }

static void test_tap_emits_on_release() {
  ButtonInterpreter b(30, 1500);
  TEST_ASSERT_EQUAL(0, feed(b, false, 0, 100).size());
  TEST_ASSERT_EQUAL(0, feed(b, true, 100, 300).size());  // none before release
  auto r = feed(b, false, 300, 500);
  TEST_ASSERT_EQUAL(1, r.size());
  TEST_ASSERT_TRUE(r[0].g == Gesture::Tap);
  TEST_ASSERT_TRUE(r[0].t >= 300);
}

static void test_hold_emits_at_threshold_without_release() {
  ButtonInterpreter b(30, 1500);
  feed(b, false, 0, 100);
  // pressed from 100; accepted at 130 (stable for 30 ms)
  auto r = feed(b, true, 100, 5000);
  TEST_ASSERT_EQUAL(1, r.size());
  TEST_ASSERT_TRUE(r[0].g == Gesture::Hold);
  TEST_ASSERT_TRUE(r[0].t >= 130 + 1500 && r[0].t <= 130 + 1510);
}

static void test_release_after_hold_emits_nothing() {
  ButtonInterpreter b(30, 1500);
  feed(b, false, 0, 100);
  auto r = feed(b, true, 100, 3100);
  append(r, feed(b, false, 3100, 3500));
  TEST_ASSERT_EQUAL(1, r.size());
  TEST_ASSERT_TRUE(r[0].g == Gesture::Hold);
}

static void test_bounce_shorter_than_debounce_is_ignored() {
  ButtonInterpreter b(30, 1500);
  feed(b, false, 0, 100);
  auto r = feed(b, true, 100, 120);
  append(r, feed(b, false, 120, 500));
  TEST_ASSERT_EQUAL(0, r.size());
}

static void test_bouncy_press_counts_once() {
  ButtonInterpreter b(30, 1500);
  feed(b, false, 0, 100);
  std::vector<Ev> r;
  for (uint32_t t = 100; t < 140; t += 10) append(r, feed(b, ((t - 100) / 10) % 2 == 0, t, t + 10));
  append(r, feed(b, true, 140, 340));
  append(r, feed(b, false, 340, 600));
  TEST_ASSERT_EQUAL(1, r.size());
  TEST_ASSERT_TRUE(r[0].g == Gesture::Tap);
}

static void test_wraparound_tap() {
  ButtonInterpreter b(30, 1500);
  uint32_t t0 = 0xFFFFFF00u;
  feed(b, false, t0, t0 + 100);
  auto r = feed(b, true, t0 + 100, t0 + 300);
  append(r, feed(b, false, t0 + 300, t0 + 600));
  TEST_ASSERT_EQUAL(1, r.size());
  TEST_ASSERT_TRUE(r[0].g == Gesture::Tap);
}

static void test_two_taps_two_gestures() {
  ButtonInterpreter b(30, 1500);
  feed(b, false, 0, 100);
  auto r = feed(b, true, 100, 300);
  append(r, feed(b, false, 300, 600));
  append(r, feed(b, true, 600, 800));
  append(r, feed(b, false, 800, 1100));
  TEST_ASSERT_EQUAL(2, r.size());
  TEST_ASSERT_TRUE(r[0].g == Gesture::Tap);
  TEST_ASSERT_TRUE(r[1].g == Gesture::Tap);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_tap_emits_on_release);
  RUN_TEST(test_hold_emits_at_threshold_without_release);
  RUN_TEST(test_release_after_hold_emits_nothing);
  RUN_TEST(test_bounce_shorter_than_debounce_is_ignored);
  RUN_TEST(test_bouncy_press_counts_once);
  RUN_TEST(test_wraparound_tap);
  RUN_TEST(test_two_taps_two_gestures);
  return UNITY_END();
}
