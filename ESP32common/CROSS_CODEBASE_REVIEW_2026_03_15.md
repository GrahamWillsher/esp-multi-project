# Cross Codebase Review (Receiver + Transmitter)
**Date:** 2026-03-15  
**Scope:**
- `espnowreceiver_2`
- `ESPnowtransmitter2/espnowtransmitter2`
- shared contracts in `esp32common`

---

## Executive summary
Both codebases are functionally rich and already use strong shared protocol contracts, but maintainability risk is rising due to a few very large multi-responsibility files and repeated request/response boilerplate.

**Project-wide engineering policy (added):**
- Use `esp32common` code wherever feasible as the default implementation path.
- If a feature is not fully common today, prefer adapting/refactoring it into a reusable common abstraction instead of duplicating app-specific variants.
- App code should mostly provide configuration/policy bindings; shared behavior should live in common modules.

Highest-value next steps:
1. **Receiver HTTP rationalization** (modular handlers + shared JSON/SSE helpers).
2. **Transmitter message handler decomposition** (extract subtype handlers/builders).
3. **Cross-codebase de-duplication** for receiver webserver/SSE behavior and payload builders.

---

## Receiver review (`espnowreceiver_2`)

### Key findings

1. **HTTP API monolith is too large and multi-domain**  
   - `lib/webserver/api/api_handlers.cpp` contains many endpoint handlers in one file and a large registration table.  
   - Evidence: many handlers from `api_data_handler` through `api_ota_upload_handler`, plus `register_all_api_handlers` with large `handlers[]` list.

2. **SSE loop logic is duplicated and hand-managed**  
   - Separate long-lived streaming loops (`api_cell_data_sse_handler`, `api_monitor_sse_handler`) each implement timing, loop duration, ping/keepalive, and disconnect handling.

3. **HTTP body parsing + JSON response patterns are repeated**  
   - Repeated `httpd_req_recv(...)`, `deserializeJson(...)`, `httpd_resp_set_type(req, "application/json")`, and ad-hoc error responses across many handlers.

4. **ESPNOW route setup is dense in one source unit**  
   - `src/espnow/espnow_tasks.cpp` registers a large number of routes and contains many handler responsibilities in the same compilation unit.

5. **Endpoint semantics are inconsistent in a few places**  
   - Mutating operations exposed as `HTTP_GET` (for example `/api/setDebugLevel`) increase API ambiguity and client-side misuse risk.

### Receiver recommendations (prioritized)

#### P0 (do first): HTTP rationalization
- Split `api_handlers.cpp` into domain modules:
  - `api_telemetry_handlers.cpp`
  - `api_settings_handlers.cpp`
  - `api_network_handlers.cpp`
  - `api_debug_handlers.cpp`
  - `api_led_handlers.cpp`
  - `api_sse_handlers.cpp`
- Add a shared helper layer:
  - `http_json_utils.h/.cpp`: `read_json_body()`, `send_json_ok()`, `send_json_error()`
  - `http_sse_utils.h/.cpp`: shared keepalive + loop duration + disconnect-detect logic
- Keep one small `api_registration.cpp` containing only route list + registration.

#### P1
- Move GET mutators to POST/PUT where practical (`setDebugLevel`, etc.).
- Introduce consistent API error shape (`{success:false,error:"...",code:"..."}`).

#### P2
- Move heavily stateful SSE data gathering into a dedicated service to reduce handler blocking complexity.

---

## Transmitter review (`ESPnowtransmitter2/espnowtransmitter2`)

### Key findings

1. **Message-handler decomposition delivered the main maintainability win**  
   - `src/espnow/message_handler.cpp` no longer owns all route registration and subtype-specific behavior; request, control, network, MQTT, route, and catalog logic have been split into dedicated modules.

2. **Battery settings payload construction is now centralized**  
   - Active battery settings serialization/checksum logic was consolidated so the transmitter no longer duplicates the same payload-building flow across multiple request branches.

