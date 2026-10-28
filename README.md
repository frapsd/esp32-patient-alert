# ESP32 Patient Alert

**One-button emergency call button for bedridden patients and caregivers, with Pushover push notifications.**

[![CI](https://github.com/frapsd/esp32-patient-alert/actions/workflows/ci.yml/badge.svg)](https://github.com/frapsd/esp32-patient-alert/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-espressif32%406.13.0-orange.svg)](https://platformio.org)
[![ESP32](https://img.shields.io/badge/board-ESP32-red.svg)](https://www.espressif.com/en/products/socs/esp32)

**English** | [Italiano](README.it.md)

> **WARNING: this is NOT a certified medical device and does NOT replace emergency services.**
> **If the situation is serious, call emergency services immediately.**
> **Without home internet (or without power) the alarm does NOT fire.** The device alerts
> nobody if it is off or disconnected from the network.

> **v1 status:** the logic is verified by automated tests and the firmware boots on the board,
> but the real network path (Wi-Fi -> Pushover -> phone) has **not yet been tested end to end**.
> Before relying on it, run tests 1-4, 8 and 10 in [docs/manual-tests.md](docs/manual-tests.md)
> (in Italian) with your real secrets.

## Features

- **Light help request**: a normal Pushover notification (water, change of position...), no loud ringing.
- **Emergency**: Pushover priority 2. It sounds even with the phone on silent and repeats every 30 seconds until a caregiver taps "acknowledge" on the phone.
- **Acknowledgement feedback**: when someone acknowledges, the LED turns solid ("help is coming") and the other caregivers get a "<name> took charge of the emergency" message.
- **Re-escalation**: if nobody acknowledges within 1500 s (25 minutes), a new emergency is sent immediately, without gaps.
- **Survives reboots**: an emergency is saved to NVS before it is sent and resumed after a restart; the "confirmed" window is restored too.
- **Offline-tolerant**: a press made without network is remembered and sent when the connection returns (the queue keeps at most 4 light requests, each with its press time).
- **Secure transport**: TLS to `api.pushover.net` with certificate verification against embedded root CAs; never disabled.
- **Watchdog**: a 60 s watchdog restarts the board if the main loop or the button/LED task hangs.
- **Buzzer cues**: every press, sent request, acknowledgement and live emergency also has a sound, for a patient who cannot see the LED.
- **Tested logic**: pure C++ in `lib/alert_core`, 127 native Unity tests (`pio test -e native`), plus a manual hardware checklist.

Principle: when in doubt, emergency. The one exception is the 3-minute "confirmed" window: there a gesture means what it always means, because a patient who cannot see the solid LED cannot know the window is open.

## How it works

```mermaid
sequenceDiagram
    participant P as Patient
    participant E as ESP32 (button + LED)
    participant W as Pushover
    participant C as Caregivers' Android phones
    P->>E: press button
    E->>E: classify gesture, save state to NVS
    E->>W: HTTPS (TLS verified) light request or emergency
    W->>C: push notification
    C->>W: acknowledge (emergency)
    E->>W: poll receipt
    W-->>E: acknowledged
    E->>E: LED solid (confirmed window, 3 min)
    W->>C: "<name> took charge" to the others
```

Until the emergency is acknowledged, Pushover repeats it every 30 s and the LED blinks fast.

## Button gestures

| Gesture | Effect |
|---|---|
| Short press (under 2.5 s) | Light request, sent immediately |
| Hold 2.5 s or more | Emergency (fires when 2.5 s elapse, without waiting for release) |
| Another press within 60 s of a light request (the LED blinks at a medium pace while this window is open) | Emergency |
| Any press while an emergency is active and unacknowledged | No new alarm (LED keeps blinking fast) |
| Press while the LED is solid "confirmed" | As always: short = light, long = emergency |

## LED patterns

| LED | Meaning |
|---|---|
| Very short flash (0.05 s) every 5 s | All good: Wi-Fi and time OK, no alarm |
| On 1 s, off 1 s | Offline: no Wi-Fi, or time not yet synchronized |
| Medium blink (0.3 s on, 0.3 s off) for 60 s from the short press | Light request registered: another press within this time raises an emergency |
| Solid for 3 s | Light request sent (outside the 60 s window; inside it only the buzzer announces it) |
| Continuous fast blink (0.1 s on, 0.1 s off) | Emergency being sent or awaiting acknowledgement |
| Solid | Emergency acknowledged, "help is coming" (lasts 3 minutes) |
| Two short flashes every 2 s | Configuration error: Pushover rejects requests (wrong token or key, monthly quota exhausted, etc.) |

When several conditions hold at once, the first in this list wins: active emergency, the "press again" window (medium blink, 60 s from the short press, also inside the confirmed window or offline: for that minute it is what the patient can act on), confirmed emergency (within the 3-minute window), configuration error, offline, "light request sent" (3 s), idle. "Request sent" (3 s) and "confirmed" (3 min) are both solid and are told apart by duration.

Timed changes (the end of the window, of "sent", of "confirmed") are carried out by the LED task, so they happen on time even while the main loop is blocked in a request. The start of the window appears when the loop processes the tap: if a request is in progress then, the medium blink starts when it ends. Whether a second press is an emergency is still decided from the moment of the press.

## Buzzer cues

The buzzer carries the LED's news, for a patient who cannot see it; only the "press again" window (medium blink) has no sound of its own. Tones stay between 500 Hz and 1.8 kHz and no step is shorter than 130 ms, for listeners with age-related hearing loss.

| Sound | Meaning |
|---|---|
| One high beep (0.3 s) as soon as the button goes down | Press registered |
| One low beep (0.3 s) as soon as the button goes down | Press registered, but nothing can be sent now: offline, configuration error, or the last request failed (Wi-Fi up but no internet). Outside an emergency a tap is still queued as a light request |
| Fast warble (about 0.8 s) after holding 2.5 s, or when the device processes a second press within 60 s (after any request already in progress) | Emergency armed, the button can be released |
| Two beeps | Light request delivered to Pushover (also inside the confirmed window, where the LED stays solid) |
| Rising three-note phrase, twice | Someone acknowledged the emergency |
| Short warble, repeated | Emergency live and not yet acknowledged: every 5 s for the first 2 minutes, then every 30 s |
| Two long low tones, repeated on the same schedule | Emergency live but **not getting out**: no Wi-Fi, a failed send (Wi-Fi up but no internet, Pushover unreachable), or Pushover rejects it |
| Two long low tones, once | Configuration error outside an emergency |

A press always sounds at once, cutting short any other cue; the other news waits until the buzzer is silent, and news cut short by a press plays again afterwards, unless an emergency started in the meantime. Presses never delay the emergency reminder; during an emergency the press beep itself says whether it is getting out (high) or not (low).

## Hardware

| Part | Notes |
|---|---|
| ESP32 dev board | Built for `esp32dev` (4 MB flash) |
| Momentary push button | Wired between the GPIO pin and GND; the firmware enables the internal pull-up |
| LED + series resistor | 220-330 ohm to GND, e.g. on GPIO26 or GPIO27, with `LED_ACTIVE_HIGH true` |
| Passive piezo buzzer | Between GPIO32 and GPIO33, driven in antiphase for twice the voltage swing. A magnetic buzzer draws far more current than a GPIO can supply (often 80-200 mA against about 40 mA): drive it from `PIN_BUZZER` through a transistor (NPN with a base resistor, flyback diode across the coil) and set `PIN_BUZZER_N -1`. Not an "active" buzzer (one with its own oscillator): it cannot play the tones |
| USB power supply | No backup battery in v1 |

Out of the box the button is on GPIO25, the LED on GPIO27 and the buzzer on GPIO32/GPIO33. To use other pins edit **only** `include/config.h` (`PIN_BUTTON`, `BUTTON_ACTIVE_LOW`, `PIN_LED`, `LED_ACTIVE_HIGH`, `PIN_BUZZER`, `PIN_BUZZER_N`). To try the firmware on a bare board, `PIN_BUTTON 0` and `PIN_LED 2` use the BOOT key and the on-board LED (GPIO2 on many boards, check yours).

- **Safe pins (button, LED, buzzer):** GPIO25, GPIO26, GPIO27, GPIO32, GPIO33. Keep `BUTTON_ACTIVE_LOW true`.
- **Avoid (button, LED, buzzer):** GPIO0, 2, 5, 12, 15 (strapping pins; the level at power-up decides how the board boots, and the final button must not be on GPIO0); GPIO6-11 (internal flash); GPIO34-39 (input only, no internal pull-up, so the pin floats and produces phantom presses).

## Quick start

### 1. Set up Pushover

1. Create an account at <https://pushover.net>.
2. Install the **Pushover** app on **every Android phone** that should receive alerts and log in. The app has a 30-day free trial, then a one-time license per platform.
3. On the website, **Create an Application/API Token**: this is `PUSHOVER_APP_TOKEN`.
4. **Create a Delivery Group** and add the caregivers: the group key is `PUSHOVER_GROUP_KEY`.
5. On each phone, so emergencies can actually sound: allow Do Not Disturb override for emergency-priority notifications in the Pushover app, set an alarm-style sound/channel (the firmware sends the `persistent` sound; users can change it in the app), and disable battery optimization for Pushover.
6. Limit: 10,000 free messages per month per Pushover account (shared by all its applications). A message to a group counts once per group member.

### 2. Configure secrets

```
cp include/secrets.example.h include/secrets.h
```

Edit `include/secrets.h` with your Wi-Fi SSID and password, the application token and the group key. The file is gitignored: never commit or share it.

### 3. Build, test, flash

Requires [PlatformIO](https://platformio.org) (platform `espressif32@6.13.0`).

```
pio test -e native              # logic tests on your computer, no board needed
pio run -e esp32dev             # build the firmware
pio run -e esp32dev -t upload   # flash the board
pio device monitor              # serial logs at 115200
```

The serial port is set in `platformio.ini` (`/dev/cu.usbserial-0001`); change it if your board shows up under another name. Logs never print tokens or keys.

Before the first flash, back up the board's original firmware (4 MB):

```
esptool --port <port> read-flash 0 0x400000 esp32-backup.bin
esptool --port <port> write-flash 0 esp32-backup.bin   # to restore it
```

Then work through [docs/manual-tests.md](docs/manual-tests.md).

## Security

- Traffic to `api.pushover.net` is TLS-encrypted and the certificate is **verified** against root CAs embedded in the firmware (`include/pushover_ca.h`): DigiCert Global Root G2, DigiCert Global Root CA, ISRG Root X1, USERTrust RSA Certification Authority. The first two cover Pushover's current chain; the others are fallbacks. If the roots expire or Pushover changes chain, verification fails and the firmware must be updated.
- The application token lives on the device and only allows **sending messages to the group**: someone who extracted it could cause false alarms, not read anything. Accepted risk; if it happens, regenerate the token on pushover.net.
- Time comes from NTP (`pool.ntp.org`, `time.google.com`) because TLS verification needs it; until it is valid the device considers itself offline.
- If Wi-Fi stays down for 60 s the device restarts the Wi-Fi connection. A 60 s watchdog reboots the board if the firmware hangs; alarm state restarts from NVS.
- `secrets.h` never goes in git. To report a vulnerability see [SECURITY.md](SECURITY.md).

## Limitations

- Without internet or power the alarm does not fire, and nobody is told the device is out of service. USB power only, no backup battery.
- Android only (the intended and tested target); no way to cancel a false alarm from the button.
- v1 has not been validated end to end on a real network (see the status note above).

## Roadmap

- Heartbeat to healthchecks.io with a Pushover alert if the device goes silent (today an off or offline device alerts nobody).
- Show the configuration error LED during an emergency too (today the fast emergency blink takes precedence; only the buzzer tells the patient that alerts are not going out).
- Immediate LED feedback on a long press even while another network request is in progress (today the fast blink starts when the main loop processes the gesture, up to a few tens of seconds later; the buzzer already sounds at once).
- An LED signal for a light request still queued (not sent) after the 60 s "press again" window.
- Escalation by phone call (SIM module).
- UPS / backup battery.

## Contributing

Issues and pull requests are welcome; see [CONTRIBUTING.md](CONTRIBUTING.md). Design documents are in [docs/](docs/README.md) (in Italian).

## License

[MIT](LICENSE) (c) 2026 frapsd
