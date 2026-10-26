# ESP Alert (Pushover) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Firmware for an ESP32 with one button and one LED. A bedridden patient uses it to send a light help request or an emergency to caregivers' Android phones via Pushover, with acknowledgement feedback on the LED.

**Architecture:** All decision logic is pure C++17 in `lib/alert_core` (button gestures, alert state machine with retry policy, Pushover request/response handling, LED patterns, persisted-state codec) and is unit-tested on the Mac with `pio test -e native`. A thin Arduino layer in `src/` does the hardware and network work. A FreeRTOS task reads the button and drives the LED every 10 ms, so both stay responsive while the main loop is blocked in HTTPS. The main loop runs the machine's commands and is protected by the task watchdog. Emergency state persists in NVS.

**Tech Stack:** PlatformIO, `espressif32` platform (Arduino framework, board `esp32dev`), Unity for tests, ArduinoJson 7, WiFiClientSecure + HTTPClient, Preferences (NVS).

**Spec:** `docs/superpowers/specs/2026-10-26-esp-alert-pushover-design.md`

## Global Constraints

- Hardware: ESP32-D0WD-V3, 4 MB flash, serial `/dev/cu.usbserial-0001`. Prototype I/O: button = BOOT/GPIO0 (active low, external pull-up on the board), LED = GPIO2 (active high). All pins and polarities are only in `include/config.h`.
- Button: Tap < 1500 ms = light request; Hold ≥ 1500 ms = emergency, fired at the threshold without waiting for release; any press within 60 000 ms of the last light-request press = emergency; any press while an emergency is unacknowledged = ignored; any press during the confirmed window = new emergency.
- Confirmed window (solid LED) = 600 s.
- Pushover emergency: `priority=2`, `retry=30`, `expire=1500`, `sound=persistent`. Light request: `priority=0`. Ack notice: `priority=0`. The recipient is the group key in the `user` parameter.
- Pushover limits: receipt poll interval 10 000 ms (API minimum 5 s); after a transient failure (network error, HTTP ≤ 0, 5xx, other non-2xx/4xx) retry no sooner than 5 000 ms; after a rejection (any 4xx including 429, or HTTP 200 with `status != 1`) retry no sooner than 300 000 ms; requests strictly sequential.
- Copy (exact, Italian):
  - Light: title `Richiesta di aiuto`, message `Il paziente chiede aiuto (non urgente).`, plus ` Richiesta delle HH:MM.` when the press time is known.
  - Emergency: title `EMERGENZA`, message `EMERGENZA — il paziente ha premuto il bottone. Conferma quando hai preso in carico.`
  - Ack notice: title `Presa in carico`, message `<device> ha preso in carico l'emergenza.` or `Qualcuno ha preso in carico l'emergenza.` when `acknowledged_by_device` is empty.
- TLS: always verify certificates against embedded root CAs. `setInsecure()` is forbidden.
- Secrets live only in `include/secrets.h`, which is gitignored. Never log the token or the group key.
- Timezone `CET-1CEST,M3.5.0,M10.5.0/3`, NTP `pool.ntp.org`, `time.google.com`. Wall time is "valid" when epoch ≥ 1704067200 (2024-01-01).
- C++17 (`-std=gnu++17`) in both environments. `alert_core` must not include Arduino headers.

## Review Focus

1. **Wi-Fi lost mid-emergency, then back:** the emergency must still be sent; LED keeps fast-blinking meanwhile. Test in Task 6: `emergency_survives_repeated_transient_failures`.
2. **Reboot or power blip while awaiting ack:** polling must resume on the same receipt. Test in Task 6: `restore_awaiting_ack_resumes_polling`.
3. **Panic mashing (tap-tap-tap or hold-and-tap):** exactly one emergency, no duplicate sends. Test in Task 6: `mashing_produces_single_emergency`.
4. **Quota exhausted (429) or wrong token:** no hammering (≥ 300 s between attempts), LED shows the config error, retries never stop. Test in Task 6: `rejected_send_waits_300s_and_shows_config_error`.
5. **Emergency expires unacknowledged after the 50 retries:** a new emergency is sent immediately. Test in Task 6: `expired_unacked_receipt_resends_emergency`.