3. **Legacy mixed-response behavior has been removed**  
   - `subtype_settings` no longer emits the old IP + battery mixed response path; granular `subtype_network_config` and `subtype_battery_config` are the active implementation.

4. **Settings manager remains the largest concentrated domain surface**  
   - `src/settings/settings_manager.cpp` still owns broad validation and update responsibilities across multiple categories and is the most obvious remaining transmitter refactor candidate.

5. **Minor hygiene cleanup was completed**  
   - The duplicate `espnow/heartbeat_manager.h` include in `src/main.cpp` was removed.

### Transmitter recommendations (remaining)

#### P1
- Add focused unit tests for payload-build correctness and checksum invariants.

#### P2
- Split `SettingsManager` by domain (`battery`, `power`, `inverter`, `can`, `contactor`) with shared validation interface.

---

## Cross-codebase findings and opportunities

1. **Receiver webserver logic exists in two places**  
   - `espnowreceiver_2/lib/webserver/api/api_handlers.cpp` and `esp32common/webserver/receiver_webserver.cpp` both implement receiver HTTP/SSE behavior patterns, increasing drift risk.

2. **Concrete duplication defect observed in shared receiver webserver path**  
   - `receiver_webserver.cpp` repeats `last_soc`/`last_power` assignments consecutively in SSE update block.

3. **Shared protocol contract is good, but serialization/mapping helpers are still scattered**  
   - `esp32common/espnow_transmitter/espnow_common.h` defines wire enums/messages; mapping logic (for example LED status/effect mapping) remains distributed in app-specific files.

4. **Large route/handler tables in both RX and TX suggest a common registration pattern can be standardized**  
   - A small internal registration DSL/helper could reduce repeated boilerplate and registration mistakes.

### Cross-codebase recommendations

#### P0
- Define clear ownership boundary:
  - `esp32common`: contracts, common helpers, reusable HTTP/SSE primitives.
  - app repos: feature handlers and app-specific policy.
- Enforce **common-first implementation rule**:
   - New shared behavior must be evaluated for `esp32common` placement first.
   - Duplicated logic in RX/TX should be migrated into common utilities before adding new variants.

#### P1
- Introduce shared helper modules in `esp32common` for:
  - JSON response generation,
  - request body parsing with limits,
  - SSE keepalive/session lifecycle.
- For “almost common” features/functions, add compatibility seams so they become reusable:
   - Extract device/framework dependencies behind narrow interfaces.
   - Parameterize repo-specific constants/limits.
   - Separate policy from mechanism (common = mechanism, app = policy).
   - Standardize input/output contracts so both RX/TX can consume the same helper.

#### P2
- Add protocol-focused integration tests (RX/TX) for:
  - data request/abort lifecycle,
  - settings payload compatibility,
  - LED wire enum mapping expectations.
- Add a migration checklist for converting non-common code to common modules:
   - Identify duplicate or near-duplicate logic.
   - Extract reusable core into `esp32common`.
   - Keep thin adapter layers in RX/TX for local wiring only.
   - Remove legacy duplicated paths after validation.

---

## Suggested implementation roadmap

### Sprint 1 (highest ROI)
- Receiver: split API file + add JSON/SSE helper layer.
- Transmitter: extract battery settings message builder + remove duplicate include.

### Sprint 2
- Transmitter: split message handler into domain files.
- Receiver: normalize endpoint methods (GET→POST for mutators).

### Sprint 3
- Cross-codebase: migrate reusable HTTP/SSE utilities to `esp32common`, then consume from both projects.

### Implementation progress (live)
- **2026-03-15 (Receiver P0 started):** Added shared JSON helper layer and removed redundant body-read/JSON response boilerplate from key POST handlers.
   - Added: `lib/webserver/utils/http_json_utils.h/.cpp`
   - Refactored handlers in `lib/webserver/api/api_handlers.cpp`:
      - `api_set_data_source_handler`
      - `api_save_setting_handler`
      - `api_save_receiver_network_handler`
      - `api_save_network_config_handler`
      - `api_save_mqtt_config_handler`
      - `api_set_test_data_mode_handler`
   - Result: request body reading is now partial-read safe in `api_handlers.cpp` and repeated `httpd_resp_set_type + httpd_resp_send` blocks were reduced in touched endpoints.
