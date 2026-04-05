# Receiver File-by-File Commonization and Rewrite Analysis (2026-03-24)

## Scope
Project: `espnowreceiver_2`

Requested objective:
- Perform a deeper receiver audit on a file-by-file basis.
- Identify additional bloat and commonization opportunities.
- Propose efficiency/readability/maintainability improvements.
- Allow full rewrites where justified.

---

## Method used

1. Reviewed major runtime areas:
   - `src/main.cpp`, `src/state_machine.cpp`
   - `src/espnow/*`, `src/mqtt/*`, `src/display/*`
2. Reviewed webserver stack:
   - `lib/webserver/common/*`
   - `lib/webserver/pages/*`
   - `lib/webserver/api/*`
   - `lib/webserver/utils/*`
3. Collected line-count metrics to target bloat hotspots.
4. Checked repeated patterns (registration boilerplate, JSON flow boilerplate, wrapper-file fragmentation, large inline JS blobs).

---

## Quick metrics (current snapshot)

### Largest receiver files (selected)
- `lib/webserver/pages/ota_page_script.cpp` — 757 lines
- `src/espnow/espnow_tasks.cpp` — 680 lines
- `src/display/tft_impl/tft_display.cpp` — 657 lines
- `lib/webserver/api/api_control_handlers.cpp` — 631 lines
- `lib/webserver/common/page_generator.cpp` — 572 lines
- `src/mqtt/mqtt_client.cpp` — 560 lines
- `src/espnow/rx_connection_handler.cpp` — 458 lines
- `lib/webserver/pages/settings_page.cpp` — 450 lines
- `lib/webserver/api/api_telemetry_handlers.cpp` — 441 lines

### Webserver layer totals
- `lib/webserver/pages/*.cpp`: 50 files, 5765 lines
- `lib/webserver/api/*.cpp`: 12 files, 2431 lines
- `lib/webserver/utils/*.cpp`: 32 files, 2242 lines
  - 22 files are ≤60 lines (high fragmentation signal)

---

## Executive summary

### What is now good
- The page decomposition work is substantially improved versus earlier state.
- `api_handlers.cpp` already uses table-based registration for API routes.
- Spec-page layout helper (`spec_page_layout`) and common JS helpers (`CatalogLoader`, `FormChangeTracker`, etc.) are strong directionally.

### Remaining high-impact bloat themes

1. **Over-fragmented transmitter utility layer**
   - 32 `.cpp` files in `lib/webserver/utils`, many tiny pass-through wrappers.
   - Significant mental and compile-surface overhead with minimal functional separation.

2. **Page generation still mixed between modern modular and legacy inline-heavy pages**
   - `settings_page.cpp` still embeds very large HTML + JS strings in one file.
   - This is now an outlier against the newer content/script split pattern.

3. **Large orchestration monoliths remain**
   - `espnow_tasks.cpp`, `rx_connection_handler.cpp`, `api_control_handlers.cpp`, `mqtt_client.cpp` each mix multiple responsibilities.

4. **Repeated JSON response plumbing and endpoint patterns**
   - API handlers still manually repeat parse/validate/build/send scaffolding.

---

## File-by-file findings (grouped)

## A) Runtime orchestration (`src/`)

