#include "nvs_store.h"
#include <Arduino.h>
#include <Preferences.h>

namespace {
constexpr const char* kNamespace = "esp-alert";
constexpr const char* kKey = "state";
}  // namespace

bool nvsLoad(PersistedState& out) {
  out = PersistedState{};
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {  // fails when the namespace does not exist yet
    Serial.println("[nvs] no saved state");
    return false;
  }
  uint8_t blob[kPersistedBlobLen] = {};
  size_t len = prefs.isKey(kKey) ? prefs.getBytes(kKey, blob, sizeof(blob)) : 0;
  prefs.end();
  if (!decodeState(blob, len, out)) {
    Serial.printf("[nvs] saved state missing or invalid (%u bytes)\n", (unsigned)len);
    return false;
  }
  return true;
}

void nvsSave(const PersistedState& s) {
  uint8_t blob[kPersistedBlobLen];
  encodeState(s, blob);
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    Serial.println("[nvs] open for write failed");
    return;
  }
  size_t written = prefs.putBytes(kKey, blob, sizeof(blob));
  prefs.end();
  if (written != sizeof(blob)) Serial.println("[nvs] write failed");
}