- **2026-03-15 (Receiver P0 continued):** Started API modular split and removed moved sections from the monolithic handler file.
   - Added: `lib/webserver/api/api_modular_handlers.h/.cpp`
   - Moved out of `lib/webserver/api/api_handlers.cpp`:
      - LED runtime handlers (`api_get_led_runtime_status_handler`, `api_resync_led_state_handler`)
      - test-data handlers (`api_get_test_data_mode_handler`, `api_set_test_data_mode_handler`)
      - event-log subscription handlers (`api_event_logs_subscribe_handler`, `api_event_logs_unsubscribe_handler`)
   - Removed redundant in-file implementations after extraction (monolith reduced with no duplicate handler logic retained).
- **2026-03-15 (Receiver P0 continued):** Extracted network and MQTT API handlers into a dedicated module.
   - Added: `lib/webserver/api/api_network_handlers.h/.cpp`
   - Moved out of `lib/webserver/api/api_handlers.cpp`:
      - receiver network handlers (`api_get_receiver_network_handler`, `api_save_receiver_network_handler`)
      - transmitter network handlers (`api_get_network_config_handler`, `api_save_network_config_handler`)
      - MQTT handlers (`api_get_mqtt_config_handler`, `api_save_mqtt_config_handler`)
   - Removed redundant monolithic implementations and retained a single canonical implementation per endpoint.
- **2026-03-15 (Receiver P0 continued):** Completed remaining domain extraction + SSE utility layer.
   - Added domain modules:
      - `lib/webserver/api/api_telemetry_handlers.h/.cpp`
      - `lib/webserver/api/api_settings_handlers.h/.cpp`
      - `lib/webserver/api/api_debug_handlers.h/.cpp`
      - `lib/webserver/api/api_sse_handlers.h/.cpp`
      - `lib/webserver/api/api_control_handlers.h/.cpp`
      - `lib/webserver/api/api_led_handlers.h/.cpp`
   - Added shared SSE helper:
      - `lib/webserver/utils/http_sse_utils.h/.cpp`
   - Registration now binds handlers from modular files; monolithic handler logic has been replaced by modular implementations.

   ### Completed so far (at-a-glance)
   - ✅ Shared JSON helper introduced and integrated:
      - `lib/webserver/utils/http_json_utils.h/.cpp`
   - ✅ Partial-read safe POST body handling applied in receiver API endpoints touched so far.
   - ✅ Monolith reduction completed for these extracted modules:
      - `lib/webserver/api/api_modular_handlers.h/.cpp`
      - `lib/webserver/api/api_network_handlers.h/.cpp`
- ✅ Remaining receiver API domains extracted into dedicated modules:
   - `api_telemetry_handlers`, `api_settings_handlers`, `api_debug_handlers`, `api_led_handlers`, `api_sse_handlers`, `api_control_handlers`
- ✅ Shared SSE helper added and used by SSE handlers:
   - `lib/webserver/utils/http_sse_utils.h/.cpp`
   - ✅ Redundant/duplicate implementations removed from `lib/webserver/api/api_handlers.cpp` for all moved handlers.
- ✅ Receiver P0 implementation scope complete.
- **2026-03-16 (Transmitter next step):** Completed first transmitter quick wins and reduced duplicate battery payload logic.
   - Updated: `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_handler.cpp`
      - Extracted shared `build_battery_settings_message()` for `battery_settings_full_msg_t` serialization.
      - Replaced magic checksum range (`sizeof(...) - 2`) with `offsetof(..., checksum)`.
      - Added `battery_chemistry_label(...)` bounds-safe lookup for logging.
   - Updated: `ESPnowtransmitter2/espnowtransmitter2/src/main.cpp`
      - Removed duplicate include of `heartbeat_manager.h`.
