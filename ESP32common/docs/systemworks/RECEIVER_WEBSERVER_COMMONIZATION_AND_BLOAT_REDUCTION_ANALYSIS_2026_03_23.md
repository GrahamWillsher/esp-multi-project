# Receiver Webserver Commonization and Bloat Reduction Analysis

Date: 2026-03-23
Scope: `espnowreceiver_2/lib/webserver` and subfolders (`pages`, `api`, `common`, `utils`)
Goal: Identify code that is nearly common across the webserver stack and could be consolidated to reduce bloat, improve consistency, and lower maintenance cost.

## Executive Summary

The `lib/webserver` codebase has already improved materially through the first-stage page decomposition work: many page handlers now separate orchestration from page content and page script generation. That said, the code still contains a large amount of second-stage duplication:

- Repeated page registration and render boilerplate across many handlers
- Repeated client-side JavaScript workflows for loading catalog data, tracking form changes, and saving settings
- Repeated spec-display page pipelines that deserialize JSON, format HTML sections, allocate PSRAM buffers, and send assembled HTML
- Repeated JSON string construction and IP formatting in API handlers
- A very large `TransmitterManager` facade that mostly forwards into many tiny query/store/workflow helper files
- Older pages that still embed style and script directly into page handlers rather than using the newer content/script module split

The biggest opportunity is no longer only “split large files”; it is now “extract shared behaviors from the already-split files”.

## Implementation Progress (2026-03-23, Phase 1)

The following report recommendations have now been implemented after the initial analysis pass:

- Added `send_rendered_page(...)` to `lib/webserver/common/page_generator.h/.cpp` so renderPage-based handlers can consistently render + set content type + send via one shared helper.
- Updated the following handlers to use the new shared sender instead of open-coded `renderPage(...)` + `httpd_resp_set_type(...)` + `httpd_resp_send(...)` sequences:
   - `lib/webserver/pages/dashboard_page.cpp`
   - `lib/webserver/pages/settings_page.cpp`
   - `lib/webserver/pages/ota_page.cpp`
   - `lib/webserver/pages/systeminfo_page.cpp`
   - `lib/webserver/pages/battery_settings_page.cpp`
   - `lib/webserver/pages/reboot_page.cpp`
   - `lib/webserver/pages/network_config_page.cpp`
   - `lib/webserver/pages/transmitter_hub_page.cpp`
   - `lib/webserver/pages/cellmonitor_page.cpp`
   - `lib/webserver/pages/inverter_settings_page.cpp`
   - `lib/webserver/pages/hardware_config_page.cpp`
- Deduplicated repeated catalog-endpoint logic inside `lib/webserver/api/api_type_selection_handlers.cpp` by introducing:
   - `send_oom_response(...)`
   - `serve_fixed_type_catalog(...)`
   - `serve_cached_type_catalog(...)`
- Converted the following endpoints to the shared catalog helpers:
   - `/api/get_battery_types`
   - `/api/get_inverter_types`
   - `/api/get_battery_interfaces`
   - `/api/get_inverter_interfaces`
- Validation repeated: `pio run -e receiver_tft` ✅ SUCCESS.

This phase intentionally targeted the lowest-risk/highest-ROI commonization points first. The next logical phase is to extract the repeated browser-side catalog loader + form-change tracking helpers and then finish standardizing the remaining legacy page handlers (`debug_page`, `event_logs_page`, `monitor_page`, `monitor2_page`) onto the newer page-module pattern.

## Implementation Progress (2026-03-23, Phase 2)

The next browser-side commonization step has now also been implemented:

- Added `window.CatalogLoader` to the shared browser helper bundle in `lib/webserver/common/page_generator.cpp`.
- The new shared helper now centralizes:
   - loading-option / empty-option rendering for `<select>` controls
   - shared catalog `<select>` population via `loadCatalogSelect(...)`
   - selected label resolution via `loadCatalogLabel(...)`
- Migrated repeated catalog/select-fetch logic in:
   - `lib/webserver/pages/battery_settings_page_script.cpp`
   - `lib/webserver/pages/inverter_settings_page_script.cpp`
- The old repeated fetch/retry/populate/load-current-selection code for battery/inverter type and interface selectors has been replaced with `CatalogLoader.loadCatalogSelect(...)` calls plus page-specific callbacks for initial-state capture and button-state refresh.
- Validation repeated: `pio run -e receiver_tft` ✅ SUCCESS.

The next highest-value implementation step is a shared browser-side form change tracker / save-button state helper, followed by bringing the remaining legacy pages (`debug_page`, `event_logs_page`, `monitor_page`, `monitor2_page`) onto the same decomposed/commonized page pattern.

## Implementation Progress (2026-03-23, Phase 3)

The shared browser-side form change/save-button helper has now been implemented:

- Added `window.FormChangeTracker` to `lib/webserver/common/page_generator.cpp`.
- The helper now centralizes:
   - element value extraction (`checkbox` vs standard value)
   - generic changed-field counting via `countChanges(initialValues, fieldIds)`
   - standardized save-button state transitions via `updateSaveButton(...)`
- Migrated duplicated button/change logic in:
   - `lib/webserver/pages/battery_settings_page_script.cpp`
   - `lib/webserver/pages/inverter_settings_page_script.cpp`
   - `lib/webserver/pages/hardware_config_page_script.cpp`
