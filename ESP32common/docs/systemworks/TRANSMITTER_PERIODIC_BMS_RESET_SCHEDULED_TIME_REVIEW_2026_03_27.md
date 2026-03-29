# Transmitter Investigation Report: Periodic BMS Reset (24h) and Scheduled-Time Design

**Date:** 2026-03-27  
**Scope:** ESPnowtransmitter2 transmitter code path for periodic BMS reset and design for scheduled (clock-time) resets persisted in transmitter NVS.

---

## Executive summary

1. The current **"Periodic BMS reset every 24h"** implementation is an **uptime-elapsed timer** based on `millis()`, not clock time.
2. The trigger condition is effectively: elapsed time since `lastPowerRemovalTime` >= 24h.
3. The process does **not reboot the transmitter firmware**. It performs a controlled sequence:
   - pause battery activity,
   - power-cycle BMS via `BMS_POWER` GPIO,
   - wait configured off-duration,
   - power on and warmup,
   - unpause.
4. A scheduled-time design (e.g. 02:30 daily) is feasible and low risk if implemented with:
   - NTP-backed absolute time when available,
   - fallback to current elapsed-24h mode when unsynced,
   - once-per-day guard state persisted to NVS.

---

## Findings: how the current 24h periodic reset works

## 1) Trigger model = elapsed uptime timer

In `comm_contactorcontrol.cpp`, periodic reset interval is hard-coded:

- `powerRemovalInterval = 24 * 60 * 60 * 1000` (24h)
- Periodic trigger check: `currentTime - lastPowerRemovalTime >= powerRemovalInterval`

This confirms it is elapsed-time based (`millis()`), not a fixed clock time.

**Source:**
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/communication/contactorcontrol/comm_contactorcontrol.cpp` (global constants and `handle_BMSpower()` state machine)

## 2) What actually gets stop/started

The reset sequence controls BMS power and pause state; it does not restart whole firmware.

Sequence in `handle_BMSpower()` + `start_bms_reset()`:

1. `start_bms_reset()` sets pause (`setBatteryPause(true, false, false, false)`) and timestamps start.
2. If contactors are emulator-controlled, it immediately cuts BMS power and enters powered-off state.
3. If contactors are BMS-controlled, it waits for safe low current before power cut.
4. Keeps BMS off for `datalayer.battery.settings.user_set_bms_reset_duration_ms`.
5. Powers BMS on, waits warmup (`bmsWarmupDuration = 3000 ms`).
6. Unpauses (`setBatteryPause(false, false, false, false)`) and returns to IDLE.

**Source:**
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/communication/contactorcontrol/comm_contactorcontrol.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/safety/safety.cpp` (`setBatteryPause()` behavior)

## 3) Safety behavior during reset

The code prevents unsafe drop-out under load when BMS controls contactors:

- Waits for 0 current, or low current window after delay.
- Aborts after timeout if still under load.

This is good and should be preserved in any scheduled-time redesign.

