#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "alert_types.h"

// Button + LED task, pinned to core 0 so both keep working while the loop task
// (core 1) is blocked in an HTTPS request. Gestures go to `gestureQueue`
// (item type Gesture); a full queue drops the gesture and logs it.
void ioStart(QueueHandle_t gestureQueue);
void ioSetPattern(LedPattern);