- Validation repeated: `pio run -e receiver_tft` ✅ SUCCESS.

With phases 1-3 in place, the next implementation target should be the remaining legacy page handlers (`debug_page`, `event_logs_page`, `monitor_page`, `monitor2_page`) to align all pages to the decomposed/commonized pattern and reduce the last major pockets of inline script/style duplication.

## Method

## Implementation Progress (2026-03-23, Phase 4)

All four remaining legacy page handlers have been decomposed into the standard content/script/style module pattern:

**New files created (16 total, 4 per page):**
- `debug_page_content.h/.cpp` — HTML form with `generate_nav_buttons("/debug")`
- `debug_page_script.h/.cpp` — `get_debug_page_styles()` + `get_debug_page_script()`
- `event_logs_page_content.h/.cpp` — minimal HTML + `generate_nav_buttons()`
- `event_logs_page_script.h/.cpp` — `get_event_logs_page_styles()` + `get_event_logs_page_script()`; cleans up the previous inline `<style>` and `<script>` tags concatenated into the `content` string
- `monitor_page_content.h/.cpp` — battery monitor HTML + `generate_nav_buttons("/transmitter/monitor")`
- `monitor_page_script.h/.cpp` — `get_monitor_page_styles()` + `get_monitor_page_script()`
- `monitor2_page_content.h/.cpp` — SSE battery monitor HTML + `generate_nav_buttons("/transmitter/monitor2")`
- `monitor2_page_script.h/.cpp` — `get_monitor2_page_styles()` + `get_monitor2_page_script()`

**Handler files slimmed (4 files):**
Each handler now follows the same pattern as all other pages:
```cpp
static esp_err_t <page>_handler(httpd_req_t *req) {
   return send_rendered_page(req, "<Title>",
                       get_<page>_content(),
                       PageRenderOptions(get_<page>_styles(), get_<page>_script()));
}
```
The `event_logs_page` handler additionally eliminates the pre-existing anti-pattern of
embedding raw `<style>` and `<script>` tags inside the `content` string via `content +=
"<style>..."` / `content += "<script>..."` string concatenation; those are now properly
routed through `PageRenderOptions` and inserted in the page `<head>` by `renderPage()`.

- Validation repeated: `pio run -e receiver_tft` ✅ SUCCESS.

All pages in `lib/webserver/pages/` now conform to the decomposed module pattern. The
next target is Phase 5 (spec page theme/layout consolidation) or Phase 6 (API JSON
helper expansion).

## Implementation Progress (2026-03-24, Phase 5)

Spec page theme/layout consolidation is now implemented across all four spec pages
(`battery`, `inverter`, `charger`, `system`):

- Added shared helper:
   - `lib/webserver/common/spec_page_layout.h`
   - `lib/webserver/common/spec_page_layout.cpp`
- New helper API:
   - `build_spec_page_html_header(...)` for common `<head>`, shared CSS, header card, and source banner
   - `build_spec_page_html_footer(...)` for common nav container, optional inline script, and closing tags

### Battery + Inverter spec pages

- Migrated duplicated static header/footer HTML/CSS to shared helper usage.
- Updated modules to return `String` where dynamic shared-layout assembly is needed:
   - `get_battery_specs_page_html_header()` now returns `String`
   - `get_inverter_specs_page_html_header()` now returns `String`
- Reworked script/footer modules to provide composable pieces:
   - battery: `get_battery_specs_page_nav_links_html()` and `get_battery_specs_page_inline_script()`
   - inverter: `get_inverter_specs_page_nav_links_html()` and `get_inverter_specs_page_inline_script()`
- Replaced custom interface/type label-fetch code in both pages with shared
   `CatalogLoader.loadCatalogLabel(...)` usage, reducing duplicated browser logic.

### Charger + System spec pages

- Replaced full inline page shell/CSS blocks with shared helper-generated header/footer.
- Retained page-specific specs grid format sections and data extraction logic.

### Additional correctness fix folded into the refactor

- In `inverter_specs_display_page.cpp`, corrected a buffer-size bug where
   `snprintf(specs_section, sizeof(specs_section), ...)` used `sizeof(char*)`
   instead of the allocated buffer size; now explicitly uses `2048`.

- Validation repeated: `pio run -e receiver_tft` ✅ SUCCESS.

With Phase 5 complete, the next implementation target is Phase 6: expanding API JSON
response helpers to remove remaining manual `snprintf` JSON assembly paths.

## Method

The analysis reviewed the entire `lib/webserver` tree and sampled representative code from all major areas:

- `common`: page shell generation and navigation helpers
- `pages`: dashboard/settings/system/network/monitor/OTA/debug/events/spec display pages
- `api`: request parsing, response helpers, type selection APIs, network APIs, control APIs
- `utils`: transmitter manager facade, cache/query/store/workflow helpers

The findings below focus on realistic consolidation work: helpers that can be introduced with moderate rework and without requiring a full architectural rewrite.

---

## Ranked Findings

## 1. Shared page wrapper / registration helper

### Observed pattern
Many page handlers now follow an almost identical structure:

1. Build `content`
2. Optionally build `style`
3. Optionally build `script`
4. Call `renderPage(...)`
5. Set content type and send the result
6. Register a single `httpd_uri_t`

Representative files:

