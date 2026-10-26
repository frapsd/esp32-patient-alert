#include "led_pattern.h"

bool ledOn(LedPattern p, uint32_t nowMs) {
  switch (p) {
    case LedPattern::Idle:
      return nowMs % 5000 < 50;
    case LedPattern::Offline:
      return nowMs % 2000 < 1000;
    case LedPattern::LightSent:
    case LedPattern::Confirmed:
      return true;
    case LedPattern::EmergencyActive:
      return nowMs % 200 < 100;
    case LedPattern::ConfigError: {
      uint32_t t = nowMs % 2000;
      return t < 150 || (t >= 300 && t < 450);
    }
  }
  return false;
}
