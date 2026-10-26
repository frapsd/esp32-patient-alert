#pragma once
#include <stdint.h>

// Wi-Fi station + SNTP wall clock (TZ Europe/Rome).
void netBegin();
// Re-kicks Wi-Fi when the core's auto-reconnect has given up (it does not retry
// on some reasons, e.g. AUTH_FAIL) and logs connectivity/time changes. Call
// once per loop iteration.
void netMaintain();
// Wi-Fi connected and wall time valid.
bool netOnline();
// Unix epoch seconds, or 0 while the wall time is not valid (< 2024-01-01).
uint32_t netWallEpoch();
// Local "HH:MM" for epoch.
void netHhmm(uint32_t epoch, char out[6]);