---

## File Structure

```
platformio.ini                 envs: esp32dev, native
.gitignore                     .pio/, include/secrets.h
include/config.h               pins, polarities, all timing constants
include/secrets.example.h      template; copy to secrets.h
include/pushover_ca.h          PUSHOVER_CA_PEM (multi-root PEM string)
lib/alert_core/library.json
lib/alert_core/src/alert_types.h        shared enums/structs
lib/alert_core/src/button_interpreter.{h,cpp}
lib/alert_core/src/led_pattern.{h,cpp}
lib/alert_core/src/pushover_protocol.{h,cpp}
lib/alert_core/src/persisted_state.{h,cpp}
lib/alert_core/src/alert_machine.{h,cpp}
src/main.cpp                   setup/loop wiring, command execution, watchdog
src/io_task.{h,cpp}            10 ms FreeRTOS task: button → gesture queue, LED drive
src/net.{h,cpp}                Wi-Fi + NTP, isOnline(), wallEpoch(), hhmm()
src/pushover_client.{h,cpp}    HTTPS POST/GET returning (code, body)
src/nvs_store.{h,cpp}          load/save PersistedState via Preferences
test/test_button/test_main.cpp
test/test_led/test_main.cpp
test/test_protocol/test_main.cpp
test/test_persist/test_main.cpp
test/test_machine/test_main.cpp
README.md                      Italian: disclaimer, setup, LED table, security notes
docs/manual-tests.md           hardware test checklist
```

---

### Task 1: Toolchain and project scaffold

**Files:**
- Create: `platformio.ini`, `.gitignore`, `include/config.h`, `include/secrets.example.h`, `lib/alert_core/library.json`, `lib/alert_core/src/alert_types.h`, `src/main.cpp` (minimal: serial "boot" log), `test/test_button/test_main.cpp` (smoke test only)

**Interfaces:**
- Produces, in `alert_types.h`:
  - `enum class Gesture : uint8_t { Tap, Hold };`
  - `enum class Mode : uint8_t { Idle = 0, EmergencyPending = 1, EmergencyAwaitingAck = 2, Confirmed = 3 };`
  - `enum class HttpOutcome : uint8_t { Ok, Transient, Rejected };`
  - `enum class LedPattern : uint8_t { Idle, Offline, LightSent, EmergencyActive, Confirmed, ConfigError };`
  - `constexpr size_t kReceiptLen = 32; constexpr size_t kDeviceLen = 64;` (buffer sizes including the NUL terminator)
- Produces, in `config.h`: `PIN_BUTTON 0`, `BUTTON_ACTIVE_LOW true`, `PIN_LED 2`, `LED_ACTIVE_HIGH true`, plus the timing constants from Global Constraints as `constexpr uint32_t` (`kDebounceMs=30`, `kHoldMs=1500`, `kLightUpgradeWindowMs=60000`, `kConfirmedWindowS=600`, `kPollIntervalMs=10000`, `kTransientRetryMs=5000`, `kRejectedRetryMs=300000`, `kLightSentShowMs=3000`, `kWatchdogS=60`, `kHttpTimeoutMs=15000`).
- Produces, in `secrets.example.h`: `WIFI_SSID`, `WIFI_PASSWORD`, `PUSHOVER_APP_TOKEN`, `PUSHOVER_GROUP_KEY` as `#define` string literals.

