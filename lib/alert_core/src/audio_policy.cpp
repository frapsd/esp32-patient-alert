#include "audio_policy.h"

AudioState audioState(const AlertMachine& m, bool online) {
  AudioState a;
  a.serial = m.emergencySerial();
  // online only means Wi-Fi and a valid clock: the internet behind the router
  // can still be down, which only a failed request reveals.
  const bool reachable = online && !m.configError();
  if (m.mode() == Mode::EmergencyAwaitingAck) {
    // Pushover holds a receipt and keeps alerting the phones on its own.
    a.reminder = Cue::EmergencyWaiting;
  } else if (m.mode() == Mode::EmergencyPending) {
    // Requests failing just before count too: a fresh emergency must not
    // sound like it is getting out until its own first send has failed.
    const bool gettingOut = reachable && !m.emergencySendFailing() && !m.lastRequestFailed();
    a.reminder = gettingOut ? Cue::EmergencyWaiting : Cue::Failed;
  }
  // During an emergency the press beep agrees with the reminder; otherwise it
  // says whether a request made now could get out.
  a.canSend = a.reminder != Cue::None ? a.reminder == Cue::EmergencyWaiting
                                      : reachable && !m.lastRequestFailed();
  return a;
}