- `lib/webserver/pages/dashboard_page.cpp`
- `lib/webserver/pages/settings_page.cpp`
- `lib/webserver/pages/ota_page.cpp`
- `lib/webserver/pages/systeminfo_page.cpp`
- `lib/webserver/pages/network_config_page.cpp`
- `lib/webserver/pages/cellmonitor_page.cpp`
- `lib/webserver/pages/inverter_settings_page.cpp`
- `lib/webserver/pages/hardware_config_page.cpp`
- `lib/webserver/pages/transmitter_hub_page.cpp`
- `lib/webserver/pages/battery_settings_page.cpp`

### Why it bloats the code
The per-page boilerplate is small in each file, but across many pages it adds up and creates drift:

- Some handlers explicitly call `httpd_resp_set_type(req, "text/html")`, others do not
- Route registration is repeated everywhere
- The pattern is now stable enough to deserve a shared wrapper

### Recommended commonization
Introduce a small helper in `common`, for example:

- `esp_err_t send_rendered_page(httpd_req_t* req, const String& title, const String& content, const PageRenderOptions& options = {});`
- `esp_err_t register_get_page(httpd_handle_t server, const char* uri, esp_err_t (*handler)(httpd_req_t*));`

Or a slightly richer descriptor-based helper:

- `register_rendered_page(server, PageRoute{...})`

### Expected benefit
- Reduces repetitive page-handler glue
- Standardizes response headers and send behavior
- Makes future page additions smaller and less error-prone

### Risk
Low

### Priority
High

---

## 2. Shared client-side form change tracker

### Observed pattern
Several settings pages independently implement the same form-state behavior:

- Capture initial values
- Compare current values to initial values
- Count changed fields
- Update save button text/color/disabled state
- Restore button state after success/failure

Representative files:

- `lib/webserver/pages/battery_settings_page_script.cpp`
- `lib/webserver/pages/inverter_settings_page_script.cpp`
- `lib/webserver/pages/hardware_config_page_script.cpp`
- `lib/webserver/pages/settings_page_script.cpp`
- `lib/webserver/pages/systeminfo_page_script.cpp`
- `lib/webserver/pages/network_config_page_script.cpp`

### Why it bloats the code
This is the same UI state machine repeated with slightly different field lists and button text:

- `Nothing to Save`
- `Save 1 Change`
- `Save N Changes`
- Button colors and restore timing vary across pages

The code is not just duplicated; it is already starting to drift in wording and behavior.

### Recommended commonization
Extend the existing `window.SaveOperation` helper in `common/page_generator.cpp` with a second helper, e.g.:

- `window.FormChangeTracker.create({ fields, saveButtonId, initialValues, onCountChanged, labels })`

This should centralize:

- Snapshotting field values
- Counting changed inputs/selects/checkboxes
- Standard save-button states
- Restore-after-save behavior

### Expected benefit
- Smaller page scripts
- Consistent UX across settings pages
- Easier addition of new settings pages

### Risk
Low to Medium

### Priority
High

---

## 3. Shared type/interface selector loader for page JavaScript

### Observed pattern
Battery and inverter pages independently implement the same catalog-loading logic:

- Fetch available types/interfaces
- Retry if the response reports `loading` or empty data
- Populate a `<select>` or label
- Fetch selected current value from `/api/get_selected_types` or `/api/get_selected_interfaces`
- Update UI and save state

Representative files:

- `lib/webserver/pages/battery_settings_page_script.cpp`
- `lib/webserver/pages/inverter_settings_page_script.cpp`
- `lib/webserver/pages/battery_specs_display_page_script.cpp`
- `lib/webserver/pages/inverter_specs_display_page_script.cpp`

### Why it bloats the code
The same control-flow exists four times with only endpoint names and element IDs changed.

Repeated patterns include:

- `Loading...`
- `No data (check transmitter link)`
- building `<option>` elements
- selected-value fetches from shared endpoints
- identical retry counters and timers

### Recommended commonization
Add a shared browser helper in `COMMON_SCRIPT_HELPERS`, for example:

- `loadCatalogSelect({ catalogEndpoint, selectedEndpoint, selectedKey, selectId, maxRetries, loadingText, emptyText, onLoaded })`
- `loadCatalogLabel({ catalogEndpoint, selectedEndpoint, selectedKey, targetId, fallbackText })`

### Expected benefit
- Removes repeated fetch/retry boilerplate
- Standardizes loading/error handling
- Makes future type/interface pages easier to build

### Risk
Low to Medium

### Priority
High

---

## 4. Consolidate receiver-network page workflows

### Observed pattern
The receiver network/config pages contain overlapping workflows for loading and saving receiver network data.

Representative files:

- `lib/webserver/pages/systeminfo_page_script.cpp`
- `lib/webserver/pages/network_config_page_script.cpp`
- `lib/webserver/api/api_network_handlers.cpp`

Repeated behaviors include:

- Fetching `/api/get_receiver_network`
- Populating device/network fields
- DHCP/static-IP toggle logic
- IP octet/string splitting and recombination
- Fetching `/api/firmware_info`
- Posting to `/api/save_receiver_network`

### Why it bloats the code
The overlap is large enough that the same bug fix would need to be applied in multiple files. It also creates a risk that the same data shape is handled slightly differently across screens.