**Source:**
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/communication/contactorcontrol/comm_contactorcontrol.cpp`

## 4) Configuration and persistence currently in use

Periodic enable flag:

- Runtime flag: `periodic_bms_reset`
- Stored via modern settings manager under contactor category (`contactor` namespace/blob)
- Exposed via settings field `CONTACTOR_PERIODIC_BMS_RESET`

Relevant files:
- `settings_field_setters.cpp` (field handling)
- `settings_persistence.cpp` (NVS read/write blob + legacy key)
- `settings_manager.cpp` (`apply_runtime_static_settings()` maps saved setting to runtime global)
- `esp32common/espnow_transmitter/espnow_common.h` (field/category enums)

BMS off-duration:

- Runtime source: `datalayer.battery.settings.user_set_bms_reset_duration_ms`
- Loaded/stored via legacy `comm_nvm.cpp` key `BMSRESETDUR`

Note: there are two persistence paths in play (legacy `batterySettings` and newer `SettingsManager` category blobs). Startup order currently loads legacy first, then settings manager applies managed values.

---

## Direct answer to your question

> Does it just work off of the system timer and when it reaches 24 hours elapsed it starts the process?

**Yes.** Current periodic behavior is elapsed-time (`millis`) based, with a fixed 24h interval. It does **not** target a specific wall-clock time-of-day.

---

## Simple solutions for "specific time" behavior

You are correct: if the requirement is a specific time-of-day, some form of clock is required.

Below are practical options from simplest to most deterministic.

### Option 1 (simplest): keep pure elapsed 24h

- No clock dependency.
- Trigger stays as: `now_millis - last_reset_millis >= 24h`.
- Lowest complexity and lowest risk.
- Limitation: reset time drifts relative to wall-clock and power-cycle/reboot shifts the daily time.

### Option 2 (recommended simple hybrid): one-time clock alignment, then elapsed 24h cycle

This matches your idea exactly.

Behavior:

1. System boots in normal elapsed mode behavior.
2. If no valid clock exists, continue with default `24h elapsed` trigger.
3. When NTP succeeds for the first time, immediately compute `first_delay_ms` to the next target HH:MM.
4. Persist a one-time alignment state (for example `first_alignment_armed=true` + computed anchor metadata).
5. Trigger first reset using this computed one-shot elapsed delay.
6. After that first reset is completed successfully, mark alignment as consumed (`first_alignment_done=true`) and always return to pure `24h elapsed` cycle.

Benefits:

- Very simple runtime (after first event, existing code path is unchanged).
- Gives initial wall-clock alignment without needing daily calendar checks.
- NTP is only required once for first-delay calculation.
- If clock drops later, behavior remains stable because elapsed cycle continues.

Trade-offs:

- Not strict "always at HH:MM every day" (by design), because daily operation is elapsed-only after first aligned reset.
- If reboot occurs before first aligned reset has fired, implementation must define whether to recompute delay or resume stored one-shot state.

### Option 2 implementation clarification (finalized)

The intended behavior is now:

- **Primary mode** = elapsed timer.
- **NTP use** = one-time assist only, to calculate first restart delay.
- **Restart time setting persistence** = target restart time (HH:MM) is stored in transmitter NVS with other static settings.
- **No NTP available** = keep default 24h elapsed restart.
- **Where to calculate first delay** = at the end of the **first successful NTP sync** (whenever it happens in runtime).
- **Runtime decision model** = first alignment remains pending until first NTP success:
  - if NTP succeeds at boot, compute/arm immediately;
  - if NTP only succeeds later (for example 2 hours after boot), compute/arm at that moment.
- **If reboot + NTP available** = recompute the initial one-shot delay from persisted HH:MM target (new boot, new decision).
- **After first aligned restart** = permanently fall back to normal 24h elapsed schedule.

This keeps logic simple and robust while still allowing one wall-clock aligned first restart.

### Answer to policy question: should "first alignment" only be set initially?

**Yes, as a one-shot pending state (not startup-only):**

- keep first-alignment pending until the first successful NTP sync event,
- on that first NTP success, calculate and arm initial delay,
- after first aligned reset succeeds, mark alignment consumed and continue pure 24h elapsed.

## Suggested improvements for Option 2

1. **Persistent flags in NVS**
  - `bms_first_align_enabled` (user setting)
  - `bms_first_align_done` (runtime state persisted)
  - `bms_first_align_target_minutes` (HH:MM as 0..1439, persisted static setting)

2. **Compute only on first valid NTP sync**
  - Guard with `if (!bms_first_align_done && bms_first_align_enabled)`.
  - Perform computation from NTP callback/"sync successful" path.

3. **Static settings integration (transmitter NVS)**
  - Restart target time must be part of transmitter static settings payload and saved in transmitter NVS (same pattern as other hardware/static data).
  - Receiver UI edits should update this field through the existing settings pipeline, then transmitter persists and applies it.

4. **Safe anchor update policy**
  - Do not alter `lastPowerRemovalTime` until first aligned reset has been successfully completed.
  - On successful completion, set elapsed anchor to "now" and continue standard 24h behavior.

5. **Boot/reboot determinism**
  - Persist enough state so reboots before first aligned reset do not cause ambiguous behavior.
  - Required behavior: on every reboot, if NTP becomes valid and first alignment is still pending, recompute one-shot delay from persisted HH:MM target.
  - If NTP is unavailable after reboot, continue default 24h elapsed until NTP succeeds; then compute/arm first delay immediately at that first successful sync.

6. **Event logging improvements**
  - Add explicit events/messages:
    - "NTP first-align delay calculated"
    - "First aligned BMS reset executed"
    - "Switched to normal 24h elapsed mode"

7. **Failure handling**
  - If first aligned reset aborts due to load, keep alignment pending and retry via elapsed checks until success, then switch to normal 24h cycle.

## Thorough review: break cases and hardening actions

To reduce regression risk, the following edge cases should be explicitly handled in implementation.

1. **`millis()` rollover (~49.7 days)**
  - Keep all elapsed checks in unsigned subtraction form (`now - anchor >= interval`), which is rollover-safe.

2. **NTP flapping / multiple sync callbacks**
  - Prevent repeated re-arming with `bms_first_align_done` + `first_alignment_armed` guard.
  - Only first successful sync while pending may arm initial delay.

3. **Target time validation**
  - Validate `bms_first_align_target_minutes` on load/update (`0..1439`).
  - If invalid, disable first alignment and fall back to pure 24h elapsed.

4. **Negative or zero computed delay**
  - If computed delay is <= 0 (time already passed), roll to next day (`+24h`) before arming.

5. **Timezone / DST jumps**
  - For Option 2 this is low impact because wall-clock is only used once.
  - Recommendation: compute first delay in local-time policy, then immediately run pure elapsed mode.

6. **Concurrent reset requests**
  - Manual/remote/periodic/first-align triggers must all honor one shared guard:
    - only start when `bms_reset_status == BMS_RESET_IDLE`.

7. **Power-cycle during pending first alignment**
  - Keep pending state in NVS.
  - On reboot, if still pending, wait for first successful NTP and recompute delay.

8. **Feature disable while pending**
  - If user disables periodic reset or first-alignment feature, clear any armed one-shot alignment state immediately.

9. **No NTP for long periods**
  - Continue stable default 24h elapsed behavior without blocking operation.

10. **Abort due to load**
  - Keep first alignment pending after abort; retry safely later.
  - Do not mark `bms_first_align_done=true` unless a reset cycle actually completes.

11. **Dual persistence path risk (legacy + managed settings)**
  - Ensure one authoritative source for new first-alignment fields (managed settings/NVS blob path).
  - Avoid partial state split between `batterySettings` and `contactor` namespaces.

12. **Observability**
  - Add counters/flags in diagnostics so field testing can confirm:
    - pending/aligned/done states,
    - computed first delay,
    - reason for last reset trigger.

### Option 3 (full scheduled daily): evaluate clock every loop and fire once/day at HH:MM

- Deterministic daily time.
- Requires day tracking (e.g., epoch day) and schedule window handling.
- More logic and more edge cases than Option 2.

### Option 4 (coarse pseudo-clock, no NTP): user sets "hours until first reset" then 24h cycle

- No real time-of-day support.
- User approximates desired daily time manually.
- Very simple but operationally inconvenient.

## Practical recommendation

If you want a low-complexity improvement now, implement **Option 2** first:

- Add scheduled target HH:MM setting,
- Use clock only to calculate **first** delay,
- Then drop into existing elapsed 24h cycle.

This gives most of the user-visible benefit with minimal change to the proven reset state machine.

---

## Design proposal: move to fixed scheduled time (daily HH:MM)

## Proposed behavior

Add scheduled daily reset time in transmitter hardware/contactors settings, persisted in NVS:

- `bms_reset_schedule_enabled` (bool)
- `bms_reset_schedule_minutes` (uint16, 0..1439; local time minutes since midnight)
- optional `bms_reset_schedule_window_min` (default 5)

Runtime decision logic:

1. If scheduled mode enabled and time is synced (`TimeManager::get_unix_time() != 0`):
   - compute local minutes-of-day,
   - when current time enters schedule window and reset not already done today, request reset.
2. If time not synced:
   - fallback to existing elapsed-24h mode (safe backward compatibility).

Guard against repeated triggering:

- Persist `last_bms_reset_day_id` (e.g. epoch day) in NVS.
- Trigger once per day max for scheduled mode.

If scheduled moment arrives under load:

- Keep existing safety checks.
- Keep a `pending_scheduled_reset` flag and retry until safe (or until window timeout policy).

## Why this is robust

- Keeps current proven reset state machine unchanged.
- Uses existing time infra (`TimeManager` + ethernet/NTP utilities).
- Avoids hard dependency on clock sync by preserving elapsed fallback.

---

## Implementation impact map

## A) Shared settings protocol / field IDs

Add new contactor field IDs in:

- `esp32common/espnow_transmitter/espnow_common.h`

Example additions after existing field 5:
- `CONTACTOR_BMS_RESET_SCHEDULE_ENABLED = 6`
- `CONTACTOR_BMS_RESET_SCHEDULE_MINUTES = 7`

## B) Transmitter settings manager + NVS

Update:

- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_manager.h`
  - add members/getters for schedule fields.
