# Receiver Codebase Full Audit (Legacy/Orphan + Efficiency/Readability + Static Heat Test)

**Date:** 2026-03-21  
**Project audited:** `espnowreceiver_2`  
**Requested scope:** whole receiver codebase, identify legacy/orphan code, identify improvement opportunities (efficiency/readability), identify hot paths and bloat, suggest rewrites.

---

## 1) Audit Method Used

This audit used:

1. **Full source inventory** across `src/`, `lib/`, `include/`, `test/`.
2. **Legacy/orphan scan** via symbol/reference checks.
3. **Build-path validation** by compiling **`receiver_tft`** (`pio run -e receiver_tft`) and inspecting compiled translation units.
4. **Static heat test** (execution-frequency risk analysis) based on callback/task loop structure and timing intervals.
5. **Bloat analysis** via top file line counts and mismatch between active backend and compiled files.

> Note: This is a **static + build-path** audit (not runtime profiler sampling). For MCU firmware this is still highly actionable because hot code paths are architecture-driven (callbacks, queue workers, and periodic tasks).

---

## 2) Executive Summary

### Overall status
- Architecture is generally modular and functional.
- Build is passing for `receiver_tft`.
- **Legacy and orphan code still remains in active build path**.

### Highest-impact findings
1. **Confirmed orphan legacy state layer is still compiled**:
   - `src/state/receiver_state_manager.cpp` (**584 lines**)
   - `src/state/connection_state_manager.cpp` (**215 lines**)
   - These have effectively no call-site usage outside their own implementation files.
2. **Deprecated test receiver stubs still compiled**:
   - `src/test/test_data.cpp` (contains deprecated stubs and heap allocations via `new` for references).
3. **Obsolete timing module still compiled**:
   - `src/time/time_sync_manager.cpp` (stub, no active references).
4. **Backend mismatch bloat in `receiver_tft` build**:
   - LVGL-specific source files are still being compiled in TFT env:
     - `src/display/pages/status_page_lvgl.cpp` (94 lines)
     - `src/display/widgets/power_widget_lvgl.cpp` (258 lines)
     - `src/display/widgets/soc_widget_lvgl.cpp` (146 lines)
5. **Very large/complex files in hot control areas** reduce readability and increase defect risk:
   - `src/espnow/espnow_tasks.cpp` (948 lines)
   - `lib/webserver/pages/settings_page.cpp` (999 lines)
   - `lib/webserver/api/api_control_handlers.cpp` (525 lines)

---

## 3) Legacy and Orphan Code Findings (Confirmed)

## 3.1 Orphaned state-wrapper subsystem (high confidence)

### Files
- `src/state/receiver_state_manager.h/.cpp`
- `src/state/connection_state_manager.h/.cpp`

### Evidence
- `receiver_state_manager` classes are referenced only within their own translation unit and declarations.
- `connection_state_manager` functions are referenced primarily within its own implementation; current data path uses `RxStateMachine` + `BatteryData` directly.
- `src/common.h` includes `state/receiver_state_manager.h`, forcing header exposure globally despite no active runtime usage.

### Impact
- Unnecessary compile and review surface.
- Potential unnecessary static/global initialization overhead.
- Increases confusion about the canonical state owner.

### Recommendation
- Remove these modules from active build path immediately (or archive).
- Remove include from `common.h` and enforce canonical path:
  - connection/data state: `RxStateMachine` + `BatteryData::TelemetrySnapshot`

---

## 3.2 Deprecated test-mode compatibility stubs still compiled

### File
- `src/test/test_data.cpp` (26 lines)

### Evidence
- Explicit deprecated comments.
- Exposes global reference variables allocated with `new` purely for compatibility.
- No active runtime task wiring found.

### Impact
- Risky pattern (`new` during static initialization) for no functional value.
- Noise in production firmware.

### Recommendation
- Remove from production source set; keep only in dedicated test builds if needed.

---

## 3.3 Obsolete time manager remains in build

### Files
- `src/time/time_sync_manager.h`
- `src/time/time_sync_manager.cpp` (stub)

