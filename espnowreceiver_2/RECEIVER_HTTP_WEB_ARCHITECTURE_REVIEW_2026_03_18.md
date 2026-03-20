# Receiver HTTP/Web Codebase Review (Architecture + Implementation)

Date: 2026-03-18
Scope: receiver HTTP server, API handlers, page modules, SSE flows, and commonization opportunities with esp32common.

---

## Executive Summary

The receiver web stack has progressed significantly (modular pages, modular APIs, SSE utility layer, telemetry snapshot cutover), but it still carries **parallel legacy patterns** and **cross-project duplication** that increase maintenance cost.

### Primary findings

1. **Commonization foundation exists but is incomplete**
   - HTTP JSON/SSE helpers are already centralized via shim headers in receiver and canonical implementations in esp32common.
2. **Page architecture is partially centralized**
   - `generatePage()` + `PAGE_DEFINITIONS` exist, but navigation and URI registration are still inconsistent.
3. **Route/metadata drift exists**
   - Expected handler count constant is stale.
   - One page register function exists but is not wired into server startup.
4. **Legacy/template-era code still present**
   - `MockSettingsStore` + placeholder replacement path remain active in the large settings page path.
5. **API patterns are not yet consistently reusable**
   - Multiple handlers repeat JSON formatting/body parsing/IP parsing logic instead of using shared helpers.
6. **SSE code has duplicated telemetry extraction and mixed signaling style**
   - Similar helper logic appears in both telemetry and SSE handlers.

---

## Files and Modules Reviewed

### Receiver web entry and orchestration
- [lib/webserver/webserver.cpp](lib/webserver/webserver.cpp)
- [lib/webserver/webserver.h](lib/webserver/webserver.h)
- [lib/webserver/page_definitions.cpp](lib/webserver/page_definitions.cpp)
- [lib/webserver/page_definitions.h](lib/webserver/page_definitions.h)

### Receiver API layer
- [lib/webserver/api/api_handlers.cpp](lib/webserver/api/api_handlers.cpp)
- [lib/webserver/api/api_type_selection_handlers.cpp](lib/webserver/api/api_type_selection_handlers.cpp)
- [lib/webserver/api/api_telemetry_handlers.cpp](lib/webserver/api/api_telemetry_handlers.cpp)
- [lib/webserver/api/api_sse_handlers.cpp](lib/webserver/api/api_sse_handlers.cpp)
- [lib/webserver/api/api_network_handlers.cpp](lib/webserver/api/api_network_handlers.cpp)
- [lib/webserver/api/api_settings_handlers.cpp](lib/webserver/api/api_settings_handlers.cpp)
- [lib/webserver/api/api_control_handlers.cpp](lib/webserver/api/api_control_handlers.cpp)
- [lib/webserver/api/api_modular_handlers.cpp](lib/webserver/api/api_modular_handlers.cpp)
- [lib/webserver/api/api_debug_handlers.cpp](lib/webserver/api/api_debug_handlers.cpp)
- [lib/webserver/api/api_led_handlers.cpp](lib/webserver/api/api_led_handlers.cpp)
- [lib/webserver/api/api_response_utils.cpp](lib/webserver/api/api_response_utils.cpp)

### Receiver page/common utilities
- [lib/webserver/common/page_generator.cpp](lib/webserver/common/page_generator.cpp)
- [lib/webserver/common/nav_buttons.cpp](lib/webserver/common/nav_buttons.cpp)
- [lib/webserver/common/common_styles.h](lib/webserver/common/common_styles.h)
- [lib/webserver/processors/settings_processor.cpp](lib/webserver/processors/settings_processor.cpp)
- Representative pages in [lib/webserver/pages](lib/webserver/pages)

### Receiver web utility/cache infrastructure
- [lib/webserver/utils/transmitter_manager.cpp](lib/webserver/utils/transmitter_manager.cpp)
- [lib/webserver/utils/sse_notifier.cpp](lib/webserver/utils/sse_notifier.cpp)
- [../esp32common/webserver_common_utils/include/webserver_common_utils/http_json_utils.h](../esp32common/webserver_common_utils/include/webserver_common_utils/http_json_utils.h)
- [../esp32common/webserver_common_utils/include/webserver_common_utils/http_sse_utils.h](../esp32common/webserver_common_utils/include/webserver_common_utils/http_sse_utils.h)

