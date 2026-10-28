# LED Upgrade Window Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a medium blink for the 60 s in which a second press escalates a light request to an emergency, with timed LED changes executed by the io task, and shorten the confirmed window to 3 minutes.

**Architecture:** The pure state machine gains the window in `led()`, a multi-step `LedPlan` (pattern now + the pattern from each known future deadline), a `tick()` that expires timed state every loop, and a long memory of the first light press so late-processed second presses still escalate. The io task receives the plan under a spinlock and picks the step itself every 10 ms, so the LED stays right while the loop task is blocked in HTTPS.

**Tech Stack:** C++17, PlatformIO, Arduino-ESP32 2.0.17 (espressif32@6.13.0), FreeRTOS, Unity native tests (`pio test -e native`).

**Spec:** `docs/superpowers/specs/2026-10-28-led-upgrade-window-design.md`

## Global Constraints

- New pattern `LedPattern::UpgradeWindow`: lit when `nowMs % 600 < 300`.
- Window shown while `hasLastLightPress_ && (uint32_t)(nowMs - lastLightPressMs_) < lightUpgradeWindowMs` (60 000 ms in firmware).
- Priority in `led()`: EmergencyActive > UpgradeWindow > Confirmed > ConfigError > Offline > LightSent > Idle.
- First light press remembered until `nowMs - lastLightPressMs_ >= lightUpgradeWindowMs + kLightPressMemoryMs`, `kLightPressMemoryMs = 3600000` (private to `AlertMachine`). The upgrade decision stays press-to-press `< lightUpgradeWindowMs`.
- `kConfirmedWindowS = 180` in `include/config.h`.
- `kLedPlanSteps = 4`. Offsets are relative to `fromMs`; a `nowMs` earlier than `fromMs` (signed difference < 0) counts as offset 0.
- Plan crosses tasks only under `portMUX_TYPE` + `taskENTER_CRITICAL`/`taskEXIT_CRITICAL`, whole-struct copy both ways. No multi-byte `std::atomic`.
- `machine.tick(millis())` runs every loop iteration, after the gesture drain.
- Commits: author `frapsd <81354371+frapsd@users.noreply.github.com>`, no Co-Authored-By or any Claude/Anthropic mention.
- Native test run: `~/.local/bin/pio test -e native`; firmware: `~/.local/bin/pio run -e esp32dev`. Both must stay warning-free.

## Review Focus

1. Second tap pressed just before 60 s, released after, with `nextCommand()`/`tick()` in between → emergency (Task 2 test).
2. Loop blocked ~100 s across the window end → LED leaves the medium blink at press + 60 s (Task 1 `ledPlanAt` + Task 2 plan tests; manual test with internet cut).
3. Tap during Confirmed → medium blink for 60 s, then solid again until the confirmed end, then idle (Task 2 plan test).
4. `millis()` wrap: stale press/confirmed/light-sent state never reappears after ~49.7 days (Task 2 tick test, Task 1 wrap test).
5. io task reading a plan published mid-copy → never a mixed plan (Task 3 spinlock, review only).

---

### Task 1: Pattern and plan picker

**Files:**
- Modify: `lib/alert_core/src/alert_types.h` (append `UpgradeWindow` to `LedPattern`, last)
- Modify: `lib/alert_core/src/led_pattern.h`, `lib/alert_core/src/led_pattern.cpp`
- Modify: `src/main.cpp` (`ledName` case `"UpgradeWindow"`)
- Test: `test/test_led/test_main.cpp`

**Interfaces:**
- Produces:
  ```cpp
  constexpr size_t kLedPlanSteps = 4;
  struct LedPlan {
    uint32_t fromMs = 0;                       // millis() when computed
    uint8_t steps = 1;                         // 1..kLedPlanSteps
    LedPattern pattern[kLedPlanSteps] = {};    // pattern[i] from offsetMs[i] on
    uint32_t offsetMs[kLedPlanSteps] = {};     // ascending, offsetMs[0] == 0
  };
  LedPattern ledPlanAt(const LedPlan&, uint32_t nowMs);  // last step with offsetMs <= elapsed
  ```

- [ ] **Step 1: failing tests** in `test/test_led/test_main.cpp`:
  - `test_upgrade_window`: on at 0, 299; off at 300, 599; on at 600.
  - `test_plan_single_step`: `steps=1`, pattern[0]=Idle → Idle at fromMs and fromMs+10^6.
  - `test_plan_switches_at_offsets`: fromMs=1000, steps 3: (UpgradeWindow,0),(Confirmed,59000),(Idle,120000) → UpgradeWindow at 1000 and 59999; Confirmed at 60000 and 120999; Idle at 121000.
  - `test_plan_now_before_from`: fromMs=1000, same plan → UpgradeWindow at 990.
  - `test_plan_across_millis_wrap`: fromMs=0xFFFFFF00, steps 2: (UpgradeWindow,0),(Idle,0x200) → UpgradeWindow at 0x000000FF, Idle at 0x00000100.