### Evidence
- No active include/reference usage in runtime.

### Recommendation
- Remove file pair or move to archive docs; exclude from build.

---

## 3.4 Legacy helper functions likely dead

### File
- `src/helpers.cpp`

### Functions flagged
- `get_power_color(...)`
- `calculate_checksum(...)`

### Evidence
- No meaningful call sites found in active receiver runtime path.

### Recommendation
- Remove or mark `static` in narrower scope where required.

---

## 4) Static Heat Test (What Runs Most + Optimization Targets)

## Tier H0 (highest frequency / most performance-sensitive)

1. **WiFi receive callback**: `on_data_recv(...)` in `src/espnow/espnow_callbacks.cpp`
   - Runs in WiFi task context.
   - Current design is good: minimal work + queue handoff.
   - Keep callback lean; never add JSON parsing or heavy logs here.

2. **ESP-NOW worker loop**: `task_espnow_worker(...)` in `src/espnow/espnow_tasks.cpp`
   - Central ingestion/routing path.
   - Handles queue receive, route dispatch, connection updates, peer registration, stale checks.

## Tier H1 (high frequency)

3. **Display renderer loop**: `DisplayUpdateQueue::task_renderer(...)`
   - High update cadence under live telemetry.
   - Mutex contention with other display paths can drop frames.

4. **MQTT loop task**: `task_mqtt_client(...)`
   - Runs every 100ms and may reconnect/loop continuously.

## Tier H2 (bursty but heavy)

5. **OTA API streaming path**: `api_ota_upload_handler(...)` in `lib/webserver/api/api_control_handlers.cpp`
   - Very large function with prolonged loops + dynamic `String` building + repeated poll waits.

---

## 5) Efficiency and Readability Improvement Opportunities

## 5.1 Split giant control file (`espnow_tasks.cpp`) into pipeline stages

### Current risk
- 948-line monolith mixes route registration, connection policy, queue processing, handlers, and sync policy.

### Rewrite proposal
Split into:
- `espnow_route_registry.cpp`
- `espnow_worker_loop.cpp`
- `espnow_settings_sync.cpp`
- `espnow_handler_data_legacy.cpp` (temporary migration bucket)

### Expected outcome
- Easier review/testability.
- Lower regression risk when adjusting retries/timeouts.

---

## 5.2 Remove dual/parallel state ownership

### Current risk
- Multiple conceptual state owners remain (`RxStateMachine`, `BatteryData`, legacy state wrappers).

### Rewrite proposal
- Keep **single canonical runtime state**:
  - Link/message state: `RxStateMachine`
  - Telemetry payload state: `BatteryData::TelemetrySnapshot`
- Remove `receiver_state_manager` and `connection_state_manager` from build and code references.

### Expected outcome
- Lower lock contention and clearer invariants.

---

## 5.3 Tighten `receiver_tft` source filter (remove backend bloat)

### Current issue
`receiver_tft` build compiles LVGL-specific widget/page sources.

### Recommendation
In `platformio.ini` (`env:receiver_tft`), exclude at minimum:
- `-<display/pages/status_page_lvgl.cpp>`
- `-<display/widgets/power_widget_lvgl.cpp>`
- `-<display/widgets/soc_widget_lvgl.cpp>`

### Expected outcome
- Smaller compile surface and lower binary bloat risk.
- Cleaner backend separation.

---

## 5.4 Refactor OTA upload handler into stateful streaming helper

### Current issue
`api_ota_upload_handler(...)` is long and mixes:
- session/challenge negotiation,
- HTTP header generation,
- stream-forward loop,
- early response parsing,
- final response handling.

### Rewrite proposal
Extract components:
- `OtaSessionChallenge acquireOtaChallenge(...)`
- `bool forwardFirmwareStream(...)`
- `TransmitterHttpResult readTransmitterResponse(...)`

Use fixed-size buffers/structs where possible; minimize `String` concatenation in tight loops.

### Expected outcome
- Better maintainability and less heap-fragmentation risk.

---

## 5.5 Remove deprecated stubs and migrate to feature flags

### Current issue
Deprecated `test_data` stubs still compile in production.

