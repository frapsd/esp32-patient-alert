# Buzzer Mute and Thinning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A 2.5 s hold during an emergency (at least 30 s old) mutes its reminders, a second hold unmutes; reminders thin out to every 2 minutes after 10 minutes.

**Architecture:** The state machine numbers emergencies; the loop publishes reminder + serial in one 16-bit atomic. `CueScheduler` (pure) owns mute state, the grace and press-edge rules, the "worse while muted" news and the three-phase rhythm. The news bitset widens to 16 bits for the new `Cue::Muted`.

**Tech Stack:** C++17, PlatformIO, Arduino-ESP32 2.0.17, FreeRTOS, Unity native tests.

**Spec:** `docs/superpowers/specs/2026-10-28-buzzer-mute-design.md`

## Global Constraints

- `Cue::Muted` appended after `EmergencyWaiting`; steps `{{1300, 250}, {800, 400}}`.
- `cueBit()` returns `uint16_t`, `firstPending(uint16_t)`, static_assert last cue `< 16`; io bitset `std::atomic<uint16_t>`.
- Rhythm: elapsed since the io task saw the emergency `< fastForMs` → `fastMs`; `< slowForMs` → `slowMs`; else `slowestMs`. Firmware: 5000 / 120000 / 30000 / 600000 / 120000; grace `kMuteGraceMs = 30000`.
- `CueScheduler(fastMs, fastForMs, slowMs, slowForMs, slowestMs, muteGraceMs)`; `CueEngine` same parameters.
- `CueInputs::emergencySerial` (`uint8_t`, meaningful only with `reminder != None`).
- `AlertMachine::emergencySerial()` (`uint8_t`): incremented in `onGesture` only when the call takes the machine from not-live to live. `AudioState::serial` = it.
- io publish: `gReminder` becomes `std::atomic<uint16_t>`: low byte reminder, high byte serial.
- Commits: author `frapsd <81354371+frapsd@users.noreply.github.com>`, no Claude/Anthropic mention.
- `~/.local/bin/pio test -e native` and `~/.local/bin/pio run -e esp32dev` warning-free.

## Review Focus

1. Anxious double hold right after "armed" → armed again, not muted.
2. Emergency ends and a new one starts with no `None` sample in between → new one not muted.
3. Hold while the acknowledgement is being processed (Confirmed queued) → armed path, Confirmed not misheard as muted.
4. Muted and the emergency stops getting out → one "not getting out" cue, still muted.
5. Unmute → reminder at once, then the current phase's rhythm.

---

### Task 1: Emergency serial

**Files:** `lib/alert_core/src/alert_machine.{h,cpp}`, `lib/alert_core/src/audio_policy.{h,cpp}`; tests `test/test_machine/test_main.cpp`, `test/test_audio/test_main.cpp`.

- [ ] **Step 1: failing tests**
  - `test_emergency_serial_counts_new_emergencies` (machine): 0 at start; Hold → 1; a Hold while live → still 1; after ack (via `toConfirmed` pattern) a Hold → 2; tap + tap within 60 s on a fresh machine → 1.
  - `test_emergency_serial_kept_on_reescalation` (machine): emergency awaiting ack, receipt `expired` → `EmergencyPending`, serial unchanged; same with receipt `Rejected`.
  - `test_audio_state_carries_serial` (audio): after a Hold `audioState(m, true).serial == m.emergencySerial()`.
- [ ] **Step 2:** run, see compile failure.
- [ ] **Step 3:** implement (`uint8_t emergencySerial_`, wraps naturally; not reset by `restore`).
- [ ] **Step 4:** all native tests pass. **Step 5:** commit `feat: number emergencies for the buzzer`.

### Task 2: Scheduler mute and thinning

**Files:** `lib/alert_core/src/buzzer_cue.{h,cpp}`; test `test/test_buzzer/test_main.cpp`.

Scheduler state added: `serial_`, `muted_`, `pressSerialValid_`, `pressSerial_`, `lastReminderIn_` (reminder seen last tick), `worseNews_`. Rules:
- Top of `decide`: reminder None → inactive, unmuted, `worseNews_` cleared. Reminder set and (inactive or `emergencySerial != serial_`) → active, `reminderSinceMs_ = now`, `serial_` updated, unmuted, `worseNews_` cleared. If muted and `lastReminderIn_ == EmergencyWaiting` and `reminder == Failed` → `worseNews_ = true`. Store `lastReminderIn_`.
- On `pressEdge`: `pressSerialValid_ = reminderActive_; pressSerial_ = serial_`.
- `emergencyRaised` → armed path as today. `holdFired` → toggle iff `reminderActive_ && pressSerialValid_ && pressSerial_ == serial_ && now - reminderSinceMs_ >= muteGraceMs && !confirming` where `confirming = pending == Confirmed || (!playerIdle && news_ == Confirmed)`. Mute: start `Muted` (news, no `dropPending`). Unmute: start `reminder` (restarts the interval). Otherwise armed path as today.
- Idle chain order: replay, pending, `worseNews_` (start `Failed` as news, clear), reminder only when not muted.

