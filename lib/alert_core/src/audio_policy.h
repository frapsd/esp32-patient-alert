#pragma once
#include "alert_machine.h"
#include "buzzer_cue.h"

// What the buzzer must say about the alarm state, kept out of the hardware
// loop so the native tests cover it.
struct AudioState {
  bool canSend = false;      // a press now could get out: Press, else PressOffline
  Cue reminder = Cue::None;  // repeated while an emergency is live
};

// online: Wi-Fi and a valid clock, as netOnline() reports it.
AudioState audioState(const AlertMachine& m, bool online);