- [ ] **Step 1: Install PlatformIO** with `pipx install platformio`. Verify with `pio --version`, which should print a version.
- [ ] **Step 2: Back up the current ESP32 flash** before anything is ever written to it: `esptool --port /dev/cu.usbserial-0001 read-flash 0 0x400000 ~/esp32-backup.bin`. The file must be 4194304 bytes.
- [ ] **Step 3: Write `platformio.ini`.**
  - `[env:esp32dev]`: `platform = espressif32@<latest 6.x, pinned exact>`, `framework = arduino`, `board = esp32dev`, `monitor_speed = 115200`, `upload_port = monitor_port = /dev/cu.usbserial-0001`, `lib_deps = bblanchon/ArduinoJson@^7`, `build_unflags = -std=gnu++11`, `build_flags = -std=gnu++17 -Wall -Wextra`.
  - `[env:native]`: `platform = native`, `test_framework = unity`, the same `lib_deps` and C++17 flags, `build_flags` also `-DNATIVE_TEST`.
  - `.gitignore`: `.pio/`, `include/secrets.h`.
  - Create a local `include/secrets.h` copied from the example with placeholder values. It is not committed.
- [ ] **Step 4: Smoke test.** `test/test_button/test_main.cpp` includes `alert_types.h` and asserts `static_cast<int>(Mode::Confirmed) == 3`. Run `pio test -e native`. Expected: PASS.
- [ ] **Step 5: Firmware build.** Run `pio run -e esp32dev`. Expected: SUCCESS.
- [ ] **Step 6: Commit** `chore: PlatformIO scaffold with native test env`.

### Task 2: ButtonInterpreter

**Files:** Create `lib/alert_core/src/button_interpreter.{h,cpp}`; Test `test/test_button/test_main.cpp` (replaces the smoke test).

**Interfaces:**
- Produces: `class ButtonInterpreter { public: ButtonInterpreter(uint32_t debounceMs, uint32_t holdMs); std::optional<Gesture> update(bool pressed, uint32_t nowMs); };`
- `pressed` is already polarity-corrected (true = pressed). It is called every ~10 ms. A raw change is accepted only after it has been stable for `debounceMs`. Press duration is measured from the accepted press edge. All time math uses unsigned subtraction, so it is safe across wraparound.

- [ ] **Step 1: Write failing tests** with debounce 30 and hold 1500. Feed samples every 10 ms with a helper `feed(interp, pressed, fromMs, toMs)` that collects the gestures.
  - `tap_emits_on_release`: press for 200 ms, release → exactly `{Tap}`, emitted after release, and none before.
  - `hold_emits_at_threshold_without_release`: press and keep it pressed → `Hold` is emitted once, between accepted-press+1500 and +1510 ms; nothing else while held up to 5000 ms.
  - `release_after_hold_emits_nothing`: hold for 3000 ms, then release → only `{Hold}` in total.
  - `bounce_shorter_than_debounce_is_ignored`: pressed for 20 ms, then released → no gesture.
  - `bouncy_press_counts_once`: alternate pressed/released every 10 ms for 40 ms, then stay pressed 200 ms, release → exactly `{Tap}`.
  - `wraparound_tap`: start at `0xFFFFFF00`, press 200 ms across the wrap → `{Tap}`.
  - `two_taps_two_gestures`: tap, 300 ms gap, tap → `{Tap, Tap}`.
- [ ] **Step 2: Run** `pio test -e native -f test_button`. Expected: FAIL (does not compile).
- [ ] **Step 3: Implement `ButtonInterpreter::update`.**
- [ ] **Step 4: Run** `pio test -e native -f test_button`. Expected: all PASS.
- [ ] **Step 5: Commit** `feat(core): button gesture interpreter`.

### Task 3: LED patterns

**Files:** Create `lib/alert_core/src/led_pattern.{h,cpp}`; Test `test/test_led/test_main.cpp`.

**Interfaces:**
- Produces: `bool ledOn(LedPattern p, uint32_t nowMs);`. This is a pure function of time; patterns are periodic in `nowMs`.
- Patterns (decided here; the README documents them):
  - `Idle`: on 0–49 of every 5000 ms (heartbeat blip)
  - `Offline`: on 0–999 of 2000
  - `LightSent`: always on (the caller shows it for 3000 ms)
  - `EmergencyActive`: on 0–99 of 200
  - `Confirmed`: always on
  - `ConfigError`: on 0–149 and 300–449 of 2000