### Recommendation
- Move legacy stubs behind explicit compile-time flag (`RECEIVER_LEGACY_TEST_STUBS=1`) default OFF.
- Or remove entirely and keep compatibility only in archived branch.

---

## 6) Bloat Hotspots by File Size (Top)

- `lib/webserver/pages/settings_page.cpp` — 999 lines
- `src/espnow/espnow_tasks.cpp` — 948 lines
- `lib/webserver/pages/ota_page.cpp` — 788 lines
- `lib/webserver/pages/network_config_page.cpp` — 688 lines
- `src/display/tft_impl/tft_display.cpp` — 657 lines
- `lib/webserver/api/api_control_handlers.cpp` — 525 lines

### Recommendation
Introduce a max file-size guideline (e.g. soft cap ~350 lines for `.cpp` except generated/page templates) and split by responsibility.

---

## 7) Suggested Cleanup Plan (priority order)

## P0 (Do first)
1. Exclude/remove confirmed orphan modules from active build:
   - `src/state/receiver_state_manager.*`
   - `src/state/connection_state_manager.*`
   - `src/time/time_sync_manager.*`
   - `src/test/test_data.*` (prod build)
2. Tighten `receiver_tft` source filter to exclude LVGL widget/page files.

## P1
3. Refactor `espnow_tasks.cpp` into 3–4 focused files.
4. Refactor OTA upload handler into smaller functions + helper structs.

## P2
5. Split oversized web page sources by section/component.
6. Remove dead helper utilities (`get_power_color`, `calculate_checksum`) if no call sites remain.

---

## 8) Candidate Full Rewrites (same behavior, cleaner implementation)

1. **ESP-NOW worker pipeline rewrite**
   - Replace giant switch/route + side effects with staged processing pipeline:
     - validate → state update → dispatch → post-actions.
2. **OTA forwarder rewrite**
   - Encapsulate stream forwarding in a reusable utility with deterministic timeout/error states.
3. **State model rewrite**
   - Delete legacy state wrappers; expose read-only snapshot adapters for UI/web layers.
4. **Display backend separation rewrite**
   - Ensure TFT/LVGL source sets are mutually exclusive per env.

## 8.5) Second-pass addendum: non-CRC high-use function analysis

This second pass explicitly focused on non-CRC runtime hot functions.

### A) `task_espnow_worker(...)` + message handlers (`src/espnow/espnow_tasks.cpp`)

Findings:
- The worker loop does queue receive, connection state updates, peer registration checks, router dispatch, stale checks, and periodic stats logging in one function.
- `handle_data_message(...)` and `handle_packet_events(...)` both repeat cache/store/display update flow.
- Multiple singleton calls are repeated in hot path (`EspNowConnectionManager::instance()`, `TransmitterManager`, `ReceiverConnectionHandler`, `RxStateMachine`).

Improvement:
1. Split into staged helpers:
   - `pre_route_link_updates(...)`
   - `dispatch_message(...)`
   - `post_route_metrics(...)`
2. Merge duplicate post-processing in `handle_data_message(...)` and `handle_packet_events(...)` into a shared helper (`apply_telemetry_sample(...)`).
3. Cache manager references once per loop iteration (`auto& conn_mgr = ...`) to reduce repeated singleton lookup overhead and improve readability.

### B) `MqttClient::messageCallback(...)` + catalog handlers (`src/mqtt/mqtt_client.cpp`)

Findings:
- Topic dispatch is linear `strcmp` matching over route table per message.
- Catalog handlers allocate/free dynamic arrays (`new TypeCatalogCache::TypeEntry[128]`) on each catalog payload.
- Several handlers create large `DynamicJsonDocument` instances per callback (`4096`, `3072`, `6144` bytes).

Improvement:
1. Replace linear topic dispatch with precomputed hash/switch or a compact fixed map keyed by topic pointer hash.
2. Remove per-message heap churn in catalog handlers by using fixed-capacity stack/local buffers (or a reusable static scratch arena guarded by callback context).
3. Keep JSON parsing but reduce peak churn by reusing document objects where callback sequencing allows.