| File | Current role | Finding | Recommendation |
|---|---|---|---|
| `src/main.cpp` | Boot and subsystem init orchestration | Large but mostly justified; contains test/debug halt logic and init sequencing | Keep structure but move one-off debug probe (`run_pre_littlefs_debug_and_halt`) behind compile-time feature gate file (`debug_boot_probe.cpp`) |
| `src/state_machine.cpp` | High-level receiver state transitions | Generally clean; state machine logic readable | No full rewrite needed; keep and only trim direct TFT side-effects into display service helpers |
| `src/espnow/espnow_tasks.cpp` | Message route registration + worker loop + dispatch side effects | Still too many concerns in one file | Split into: `espnow_route_registry.cpp`, `espnow_worker_loop.cpp`, `espnow_config_sync.cpp`, `espnow_metadata_handlers.cpp` |
| `src/espnow/rx_connection_handler.cpp` | Connection state transitions, retries, initialization requests, deferred events | Mixed state handling + messaging + retry policy in one class | Extract retry policy and initialization request planner into dedicated strategy module |
| `src/mqtt/mqtt_client.cpp` | MQTT connection, subscription mgmt, topic dispatch, parse/store | Good functionality but overgrown; repeated parse/store template logic | Introduce topic table descriptor with parse-and-store callbacks; unify handler pattern |
| `src/mqtt/mqtt_task.cpp` | MQTT task loop | Compact and focused | No action |
| `src/display/tft_impl/tft_display.cpp` | TFT rendering engine | Large but expected for rendering layer | No broad rewrite unless introducing render command batching |
| `src/display/display_core.cpp` | Display compositing logic | Acceptable size and cohesion | No action |
| `src/display/display_update_queue.cpp` | Display queue producer/consumer | Good decoupling point already | No action |
| `src/common.h` | Global declarations | Over-includes many heavy headers; increases transitive compile cost | Replace with minimal forward-declare header + split domain-specific includes |

### Suggested full rewrite candidate A1
`espnow_tasks.cpp` should be rewritten into a **route table + hook modules** architecture.

Proposed signatures:
- `void register_core_routes(EspnowMessageRouter& router, const RouteDeps& deps);`
- `void register_config_routes(EspnowMessageRouter& router, const RouteDeps& deps);`
- `void run_worker_loop(QueueHandle_t queue, WorkerDeps& deps);`

Expected gains:
- Reduced merge conflicts in active development area.
- Clear ownership boundaries for routing vs runtime loop vs side effects.

---

## B) Webserver common layer (`lib/webserver/common`)

| File | Current role | Finding | Recommendation |
|---|---|---|---|
| `common/page_generator.cpp` | HTML shell + common JS helper bundle | Valuable, but becoming a mega-script sink (572 lines) | Split helper JS generation into separate units by concern (`save_operation`, `catalog_loader`, `component_apply`, `network_form`) |
| `common/page_generator.h` | Render API | Good abstraction (`send_rendered_page`) | Add small descriptor-driven page registration helper |
| `common/spec_page_layout.cpp` | Shared spec-page HTML/CSS | Good shared base | Add generic `render_spec_page(...)` helper to remove remaining per-page PSRAM string assembly duplication |
| `common/nav_buttons.cpp` | Page button generation | Works, but now largely superseded by single-dashboard top bar pattern | Keep helper for pages that need multi-nav; add explicit `render_dashboard_topbar()` helper |

### Suggested rewrite candidate B1
Create `web_ui_common.h/.cpp` with reusable topbar/template snippets:
- `String render_dashboard_topbar();`
- `String render_page_title(const char* title);`
- `String render_card_start(const char* title);`

This removes repeated raw literals across multiple page content files.

---

## C) Web pages (`lib/webserver/pages`)

### High-priority files

| File | Finding | Recommendation |
|---|---|---|
| `pages/settings_page.cpp` | Outlier: still large embedded HTML+JS monolith (450 lines) | **Immediate split** into `settings_page_content.cpp/.h` and `settings_page_script.cpp/.h` (this existed previously and should be restored in modern pattern) |
| `pages/ota_page_script.cpp` | Very large workflow script (757 lines), many nested async branches | Extract reusable OTA client state machine helper from page script; leave page file as configuration shell |
| `pages/dashboard_page_script.cpp` | Growing client-side logic | Move recurrent fetch/update patterns into shared polling helper with declarative endpoints |
| `pages/battery_settings_page_script.cpp` | Strong use of shared helpers but still long | Further reduce via `SettingsPageController` abstraction for field schema-driven save/load |
| `pages/network_config_page_script.cpp` | Repeats IP-octet and save-state patterns | Adopt shared IP octet field adapter + generic save controller |

