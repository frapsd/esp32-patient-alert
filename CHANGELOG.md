# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.1.0] - 2026-10-26

First version. The logic is covered by automated tests; the real network path (Wi-Fi, Pushover, phone) has not yet been tested end to end.

### Added
- One-button gestures: tap sends a light help request; hold 1.5 s, a second press within 60 s, or a press during the confirmed window sends an emergency.
- Pushover delivery: normal-priority light requests and priority 2 emergencies that repeat every 30 s until acknowledged; follow-up message when someone acknowledges; automatic re-send if unacknowledged after 1500 s.
- Status LED patterns for idle, offline, light request sent, emergency pending, confirmed and configuration error.
- Offline queue for button presses (up to 4 light requests) with sending when the connection returns.
- Alert state persisted in NVS, saved before sending and restored after a reboot.
- TLS to `api.pushover.net` with certificate verification against embedded root CAs; NTP time sync.
- Wi-Fi recovery and a 60 s task watchdog.
- Native Unity test suite (`pio test -e native`) for the pure logic in `lib/alert_core`, and a manual hardware test checklist.