### C) `ReceiverConnectionHandler::send_initialization_requests(...)` + `tick()` (`src/espnow/rx_connection_handler.cpp`)

Findings:
- `send_initialization_requests(...)` repeats request construction + `esp_now_send(...)` boilerplate many times.
- `tick()` mixes retry scheduling and send actions for multiple independent retry channels in one large block.

Improvement:
1. Add small helper wrappers:
   - `send_config_request(config_section_t section)`
   - `send_request_data(subtype_t subtype)`
2. Move retry engine to table-driven descriptors (request kind, predicate, counter, max, sender fn) to remove repeated branch blocks.
3. Keep behavior identical (same intervals, retry counts, and order) while reducing cyclomatic complexity.

### Priority for implementation

P0 (safe, high value):
1. Extract shared telemetry post-processing helper from `handle_data_message(...)` / `handle_packet_events(...)`.
2. Refactor init-send boilerplate in `rx_connection_handler.cpp` into helper wrappers.

P1 (safe, medium effort):
3. Table-drive retry logic in `ReceiverConnectionHandler::tick()`.
4. Replace per-message `new[]` catalog buffers in `mqtt_client.cpp`.

P2 (optional optimization):
5. Replace linear topic routing in `messageCallback(...)` with hashed dispatch.
6. Reuse JSON document buffers where practical.

---

## 9) Risk Notes

- This audit found no immediate correctness blocker in the active `receiver_tft` build.
- Main risk is **maintenance and regression risk** from parallel legacy paths and oversized functions.
- Removing orphan code before additional feature work will reduce future bug probability and review time.

---

## 10) Final Conclusion

The receiver codebase is functional and modular in many areas, but still carries measurable legacy/orphan drag and build-path bloat. The biggest immediate win is to remove unreferenced state/test/time modules and tighten environment source filters, then split `espnow_tasks.cpp` and OTA control paths into focused units.

## 10.1) Implementation progress update (2026-03-22, Wave 1)

Completed:
1. Removed orphan modules from source tree:
   - `src/state/receiver_state_manager.h/.cpp`
   - `src/state/connection_state_manager.h/.cpp`
   - `src/time/time_sync_manager.h/.cpp`
   - `src/test/test_data.h/.cpp`
2. Removed stale includes/handles tied to removed modules:
   - Removed `state/receiver_state_manager.h` include from `src/common.h`
   - Removed `task_test_data` declaration/definition from `src/common.h` and `src/globals.cpp`
   - Removed stale include of `state/connection_state_manager.h` from `src/state_machine.cpp`
3. Preserved webserver link compatibility by defining legacy test-mode globals in `src/globals.cpp` and normalizing extern declarations in webserver sources.
4. Tightened `receiver_tft` source filter to exclude LVGL page/widget files from TFT builds:
   - `-<display/pages/status_page_lvgl.cpp>`
   - `-<display/widgets/power_widget_lvgl.cpp>`
   - `-<display/widgets/soc_widget_lvgl.cpp>`
5. Extracted shared telemetry post-processing helper in `src/espnow/espnow_tasks.cpp`:
   - Added `apply_telemetry_sample(...)`
   - Refactored both `handle_data_message(...)` and `handle_packet_events(...)` to use the shared helper.
6. Reduced initialization/request-send boilerplate in `src/espnow/rx_connection_handler.cpp`:
   - Added local send wrappers `send_config_section_request(...)` and `send_request_data_message(...)`
   - Refactored `send_initialization_requests(...)` to a table-driven section loop (`MQTT`, `NETWORK`, `METADATA`, `BATTERY`)
   - Reused the request-data wrapper in `tick()` retry send path.
7. Refactored remaining catalog retry logic in `src/espnow/rx_connection_handler.cpp`:
   - Replaced repeated retry condition/send blocks with a descriptor-table loop (`label`, `should_request`, `send_fn`, `retry_count`).
   - Preserved retry gating (`CATALOG_RETRY_INITIAL_DELAY_MS`, `CATALOG_RETRY_INTERVAL_MS`, `CATALOG_MAX_RETRIES`) and per-request retry counters.
