#pragma once
#include <cstdint>
#include "alert_types.h"

// Pure function of time: whether the status LED is lit for pattern p at nowMs.
// Patterns are periodic in nowMs (nowMs % period).
bool ledOn(LedPattern p, uint32_t nowMs);
