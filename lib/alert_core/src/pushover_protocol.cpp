#include "pushover_protocol.h"
#include <string.h>
#include <ArduinoJson.h>

HttpOutcome classifyHttp(int httpCode, bool bodyStatusOk) {
  if (httpCode <= 0) return HttpOutcome::Transient;
  if (httpCode == 200) return bodyStatusOk ? HttpOutcome::Ok : HttpOutcome::Rejected;
  if (httpCode >= 400 && httpCode <= 499) return HttpOutcome::Rejected;
  return HttpOutcome::Transient;
}

// Copies src into dst (capacity cap incl. NUL), truncating; null src gives "".
static void copyTrunc(char* dst, size_t cap, const char* src) {
  if (!src) src = "";
  size_t n = strlen(src);
  if (n > cap - 1) n = cap - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

// Parses body; returns false when unparseable. okOut is JSON status == 1.
static bool parseBody(const char* body, JsonDocument& doc, bool& okOut) {
  okOut = false;
  if (!body) return false;
  if (deserializeJson(doc, body)) return false;
  okOut = doc["status"].as<int>() == 1;
  return true;
}

static HttpOutcome outcomeFor(int httpCode, bool parsed, bool statusOk) {
  if (httpCode == 200 && !parsed) return HttpOutcome::Transient;
  return classifyHttp(httpCode, statusOk);
}

SendResponse parseSendResponse(int httpCode, const char* body) {
  SendResponse r;
  r.receipt[0] = '\0';
  JsonDocument doc;
  bool ok;
  bool parsed = parseBody(body, doc, ok);
  r.outcome = outcomeFor(httpCode, parsed, ok);
  if (parsed) copyTrunc(r.receipt, kReceiptLen, doc["receipt"] | (const char*)nullptr);
  return r;
}

ReceiptStatus parseReceiptResponse(int httpCode, const char* body) {
  ReceiptStatus r;
  r.acknowledged = false;
  r.expired = false;
  r.ackDevice[0] = '\0';
  JsonDocument doc;
  bool ok;
  bool parsed = parseBody(body, doc, ok);
  r.outcome = outcomeFor(httpCode, parsed, ok);
  if (parsed) {
    r.acknowledged = doc["acknowledged"].as<int>() == 1;
    r.expired = doc["expired"].as<int>() == 1;
    copyTrunc(r.ackDevice, kDeviceLen, doc["acknowledged_by_device"] | (const char*)nullptr);
  }
  return r;
}

std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields) {
  static const char* hex = "0123456789ABCDEF";
  auto enc = [&](const std::string& s, std::string& out) {
    for (unsigned char c : s) {
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
          c == '-' || c == '_' || c == '.' || c == '~') {
        out += (char)c;
      } else {
        out += '%';
        out += hex[c >> 4];
        out += hex[c & 15];
      }
    }
  };
  std::string out;
  for (size_t i = 0; i < fields.size(); i++) {
    if (i) out += '&';
    enc(fields[i].first, out);
    out += '=';
    enc(fields[i].second, out);
  }
  return out;
}

static std::string s(const char* p) { return p ? p : ""; }

std::string lightBody(const char* token, const char* user, const char* hhmm) {
  std::string msg = "Il paziente chiede aiuto (non urgente).";
  if (hhmm && *hhmm) msg += std::string(" Richiesta delle ") + hhmm + ".";
  return formEncode({{"token", s(token)}, {"user", s(user)}, {"title", "Richiesta di aiuto"},
                     {"message", msg}, {"priority", "0"}});
}

std::string emergencyBody(const char* token, const char* user) {
  return formEncode({{"token", s(token)}, {"user", s(user)}, {"title", "EMERGENZA"},
                     {"message", "EMERGENZA \xE2\x80\x94 il paziente ha premuto il bottone. Conferma quando hai preso in carico."},
                     {"priority", "2"}, {"retry", "30"}, {"expire", "1500"}, {"sound", "persistent"}});
}

std::string ackNoticeBody(const char* token, const char* user, const char* device) {
  std::string who = (device && *device) ? device : "Qualcuno";
  return formEncode({{"token", s(token)}, {"user", s(user)}, {"title", "Presa in carico"},
                     {"message", who + " ha preso in carico l'emergenza."}, {"priority", "0"}});
}

std::string receiptPath(const char* receipt, const char* token) {
  return "/1/receipts/" + s(receipt) + ".json?token=" + s(token);
}