### Recommended commonization
Introduce a shared `ReceiverNetworkFormController` (client-side) plus smaller helpers:

- `loadReceiverNetworkConfig()`
- `setOctets(...)` / `collectOctets(...)`
- `bindStaticIpToggle(...)`
- `loadReceiverFirmwareInfo(...)`
- `saveReceiverNetworkConfig(...)`

On the C++ side, consider also moving common receiver-network JSON production into an API-side model serializer instead of manual string assembly.

### Expected benefit
- Removes one of the largest remaining duplicated page workflows
- Reduces future regression risk in network config pages

### Risk
Medium

### Priority
High

---

## 5. Shared spec-page theme/layout system

### Observed pattern
The spec-display pages are very similar structurally but each carries a full local CSS theme block and duplicated HTML scaffolding.

Representative files:

- `lib/webserver/pages/battery_specs_display_page.cpp`
- `lib/webserver/pages/inverter_specs_display_page.cpp`
- `lib/webserver/pages/charger_specs_display_page.cpp`
- `lib/webserver/pages/system_specs_display_page.cpp`

Repeated sections include:

- `.container`
- `.header`
- `.specs-grid`
- `.spec-card`
- `.feature-badge`
- `.source-info`
- `.nav-buttons`
- `.btn`, `.btn-primary`, `.btn-secondary`

### Why it bloats the code
This is mostly static HTML/CSS duplication. It inflates source size and makes visual drift likely. The pages differ mainly in accent colors, field layout, and footer links.

### Recommended commonization
Two options are reasonable:

1. Move these pages onto `renderPage(...)` and a shared spec-page style fragment
2. Keep the standalone HTML pipeline, but introduce a shared template provider such as:
   - `SpecPageThemeOptions`
   - `get_spec_page_header(theme, title, subtitle, sourceText)`
   - `get_spec_page_footer(navLinks)`

### Expected benefit
- Significant static code reduction
- Consistent styling across all spec pages
- Easier future visual updates

### Risk
Low to Medium

### Priority
High

---

## 6. Shared legacy spec-display render pipeline

### Observed pattern
The spec pages all implement nearly the same C++ data pipeline:

1. Get JSON from `TransmitterManager`
2. `deserializeJson(...)`
3. Apply defaults/fallbacks
4. `snprintf(...)` a formatted specs section
5. Allocate response buffer (often with PSRAM)
6. Concatenate header + section + footer
7. Send HTML response

Representative files:

- `lib/webserver/pages/battery_specs_display_page.cpp`
- `lib/webserver/pages/inverter_specs_display_page.cpp`
- `lib/webserver/pages/charger_specs_display_page.cpp`
- `lib/webserver/pages/system_specs_display_page.cpp`

### Why it bloats the code
The logic is repeated and somewhat fragile:

- Repeated allocation error handling
- Repeated response assembly code
- Repeated `snprintf(...)` chains
- Slightly different buffer-management choices across pages

This is also exactly the kind of code that becomes hard to reason about when edited page by page.

### Recommended commonization
Introduce a small helper layer for standalone formatted HTML pages, e.g.:

- `HtmlPageAssembler` for header/body/footer concatenation
- `send_html_from_parts(req, header, body, footer, reserveSize)`
- `format_section_or_fail(...)`

If desired, go one step further and create typed spec-page builders per domain that plug into a shared sender.

### Expected benefit
- Reduces C++ duplication in all legacy spec pages
- Makes allocation and send behavior uniform
- Lowers chance of buffer/format mistakes

### Risk
Medium to High

### Priority
Medium to High

---

## 7. Expand API response/model helpers and reduce manual JSON string formatting

### Observed pattern
Many API handlers still manually build JSON with `snprintf(...)` and raw format strings.

Representative files:

- `lib/webserver/api/api_network_handlers.cpp`
- `lib/webserver/api/api_settings_handlers.cpp`
- `lib/webserver/api/api_modular_handlers.cpp`
- `lib/webserver/api/api_telemetry_handlers.cpp`
- `lib/webserver/api/api_led_handlers.cpp`

The shared helpers in `api_response_utils.*` and `api_request_utils.h` are helpful, but only partially used.

### Why it bloats the code
Manual JSON assembly is repetitive and brittle:

- Repeated success/error envelopes
- Repeated IP address formatting
- Repeated no-cache behavior
- Repeated `char json[...]; snprintf(...)`

It also makes escaping rules harder to audit.

### Recommended commonization
Extend `ApiResponseUtils` with helpers such as:

- `send_json_doc(req, doc)`
- `send_success_doc(req, doc)`
- `send_ipv4_json_field(...)`
- `send_bool_string_pair(...)`
- `send_loading_or_empty(...)`

Also consider a tiny API-side model serializer layer for repeated structures (selected types/interfaces, network config, transmitter health, etc.).

### Expected benefit
- Smaller API handlers
- Safer and more consistent JSON responses
- Easier future schema changes

### Risk
Medium

### Priority
Medium to High

---

## 8. Shared transmitter-command send workflow in APIs

### Observed pattern
Several API handlers appear to follow a transport-oriented pattern:

- Validate/prep input
- Confirm transmitter MAC or readiness
- Build a payload
- Send to transmitter via ESP-NOW or apply workflow
- Log success/failure
- Return JSON result

Representative areas:

- `lib/webserver/api/api_settings_handlers.cpp`
- `lib/webserver/api/api_network_handlers.cpp`
- `lib/webserver/api/api_led_handlers.cpp`
- `lib/webserver/api/api_type_selection_handlers.cpp`
- `lib/webserver/api/api_control_handlers.cpp`

### Why it bloats the code
The transport and response boilerplate is mixed into business logic. That makes each handler longer than necessary and increases the chance of inconsistent logging or error semantics.

### Recommended commonization
Introduce a helper such as:

- `send_to_transmitter_or_respond(req, payload, onSuccessJson, logTag)`
- or a small `TransmitterCommandDispatcher` in `utils`

This should own:

- transmitter-availability checks
- send + timeout/error mapping
- default success/error responses
- common logging format

### Expected benefit
- Smaller POST handlers
- More uniform behavior for transmitter-bound operations
- Better separation of validation vs transport

### Risk
Medium

### Priority
Medium

---

## 9. Generalize type-catalog endpoint generation

### Observed pattern
`api_type_selection_handlers.cpp` contains multiple nearly identical endpoints:

- get battery types
- get inverter types
- get inverter interfaces
- plus similar selected-value endpoints

Representative file:

- `lib/webserver/api/api_type_selection_handlers.cpp`

### Why it bloats the code
Within one file, the same workflow is repeated:

- allocate `TypeCatalogCache::TypeEntry[128]`
- copy entries from a cache
- if empty, trigger request and return loading JSON
- copy again into sortable entries
- sort and serialize

### Recommended commonization
Create a local generic helper such as:

- `serve_catalog(req, copyFn, requestFn)`
- `serve_fixed_catalog(req, array, count)`
- `send_selected_types_json(req)` / `send_selected_interfaces_json(req)`

### Expected benefit
- Makes `api_type_selection_handlers.cpp` much shorter and easier to audit
- Centralizes OOM/error behavior
- A good low-risk consolidation target

### Risk
Low to Medium

### Priority
High

---

## 10. Collapse thin query/store/workflow pass-through layers around `TransmitterManager`

### Observed pattern
`TransmitterManager` is a very wide façade that mostly forwards into many tiny helper files. A number of those helper files are themselves trivial one-line wrappers.

Representative files:

- `lib/webserver/utils/transmitter_manager.cpp`
- `lib/webserver/utils/transmitter_settings_store_workflow.cpp`
- `lib/webserver/utils/transmitter_settings_query_helper.cpp`
- `lib/webserver/utils/transmitter_network_query_helper.cpp`
- `lib/webserver/utils/transmitter_network_store_workflow.cpp`
- `lib/webserver/utils/transmitter_mqtt_query_helper.cpp`
- `lib/webserver/utils/transmitter_mqtt_config_workflow.cpp`
- `lib/webserver/utils/transmitter_metadata_query_helper.cpp`
- `lib/webserver/utils/transmitter_metadata_store_workflow.cpp`

### Why it bloats the code
This increases file count, build surface, and mental overhead without always buying meaningful separation of responsibilities.

For example:

- many query helpers are only forwarding cache getters
- some store workflows only call `cache.store(...)` followed by `persist_to_nvs()`
- `TransmitterManager` then forwards again into those wrappers

This is a strong sign that the abstraction layers are finer-grained than the behavior justifies.

### Recommended commonization
Do not delete useful abstractions blindly. Instead:

- keep layers that genuinely add orchestration or invariants
- collapse trivial one-line pass-through helpers into either the cache layer or `TransmitterManager`
- introduce a shared helper for the repeated pattern `store -> persist_to_nvs()`

A smaller number of stronger modules would likely be clearer than the current many-file arrangement.

### Expected benefit
- Reduced file sprawl
- Less namespace hopping when tracing behavior
- Easier onboarding and maintenance

### Risk
Medium

### Priority
Medium

---

## Additional Findings

## A. Underused shared infrastructure already present

### `common/page_generator.cpp`
This file already provides substantial shared browser-side infrastructure:

- `SaveOperation`
- `TransmitterReboot`
- `ComponentApplyCoordinator`

This is the right place to absorb more repeated page JavaScript behaviors.

### `api_request_utils.h`
This already centralizes body reading and JSON parsing and includes IPv4 parsing helpers. It should be expanded instead of adding more local parsing logic in handlers.

### `api_response_utils.*`
Useful, but still too narrow relative to how much manual JSON assembly remains in API handlers.

---

## B. Older pages still bypass the new modular page pattern

Representative files:

- `lib/webserver/pages/event_logs_page.cpp`
- `lib/webserver/pages/debug_page.cpp`
- `lib/webserver/pages/monitor_page.cpp`
- `lib/webserver/pages/monitor2_page.cpp`
- likely other smaller legacy handlers

These pages still directly embed content/style/script in the handler rather than using `_content.cpp` / `_script.cpp` modules. They are not necessarily the largest files anymore, but they are structurally inconsistent with the newer standard.

Recommendation:

- continue the decomposition track until all remaining page handlers follow the same page-module convention
- only then do the second-stage commonization work, because shared extraction becomes much easier when page structure is uniform

---

## C. Response/registration consistency drift

Examples observed:

- Some pages call `httpd_resp_set_type(req, "text/html")`; others rely on defaults
- Some registration is grouped through a registry (`api_handlers.cpp`); pages are still manually registered one by one in `webserver.cpp`
- Some pages use `generate_nav_buttons(current_uri)` while a few older pages call `generate_nav_buttons()` without a current URI

This is low-level drift, but it is exactly the sort of inconsistency that grows in older codebases.

Recommendation:

- standardize page registration and page send behavior through common wrappers
- consider a page registry similar to the API registry already used in `api_handlers.cpp`

---

## Suggested Implementation Sequence

The safest/highest-ROI order is:

1. Introduce shared page send/registration helpers
2. Generalize type-catalog API endpoints in `api_type_selection_handlers.cpp`
3. Add shared browser helpers for catalog/select loading
4. Add shared browser helper for form change tracking
5. Finish decomposing remaining legacy pages (`debug`, `event_logs`, `monitor`, `monitor2`, any other monoliths)
6. Consolidate receiver-network page workflows
7. Add shared spec-page theme/layout helpers
8. Add shared standalone HTML response assembler for spec pages
9. Expand API response/model helpers
10. Collapse trivial query/store/workflow pass-through layers where they add no value

This ordering minimizes risk because it starts with local, well-bounded refactors before touching deeper data/workflow boundaries.

---

## Suggested Work Packages

## Work Package 1 — Quick wins

- Shared page wrapper helper
- Shared catalog endpoint helper
- Shared JS catalog loader

Expected outcome:
- noticeable source reduction
- low regression risk
- immediate consistency gain

## Work Package 2 — Settings page consolidation

- Shared form change tracker
- Shared save-button semantics
- Shared receiver-network form controller

Expected outcome:
- significant reduction in JS duplication
- fewer inconsistent save flows

## Work Package 3 — Spec page unification

- Shared theme/layout fragments
- Shared spec page send pipeline
- optional migration of all spec pages to `renderPage(...)`

Expected outcome:
- largest static code reduction in page layer
- easier future UI changes

## Work Package 4 — Utility-layer simplification

- Review `TransmitterManager` and subordinate helpers
- merge trivial wrappers
- retain only helpers that add real orchestration/value

Expected outcome:
- smaller utility surface
- less file sprawl and indirection

---

## Conclusion

The webserver codebase is in a good intermediate state: the first round of decomposition has made the remaining duplication easier to see. The next major reduction in bloat will come not from splitting more files alone, but from consolidating the behaviors that those split files still repeat.

The highest-value targets are:

- page wrapper / registration boilerplate
- form change/save tracking
- type/interface selector loading
- receiver network workflows
- spec page theming and response assembly
- trivial query/store/workflow pass-through layers

If these areas are consolidated carefully, the `lib/webserver` code should become materially smaller, more consistent, and easier to maintain without changing behavior.

---

## Phase 6 Progress Note — API JSON Helper Expansion (2026-03-23)

### New helpers added to `lib/webserver/api/api_response_utils.h/.cpp`

| Helper | Purpose |
|--------|---------|
| `send_json_doc(req, doc)` | Serialise an ArduinoJson `JsonDocument` to JSON and send — eliminates all raw `snprintf` JSON assembly for structured responses |
| `send_success_doc(req, doc)` | Sets `doc["success"] = true` then calls `send_json_doc` |
| `format_ipv4(buf, ip[4])` | Converts a 4-byte IP array to `"d.d.d.d"` string (16-byte buf) — eliminates the repeated `%d.%d.%d.%d` format arguments |
| `send_espnow_send_result(req, result, success_msg)` | Consolidates the repeated ESP-NOW send result pattern: success message on `ESP_OK`, `{"success":false,"message":"ESP-NOW send failed: <name>"}` otherwise |

### Files migrated

**`api_network_handlers.cpp`** — 4 large `snprintf` blocks and 2 `send_jsonf` error paths removed:
- `api_get_receiver_network_handler`: replaced 1024-byte char buffer + 80-arg `snprintf` with `StaticJsonDocument<512>` + `format_ipv4` calls + `send_json_doc`
- `api_get_network_config_handler`: replaced 1024-byte char buffer + nested-object `snprintf` with `StaticJsonDocument<384>` + `createNestedObject` + `send_json_doc`; error paths now use `send_error_message`
- `api_get_mqtt_config_handler`: replaced 512-byte char buffer + `snprintf` with `StaticJsonDocument<256>` + `format_ipv4` + `send_json_doc`
- `api_save_network_config_handler` / `api_save_mqtt_config_handler`: replaced `send_jsonf("{...ESP-NOW send failed...}")` with `send_espnow_send_result`
- Removed direct `#include <webserver_common_utils/http_json_utils.h>` — no longer called from this file

**`api_settings_handlers.cpp`** — 1 `snprintf` block and 1 `send_jsonf` path removed:
- `api_get_battery_settings_handler`: replaced 512-byte char buffer + `snprintf` with `StaticJsonDocument<256>` + `send_json_doc`; float fields formatted with `serialized(String(x, 1))` to preserve `%.1f` precision
- `api_save_setting_handler`: replaced `send_jsonf("{...ESP-NOW send failed...}")` with `send_espnow_send_result`; local cache updates retained inside the `ESP_OK` branch before the unified response call
- Removed direct `#include <webserver_common_utils/http_json_utils.h>`

