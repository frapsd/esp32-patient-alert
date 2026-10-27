#include "io_task.h"
#include <Arduino.h>
#include <esp_rom_gpio.h>
#include <esp_task_wdt.h>
#include <soc/gpio_sig_map.h>
#include <atomic>
#include "button_interpreter.h"
#include "buzzer_cue.h"
#include "config.h"
#include "led_pattern.h"
#include "net.h"

namespace {
constexpr uint32_t kIoPeriodMs = 10;
constexpr BaseType_t kIoCore = 0;
constexpr UBaseType_t kIoPriority = 2;
constexpr uint32_t kIoStackBytes = 3072;

QueueHandle_t gQueue = nullptr;
std::atomic<uint8_t> gPattern{static_cast<uint8_t>(LedPattern::Offline)};
// Written by the loop task on core 1, read by the io task on core 0.
std::atomic<uint8_t> gPendingCues{0};  // cueBit() set of news to play
std::atomic<bool> gEmergencyRaised{false};
std::atomic<uint8_t> gReminder{static_cast<uint8_t>(Cue::None)};
std::atomic<bool> gCanSend{false};

// Reprogramming the LEDC timer on every tick makes the piezo stutter, so only
// write when the frequency actually changes.
void buzzerWrite(uint16_t freqHz) {
  static uint16_t last = 0;
  if (freqHz == last) return;
  last = freqHz;
  if (freqHz == 0) {
    ledcWrite(BUZZER_LEDC_CHANNEL, 0);
  } else {
    ledcWriteTone(BUZZER_LEDC_CHANNEL, freqHz);
  }
}

void ioTask(void*) {
  esp_task_wdt_add(nullptr);  // watchdog already initialised by setup()
  ButtonInterpreter button(kDebounceMs, kHoldMs);
  CueEngine cues(kEmergencyChirpMs, kEmergencyChirpFastForMs, kEmergencyChirpSlowMs);
  bool wasPressed = false;
  uint32_t pressEpoch = 0;
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    esp_task_wdt_reset();
    uint32_t now = millis();
    bool level = digitalRead(PIN_BUTTON) == HIGH;
    bool pressed = BUTTON_ACTIVE_LOW ? !level : level;
    std::optional<Gesture> g = button.update(pressed, now);

    CueInputs in;
    // At the press, not at the gesture: a Tap is only emitted on release, and
    // the patient needs to hear straight away that the press registered.
    const bool nowPressed = button.pressedStable();
    in.pressEdge = nowPressed && !wasPressed;
    wasPressed = nowPressed;
    // netWallEpoch() only reads time(); safe from this core.
    if (in.pressEdge) pressEpoch = netWallEpoch();
    in.holdFired = g && *g == Gesture::Hold;
    in.canSend = gCanSend.load(std::memory_order_relaxed);
    in.emergencyRaised = gEmergencyRaised.exchange(false, std::memory_order_relaxed);
    const uint8_t pendingMask = gPendingCues.load(std::memory_order_relaxed);
    in.pending = firstPending(pendingMask);
    in.reminder = static_cast<Cue>(gReminder.load(std::memory_order_relaxed));
    CueDecision d;
    buzzerWrite(cues.tick(in, now, d));
    // Clear only what was read: news posted meanwhile stays for later.
    if (d.dropPending) {
      gPendingCues.fetch_and(static_cast<uint8_t>(~pendingMask), std::memory_order_relaxed);
    } else if (d.pendingTaken) {
      gPendingCues.fetch_and(static_cast<uint8_t>(~cueBit(in.pending)), std::memory_order_relaxed);
    }

    if (g) {
      // A Tap is stamped at the press, not at its release up to kHoldMs
      // later; a Hold at its threshold, the moment it became an emergency.
      // (A Hold found only at release, after a starved tick, still gets its
      // threshold time.)
      const bool hold = *g == Gesture::Hold;
      GestureEvent item{*g, hold ? button.pressStartMs() + kHoldMs : button.pressStartMs(),
                        hold ? netWallEpoch() : pressEpoch};
      bool queued = xQueueSend(gQueue, &item, 0) == pdTRUE;
      if (!queued && hold) {
        // The patient already heard "armed": a Hold makes room by dropping
        // the oldest gesture rather than being dropped itself. The loop may
        // have drained the queue meanwhile, so retry the send either way.
        GestureEvent oldest;
        const bool evicted = xQueueReceive(gQueue, &oldest, 0) == pdTRUE;
        queued = xQueueSend(gQueue, &item, 0) == pdTRUE;
        if (queued && evicted) Serial.println("[io] gesture queue full, oldest gesture dropped for a hold");
      }
      if (!queued) Serial.println("[io] gesture queue full, gesture dropped");
    }
    bool lit = ledOn(static_cast<LedPattern>(gPattern.load(std::memory_order_relaxed)), now);
    digitalWrite(PIN_LED, lit == LED_ACTIVE_HIGH ? HIGH : LOW);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(kIoPeriodMs));
  }
}
}  // namespace

void ioStart(QueueHandle_t gestureQueue) {
  gQueue = gestureQueue;
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LED_ACTIVE_HIGH ? LOW : HIGH);
  // ledcWriteTone only needs a square wave, so the duty resolution is
  // irrelevant; the frequency is reprogrammed per tone step.
  ledcSetup(BUZZER_LEDC_CHANNEL, 2000, 10);
  ledcAttachPin(PIN_BUZZER, BUZZER_LEDC_CHANNEL);
  ledcWrite(BUZZER_LEDC_CHANNEL, 0);
#if PIN_BUZZER_N >= 0
  // Same LEDC channel routed to a second pin with the output inverted, so the
  // piezo sits between two pins swinging in opposite directions: 6.6 V across
  // the element instead of 3.3 V. Channels 0-7 are the high-speed group.
  // A piezo is capacitive, so the steady opposite levels while idle pass no
  // current; a magnetic buzzer needs PIN_BUZZER_N set to -1 and a transistor.
  pinMode(PIN_BUZZER_N, OUTPUT);
  esp_rom_gpio_connect_out_signal(PIN_BUZZER_N, LEDC_HS_SIG_OUT0_IDX + BUZZER_LEDC_CHANNEL, true,
                                  false);
#endif
  xTaskCreatePinnedToCore(ioTask, "io", kIoStackBytes, nullptr, kIoPriority, nullptr, kIoCore);
}

void ioSetPattern(LedPattern p) {
  gPattern.store(static_cast<uint8_t>(p), std::memory_order_relaxed);
}

void ioPlayCue(Cue c) {
  gPendingCues.fetch_or(cueBit(c), std::memory_order_relaxed);
}

void ioEmergencyRaised() {
  gEmergencyRaised.store(true, std::memory_order_relaxed);
}

void ioSetAudioState(const AudioState& a) {
  gCanSend.store(a.canSend, std::memory_order_relaxed);
  gReminder.store(static_cast<uint8_t>(a.reminder), std::memory_order_relaxed);
}