- **2026-03-16 (Receiver hardening):** Enforced explicit JSON value-type validation in setting updates.
   - Updated: `espnowreceiver_2/lib/webserver/api/api_settings_handlers.cpp`
      - `api_save_setting_handler` now explicitly accepts `bool`, numeric, float/double, and string values.
      - Unsupported JSON types now return typed error payload with `code:"invalid_value_type"`.
- **2026-03-16 (Common SSE hardening):** Fixed duplicated state assignments and completed targeted SSE duplicate-write audit.
   - Updated:
      - `esp32common/webserver/receiver_webserver.cpp`
      - `esp32common/webserver/receiver/webserver.cpp`
   - Removed duplicate `last_soc`/`last_power` writes in SSE update path.
   - Audited receiver SSE handlers for same pattern; no additional active duplicates found.
- **2026-03-16 (Transmitter checksum hardening):** Removed remaining checksum magic-number loop bound in config response path.
   - Updated: `ESPnowtransmitter2/espnowtransmitter2/src/espnow/version_beacon_manager.cpp`
      - Replaced `sizeof(settings_msg) - 2` checksum bound with `offsetof(battery_settings_full_msg_t, checksum)`.
   - Completed checksum-range normalization for active battery settings payload builders/responses.
- **2026-03-16 (Transmitter P0 decomposition started):** Extracted request/abort data handling into dedicated module.
   - Added:
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/request_data_handlers.h`
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/request_data_handlers.cpp`
   - Updated:
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_handler.cpp`
   - `EspnowMessageHandler::handle_request_data` and `handle_abort_data` now delegate to `TxRequestDataHandlers`.
   - Moved battery settings payload/logging helper logic out of monolith into the new request-data module.
- **2026-03-16 (Transmitter P0 decomposition continued):** Extracted control/debug handling into dedicated module.
   - Added:
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/control_handlers.h`
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/control_handlers.cpp`
   - Updated:
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_handler.cpp`
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_handler.h`
   - `handle_reboot`, `handle_ota_start`, `handle_debug_control`, debug ACK/persistence helpers now delegate to `TxControlHandlers`.
   - Transmitter build validated after extraction.
- **2026-03-16 (Transmitter P0 decomposition continued):** Extracted MQTT config request/update/ACK handling into dedicated module.
   - Added:
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/mqtt_config_handlers.h`
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/mqtt_config_handlers.cpp`
   - Updated:
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_handler.cpp`
   - `handle_mqtt_config_request`, `handle_mqtt_config_update`, and `send_mqtt_config_ack` now delegate to `TxMqttConfigHandlers`.
   - Transmitter build validated after extraction.
- **2026-03-16 (Transmitter P0 decomposition continued):** Extracted network config request/update/ACK + background validation processing into dedicated module.
   - Added:
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/network_config_handlers.h`
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/network_config_handlers.cpp`
   - Updated:
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_handler.cpp`
   - `handle_network_config_request`, `handle_network_config_update`, `send_network_config_ack`, and queued update processing now delegate to `TxNetworkConfigHandlers`.
   - Transmitter build validated after extraction.
- **2026-03-16 (Transmitter P0 decomposition continued):** Extracted route-registration logic from `message_handler.cpp` into dedicated compilation unit.
   - Added:
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_routes.cpp`
   - Updated:
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_handler.cpp`
   - `setup_message_routes()` implementation moved out of monolith into `message_routes.cpp` with behavior-preserving route bindings.
   - Transmitter build validated after extraction.
- **2026-03-16 (Transmitter P0 decomposition continued):** Extracted component selection + type-catalog handling from `message_handler.cpp` into dedicated module.
   - Added:
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/component_catalog_handlers.h`
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/component_catalog_handlers.cpp`
   - Updated:
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_handler.cpp`
   - `handle_component_config`, `handle_component_interface`, and all type-catalog request/version handlers now delegate to `TxComponentCatalogHandlers`.
   - Transmitter build validated after extraction.