- [ ] **Step 1: Failing tests**, one per pattern, checking on/off at boundary times (for example `EmergencyActive`: 0 → on, 99 → on, 100 → off, 199 → off, 200 → on; `ConfigError`: 0 on, 150 off, 300 on, 450 off, 1999 off).
- [ ] **Step 2: Run** `pio test -e native -f test_led`. Expected: FAIL.
- [ ] **Step 3: Implement `ledOn`.**
- [ ] **Step 4: Run.** Expected: PASS.
- [ ] **Step 5: Commit** `feat(core): LED patterns`.

### Task 4: Pushover protocol (request bodies, response classification)

**Files:** Create `lib/alert_core/src/pushover_protocol.{h,cpp}`; Test `test/test_protocol/test_main.cpp`.

**Interfaces:**
- Produces:
  - `HttpOutcome classifyHttp(int httpCode, bool bodyStatusOk);`: `httpCode <= 0` → Transient; `200 && bodyStatusOk` → Ok; `200 && !bodyStatusOk` → Rejected; `400..499` → Rejected; anything else → Transient.
  - `struct SendResponse { HttpOutcome outcome; char receipt[kReceiptLen]; };`
  - `SendResponse parseSendResponse(int httpCode, const char* body);`: `bodyStatusOk` means JSON `status == 1`. Unparseable body with 200 → Transient (the server misbehaved, so retry). `receipt` is copied, truncated safely, empty if absent.
  - `struct ReceiptStatus { HttpOutcome outcome; bool acknowledged; bool expired; char ackDevice[kDeviceLen]; };`
  - `ReceiptStatus parseReceiptResponse(int httpCode, const char* body);`
  - `std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields);`: application/x-www-form-urlencoded; unreserved characters `A-Za-z0-9-_.~` are kept, everything else is `%XX` byte by byte (UTF-8 safe), space → `%20`.
  - `std::string lightBody(const char* token, const char* user, const char* hhmm);` (`hhmm` is empty when the time is unknown)
  - `std::string emergencyBody(const char* token, const char* user);`
  - `std::string ackNoticeBody(const char* token, const char* user, const char* device);`
  - `std::string receiptPath(const char* receipt, const char* token);` → `/1/receipts/<receipt>.json?token=<token>`

- [ ] **Step 1: Failing tests:**
  - `classify_table`: -1→T, 200+ok→Ok, 200+!ok→R, 400→R, 429→R, 500→T, 503→T, 302→T.
  - `parse_send_ok_with_receipt`: `(200, {"status":1,"request":"x","receipt":"rABC"})` → Ok, `"rABC"`.
  - `parse_send_rejected`: `(400, {"status":0,"errors":["application token is invalid"]})` → Rejected.
  - `parse_send_garbage_200`: `(200, "<html>")` → Transient.
  - `parse_send_long_receipt_truncated`: a receipt of 100 characters → NUL-terminated, length 31.
  - `parse_receipt_acked`: `{"status":1,"acknowledged":1,"acknowledged_by_device":"pixel-marco","expired":0}` → acknowledged, device `"pixel-marco"`.
  - `parse_receipt_expired_unacked`: `acknowledged 0, expired 1` → expired && !acknowledged.
  - `parse_receipt_404`: `(404, {"status":0})` → Rejected.
  - `form_encode_utf8`: `{"m","a b—è&="}` → `m=a%20b%E2%80%94%C3%A8%26%3D`.
  - `emergency_body_fields`: the decoded body contains `priority=2`, `retry=30`, `expire=1500`, `sound=persistent`, title `EMERGENZA`, and the exact emergency message from Global Constraints.
  - `light_body_with_and_without_time`: the message is exactly `Il paziente chiede aiuto (non urgente). Richiesta delle 14:32.` with the time, and `Il paziente chiede aiuto (non urgente).` without it; `priority=0`.
  - `ack_notice_named_and_anonymous`: `pixel-marco ha preso in carico l'emergenza.` / `Qualcuno ha preso in carico l'emergenza.`
  - Tests decode bodies with a small test-only helper `std::map<std::string,std::string> formDecode(const std::string&)`.