### Medium-priority files

| File | Finding | Recommendation |
|---|---|---|
| `pages/systeminfo_page_script.cpp` | Similar form behavior to network/settings pages | Convert to shared form schema config |
| `pages/hardware_config_page_script.cpp` | Similar component apply flow | Use common component-apply orchestration only; keep page-specific validation |
| `pages/inverter_specs_display_page.cpp` | Manual PSRAM + snprintf assembly remains | Replace with common spec rendering pipeline function |
| `pages/system_specs_display_page.cpp` | Similar spec assembly pattern | Same as above |
| `pages/charger_specs_display_page.cpp` | Similar spec assembly pattern | Same as above |
| `pages/battery_specs_display_page.cpp` | Similar spec assembly pattern | Same as above |

### No-action / low-action pages (current form acceptable)
- `debug_page.cpp` (+content/script)
- `event_logs_page.cpp` (+content/script)
- `monitor_page.cpp` (+content/script)
- `monitor2_page.cpp` (+content/script)
- `reboot_page.cpp`

(These are now reasonably modular and short enough after recent cleanup.)

---

## D) API layer (`lib/webserver/api`)

| File | Finding | Recommendation |
|---|---|---|
| `api_control_handlers.cpp` | Very large; mixes reboot, OTA status proxy, receiver self-OTA, transmitter stream-forward OTA | **Split by concern**: `api_ota_receiver_handlers.cpp`, `api_ota_transmitter_handlers.cpp`, `api_reboot_handlers.cpp` |
| `api_telemetry_handlers.cpp` | Many similar “build JSON from cache/state” handlers | Introduce reusable JSON field builder helpers and endpoint descriptor registry |
| `api_network_handlers.cpp` | Duplicates parsing/validation/send patterns | Extract `NetworkConfigDto` parse/validate/serialize functions |
| `api_type_selection_handlers.cpp` | Improved but still custom serialization loops repeated | Keep and further unify via typed catalog serializer helper (`serialize_catalog<T>()`) |
| `api_response_utils.cpp` | Useful central utility | Extend with `send_json_error_with_detail`, `send_json_doc_no_cache`, typed IPv4 wrappers |
| `api_handlers.cpp` | Good table-driven route registration | No major rewrite; optional split core vs OTA route arrays for readability |

### Suggested rewrite candidate D1
Create `api/api_json_endpoint.h/.cpp` mini-framework:
- `using JsonHandlerFn = bool(*)(JsonDocument& out, String& err);`
- `esp_err_t serve_json_endpoint(httpd_req_t* req, JsonHandlerFn fn);`

This removes repetitive “prepare doc + serialize + send + error envelope” code.

### Suggested rewrite candidate D2
OTA control rewrite as a dedicated service object (rather than static file-scope helpers):
- `class OtaProxyService { acquireChallenge(); openConnection(); forwardStream(); readResponse(); }`

Benefits:
- Testable state machine.
- Cleaner error mapping.
- Easier future support for retries/chunk checksums/resume.

---

## E) Transmitter cache/util layer (`lib/webserver/utils`)

This is the biggest structural bloat source in the receiver web stack.

### Evidence
- 32 `.cpp` files, 2242 total lines.
- 22 files are ≤60 lines.
- Many files only forward calls to another cache module.

### Representative tiny pass-through wrappers
- `transmitter_connection_state_resolver.cpp` (10)
- `transmitter_time_status_update_workflow.cpp` (10)
- `transmitter_write_through.cpp` (12)
- `transmitter_mac_query_helper.cpp` (14)
- `transmitter_metadata_store_workflow.cpp` (15)
- `transmitter_runtime_status_update.cpp` (17)
- `transmitter_battery_spec_sync.cpp` (17)
- `transmitter_active_mac_resolver.cpp` (19)
- `transmitter_mac_registration.cpp` (19)
- `transmitter_event_logs_workflow.cpp` (23)
- `transmitter_metadata_query_helper.cpp` (25)
- `transmitter_peer_registry.cpp` (26)