- **2026-03-16 (Cross-codebase ownership item 2 advanced):** Executed ownership boundary next step by migrating reusable HTTP/SSE mechanism helpers into `esp32common` as shared primitives.
   - Added shared common headers:
      - `esp32common/include/esp32common/webserver/http_json_utils.h`
      - `esp32common/include/esp32common/webserver/http_sse_utils.h`
   - Added shared common implementations:
      - `esp32common/webserver/http_json_utils.cpp`
      - `esp32common/webserver/http_sse_utils.cpp`
   - Updated receiver app-local helper files to compatibility shims (no duplicate implementations retained):
      - `espnowreceiver_2/lib/webserver/utils/http_json_utils.h/.cpp`
      - `espnowreceiver_2/lib/webserver/utils/http_sse_utils.h/.cpp`
   - Ownership decision applied:
      - `esp32common` now owns HTTP JSON body/response helper mechanism and SSE session/keepalive mechanism.
      - receiver app keeps endpoint policy, route wiring, and payload/domain specifics.
      - `esp32common/webserver/receiver_webserver.cpp` is treated as a legacy compatibility surface and should be aligned to common primitives (no new feature logic added there).
- **2026-03-16 (Cross-codebase ownership item 2 phase-2 linkage trial):** Evaluated direct receiver linkage to common helper implementations and recorded integration constraint.
   - Trial outcome:
      - Direct linkage route triggered PlatformIO library resolution conflicts around `webserver` library discovery (receiver app webserver vs `esp32common/webserver`), causing unstable/incorrect source-set selection.
      - Stable receiver build was restored by keeping receiver-local helper `.cpp` symbol providers active.
   - Current state:
      - Common helper declarations/implementations remain authoritative references in `esp32common`.
      - Receiver still uses compatibility headers plus local helper `.cpp` providers until a dedicated common helper library target is introduced.
- **2026-03-16 (Cross-codebase ownership item 2 phase-2 completed):** Introduced a dedicated shared helper library target and switched receiver helper linkage to common object providers.
   - Added dedicated common helper library:
      - `esp32common/webserver_common_utils/library.json`
      - `esp32common/webserver_common_utils/include/webserver_common_utils/http_json_utils.h`
      - `esp32common/webserver_common_utils/include/webserver_common_utils/http_sse_utils.h`
      - `esp32common/webserver_common_utils/src/http_json_utils.cpp`
      - `esp32common/webserver_common_utils/src/http_sse_utils.cpp`
   - Updated receiver compatibility shim headers to consume the dedicated library include paths:
      - `espnowreceiver_2/lib/webserver/utils/http_json_utils.h`
      - `espnowreceiver_2/lib/webserver/utils/http_sse_utils.h`
   - Converted receiver helper `.cpp` files to shim-only translation units (no local symbol providers):
      - `espnowreceiver_2/lib/webserver/utils/http_json_utils.cpp`
      - `espnowreceiver_2/lib/webserver/utils/http_sse_utils.cpp`
   - Validation:
      - `pio run -e receiver_tft` ✅
      - `pio run -e receiver_lvgl` ✅
- **2026-03-16 (Cross-codebase ownership item 2 cleanup):** Removed obsolete experimental helper wrappers under `esp32common/webserver_common` now that `webserver_common_utils` is the active shared helper package.
   - Removed:
      - `esp32common/webserver_common/http_json_utils.h/.cpp`
      - `esp32common/webserver_common/http_sse_utils.h/.cpp`
   - Goal:
      - Reduce duplicate helper surfaces and prevent drift between inactive compatibility wrappers and active shared implementations.
- **2026-03-16 (Transmitter legacy-path cleanup completed):** Finished the full `subtype_settings` retirement sequence and aligned the implementation with the granular protocol model.
   - Updated:
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/request_data_handlers.cpp`
      - `ESPnowtransmitter2/espnowtransmitter2/src/config/network_config.h`
      - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_handler.cpp`
   - Outcome:
      - Audited receiver/runtime usage and confirmed active clients use granular request subtypes.
      - Routed Ethernet IP push behavior to `subtype_network_config`.
      - Removed the legacy `subtype_settings` mixed-response branch (IP + battery payload).
      - Removed the transitional `LEGACY_SUBTYPE_SETTINGS_ENABLED` compatibility flag.
      - Left `subtype_settings` as an unsupported legacy wire value only; granular subtypes are the sole active path.
   - Validation:
      - `pio run` (transmitter default env) ✅
      - `pio run -e receiver_tft` ✅
      - `pio run -e receiver_lvgl` ✅