- [ ] **Step 2: Run** `pio test -e native -f test_protocol`. Expected: FAIL.
- [ ] **Step 3: Implement** with ArduinoJson `JsonDocument` + `deserializeJson`.
- [ ] **Step 4: Run.** Expected: PASS.
- [ ] **Step 5: Commit** `feat(core): Pushover request bodies and response parsing`.

### Task 5: Persisted state codec

**Files:** Create `lib/alert_core/src/persisted_state.{h,cpp}`; Test `test/test_persist/test_main.cpp`.

**Interfaces:**
- Produces:
  - `struct PersistedState { Mode mode = Mode::Idle; char receipt[kReceiptLen] = {}; uint32_t confirmedAtEpoch = 0; };`
  - `constexpr size_t kPersistedBlobLen = 48;`. Layout: version byte (=1), mode byte, 32-byte receipt, 4-byte LE epoch, 2 bytes zero padding, 8-byte trailer where the first 4 bytes are the LE CRC32 (IEEE) of bytes 0..39 and the last 4 are zero.
  - `void encodeState(const PersistedState&, uint8_t out[kPersistedBlobLen]);`
  - `bool decodeState(const uint8_t* in, size_t len, PersistedState& out);`: returns false and leaves `out` default (Idle) on a wrong length, wrong version, bad CRC, mode > 3, or a receipt not NUL-terminated.

- [ ] **Step 1: Failing tests:** `roundtrip_awaiting_ack`, `roundtrip_confirmed_epoch`, `bad_crc_rejected`, `wrong_len_rejected`, `wrong_version_rejected`, `invalid_mode_rejected`, `unterminated_receipt_rejected`.
- [ ] **Step 2: Run** `pio test -e native -f test_persist`. Expected: FAIL.
- [ ] **Step 3: Implement** (bitwise CRC32 is fine).
- [ ] **Step 4: Run.** Expected: PASS.
- [ ] **Step 5: Commit** `feat(core): persisted alert state codec with CRC`.

### Task 6: Alert state machine

**Files:** Create `lib/alert_core/src/alert_machine.{h,cpp}`; Test `test/test_machine/test_main.cpp`.

**Interfaces:**
- Consumes: `Gesture`, `Mode`, `HttpOutcome`, `LedPattern` (Task 1); `SendResponse`, `ReceiptStatus` (Task 4); `PersistedState` (Task 5).
- Produces:

```cpp
struct MachineConfig {
  uint32_t lightUpgradeWindowMs, confirmedWindowS, pollIntervalMs,
           transientRetryMs, rejectedRetryMs, lightSentShowMs;
};
enum class CommandKind : uint8_t { None, SendEmergency, PollReceipt, SendLight, SendAckNotice };
struct Command {
  CommandKind kind = CommandKind::None;
  uint32_t pressEpoch = 0;             // SendLight: wall time of the press, 0 = unknown
  char receipt[kReceiptLen] = {};      // PollReceipt
  char device[kDeviceLen] = {};        // SendAckNotice: "" = anonymous
};
class AlertMachine {
 public:
  explicit AlertMachine(const MachineConfig&);
  void restore(const PersistedState&, uint32_t nowMs, uint32_t nowEpoch);
  void onGesture(Gesture, uint32_t nowMs, uint32_t nowEpoch);
  Command nextCommand(uint32_t nowMs);     // None if nothing is due yet
  void onEmergencySent(const SendResponse&, uint32_t nowMs);
  void onReceipt(const ReceiptStatus&, uint32_t nowMs, uint32_t nowEpoch);
  void onLightSent(HttpOutcome, uint32_t nowMs);
  void onAckNoticeSent(HttpOutcome, uint32_t nowMs);
  Mode mode() const;
  LedPattern led(uint32_t nowMs, bool online) const;
  bool takeDirty();                        // true once after any change to the persisted fields
  PersistedState persisted() const;
};
```

