#pragma once
#include <stdint.h>

#define PIN_BUTTON 0
#define BUTTON_ACTIVE_LOW true
#define PIN_LED 2
#define LED_ACTIVE_HIGH true

constexpr uint32_t kDebounceMs = 30;
constexpr uint32_t kHoldMs = 1500;
constexpr uint32_t kLightUpgradeWindowMs = 60000;
constexpr uint32_t kConfirmedWindowS = 600;
constexpr uint32_t kPollIntervalMs = 10000;
constexpr uint32_t kTransientRetryMs = 5000;
constexpr uint32_t kRejectedRetryMs = 300000;
constexpr uint32_t kLightSentShowMs = 3000;
constexpr uint32_t kWatchdogS = 60;
constexpr uint32_t kHttpTimeoutMs = 15000;