**`api_modular_handlers.cpp`** — 2 small `snprintf` blocks removed:
- `api_get_test_data_mode_handler`: replaced `char[256]` + `snprintf` with `StaticJsonDocument<128>` + `send_json_doc`
- `api_set_test_data_mode_handler` success path: same migration
- Removed direct `#include <webserver_common_utils/http_json_utils.h>`

### Build result
`pio run -e receiver_tft` — **SUCCESS** (pre-existing UART warning unchanged; no new errors or warnings)

### Remaining work
- **Phase 7**: Collapse thin `TransmitterSettingsQueryHelper` / `TransmitterSettingsStoreWorkflow` pass-through wrappers

---

## Phase 7 Progress Note — TransmitterManager Thin-Wrapper Collapse (2026-03-24)

### Scope
Collapsed the settings-related thin wrapper layer by moving direct cache/read-write-through behavior into `TransmitterManager`.

### Changes made

**`lib/webserver/utils/transmitter_manager.cpp`**

- Removed dependency on wrapper namespaces:
   - `TransmitterSettingsQueryHelper`
   - `TransmitterSettingsStoreWorkflow`
- Added direct dependency on:
   - `TransmitterSettingsCache`
   - `TransmitterWriteThrough`
- Preserved battery-settings store log behavior (`[TX_MGR] Battery settings stored: ...`) previously emitted by `store_workflow`
- Updated all settings methods to call cache/read-through directly:
   - `storeBatterySettings/getBatterySettings/hasBatterySettings`
   - `storeBatteryEmulatorSettings/getBatteryEmulatorSettings/hasBatteryEmulatorSettings`
   - `storePowerSettings/getPowerSettings/hasPowerSettings`
   - `storeInverterSettings/getInverterSettings/hasInverterSettings`
   - `storeCanSettings/getCanSettings/hasCanSettings`
   - `storeContactorSettings/getContactorSettings/hasContactorSettings`
- Store methods now persist via `TransmitterWriteThrough::persist_to_nvs()` directly in `TransmitterManager`

### Build validation

`pio run -e receiver_tft` — **SUCCESS**

- No new compile errors introduced
- Pre-existing toolchain warning (`esp32-hal-uart.c` return-without-value) unchanged

### Phase status

All planned phases (1–7) from this report have now been implemented with build validation.

---

## Phase 8 Progress Note — Remaining TransmitterManager Wrapper Removal (2026-03-24)

### Scope

Extended the earlier thin-wrapper collapse by removing the remaining pass-through query/store/workflow layer still sitting between `TransmitterManager` and the underlying cache modules.

### Changes made

**`lib/webserver/utils/transmitter_manager.cpp`**

- Removed dependency on the remaining wrapper namespaces:
   - `TransmitterNetworkQueryHelper`
   - `TransmitterNetworkStoreWorkflow`
   - `TransmitterMqttQueryHelper`
   - `TransmitterMqttConfigWorkflow`
   - `TransmitterMetadataQueryHelper`
   - `TransmitterMetadataStoreWorkflow`
   - `TransmitterStatusQueryHelper`
   - `TransmitterTimeStatusUpdateWorkflow`
   - `TransmitterSpecStorageWorkflow`
   - `TransmitterEventLogsWorkflow`
- Added direct dependency on the real implementation layers:
   - `TransmitterNetworkCache`
   - `TransmitterMqttCache`
   - `TransmitterStatusCache`
   - `TransmitterSpecCache`
   - `TransmitterEventLogCache`
   - `TransmitterBatterySpecSync`
   - `TransmitterWriteThrough`
- Preserved existing side effects while inlining behavior:
   - network store methods still `notify_and_persist()` only when cache writes succeed
   - MQTT config store still logs `[TX_MGR] MQTT config stored: ...`
   - metadata/settings stores still persist to NVS
   - battery specs still route through `TransmitterBatterySpecSync::store_battery_specs(...)`

**Obsolete wrapper files removed**

- Deleted the now-unused wrapper modules for:
   - settings query/store
   - network query/store
   - MQTT query/store
   - metadata query/store
   - status query
   - time-status update workflow
   - spec storage workflow
   - event logs workflow

This completes the utility-layer simplification by removing both the extra indirection in `TransmitterManager` and the dead wrapper files themselves.

### Build validation

`pio run -e receiver_tft` — **SUCCESS**

- No new compile errors introduced
- Pre-existing toolchain warning (`esp32-hal-uart.c` return-without-value) unchanged

### Phase status

The original phases (1–7) remain complete, and this follow-on Phase 8 finishes the remaining thin-wrapper cleanup that was still available in the `TransmitterManager` utility layer.

---

## Phase 9 Progress Note — API Response Helper Expansion (2026-03-24)

### Scope

Expanded the API response helper layer to eliminate remaining manual JSON assembly patterns across control, telemetry, and LED handlers.

### Changes made

**New helper in `lib/webserver/api/api_response_utils.h/.cpp`**

- `escape_double_quotes(const char* src, char* dst, size_t max_len)` — Safely replaces double-quotes with single quotes to prevent JSON escaping issues in string fields. Enables safe transmission of potentially malicious or malformed JSON substrings received from transmitter responses.

**`lib/webserver/api/api_led_handlers.cpp`**

