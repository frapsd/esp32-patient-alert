# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- Buzzer cues for a patient who cannot see the LED: a beep the moment the button goes down (a lower one when nothing can be sent), a warble when the hold arms an emergency, then cues for a delivered light request and for an acknowledgement. While an emergency is live a reminder repeats every 5 s for 2 minutes, then every 30 s, with a distinct low tone when the emergency is not getting out (no Wi-Fi, a failed send, or rejected). A cue cut short by a press plays again afterwards.
- Default pins for the final hardware: button GPIO25, LED GPIO27, piezo buzzer between GPIO32 and GPIO33 in antiphase.

### Changed
- Hold threshold raised from 1.5 s to 2.5 s.
- Inside the 10-minute confirmed window gestures keep their usual meaning (tap = light request, hold or a second tap within 60 s = emergency). Before, any press there sent a new emergency; the patient cannot see the solid LED that says the window is open. A press made before the acknowledgement and processed after it is still ignored.

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
