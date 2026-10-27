#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "alert_machine.h"
#include "audio_policy.h"
#include "config.h"
#include "io_task.h"
#include "net.h"
#include "nvs_store.h"
#include "pushover_client.h"
#include "pushover_protocol.h"
#include "secrets.h"

namespace {
constexpr const char* kMessagesPath = "/1/messages.json";
constexpr UBaseType_t kGestureQueueLen = 8;
constexpr uint32_t kLoopDelayMs = 20;

AlertMachine machine(MachineConfig{kLightUpgradeWindowMs, kConfirmedWindowS, kPollIntervalMs,
                                   kTransientRetryMs, kRejectedRetryMs, kLightSentShowMs});
QueueHandle_t gestureQueue = nullptr;
Mode lastMode = Mode::Idle;
LedPattern lastLed = LedPattern::Offline;
bool saveFailing = false;  // last nvsSave failed: retry every iteration
bool lastConfigError = false;

const char* modeName(Mode m) {
  switch (m) {
    case Mode::Idle: return "Idle";
    case Mode::EmergencyPending: return "EmergencyPending";
    case Mode::EmergencyAwaitingAck: return "EmergencyAwaitingAck";
    case Mode::Confirmed: return "Confirmed";
  }
  return "?";
}

const char* ledName(LedPattern p) {
  switch (p) {
    case LedPattern::Idle: return "Idle";
    case LedPattern::Offline: return "Offline";
    case LedPattern::LightSent: return "LightSent";
    case LedPattern::EmergencyActive: return "EmergencyActive";
    case LedPattern::Confirmed: return "Confirmed";
    case LedPattern::ConfigError: return "ConfigError";
  }
  return "?";
}

const char* outcomeName(HttpOutcome o) {
  switch (o) {
    case HttpOutcome::Ok: return "Ok";
    case HttpOutcome::Transient: return "Transient";
    case HttpOutcome::Rejected: return "Rejected";
  }
  return "?";
}

void logModeChange() {
  Mode m = machine.mode();
  if (m != lastMode) {
    Serial.printf("[alert] mode %s -> %s\n", modeName(lastMode), modeName(m));
    // Only an acknowledgement enters Confirmed after setup(), which seeds
    // lastMode from the restored state: a reboot inside the window stays quiet.
    if (m == Mode::Confirmed) ioPlayCue(Cue::Confirmed);
    lastMode = m;
  }
}

// Hands the LED and the buzzer the current state. Called right after gestures
// too, not only at the end of loop(): the send that follows can block for
// tens of seconds, and a new emergency must not stay silent meanwhile.
void publishIo() {
  LedPattern led = machine.led(millis(), netOnline());
  if (led != lastLed) {
    Serial.printf("[alert] led %s -> %s\n", ledName(lastLed), ledName(led));
    lastLed = led;
  }
  ioSetPattern(led);

  // The patient may not see the LED, so the buzzer carries the same news.
  const AudioState audio = audioState(machine, netOnline());
  const bool configError = machine.configError();
  // A configuration error outside an emergency sounds once, when it appears.
  if (configError && !lastConfigError && audio.reminder == Cue::None) ioPlayCue(Cue::Failed);
  lastConfigError = configError;
  ioSetAudioState(audio);
}

// Saves the persisted state when it changed. takeDirty() clears the flag, so a
// failed save is retried from saveFailing on every call until it succeeds (a
// lost EmergencyPending would come back Idle on reboot).
void persistIfDirty() {
  if (machine.takeDirty() || saveFailing) {
    bool ok = nvsSave(machine.persisted());
    if (!ok && !saveFailing) Serial.println("[nvs] state save failed, retrying");
    if (ok && saveFailing) Serial.println("[nvs] state save recovered");
    saveFailing = !ok;
  }
}

// Executes one command synchronously and feeds its result back. Bodies and
// paths contain the app token and are never logged.
void execute(const Command& cmd) {
  switch (cmd.kind) {
    case CommandKind::None:
      return;
    case CommandKind::SendEmergency: {
      HttpResult r = pushoverPost(kMessagesPath, emergencyBody(PUSHOVER_APP_TOKEN, PUSHOVER_GROUP_KEY));
      SendResponse sr = parseSendResponse(r.code, r.body.c_str());
      Serial.printf("[alert] emergency sent: HTTP %d %s receipt=%s\n", r.code,
                    outcomeName(sr.outcome), sr.receipt);
      machine.onEmergencySent(sr, millis());
      return;
    }
    case CommandKind::PollReceipt: {
      HttpResult r = pushoverGet(receiptPath(cmd.receipt, PUSHOVER_APP_TOKEN));
      ReceiptStatus rs = parseReceiptResponse(r.code, r.body.c_str());
      Serial.printf("[alert] receipt %s: HTTP %d %s acknowledged=%d expired=%d\n", cmd.receipt,
                    r.code, outcomeName(rs.outcome), rs.acknowledged, rs.expired);
      machine.onReceipt(rs, millis(), netWallEpoch());
      return;
    }
    case CommandKind::SendLight: {
      char hhmm[6] = "";
      if (cmd.pressEpoch != 0) netHhmm(cmd.pressEpoch, hhmm);
      HttpResult r = pushoverPost(
          kMessagesPath, lightBody(PUSHOVER_APP_TOKEN, PUSHOVER_GROUP_KEY, hhmm[0] ? hhmm : nullptr));
      HttpOutcome o = parseSendResponse(r.code, r.body.c_str()).outcome;
      Serial.printf("[alert] light request sent: HTTP %d %s\n", r.code, outcomeName(o));
      machine.onLightSent(o, millis());
      // From the outcome, not the LED: inside the confirmed window the solid
      // LED hides LightSent, but the patient still needs to hear it went out.
      if (o == HttpOutcome::Ok) ioPlayCue(Cue::LightSent);
      return;
    }
    case CommandKind::SendAckNotice: {
      HttpResult r =
          pushoverPost(kMessagesPath, ackNoticeBody(PUSHOVER_APP_TOKEN, PUSHOVER_GROUP_KEY, cmd.device));
      HttpOutcome o = parseSendResponse(r.code, r.body.c_str()).outcome;
      Serial.printf("[alert] ack notice sent: HTTP %d %s\n", r.code, outcomeName(o));
      machine.onAckNoticeSent(o, millis());
      return;
    }
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("[boot] esp-alert starting");

  PersistedState saved;
  nvsLoad(saved);
  machine.restore(saved, millis(), netWallEpoch());  // wall time may still be 0 here
  lastMode = machine.mode();
  Serial.printf("[alert] restored mode=%s\n", modeName(lastMode));

  // The watchdog must exist before the io task subscribes to it.
  esp_task_wdt_init(kWatchdogS, true);  // panic => reboot; state comes back from NVS
  esp_task_wdt_add(nullptr);            // subscribe the loop task

  gestureQueue = xQueueCreate(kGestureQueueLen, sizeof(GestureEvent));
  ioSetPattern(lastLed);
  ioStart(gestureQueue);  // the io task subscribes itself to the watchdog
  Serial.printf("[boot] watchdog %us on loop and io tasks\n", (unsigned)kWatchdogS);
  netBegin();
}

void loop() {
  esp_task_wdt_reset();

  GestureEvent ev;
  while (xQueueReceive(gestureQueue, &ev, 0) == pdTRUE) {
    uint32_t now = millis();
    Serial.printf("[alert] gesture %s (pressed %ums ago)\n", ev.g == Gesture::Hold ? "Hold" : "Tap",
                  (unsigned)(now - ev.pressMs));
    const bool wasEmergency = machine.emergencyLive();
    machine.onGesture(ev.g, now, ev.pressMs, ev.pressEpoch);
    // A Hold already played the "armed" warble at the threshold; an emergency
    // from the upgrade rule has not been announced yet.
    if (!wasEmergency && machine.emergencyLive() && ev.g != Gesture::Hold) ioEmergencyRaised();
    logModeChange();
  }
  // Spec: the emergency is in NVS before it is sent, so a power cut during the
  // (blocking, up to tens of seconds) send cannot lose it. If this save fails
  // the send still goes ahead; persistIfDirty() keeps retrying every loop.
  persistIfDirty();
  publishIo();

  netMaintain();
  machine.onWallClock(millis(), netWallEpoch());  // settles a Confirmed restored without time
  logModeChange();

  // AlertMachine has no in-flight tracking: nextCommand() keeps returning the
  // same command until its result is fed back. So call it at most once per
  // iteration, execute synchronously, and feed the result back (inside
  // execute) before the next call.
  if (netOnline()) {
    Command cmd = machine.nextCommand(millis());
    execute(cmd);
    logModeChange();
  }
  persistIfDirty();
  publishIo();

  delay(kLoopDelayMs);
}