- `.../settings_field_setters.cpp`
  - add setters + range validation (`0..1439`).
- `.../settings_persistence.cpp`
  - extend `ContactorSettingsBlob`, schema bump, legacy fallback keys.
- `.../settings_manager.cpp`
  - map new managed fields into runtime globals in `apply_runtime_static_settings()`.

## C) Transmitter contactor/BMS reset runtime

Update:

- `.../battery_emulator/communication/contactorcontrol/comm_contactorcontrol.cpp`
- `.../battery_emulator/communication/contactorcontrol/comm_contactorcontrol.h`

Add schedule check helper using wall-clock time and once/day guard, while preserving existing reset sequence and safety checks.

## D) Receiver hardware config UI/API

Update:

- `espnowreceiver_2/lib/webserver/pages/hardware_config_page_content.cpp`
  - add schedule enable + time input.
- `.../hardware_config_page_script.cpp`
  - add field mapping (category 8, new field IDs), load/save handling.
- `.../api/api_settings_handlers.cpp`
  - local cache update for new fields.

## E) Optional migration cleanup (recommended)

Current code still has legacy `PERBMSRESET`/`BMSRESETDUR` path in `comm_nvm.cpp`. Consider converging all contactor/BMS-reset controls into one managed settings path to avoid dual-source drift.