Behavior (the decisions the implementer cannot make alone):
- **Gestures:**
  - `Idle` + `Hold` → emergency.
  - `Idle` + `Tap` → emergency if a previous light-request press happened within `lightUpgradeWindowMs`; otherwise enqueue a light request (FIFO, capacity 4, drop the newest when full) and record the press time.
  - `EmergencyPending`/`EmergencyAwaitingAck` + any gesture → ignored.
  - `Confirmed` + any gesture → emergency.
- **Triggering an emergency:** clear the light queue, mode `EmergencyPending`, clear the receipt, the send is due immediately, mark dirty. A pending ack notice is kept.
- **`nextCommand` priority:** first the emergency (SendEmergency in `EmergencyPending`, PollReceipt in `EmergencyAwaitingAck`), then the ack notice, then the head of the light queue. Only commands whose due time ≤ now are returned. Each kind has its own due time.
- **Result handling:**

  | Result | Effect |
  |---|---|
  | Ok on send emergency | Store the receipt, mode `EmergencyAwaitingAck`, next poll at now + poll interval, dirty |
  | Ok on send emergency with an empty receipt | Treat as Transient |
  | Transient, any send | Retry at now + `transientRetryMs` |
  | Rejected, any send | Retry at now + `rejectedRetryMs` and set the `configError` flag; the next Ok clears it |
  | Receipt Ok + acknowledged | `Confirmed`, `confirmedAtMs = now`, `confirmedAtEpoch = nowEpoch`, queue an ack notice with the device, dirty |
  | Receipt Ok + expired and not acknowledged | `EmergencyPending`, send due now, dirty |
  | Receipt Ok, neither | Next poll at now + poll interval |
  | Receipt Transient | Next poll at now + `transientRetryMs` |
  | Receipt Rejected | `EmergencyPending` (resend), send due at now + `rejectedRetryMs`, `configError` set, dirty |
  | Light Ok | Pop the head; LightSent shown until now + `lightSentShowMs` |
  | Ack notice Ok | Clear the notice |

- **Confirmed expiry:** when `now - confirmedAtMs ≥ confirmedWindowS*1000`, mode becomes `Idle` and dirty. This is checked in `nextCommand`, `onGesture` and `led`; `led` is const, so it only reports `Idle`, and the transition happens in the non-const calls.
- **`restore`:**
  - `EmergencyPending` → send due now.
  - `EmergencyAwaitingAck` with a non-empty receipt → poll due now. With an empty receipt → `EmergencyPending`.
  - `Confirmed` → if `nowEpoch != 0 && nowEpoch - confirmedAtEpoch < window`, stay `Confirmed` with `confirmedAtMs` back-dated so the window ends on time; otherwise `Idle`.
  - Anything else → `Idle`.
- **`led(now, online)` precedence:**
  1. `EmergencyActive` (Pending or AwaitingAck, even offline)
  2. `Confirmed`
  3. `ConfigError`
  4. `Offline` (when `!online`)
  5. `LightSent` (inside its show window)
  6. `Idle`

