# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- Buzzer cues for a patient who cannot see the LED: a beep the moment the button goes down (a lower one when nothing can be sent), a warble when the hold arms an emergency, then cues for a delivered light request and for an acknowledgement. While an emergency is live a reminder repeats every 5 s for 2 minutes, then every 30 s, with a distinct low tone when the emergency is not getting out (no Wi-Fi, a failed send, or rejected). A cue cut short by a press plays again afterwards, unless an emergency started meanwhile; presses never delay the reminder, outside an emergency the press beep is low whenever the last request failed (during one it agrees with the reminder), news still queued when an emergency starts is dropped, and an emergency raised by a second press within 60 s gets the same warble as a hold.
- LED "press again" window: after a short press the LED blinks at a medium pace (0.3 s on, 0.3 s off) for the 60 s in which another press raises an emergency. It ranks just below an active emergency, so it also shows inside the confirmed window and offline; inside it the 3 s "sent" solid is not shown (the buzzer still announces delivery).
- Default pins for the final hardware: button GPIO25, LED GPIO27, piezo buzzer between GPIO32 and GPIO33 in antiphase.

### Changed
- Hold threshold raised from 1.5 s to 2.5 s.
- Confirmed window ("help is coming", solid LED) shortened from 10 to 3 minutes.
- Timed LED changes are carried out by the button/LED task from a plan published by the main loop, so the window, "sent" and "confirmed" end on time even while the loop is blocked in a request.
- Inside the confirmed window gestures keep their usual meaning (tap = light request, hold or a second tap within 60 s = emergency). Before, any press there sent a new emergency; the patient cannot see the solid LED that says the window is open. A gesture made while the emergency was still unacknowledged and processed after the acknowledgement is still ignored. A tap now counts from the moment the button went down (not its release), a hold from its 2.5 s threshold.

### Fixed
- A second tap pressed just before the 60 s were up but released after them was sent as a light request instead of an emergency: the first press was forgotten exactly at 60 s while the tap was only read at its release. The first press is now remembered for an hour after the window; the decision is still press to press.
- Timed state (upgrade window, "sent", confirmed window) now expires on every loop iteration, also offline, so a stale timestamp cannot come back after the 49.7-day `millis()` wrap.

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