### Recommendation (full consolidation rewrite)
Replace many wrapper/workflow/query files with a smaller, clearer module set:

Target module set:
1. `transmitter_identity_store.*`
2. `transmitter_network_store.*`
3. `transmitter_mqtt_store.*`
4. `transmitter_settings_store.*`
5. `transmitter_specs_store.*`
6. `transmitter_event_log_store.*`
7. `transmitter_runtime_store.*`
8. `transmitter_persistence.*`

Then make `TransmitterManager` a thin facade over these 8 modules only.

### Suggested rewrite candidate E1
Replace current many-function static facade with typed snapshots:
- `TransmitterSnapshot get_snapshot();`
- `NetworkSnapshot get_network_snapshot();`
- `MqttSnapshot get_mqtt_snapshot();`
- `SettingsSnapshot get_settings_snapshot();`

And mutator APIs:
- `void apply_network_update(const NetworkUpdate& update);`
- `void apply_mqtt_update(const MqttUpdate& update);`

This shrinks API surface and avoids dozens of one-line forwarders.

---

## Duplicate-code patterns and commonization opportunities

## Pattern 1: Repeated page registration boilerplate
Current in nearly every `register_*_page(...)`:
- local `httpd_uri_t`
- assign 4 fields
- call `httpd_register_uri_handler`

### Improvement
Add in `common/page_registry_utils.h`:
- `esp_err_t register_get_uri(httpd_handle_t server, const char* uri, esp_err_t (*handler)(httpd_req_t*));`

---

## Pattern 2: Repeated spec-page assembly (parse -> format -> send)
Observed in battery/inverter/charger/system spec pages.

### Improvement
Add `common/spec_page_renderer.h/.cpp`:
- `esp_err_t render_spec_page(httpd_req_t* req, const SpecPageInput& input);`

Where `SpecPageInput` includes:
- source JSON string
- parser callback
- section format string or render callback
- nav links + inline script suppliers

---

## Pattern 3: Repeated API JSON response scaffolding
Many handlers repeat:
- `DynamicJsonDocument doc(...)`
- fill fields
- `serializeJson(...)`
- send

### Improvement
Add endpoint helper wrappers:
- `esp_err_t send_json_builder(httpd_req_t* req, std::function<void(JsonDocument&)> fill);`
- `esp_err_t send_json_builder_no_cache(...)`

---

## Pattern 4: IP octet parsing/formatting across scripts and APIs
Repeated in:
- `settings_page.cpp` embedded script
- `network_config_page_script.cpp`
- `systeminfo_page_script.cpp`
- multiple API handlers

### Improvement
- Browser helper: `window.IpFieldAdapter` (`setOctets`, `collectOctets`, `validateOctets`)
- C++ helper already partial in `ApiResponseUtils`; expand with strict parse/normalize utilities

---

## Full rewrite candidates (recommended)

## R1) Consolidate transmitter utils stack (highest ROI)
- **Type:** architecture rewrite (module consolidation)
- **Why:** strongest bloat signal, many pass-through files
- **Risk:** medium (cross-file API changes)
- **Expected gain:** reduced file count and call depth; easier onboarding/debugging

## R2) Rewrite OTA page script as explicit finite-state UI controller
- **Type:** full JS workflow rewrite in page script
- **Why:** 757-line nested async flow, hard to reason about
- **Risk:** medium (OTA UX sensitive)
- **Expected gain:** clearer behavior, lower regression risk, easier testing

## R3) Split `api_control_handlers.cpp` into dedicated OTA services
- **Type:** backend flow rewrite by concern
- **Why:** currently combines too many responsibilities
- **Risk:** medium
- **Expected gain:** safer OTA modifications and clearer fault handling

