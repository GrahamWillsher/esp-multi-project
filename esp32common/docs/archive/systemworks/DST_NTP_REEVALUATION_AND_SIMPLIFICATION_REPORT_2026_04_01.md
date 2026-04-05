# DST/NTP Re-Analysis and Simplification Report (2026-04-01)

## Scope
Re-evaluate:
1. How NTP and DST are actually re-evaluated in the current firmware.
2. Whether BST/DST-specific logic should be removed.
3. Operational impact on periodic BMS reset alignment.
4. Recommended direction with risk trade-offs.

---

## Executive Summary
- **NTP does not provide DST/BST policy**. NTP provides UTC epoch only.
- **DST behavior comes from the local `TZ` rules + `localtime()`**, not from NTP packets.
- In this firmware, **runtime wall-clock offset used in heartbeats is cached** and refreshed mainly on:
  - timezone configuration changes, and
  - successful NTP sync cycles.
- Current NTP cadence is every **30 minutes**, so a seasonal DST boundary can be reflected with up to ~30 minutes lag in the cached `utc_offset_min` heartbeat value.
- BMS reset logic currently includes extra DST transition alignment machinery. This is functionally valid but materially increases complexity.

**Selected direction (updated):**
- Use a **variant of Option B**:
  - Keep timezone correctness via `TZ` + `tzset` + `localtime`.
  - Use geolocation flow that yields timezone identity (lat/lon-capable preferred).
  - Use **offset-change detection** (`previous_utc_offset_min != current_utc_offset_min`) as the seasonal trigger.
  - On offset change, trigger alignment again and re-anchor/reset the 24h BMS reset cadence.
- Remove old legacy DST queue/prediction logic from the BMS reset control path.

---

## Ground Truth from Current Code

## 1) What NTP updates in this codebase
**Transmitter NTP utility task** runs periodic sync and timezone refresh workflow:
- `NTP_SYNC_INTERVAL_MS` is `30 * 60 * 1000` (30 min).
- Periodic NTP sync trigger and execution in:
  - `ESPnowtransmitter2/.../lib/ethernet_utilities/ethernet_utilities.h`
  - `ESPnowtransmitter2/.../lib/ethernet_utilities/ethernet_utilities.cpp`

NTP result handling:
- On success, system time is set via `settimeofday()`.
- Then timezone-derived cache refresh is executed (`refresh_cached_utc_offset()`) and transition snapshot cache is rebuilt.

## 2) Where DST actually comes from
DST is derived from C library timezone behavior:
- Timezone set with `setenv("TZ", ...)` + `tzset()`.
- Offset at epoch computed using `localtime_r(...); strftime("%z", ...)`.
- Transition discovery scans future days and bisects transition times based on offset change.

This is implemented in:
- `ESPnowtransmitter2/.../lib/ethernet_utilities/ethernet_utilities.cpp`
  - `get_utc_offset_min_at()`
  - `find_offset_transition_time()`
  - `refresh_time_transition_snapshot_cache()`

## 3) What the receiver uses for live time
Receiver dashboard live time is heartbeat-driven:
- Uses `unix_time + utc_offset_min` from `/api/transmitter_health`.
- No on-demand DST calculation in receiver runtime display path.

Relevant files:
- `espnowreceiver_2/lib/webserver/pages/dashboard_page_script.cpp`
- `espnowreceiver_2/lib/webserver/api/api_telemetry_handlers.cpp`
- `espnowreceiver_2/src/espnow/rx_heartbeat_manager.cpp`

## 4) Heartbeat offset source
Transmitter heartbeat packs:
- `hb.unix_time` from `TimeManager::get_unix_time()`
- `hb.utc_offset_min` from `get_cached_utc_offset_min()`

Relevant file:
- `ESPnowtransmitter2/.../src/espnow/heartbeat_manager.cpp`

This means seasonal boundary reflection in receiver display depends on when cached offset is refreshed.

## 5) BMS periodic reset alignment complexity
Periodic BMS reset path includes:
- initial alignment to a target local time,
- DST transition queue persisted in NVS,
- pre-arming a DST realignment reset when transition is within 24h,
- post-success queue pop and continuation.

Relevant file:
- `ESPnowtransmitter2/.../src/battery_emulator/communication/contactorcontrol/comm_contactorcontrol.cpp`

This is the major BST/DST complexity concentration.

---

## Updated Architecture Decision (User Selected)

## Option B-Variant (Chosen)
1. Keep civil-time correctness from timezone rules (`TZ`/`tzset`/`localtime`).
2. Remove old custom DST transition queue logic in BMS reset scheduler.
3. Detect seasonal change by **offset delta**:
  - store `last_seen_utc_offset_min`,
  - when `utc_offset_min` changes, call alignment function,
  - reset/re-anchor periodic 24h reset timer from that alignment point.
4. Keep implementation target at hour-level DST correction (twice-yearly practical correction), not transition forecasting.

Rationale:
- Requirement is practical seasonal hour correction, not exact transition-second scheduling.
- Offset delta is a direct operational signal and much simpler than queue persistence/forecasting.
- Complexity and maintenance burden are reduced materially.

---

## NTP vs DST: Correct Behavior Model
From ESP-IDF and libc behavior:
- NTP/SNTP synchronizes absolute time (UTC basis).
- Local timezone and DST are applied by libc when converting epoch to local civil time (`localtime`).
- Therefore, DST is not negotiated over NTP.