- [ ] **Step 2:** run `pio test -e native -f test_led` → FAIL (compile: no `UpgradeWindow`/`LedPlan`).
- [ ] **Step 3:** implement enum value, `ledOn` case, `LedPlan`, `ledPlanAt`, `ledName` case.
- [ ] **Step 4:** run `pio test -e native` → all PASS; `pio run -e esp32dev` → builds.
- [ ] **Step 5:** commit `feat: LED upgrade-window pattern and timed LED plan`.

### Task 2: State machine

**Files:**
- Modify: `lib/alert_core/src/alert_machine.h`, `lib/alert_core/src/alert_machine.cpp`
- Test: `test/test_machine/test_main.cpp`

**Interfaces:**
- Consumes: `LedPlan`, `kLedPlanSteps`, `LedPattern::UpgradeWindow` (Task 1).
- Produces: `void tick(uint32_t nowMs);` (expire timed state, mark dirty on persisted change) and `LedPlan ledPlan(uint32_t nowMs, bool online) const;`.

`ledPlan`: step 0 = `led(nowMs, online)`; collect remaining time to each live deadline (window end `lastLightPressMs_ + lightUpgradeWindowMs` while the window is open, LightSent end while `age < lightSentShowMs`, Confirmed end while Confirmed and `age < confirmedWindowS*1000`; each deadline live only under the same age check `led()` uses, so no remaining time ever underflows); sort ascending; for each, `led(nowMs + remaining, online)`, appended only if different from the last appended step. `fromMs = nowMs`.

- [ ] **Step 1: failing tests** (kCfg as in the file: window 60 000, confirmed 600 s, LightSent 3 000):
  - Update `test_tap_queues_light_request`: `led(1200)`, `led(4199)`, `led(4200)` → UpgradeWindow; `led(60999)` → UpgradeWindow; `led(61000)` → Idle.
  - Update `test_rejected...` block at the tap at `t = 1220000`: `led(t, true)` → UpgradeWindow; `led(t, false)` → UpgradeWindow too; `led(t + 60000, true)` and `led(t + 60000, false)` → ConfigError (rest unchanged).
  - Update `test_tap_during_confirmed_is_a_light_request` (line ~194): `led(t + 20000)` → UpgradeWindow; add `led(t + 80000, true)` → Confirmed.
  - `test_window_priority`: tap at 0 → `led(0,false)` UpgradeWindow (over Offline); after `toConfirmed(m, 100000, "")` (ack at 110000) a tap at 120000 → UpgradeWindow at 120000, Confirmed at 180000; a Hold at 130000 instead → EmergencyActive.
  - `test_second_tap_released_after_window_upgrades`: tap press/processed 0; `drain(m,0)`, `onLightSent(Ok, 500)`; `drain(m, 60020)`; `m.tick(60020)`; Tap with `pressMs=59000` processed at 60300 → mode EmergencyPending.
  - `test_late_first_tap_shows_no_window`: Tap processed at 70000 with `pressMs=5000` → `led(70000,true)` Idle, still SendLight queued.
  - `test_press_memory_lasts_one_hour`: tap at 0; Tap pressed at 59000 processed at 3659999 after `tick(3659999)` → EmergencyPending. Fresh machine, same but processed at 3660000 after `tick(3660000)` → Idle (light request).
  - `test_press_memory_expires_before_wrap`: tap at 0; `tick(3660000)`; then a Tap with now = press = 10 (as after a `millis()` wrap, 10 - 0 < 60 000) → mode Idle, a light request (without the expiry it would alias to an emergency).
  - `test_tick_expires_confirmed`: `toConfirmed(m, 0, "")` (ack 10000); `tick(609999)` → Confirmed, no dirty; `tick(610000)` → Idle, `takeDirty()` true.
  - `test_led_plan_tap`: tap at 0, plan at 300 online → steps 2: (UpgradeWindow,0),(Idle,59700).
  - `test_led_plan_hides_light_sent_in_window`: tap 0, LightSent at 1200, plan at 1200 → steps 2: (UpgradeWindow,0),(Idle,58800).
  - `test_led_plan_window_then_confirmed`: `toConfirmed(m, 0, "")` (ack 10000), tap at 20000, plan at 20000 → (UpgradeWindow,0),(Confirmed,60000),(Idle,590000).
  - `test_led_plan_light_sent_after_window`: tap 0 offline (no drain); plan at 70000: send Ok at 70000 first → (LightSent,0),(Idle,3000).
  - `test_led_plan_four_steps`: `toConfirmed(m, 0, "")` (ack 10000), tap at 549000, drain + `onLightSent(Ok, 608000)`, plan at 608000 → (UpgradeWindow,0),(Confirmed,1000),(LightSent,2000),(Idle,3000).
  - `test_led_plan_emergency_single_step`: Hold at 0 → steps 1, EmergencyActive.
