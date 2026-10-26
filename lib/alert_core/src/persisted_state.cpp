#include "persisted_state.h"
#include <string.h>

static const uint8_t kVersion = 1;
static const size_t kCrcOffset = 40;

uint32_t crc32Ieee(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
  }
  return crc ^ 0xFFFFFFFFu;
}

static void putLe32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static uint32_t getLe32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void encodeState(const PersistedState& s, uint8_t out[kPersistedBlobLen]) {
  memset(out, 0, kPersistedBlobLen);
  out[0] = kVersion;
  out[1] = (uint8_t)s.mode;
  size_t n = 0;
  while (n < kReceiptLen - 1 && s.receipt[n] != '\0') n++;
  memcpy(out + 2, s.receipt, n);  // remaining bytes (incl. terminator) stay zero
  putLe32(out + 34, s.confirmedAtEpoch);
  putLe32(out + kCrcOffset, crc32Ieee(out, kCrcOffset));
}

bool decodeState(const uint8_t* in, size_t len, PersistedState& out) {
  out = PersistedState();
  if (in == nullptr || len != kPersistedBlobLen) return false;
  if (in[0] != kVersion) return false;
  if (getLe32(in + kCrcOffset) != crc32Ieee(in, kCrcOffset)) return false;
  if (in[1] > (uint8_t)Mode::Confirmed) return false;
  if (memchr(in + 2, '\0', kReceiptLen) == nullptr) return false;
  PersistedState r;
  r.mode = (Mode)in[1];
  memcpy(r.receipt, in + 2, kReceiptLen);
  r.confirmedAtEpoch = getLe32(in + 34);
  out = r;
  return true;
}