8. Build verification completed:
   - `pio run -e receiver_tft` ✅ SUCCESS
9. Removed per-message heap allocations in MQTT catalog handlers (`src/mqtt/mqtt_client.cpp`):
   - Replaced `new TypeCatalogCache::TypeEntry[128]` / `delete[]` with fixed-capacity reusable scratch buffers (`std::array<TypeCatalogCache::TypeEntry, 128>`) in both `handleBatteryTypeCatalog(...)` and `handleInverterTypeCatalog(...)`, keeping the parse workspace off the MQTT task stack.
   - Preserved parsing behavior, bounds checks, and `TypeCatalogCache::replace_*_entries(...)` handoff.
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS
10. Replaced linear MQTT topic routing with hash-based dispatch (`src/mqtt/mqtt_client.cpp`):
   - `messageCallback(...)` now uses FNV-1a hash switch dispatch with `strcmp` collision guards for exact-match safety.
   - Preserved all existing route handlers and fallback behavior for unknown topics.
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS
11. Applied hot-loop local-reference cleanup in `src/espnow/espnow_tasks.cpp`:
   - `task_espnow_worker(...)` now caches `RxStateMachine`, `ReceiverConnectionHandler`, and `EspNowConnectionManager` references once at task scope instead of repeatedly calling singleton accessors for each dequeued message.
   - Preserved queue handling, liveness updates, peer registration flow, and state-machine transitions.
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS
12. Increased MQTT task stack headroom in `src/config/task_config.h`:
   - Raised `TaskConfig::MQTT_CLIENT_STACK` from `8192` to `10240` bytes for embedded runtime safety margin under current JSON/catalog/logging workload.
   - Removed temporary MQTT stack-watermark telemetry from `src/mqtt/mqtt_task.cpp`.
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS
13. Began OTA upload-path decomposition in `lib/webserver/api/api_control_handlers.cpp`:
   - Extracted shared transmitter HTTP response parsing into `read_http_response_from_transmitter(...)` and replaced duplicated inline parsing blocks in `api_ota_upload_handler(...)`.
   - Preserved early-response rejection handling and final response parsing behavior while reducing duplication/cyclomatic complexity.
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS
14. Continued OTA upload-path decomposition in `lib/webserver/api/api_control_handlers.cpp`:
   - Extracted OTA challenge acquisition/arming flow into focused helpers:
     - `store_ota_challenge(...)`
     - `try_get_prearmed_ota_challenge(...)`
     - `try_arm_ota_challenge(...)`
     - `acquire_ota_session_challenge(...)`
   - Reduced `api_ota_upload_handler(...)` complexity by removing inline `/api/ota_status` + `/api/ota_arm` negotiation logic and reusing a single challenge struct for outbound auth headers.
   - Preserved existing fallback/error behavior (including explicit OTA secret provisioning guidance) and logging semantics.
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS
15. Continued OTA upload-path decomposition in `lib/webserver/api/api_control_handlers.cpp`:
   - Extracted firmware stream-forward loop from `api_ota_upload_handler(...)` into `forward_ota_stream_to_transmitter(...)` with explicit typed status via `OtaForwardError`/`OtaForwardResult`.
   - Preserved early transmitter-rejection parsing, upload-receive failure handling, and stream-stall timeout behavior while reducing inline loop complexity in the handler.
   - Kept final transmitter response wait/parse path unchanged and reused `OtaForwardResult.total_forwarded` for final success logging.
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS
16. Completed OTA upload-path decomposition in `lib/webserver/api/api_control_handlers.cpp`:
   - Extracted TCP connection setup + full HTTP request header write into `open_ota_transmitter_connection(WiFiClient&, const OtaSessionChallenge&, const char*, size_t)` — returns `bool` (false on connect failure).
   - Extracted 70-second response-wait polling loop + `read_http_response_from_transmitter(...)` call into `await_and_parse_ota_response(WiFiClient&)` — returns `OtaResponseResult` with typed `State` enum (`Ok`, `TimedOut`, `ParseFailed`) replacing three separate inline error paths.
   - Added `OtaResponseResult` struct (with nested `State` enum) to the anonymous namespace alongside existing typed result structs.
   - `api_ota_upload_handler(...)` now consists entirely of named helper calls with explicit error dispatch; all inline TCP/header/wait logic removed.
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS (RAM 33.0%, Flash 19.2%)
17. Split `src/espnow/espnow_tasks.cpp` (1020 lines) into three focused compilation units:
   - `espnow_tasks_internal.h` — new internal shared header declaring `apply_telemetry_sample(...)`, `store_transmitter_mac(...)`, and all handler function signatures; used by all three TUs.
   - `espnow_message_handlers.cpp` — data, packet, LED, and debug message handler implementations (`handle_data_message`, `handle_flash_led_message`, `handle_debug_ack_message`, `handle_packet_events`, `handle_packet_logs`, `handle_packet_cell_info`, `handle_packet_unknown`).
   - `espnow_settings_sync.cpp` — Phase 2 settings synchronisation handlers (`handle_settings_update_ack`, `handle_settings_changed`, `handle_component_apply_ack_message`) and private `request_category_refresh(...)` helper (static within that TU).
   - `espnow_tasks.cpp` reduced from 1020 lines to 736 lines; retains includes, globals, utility helpers (`apply_telemetry_sample`, `store_transmitter_mac`, `update_received_data_cache`, `estimate_voltage_mv`, `update_display_if_changed`), `setup_message_routes()`, and `task_espnow_worker()`.
   - `apply_telemetry_sample` and `store_transmitter_mac` promoted from `static` to external linkage (declared via internal header; defined once in `espnow_tasks.cpp`).
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS (RAM 33.0%, Flash 19.2% — identical binary footprint)
18. P2 dead-helper cleanup started (`src/helpers.*`):
   - Removed unused `get_power_color(...)` declaration and implementation from `src/helpers.h/.cpp` (no production call sites).
   - Kept `pre_calculate_color_gradient(...)` and `calculate_checksum(...)` intact because they are still exercised by helper tests.
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS.
19. P2 web page decomposition started with `settings_page` split into focused modules:
   - Added `lib/webserver/pages/settings_page_script.h/.cpp` and moved the full inline JavaScript payload out of `lib/webserver/pages/settings_page.cpp` into `get_settings_page_script()`.
   - Added `lib/webserver/pages/settings_page_content.h/.cpp` and moved the HTML content composition (including nav button generation) into `get_settings_page_content()`.
   - Updated `settings_page.cpp` to orchestrate only page assembly via `get_settings_page_content()` + `get_settings_page_script()` and `renderPage(...)`, preserving route registration/behavior.
   - Removed now-unused includes from `settings_page.cpp` (`../utils/transmitter_manager.h`, `../common/nav_buttons.h`).
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS (RAM 33.0%, Flash 19.2%; final Flash 19.2% with +144 bytes).
20. Continued P2 web page decomposition with `ota_page` split into focused modules:
   - Added `lib/webserver/pages/ota_page_script.h/.cpp` and moved the full inline JavaScript payload out of `lib/webserver/pages/ota_page.cpp` into `get_ota_page_script()`.
   - Added `lib/webserver/pages/ota_page_content.h/.cpp` and moved OTA page HTML composition (including nav button generation) into `get_ota_page_content()`.
   - Updated `ota_page.cpp` to orchestrate page assembly only (`get_ota_page_content()` + `get_ota_page_script()` + `renderPage(...)`), preserving route registration/OTA behavior.
   - Removed now-unused includes from `ota_page.cpp` (`../common/nav_buttons.h`, `../utils/transmitter_manager.h`).
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS (RAM 33.0%, Flash 19.2%; final Flash 19.2% with +196 bytes).
21. Continued P2 web page decomposition with `network_config_page` modular extraction:
   - Added `lib/webserver/pages/network_config_page_content.h/.cpp` and moved dynamic page HTML composition into `get_network_config_page_content(bool isAPMode)` (including AP-mode conditional layout and nav generation).
   - Added `lib/webserver/pages/network_config_page_style.h/.cpp` and extracted all page-scoped CSS into `get_network_config_page_style()`.
   - Added `lib/webserver/pages/network_config_page_script.h/.cpp` and extracted full page JavaScript into `get_network_config_page_script()`.
   - Updated `network_config_page.cpp` to an orchestration flow only: AP-mode detection + content/style/script assembly + `renderPage(...)` + unchanged route registration.
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS (RAM 33.0%, Flash 19.2%; final Flash 19.2% with +220 bytes).
22. Continued P2 web page decomposition with `battery_settings_page` modular extraction:
   - Added `lib/webserver/pages/battery_settings_page_content.h/.cpp` and moved battery settings HTML composition (including nav generation) into `get_battery_settings_page_content()`.
   - Added `lib/webserver/pages/battery_settings_page_script.h/.cpp` and moved full page JavaScript logic into `get_battery_settings_page_script()`.
   - Updated `battery_settings_page.cpp` handler to assemble content/script via extracted modules and keep the same `renderPage(...)` and route behavior.
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS (RAM 33.0%, Flash 19.2%; final Flash 19.2% with +172 bytes).
23. Continued P2 web page decomposition with `dashboard_page` modular extraction:
   - Added `lib/webserver/pages/dashboard_page_content.h/.cpp` and moved dashboard HTML assembly into `get_dashboard_page_content(...)`, keeping transmitter/receiver card values injected from the handler.
   - Added `lib/webserver/pages/dashboard_page_script.h/.cpp` and moved the full dashboard polling/event-log/time-status JavaScript into `get_dashboard_page_script()`.
   - Updated `dashboard_page.cpp` to orchestration-only flow: collect current transmitter/receiver summary fields, assemble content/script, and render via `renderPage(...)`.
   - Removed the now-unused local `request_metadata` state from `dashboard_page.cpp` while preserving route behavior and live refresh semantics.
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS (RAM 33.0%, Flash 19.2%; final Flash 19.2% with 1539041 bytes flash used).
24. Repaired cleanup regression in `network_config_page.cpp` and re-validated modular page architecture:
   - Replaced the malformed/truncated legacy-disabled block (unterminated `#if 0` + raw string) with a clean orchestration-only implementation.
   - `network_config_page.cpp` now consistently delegates to `get_network_config_page_content(...)`, `get_network_config_page_style()`, and `get_network_config_page_script()`, then renders via `renderPage(...)` with unchanged route registration.
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS (RAM 33.0%, Flash 19.2%; final flash used 1539041 bytes).
25. Continued P2 web page decomposition with `systeminfo_page` modular extraction:
   - Added `lib/webserver/pages/systeminfo_page_content.h/.cpp` and moved receiver configuration HTML composition (including nav generation) into `get_systeminfo_page_content()`.
   - Added `lib/webserver/pages/systeminfo_page_script.h/.cpp` and moved the full configuration UI JavaScript into `get_systeminfo_page_script()`.
   - Updated `systeminfo_page.cpp` to orchestration-only flow (content/script assembly + `renderPage(...)`) while preserving route registration and runtime behavior.
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS (RAM 33.0%, Flash 19.3%; final flash used 1539233 bytes).
26. Continued P2 web page decomposition with `transmitter_hub_page` modular extraction:
   - Added `lib/webserver/pages/transmitter_hub_page_content.h/.cpp` and moved dynamic transmitter hub HTML composition into `get_transmitter_hub_page_content(...)`.
   - Added `lib/webserver/pages/transmitter_hub_page_script.h/.cpp` and moved all page JavaScript (firmware metadata polling + test data mode control) into `get_transmitter_hub_page_script()`.
   - Updated `transmitter_hub_page.cpp` to orchestration-only flow: collect runtime transmitter summary fields, assemble content/script modules, and render via `renderPage(...)` with unchanged route registration.
   - Build verification repeated: `pio run -e receiver_tft` ✅ SUCCESS (RAM 33.0%, Flash 19.3%; final flash used 1539177 bytes).