### Common project comparison points (esp32common)
- [esp32common/webserver/http_json_utils.cpp](../esp32common/webserver/http_json_utils.cpp)
- [esp32common/webserver/http_sse_utils.cpp](../esp32common/webserver/http_sse_utils.cpp)
- [esp32common/include/esp32common/webserver/http_json_utils.h](../esp32common/include/esp32common/webserver/http_json_utils.h)
- [esp32common/include/esp32common/webserver/http_sse_utils.h](../esp32common/include/esp32common/webserver/http_sse_utils.h)
- Legacy/duplicate receiver web stack still present under:
  - [esp32common/webserver/receiver/webserver.cpp](../esp32common/webserver/receiver/webserver.cpp)
  - [esp32common/webserver/receiver/settings_processor.cpp](../esp32common/webserver/receiver/settings_processor.cpp)

---

## Architectural Assessment

## 1) Current strengths

- Modular split by concern (pages/api/utils/common/processors) is good.
- `TransmitterManager` is a strong data cache façade and supports debounced persistence.
- Receiver has already moved onto canonical telemetry snapshot consumption in monitor endpoints.
- SSE and JSON helpers are now commonized in esp32common with receiver-side compatibility shims.

## 2) Architectural gaps

### A. Page registry does not fully drive routing/navigation

`PAGE_DEFINITIONS` currently contains only a subset of active pages while 18 pages are registered in startup.

Impact:
- Navigation consistency is partially manual.
- URI/subtype/source-of-truth drift risk remains high.

### B. Registration metadata drift

`EXPECTED_HANDLER_COUNT` in startup is stale versus actual registered handlers.
Current rough totals are:
- 18 page handlers
- 38 core API handlers
- 8 type-selection API handlers
- 1 wildcard notfound
= 65 total

But static expectation value still references older totals.

Impact:
- Runtime metrics and startup validation can mislead debugging/operations.

### C. Orphan page registration function

`register_network_config_page()` exists but is not wired into startup registration.

Impact:
- Dead/partial functionality path and maintenance confusion.

### D. Legacy template processor path still active

`settings_page.cpp` still does placeholder substitution via `settings_processor()` and uses `MockSettingsStore` for many values.

Impact:
- Mixed architecture (template-era + API-driven dynamic UI).
- Harder to reason about data truth source.

### E. Common function misuse bug pattern

Some pages call `generatePage()` with a URI as the third parameter, but the third parameter is `extraStyles`.

Impact:
- URI strings are injected into `<style>` as invalid CSS.
- Indicates old function signature assumptions or refactor residue.

### F. API utility commonization incomplete

Body parsing + JSON response + input validation are still repeatedly implemented in multiple handlers instead of using stricter shared primitives.

Impact:
- Repetition, inconsistent error text/status, harder testing.

### G. SSE utility usage inconsistent

`api_monitor_sse_handler()` directly uses event bits and wait logic; other paths use `SSENotifier` wrappers.

Impact:
- Duplicated signaling style and higher accidental complexity.

---

## Code-Level Findings (Maintainability + Correctness)

## High priority

1. **`generatePage()` call misuse**
   - Affected pages pass URI-like strings as `extraStyles` (e.g. `"/"`, `"/transmitter"`, etc.).
   - Fix: adopt typed options or wrapper like `render_page(req, title, content, PageRenderOptions{...})` to avoid positional misuse.

2. **Stale expected handler count**
   - Fix startup expected count to computed value from registries (not manual constant).

3. **Orphan network config page registration**
   - Decide: register it intentionally or remove the page module entirely.

4. **Large settings template legacy processor**
   - Move settings page fully to API-driven DOM population and remove `%PLACEHOLDER%` replacement flow.

## Medium priority

5. **Duplicate telemetry formatter helper**
   - `fill_snapshot_telemetry(...)` duplicated in telemetry + SSE handlers.
   - Move to a shared helper module (receiver-local first, then esp32common if reusable across projects).

6. **Repeated request parsing and IP parsing logic**
   - Introduce reusable request/body/IP validators in common web utilities.

7. **Mixed response mechanisms**
   - Some handlers use `ApiResponseUtils`, others directly call `HttpJsonUtils` + ad-hoc `snprintf`.
   - Standardize around one thin response layer.

8. **String-heavy page assembly**
   - Very large `String` + rawliteral scripts in C++ are hard to maintain.
   - Consider static assets in LittleFS for large pages/scripts (progressive migration).

## Low priority

9. **Naming consistency in page comments/routes**
   - Several comments still refer to old routes (`/`, `/monitor2`) while actual registration is under `/transmitter/...`.

10. **Legacy duplicate receiver webserver in esp32common**
   - Keep only as temporary transition target; then remove when shared components are extracted and consumed.

---

## Commonization Opportunities (What should move to esp32common)

## Phase-ready candidates

1. **HTTP API registration helpers**
   - Shared helper for URI-array registration + count + logging.

2. **JSON body read + validation helpers**
   - Wrap `read_request_body`, JSON parse, required field validation, typed extraction.

