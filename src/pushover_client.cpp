#include "pushover_client.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "pushover_ca.h"

namespace {
constexpr const char* kHost = "api.pushover.net";
constexpr uint16_t kPort = 443;
constexpr size_t kMaxBody = 4096;

// Watchdog budget: every library stage is bounded by kHttpTimeoutMs (15 s) and
// the WDT is fed between groups of at most two stages, so no stretch between
// feeds exceeds ~30 s of the 60 s watchdog:
//   DNS (<=15 s) | TCP connect + TLS handshake (<=15 + 15 s) |
//   request write + response headers (<=15 + 15 s idle) | body (<=15 s total).
// The socket is non-blocking (ssl_client), so available()/read() never stall.

// Reads the response body (HTTP/1.0: Content-Length or close-delimited) with
// an overall deadline. Keeps the first kMaxBody bytes. False on timeout or a
// short body.
bool readBody(WiFiClientSecure& tls, int contentLength, std::string& out) {
  uint8_t buf[256];
  int remaining = contentLength;  // -1 = until the server closes
  uint32_t start = millis();
  while (remaining != 0) {
    if (millis() - start >= kHttpTimeoutMs) return false;
    int avail = tls.available();
    if (avail <= 0) {
      if (!tls.connected()) return remaining < 0;
      delay(5);
      continue;
    }
    size_t want = sizeof(buf);
    if ((size_t)avail < want) want = (size_t)avail;
    if (remaining > 0 && (size_t)remaining < want) want = (size_t)remaining;
    int n = tls.read(buf, want);
    if (n <= 0) continue;
    size_t room = kMaxBody - out.size();
    out.append(reinterpret_cast<const char*>(buf), (size_t)n < room ? (size_t)n : room);
    if (remaining > 0) remaining -= n;
  }
  return true;
}

HttpResult request(const char* method, const char* path, const std::string* form) {
  HttpResult result{-1, {}};
  WiFiClientSecure tls;
  tls.setCACert(PUSHOVER_CA_PEM);
  tls.setTimeout(kHttpTimeoutMs / 1000);           // TCP connect + socket I/O, seconds
  tls.setHandshakeTimeout(kHttpTimeoutMs / 1000);  // core default is 120 s

  esp_task_wdt_reset();
  IPAddress ip;
  if (!WiFi.hostByName(kHost, ip)) {
    Serial.println("[http] DNS lookup failed");
    return result;
  }
  esp_task_wdt_reset();
  // Connect here (not inside HTTPClient) so the WDT can be fed between phases.
  // Passing the host name keeps SNI and certificate host-name verification.
  if (!tls.connect(ip, kPort, kHost, PUSHOVER_CA_PEM, nullptr, nullptr)) {
    char err[96] = "";
    int e = tls.lastError(err, sizeof(err));
    Serial.printf("[http] TLS connect failed (%d: %s)\n", e, err);
    tls.stop();
    esp_task_wdt_reset();
    return result;
  }
  esp_task_wdt_reset();

  HTTPClient http;
  http.setConnectTimeout(kHttpTimeoutMs);
  http.setTimeout(kHttpTimeoutMs);
  http.setReuse(false);
  http.useHTTP10(true);  // no chunked encoding: body is Content-Length or close-delimited
  if (!http.begin(tls, kHost, kPort, path, true)) {
    Serial.println("[http] begin failed");
    tls.stop();
    return result;
  }
  int code;
  if (form) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    code = http.sendRequest(method, (uint8_t*)form->data(), form->size());
  } else {
    code = http.sendRequest(method);
  }
  esp_task_wdt_reset();
  if (code > 0) {
    if (readBody(tls, http.getSize(), result.body)) {
      result.code = code;
    } else {
      Serial.printf("[http] HTTP %d but body read failed or timed out\n", code);
      result.body.clear();
    }
  } else {
    Serial.printf("[http] request failed: %s\n", HTTPClient::errorToString(code).c_str());
    result.code = code;
  }
  http.end();
  tls.stop();
  esp_task_wdt_reset();
  return result;
}
}  // namespace

HttpResult pushoverPost(const char* path, const std::string& form) {
  return request("POST", path, &form);
}

HttpResult pushoverGet(const std::string& pathAndQuery) {
  return request("GET", pathAndQuery.c_str(), nullptr);
}
