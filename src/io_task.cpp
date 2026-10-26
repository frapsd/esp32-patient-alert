#include "io_task.h"
#include <Arduino.h>
#include <atomic>
#include "button_interpreter.h"
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

void ioTask(void*) {
  ButtonInterpreter button(kDebounceMs, kHoldMs);
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    uint32_t now = millis();
    bool level = digitalRead(PIN_BUTTON) == HIGH;
    bool pressed = BUTTON_ACTIVE_LOW ? !level : level;
    if (std::optional<Gesture> g = button.update(pressed, now)) {
      // netWallEpoch() only reads time(); safe from this core.
      GestureEvent item{*g, now, netWallEpoch()};
      if (xQueueSend(gQueue, &item, 0) != pdTRUE) {
        Serial.println("[io] gesture queue full, gesture dropped");
      }
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
  xTaskCreatePinnedToCore(ioTask, "io", kIoStackBytes, nullptr, kIoPriority, nullptr, kIoCore);
}

void ioSetPattern(LedPattern p) {
  gPattern.store(static_cast<uint8_t>(p), std::memory_order_relaxed);
}
