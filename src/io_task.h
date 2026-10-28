#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "alert_types.h"
#include "audio_policy.h"
#include "buzzer_cue.h"
#include "led_pattern.h"

// Button + LED task, pinned to core 0 so both keep working while the loop task
// (core 1) is blocked in an HTTPS request. Gestures go to `gestureQueue`
// (item type GestureEvent), stamped so the loop can process them late: a Tap
// with its accepted press edge (it is only emitted at release), a Hold with its
// threshold. Both match the spec (the 60 s upgrade counts press to press) and
// let a gesture made before an acknowledgement be told apart from one made
// after it. A full queue drops the gesture and logs it; a Hold drops the
// oldest gesture instead, since the patient has already heard it armed. The task is subscribed to the task watchdog (initialised by
// setup() before ioStart()), so a stuck io task reboots the board too.
struct GestureEvent {
  Gesture g;
  uint32_t pressMs;     // millis(): Tap at the press edge, Hold at the threshold
  uint32_t pressEpoch;  // netWallEpoch() at the same moment, 0 = wall time unknown
};

void ioStart(QueueHandle_t gestureQueue);
// Published by the loop task after every change; the io task follows it on its
// own until the next one, so timed patterns end on time.
void ioSetLedPlan(const LedPlan&);
// Queues one news cue for the buzzer; callable from the loop task. It plays
// once the buzzer is silent. Different cues queued meanwhile are all kept.
void ioPlayCue(Cue);
// The loop raised an emergency without a Hold (the upgrade rule): the buzzer
// announces it at once. A Hold is announced by the io task itself.
void ioEmergencyRaised();
// Published by the loop task after every change; see audioState().
void ioSetAudioState(const AudioState&);
