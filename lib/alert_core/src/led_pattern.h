#pragma once
#include <cstddef>
#include <cstdint>
#include "alert_types.h"

// Pure function of time: whether the status LED is lit for pattern p at nowMs.
// Patterns are periodic in nowMs (nowMs % period).
bool ledOn(LedPattern p, uint32_t nowMs);

// What the LED shows from now on, as far as it is known when computed: the
// pattern now and the pattern from each later deadline. The loop task can
// block in a request for over a minute; the io task follows the plan meanwhile,
// so a timed pattern (the upgrade window above all) ends on time.
constexpr size_t kLedPlanSteps = 4;
struct LedPlan {
  uint32_t fromMs = 0;                     // millis() when computed
  uint8_t steps = 1;                       // 1..kLedPlanSteps
  LedPattern pattern[kLedPlanSteps] = {};  // pattern[i] from offsetMs[i] on
  uint32_t offsetMs[kLedPlanSteps] = {};   // ascending, offsetMs[0] == 0
};

// The step in force at nowMs. Offsets are relative to fromMs, so this is
// wraparound-safe; a nowMs slightly before fromMs counts as fromMs.
LedPattern ledPlanAt(const LedPlan&, uint32_t nowMs);
