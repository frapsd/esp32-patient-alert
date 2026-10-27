#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "alert_types.h"
#include "buzzer_cue.h"

// Button + LED task, pinned to core 0 so both keep working while the loop task
// (core 1) is blocked in an HTTPS request. Gestures go to `gestureQueue`
// (item type GestureEvent), stamped when the interpreter emits them so the
// loop can process them late; a full queue drops the gesture and logs it.
// A Tap is emitted (and stamped) at release, up to kHoldMs after the actual
// press; a Hold at the hold threshold. The late Tap stamp only makes the 60 s
// upgrade and the confirmed window look slightly later, so both err toward
// an emergency. The task is subscribed to the task watchdog (initialised by
// setup() before ioStart()), so a stuck io task reboots the board too.
struct GestureEvent {
  Gesture g;
  uint32_t pressMs;     // millis() when emitted: Tap at release, Hold at threshold
  uint32_t pressEpoch;  // netWallEpoch() at the same moment, 0 = wall time unknown
};

void ioStart(QueueHandle_t gestureQueue);
void ioSetPattern(LedPattern);
// Queues one cue for the buzzer; callable from the loop task. It plays once the
// buzzer is silent. Only the latest unplayed cue is kept.
void ioPlayCue(Cue);
// canSend picks the press cue (Press or PressOffline). reminder is repeated
// while an emergency is live, Cue::None otherwise.
void ioSetAudioState(bool canSend, Cue reminder);
