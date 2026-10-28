#include <unity.h>
#include <initializer_list>
#include "alert_types.h"
#include "led_pattern.h"

void setUp() {}
void tearDown() {}

static void test_idle() {
  TEST_ASSERT_TRUE(ledOn(LedPattern::Idle, 0));
  TEST_ASSERT_TRUE(ledOn(LedPattern::Idle, 49));
  TEST_ASSERT_FALSE(ledOn(LedPattern::Idle, 50));
  TEST_ASSERT_FALSE(ledOn(LedPattern::Idle, 4999));
  TEST_ASSERT_TRUE(ledOn(LedPattern::Idle, 5000));
}

static void test_offline() {
  TEST_ASSERT_TRUE(ledOn(LedPattern::Offline, 0));
  TEST_ASSERT_TRUE(ledOn(LedPattern::Offline, 999));
  TEST_ASSERT_FALSE(ledOn(LedPattern::Offline, 1000));
  TEST_ASSERT_FALSE(ledOn(LedPattern::Offline, 1999));
  TEST_ASSERT_TRUE(ledOn(LedPattern::Offline, 2000));
}

static void test_light_sent_always_on() {
  for (uint32_t t : {0u, 1u, 999u, 3000u, 123457u})
    TEST_ASSERT_TRUE(ledOn(LedPattern::LightSent, t));
}

static void test_emergency_active() {
  TEST_ASSERT_TRUE(ledOn(LedPattern::EmergencyActive, 0));
  TEST_ASSERT_TRUE(ledOn(LedPattern::EmergencyActive, 99));
  TEST_ASSERT_FALSE(ledOn(LedPattern::EmergencyActive, 100));
  TEST_ASSERT_FALSE(ledOn(LedPattern::EmergencyActive, 199));
  TEST_ASSERT_TRUE(ledOn(LedPattern::EmergencyActive, 200));
}

static void test_confirmed_always_on() {
  for (uint32_t t : {0u, 1u, 999u, 3000u, 123457u})
    TEST_ASSERT_TRUE(ledOn(LedPattern::Confirmed, t));
}

static void test_config_error() {
  TEST_ASSERT_TRUE(ledOn(LedPattern::ConfigError, 0));
  TEST_ASSERT_TRUE(ledOn(LedPattern::ConfigError, 149));
  TEST_ASSERT_FALSE(ledOn(LedPattern::ConfigError, 150));
  TEST_ASSERT_FALSE(ledOn(LedPattern::ConfigError, 299));
  TEST_ASSERT_TRUE(ledOn(LedPattern::ConfigError, 300));
  TEST_ASSERT_TRUE(ledOn(LedPattern::ConfigError, 449));
  TEST_ASSERT_FALSE(ledOn(LedPattern::ConfigError, 450));
  TEST_ASSERT_FALSE(ledOn(LedPattern::ConfigError, 1999));
  TEST_ASSERT_TRUE(ledOn(LedPattern::ConfigError, 2000));
}

static void test_upgrade_window() {
  TEST_ASSERT_TRUE(ledOn(LedPattern::UpgradeWindow, 0));
  TEST_ASSERT_TRUE(ledOn(LedPattern::UpgradeWindow, 299));
  TEST_ASSERT_FALSE(ledOn(LedPattern::UpgradeWindow, 300));
  TEST_ASSERT_FALSE(ledOn(LedPattern::UpgradeWindow, 599));
  TEST_ASSERT_TRUE(ledOn(LedPattern::UpgradeWindow, 600));
}

#define ASSERT_LED(expected, actual) TEST_ASSERT_EQUAL_INT((int)(expected), (int)(actual))

static LedPlan threeSteps(uint32_t fromMs) {
  LedPlan p;
  p.fromMs = fromMs;
  p.steps = 3;
  p.pattern[0] = LedPattern::UpgradeWindow;
  p.pattern[1] = LedPattern::Confirmed;
  p.offsetMs[1] = 59000;
  p.pattern[2] = LedPattern::Idle;
  p.offsetMs[2] = 120000;
  return p;
}

static void test_plan_single_step() {
  LedPlan p;
  p.fromMs = 1000;
  p.pattern[0] = LedPattern::Idle;
  ASSERT_LED(LedPattern::Idle, ledPlanAt(p, 1000));
  ASSERT_LED(LedPattern::Idle, ledPlanAt(p, 1001000));
}

static void test_plan_switches_at_offsets() {
  const LedPlan p = threeSteps(1000);
  ASSERT_LED(LedPattern::UpgradeWindow, ledPlanAt(p, 1000));
  ASSERT_LED(LedPattern::UpgradeWindow, ledPlanAt(p, 59999));
  ASSERT_LED(LedPattern::Confirmed, ledPlanAt(p, 60000));
  ASSERT_LED(LedPattern::Confirmed, ledPlanAt(p, 120999));
  ASSERT_LED(LedPattern::Idle, ledPlanAt(p, 121000));
}

// The io task may sample millis() just before the loop computed the plan.
static void test_plan_now_before_from() {
  ASSERT_LED(LedPattern::UpgradeWindow, ledPlanAt(threeSteps(1000), 990));
}

static void test_plan_across_millis_wrap() {
  LedPlan p;
  p.fromMs = 0xFFFFFF00u;
  p.steps = 2;
  p.pattern[0] = LedPattern::UpgradeWindow;
  p.pattern[1] = LedPattern::Idle;
  p.offsetMs[1] = 0x200;
  ASSERT_LED(LedPattern::UpgradeWindow, ledPlanAt(p, 0x000000FFu));
  ASSERT_LED(LedPattern::Idle, ledPlanAt(p, 0x00000100u));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_idle);
  RUN_TEST(test_offline);
  RUN_TEST(test_light_sent_always_on);
  RUN_TEST(test_emergency_active);
  RUN_TEST(test_confirmed_always_on);
  RUN_TEST(test_config_error);
  RUN_TEST(test_upgrade_window);
  RUN_TEST(test_plan_single_step);
  RUN_TEST(test_plan_switches_at_offsets);
  RUN_TEST(test_plan_now_before_from);
  RUN_TEST(test_plan_across_millis_wrap);
  return UNITY_END();
}
