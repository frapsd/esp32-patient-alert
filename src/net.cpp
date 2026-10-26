#include "net.h"
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "config.h"
#include "secrets.h"

namespace {
constexpr const char* kTz = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr const char* kNtp1 = "pool.ntp.org";
constexpr const char* kNtp2 = "time.google.com";
constexpr time_t kValidEpoch = 1704067200;  // 2024-01-01T00:00:00Z
constexpr uint32_t kRekickMs = 60000;

uint32_t gLastKickMs = 0;
bool gWasConnected = false;
bool gTimeLogged = false;
volatile uint8_t gLastReason = 0;

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      uint8_t reason = info.wifi_sta_disconnected.reason;
      if (reason != gLastReason) {  // the core retries every few seconds; log changes only
        gLastReason = reason;
        Serial.printf("[net] Wi-Fi disconnected, reason %u\n", reason);
      }
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      gLastReason = 0;
      Serial.printf("[net] Wi-Fi connected, ip %s\n", WiFi.localIP().toString().c_str());
      break;
    default:
      break;
  }
}

void connect() {
  Serial.println("[net] Wi-Fi connecting");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  gLastKickMs = millis();
}
}  // namespace

void netBegin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(onWifiEvent);
  configTzTime(kTz, kNtp1, kNtp2);
  connect();
}

void netMaintain() {
  bool connected = WiFi.status() == WL_CONNECTED;
  uint32_t now = millis();
  if (connected) {
    gLastKickMs = now;
  } else if (now - gLastKickMs >= kRekickMs) {
    Serial.printf("[net] Wi-Fi still down (status %d), retrying\n", (int)WiFi.status());
    WiFi.disconnect();
    connect();
  }
  if (connected != gWasConnected) {
    gWasConnected = connected;
    if (!connected) Serial.println("[net] Wi-Fi link down");
  }
  if (!gTimeLogged && netWallEpoch() != 0) {
    gTimeLogged = true;
    char hhmm[6];
    netHhmm(netWallEpoch(), hhmm);
    Serial.printf("[net] wall time valid, local %s\n", hhmm);
  }
}

bool netOnline() {
  return WiFi.status() == WL_CONNECTED && netWallEpoch() != 0;
}

uint32_t netWallEpoch() {
  time_t now = time(nullptr);
  return now >= kValidEpoch ? static_cast<uint32_t>(now) : 0;
}

void netHhmm(uint32_t epoch, char out[6]) {
  time_t t = static_cast<time_t>(epoch);
  struct tm local;
  localtime_r(&t, &local);
  snprintf(out, 6, "%02u:%02u", (unsigned)local.tm_hour % 24u, (unsigned)local.tm_min % 60u);
}
