#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "alert_types.h"

// Button + LED task, pinned to core 0 so both keep working while the loop task
// (core 1) is blocked in an HTTPS request. Gestures go to `gestureQueue`
// (item type GestureEvent), stamped at detection so the loop can process them
// late; a full queue drops the gesture and logs it.
struct GestureEvent {
  Gesture g;
  uint32_t pressMs;     // millis() at detection
  uint32_t pressEpoch;  // netWallEpoch() at detection, 0 = wall time unknown
};

void ioStart(QueueHandle_t gestureQueue);
void ioSetPattern(LedPattern);