3. **Network/IP parse and formatting helpers**
   - Shared strict `parse_ipv4`, `format_ipv4`, and range-safe validation.

4. **SSE session helper**
   - Common session scaffolding (`begin`, `retry`, `ping cadence`, `close`, error metrics hooks).

5. **Page rendering helper v2**
   - Replace positional `generatePage(title, content, extraStyles, script)` with typed options struct to prevent argument misuse.

6. **Navigation/page registry framework**
   - Move page registry data model and nav generation to common.
   - Receiver/project then provides only registry entries and route handlers.

---

## Proposed Refactor Plan (with mandatory legacy removal)

**Policy for every completed step:**

> On each step completion, all obsolete/parallel/legacy code paths introduced by that step are removed immediately, and this document is updated with the removed items.

### Step 1 — Registry and route integrity hardening

Scope:
- Make page registry authoritative for all navigable pages.
- Fix stale expected handler count by computed registration totals.
- Resolve orphan `register_network_config_page()` (register or delete).

Legacy/removal required at completion:
- Remove any duplicate/manual route declarations that become redundant.
- Remove outdated comments/docs that describe old route layouts.

Completion update (2026-03-18): ✅
- `PAGE_DEFINITIONS` expanded to authoritative navigable page list (including transmitter/receiver/tool/spec pages).
- `api_monitor_sse_handler()` subtype lookup updated to canonical route (`/transmitter/monitor2`) so page-to-subtype mapping is route-correct.
- Startup now computes expected handler totals from page count + `expected_all_api_handlers()` instead of stale hardcoded total.
- Orphan route was resolved by wiring `register_network_config_page()` into startup registration.
- Outdated route references were removed from page navigation contexts (`/monitor`, `/monitor2`, `/battery_settings`, `/reboot`, and root-settings comment text).

### Step 2 — Page rendering API hardening

Scope:
- Introduce typed render options wrapper for page generation.
- Replace all incorrect positional `generatePage(...)` usage.

Legacy/removal required at completion:
- Remove ambiguous/legacy overload usage patterns.
- Remove any URI-as-style argument call sites.

Completion update (2026-03-18): ✅
- Added typed page render API: `PageRenderOptions` + `renderPage(...)` in the shared page generator.
- Migrated all 15 receiver page call sites away from positional `generatePage(...)`.
- Removed all URI-as-third-argument misuse (`"/"`, `"/transmitter"`, `"/transmitter/hardware"`, `"/cellmonitor"`) that had been silently treated as CSS.
- Removed the ambiguous positional page rendering API from active use entirely; all page rendering is now named/typed via options.

### Step 3 — Settings page architecture cleanup

Scope:
- Convert settings page to pure API-driven model.
- Eliminate `%PLACEHOLDER%` server-side replacement loop.

Legacy/removal required at completion:
- Remove `settings_processor()` template placeholder path.
- Remove `MockSettingsStore` dependence in receiver web stack.

Completion update (2026-03-18): ✅
- Removed the `%PLACEHOLDER%` replacement loop from the receiver settings page; page population is now entirely API/JavaScript-driven.
- Removed `settings_processor` include and the remaining template-processing comments from the receiver settings page implementation.
- Deleted the receiver-local `settings_processor.h/.cpp` files.
- Removed `MockSettingsStore` from the receiver webserver public header.
- Confirmed no remaining `settings_processor` or `MockSettingsStore` references under the receiver web stack.

### Step 4 — API utility consolidation

Scope:
- Standardize request parsing, JSON responses, and input validation utilities.
- Consolidate repeated IP parser and JSON error-handling patterns.

Legacy/removal required at completion:
- Remove duplicate local parsing functions that shared helpers replace.
- Remove ad-hoc inconsistent response formatting branches.

Completion update (2026-03-18): ✅
- Added a shared receiver API request helper layer (`api_request_utils.h`) for request-body reading, JSON deserialization, and strict IPv4 parsing/defaulting.
- Extended `ApiResponseUtils` with standardized success, JSON parse error, and transmitter-MAC-unknown response helpers.
- Migrated the main duplicated request/response callers (`api_network_handlers.cpp`, `api_settings_handlers.cpp`, `api_modular_handlers.cpp`, `api_type_selection_handlers.cpp`, `api_led_handlers.cpp`) onto the shared helpers.
- Removed the receiver-local `parse_ip_string()` function and the repeated per-handler `read_request_body` + `deserializeJson` error branches that it superseded.
- Removed repeated ad-hoc `"Transmitter MAC unknown"`, `"JSON parse error"`, and bare `{"success":true}` response formatting branches where shared helpers now own the behavior.

