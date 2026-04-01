# Whole Codebase Full Review — Transmitter, Receiver, and Common

**Date:** 2026-04-01  
**Scope:**
- `ESPnowtransmitter2/espnowtransmitter2`
- `espnowreceiver_2`
- `esp32common` / `ESP32common`

---

## 1) Scope and constraint checks

This review was performed section-by-section across runtime, networking, ESP-NOW, webserver/API/pages, display, settings/config, memory/logging, and shared common modules.

Constraint-sensitive areas were explicitly validated in current code before recommendations:

- MQTT buffer sizes (`6144`) are currently aligned TX/RX.
- Existing JSON document sizes are treated as constrained and should **not** be changed casually.
- Recommendations below avoid arbitrary size changes unless there is a correctness bug.

---

## 2) Architecture health snapshot

Overall architecture is in a **good direction**:

- Shared commonization is real and active (runtime helpers, ESP-NOW common layer, shared web utilities).
- TX/RX state-machine patterns remain mostly consistent.
- Receiver page/API registration is organized and maintainable.

Main remaining debt is not broad instability; it is concentrated in:

1. legacy overlap and compatibility shims,
2. duplicated transmitter Battery Emulator subtree elements,
3. mixed old/new display and UI pathways,
4. wrapper layers that now add indirection without much unique logic.

---

## 3) Redundant / obsolete code findings

### 3.1 Transmitter

1. Obsolete source artifact present: ✅ complete
   - `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/datalayer/datalayer.cpp.bak`

2. Duplicated settings headers / ownership ambiguity: ✅ complete
   - `ESPnowtransmitter2/espnowtransmitter2/src/datalayer/system_settings.h`
   - `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/system_settings.h`

3. Compatibility translation unit retained but now effectively empty: ✅ complete (intentional compatibility TU)
   - `ESPnowtransmitter2/espnowtransmitter2/src/runtime/bootstrap_phase_runner.cpp`

### 3.2 Receiver

1. Compatibility TU retained: ✅ complete
   - `espnowreceiver_2/lib/webserver/common/spec_page_layout.cpp`

2. Wrapper-heavy manager that has grown into pass-through fan-out: ✅ complete
   - `espnowreceiver_2/lib/webserver/utils/transmitter_manager.cpp`

3. Overlap in MQTT state concerns: ✅ complete (ownership boundaries clarified)
   - runtime MQTT handling in `espnowreceiver_2/src/mqtt/mqtt_client.cpp`
   - web/spec cache handling in `espnowreceiver_2/lib/webserver/utils/transmitter_mqtt_specs.cpp`

### 3.3 Common / docs

1. Protocol docs not fully aligned with current code shape in places: ✅ complete
   - `ESP32common/docs/ESP-NOW_Communication_Architecture.md`

2. Receiver architecture review doc references removed/non-canonical paths: ✅ complete
   - `espnowreceiver_2/RECEIVER_HTTP_WEB_ARCHITECTURE_REVIEW_2026_03_18.md`

---

## 4) Reuse and consolidation opportunities

### 4.1 ESP-NOW TX/RX connection logic reuse (high value)

