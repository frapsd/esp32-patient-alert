#pragma once
#include <cstdint>
#include <optional>
#include "alert_types.h"

// Debounces a polarity-corrected button signal and emits one gesture per press:
// Hold when held >= holdMs (measured from the accepted press edge), otherwise
// Tap on the accepted release. Wraparound-safe (unsigned time math).
class ButtonInterpreter {
 public:
  ButtonInterpreter(uint32_t debounceMs, uint32_t holdMs)
      : debounceMs_(debounceMs), holdMs_(holdMs) {}
  std::optional<Gesture> update(bool pressed, uint32_t nowMs);
  // Debounced level, for feedback that must fire at the press: a Tap is
  // only known at release, up to holdMs later.
  bool pressedStable() const { return stable_; }
  // Accepted press edge of the current or last press: when it really happened,
  // as opposed to when its gesture was emitted.
  uint32_t pressStartMs() const { return acceptedPressMs_; }

 private:
  uint32_t debounceMs_;
  uint32_t holdMs_;
  bool raw_ = false;
  uint32_t rawSinceMs_ = 0;
  bool stable_ = false;
  uint32_t acceptedPressMs_ = 0;
  bool holdFired_ = false;
};