27. Continued P2 web page decomposition with `cellmonitor_page` modular extraction:
   - Added `lib/webserver/pages/cellmonitor_page_content.h/.cpp` and moved cell monitor HTML (data source status bar, statistics grid, cell voltage grid, voltage distribution bar chart) into `get_cellmonitor_page_content()`.
   - Added `lib/webserver/pages/cellmonitor_page_script.h/.cpp` and moved the full SSE-based cell data JavaScript (SSE connection to `/api/cell_stream`, `renderCells`, `renderVoltageBar`, `updateBarHighlight`, `highlightCell`, exponential reconnect) into `get_cellmonitor_page_script()`.
   - Updated `cellmonitor_page.cpp` to orchestration-only flow: delegates to content/script modules and renders via `renderPage("Cell Monitor", content, PageRenderOptions("", script))` with unchanged route (`/cellmonitor`) and handler registration.
   - Build verification: `pio run -e receiver_tft` ✅ SUCCESS (RAM 33.0%, Flash 19.3%).

28. Continued P2 web page decomposition with `inverter_specs_display_page` modular extraction:
   - Added `lib/webserver/pages/inverter_specs_display_page_content.h/.cpp` exposing `get_inverter_specs_page_html_header()` (static CSS + page preamble) and `get_inverter_specs_section_fmt()` (printf-compatible format template for the specs data grid).
   - Added `lib/webserver/pages/inverter_specs_display_page_script.h/.cpp` exposing `get_inverter_specs_page_html_footer()` (nav buttons + interface-name-fetch JS + page closing tags).
   - Updated `inverter_specs_display_page.cpp` to orchestration-only flow: JSON parsing, fallback protocol name lookup, PSRAM buffer allocation, snprintf assembly using extracted module functions, send. Route `/inverter_settings.html` unchanged.
