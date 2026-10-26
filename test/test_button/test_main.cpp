#include <unity.h>
#include "alert_types.h"

void setUp() {}
void tearDown() {}

static void test_mode_values() {
  TEST_ASSERT_EQUAL_INT(3, static_cast<int>(Mode::Confirmed));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_mode_values);
  return UNITY_END();
}
