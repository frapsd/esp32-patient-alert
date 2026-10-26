#include "button_interpreter.h"

std::optional<Gesture> ButtonInterpreter::update(bool pressed, uint32_t nowMs) {
  if (pressed != raw_) {
    raw_ = pressed;
    rawSinceMs_ = nowMs;
  }
  if (raw_ != stable_ && static_cast<uint32_t>(nowMs - rawSinceMs_) >= debounceMs_) {
    stable_ = raw_;
    if (stable_) {
      acceptedPressMs_ = nowMs;
      holdFired_ = false;
    } else if (!holdFired_) {
      return Gesture::Tap;
    }
    return std::nullopt;
  }
  if (stable_ && !holdFired_ && static_cast<uint32_t>(nowMs - acceptedPressMs_) >= holdMs_) {
    holdFired_ = true;
    return Gesture::Hold;
  }
  return std::nullopt;
}