---

## Post-primary implementation hardening backlog (carry out/investigate after P0/P1)

These items should be actioned once the primary review steps above are complete, so they can be done against the new modularized code rather than repeatedly touching legacy layouts.

1. **Make HTTP POST body reads fully robust (partial read safe)**
   - In receiver API handlers, replace single-call `httpd_req_recv(...)` patterns with looped reads until full `content_len` is consumed.
   - Apply in all JSON POST endpoints for consistent behavior under fragmented socket reads.

2. **Tighten value type validation in setting updates**
   - In `api_save_setting_handler`, explicitly reject unsupported JSON value types instead of falling through with a default/empty payload branch.
   - Return a typed error (`invalid_value_type`) for UI clarity and debugging.

3. **Add bounds checks for enum-indexed logging strings**
   - In transmitter battery settings response/log paths, protect chemistry string indexing with explicit range checks.
   - Fallback to a safe label (for example `"UNKNOWN"`) if an unexpected enum value appears.

4. **Remove checksum magic numbers in payload builders**
   - Replace `sizeof(...) - 2` style checksum ranges with `offsetof(..., checksum)` (or equivalent field-based approach) to make serialization safer against struct changes.
   - Apply consistently to all settings/config payload checksum builders.

5. **Fix and audit duplicate state assignment in SSE code paths**
   - Correct duplicate `last_soc`/`last_power` assignments in `esp32common/webserver/receiver_webserver.cpp`.
   - Perform a quick audit for similar duplicate state writes in other SSE handlers.

---

## Quick win checklist
- [x] Remove duplicate include in transmitter `main.cpp`.
- [x] Extract `battery_settings_full_msg_t` builder function.
- [x] Create `http_json_utils` and replace repetitive response code in receiver API.
- [x] Begin `api_handlers.cpp` modular extraction and remove moved redundant blocks.
- [x] Extract network + MQTT handlers from monolith into dedicated module.
- [x] Create `http_sse_utils` and unify SSE loop behavior.
- [x] Extract telemetry handlers into dedicated module.
- [x] Extract settings handlers into dedicated module.
- [x] Extract debug handlers into dedicated module.
- [x] Extract LED handlers into dedicated module.
- [x] Extract SSE handlers into dedicated module.
- [x] Complete `subtype_settings` legacy-path retirement and move active behavior to granular request subtypes.
- [x] Decide ownership fate of `esp32common/webserver/receiver_webserver.cpp` (retire, refactor, or align).
- [x] (Post-primary) Make POST body reads partial-read safe across receiver API handlers.
- [x] (Post-primary) Enforce explicit JSON value type validation in setting update endpoint(s).
- [x] (Post-primary) Add enum bounds checks for chemistry/logging string lookups.
- [x] (Post-primary) Replace checksum magic-number loops with field-offset based checksum ranges.
- [x] (Post-primary) Fix duplicated SSE state assignments and run duplicate-write audit.
- [x] (Transmitter P0) Extract MQTT config handlers from `message_handler.cpp` into `mqtt_config_handlers` module.
- [x] (Transmitter P0) Extract network config handlers from `message_handler.cpp` into `network_config_handlers` module.
- [x] (Transmitter P0) Continue `message_handler.cpp` decomposition (`control_handlers`, `config_handlers`, `message_routes`).
- [x] (Transmitter P0) Continue residual decomposition of remaining component/type-catalog handlers from `message_handler.cpp`.
- [x] (Cross-codebase item 2 cleanup) Remove obsolete `esp32common/webserver_common` helper wrappers after adopting `webserver_common_utils`.