Practical implication for this firmware:
- Any code using `localtime` with valid TZ rules can evaluate DST transitions correctly.
- But **cached offsets** (used by heartbeat) only change when cache is refreshed.

---

## Observed/Expected Edge Cases

1. **Seasonal boundary lag in heartbeat offset**
- Because `utc_offset_min` is cached, boundary update can lag until next cache refresh (currently tied to NTP cadence and timezone reconfigure points).

2. **Unknown timezone mapping fallback**
- If timezone mapping is unknown and falls back to fixed offset POSIX, DST transitions may not exist (intended fallback behavior).

3. **BMS reset alignment dependency on local-time model**
- DST queue logic assumes correct timezone rules are loaded and stable.

---

## Should BST/DST Code Be Removed?

## Option A — Keep full DST machinery (status quo)
Pros:
- Best wall-clock correctness around DST season changes.
- BMS reset can remain anchored to intended local-time behavior.

Cons:
- Highest complexity and maintenance burden.
- More moving parts (queue persistence, transition prediction, snapshot transport).

## Option B — Partial simplification (recommended default)
Keep:
- `TZ`/`tzset` and localtime-based offset calculation.
- Heartbeat `utc_offset_min` for runtime display.

Remove/reduce:
- Non-essential DST transition publication/diagnostic complexity where not operationally required.
- Optional: simplify `/debug` DST presentation to diagnostic-only mode.

Pros:
- Large complexity reduction with minimal operational risk.
- Keeps local-time correctness for most runtime behavior.

Cons:
- Loses some deep DST introspection tooling.

## Option C — Remove BST/DST-specific scheduling logic from BMS reset
Behavior:
- Keep periodic reset as pure elapsed 24h cycle.
- Optional one-time initial alignment retained.
- No DST queue realignment events.

Pros:
- Big simplification in critical control path.
- Easier reasoning and testing.

Cons:
- Seasonal local wall-clock drift of approximately 1 hour can persist until manual/operator re-alignment policy is applied.

## Option B-Variant — Partial simplification + offset-change re-alignment (selected)
Behavior:
- Keep `TZ`/`tzset`/`localtime` timezone correctness.
- Keep periodic 24h reset model.
- On `utc_offset_min` change, trigger re-alignment and reset cadence anchor.
- Remove old DST queue persistence/forecasting path.

Pros:
- Very low complexity and easy testability.
- Preserves practical DST correction behavior for operations.
- Avoids stale queue edge cases.

Cons:
- Trigger timing depends on offset refresh cadence (e.g., next sync cycle), not exact transition second.

---

## Operational Decision Framework
Use this binary rule:

- If requirement is **"reset near a fixed local wall-clock time year-round"** → keep DST realignment logic (or move to managed timezone service).
- If requirement is **"reset once per ~24h and ±1h seasonal drift is acceptable"** → remove DST queue realignment logic.

---

## Recommended Direction
1. Adopt **Option B-Variant** now.
2. Remove old DST queue logic from BMS scheduler.
3. Implement offset-change trigger (`utc_offset_min` delta) to invoke re-alignment and reset 24h cadence.
4. Keep fallback safety already present:
   - when time unsynced, current code already falls back to elapsed-interval behavior.

---

## Implementation Outline (Selected Option B-Variant)
In `comm_contactorcontrol.cpp`:
- Remove DST queue persistence/refill/pop machinery (`dstQueue*`, `refill_dst_queue_if_needed`, `maybe_arm_dst_realign`, NVS queue persistence helpers).
- Keep scheduler:
  - optional initial alignment (`bms_first_align_enabled`) OR direct elapsed 24h cycle,
  - periodic reset by elapsed 24h.
- Add offset-change trigger:
  - cache last seen `utc_offset_min`,
  - when changed, call alignment function,
  - reset/re-anchor cadence timer from alignment completion.
- Retain unsynced fallback behavior and reset safety gating under load.

In `ethernet_utilities.cpp` / `version_beacon_manager.cpp`:
- Remove transition-array publication if only used by old DST machinery/debug.
- Keep heartbeat `utc_offset_min` generation (low overhead and useful for UI/runtime).

---

## Other Simple Ways Investigated

1. **Pure elapsed 24h only (no seasonal correction)**
- Simplest path.
- Drawback: seasonal ±1h local-time drift persists until manual re-alignment.

2. **Daily fixed local-time trigger via `localtime` only**
- Simple daily wall-clock trigger.
- Needs careful handling for spring-forward/fall-back duplicate/missing hour windows.

3. **Offset-change trigger + 24h cadence (selected)**
- Best simplicity/correctness trade-off for this requirement.

4. **External transition API schedule**
- Highest explicit transition correctness.
- Adds auth/network dependency and complexity not necessary for this use case.

---

## Validation Checklist After Simplification
1. Time sync, reboot, and network flap scenarios.
2. First-alignment behavior at target minute.
3. 24h periodic cycle stability over multiple days.
4. Offset-change trigger validation:
  - force/simulate `utc_offset_min` change and verify alignment runs once.
  - verify cadence timer anchor resets correctly.
5. Seasonal transition day simulation:
  - verify offset delta is detected and re-alignment occurs.
6. Receiver dashboard time continuity from heartbeat fields.

---

## Final Conclusion
- **NTP is not the DST source**; local timezone rules are.
- The current code is technically sound but has high DST-specific complexity in BMS scheduling.
- A controlled simplification is feasible and reasonable.
- Selected practical path: **Option B-variant** — remove old DST queue coding, keep timezone correctness, and re-align/reset 24h BMS cadence on `utc_offset_min` changes.