Repeated deferred-peer and cleanup patterns exist in:
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_connection_handler.cpp`
- `espnowreceiver_2/src/espnow/rx_connection_handler.cpp`

**Recommendation:** extract shared helper primitives into `esp32common` with small callback hooks for TX/RX-specific actions.

### 4.2 JSON response pipeline reuse in receiver

Current response flow splits responsibilities across:
- `espnowreceiver_2/lib/webserver/api/api_response_utils.cpp`
- `ESP32common/webserver_common_utils/src/http_json_utils.cpp`

**Recommendation:** unify to one canonical path for JSON send semantics and reduce unnecessary intermediate `String` copies.

### 4.3 Battery Emulator subtree deduplication

Duplicated/near-duplicated components still exist between local transmitter and embedded Battery Emulator tree.

**Recommendation:** define one canonical include/ownership boundary for shared datalayer/settings headers and remove duplicate definitions.

### 4.4 Settings cache/persistence pattern reuse

Receiver has reusable cache patterns in:
- `espnowreceiver_2/lib/webserver/utils/transmitter_settings_cache.cpp`
- `espnowreceiver_2/lib/webserver/utils/transmitter_state.cpp`

**Recommendation:** formalize this pattern for future modules (single helper template/policy).

---

## 5) Candidate rewrites

## A) Safe immediate cleanup (low risk)

1. Remove obsolete artifact:
   - `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/datalayer/datalayer.cpp.bak`

2. Sync stale architecture docs to actual code paths.

3. Keep compatibility TUs only if still needed; otherwise remove with a short migration note.

## B) Medium refactor (moderate risk)

1. Consolidate ESP-NOW connection helper logic into `esp32common`.
2. Collapse duplicate transmitter settings/datalayer headers.
3. Reduce wrapper indirection in receiver manager layer where direct namespace/module calls are now stable.

## C) High-risk rewrite candidates

1. Receiver display architecture unification:
   - `espnowreceiver_2/src/display/*`
   - `espnowreceiver_2/platformio.ini` env/source filter interactions

2. Transmitter Battery Emulator boundary rewrite (upstream vs local customization delineation):
   - `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/*`

These should be done only in isolated phases with regression baselines.

---

## 6) Constraint-sensitive areas verified

Current values verified in code:

- TX MQTT client buffer: `6144`
  - `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp`

- RX MQTT client buffer: `6144`
  - `espnowreceiver_2/src/mqtt/mqtt_client.cpp`

- TX document sizes include 3072/4096/6144 in catalog/spec/event paths.
- RX parse document sizes include fixed 512/2048/3072/4096/6144 and one payload-derived dynamic cap for `spec_data_2`.

### Constraint recommendation (no arbitrary tuning)

Do **not** change these sizes during cleanup phases unless there is a measured correctness failure.

Instead:

1. Add centralized payload budget constants/comments-of-record per topic.
2. Add pre-publish / pre-parse budget telemetry.
3. Add CI/static consistency checks for topic↔document-size mapping.

---

## 7) Prioritized action plan

## Phase 1 — Stabilize and de-risk

1. Remove obsolete artifacts (`.bak`) and stale doc references.
2. Create one canonical “module map” document for TX/RX/common ownership.
3. Add MQTT/JSON guardrails (constants + checks + logging), no payload size changes.

## Phase 2 — Consolidate medium debt

1. Extract shared ESP-NOW connection helper utilities into `esp32common`.
2. Collapse duplicate transmitter headers/settings sources.
3. Consolidate receiver JSON response helpers to reduce allocation/copy churn.

## Phase 3 — Controlled rewrites

1. Receiver display stack unification.
2. Transmitter Battery Emulator boundary rewrite.

---

## 8) Acceptance criteria

### Phase 1 acceptance

- No stale artifacts or stale path references in active source/docs.
- Canonical module map exists and matches current tree.
- MQTT/JSON governance checks added; behavior unchanged.

### Phase 2 acceptance

- TX/RX connection handlers consume shared helpers without protocol behavior drift.
- Duplicate headers removed with no build/runtime regression.
- Receiver JSON response paths remain API-compatible and reduce avoidable copies.

### Phase 3 acceptance

- Receiver builds cleanly across intended envs with deterministic display source selection.
- TX/RX soak tests pass for reconnect, heartbeat, OTA, and catalog/spec flows.
- Battery/inverter profile behavior remains compatible with baseline.

---

## 9) Explicit do-not-change constraints

1. Do not change MQTT buffer size contracts (`6144`) without demonstrated correctness break.
2. Do not casually change JSON document sizes in existing topic handlers.
3. Do not change topic names/payload schemas in cleanup-only phases.
4. Do not change heartbeat/reconnect timing contracts without synchronized TX+RX validation and updated docs.

---

## Final summary

The codebase is broadly healthy and improving, with most remaining issues concentrated in overlap and ownership clarity rather than systemic instability.

Highest leverage improvements are:
1. artifact/doc cleanup,
2. shared ESP-NOW helper extraction,
3. duplicate header collapse,
4. response/helper consolidation,
5. deferred high-risk display and Battery Emulator boundary rewrites under controlled phases.

---

## 10) Receiver display-processing deep review (SOC / power path)

### 10.1 Current runtime data path (receiver)

Observed path for telemetry to display:

1. ESP-NOW telemetry ingestion updates local cache (`apply_telemetry_sample`)
   - `espnowreceiver_2/src/espnow/espnow_tasks.cpp`
2. SOC/power changes are enqueued to decoupled display queue
   - `espnowreceiver_2/src/display/display_update_queue.cpp`
3. `DisplayRenderer` task dequeues snapshots and calls `display_soc()` / `display_power()`
   - `espnowreceiver_2/src/config/runtime_task_startup.cpp`
   - `espnowreceiver_2/src/display/display_update_queue.cpp`
4. Public display wrapper dispatches to the active TFT implementation (`TftDisplay`)
   - `espnowreceiver_2/src/display/display.cpp`

This is a solid high-level architecture (producer/consumer decoupling), but implementation overlap remains significant.

### 10.2 Key findings (reusability + rewrite signals)

#### A) Primary display path is now TFT-only

Display ownership is now intentionally centered on the TFT implementation:

-- Active path:
   - `src/display/display.cpp`
   - `src/display/tft_impl/*`
- Legacy/alternate path still present:
  - `src/display/display_core.cpp`
  - `src/display/display_splash.cpp`

This removes backend drift pressure and makes display behavior easier to reason about.

#### B) SOC/power rendering logic is still duplicated across TFT pathways

SOC and power calculations are still implemented in more than one TFT-oriented area:

- SOC gradient generation:
  - TFT path uses 500-step shared gradient arrays (`tft_impl/tft_display.cpp` + `Display::soc_color_gradient`)
- Power scaling and thresholds:
  - TFT path (`tft_impl/tft_display.cpp`) uses `LayoutSpec::PowerBar::MAX_POWER_W`
  - Legacy TFT widget path (`widgets/power_bar_widget.cpp`) has separate defaults/state machine

Result: visual and semantic drift can still happen between active TFT renderer logic and legacy TFT widget logic.

#### C) Widget layer reuse is currently low

`pages/status_page.cpp` + `widgets/proportional_number_widget.*` + `widgets/power_bar_widget.*` form a reasonable reusable widget model, but current active TFT backend logic in `tft_impl/tft_display.cpp` reimplements SOC/power rendering directly instead of using these widgets.

This means maintenance cost is paid twice, with limited runtime benefit.

#### D) Legacy-path quality smell indicates stale code

`display_core.cpp` still contains duplicate/erroneous draw call patterns in now-overlapped logic. This is a strong indicator that the file is legacy residue and should not remain in long-term canonical flow.

### 10.3 Reuse opportunities (recommended)

1. **Extract backend-neutral telemetry view-model helpers** (high value)
   - Single helpers for:
     - SOC clamp/index/color selection
     - Power clamp/bar-count/direction/zero-threshold
     - Power text formatting
   - Store under shared receiver display domain module.

2. **Unify layout and thresholds into one token set**
   - Keep geometry and semantic thresholds in one authoritative spec (`layout/display_layout_spec.h` style), consumed by all active TFT display paths.

3. **Adopt one widget contract for the TFT display path**
   - Keep page-level API (`update_soc`, `update_power`) and backend-specific draw adapters only.
   - Avoid embedding duplicate business logic inside each renderer.

### 10.4 Rewrite recommendations

#### Low-risk now

1. Mark legacy display paths as deprecated in-code and docs:
   - `display_core.cpp`
   - any no-longer-canonical splash dispatcher duplication
2. Normalize power/SOC constants across active TFT code.
3. Introduce a tiny shared `display_value_model` module (no rendering code).

#### Medium-risk

1. Route active TFT rendering through one status/widget abstraction, leaving only driver draw primitives backend-specific.
2. Remove inactive display pathways fully once parity tests pass.

#### High-risk (phase-gated)

1. Remove legacy display-core stack entirely after parity tests.
2. Collapse to one canonical source-filter model in `platformio.ini` so unsupported/legacy display code cannot compile accidentally.

### 10.5 Suggested acceptance checks for display refactor phase

1. SOC color at key points (0/25/50/75/100) matches expected TFT gradient contract.
2. Power bars/labels match sign and magnitude transitions for the same telemetry stream.
3. No dropped-update regressions under high-rate ESP-NOW telemetry (queue-depth stress test).
4. TFT build remains deterministic under concurrent LED + telemetry updates.
5. TFT env builds with explicit canonical source inclusion/exclusion (no ambiguous overlap).

---

## 11) Implementation progress log (legacy removal)

### Step 1 — `display_core` legacy module retired ✅

Completed:
- Replaced legacy compatibility declarations in `src/display/display_core.h` with a minimal retired-module header that points to canonical `display.h`.
- Removed old legacy implementation body from `src/display/display_core.cpp` and replaced it with a retired-module tombstone.

Outcome:
- Broken/duplicate legacy rendering logic is no longer present as active source.

### Step 2 — `display_splash` legacy module retired ✅

Completed:
- Removed unnecessary include of `display/display_splash.h` from `src/main.cpp`.
- Replaced legacy declarations in `src/display/display_splash.h` with a minimal retired-module header pointing to canonical `display.h`.
- Removed legacy implementation body from `src/display/display_splash.cpp` and replaced it with a retired-module tombstone.

Outcome:
- Old splash helper path is removed from active source; canonical splash behavior remains in backend display implementation.

### Step 3 — Build/status validation ✅

Completed:
- Confirmed TFT source filter continues to exclude inactive display pathways in `platformio.ini`.
- Rebuilt receiver TFT environment successfully after these removals.

Build command:
- `pio run -e lilygo-t-display-s3_tft -j 12`

### Step 4 — Webserver compatibility TU cleanup ✅

Completed:
- Retired legacy compatibility translation unit body in `lib/webserver/common/spec_page_layout.cpp`.
- Kept `lib/webserver/common/spec_page_layout.h` as a thin compatibility header with explicit migration note to common implementation.

Outcome:
- Receiver now relies on canonical common layout implementation with reduced local legacy surface.

### Step 5 — `TransmitterManager` legacy passthrough API reduction ✅

Completed:
- Removed unused legacy NVS passthrough methods from `TransmitterManager` public API:
   - `loadFromNVS()`
   - `saveToNVS()`
- Removed matching implementation bodies from `lib/webserver/utils/transmitter_manager.cpp`.

Outcome:
- Reduced wrapper-only surface area in receiver manager layer while preserving active behavior (`init()` + internal persistence flows remain intact).

Build validation:
- `pio run -e lilygo-t-display-s3_tft -j 12` ✅

### Step 6 — `TransmitterManager` unused MQTT credential wrapper removal ✅

Completed:
- Removed unused `TransmitterManager` public API method:
   - `getMqttPassword()`
- Removed matching implementation body from `lib/webserver/utils/transmitter_manager.cpp`.

Outcome:
- Reduced exposed wrapper surface for sensitive credential access while preserving active MQTT/network API behavior.

Build validation:
- `pio run -e lilygo-t-display-s3_tft -j 12` ✅

### Step 7 — `TransmitterManager` unused MQTT server-string wrapper removal ✅

Completed:
- Removed unused `TransmitterManager` public API method:
   - `getMqttServerString()`
- Removed matching implementation body from `lib/webserver/utils/transmitter_manager.cpp`.

Outcome:
- Further reduced wrapper-only MQTT helper surface without affecting active API consumers, which already use `getMqttServer()` and `getMqttPort()` directly.

Build validation:
- `pio run -e lilygo-t-display-s3_tft -j 12` ✅

### Step 8 — `TransmitterManager` unused beacon/send/heartbeat getters removed ✅

Completed:
- Removed three unused `TransmitterManager` public API methods with zero external callers:
   - `getLastBeaconTime()`
   - `wasLastSendSuccessful()`
   - `getHeartbeatFlags()`
- Removed all three matching implementation bodies from `lib/webserver/utils/transmitter_manager.cpp`.

Outcome:
- Reduced `TransmitterManager` public surface by removing dead one-line passthrough getters while preserving active writer paths.

Build validation:
- `pio run -e lilygo-t-display-s3_tft -j 12` ✅

### Step 9 — `TransmitterManager` unused settings `has*` checkers + metadata version number removed ✅

Completed:
- Removed five unused `TransmitterManager` public API methods with zero external callers:
   - `getMetadataVersionNumber()`
   - `hasPowerSettings()`
   - `hasInverterSettings()`
   - `hasCanSettings()`
   - `hasContactorSettings()`
- Removed all five matching declaration + implementation pairs.

Outcome:
- Further tightened `TransmitterManager` API surface; retained only `has*` predicates with verified callers (`hasBatterySettings`, `hasBatteryEmulatorSettings`).

Build validation:
- `pio run -e lilygo-t-display-s3_tft -j 12` ✅

### Step 10 — `TransmitterManager` last unused event-log getters removed + transmitter `.bak` artifact deleted ✅

Completed:
- Removed 2 unused `TransmitterManager` public API methods with zero external callers:
   - `getEventLogCount()` — snapshot accessor (`getEventLogsSnapshot`) is used; the count helper was never called.
   - `getEventLogsLastUpdateMs()` — same: no consumers in the codebase.
- Removed both declaration + implementation pairs from `lib/webserver/utils/transmitter_manager.h/.cpp`.
- Deleted obsolete transmitter artifact identified in Section 3.1 of this review:
   - `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/datalayer/datalayer.cpp.bak`

Outcome:
- `TransmitterManager` wrapper surface is now clean — all remaining methods have verified external callers.
- Transmitter source tree no longer contains an uncommitted stale backup file.

Build validation:
- Receiver: `pio run -e lilygo-t-display-s3_tft -j 12` ✅
- Transmitter: `pio run -j 12` ✅

---

## 12) Phase 3 implementation log (controlled rewrites)

### Phase 3.1 — Receiver display source-filter hardening ✅

Problem:
- The legacy `lilygo-t-display-s3` env was missing `-D USE_TFT=1`, making its compile-time display path non-deterministic relative to the `_tft` env (same hardware, different guards).
- Both envs had unlabelled source filter exclusion blocks — no comments explaining what each exclusion was or why.

Actions:
- Added `-D USE_TFT=1` to `lilygo-t-display-s3` `build_flags` to match the `_tft` env's display path.
- Added per-block comments to both env `build_src_filter` sections:
  - LVGL exclusions labelled as pre-emptive safety exclusions (files not present).
  - Retired legacy stubs (`display_core.cpp`, `display_splash.cpp`) labelled as tombstoned and excluded to prevent accidental linkage.

Acceptance criteria met:
- Receiver builds cleanly with deterministic display source selection.
- No ambiguous display path can compile in either environment.

Build validation:
- `pio run -e lilygo-t-display-s3_tft -j 12` ✅

### Phase 3.2 — Transmitter Battery Emulator boundary: duplicate `system_settings.h` resolved ✅

Problem:
- `src/datalayer/system_settings.h` was an independent copy of `src/battery_emulator/system_settings.h` — identical content, two separate definitions that could silently diverge.
- No boundary documentation — unclear which file was canonical.

Actions:
- Replaced `src/datalayer/system_settings.h` with a redirect shim:
  - Uses a distinct include guard (`DATALAYER_SYSTEM_SETTINGS_SHIM_H_`) to avoid guard collision.
  - Delegates via `#include "../battery_emulator/system_settings.h"`.
  - Documents the intent clearly in the file header.
- Added upstream boundary marker comment to `src/battery_emulator/system_settings.h` identifying it as the single canonical owner.
- Replaced the original verbose multi-parameter comment block with a clean concise form to match the new ownership model.

Ownership now:
- `src/battery_emulator/system_settings.h` — canonical owner (upstream boundary layer).
- `src/datalayer/system_settings.h` — shim/redirect; will always match the canonical copy.

Build validation:
- Transmitter: `pio run -j 12` ✅

---

## 13) Current status snapshot (as of 2026-04-01)

- Receiver cleanup steps (1–10): ✅ complete
- Receiver Phase 3.1 (display source-filter hardening): ✅ complete
- Transmitter Phase 3.2 (`system_settings.h` boundary rewrite): ✅ complete

Latest validation:
- Receiver: `pio run -e lilygo-t-display-s3_tft -j 12` ✅
- Transmitter: `pio run -j 12` ✅

### Step 9 — `TransmitterManager` unused settings `has*` checkers and metadata version number removed ✅

Completed:
- Removed 5 unused `TransmitterManager` public API methods with zero external callers:
   - `getMetadataVersionNumber()` — version tuple getters are used; the packed-number form had no consumers.
   - `hasPowerSettings()` — store/get for power settings remain; only the guard predicate was unused.
   - `hasInverterSettings()` — same pattern as power.
   - `hasCanSettings()` — same pattern.
   - `hasContactorSettings()` — same pattern.
- Removed all five matching declaration + implementation pairs.

Outcome:
- `TransmitterManager` public surface further tightened; only `has*` predicates with proven external callers (`hasBatterySettings`, `hasBatteryEmulatorSettings`) are retained.

Build validation:
- `pio run -e lilygo-t-display-s3_tft -j 12` ✅

### Step 8 — `TransmitterManager` unused beacon/send/heartbeat getters removed ✅

Completed:
- Removed three unused `TransmitterManager` public API methods with zero external callers:
   - `getLastBeaconTime()` — only setter/storage path is alive; no consumer queried it.
   - `wasLastSendSuccessful()` — `updateSendStatus()` (the writer) is still present; the getter had no callers.
   - `getHeartbeatFlags()` — `updateHeartbeatFlags()` (the writer) is still present; the getter had no callers.
- Removed all three matching implementation bodies from `lib/webserver/utils/transmitter_manager.cpp`.

Outcome:
- Reduced `TransmitterManager` public surface by three one-liner passthroughs with no active consumers; all writers remain intact.

Build validation:
- `pio run -e lilygo-t-display-s3_tft -j 12` ✅

Completed:
- Removed unused `TransmitterManager` public API method:
   - `getMqttServerString()`
- Removed matching implementation body from `lib/webserver/utils/transmitter_manager.cpp`.

Outcome:
- Further reduced wrapper-only MQTT helper surface without affecting active API consumers, which already use `getMqttServer()` and `getMqttPort()` directly.

Build validation:
- `pio run -e lilygo-t-display-s3_tft -j 12` ✅