---

## Suggested acceptance criteria

### Stage 1 (Option 2: first-align once, then elapsed 24h)

1. Existing periodic reset behavior remains backward compatible.
2. First-alignment target HH:MM is saved in transmitter NVS and survives reboot.
3. If NTP is available at boot, first delay is computed and armed.
4. If NTP is unavailable at boot but arrives later, first delay is computed and armed at first successful sync.
5. If NTP never arrives, system continues pure 24h elapsed behavior.
6. After first successful aligned reset, system always runs pure 24h elapsed behavior.
7. Safety behavior remains unchanged (no forced drop under load).
8. Event log differentiates first-aligned, elapsed periodic, manual/remote, and aborted resets.

### Stage 2 (optional future: full daily HH:MM)

1. Once-per-day guard prevents duplicate same-day scheduled resets.
2. With valid clock, reset occurs in configured daily schedule window.
3. Without valid clock, fallback policy is explicit and deterministic.

---

## Implementation task schedule (Option 2 delivery plan)

This schedule is aligned with repository standards in `esp32common/docs/project guidlines.md`:

- one responsibility per module,
- shared protocol updates in `esp32common/` first,
- deterministic state handling,
- remove superseded/duplicate paths,
- build + validation gates at each step,
- update docs with behavior changes.

## Delivery approach

- **Method**: phased, test-gated implementation (design -> protocol -> transmitter -> receiver -> integration -> hardening).
- **Target scope**: Stage 1 only (Option 2 one-shot first alignment then elapsed 24h).
- **Change control**: no behavior-breaking changes to existing periodic reset safety state machine.

## Work breakdown structure (WBS)

### Phase 0 — Baseline and design freeze (0.5 day)

**Tasks**
1. Freeze Option 2 state model (`pending`, `armed`, `done`).
2. Freeze NVS key/schema decisions for transmitter.
3. Freeze receiver UI/API field definitions.

**Deliverables**
- Finalized state transition note in this document.
- Field ID allocation for contactor settings extension.

**Exit criteria**
- No open logic ambiguities (boot/late NTP/reboot/abort cases resolved).

### Phase 1 — Shared protocol update (0.5 day)

**Tasks**
1. Add new contactor field IDs in shared protocol enums.
2. Keep backward compatibility for older receiver/transmitter pairs (unknown fields ignored safely).

**Files (expected)**
- `esp32common/espnow_transmitter/espnow_common.h`

**Deliverables**
- Updated protocol constants committed.

**Exit criteria**
- Transmitter and receiver compile with shared header update.

### Phase 2 — Transmitter settings/NVS integration (1.0 day)

**Tasks**
1. Add managed settings members/getters for:
  - first-align enable,
  - first-align target minutes,
  - runtime pending/done state (persisted).
2. Extend contactor settings persistence blob + schema bump + migration fallback.
3. Add validation (`0..1439`) and safe defaults.
4. Map settings to runtime globals in one place (`apply_runtime_static_settings()`).

**Files (expected)**
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_manager.h`
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_manager.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_field_setters.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_persistence.cpp`

**Deliverables**
- NVS-backed fields available through existing settings pipeline.

**Exit criteria**
- Reboot persistence verified for new fields.

### Phase 3 — Transmitter runtime logic (1.5 days)

**Tasks**
1. Implement first-alignment pending/arming logic in BMS reset runtime path.
2. Hook first-delay computation to first successful NTP sync event.
3. Ensure single-arm semantics under NTP flapping.
4. Preserve existing safety behavior and `BMS_RESET_*` state machine.
5. Mark alignment `done` only after a successful aligned reset completion.