### Step 5 — SSE + telemetry helper unification

Scope:
- Move duplicated telemetry formatting helper to shared module.
- Standardize notifier/event wait style.

Legacy/removal required at completion:
- Remove duplicated helper definitions from individual handlers.
- Remove direct event-bit manipulation where wrapper APIs supersede it.

Completion update (2026-03-18): ✅
- Added shared telemetry snapshot helper module (`telemetry_snapshot_utils.h`) and migrated both telemetry HTTP and SSE monitor flows to consume it.
- Removed duplicated local `fill_snapshot_telemetry(...)` definitions from `api_telemetry_handlers.cpp` and `api_sse_handlers.cpp`.
- Replaced direct monitor SSE `xEventGroupWaitBits(...)` usage with `SSENotifier::waitForUpdate(...)` wrapper for consistent event-wait style.
- Removed legacy direct event-group accessor API (`SSENotifier::getEventGroup()`) after wrapper migration.

### Step 6 — Cross-project common extraction finalization

Scope:
- Extract reusable web primitives to esp32common.
- Consume from receiver via canonical includes.

Legacy/removal required at completion:
- Remove duplicated legacy receiver webserver stack in esp32common (or archive explicitly out of build).
- Remove receiver-local compatibility shims no longer needed.

Completion update (2026-03-18): ✅
- Receiver web API handlers now include canonical shared HTTP utility headers directly from `webserver_common_utils` (`http_json_utils.h`, `http_sse_utils.h`) instead of receiver-local shim headers.
- Removed receiver-local compatibility shim files:
   - `lib/webserver/utils/http_json_utils.h/.cpp`
   - `lib/webserver/utils/http_sse_utils.h/.cpp`
- Updated `esp32common/library.json` source filter to explicitly exclude the legacy duplicate receiver webserver subtree from build (`webserver/receiver/`).
- Verified receiver build still succeeds after commonization (`receiver_tft` build success).

---

## Step Tracking and Live Update Section

Status legend: ✅ complete | 🔄 in progress | ⏳ pending

| Step | Status | Completed work | Legacy/Redundant removed | Notes |
|---|---|---|---|---|
| 1. Registry + route integrity | ✅ complete | Registry expanded, expected count derived from registries, `/receiver/network` page registration wired, route references normalized | Removed stale hardcoded expected-handler constant usage and removed outdated old-route references in nav/subtype mapping/comments | Build verified (`receiver_tft` success) |
| 2. Render API hardening | ✅ complete | Added `PageRenderOptions` + `renderPage(...)`, migrated all page render call sites | Removed ambiguous positional `generatePage(...)` usage and eliminated all URI-as-style argument call sites | Build verified (`receiver_tft` success) |
| 3. Settings architecture cleanup | ✅ complete | Settings page converted to pure API-driven rendering path | Removed `%PLACEHOLDER%` replacement loop, deleted receiver `settings_processor.h/.cpp`, removed `MockSettingsStore` from receiver web stack | Build verified (`receiver_tft` success) |
| 4. API utility consolidation | ✅ complete | Added shared request/validation helpers and migrated the main duplicated API handlers | Removed receiver-local `parse_ip_string()` and repeated ad-hoc body-parse / JSON-error / MAC-unknown response branches | Build verified (`receiver_tft` success) |
| 5. SSE/telemetry helper unify | ✅ complete | Shared telemetry snapshot helper extracted and SSE monitor wait style standardized to notifier wrapper | Removed duplicated local telemetry helper definitions, removed direct monitor SSE event-bit manipulation, removed legacy `SSENotifier::getEventGroup()` accessor | Build verified (`receiver_tft` success) |
| 6. Cross-project extraction finalization | ✅ complete | Receiver switched to canonical `webserver_common_utils` includes and legacy duplicate common receiver subtree excluded from `esp32common` build | Removed receiver-local HTTP JSON/SSE compatibility shims and archived legacy common receiver web sources out of build via `srcFilter` exclusion | Build verified (`receiver_tft` success) |

---

## Suggested Immediate Implementation Order

1. Step 1 (low-risk correctness + observability fixes)
2. Step 2 (prevent further rendering signature misuse)
3. Step 3 (largest legacy removal win)
4. Step 4 + Step 5 (shared helper consolidation)
5. Step 6 (final commonization and cleanup)

---

## Definition of Done for this review stream

- Receiver web stack is registry-driven and deterministic.
- Page rendering and API behavior are consistent and utility-driven.
- SSE and telemetry helper logic is single-source.
- Old template-era and compatibility-only receiver web paths are removed.
- esp32common contains true shared primitives, not duplicated project forks.
- This document is updated at each completed step with explicit removals.