- [ ] **Step 1: Failing tests.** Use a fixture with the config values from Global Constraints and helper `drain(m, now)` that returns the `nextCommand` kind. Required tests, including the Review Focus ones:
  - `tap_queues_light_request`: tap → `SendLight` with `pressEpoch` passed through; after `onLightSent(Ok)` → `None`; LED `LightSent` for 3000 ms, then `Idle`.
  - `hold_triggers_emergency`: hold → `SendEmergency`; mode `EmergencyPending`; dirty once.
  - `second_tap_within_60s_upgrades`: tap at t=0, tap at t=59 999 → `SendEmergency`, and the light queue is empty (the second `drain` after emergency handling does not return `SendLight`).
  - `second_tap_after_60s_is_light`: taps at 0 and 60 000 → two light requests.
  - `mashing_produces_single_emergency` (RF3): tap, tap, tap, hold within 2 s → exactly one `SendEmergency` before any result is fed; the gestures after the first emergency are ignored.
  - `emergency_ack_flow`: send Ok with receipt `r1` → `PollReceipt r1` not before +10 000 ms; receipt acked by `pixel-marco` → mode `Confirmed`, `SendAckNotice` with device `pixel-marco`, LED `Confirmed`; after 600 s → `Idle`.
  - `press_during_confirmed_is_emergency`: in `Confirmed`, tap → `SendEmergency`.
  - `press_during_awaiting_ack_ignored`.
  - `emergency_survives_repeated_transient_failures` (RF1): 5 × Transient → each retry exactly at +5000 ms, never earlier; then Ok → `AwaitingAck`. LED `EmergencyActive` throughout, even with `online=false`.
  - `rejected_send_waits_300s_and_shows_config_error` (RF4): Rejected → `None` at +299 999, `SendEmergency` at +300 000; LED stays `EmergencyActive` while the emergency is pending (precedence); after a later light Rejected in Idle → LED `ConfigError`.
  - `expired_unacked_receipt_resends_emergency` (RF5): poll result expired && !acked → `SendEmergency` immediately; mode `EmergencyPending`.
  - `receipt_rejected_resends_after_300s`.
  - `restore_awaiting_ack_resumes_polling` (RF2): restore `{AwaitingAck, "r9"}` → `PollReceipt r9` immediately.
  - `restore_pending_sends_now`, `restore_awaiting_without_receipt_resends`.
  - `restore_confirmed_within_window_keeps_solid_led_for_remaining_time`: confirmedAtEpoch = E, restore at E+100 → `Confirmed` until +500 s, then `Idle`.
  - `restore_confirmed_unknown_time_goes_idle`: nowEpoch 0 → `Idle`.
  - `light_queue_capacity_4`: 6 spaced-out taps (≥ 60 s apart) while every send is Transient → at most 4 sends are pending, FIFO order of `pressEpoch`.
  - `emergency_has_priority_over_pending_light_and_ack_notice`.
- [ ] **Step 2: Run** `pio test -e native -f test_machine`. Expected: FAIL.
- [ ] **Step 3: Implement `AlertMachine`.**
- [ ] **Step 4: Run** `pio test -e native` (all suites). Expected: all PASS.
- [ ] **Step 5: Commit** `feat(core): alert state machine with retry policy and restore`.

### Task 7: Hardware and network layer, firmware on the device

**Files:** Create `src/io_task.{h,cpp}`, `src/net.{h,cpp}`, `src/pushover_client.{h,cpp}`, `src/nvs_store.{h,cpp}`, `include/pushover_ca.h`; Modify `src/main.cpp`.

**Interfaces:**
- Consumes: everything in `alert_core`, plus `config.h` and `secrets.h`.
- Produces:
  - `void ioStart(QueueHandle_t gestureQueue);` and `void ioSetPattern(LedPattern);`. The task is pinned to core 0 at priority 2 with a 10 ms `vTaskDelayUntil`. It polarity-corrects `digitalRead(PIN_BUTTON)` and feeds a `ButtonInterpreter`. A gesture is sent with `xQueueSend` and timeout 0; if the queue is full it logs and drops. The LED is driven with `ledOn(pattern, millis())`; the pattern is held in a `std::atomic<uint8_t>`.
  - `void netBegin(); bool netOnline(); uint32_t netWallEpoch(); void netHhmm(uint32_t epoch, char out[6]);`. `netOnline()` means Wi-Fi is connected and the wall time is valid. `netWallEpoch()` returns 0 when the time is invalid. Uses `WiFi.mode(WIFI_STA)`, `WiFi.persistent(false)`, `setAutoReconnect(true)`, and `configTzTime` with the TZ and NTP servers from Global Constraints.
  - `struct HttpResult { int code; std::string body; }; HttpResult pushoverPost(const char* path, const std::string& form); HttpResult pushoverGet(const std::string& pathAndQuery);`. Each call uses a fresh `WiFiClientSecure` with `setCACert(PUSHOVER_CA_PEM)` and an `HTTPClient` with `setTimeout(kHttpTimeoutMs)` and `setConnectTimeout(kHttpTimeoutMs)`. The host is `api.pushover.net`. The body is capped at 4 KB. Never log `form` or a query containing the token.
  - `bool nvsLoad(PersistedState&); void nvsSave(const PersistedState&);` (namespace `esp-alert`, key `state`, `putBytes` / `getBytes`).
  - `PUSHOVER_CA_PEM`: concatenated PEM of DigiCert Global Root G2, DigiCert Global Root CA (the current api.pushover.net chain ends in these, as observed with `openssl s_client` on 2026-10-26), plus ISRG Root X1 and USERTrust RSA Certification Authority as fallbacks if Pushover changes CA. Take them from `python3 -m certifi`'s bundle and write each root's SHA-256 fingerprint in a comment.