**Files (expected)**
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/communication/contactorcontrol/comm_contactorcontrol.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/communication/contactorcontrol/comm_contactorcontrol.h`
- (if needed) `ESPnowtransmitter2/espnowtransmitter2/src/network/time_manager.cpp/.h`

**Deliverables**
- Functional first-alignment runtime behavior.

**Exit criteria**
- All Stage 1 acceptance criteria pass in bench tests.

### Phase 4 — Receiver UI/API integration (1.0 day)

**Tasks**
1. Add UI fields for first-align enable + target time.
2. Extend settings field map and save/load plumbing.
3. Extend API-side local cache mapping for new fields.

**Files (expected)**
- `espnowreceiver_2/lib/webserver/pages/hardware_config_page_content.cpp`
- `espnowreceiver_2/lib/webserver/pages/hardware_config_page_script.cpp`
- `espnowreceiver_2/lib/webserver/api/api_settings_handlers.cpp`

**Deliverables**
- Editable and persisted configuration from receiver web UI.

**Exit criteria**
- UI round-trip to transmitter NVS verified.

### Phase 5 — Integration, reliability hardening, and cleanup (1.0 day)

**Tasks**
1. Add explicit events/logging for alignment state transitions.
2. Validate edge cases from "break cases and hardening actions" section.
3. Remove any duplicate/obsolete interim code introduced during implementation.
4. Update architecture/review docs with final behavior.

**Deliverables**
- Hardened release candidate for Option 2.

**Exit criteria**
- Full validation checklist complete, no known critical edge-case gaps.

## Validation and quality gates (industry-standard)

### Coding and architecture gates

1. **Single-responsibility changes** per module.
2. **No ISR misuse** (no heavy logic in callbacks).
3. **Deterministic state transitions** documented and testable.
4. **Backward compatibility** preserved for existing periodic reset behavior.

### Build/test gates

1. Transmitter build passes:
  - `pio run` (transmitter project).
2. Receiver build passes:
  - `pio run -e lilygo-t-display-s3_tft` (receiver project).
3. Static checks:
  - no unresolved symbols,
  - no dead declarations from refactor.

### Functional test matrix (minimum)

1. **Boot + immediate NTP success** -> first delay computed and armed.
2. **Boot + delayed NTP success (e.g., +2h)** -> first delay computed at first successful sync.
3. **No NTP entire run** -> pure 24h elapsed behavior maintained.
4. **Reboot before first aligned reset** -> pending state survives; recompute on first NTP success.
5. **Aligned reset abort (load not safe)** -> pending retained; retry later; not marked done.
6. **Post-success path** -> `done=true`, pure elapsed 24h cycle resumes.

### Documentation and traceability gates

1. Update this systemworks report when implementation completes.
2. Update transmitter architecture notes if runtime/service behavior changes.
3. Record removed legacy/duplicate code paths (mandatory cleanup policy).

## Risk register (short form)

1. **Dual source-of-truth risk (legacy vs managed settings)**
  - Mitigation: keep new fields only in managed settings path.
2. **NTP callback race / repeated arm**
  - Mitigation: explicit pending/armed/done guards.
3. **Behavior drift after reboot**
  - Mitigation: persisted alignment state + deterministic recompute rules.

## Proposed timeline summary

- Total engineering effort: **~5.5 working days**
- Recommended execution: **single sprint** with mid-sprint integration checkpoint.

Milestones:
1. M1 (Day 1): Protocol + NVS model complete.
2. M2 (Day 3): Transmitter runtime complete and bench-tested.
3. M3 (Day 4): Receiver UI/API complete.
4. M4 (Day 5-6): Integration/hardening/docs/sign-off.

---

## Recommendation

Proceed in **two stages**:

### Stage 1 (simple, fast, low risk)

- Implement Option 2: clock-assisted first alignment, then elapsed 24h cycle.
- Reuse existing periodic reset state machine unchanged after first trigger.

### Stage 2 (optional, if strict wall-clock behavior is needed)

- Upgrade to full once-per-day HH:MM scheduling with day-guard persistence.

Current long-term hybrid target remains:

- Scheduled wall-clock reset as primary mode,
- elapsed 24h fallback when unsynced,
- persisted once/day guard.

This gives deterministic daily restart timing without sacrificing resilience when network time is temporarily unavailable.

---

## Addendum (2026-03-29): Simplified UX + automatic summer/winter re-alignment

## User-experience objective (confirmed)

The hardware page should be reduced to a simple mental model:

1. `Periodic BMS reset every 24h` (checkbox)
2. `Reset target time` (HH:MM)

And nothing else user-facing for alignment internals.

This means:

- `First reset align to time (NTP once)` must be hidden from UI.
- Alignment arming/consumption remains internal runtime behavior.
- If periodic reset is disabled, target time control is hidden/disabled.

## Recommended receiver UI behavior

### Visibility/interaction rules

1. Show `Reset target time` only when `Periodic BMS reset every 24h` is checked.
2. Use a native `HH:MM` input (`input type="time"`, minute precision).
3. Persist target time even when hidden (so re-enabling periodic restores last chosen value).
4. Do not expose internal enable/armed/done flags.

### Validation rules

- Accept `00:00` through `23:59`.
- Transmit/store as minutes `0..1439`.
- If empty/invalid at submit, reject save with clear message and keep previous valid value.

## Runtime behavior recommendation (internal only)

Keep Option 2 core unchanged:

1. After boot, run elapsed 24h behavior as baseline.
2. On first valid NTP sync, compute delay to next target HH:MM and arm one-shot first alignment.
3. After successful aligned reset, drop back to pure elapsed 24h cycle.

Additionally, add automatic re-alignment on clock-offset changes (DST summer/winter).

## DST/summer-winter change handling (simple mechanism)

### Problem

With Option 2, once first alignment is consumed, runtime stays elapsed-only. On DST shift (typically $\pm 60$ minutes), elapsed-only cadence drifts relative to local target time.

### Recommended mechanism

Use NTP sync events to detect local UTC-offset changes and request a one-shot re-alignment.

#### Internal state (transmitter)

- `last_applied_utc_offset_min` (persisted runtime state)
- `realign_pending_due_to_tz_change` (volatile flag)

#### Detection

At each successful NTP sync:

1. Compute current local UTC offset (minutes).
2. If no baseline exists, store it and continue.
3. If offset changed (preferably exact $\pm 60$ min, optionally any non-zero delta):
  - set `realign_pending_due_to_tz_change = true`
  - clear first-align consumed state for this cycle only
  - arm one-shot delay to next configured HH:MM

#### Execution

1. Execute reset using existing safe `BMS_RESET_*` state machine.
2. On successful completion:
  - clear `realign_pending_due_to_tz_change`
  - update `last_applied_utc_offset_min`
  - return to pure elapsed 24h cadence

#### Safety and failure policy

- If reset aborts due to load, keep re-alignment pending and retry using normal guard logic.
- Never bypass existing contactor/load safety checks.
- If NTP unavailable, do nothing special; continue elapsed 24h behavior.

## Why this meets your simplicity requirement

- User config remains only two decisions: periodic on/off and target HH:MM.
- No user exposure to first-align plumbing.
- DST correction is automatic when NTP is present.
- Existing proven reset state machine remains authoritative for safety.

## Implementation recommendations (targeted)

### Receiver

1. Remove/hide first-align checkbox from hardware page.
2. Replace minutes integer field with HH:MM time field.
3. Bind time row visibility to periodic checkbox state.
4. Keep existing protocol payload as minutes `0..1439` (no wire change required).

### Transmitter

1. Treat first-align enable as internal policy derived from periodic mode (and valid target time).
2. Add offset-change detection at NTP-sync handling point.
3. Persist `last_applied_utc_offset_min` in runtime/NVS state.
4. Re-arm one-shot alignment automatically on detected DST offset change.

## Acceptance criteria for this addendum

1. Hardware UI shows target time only when periodic reset is enabled.
2. Target time is edited and displayed as HH:MM.
3. No first-align control is visible to the user.
4. On DST change with NTP available, system performs one automatic re-alignment cycle, then returns to elapsed 24h.
5. Without NTP, system remains stable on elapsed 24h with no extra failures.

## Final recommendation

Adopt this simplified UI + internal DST re-alignment policy. It preserves the original low-risk Option 2 architecture while making user interaction straightforward and robust across summer/winter time changes.

---

## Addendum (2026-03-29B): NVS guarantee for target time + efficient DST strategies

## Mandatory storage requirement (confirmed)

The `Reset target time` must be persisted in **transmitter NVS** as authoritative state.

### Required persisted fields

1. `contactor_bms_first_align_target_minutes` (`0..1439`) — mandatory user setting.
2. `contactor_periodic_bms_reset` (bool) — feature enable state.

### Requirement statement

- Receiver UI may display/edit HH:MM, but transmitter NVS is source of truth.
- After transmitter reboot, target time must be restored from NVS without receiver re-send.
- If receiver is offline, transmitter still executes configured behavior.

## DST handling goal refinement

You requested avoiding a check on every NTP execution. Agreed.

Target behavior:

1. On first successful NTP sync, determine next DST boundary.
2. Persist boundary metadata.
3. Perform low-frequency checks (daily or coarse interval), not per NTP loop.
4. Re-arm one-shot re-alignment only when boundary is crossed.

## Preferred approach (A): first-sync compute next DST epoch, then daily gate check

### How it works

On first valid NTP sync:

1. Compute current local offset.
2. Compute next DST transition epoch (`next_dst_change_epoch`).
3. Persist:
   - `last_applied_utc_offset_min`
   - `next_dst_change_epoch`
   - optional `dst_realign_pending` flag.

Runtime:

- Check once per day (or at boot + daily tick):
  - if `now_epoch >= next_dst_change_epoch`:
    1. refresh offset via current localtime rules,
    2. if offset changed, arm one-shot alignment,
    3. compute/persist the following DST boundary.

### Why this is efficient

- No per-NTP-loop inspection.
- One coarse daily comparison against a persisted epoch.
- Deterministic and easy to test.

## Two other simple alternatives (minimum)

### Approach B: weekly coarse offset audit (scheduled wall-clock check)

Instead of tracking exact next boundary epoch:

1. Persist `last_applied_utc_offset_min`.
2. Run a weekly check at a fixed local time (e.g., Sunday 03:30).
3. Compare current offset to persisted offset.
4. If changed, arm one-shot realignment and update persisted offset.

Pros:

- Very simple logic.
- No DST rule calculation required.

Trade-offs:

- Up to 7 days late in detecting shift.

### Approach C: monthly offset audit + immediate boot check

1. Persist `last_applied_utc_offset_min` and `last_dst_audit_day_id`.
2. At boot, always perform one offset comparison (if NTP valid).
3. During runtime, perform one audit monthly (or every 14 days).
4. If offset changed, arm one-shot alignment and update offset.

Pros:

- Minimal periodic activity.
- Handles long uptime and reboot cases.

Trade-offs:

- Detection latency depends on audit period.

### Approach D: timezone-rule deterministic transition table (yearly precompute)

1. On first sync of each year, precompute 1-2 local DST transition epochs for that year.
2. Persist them in NVS.
3. Check only against those epochs daily.

Pros:

- Precise and still very low frequency.
- No repeated expensive rule resolution.

Trade-offs:

- Slightly more code than A/B/C.

## Recommendation ranking

1. **A (preferred): first-sync next-transition epoch + daily gate check**.
2. D: yearly precompute table + daily gate.
3. B: weekly offset audit.
4. C: monthly/fortnightly audit + boot check.

## Suggested implementation decision

Adopt **Approach A** now because it directly matches your proposal and keeps runtime simple.

Implementation notes:

- keep checks in existing low-frequency control path (daily gate),
- do not add a fast periodic task,
- trigger realignment once, then resume elapsed 24h cycle.

## Acceptance criteria (for this update)

1. `Reset target time` survives transmitter reboot via NVS.
2. DST logic does not run on every NTP loop.
3. DST transition causes exactly one realignment trigger (subject to safety guards).
4. After successful DST realignment, system returns to standard elapsed 24h behavior.

---

## Addendum (2026-03-29C): Validation of 4-DST precompute strategy + ESP-NOW structure check

## Requested strategy (restated)

1. On first successful NTP sync, compute next **4 DST transition epochs** and store in transmitter NVS.
2. At each daily periodic BMS reset decision point, check whether the next DST transition occurs within the next 24h.
3. If yes, arm one-shot re-alignment, consume that DST transition entry, and continue normal operation.
4. On subsequent NTP sync, if fewer than 4 entries remain, append the next future transition to refill to 4.

## Validity assessment

This strategy is **valid** and operationally sound for the intended low-frequency design.

### Why it is valid

- Runtime cost is very low (daily comparison against one epoch).
- Works without continuous NTP availability.
- Provides approximately 2 years of DST coverage in typical twice-yearly DST regions.
- Keeps first-alignment/DST logic internal and user-invisible.

### Recommended internal model

Persist in transmitter NVS (internal namespace, not user-facing):

- `dst_transition_epoch_queue[4]` (sorted ascending UTC epoch seconds)
- `dst_transition_count` (0..4)
- `last_applied_utc_offset_min`

Behavior:

1. If queue empty and NTP valid -> seed 4 transitions.
2. Daily gate: if `next_transition - now <= 24h` and `next_transition >= now`, arm re-alignment.
3. After successful re-aligned reset -> pop head entry.
4. Next time NTP is valid -> refill queue back to 4 future entries.

## ESP-NOW structure requirement check (for new user NVS field)

For `Reset target time` persistence (user-configurable), ESP-NOW structure should include:

1. Shared field ID in [esp32common/espnow_transmitter/espnow_common.h](esp32common/espnow_transmitter/espnow_common.h)
  - `CONTACTOR_BMS_FIRST_ALIGN_TARGET_MINUTES`.
2. Receiver field map uses category `SETTINGS_CONTACTOR` + this field ID.
3. Transmitter settings setter/validation persists `0..1439` into contactor NVS blob.
4. API/cache types expose the same field for load/save round-trip.

Important: no new ESP-NOW payload struct is required for DST queue internals, because DST entries are transmitter-internal runtime metadata (not end-user settings).

## Issues to highlight

### 1) Not all regions follow 2 transitions/year

- Some regions have no DST, suspended DST, or policy changes.
- Mitigation: queue refill logic must always derive future transitions from current timezone rules at each NTP refill; do not hardcode 2/year.

### 2) Timezone configuration changes by user/firmware

- If timezone string/rules change, stored queue may become invalid.
- Mitigation: invalidate queue on timezone config/version change and reseed on next NTP.

### 3) Boundary timing and duplicate triggers

- A 24h look-ahead can be true for multiple daily checks if not guarded.
- Mitigation: add `dst_realign_pending`/`dst_entry_consumed` guard so one transition causes one re-alignment attempt chain.

### 4) Reset abort under load

- If realignment reset is triggered near transition but aborts, queue entry must not be removed until success.
- Mitigation: pop DST entry only after successful reset completion.

### 5) Multi-year no-NTP operation beyond queue horizon

- 4 entries may eventually exhaust.
- Mitigation: once queue empty, continue elapsed 24h safely; refill when NTP returns.

## Recommended acceptance criteria for this strategy

1. On first NTP sync, exactly 4 future DST transitions are stored in NVS (if available for timezone).
2. Daily gate detects transition within 24h and arms exactly one re-alignment sequence.
3. DST entry is removed only after successful re-aligned reset completion.
4. On later NTP sync, queue is refilled to 4 future entries.
5. `Reset target time` remains persisted and recoverable from transmitter NVS across reboot.

## Conclusion

Your proposed approach is valid, efficient, and preferable to per-NTP-loop checks. The main safeguards needed are queue invalidation on timezone-rule changes, duplicate-trigger protection, and pop-on-success semantics.

---

## Addendum (2026-03-29D): No-DST regions + optional timezone/alignment fallback policy

## Required policy update (confirmed)

Timezone and alignment are **optional enhancements**. The baseline behavior must always be available.

### Hard fallback rules

1. If timezone is missing/invalid -> run pure elapsed 24h reset.
2. If NTP is unavailable/unsynced -> run pure elapsed 24h reset.
3. If region has no DST transitions -> set DST queue empty and run pure elapsed 24h reset.
4. If first-alignment cannot be computed -> do not block reset behavior; continue elapsed 24h.

### DST queue rule for no-DST regions

- On NTP seed/refill, if timezone rules produce zero future transitions:
  - store `dst_transition_count = 0`,
  - clear queue values,
  - continue standard elapsed 24h cycle.

No retries or synthetic DST entries are needed.

## Recheck: outlying cases and proposed solutions

### Outlier 1: timezone configured but malformed

Risk:
- Transition computation fails or produces undefined behavior.

Solution:
- Validate timezone input at load/apply.
- If invalid, mark timezone unavailable and force elapsed 24h fallback.
- Emit one warning event/log (`TZ_INVALID_FALLBACK_24H`).

### Outlier 2: timezone changed after queue already seeded

Risk:
- Existing queue no longer reflects new region rules.

Solution:
- On timezone change detection, invalidate queue immediately (`count=0`).
- Re-seed queue only after next valid NTP sync.
- Until reseed, use elapsed 24h fallback.

### Outlier 3: DST transition inside next 24h but reset aborts under load

Risk:
- Transition is consumed too early and missed.

Solution:
- Do not pop queue entry until reset completes successfully.
- Maintain `dst_realign_pending` and retry safely under existing guards.

### Outlier 4: NTP returns wrong time briefly, then corrects

Risk:
- False re-alignment arming.

Solution:
- Require stable time criteria before seeding/refilling queue (e.g., two consistent syncs or minimum sync age).
- If stability check fails, keep elapsed 24h fallback.

### Outlier 5: no DST now, DST rules introduced later by region policy

Risk:
- Permanent empty queue unless NTP reseed path revisits transitions.

Solution:
- At each successful NTP refill opportunity, recompute future transitions from current rules even when prior queue was empty.
- If transitions now exist, seed queue and proceed normally.

### Outlier 6: queue exhausted after long no-NTP period (>2 years)

Risk:
- No more DST-aware realignment available.

Solution:
- When queue exhausts, continue elapsed 24h (safe baseline).
- On next NTP availability, reseed queue to 4 entries.

### Outlier 7: repeated daily checks trigger duplicate arm near boundary

Risk:
- Multiple arm attempts for same transition.

Solution:
- Add idempotent guard by transition epoch (`armed_for_epoch`).
- Only one active arm per queued transition.

## Final operational rule set (authoritative)

1. **Baseline always on:** elapsed 24h periodic reset.
2. **Optional enhancements:** target-time alignment + DST queue logic only when prerequisites are valid.
3. **No prerequisites, no problem:** automatically stay in elapsed 24h mode.
4. **Safety first:** existing reset/load/contactors safety state machine remains the final gate.