- [ ] **Step 1: failing tests** (helper `scheduler()` becomes `CueScheduler(5000, 120000, 30000, 600000, 120000, 30000)`; `kAllCues` gains `Cue::Muted`; mask test uses `uint16_t`):
  - `test_hold_mutes_after_grace`: reminder Waiting serial 1 from t=0 (start it), press edge at 40000, hold at 42500 → `Muted`, no `dropPending`; idle ticks to 200000 → no reminder starts.
  - `test_second_hold_unmutes`: after the above, press 50000, hold 52500 → starts `EmergencyWaiting`; next one not before 52500 + 5000 (phase fast, elapsed < 120000) → at 57499 None, 57500 Waiting.
  - `test_hold_within_grace_is_armed`: reminder from 0, press 10000, hold 12500 → `EmergencyArmed`.
  - `test_hold_pressed_before_emergency_is_armed`: no reminder; press 40000; reminder serial 1 appears 41000 (with reminderSince from 0 impossible; so start reminder at 0 on a *different* serial 1, then serial 2 appears at 41000) → hold 42500 → `EmergencyArmed`.
  - `test_hold_while_confirmed_queued_is_armed`: reminder from 0, press 40000, hold 42500 with `pending = Confirmed` → `EmergencyArmed`, `dropPending`.
  - `test_new_serial_clears_mute`: muted on serial 1; reminder stays Waiting but serial 2 at 100000 → reminder plays at 100000 (fresh emergency: interval since reminderSince) — assert a reminder starts at 100000 or within 5000.
  - `test_none_clears_mute`: muted; one tick with reminder None; reminder serial 1 again → reminders play.
  - `test_muted_worse_news_once`: muted with Waiting; reminder Failed → `Failed` starts once (when idle); later ticks to +200000 → nothing more.
  - `test_muted_better_is_silent`: muted with Failed; reminder Waiting → nothing.
  - `test_muted_still_beeps_and_hears_news`: muted; press edge → `Press`; pending Confirmed → `Confirmed`.
  - `test_reminder_thins_in_three_phases`: reminder from 0, always idle, step 10 ms ticks or targeted ticks: last reminder before 120000 at 115000, then next at 145000 (30 s), …, at 600000 phase switch: last at 595000, next at 715000 (120 s), not at 625000.
  - `test_muted_cue_is_audible_and_falls` (pitch/length via `cueSteps`): every step ≥ 130 ms, first freq > second.
- [ ] **Step 2:** run, see RED (compile).
- [ ] **Step 3:** implement; update the class comments (three phases, mute rules).
- [ ] **Step 4:** all native tests pass. **Step 5:** commit `feat: mute and thin out emergency reminders`.

### Task 3: Firmware wiring

**Files:** `src/io_task.{h,cpp}`, `include/config.h`, `src/main.cpp` (only if needed).

- [ ] **Step 1:** config: `kEmergencyChirpSlowForMs = 600000`, `kEmergencyChirpSlowestMs = 120000`, `kMuteGraceMs = 30000`, comment the three phases.
- [ ] **Step 2:** io: `gPendingCues` `uint16_t` (casts in `fetch_and`), `gReminder` → `std::atomic<uint16_t>` packed (low reminder, high serial), `in.reminder`/`in.emergencySerial` unpacked from one load; `CueEngine` gets the new constants.
- [ ] **Step 3:** build warning-free, tests pass. **Step 4:** commit `feat: io task publishes the emergency serial; reminder thinning constants`.

### Task 4: Documentation

**Files:** `README.md`, `README.it.md`, `CHANGELOG.md`, `docs/manual-tests.md`, `docs/superpowers/specs/2026-10-26-esp-alert-pushover-design.md`.

- [ ] Per the spec's Documentazione section: gesture row "Pressione lunga durante un'emergenza attiva da almeno 30 s → silenzia (o riattiva) i richiami del buzzer; nessun nuovo allarme", principle exception, buzzer table (Muted row, "armed" row incl. first 30 s of an emergency, reminder row three phases, worse-while-muted), Limitations (muting is not cancelling; reboot unmutes, rhythm restarts), test count, CHANGELOG Added/Changed, manual tests 2, 6, 7, 10, 11 + new mute test. Commit `docs: mute and thinning of emergency reminders`.
