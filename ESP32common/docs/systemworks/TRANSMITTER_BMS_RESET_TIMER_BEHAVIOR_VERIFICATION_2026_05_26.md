# Transmitter BMS Reset Timer Behavior Verification (2026-05-26)

## Scope
Verify whether transmitter runtime behavior matches this model:
1. Default periodic reset is elapsed 24h from startup/reset anchor.
2. If NTP/time becomes available and a reset target time is configured, the next reset is re-aligned to that target time.
3. After that aligned reset, cadence continues every 24h.

---

## Code paths reviewed
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/communication/contactorcontrol/comm_contactorcontrol.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_manager.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_field_setters.cpp`

---

## End-to-end summary

The transmitter behaves like a simple 24-hour reset timer with alignment tweaks when clock time context is available:

1. **Boot**: the elapsed reset counter starts from the boot/reset anchor.
2. **No time yet**: if NTP is not available, the next reset remains a plain 24-hour elapsed reset.
3. **NTP arrives + target time present**: the code recalculates the next reset so it lands on the requested local target time.
4. **After that reset completes**: the 24-hour counter starts again from the new reset moment.
5. **UTC offset changes later**: the code re-arms the alignment so the next reset can be nudged back to the configured target time again.

### Figure 1: boot with no NTP
- Boot at **12:00**
- No NTP/time available
- Next reset occurs at **12:00 the next day**

### Figure 2: boot with NTP and target time
- Boot at **12:00**
- NTP becomes available
- Reset target time is **07:00**
- The transmitter recalculates the next reset to **07:00**
- After that reset, the 24-hour timer restarts from **07:00** and the next reset is **07:00 the following day**

### Figure 3: UTC offset change later
- Current aligned schedule is based on local time
- UTC offset changes by **+1 hour** or **-1 hour**
- The transmitter re-arms alignment using the current local time and the configured target time
- Result: the next reset is nudged back toward the same local target time instead of drifting

---

## Findings

### Quick answer
Yes. In current transmitter runtime, UTC offset changes are detected and the reset alignment delay is automatically recalculated/re-armed.

Also verified: `Reset target time` is actively used in runtime delay calculation (`target_seconds = bms_first_align_target_minutes * 60`).

### 1) 24h elapsed baseline exists
Confirmed.

`comm_contactorcontrol.cpp` defines:
- `powerRemovalInterval = 24 * 60 * 60 * 1000`
- periodic due condition: `elapsed_due = (currentTime - lastPowerRemovalTime >= powerRemovalInterval)`

This is the elapsed fallback schedule.

### 2) NTP target-time re-alignment exists
Confirmed.

`comm_contactorcontrol.cpp` has `arm_alignment_to_target_from_ntp(...)`:
- reads local time from NTP-backed epoch (`TimeManager::instance().get_unix_time()` + `localtime_r`)
- computes delay to configured `bms_first_align_target_minutes`
- arms one-shot alignment (`firstAlignArmed`, `firstAlignDelayMs`, `firstAlignAnchorMs`)

At runtime:
- aligned due condition: `aligned_due = firstAlignArmed && (currentTime - firstAlignAnchorMs >= firstAlignDelayMs)`
- if true, it triggers reset before elapsed fallback.

Important scope note:
- `Reset target time` is used for **alignment events** (initial alignment and offset-change re-alignment).
- After alignment is consumed, runtime continues with **pure elapsed 24h** cadence until another re-alignment event occurs.
- Therefore, target time is **not** used to force every single daily reset to the same wall-clock HH:MM.

### 3) Post-alignment behavior returns to 24h elapsed cycle
Confirmed.

After successful aligned reset, code clears alignment arm and logs:
- `"initial alignment consumed, continuing with pure elapsed 24h cycle"`

Then normal 24h elapsed logic continues via `elapsed_due`.

### 4) Reset actually halts BMS briefly
Confirmed.

`start_bms_reset()`:
- calls `setBatteryPause(true, ...)`
- powers BMS off (`bms_power_off()`), either immediately or after wait-for-safe-current state

`handle_BMSpower()` then:
- keeps BMS off for `user_set_bms_reset_duration_ms`
- powers on
- waits `bmsWarmupDuration` (3s)
- unpauses (`setBatteryPause(false, ...)`)

So this is a real short interruption/power-cycle, not just a logical flag.

---

## Important nuance vs expected model

### A) `bms_first_align_enabled` appears not to gate alignment logic
`SettingsManager::apply_runtime_static_settings()` assigns:
- `bms_first_align_enabled = contactor_bms_first_align_enabled_`

However, in `comm_contactorcontrol.cpp`, alignment arming checks `periodic_bms_reset` but does **not** check `bms_first_align_enabled` in the arming functions shown.

Implication: if periodic reset is enabled and time is available, alignment logic may arm regardless of the first-align enable setting.

For the simple model you described, this means the target-time alignment behavior is already present, but the dedicated first-align enable flag is not currently acting as a runtime guard in the code path reviewed.

### B) UTC-offset handling is active (not a fixed +1h buffer strategy)
Verified in `comm_contactorcontrol.cpp`:
- `maybe_arm_offset_change_realign(...)` compares cached UTC offset and current offset.
- On detected change, runtime logs "UTC offset change detected (...)" and calls `arm_alignment_to_target_from_ntp(...)` with reason `OFFSET_CHANGE`.
- This path automatically recomputes delay-to-target using current local time and configured target minutes.

This means current runtime behavior is active offset-change re-alignment, not a passive "set target one hour later and leave it" approach.

### C) No explicit "+1 hour" target adjustment is present
No code path was found that modifies configured target time by `+60` minutes (or equivalent) as a DST simplification mechanism.
Current logic computes delay directly from local clock time to configured target:
- `current_seconds = hh*3600 + mm*60 + ss`
- `target_seconds = bms_first_align_target_minutes * 60`
- `delta_seconds = target_seconds - current_seconds` (wrap to next day if needed)

### D) Double-check: exactly what changes timer/alignment anchors
Confirmed against `comm_contactorcontrol.cpp`:

1. **24h elapsed counter anchor (`lastPowerRemovalTime`) changes on reset events**
	- Starts at `0` on boot (so first elapsed trigger is effectively ~24h after boot if no alignment fires first).
	- Set in `start_bms_reset()` when a reset sequence begins.
	- Set again when BMS is actually powered off in `BMS_RESET_WAITING_FOR_PAUSE -> BMS_RESET_POWERED_OFF` transition.

2. **Alignment delay is recalculated/armed when NTP time becomes usable**
	- `maybe_arm_initial_alignment_from_ntp(...)` calls `arm_alignment_to_target_from_ntp(...)`.

3. **Alignment delay is recalculated/armed again on UTC offset change**
	- `maybe_arm_offset_change_realign(...)` detects offset delta and re-arms via `arm_alignment_to_target_from_ntp(...)`.

So your summary is correct in intent: reset events restart elapsed timing, and alignment can be recalculated on NTP acquisition and UTC-offset change.
Minor implementation detail: disabling periodic reset also clears any armed alignment state.

### E) What happens if the reset target time is changed?
The runtime setting is updated immediately when the user changes `Reset target time`:
- `SettingsManager::apply_runtime_static_settings()` copies the new value into `bms_first_align_target_minutes`
- the contactor settings setter saves the new value and applies runtime settings right away

Behavior depends on whether alignment is already armed:

1. **If alignment is not yet armed**
	- the next NTP alignment calculation will use the new target time immediately
	- example: boot at **12:00**, target changed from **07:00** to **08:30** before NTP alignment fires
	- next scheduled alignment uses **08:30**

2. **If alignment is already armed**
	- the currently armed delay is not retroactively rewritten
	- example: at **12:00**, target **07:00** arms the next reset for the following morning
	- if the user changes target to **08:30** at **13:00**, the already-armed reset still stays on the old arm for that cycle
	- the new target time applies to the next alignment event after that cycle completes, NTP is reacquired, or UTC offset changes

3. **If the target time is earlier than the current local time**
	- the code wraps to the next day using `delta_seconds += 24 * 3600`
	- example: current local time **12:00**, target **07:00**
	- the next alignment is scheduled for **07:00 tomorrow**, not earlier today

This keeps the behavior simple: the target time is a scheduling anchor for the next alignment, not a continuously re-evaluated live deadline.

### F) Shortcoming confirmed: pre-trigger time changes are not reapplied strongly enough
For your expected UX, this is the main gap:
- if user edits `Reset target time` in `/transmitter/hardware` before a pending aligned reset has fired,
- the pending arm should move immediately to the new target.

Current behavior can keep the old pending arm for that cycle.

---

## Proposed fix (event-driven from `/transmitter/hardware`, no polling)

### Required trigger model
Use the existing settings write/apply event path (already called when hardware setting is changed), not periodic polling.

### Suggested implementation
Add a small contactor/BMS hook, e.g. `on_bms_reset_alignment_settings_changed()`.

Invoke it immediately after successful contactor setting apply for these fields:
- `CONTACTOR_PERIODIC_BMS_RESET`
- `CONTACTOR_BMS_FIRST_ALIGN_ENABLED`
- `CONTACTOR_BMS_FIRST_ALIGN_TARGET_MINUTES`

Hook behavior:
1. If periodic reset disabled -> clear any armed alignment state.
2. If periodic reset enabled and reset state is idle and NTP valid -> recompute and overwrite pending alignment now from current local time + new target.
3. If reset is already in-progress -> do not interrupt current reset; apply new target to next arming opportunity.
4. Emit log/event with old target, new target, and newly computed delay seconds.

This satisfies your requirement that changing `time` in `/transmitter/hardware` is what triggers scheduling update, without adding background polling.

---

## Verdict
Your described model is **implemented and matches core runtime behavior**:
- baseline elapsed 24h,
- optional NTP-time alignment to target,
- then 24h elapsed cadence.

Specific answer to "are we missing use of reset target time?":
- **No, it is used** in runtime.
- But it is used as an alignment anchor, not as a strict every-day-at-HH:MM scheduler.

Overall, this does achieve the simple design goal you described:
- keep the inherent 24h reset structure,
- use the target time only to shift the next reset anchor when time/NTP is available,
- then let the 24h timer run again from that aligned reset.

Core backend structure is already sufficient for the model, with one focused improvement still recommended:
- event-drive immediate re-arm when `Reset target time` changes and a pending alignment has not fired.

Important rule (explicit): there should only ever be **one active next-reset time** at runtime.
- Either the elapsed 24h due time is active,
- or an aligned target-time arm is active,
- but not two competing schedules.

Nuances reviewed (updated):
1. Timer-start behavior should re-apply/reset control flags and values from current runtime settings.
	- When the reset timer anchor is (re)started, runtime should read current BMS reset control values and apply them consistently (`periodic_bms_reset`, align enable, target minutes, duration).
	- This keeps behavior deterministic after setting edits and avoids stale state carrying across arms.
	- Practical expectation: active scheduling state always reflects latest accepted `/transmitter/hardware` values.

2. UTC offset-change logic can trigger re-alignment later.
	- This is valid and acceptable for your design.
	- If UTC offset moves backward, next trigger can be delayed; this is expected and not a problem per your requirement.

3. There should be exactly one elapsed timer and one active next-reset decision path.
	- On `Reset target time` change, use a fixed safety threshold.
	- **Define safe-to-move as `remaining_time_to_current_reset >= 5 minutes`**.
	- If safe-to-move: rewrite the one active pending reset immediately to the new target.
	- If not safe-to-move (`< 5 minutes`): keep current pending reset and apply the new target to the next cycle.
	- This preserves the single-timer model while giving deterministic "move-or-next" behavior on edits.

Resolution: keep the single 24-hour timer model and implement event-driven "move-or-next" on `/transmitter/hardware` target-time edits.
- If `remaining_time_to_current_reset >= 5 minutes`, move the active pending reset immediately.
- If `< 5 minutes`, keep current pending reset and apply the new target to the next cycle.
- No polling; trigger only from successful hardware setting apply events.