- [ ] **Step 2:** run `pio test -e native -f test_machine` → FAIL.
- [ ] **Step 3:** implement: `upgradeWindowOpen(nowMs)` private helper used by `led()`; priority per Global Constraints; memory expiry in `expireTimedState`; `tick`; `ledPlan`. Update the comment on `hasLastLightPress_`/expiry explaining the memory.
- [ ] **Step 4:** `pio test -e native` all PASS.
- [ ] **Step 5:** commit `feat: upgrade window on the LED, timed LED plan, tick; keep the first press until late taps are read`.

### Task 3: Firmware wiring

**Files:**
- Modify: `src/io_task.h`, `src/io_task.cpp`, `src/main.cpp`, `include/config.h`

**Interfaces:**
- Consumes: `AlertMachine::ledPlan`, `AlertMachine::tick`, `ledPlanAt` (Tasks 1-2).
- Produces: `void ioSetLedPlan(const LedPlan&);` replacing `ioSetPattern`.

- [ ] **Step 1:** io task: `LedPlan gPlan` + `portMUX_TYPE gPlanMux = portMUX_INITIALIZER_UNLOCKED`; `ioSetLedPlan` copies under the lock; the task copies under the lock each tick and lights `ledOn(ledPlanAt(plan, now), now)`. Initial `gPlan`: `pattern[0] = LedPattern::Offline` (a default `LedPlan` is Idle). `led_pattern.h` includes `<cstddef>`; `alert_machine.h` includes `led_pattern.h`.
- [ ] **Step 2:** main: `publishIo()` publishes `machine.ledPlan(millis(), netOnline())`, logs changes of step 0 as today; `setup()` publishes a one-step plan of `lastLed`; `loop()` calls `machine.tick(millis())` right after the gesture drain, before `persistIfDirty()`; `publishIo()` also right after `machine.onWallClock(...)`, so a Confirmed window settled by the wall clock reaches the LED before the next blocking send.
- [ ] **Step 3:** `kConfirmedWindowS = 180`.
- [ ] **Step 4:** `pio run -e esp32dev` builds with no warnings; `pio test -e native` PASS.
- [ ] **Step 5:** commit `feat: io task runs the LED plan; confirmed window 3 minutes`.

### Task 4: Documentation

**Files:** `README.it.md`, `README.md`, `CHANGELOG.md`, `docs/manual-tests.md`, `docs/superpowers/specs/2026-10-26-esp-alert-pushover-design.md` (pointer at the LED section and at "es. 10 minuti").

- [ ] **Step 1:** LED table: new row "Lampeggio medio (0,3 s acceso, 0,3 s spento) per 60 s dalla pressione breve | Richiesta leggera registrata: premere ancora entro questo tempo chiama l'emergenza" (and English). Priority list per Global Constraints. All "10 minuti/10-minute/10 min" for the confirmed window → 3. Buzzer intro sentence: the buzzer carries the LED's news except the "press again" window, which is LED-only. Roadmap line on the queued light request (`README.it.md` ~228, `README.md` ~173): reword, the window now covers its first 60 s. Limitations: the medium blink starts only when the loop processes the tap (late if a send is in progress).
- [ ] **Step 2:** CHANGELOG Unreleased: Added (window pattern), Changed (confirmed 3 min, priority), Fixed (second tap released after 60 s read as light request; stale timed LED while the loop is blocked; millis wrap expiry).
- [ ] **Step 3:** manual tests: test 1 (tap: medium blink 60 s, delivery = two beeps), test 2 (double tap), test 3 (tap in confirmed window: medium blink then solid), all 10-minute mentions → 3, new test: internet cut with Wi-Fi up, tap, medium blink ends at 60 s.
- [ ] **Step 4:** README test count matches `pio test -e native` output. Commit `docs: LED upgrade window, confirmed window 3 minutes`.
