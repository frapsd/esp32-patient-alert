#pragma once
#include <stddef.h>
#include <stdint.h>
#include "alert_types.h"

struct PersistedState {
  Mode mode = Mode::Idle;
  char receipt[kReceiptLen] = {};
  uint32_t confirmedAtEpoch = 0;
};

constexpr size_t kPersistedBlobLen = 48;

// CRC32 (IEEE, reflected, poly 0xEDB88320).
uint32_t crc32Ieee(const uint8_t* data, size_t len);

void encodeState(const PersistedState& s, uint8_t out[kPersistedBlobLen]);
// On any failure returns false and sets `out` to a default PersistedState.
bool decodeState(const uint8_t* in, size_t len, PersistedState& out);