- `main.cpp` loop, in this order every iteration:
  1. `esp_task_wdt_reset()` (WDT `kWatchdogS`, panic = reboot, loop task subscribed).
  2. Drain the gesture queue into `machine.onGesture(g, millis(), netWallEpoch())`.
  3. If `netOnline()`: take `nextCommand`, execute it with the client and protocol, and feed the result back. For `SendLight`, `hhmm` is formatted from `pressEpoch` only if it is non-zero.
  4. If `takeDirty()`: `nvsSave(machine.persisted())`.
  5. `ioSetPattern(machine.led(millis(), netOnline()))`.
  6. `delay(20)`.

  In `setup`: serial log, `nvsLoad` then `restore(..., millis(), netWallEpoch())` before Wi-Fi is up (wall time may be 0, which is acceptable per Task 6), `ioStart`, `netBegin`, WDT init. Log state transitions with a `[alert]` prefix and no secrets.

- [ ] **Step 1: Build** `pio run -e esp32dev`. Expected: SUCCESS with zero warnings from `src/` and `lib/`.
- [ ] **Step 2: Verify TLS roots offline.** Run `openssl verify -CAfile <(the PEM from pushover_ca.h) <(openssl s_client -connect api.pushover.net:443 -showcerts </dev/null 2>/dev/null | awk '/BEGIN/,/END/' )` over the server chain. Expected: `OK`.
- [ ] **Step 3: Flash** with `pio run -e esp32dev -t upload` (the backup from Task 1 must exist first).
- [ ] **Step 4: Serial check.** Run `pio device monitor` for about 30 s; read it non-interactively with `timeout 30 pio device monitor --quiet > log`. Expected: boot log, `[alert] restored mode=Idle`, and Wi-Fi attempts. With placeholder secrets it never connects, the LED pattern is Offline, and there are no crashes or WDT resets in 30 s.
- [ ] **Step 5: Commit** `feat: ESP32 firmware wiring (io task, net, TLS client, NVS, watchdog)`.

### Task 8: README and manual test checklist

**Files:** Create `README.md` (Italian) and `docs/manual-tests.md`.

- [ ] **Step 1: README** sections:
  - A bold disclaimer at the top: not a certified medical device and does not replace 112; without home internet the alarm does not leave.
  - What it does.
  - Button gestures (the table from the spec).
  - LED patterns (from Task 3).
  - Pushover setup: account, app install on each phone, Application → token, Delivery Group → group key, Android permissions (DND override, alarm channel, battery optimisation off), a 30-day trial note.
  - `secrets.h` setup.
  - Build, test and flash commands.
  - Security notes: the token on the device only allows sending to the group; TLS is verified against embedded roots.
  - Moving to the final button pin: not GPIO0; an internal pull-up, button to GND.
  - Limits and next steps (offline heartbeat via healthchecks.io, SIM call escalation, UPS).
- [ ] **Step 2: `docs/manual-tests.md`**, a checklist covering the spec's manual tests: short press, long press, double press, Wi-Fi off during an emergency, reboot during an emergency, phone acknowledgement, expiry (shorten `expire` temporarily only in a test build), and press during confirmed. Each item has its expected LED and phone behavior.
- [ ] **Step 3: Commit** `docs: README and manual test checklist`.
