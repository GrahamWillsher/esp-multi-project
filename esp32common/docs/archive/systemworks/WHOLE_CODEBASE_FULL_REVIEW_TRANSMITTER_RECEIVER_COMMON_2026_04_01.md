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

### 4.1 ESP-NOW TX/RX connection logic reuse (high value) ✅ complete

Repeated deferred-peer and cleanup patterns exist in:
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_connection_handler.cpp`
- `espnowreceiver_2/src/espnow/rx_connection_handler.cpp`

**Recommendation:** extract shared helper primitives into `esp32common` with small callback hooks for TX/RX-specific actions.

**Implemented:** Extracted `has_valid_mac()` and `is_broadcast_mac()` into a new shared header:
- `esp32common/espnow_common_utils/espnow_mac_utils.h` — implementation (inline functions, `EspNowMacUtils` namespace)
- `esp32common/include/esp32common/espnow/mac_utils.h` — stable public forwarding header
Both connection handlers now use `EspNowMacUtils::has_valid_mac()` and `EspNowMacUtils::is_broadcast_mac()`; their local static function copies have been removed.

### 4.2 JSON response pipeline reuse in receiver ✅ complete

Current response flow splits responsibilities across:
- `espnowreceiver_2/lib/webserver/api/api_response_utils.cpp`
- `ESP32common/webserver_common_utils/src/http_json_utils.cpp`

**Recommendation:** unify to one canonical path for JSON send semantics and reduce unnecessary intermediate `String` copies.

**Implemented:**
- Added canonical pipeline architecture comment to `http_json_utils.h` documenting it as the primitive layer.
- Added matching layer-split comment to `api_response_utils.h` documenting it as the higher-level receiver layer.
- Rewrote `send_json_doc()` to use a stack buffer (512-byte threshold) for the common case, avoiding an unnecessary heap `String` allocation. Larger documents take a precisely-sized single heap allocation. Both paths delegate to `HttpJsonUtils::send_json()` as before.

### 4.3 Battery Emulator subtree deduplication ✅ complete

Duplicated/near-duplicated components still exist between local transmitter and embedded Battery Emulator tree.

**Recommendation:** define one canonical include/ownership boundary for shared datalayer/settings headers and remove duplicate definitions.

**Implemented:** Added explicit ownership boundary comments to both `datalayer.h` files:
- `src/datalayer/datalayer.h` — marked as LOCAL EXTENDED COPY with maintenance contract: struct definitions must stay in sync; typedef aliases and include paths are the only permitted differences; cross-reference to upstream; future goal noted (collapse to shim after enum renaming resolved).
- `src/battery_emulator/datalayer/datalayer.h` — marked as UPSTREAM BOUNDARY with back-reference to local copy and synchronisation reminder.
A full redirect shim was not attempted here because the local copy adds typedef aliases (`bms_status_enum`, `real_bms_status_enum`) that resolve upstream naming conflicts — resolving those at source is tracked as a Phase 5C item.

### 4.4 Settings cache/persistence pattern reuse ✅ complete

Receiver has reusable cache patterns in:
- `espnowreceiver_2/lib/webserver/utils/transmitter_settings_cache.cpp`
- `espnowreceiver_2/lib/webserver/utils/transmitter_state.cpp`

**Recommendation:** formalize this pattern for future modules (single helper template/policy).

**Implemented:** Added a five-rule pattern contract comment block to `transmitter_settings_cache.h` documenting:
1. STORAGE — value-typed plain-old-data structs, no heap pointers.
2. ACCESSORS — symmetric `store_*` / `get_*` / `has_*` triplet per domain; `has_*()` returns true after first successful store.
3. PERSISTENCE — `load_from_prefs()` / `save_to_prefs()` via NVS Preferences; call load on boot, save after any store that must survive reboot.
4. THREAD SAFETY — no internal mutex; callers responsible for task-level serialisation.
5. OWNERSHIP — this module owns receiver-side cache of transmitter-reported settings; does not own runtime MQTT/network state.

---

## 5) Candidate rewrites

## A) Safe immediate cleanup (low risk)

1. Remove obsolete artifact: ✅ complete
   - `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/datalayer/datalayer.cpp.bak`

2. Sync stale architecture docs to actual code paths: ✅ complete

3. Keep compatibility TUs only if still needed; otherwise remove with a short migration note: ✅ complete

**Implemented in 5A:**
- `.bak` artifact removed from transmitter Battery Emulator subtree.
- Architecture path references aligned to canonical `webserver_common_utils/*` locations where stale paths remained.
- Compatibility TU policy documented and retained only for explicit migration stability cases; canonical ownership paths are now stated in-file.

## B) Medium refactor (moderate risk)

1. Consolidate ESP-NOW connection helper logic into `esp32common`: ✅ complete
2. Collapse duplicate transmitter settings/datalayer headers: ✅ complete
3. Reduce wrapper indirection in receiver manager layer where direct namespace/module calls are now stable: ✅ complete

**Implemented in 5B:**
- B.1 completed via shared `EspNowMacUtils` extraction in `esp32common` and adoption in both TX/RX connection handlers.
- B.2 completed by converting `src/datalayer/datalayer_extended.h` to a compatibility shim that includes canonical `src/battery_emulator/datalayer/datalayer_extended.h`.
- B.3 completed by prior `TransmitterManager` API surface reduction (unused passthrough wrapper removals with build validation at each step).

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

---

## 14) Phase 5A implementation log (safe immediate cleanup)

### 5A.1 — Obsolete artifact removal ✅

Status:
- Confirmed obsolete transmitter artifact is removed:
   - `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/datalayer/datalayer.cpp.bak`

### 5A.2 — Stale architecture doc path sync ✅

Completed:
- Updated stale webserver helper references in:
   - `espnowreceiver_2/RECEIVER_COMMON_CODE_REVIEW_2026_03_17.md`
- Replaced legacy paths with canonical common helper locations:
   - `esp32common/webserver/http_json_utils.h` → `esp32common/webserver_common_utils/include/webserver_common_utils/http_json_utils.h`
   - `esp32common/webserver/http_sse_utils.h` → `esp32common/webserver_common_utils/include/webserver_common_utils/http_sse_utils.h`
   - `esp32common/webserver/catalog_response_utils.h` → `esp32common/webserver_common_utils/include/webserver_common_utils/catalog_response_utils.h` (recommended location)
- Replaced stale note about `esp32common/webserver/receiver_webserver.cpp` with retirement/canonical-stack note.

### 5A.3 — Compatibility TU policy enforcement ✅

Decision and outcome:
- Compatibility translation units are retained only where explicitly needed for migration/source-tree stability.
- In-file migration notes now clearly point to canonical owners:
   - `espnowreceiver_2/lib/webserver/common/spec_page_layout.h` → `<webserver_common_utils/spec_page_layout.h>`
   - `ESPnowtransmitter2/espnowtransmitter2/src/runtime/bootstrap_phase_runner.cpp` → `esp32common/runtime_common_utils/src/bootstrap_phase_runner.cpp`

Build validation:
- Receiver: `pio run -e lilygo-t-display-s3_tft -j 12` ✅
- Transmitter: `pio run -j 12` ✅

---

## 15) Current status snapshot (as of 2026-04-02)

- Section 3 findings: ✅ complete
- Section 4 consolidation opportunities: ✅ complete
- Section 5A safe immediate cleanup: ✅ complete

Latest validation:
- Receiver: `pio run -e lilygo-t-display-s3_tft -j 12` ✅
- Transmitter: `pio run -j 12` ✅

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

---

## 14) Phase 4 implementation log (consolidation)

### 4.1 — Shared ESP-NOW MAC utilities extracted into `esp32common` ✅

Problem:
- Both `tx_connection_handler.cpp` and `rx_connection_handler.cpp` contained identical static helper functions (`has_valid_mac`) and near-identical inline broadcast-check loops that were not shared.

Actions:
- Created `esp32common/espnow_common_utils/espnow_mac_utils.h`:
  - `EspNowMacUtils::has_valid_mac(const uint8_t*)` — returns true if any byte is non-zero.
  - `EspNowMacUtils::is_broadcast_mac(const uint8_t*)` — returns true if all bytes are 0xFF.
  - Both are `inline` free functions with no state and no device-specific logic.
- Created `esp32common/include/esp32common/espnow/mac_utils.h` — stable public forwarding header (follows existing `esp32common` forwarding-header convention).
- Updated `tx_connection_handler.cpp`:
  - Added `#include <esp32common/espnow/mac_utils.h>`.
  - Removed local `has_valid_mac()` static function.
  - Replaced 9-line inline broadcast loop in CONNECTED→IDLE callback with `EspNowMacUtils::is_broadcast_mac(peer_mac)`.
  - Replaced `has_valid_mac()` call in `on_peer_registered()` with `EspNowMacUtils::has_valid_mac()`.
- Updated `rx_connection_handler.cpp` with matching changes; also replaced `has_valid_mac()` calls in `on_link_activity()` and `tick()`.

Build validation:
- Transmitter: `pio run -j 12` ✅
- Receiver: `pio run -e lilygo-t-display-s3_tft -j 12` ✅

### 4.2 — JSON response pipeline canonicalised ✅

Problem:
- The architecture split between `HttpJsonUtils` (common primitives) and `ApiResponseUtils` (receiver higher-level helpers) was not documented, risking future handlers bypassing `ApiResponseUtils` and calling primitives directly.
- `send_json_doc()` serialised to an Arduino `String` (heap allocation) before passing to `HttpJsonUtils::send_json()`.

Actions:
- Added canonical pipeline architecture comment block to `http_json_utils.h` identifying it as the primitive layer and directing callers to use `ApiResponseUtils`.
- Added matching pipeline comment to `api_response_utils.h` identifying it as the higher-level receiver layer.
- Rewrote `send_json_doc()`:
  - Uses `measureJson(doc)` first to determine actual serialised length.
  - Stack path (≤512 bytes): serialises directly to a 513-byte stack buffer — zero heap allocation.
  - Heap path (>512 bytes): allocates exactly `json_len + 1` bytes via `new (std::nothrow)` — one right-sized allocation.
  - Both paths call `HttpJsonUtils::send_json()` as before; intermediate `String` copy eliminated.

Build validation:
- Receiver: `pio run -e lilygo-t-display-s3_tft -j 12` ✅

### 4.3 — Battery Emulator datalayer ownership boundary documented ✅

Problem:
- `src/datalayer/datalayer.h` and `src/battery_emulator/datalayer/datalayer.h` were independent copies with no documented reason for the duplication, risking silent divergence.
- A full redirect shim (as used for `system_settings.h`) was not possible because the local copy adds typedef aliases resolving upstream enum naming conflicts.

Actions:
- Added OWNERSHIP BOUNDARY comment to `src/datalayer/datalayer.h` documenting it as a LOCAL EXTENDED COPY; maintenance contract (struct sync, typedef-only diff); future goal (collapse to shim post enum resolution).
- Added UPSTREAM BOUNDARY comment to `src/battery_emulator/datalayer/datalayer.h` with back-reference to local copy and sync reminder.

Build validation:
- Transmitter: `pio run -j 12` ✅

### 4.4 — Settings cache/persistence pattern formalised ✅

Problem:
- The cache/persistence pattern used in `transmitter_settings_cache` was implicit and undocumented as a reusable contract.

Actions:
- Added five-rule CACHE / PERSISTENCE PATTERN CONTRACT comment block to `transmitter_settings_cache.h`:
  1. STORAGE — value-typed plain-old-data structs, no heap pointers.
  2. ACCESSORS — symmetric `store_*` / `get_*` / `has_*` triplet; `has_*()` true after first store.
  3. PERSISTENCE — `load_from_prefs()` / `save_to_prefs()` via NVS Preferences.
  4. THREAD SAFETY — no internal mutex; callers responsible.
  5. OWNERSHIP — owns receiver-side cache of transmitter-reported settings only.

---

## 15) Current status snapshot (as of 2026-04-02)

- Section 3 (all findings): ✅ complete
- Section 4.1 (shared ESP-NOW MAC utilities): ✅ complete
- Section 4.2 (JSON pipeline canonicalised): ✅ complete
- Section 4.3 (datalayer boundary documented): ✅ complete
- Section 4.4 (settings cache pattern formalised): ✅ complete

Latest validation:
- Receiver: `pio run -e lilygo-t-display-s3_tft -j 12` ✅
- Transmitter: `pio run -j 12` ✅
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