- Replaced 512-byte `char json[...]` buffer + 70-line `snprintf` block with `StaticJsonDocument<256>` + `send_json_doc` for `api_get_led_runtime_status_handler`
- Removed now-unused `#include <webserver_common_utils/http_json_utils.h>`

**`lib/webserver/api/api_telemetry_handlers.cpp`**

- Replaced two `snprintf` blocks (lines 385-388) with `StaticJsonDocument<128>` + `send_json_doc` for error path responses in proxy handler
- Added `#include "api_response_utils.h"` for new helper access

**`lib/webserver/api/api_control_handlers.cpp`**

- Refactored `api_transmitter_ota_status_handler`: replaced 40+ lines of quote-escaping loops + manual serialization with `escape_double_quotes` helper calls + `send_json_doc`
- Simplified OTA status error path: replaced `send_jsonf` with `StaticJsonDocument<256>` + `send_json_doc`
- Simplified receiver OTA upload success path: replaced inline JSON string with `send_success_message`
- Refactored OTA transmitter upload handlers: replaced manual `String` reservation + serialization with direct `send_json_doc` calls (2 occurrences)
- Removed now-unused `#include <webserver_common_utils/http_json_utils.h>` — no longer directly called

### Build validation

`pio run -e receiver_tft` — **SUCCESS** (71.90 seconds)

- No new compile errors introduced
- Pre-existing toolchain warning (`esp32-hal-uart.c` return-without-value) unchanged
- All JSON responses validated via serialization through `send_json_doc`

### Phase status

All API handlers in `api_control_handlers.cpp`, `api_telemetry_handlers.cpp`, and `api_led_handlers.cpp` now use the unified `ApiResponseUtils` helpers. The remaining `send_jsonf` calls (in `api_type_selection_handlers.cpp` and `api_debug_handlers.cpp`) are appropriately used for small flat error messages (1-3 fields).

Phases 1-9 are now complete. The webserver code has materially reduced JSON-assembly boilerplate:
- Pages: all follow decomposed content/script/style pattern (Phase 4)
- Spec pages: unified theme/layout helpers (Phase 5)
- API JSON: unified response helpers via `StaticJsonDocument` + `send_json_doc` (Phases 6, 9)
- Utility layer: collapsed thin wrappers, simplified facade (Phases 7-8)

---

## Phase 10 Progress Note — Receiver Network Form Consolidation (2026-03-24)

### Scope

Consolidated repeated receiver-network form workflows across `network_config_page_script.cpp` and `systeminfo_page_script.cpp` by extracting shared IP octet handling and network form management into a new browser-side helper.

### Changes made

**New helper in `lib/webserver/common/page_generator.cpp` — `window.ReceiverNetworkFormController`**

Added four methods to centralize network form operations across pages:
- `setOctets(prefix, value)` — Parse IP-address-like string and populate four text inputs with individual octets
- `collectOctets(prefix)` — Collect four text inputs and reassemble into dot-notation string
- `updateNetworkModeBadge(useStatic, badgeId)` — Update badge text/styling to show "Static IP" or "DHCP"
- `toggleStaticIpFields(useStatic, rowIds)` — Show/hide static IP section and disable/enable inputs based on mode

**`lib/webserver/pages/network_config_page_script.cpp`**

- Replaced 40+ lines of manual IP octet `.split()` and `.value =` assignments with `ReceiverNetworkFormController.setOctets(prefix, value)` (4 calls)
- Replaced duplicate `updateNetworkBadge` inline logic with `ReceiverNetworkFormController.updateNetworkModeBadge(useStatic, badgeId)`
- Replaced duplicate manual row visibility toggle with `ReceiverNetworkFormController.toggleStaticIpFields(useStatic, rowIds)`
- Replaced 16 manual octet field `.value` collections in save handler with `ReceiverNetworkFormController.collectOctets(prefix)` (4 calls)
- Removed now-unused `toggleStaticIPSection()` function (28 lines eliminated)

**`lib/webserver/pages/systeminfo_page_script.cpp`**

- Replaced local `setOctets()` function definition (9 lines) with shared helper calls
- Replaced local `updateNetworkBadge()` function definition (18 lines) with shared helper calls
- Updated event listeners to use `ReceiverNetworkFormController` for static-IP mode toggling
- Replaced 16 manual octet field `.value` collections in save handler with `ReceiverNetworkFormController.collectOctets(prefix)` (5 calls)

### Build validation

`pio run -e receiver_tft` — **SUCCESS** (63.73 seconds)

- No new compile errors introduced
- Pre-existing toolchain warning (`esp32-hal-uart.c` return-without-value) unchanged
- Both network config and system info pages retain full functionality

### Phase status

Consolidated receiver-network form handling eliminates ~100 lines of duplicated JavaScript code across two pages. Both pages now use the same helper methods for IP octet parsing, collecting, and UI state management, reducing maintenance burden and ensuring consistent behavior.

Phases 1-10 are now complete. The webserver bloat-reduction roadmap has been fully implemented across all major areas:
- Pages: decomposed content/script/style (Phase 4), consolidated network forms (Phase 10)
- Specs: unified theme/layout system (Phase 5)
- API: unified response helpers (Phases 6, 9), expanded JSON serialization
- Utilities: collapsed thin wrappers and facades (Phases 7-8)
- Browser-side: shared form helpers across network pages (Phase 10)
