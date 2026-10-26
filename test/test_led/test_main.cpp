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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_idle);
  RUN_TEST(test_offline);
  RUN_TEST(test_light_sent_always_on);
  RUN_TEST(test_emergency_active);
  RUN_TEST(test_confirmed_always_on);
  RUN_TEST(test_config_error);
  return UNITY_END();
}
