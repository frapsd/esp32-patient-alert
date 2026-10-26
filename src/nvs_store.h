#pragma once
#include "persisted_state.h"

// NVS namespace "esp-alert", key "state", blob encoded by encodeState().
// nvsLoad returns false (and a default state) when missing or corrupt.
bool nvsLoad(PersistedState&);
// Returns false when the blob was not written; the caller must retry.
bool nvsSave(const PersistedState&);
