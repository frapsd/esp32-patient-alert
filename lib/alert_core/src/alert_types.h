#pragma once
#include <stddef.h>
#include <stdint.h>

enum class Gesture : uint8_t { Tap, Hold };
enum class Mode : uint8_t { Idle = 0, EmergencyPending = 1, EmergencyAwaitingAck = 2, Confirmed = 3 };
enum class HttpOutcome : uint8_t { Ok, Transient, Rejected };
enum class LedPattern : uint8_t {
  Idle, Offline, LightSent, EmergencyActive, Confirmed, ConfigError, UpgradeWindow
};

// Buffer sizes, including the NUL terminator.
constexpr size_t kReceiptLen = 32;
constexpr size_t kDeviceLen = 64;