## R4) Re-split `settings_page.cpp` into content/script modules
- **Type:** structural rewrite (restore decomposition pattern)
- **Why:** inconsistent with rest of codebase and hard to maintain
- **Risk:** low
- **Expected gain:** immediate readability and merge-conflict reduction

## R5) Route registry rewrite in `espnow_tasks.cpp`
- **Type:** C++ architecture cleanup
- **Why:** route registration and worker loop are over-coupled
- **Risk:** medium
- **Expected gain:** cleaner routing evolution and testing

---

## Proposed helper APIs (signature-level suggestions)

## 1) Page registration helper
- `esp_err_t register_get_uri(httpd_handle_t server, const char* uri, esp_err_t (*handler)(httpd_req_t*));`

## 2) Generic spec page renderer
- `struct SpecRenderContext { String title; String source_topic; String nav_html; String inline_script; };`
- `esp_err_t render_spec_page(httpd_req_t* req, const SpecRenderContext& ctx, const String& section_html);`

## 3) JSON endpoint helper
- `using FillJsonFn = bool(*)(JsonDocument& out_doc, String& error_msg);`
- `esp_err_t serve_json(httpd_req_t* req, FillJsonFn fill, bool no_cache = false);`

## 4) OTA proxy service
- `class OtaProxyService { esp_err_t forward_to_transmitter(httpd_req_t* req); esp_err_t upload_receiver(httpd_req_t* req); };`

## 5) Unified transmitter snapshots
- `bool get_transmitter_snapshot(TransmitterSnapshot& out);`
- `bool get_network_snapshot(NetworkSnapshot& out);`
- `bool get_mqtt_snapshot(MqttSnapshot& out);`

---

## Prioritized implementation roadmap

## P0 (low risk, immediate)
1. Re-split `settings_page.cpp` into content/script modules.
2. Add small page registration helper (`register_get_uri`) and migrate all page `register_*` functions.
3. Expand `ApiResponseUtils` with a JSON-builder send helper to remove repeated scaffolding.

**Acceptance criteria:**
- `receiver_tft` build passes.
- No functional behavior changes.
- Reduced lines in `settings_page.cpp` by >60%.

## P1 (medium risk, high payoff)
4. Split `api_control_handlers.cpp` by OTA concern.
5. Refactor `ota_page_script.cpp` to explicit state-machine style controller object.
6. Introduce generic spec page render helper and migrate battery/inverter/charger/system pages.

**Acceptance criteria:**
- All OTA flows unchanged functionally (receiver self-OTA, transmitter OTA, status poll, reboot).
- Spec pages produce identical visible fields.

## P2 (medium risk, architectural)
7. Consolidate transmitter utility wrapper stack from 32 files into ~8 cohesive modules.
8. Refactor `espnow_tasks.cpp` into route registry + worker loop split.

**Acceptance criteria:**
- No regression in metadata/network/MQTT/settings cache visibility.
- Message routing tests and runtime logs equivalent for probe/data/config ack/version beacon paths.

---

## Suggested “no action” list (for focus control)

These are large but currently justified and should not be rewritten first:
- `src/display/tft_impl/tft_display.cpp`
- `src/display/display_core.cpp`
- `src/display/display_update_queue.cpp`
- `src/mqtt/mqtt_task.cpp`
- `lib/webserver/api/api_handlers.cpp` (already table-driven)

---

## Final recommendation

The strongest next step is **not another broad split pass**; it is a **targeted consolidation pass**:

1. Fix the remaining monolith outliers (`settings_page.cpp`, `ota_page_script.cpp`, `api_control_handlers.cpp`).
2. Collapse the utility-wrapper explosion in `lib/webserver/utils`.
3. Add 2–3 strategic shared helpers (page registration, JSON endpoint builder, spec renderer).

That combination will improve efficiency/readability/maintainability more than incremental micro-edits, while keeping change risk manageable.
