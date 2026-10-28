#pragma once
#include <stdint.h>

#define PIN_BUTTON 25
#define BUTTON_ACTIVE_LOW true
#define PIN_LED 27
#define LED_ACTIVE_HIGH true
#define PIN_BUZZER 32
// Second buzzer leg, driven in antiphase: the piezo then sees twice the
// voltage swing (about +6 dB) for no extra parts. Set to -1 for a single-ended
// drive from PIN_BUZZER; this pin is then left alone. A magnetic buzzer needs
// that, through a transistor: its coil draws more than a GPIO can supply.
#define PIN_BUZZER_N 33
#define BUZZER_LEDC_CHANNEL 0

constexpr uint32_t kDebounceMs = 30;
constexpr uint32_t kHoldMs = 2500;
constexpr uint32_t kLightUpgradeWindowMs = 60000;
constexpr uint32_t kConfirmedWindowS = 180;
constexpr uint32_t kPollIntervalMs = 10000;
constexpr uint32_t kTransientRetryMs = 5000;
constexpr uint32_t kRejectedRetryMs = 300000;
constexpr uint32_t kLightSentShowMs = 3000;
constexpr uint32_t kWatchdogS = 60;
constexpr uint32_t kHttpTimeoutMs = 15000;
// Reminder while an emergency is live: every 5 s for the first 2 minutes, then
// every 30 s, so a long unacknowledged emergency stays bearable at night.
constexpr uint32_t kEmergencyChirpMs = 5000;
constexpr uint32_t kEmergencyChirpFastForMs = 120000;
constexpr uint32_t kEmergencyChirpSlowMs = 30000;