29. Continued P2 web page decomposition with `battery_specs_display_page` modular extraction:
   - Added `lib/webserver/pages/battery_specs_display_page_content.h/.cpp` exposing `get_battery_specs_page_html_header()` and `get_battery_specs_section_fmt()`.
   - Added `lib/webserver/pages/battery_specs_display_page_script.h/.cpp` exposing `get_battery_specs_page_html_footer()` (nav buttons + type/interface label-fetch JS + page closing tags).
   - Updated `battery_specs_display_page.cpp` to orchestration-only flow: JSON parsing, PSRAM buffer allocation, snprintf assembly, send. Route `/battery_settings.html` unchanged.
30. Continued P2 web page decomposition with `inverter_settings_page` modular extraction:
   - Added `lib/webserver/pages/inverter_settings_page_content.h/.cpp` and moved inverter protocol/interface selector HTML (including nav generation) into `get_inverter_settings_page_content()`.
   - Added `lib/webserver/pages/inverter_settings_page_script.h/.cpp` and moved the full settings JavaScript (dropdown population with retry, change tracking, SaveOperation.runComponentApply) into `get_inverter_settings_page_script()`.
   - Updated `inverter_settings_page.cpp` to orchestration-only flow via `renderPage(...)` with `PageRenderOptions`. Route `/transmitter/inverter` unchanged.
31. Continued P2 web page decomposition with `hardware_config_page` modular extraction:
   - Added `lib/webserver/pages/hardware_config_page_content.h/.cpp` and moved breadcrumb, LED pattern selector, live LED runtime status card, and save button HTML (including nav generation) into `get_hardware_config_page_content()`.
   - Added `lib/webserver/pages/hardware_config_page_script.h/.cpp` and moved the embedded script block (LED mode change tracking, loadLiveLedStatus, loadHardwareSettings, saveHardwareSettings, resyncLedState, 2-second poll) into `get_hardware_config_page_script()`.
   - Updated `hardware_config_page.cpp` to orchestration-only flow via `renderPage("Hardware Config", content, PageRenderOptions("", script))`. Route `/transmitter/hardware` unchanged.
   - Build verification: `pio run -e receiver_tft` SUCCESS (RAM 33.0%, Flash 19.3%).
Still pending from receiver P0/P1:
1. No additional non-CRC Wave 1 receiver P0/P1 refactors pending in this track.
