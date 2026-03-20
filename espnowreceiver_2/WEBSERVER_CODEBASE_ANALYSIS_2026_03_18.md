# Receiver Webserver Codebase — Analysis & Refactor Plan

**Date:** 2026-03-18
**Scope:** Full read of `lib/webserver/` — all API, page, common, and utility files
**Purpose:** Honest codebase assessment with an actionable, tracked refactor plan

---

## Standing Policies (apply to every step)

> **1. Legacy removal is mandatory at each step.**
> All redundant, obsolete, duplicate, or compatibility-only code introduced or superseded by a step is removed at that step's completion. Code that is merely "unused but harmless" does not survive.

> **2. This document is updated at each step completion.**
> The tracking table and step notes are updated to record exactly what was changed and what was removed. The document is the single source of truth for the state of the refactor.

> **3. Prefer a small signature adjustment over cloning.**
> When a function is *almost* reusable, adjust its signature or add a parameter rather than copying it. Cloning is only acceptable when the two functions have genuinely divergent invariants that cannot be unified without breaking consumers. Every copy-for-minor-variation is a future drift bug.

---

## Summary Verdict

The codebase is **significantly cleaner** than the original battery emulator split. The modular page/API split is sound, routing is registry-driven, HTTP utilities are centralised, and SSE/telemetry helpers are shared. The recent refactor steps eliminated the largest structural gaps.

However, **5 confirmed defects** exist in production-running code (not style issues), and **9 maintainability problems** have grown naturally as features accumulated. These are documented and planned below.

---

## 1. Confirmed Defects

### D1 — `Serial.println` / `Serial.flush()` in production API handler
**File:** [lib/webserver/api/api_settings_handlers.cpp](lib/webserver/api/api_settings_handlers.cpp) — `api_save_setting_handler()`

```cpp
Serial.println("\n\n===== API SAVE SETTING CALLED =====");
Serial.flush();   // blocks UART
Serial.printf("Content length: %d\n", req->content_len);
// ...multiple further Serial.printf calls throughout the handler...
```

These bypass the `LOG_*` system, pollute serial output unconditionally in all environments, and `Serial.flush()` blocks the UART during a live request. All other handlers use `LOG_INFO`/`LOG_DEBUG` correctly. Convert — do not comment out, remove.

---

### D2 — Cell data JSON built twice in parallel
**File A:** [lib/webserver/api/api_telemetry_handlers.cpp](lib/webserver/api/api_telemetry_handlers.cpp) — `api_cell_data_handler()`
**File B:** [lib/webserver/api/api_sse_handlers.cpp](lib/webserver/api/api_sse_handlers.cpp) — `sendCellData` lambda inside `api_cell_data_sse_handler()`

Both build an identical JSON structure from a `CellDataSnapshot` using the same `String +=` pattern: cells array, balancing array, min/max voltage, balancing_active, mode. Any schema change must be made in both places. The schema has already diverged: the HTTP handler emits `"mode"` twice in the same JSON object; the SSE path emits it once.

**Signature adjustment (preferred over cloning):**
`TelemetrySnapshotUtils` already owns `fill_snapshot_telemetry()` — extend it with one free function:

```cpp
// telemetry_snapshot_utils.h — ADD only, no handler signatures change
String serialize_cell_data(const TransmitterManager::CellDataSnapshot& snapshot);
```

Both callers replace their inline JSON build with a single call. No handler signatures change.

---

### D3 — `DynamicJsonDocument` in a handler that should use static allocation
**File:** [lib/webserver/api/api_modular_handlers.cpp](lib/webserver/api/api_modular_handlers.cpp) — `api_set_test_data_mode_handler()`

```cpp
DynamicJsonDocument doc(256);   // heap alloc for 256 bytes
```

`StaticJsonDocument<256>` is used consistently everywhere else. This single dynamic allocation for a small, known-size document adds unnecessary heap fragmentation on the ESP32. Change to `StaticJsonDocument<256>`.

(`api_system_metrics_handler` uses `DynamicJsonDocument(2048)` for a large variable-size output — that is acceptable.)

---

### D4 — `debug_page.cpp` builds CSS/JS via string concatenation into `<body>`
**File:** [lib/webserver/pages/debug_page.cpp](lib/webserver/pages/debug_page.cpp)

`debug_page_processor()` builds HTML entirely via `html += "<tag>..."` concatenation and injects `<style>` and `<script>` blocks into the body content string passed to `renderPage()`. The result:

- Page-specific CSS lands in `<body>`, not `<head>`
- `renderPage()` is called with empty `PageRenderOptions`, so `<head>` has no page script
- No rawliteral strings — the page is unreadable C++ that no tooling (lint, format, IntelliSense) can help with
- `generate_nav_buttons()` called without a current-URI argument so the active page is not excluded from its own nav links

This is the only page in the codebase that violates the standard pattern. Rewrite using rawliteral content + `PageRenderOptions(extraStyles, script)` — identical structure to every other page.

---

### D5 — `expected_page_handler_count` is a magic number not derived from `PAGE_COUNT`
**File:** [lib/webserver/webserver.cpp](lib/webserver/webserver.cpp)

```cpp
const int expected_page_handler_count = 19;                       // magic number
const int expected_api_handler_count = expected_all_api_handlers(); // correctly derived
```

The value `19` happens to match `PAGE_COUNT` today — both are 19 — so the startup validation check passes. But `expected_api_handler_count` is correctly derived from a function. Adding a page to `PAGE_DEFINITIONS` without updating this magic number causes a silent discrepancy. Fix is one line:

```cpp
const int expected_page_handler_count = PAGE_COUNT;
```

---

## 2. Maintainability Issues

### M1 — `TransmitterManager` is a God Class
**Files:** [lib/webserver/utils/transmitter_manager.h](lib/webserver/utils/transmitter_manager.h) / [transmitter_manager.cpp](lib/webserver/utils/transmitter_manager.cpp) (341-line header, 1265-line implementation)

The class owns **15 distinct data domains** as a single static class with a single mutex:

| Domain | Key members |
|--------|------------|
| MAC address | `mac[6]`, `mac_known` |
| Current network IP | `current_ip/gw/subnet` |
| Static network config | `static_ip/gw/subnet/dns1/dns2` |
| MQTT config | server, port, creds, version |
| Runtime beacon status | last_beacon_time, send_success, ethernet |
| Firmware metadata | env, device, version, build_date |
| Battery settings | `BatterySettings` |
| Battery emulator settings | `BatteryEmulatorSettings` |
| Power settings | `PowerSettings` |
| Inverter settings | `InverterSettings` |
| CAN/Contactor settings | `CanSettings`, `ContactorSettings` |
| Static spec JSON blobs | 5 `String` fields |
| Cell monitor data | `cell_voltages_mV_[128]` + balancing |
| Event logs | `vector<EventLogEntry>` |
| Time data | `uptime_ms`, `unix_time` |

Adding any new data domain defaults to "put it in TransmitterManager". Suggested long-term decomposition (Step 7, multi-sprint):

- `TransmitterNetworkCache` — IP + MQTT + network config
- `TransmitterSettingsCache` — battery/inverter/power/CAN/contactor
- `TransmitterStatusCache` — runtime beacon, metadata, time
- `TransmitterSpecCache` — static spec JSON, cell data, event logs
- `TransmitterManager` becomes a thin init/NVS facade delegating to these

---

### M2 — `common_styles.h` uses `const char*` and contains a duplicate CSS class
**File:** [lib/webserver/common/common_styles.h](lib/webserver/common/common_styles.h)

```cpp
const char* COMMON_STYLES = R"rawliteral(...)rawliteral";
```

Declaring as `const char*` at header scope means each translation unit that includes it gets its own copy of the pointer; the string literal may not be deduplicated in flash. Correct form:

```cpp
static constexpr char COMMON_STYLES[] = R"rawliteral(...)rawliteral";
```

Additionally, `.note` and `.settings-note` are pixel-identical CSS rules (same background, padding, border-radius, font-weight). One must be removed; all pages should use one canonical class name.

---

### M3 — Nav skip list hardcoded instead of data-driven
**File:** [lib/webserver/common/nav_buttons.cpp](lib/webserver/common/nav_buttons.cpp)

```cpp
if (strcmp(PAGE_DEFINITIONS[i].uri, "/transmitter/config") == 0 ||
    strcmp(PAGE_DEFINITIONS[i].uri, "/receiver/config") == 0) {
    continue;
}
```

`PageInfo` already carries `needs_sse` as a data-driven flag. Adding `hide_from_nav` follows the same pattern.

**Signature adjustment (preferred):** Add one field to `PageInfo`, set it in the two relevant `PAGE_DEFINITIONS` entries, replace the `strcmp` block with `if (PAGE_DEFINITIONS[i].hide_from_nav) continue;`. No handler or page file changes.

---

### M4 — `api_get_data_source_handler` and `api_set_data_source_handler` are dead stubs
**File A:** [lib/webserver/api/api_telemetry_handlers.cpp](lib/webserver/api/api_telemetry_handlers.cpp)
**File B:** [lib/webserver/api/api_settings_handlers.cpp](lib/webserver/api/api_settings_handlers.cpp)

The GET handler unconditionally returns `{"mode":"live"}`. The POST handler ignores its body and returns a hardcoded string. Neither performs any logic. Both consume URI handler slots and `kCoreApiHandlers` entries. Remove: function bodies, `.h` declarations, both handler table entries, and all JS call sites.

---

### M5 — Dual MAC address fallback in `api_reboot_handler`
**File:** [lib/webserver/api/api_control_handlers.cpp](lib/webserver/api/api_control_handlers.cpp)

The handler checks `TransmitterManager`, then falls back to `ESPNow::transmitter_mac` if no MAC is known. This creates two sources of truth for the transmitter MAC.

**Signature adjustment (preferred):** Move the fallback logic inside `TransmitterManager::getMAC()` / `isMACKnown()` — check `ESPNow::transmitter_mac` as a secondary source. The reboot handler then has a single clean call to `TransmitterManager`. Any future caller gets the same resilience without reimplementing the fallback.

---

### M6 — `capitalizeWords()` (C++) duplicates `formatEnvName()` (JS)
**File (C++):** [lib/webserver/pages/dashboard_page.cpp](lib/webserver/pages/dashboard_page.cpp)
**File (JS):** [lib/webserver/common/page_generator.cpp](lib/webserver/common/page_generator.cpp) — inside `COMMON_SCRIPT_HELPERS`

Both do the same transform (split on `-_`, capitalise each word). They can diverge independently.

**Signature adjustment (preferred):** Remove `capitalizeWords()` from C++ entirely. Have the dashboard handler emit the raw `metadata.env_name` as a `data-` attribute or JSON field. The browser-side `formatEnvName()` applies the transform on `DOMContentLoaded` — as it already does for subsequent API refreshes. One function removed, no new code, no divergence possible.

---

### M7 — JSON serialization strategy is inconsistent across handlers
Four different approaches in use with no documented policy:

| Approach | Where | Risk |
|----------|-------|------|
| `snprintf` into stack `char[]` | Most telemetry handlers | Buffer overrun if schema grows; `%s` from `String` requires `.c_str()` churn |
| `String +=` concatenation | Cell data handlers, SSE | Heap fragmentation, no size bounds |
| `StaticJsonDocument` + `serializeJson` | firmware_info, type_selection | Correct and safe |
| `DynamicJsonDocument` | system_metrics (acceptable), modular handler (D3 — fix) | Heap fragmentation for small docs |

**Policy:** `StaticJsonDocument` for all structured API responses where the output schema is bounded. Reserve `snprintf` only for flat 2–3 field responses (`{"success":true,"message":"..."}` style). No new `DynamicJsonDocument` for documents under 512 bytes.

---

### M8 — Metrics infrastructure embedded in telemetry handler file
**File:** [lib/webserver/api/api_telemetry_handlers.cpp](lib/webserver/api/api_telemetry_handlers.cpp)

`HttpHandlerMetrics`, `HttpHandlerTimer`, `EventLogProxyMetrics`, and the `portMUX` are all defined privately in this file. The per-handler instrumentation is therefore coupled to the telemetry handler file and cannot be added to other handler files. Long-term: extract to `webserver_metrics.h` / `webserver_metrics.cpp`.

---

### M9 — `generate_sorted_type_json` uses heap allocation for a compile-time-sized copy
**File:** [lib/webserver/api/api_type_selection_handlers.cpp](lib/webserver/api/api_type_selection_handlers.cpp)

`battery_interfaces[]` has 6 entries — a compile-time constant. `new[]`/`delete[]` is unnecessary heap allocation for a known-size sort buffer. Replace with a stack array bounded by a compile-time constant:

```cpp
TypeEntry sorted_copy[16]; // or MAX_TYPE_ENTRIES
static_assert(count <= 16, "increase sorted_copy size");
```

No signature or caller changes required.

---

## 3. What Is Working Well (Preserve These)

| Pattern | Location | Why it's good |
|---------|---------|--------------|
| `PageRenderOptions` typed struct | `page_generator.h` | Named params prevent positional misuse |
| `ApiRequestUtils` namespace | `api_request_utils.h` | Consistent body read, JSON parse, IP parse |
| `ApiResponseUtils` namespace | `api_response_utils.cpp` | Single place for all response shapes |
| `SSENotifier` wrapper | `sse_notifier.h` | Hides event-group detail; clean `waitForUpdate()` |
| `TelemetrySnapshotUtils` | `telemetry_snapshot_utils.h` | Shared telemetry helper — extend here for D2 |
| Handler table in `api_handlers.cpp` | `api_handlers.cpp` | All routes in one array; `sizeof/sizeof` count is self-maintaining |
| `ScopedMutex` RAII | `transmitter_manager.cpp` | Correct mutex pattern, no forget-to-release risk |
| `PAGE_DEFINITIONS` registry | `page_definitions.cpp` | Single source for URI, subtype, SSE flag |
| `expected_all_api_handlers()` derived count | `api_handlers.cpp` | Self-maintaining registration check |

---

## 4. Refactor Steps

> **Policy for every step:** On completion, all code superseded by that step is removed immediately. No stubs, no `#ifdef` guards, no "leave it just in case". This document is then updated with the completion date and a summary of what changed and what was removed.

---

### Step 1 — Quick defect fixes

**Addresses:** D1, D3, D5, M10 (duplicate log + hardcoded URL list)

Changes:
- Convert all `Serial.println`/`Serial.flush()`/`Serial.printf` in `api_save_setting_handler` to `LOG_*` (D1)
- Replace `DynamicJsonDocument doc(256)` with `StaticJsonDocument<256>` in `api_set_test_data_mode_handler` (D3)
- Replace `const int expected_page_handler_count = 19` with `= PAGE_COUNT` (D5)
- Remove the duplicate `LOG_INFO("WEBSERVER", "Access webserver at: ...")` — keep only the one emitted after handler registration, remove the earlier duplicate
- Replace the 15-line hardcoded `LOG_DEBUG` URL list with a registry loop:

```cpp
for (int i = 0; i < PAGE_COUNT; i++) {
    LOG_DEBUG("WEBSERVER", "  - %s (%s)", PAGE_DEFINITIONS[i].uri, PAGE_DEFINITIONS[i].name);
}
```

Legacy removed at completion:
- All `Serial.*` calls removed from `api_save_setting_handler` with no stubs
- 15-line hardcoded URL debug block removed entirely
- Duplicate `LOG_INFO` line removed

**Completion:** ✅ complete (2026-03-18)
**Implemented:** raw `Serial.println` / `Serial.flush` / `Serial.printf` removed from `api_save_setting_handler`; `api_set_test_data_mode_handler` now uses `StaticJsonDocument<256>`; `webserver.cpp` now derives page handler count from `PAGE_COUNT`; hardcoded page URL debug lines replaced with a `PAGE_DEFINITIONS` loop; duplicate webserver access log removed.

---

### Step 2 — Cell data serialization extraction

**Addresses:** D2

Changes:
- Add `serialize_cell_data(const TransmitterManager::CellDataSnapshot&) -> String` to `telemetry_snapshot_utils.h`
- Replace inline JSON build in `api_cell_data_handler` with the new helper call
- Replace inline JSON build in `sendCellData` lambda with the new helper call
- Fix the duplicate `"mode"` field in the HTTP handler response at the same time

Legacy removed at completion:
- Both inline `String json = ...` cell-data build blocks removed from their respective handler files
- Duplicate `"mode"` field removed from the HTTP cell-data response

**Completion:** ✅ complete (2026-03-18)
**Implemented:** added `TelemetrySnapshotUtils::serialize_cell_data(const TransmitterManager::CellDataSnapshot&)`; both `api_cell_data_handler` and the SSE `sendCellData` lambda now call the shared serializer.

---

### Step 3 — `debug_page.cpp` rewrite

**Addresses:** D4

Changes:
- Rewrite `debug_page_handler` using rawliteral HTML content + `PageRenderOptions(extraStyles, script)` — same structure as every other page
- Move `<style>` content into `extraStyles`; move `<script>` content into `script` as rawliteral
- Fix `generate_nav_buttons()` call to pass `"/debug"` as the current URI

Legacy removed at completion:
- `debug_page_processor()` function wrapper removed entirely
- All `html += "<..."` concatenation lines removed
- Inline `<style>` and `<script>` tag strings removed from `renderPage()` content argument

**Completion:** ✅ complete (2026-03-18)
**Implemented:** `debug_page.cpp` now uses rawliteral HTML content, page-specific CSS in `extra_styles`, and JS in `script` via `PageRenderOptions(extra_styles, script)`. Navigation now calls `generate_nav_buttons("/debug")`.

---

### Step 4 — Nav skip list made data-driven

**Addresses:** M3

Changes:
- Add `bool hide_from_nav` field to `PageInfo` struct in `page_definitions.h`
- Update all 19 `PAGE_DEFINITIONS` entries with the new field (set `true` for `/transmitter/config` and `/receiver/config`)
- Replace the hardcoded URI comparison block in `nav_buttons.cpp` with `if (PAGE_DEFINITIONS[i].hide_from_nav) continue;`

Legacy removed at completion:
- The two `strcmp` URI comparison lines and their surrounding `if` block removed from `nav_buttons.cpp`

**Completion:** ✅ complete (2026-03-18)
**Implemented:** Added `hide_from_nav` to `PageInfo`; updated all 19 `PAGE_DEFINITIONS` initializers with explicit `hide_from_nav` values; set `true` for `/transmitter/config` and `/receiver/config`; `nav_buttons.cpp` now uses `PAGE_DEFINITIONS[i].hide_from_nav` and no longer hardcodes URIs.

---

### Step 5 — Dead endpoint removal and CSS deduplication

**Addresses:** M2, M4

Changes:
- Remove `api_get_data_source_handler` from `.cpp` and `.h`
- Remove `api_set_data_source_handler` from `.cpp` and `.h`
- Remove both from the `kCoreApiHandlers` registration table
- Audit all page JavaScript for calls to `/api/get_data_source` or `/api/set_data_source` — remove those call sites
- Remove duplicate `.settings-note` CSS class from `common_styles.h`; migrate any page using `.settings-note` to `.note`
- Fix `common_styles.h` declaration from `const char*` to `static constexpr char[]`
- Assess `/api/transmitter_ip` overlap with `/api/get_network_config` — if no page uses it exclusively, remove it

Legacy removed at completion:
- Handler function bodies, `.h` declarations, and `kCoreApiHandlers` entries for all removed endpoints
- All JS call sites for removed endpoints
- Duplicate `.settings-note` CSS class definition

**Completion:** ✅ complete (2026-03-18)
**Implemented:** Removed `api_get_data_source_handler` and `api_set_data_source_handler` from source/header and `kCoreApiHandlers`; audited page JS and found no call sites to these endpoints; removed `/api/transmitter_ip` endpoint as unused overlap with `/api/get_network_config`; updated telemetry metrics IDs/names to remove retired handlers; changed `COMMON_STYLES` to `static constexpr char[]`; removed duplicate `.settings-note` rule.

---

### Step 6 — JSON serialization standardization

**Addresses:** M7, M9

Changes:
- Audit `api_telemetry_handlers.cpp` for `snprintf`-built responses with more than 3 fields or nested objects — migrate to `StaticJsonDocument` + `serializeJson`
- Replace `new[]`/`delete[]` in `generate_sorted_type_json` with a stack array bounded by a compile-time constant
- Add a comment block to `api_response_utils.h` documenting the JSON strategy policy for this codebase

Legacy removed at completion:
- Replaced `snprintf` buffer + format string pairs removed
- The `new[]`/`delete[]` block removed

**Completion:** ✅ complete (2026-03-18)
**Implemented:** Migrated multi-field/nested telemetry responses in `api_data_handler`, `api_monitor_handler`, `api_dashboard_data_handler`, `api_version_handler`, and `api_transmitter_health_handler` to `StaticJsonDocument` + `serializeJson`; kept `snprintf` only for tiny scalar formatting and compact single-field error messages; replaced `new[]`/`delete[]` in `generate_sorted_type_json` with stack buffer `TypeEntry sorted_copy[kMaxTypeEntries]` plus bound check; added JSON response policy comment block in `api_response_utils.h`.

---

### Step 7 — `TransmitterManager` decomposition *(backlog — multi-sprint)*

**Addresses:** M1

Changes (agreed in advance per sub-step):
- Identify and freeze split boundaries: `TransmitterNetworkCache`, `TransmitterSettingsCache`, `TransmitterStatusCache`, `TransmitterSpecCache`
- Migrate one domain group at a time; keep `TransmitterManager` as init/NVS facade until all groups are migrated
- Update all consumers to use the specific cache they need

Legacy removed at completion (per sub-step):
- Each member group removed from `TransmitterManager` immediately when it migrates to its new type
- No member exists simultaneously in both old and new location at any point

#### Sub-step 1 — RuntimeStatusCache extraction ✅ COMPLETE
**Target:** Runtime-tracked members: `ethernet_connected`, `last_beacon_time_ms`, `last_espnow_send_success`, `uptime_ms`, `unix_time`, `time_source` → `struct RuntimeStatusCache`

**Implementation:**
- [x] Defined `RuntimeStatusCache` struct in `transmitter_manager.h` with 6 members (designated initializer)
- [x] Replaced 6 scattered static members with single `static RuntimeStatusCache runtime_status_` in `transmitter_manager.cpp`
- [x] Updated 8 accessor methods: `isEthernetConnected()`, `getLastBeaconTime()`, `getLastEspnowSendSuccess()`, `getUptime()`, `getUnixTime()`, `getTimeSource()`, `updateRuntimeStatus()`, `updateTimeData()`
- [x] All usages converted to `runtime_status_.field` pattern (e.g., `return runtime_status_.ethernet_connected;`)
- [x] Build validated: receiver_tft SUCCESS, RAM 28.7%, Flash 19.1% (unchanged)
- [x] No breaking changes to public API

**Legacy removed:**
- 6 static members merged into struct (no member exists in both old and new location)

**Completion:** ✅ 2026-03-22 — verified successful build

#### Sub-step 2 — NetworkCache extraction ✅ COMPLETE
**Target:** Network config members (10 fields + 1 mode + 1 version): `current_ip[4]`, `current_gateway[4]`, `current_subnet[4]`, `static_ip[4]`, `static_gateway[4]`, `static_subnet[4]`, `static_dns_primary[4]`, `static_dns_secondary[4]`, `ip_known`, `is_static_ip`, `network_config_version` → `struct NetworkCache`

**Implementation:**
- [x] Defined `NetworkCache` struct in `transmitter_manager.h` with 11 members (all IP arrays, DNS, mode, version, ip_known)
- [x] Replaced 11 scattered static members with single `static NetworkCache network_cache_` in `transmitter_manager.cpp` (designated initializer includes default DNS: 8.8.8.8 and 8.8.4.4)
- [x] Updated 11 accessor/mutation methods: `getIP()`, `getGateway()`, `getSubnet()`, `getStaticIP()`, `getStaticGateway()`, `getStaticSubnet()`, `getStaticDNSPrimary()`, `getStaticDNSSecondary()`, `isIPKnown()`, `isStaticIP()`, `getNetworkConfigVersion()`, `updateNetworkMode()`, `getIPString()`, `getURL()`
- [x] Updated 3 entry-point methods: `storeIPData()`, `storeNetworkConfig()` (call sites: loadFromNVS, saveToNVS in NVS persist layer)
- [x] All 14 usages converted to `network_cache_.field` pattern across 5 methods
- [x] Build validated: receiver_tft SUCCESS, RAM 28.7%, Flash 19.1% (unchanged)
- [x] No breaking changes to public API

**Legacy removed:**
- 11 static members merged into struct (no member exists in both old and new location)

**Completion:** ✅ 2026-03-22 — verified successful build

#### Sub-step 3 — SettingsCache extraction ✅ COMPLETE
**Target:** MQTT/battery/inverter/CAN settings (6 member groups × ~5-6 fields each)

**Sub-step 3a — MQTT cache extraction ✅ COMPLETE**
- [x] Added `MqttCache` struct in `transmitter_manager.h` and replaced 9 scattered MQTT static members with `static MqttCache mqtt_cache_`
- [x] Migrated NVS load/save paths to `mqtt_cache_` fields (`loadFromNVS()`, `persist_to_nvs_now()`)
- [x] Migrated MQTT config/accessor/runtime methods to `mqtt_cache_` field access:
    - `storeMqttConfig()`
    - `isMqttEnabled()`, `getMqttServer()`, `getMqttPort()`, `getMqttUsername()`, `getMqttPassword()`, `getMqttClientId()`
    - `isMqttConnected()`, `isMqttConfigKnown()`, `getMqttServerString()`, `getMqttConfigVersion()`
    - `updateRuntimeStatus()` now updates `mqtt_cache_.mqtt_connected`
- [x] Build validated: receiver_tft SUCCESS, RAM 28.7%, Flash 19.1%
- [x] Public API unchanged

**Sub-step 3b — Remaining settings groups ✅ COMPLETE**
- [x] Added `SettingsCache` struct in `transmitter_manager.h` and replaced remaining settings static members with `static SettingsCache settings_cache_`
- [x] Migrated NVS load/save paths to `settings_cache_` fields for all settings groups:
    - `BatterySettings`, `BatteryEmulatorSettings`, `PowerSettings`
    - `InverterSettings`, `CanSettings`, `ContactorSettings`
- [x] Migrated settings mutator/accessor methods to `settings_cache_` field access:
    - `storeBatterySettings()`, `getBatterySettings()`, `hasBatterySettings()`
    - `storeBatteryEmulatorSettings()`, `getBatteryEmulatorSettings()`, `hasBatteryEmulatorSettings()`
    - `storePowerSettings()`, `getPowerSettings()`, `hasPowerSettings()`
    - `storeInverterSettings()`, `getInverterSettings()`, `hasInverterSettings()`
    - `storeCanSettings()`, `getCanSettings()`, `hasCanSettings()`
    - `storeContactorSettings()`, `getContactorSettings()`, `hasContactorSettings()`
- [x] Updated downstream battery spec sync write path to `settings_cache_.battery_settings.cell_count`
- [x] Build validated: receiver_tft SUCCESS, RAM 28.7%, Flash 19.1%
- [x] Public API unchanged

**Completion:** ✅ 2026-03-22 — settings groups fully consolidated

#### Sub-step 4 — SpecCache extraction ✅ COMPLETE
**Target:** Static specs (`static_specs_json`, `battery_specs_json`, `inverter_specs_json`, `charger_specs_json`, `system_specs_json`, `static_specs_known`) → `struct SpecCache`

**Implementation:**
- [x] Added `SpecCache` struct in `transmitter_manager.h` and replaced 6 scattered static spec members with `static SpecCache spec_cache_`
- [x] Migrated static-spec storage methods to `spec_cache_` field access:
    - `storeStaticSpecs()`
    - `storeBatterySpecs()`
    - `storeInverterSpecs()`
    - `storeChargerSpecs()`
    - `storeSystemSpecs()`
- [x] Migrated static-spec accessors to `spec_cache_` field access:
    - `hasStaticSpecs()`
    - `getStaticSpecsJson()`, `getBatterySpecsJson()`, `getInverterSpecsJson()`, `getChargerSpecsJson()`, `getSystemSpecsJson()`
- [x] Build validated: receiver_tft SUCCESS, RAM 28.7%, Flash 19.1%
- [x] Public API unchanged

**Completion:** ✅ 2026-03-22 — static spec cache fully consolidated

**Overall Step 7 Completion:** ✅ 4/4 sub-steps complete (100%)

---

### Step 8 — Transmitter MAC source-of-truth unification

**Addresses:** M5

Changes:
- Move fallback MAC resolution into `TransmitterManager` so all callers share one source of truth
    - `getMAC()` now returns registered MAC when known, otherwise falls back to non-zero `ESPNow::transmitter_mac`
    - `isMACKnown()` now delegates to `getMAC() != nullptr`
    - `getMACString()` now formats whichever MAC `getMAC()` resolves
- Simplify `api_reboot_handler` to use a single `TransmitterManager::getMAC()` path (no endpoint-local fallback logic)

Legacy removed at completion:
- Endpoint-local fallback branch in `api_reboot_handler` that duplicated `ESPNow::transmitter_mac` probing logic
- Local `ESPNow::transmitter_mac` extern dependency from `api_control_handlers.cpp`

**Completion:** ✅ complete (2026-03-19)
**Implemented:** centralized fallback resolution in `transmitter_manager.cpp` (`getMAC`, `isMACKnown`, `getMACString`) and removed duplicate fallback logic from `api_control_handlers.cpp` reboot endpoint.

---

### Step 9 — Env-name formatting deduplication on dashboard

**Addresses:** M6

Changes:
- Removed C++ `capitalizeWords()` helper from `dashboard_page.cpp`
- Stopped server-side title-casing of environment names in dashboard handler
    - transmitter device display now emits raw `metadata.env`
    - receiver device display now emits raw `FirmwareMetadata::metadata.env_name`
- Added client-side `formatEnvName()` helper in dashboard page script and applied it on page load
    - normalizes `-`/`_` to spaces
    - title-cases words in browser

Legacy removed at completion:
- C++ `capitalizeWords()` implementation and its call sites in dashboard page generation

**Completion:** ✅ complete (2026-03-19)
**Implemented:** dashboard server output now provides raw env-name strings and browser-side script performs env-name formatting, eliminating the duplicate C++ transform path.

---

### Step 10 — Webserver metrics extraction

**Addresses:** M8

Changes:
- Extracted HTTP handler and event-log proxy metric infrastructure from `api_telemetry_handlers.cpp` into dedicated module:
    - `lib/webserver/api/webserver_metrics.h`
    - `lib/webserver/api/webserver_metrics.cpp`
- Moved from telemetry handler file into reusable module:
    - metric ID enum and handler-name map
    - HTTP handler latency accumulator and RAII timer (`HttpHandlerTimer`)
    - event-log proxy request/result counters
    - snapshot getters for system-metrics endpoint rendering
- Updated `api_telemetry_handlers.cpp` to consume the extracted metrics API rather than owning file-local metric globals and synchronization primitives.

Legacy removed at completion:
- File-local metric structs/globals and metrics mux from `api_telemetry_handlers.cpp`
- File-local `HttpHandlerTimer` and `recordEventLogProxyResult` implementations in telemetry handler file

**Completion:** ✅ complete (2026-03-19)
**Implemented:** metrics ownership moved into `webserver_metrics.{h,cpp}` and telemetry handlers now consume module APIs for both latency and event-log proxy metrics.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` succeeded (RAM 28.7%, Flash 19.1%).

---

### Step 11 — Transmitter status/metadata delegation

**Addresses:** M1 (decomposition follow-through)

Changes:
- Integrated existing `TransmitterStatusCache` module as the single owner of runtime status and firmware metadata state.
- Updated `TransmitterManager` to delegate status/metadata operations instead of owning duplicate fields:
    - metadata store/get/version APIs now forward to `TransmitterStatusCache`
    - runtime beacon/time/send-status APIs now forward to `TransmitterStatusCache`
    - transmitter-connected check now composes cache connection state with `TransmitterManager::isMACKnown()`
- Updated NVS load/save paths in `TransmitterManager` to call:
    - `TransmitterStatusCache::load_metadata_from_prefs()`
    - `TransmitterStatusCache::save_metadata_to_prefs()`

Legacy removed at completion:
- `TransmitterManager` file-local ownership of runtime status cache struct and metadata fields
- Duplicated metadata load/save key handling in `TransmitterManager` persistence paths

**Completion:** ✅ complete (2026-03-19)
**Implemented:** `TransmitterManager` now acts as a facade for status/metadata while concrete state ownership and metadata persistence logic are centralized in `transmitter_status_cache.{h,cpp}`.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` succeeded (RAM 28.7%, Flash 19.1%).

---

### Step 12 — Status cache API cleanup

**Addresses:** M1 (decomposition polish)

Changes:
- Removed unused `mqtt_conn` parameter from `TransmitterStatusCache::update_runtime_status(...)`.
- Updated `TransmitterManager::updateRuntimeStatus(...)` delegation call to pass only Ethernet runtime state.
- Kept MQTT runtime ownership in `TransmitterManager::mqtt_cache_` where it is actively consumed.

Legacy removed at completion:
- Stale/unused parameter in delegated status cache API surface

**Completion:** ✅ complete (2026-03-19)
**Implemented:** runtime status delegation API now matches actual ownership boundaries and no longer carries unused MQTT argument.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` succeeded (RAM 28.7%, Flash 19.1%).

---

### Step 13 — Cell data cache extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Extracted cell data ownership into new `cell_data_cache.{h,cpp}` module.
- Moved cell voltage arrays, balancing flags, min/max statistics, balancing-active state, and source-tag storage out of `TransmitterManager`.
- Updated MQTT ingest path to store parsed cell data via `CellDataCache::store_cell_data(...)`.
- Updated telemetry/SSE serialization call sites to consume `CellDataCache::CellDataSnapshot`.

Legacy removed at completion:
- `TransmitterManager::CellDataSnapshot`
- Cell data storage/accessor methods and backing state from `TransmitterManager`

**Completion:** ✅ complete (2026-03-19)
**Implemented:** cell-monitor data is now owned by a dedicated cache module with thread-safe snapshot reads.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` and `pio run -e receiver_lvgl -j 12` succeeded.

---

### Step 14 — Spec cache extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Extracted spec JSON ownership into new `transmitter_spec_cache.{h,cpp}` module.
- Moved combined and per-domain JSON storage for battery/inverter/charger/system specs out of `TransmitterManager`.
- Kept `TransmitterManager` as a compatibility facade by delegating spec getters/setters to `TransmitterSpecCache`.
- Preserved the one remaining cross-domain side effect in `TransmitterManager::storeBatterySpecs(...)`: syncing `settings_cache_.battery_settings.cell_count` from battery specs payloads.

Legacy removed at completion:
- `TransmitterManager::SpecCache`
- Spec JSON ownership and serialization logic from `TransmitterManager`

**Completion:** ✅ complete (2026-03-19)
**Implemented:** spec-data storage is now centralized in its own cache module while the existing public `TransmitterManager` API remains stable.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` and `pio run -e receiver_lvgl -j 12` succeeded.

---

### Step 15 — Event log cache extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Extracted event-log storage and parsing into new `transmitter_event_log_cache.{h,cpp}` module.
- Moved event-log ownership state (`vector`, known flag, last-update timestamp) out of `TransmitterManager`.
- Updated `TransmitterManager` event-log API methods to delegate to `TransmitterEventLogCache`.
- Preserved existing `TransmitterManager::EventLogEntry` external API by converting cache snapshot entries in the facade.

Legacy removed at completion:
- Event-log backing state and parse/store logic from `TransmitterManager`

**Completion:** ✅ complete (2026-03-19)
**Implemented:** event-log lifecycle is now owned by a dedicated cache module while call-site compatibility remains unchanged.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` and `pio run -e receiver_lvgl -j 12` succeeded.

---

### Step 16 — Settings cache extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Extracted settings ownership and settings NVS persistence into new `transmitter_settings_cache.{h,cpp}` module.
- Added `transmitter_settings_types.h` and moved settings structs (`BatterySettings`, `BatteryEmulatorSettings`, `PowerSettings`, `InverterSettings`, `CanSettings`, `ContactorSettings`) to this shared header.
- Updated `TransmitterManager` settings APIs to delegate to `TransmitterSettingsCache` while preserving signatures.
- Updated `TransmitterManager` NVS load/save paths to delegate settings persistence to cache helper functions.
- Preserved battery cell-count sync from spec payloads via `TransmitterSettingsCache::update_battery_cell_count(...)`.
- Removed obsolete `TransmitterManager` local mutex scaffolding (`data_mutex_`, `ScopedMutex`) left unused after prior extractions.

Legacy removed at completion:
- `TransmitterManager::SettingsCache`
- Settings parse/persist blocks in `TransmitterManager` NVS paths
- Obsolete mutex scaffolding in `TransmitterManager`

**Completion:** ✅ complete (2026-03-19)
**Implemented:** settings domain and persistence responsibilities now live in a dedicated cache module while `TransmitterManager` remains a stable facade.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` and `pio run -e receiver_lvgl -j 12` succeeded.

---

### Step 17 — MQTT cache extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Extracted MQTT cache ownership and MQTT NVS persistence into new `transmitter_mqtt_cache.{h,cpp}` module.
- Moved MQTT config/runtime state (`enabled/server/port/credentials/client-id/connected/config-known/version`) out of `TransmitterManager`.
- Updated `TransmitterManager` MQTT API methods and NVS load/save paths to delegate to `TransmitterMqttCache`.
- Preserved `TransmitterManager` public API signatures and runtime behavior.
- Added null-safe server logging fallback in `TransmitterManager::storeMqttConfig(...)`.

Legacy removed at completion:
- `TransmitterManager::MqttCache`
- MQTT persistence key constants and MQTT load/save logic from `TransmitterManager`

**Completion:** ✅ complete (2026-03-19)
**Implemented:** MQTT domain ownership now lives in a dedicated cache module while `TransmitterManager` remains a compatibility facade.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` and `pio run -e receiver_lvgl -j 12` succeeded.

---

### Step 18 — Network cache extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Extracted network cache ownership and network NVS persistence into new `transmitter_network_cache.{h,cpp}` module.
- Moved network state (`current/static IP/gateway/subnet`, DNS, `ip_known`, static-mode flag, network version) out of `TransmitterManager`.
- Updated `TransmitterManager` network API methods and NVS load/save paths to delegate to `TransmitterNetworkCache`.
- Preserved `TransmitterManager` signatures and behavior for network call sites.
- Kept notification/persistence orchestration in facade methods (`SSENotifier::notifyDataUpdated()`, `saveToNVS()`).

Legacy removed at completion:
- `TransmitterManager::NetworkCache`
- Network persistence key constants and network load/save logic from `TransmitterManager`

**Completion:** ✅ complete (2026-03-19)
**Implemented:** network domain ownership now lives in a dedicated cache module while `TransmitterManager` remains a compatibility facade.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` and `pio run -e receiver_lvgl -j 12` succeeded.

---

### Step 19 — Identity cache extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Extracted registered transmitter identity ownership into new `transmitter_identity_cache.{h,cpp}` module.
- Moved registered MAC state and MAC formatting helper out of `TransmitterManager`.
- Updated `TransmitterManager` MAC APIs to delegate to `TransmitterIdentityCache` while preserving behavior.
- Preserved fallback behavior in `TransmitterManager::getMAC()` to use ESPNow global transmitter MAC when local registered identity is unavailable.
- Preserved ESP-NOW peer registration flow in `registerMAC(...)` by using cache-provided registered MAC.

Legacy removed at completion:
- `TransmitterManager` direct registered-MAC state fields (`mac`, `mac_known`)

**Completion:** ✅ complete (2026-03-19)
**Implemented:** identity ownership now lives in a dedicated cache module while `TransmitterManager` remains a compatibility facade.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` and `pio run -e receiver_lvgl -j 12` succeeded.

---

### Step 20 — NVS persistence extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Extracted NVS persistence orchestration into new `transmitter_nvs_persistence.{h,cpp}` module.
- Moved debounced FreeRTOS timer ownership and timer callback out of `TransmitterManager`.
- Moved NVS load/save fan-out (`mqtt/network/status-metadata/settings`) out of `TransmitterManager`.
- Updated `TransmitterManager::{init,loadFromNVS,saveToNVS}` to delegate to `TransmitterNvsPersistence` while preserving public API signatures.

Legacy removed at completion:
- `TransmitterManager` local NVS timer/persistence internals (`nvs_save_timer_`, callback, schedule, immediate persist helpers)

**Completion:** ✅ complete (2026-03-19)
**Implemented:** NVS persistence lifecycle is now owned by a dedicated module while `TransmitterManager` remains the compatibility facade for callers.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` and `pio run -e receiver_lvgl -j 12` succeeded.

---

### Step 21 — ESP-NOW peer registry extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Extracted ESP-NOW peer registration into new `transmitter_peer_registry.{h,cpp}` module.
- Moved `esp_now_is_peer_exist(...)`/`esp_now_add_peer(...)` logic out of `TransmitterManager::registerMAC(...)`.
- Updated `TransmitterManager::registerMAC(...)` to delegate peer registration via `TransmitterPeerRegistry::ensure_peer_registered(...)`.
- Preserved identity registration, MAC logging, and SSE notification behavior in `registerMAC(...)`.

Legacy removed at completion:
- Direct ESP-NOW peer-add logic and ESP-NOW transport includes from `TransmitterManager`

**Completion:** ✅ complete (2026-03-19)
**Implemented:** transport-specific peer registration is now isolated in a dedicated helper module while `TransmitterManager` remains a compatibility facade.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` and `pio run -e receiver_lvgl -j 12` succeeded.

---

### Step 22 — Event-log type unification

**Addresses:** M1 (decomposition follow-through)

Changes:
- Added shared event-log type header `transmitter_event_log_types.h` with canonical `TransmitterEventLogTypes::EventLogEntry`.
- Updated `transmitter_event_log_cache.h` to alias its `EventLogEntry` to the shared type.
- Updated `TransmitterManager` public `EventLogEntry` to alias the same shared type.
- Simplified `TransmitterManager::getEventLogsSnapshot(...)` to direct delegation without per-entry conversion.

Legacy removed at completion:
- Duplicate event-log entry struct definitions across cache/facade
- Manual event-log snapshot conversion loop in `TransmitterManager`

**Completion:** ✅ complete (2026-03-19)
**Implemented:** event-log data contract is centralized in one type and facade glue code has been reduced.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` and `pio run -e receiver_lvgl -j 12` succeeded.

---

### Step 23 — MAC registration workflow extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Added `transmitter_mac_registration.{h,cpp}` to own MAC-registration workflow orchestration.
- Moved identity registration, log emission, SSE notification, and peer-registration chaining out of `TransmitterManager::registerMAC(...)`.
- Updated `TransmitterManager::registerMAC(...)` to delegate to `TransmitterMacRegistration::register_mac(...)`.
- Preserved the public facade API and all existing registration side effects.

Legacy removed at completion:
- Direct MAC-registration workflow orchestration in `TransmitterManager::registerMAC(...)`

**Completion:** ✅ complete (2026-03-19)
**Implemented:** MAC registration side effects now live in a dedicated helper while `TransmitterManager` stays a thinner compatibility facade.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` and `pio run -e receiver_lvgl -j 12` succeeded.

---

### Step 24 — Battery-spec sync workflow extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Added `transmitter_battery_spec_sync.{h,cpp}` to own the battery-spec storage + settings-sync workflow.
- Moved battery `number_of_cells` parsing and `TransmitterSettingsCache::update_battery_cell_count(...)` orchestration out of `TransmitterManager::storeBatterySpecs(...)`.
- Updated `TransmitterManager::storeBatterySpecs(...)` to delegate to `TransmitterBatterySpecSync::store_battery_specs(...)`.
- Preserved existing behavior and log output for battery cell-count synchronization.

Legacy removed at completion:
- Direct battery-spec/settings sync workflow logic in `TransmitterManager::storeBatterySpecs(...)`

**Completion:** ✅ complete (2026-03-19)
**Implemented:** battery-spec cross-domain synchronization now lives in a dedicated helper while `TransmitterManager` remains a thinner facade.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` and `pio run -e receiver_lvgl -j 12` succeeded.

---

### Step 25 — Write-through helper extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Added `transmitter_write_through.{h,cpp}` to centralize write-through side effects.
- Replaced repeated inline `notify + save` and `save` orchestration in `TransmitterManager` with helper delegation.
- Updated `TransmitterManager::saveToNVS()` to delegate through `TransmitterWriteThrough::persist_to_nvs()`.
- Preserved behavior for network/dashboard update paths and all write-through persistence paths.

Legacy removed at completion:
- Repeated inline write-through orchestration in `TransmitterManager`

**Completion:** ✅ complete (2026-03-19)
**Implemented:** write-through side effects are now centralized in a dedicated helper and facade repetition has been reduced.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` and `pio run -e receiver_lvgl -j 12` succeeded.

---

### Step 26 — Runtime-status workflow extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Added `transmitter_runtime_status_update.{h,cpp}` to own runtime-status update orchestration.
- Moved MQTT runtime connection update, Ethernet change detection, and conditional status logging out of `TransmitterManager::updateRuntimeStatus(...)`.
- Updated `TransmitterManager::updateRuntimeStatus(...)` to delegate to `TransmitterRuntimeStatusUpdate::update_runtime_status(...)`.
- Preserved existing behavior and logging semantics.

Legacy removed at completion:
- Direct runtime-status orchestration in `TransmitterManager::updateRuntimeStatus(...)`

**Completion:** ✅ complete (2026-03-19)
**Implemented:** runtime-status cross-cache coordination now lives in a dedicated helper while the facade remains thinner.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` and `pio run -e receiver_lvgl -j 12` succeeded.

---

### Step 27 — Active MAC resolver extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Added `transmitter_active_mac_resolver.{h,cpp}` to own active MAC resolution.
- Moved registered-MAC preference + ESPNow fallback scan out of `TransmitterManager::getMAC()`.
- Updated `TransmitterManager::getMAC()` to delegate to `TransmitterActiveMacResolver::get_active_mac()`.
- Preserved source-of-truth behavior and null-return semantics.

Legacy removed at completion:
- Direct active-MAC fallback scan and ESPNow global MAC dependency in `TransmitterManager`

**Completion:** ✅ complete (2026-03-19)
**Implemented:** active-MAC source resolution now lives in a focused helper while the facade remains thinner.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` and `pio run -e receiver_lvgl -j 12` succeeded.

---

### Step 28 — MQTT-config workflow extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Added `transmitter_mqtt_config_workflow.{h,cpp}` to own MQTT-config store orchestration.
- Moved MQTT config store + runtime-note semantics + null-safe server-IP log formatting out of `TransmitterManager::storeMqttConfig(...)`.
- Updated `TransmitterManager::storeMqttConfig(...)` to delegate to `TransmitterMqttConfigWorkflow::store_mqtt_config(...)`.
- Preserved write-through persistence and existing behavior/logging semantics.

Legacy removed at completion:
- Direct MQTT-config workflow orchestration in `TransmitterManager::storeMqttConfig(...)`

**Completion:** ✅ complete (2026-03-19)
**Implemented:** MQTT-config side-effect orchestration now lives in a focused helper while the facade remains thinner.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` and `pio run -e receiver_lvgl -j 12` succeeded.

---

### Step 29 — Network-store workflow extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Added `transmitter_network_store_workflow.{h,cpp}` to own network write workflows.
- Moved network-cache store + notify/persist orchestration out of `TransmitterManager::storeIPData(...)`.
- Moved full-network-config store + notify/persist orchestration out of `TransmitterManager::storeNetworkConfig(...)`.
- Updated both facade methods to delegate to the new helper.

Legacy removed at completion:
- Direct network write workflow orchestration in `TransmitterManager::storeIPData(...)` and `TransmitterManager::storeNetworkConfig(...)`

**Completion:** ✅ complete (2026-03-19)
**Implemented:** network write workflows now live in a focused helper while the facade remains thinner.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` (RAM 28.7%, Flash 19.2%) and `pio run -e receiver_lvgl -j 12` (RAM 28.2%, Flash 21.4%) succeeded.

---

### Step 30 — Settings-store workflow extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Added `transmitter_settings_store_workflow.{h,cpp}` to own settings write workflows.
- Moved settings-cache store + persist orchestration out of `TransmitterManager::storeBatterySettings(...)`.
- Moved settings-cache store + persist orchestration out of `TransmitterManager::storeBatteryEmulatorSettings(...)`.
- Moved settings-cache store + persist orchestration out of `TransmitterManager::storePowerSettings(...)`.
- Moved settings-cache store + persist orchestration out of `TransmitterManager::storeInverterSettings(...)`.
- Moved settings-cache store + persist orchestration out of `TransmitterManager::storeCanSettings(...)`.
- Moved settings-cache store + persist orchestration out of `TransmitterManager::storeContactorSettings(...)`.
- Updated all six facade methods to delegate to the new helper.

Legacy removed at completion:
- Direct settings write workflow orchestration in the six settings store methods listed above.

**Completion:** ✅ complete (2026-03-19)
**Implemented:** settings write workflows now live in a focused helper while the facade remains thinner.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` (RAM 28.7%, Flash 19.2%) and `pio run -e receiver_lvgl -j 12` (RAM 28.2%, Flash 21.4%) succeeded.

---

### Step 31 — Metadata-store workflow extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Added `transmitter_metadata_store_workflow.{h,cpp}` to own metadata write workflow.
- Moved metadata-store + persist orchestration out of `TransmitterManager::storeMetadata(...)`.
- Updated the facade method to delegate to the new helper.

Legacy removed at completion:
- Direct metadata write workflow orchestration in `TransmitterManager::storeMetadata(...)`.

**Completion:** ✅ complete (2026-03-19)
**Implemented:** metadata write workflow now lives in a focused helper while the facade remains thinner.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` (RAM 28.7%, Flash 19.2%) and `pio run -e receiver_lvgl -j 12` (RAM 28.2%, Flash 21.4%) succeeded.

---

### Step 32 — Connection-state resolver extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Added `transmitter_connection_state_resolver.{h,cpp}` to own transmitter connection-state resolution.
- Moved inline connection-state composition logic out of `TransmitterManager::isTransmitterConnected()`.
- Updated the facade method to delegate to the new helper.

Legacy removed at completion:
- Inline connection-state composition in `TransmitterManager::isTransmitterConnected()`.

**Completion:** ✅ complete (2026-03-19)
**Implemented:** connection-state resolution now lives in a focused helper while the facade remains thinner.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` (RAM 28.7%, Flash 19.2%) and `pio run -e receiver_lvgl -j 12` (RAM 28.2%, Flash 21.4%) succeeded.

---

### Step 33 — MAC-query helper extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Added `transmitter_mac_query_helper.{h,cpp}` to own MAC identity query composition.
- Updated `TransmitterManager::getMAC()` to delegate to the helper.
- Updated `TransmitterManager::isMACKnown()` to delegate to the helper.
- Updated `TransmitterManager::getMACString()` to delegate to the helper.

Legacy removed at completion:
- Inline MAC known-state and MAC string composition logic in `TransmitterManager`.

**Completion:** ✅ complete (2026-03-19)
**Implemented:** MAC identity query composition now lives in a focused helper while the facade remains thinner.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` (RAM 28.7%, Flash 19.2%) and `pio run -e receiver_lvgl -j 12` (RAM 28.2%, Flash 21.4%) succeeded.

---

### Step 34 — Time-status update workflow extraction

**Addresses:** M1 (decomposition follow-through)

Changes:
- Added `transmitter_time_status_update_workflow.{h,cpp}` to own time/status runtime update workflow.
- Moved inline time-data update delegation out of `TransmitterManager::updateTimeData(...)`.
- Moved inline send-status update delegation out of `TransmitterManager::updateSendStatus(...)`.
- Updated both facade methods to delegate to the new helper.

Legacy removed at completion:
- Inline time/status runtime update composition in `TransmitterManager::updateTimeData(...)` and `TransmitterManager::updateSendStatus(...)`.

**Completion:** ✅ complete (2026-03-19)
**Implemented:** time/status runtime update workflow now lives in a focused helper while the facade remains thinner.
**Build validation:** ✅ `pio run -e receiver_tft -j 12` (RAM 28.7%, Flash 19.2%) and `pio run -e receiver_lvgl -j 12` (RAM 28.2%, Flash 21.4%) succeeded.

---

## 5. Step Tracking

Status: ✅ complete | 🔄 in progress | ⏳ pending

| Step | Status | Primary files | Legacy removed | Defects/issues |
|------|--------|--------------|----------------|----------------|
| 1. Quick defect fixes | ✅ | `api_settings_handlers.cpp`, `api_modular_handlers.cpp`, `webserver.cpp` | Serial.* calls; hardcoded URL list; duplicate LOG_INFO | D1, D3, D5 |
| 2. Cell JSON extraction | ✅ | `telemetry_snapshot_utils.h`, `api_telemetry_handlers.cpp`, `api_sse_handlers.cpp` | Inline JSON build blocks; duplicate "mode" field | D2 |
| 3. debug_page rewrite | ✅ | `debug_page.cpp` | `debug_page_processor()`; all `html +=` concatenation | D4 |
| 4. Nav data-driven | ✅ | `page_definitions.h/.cpp`, `nav_buttons.cpp` | Hardcoded URI strcmp comparisons | M3 |
| 5. Dead endpoints + CSS | ✅ | `api_telemetry_handlers.*`, `api_settings_handlers.*`, `api_handlers.cpp`, `common_styles.h`, page JS | Dead handler functions + table entries + JS calls; duplicate CSS class | M2, M4 |
| 6. JSON standardization | ✅ | `api_telemetry_handlers.cpp`, `api_type_selection_handlers.cpp`, `api_response_utils.h` | Replaced snprintf blocks; new[]/delete[] sort buffer | M7, M9 |
| 7.1 RuntimeStatusCache | ✅ | `transmitter_manager.h/.cpp` | 6 static members consolidated into struct | M1 (25%) |
| 7.2 NetworkCache | ✅ | `transmitter_manager.h/.cpp` | 11 static members consolidated into struct | M1 (50%) |
| 7.3a MqttCache | ✅ | `transmitter_manager.h/.cpp` | 9 MQTT static members consolidated into struct | M1 (62.5%) |
| 7.3b SettingsCache | ✅ | `transmitter_manager.h/.cpp` | 12 remaining settings static members consolidated into struct | M1 (75%) |
| 7.4 SpecCache | ✅ | `transmitter_manager.h/.cpp` | 6 static spec members consolidated into struct | M1 (100%) |
| 8. MAC source-of-truth unify | ✅ | `transmitter_manager.cpp`, `api_control_handlers.cpp` | Reboot handler dual-source MAC fallback branch | M5 |
| 9. Dashboard env formatting dedup | ✅ | `dashboard_page.cpp` | C++ `capitalizeWords()` helper and call sites | M6 |
| 10. Webserver metrics extraction | ✅ | `api_telemetry_handlers.cpp`, `webserver_metrics.h/.cpp` | Telemetry-file-local metrics globals/mux/timer | M8 |
| 11. Status/metadata delegation | ✅ | `transmitter_manager.h/.cpp`, `transmitter_status_cache.h/.cpp` | Duplicate runtime/metadata ownership in `TransmitterManager` | M1 |
| 12. Status cache API cleanup | ✅ | `transmitter_status_cache.h/.cpp`, `transmitter_manager.cpp` | Unused delegated runtime parameter (`mqtt_conn`) | M1 |
| 13. Cell data cache extraction | ✅ | `cell_data_cache.h/.cpp`, `transmitter_manager.h/.cpp`, `mqtt_client.cpp`, `api_telemetry_handlers.cpp`, `api_sse_handlers.cpp` | Cell data ownership and snapshot API in `TransmitterManager` | M1 |
| 14. Spec cache extraction | ✅ | `transmitter_spec_cache.h/.cpp`, `transmitter_manager.h/.cpp` | Spec JSON ownership and serialization logic in `TransmitterManager` | M1 |
| 15. Event log cache extraction | ✅ | `transmitter_event_log_cache.h/.cpp`, `transmitter_manager.h/.cpp` | Event-log state and parsing logic in `TransmitterManager` | M1 |
| 16. Settings cache extraction | ✅ | `transmitter_settings_types.h`, `transmitter_settings_cache.h/.cpp`, `transmitter_manager.h/.cpp` | Settings ownership/persistence and obsolete mutex scaffolding in `TransmitterManager` | M1 |
| 17. MQTT cache extraction | ✅ | `transmitter_mqtt_cache.h/.cpp`, `transmitter_manager.h/.cpp` | MQTT ownership/persistence state and logic in `TransmitterManager` | M1 |
| 18. Network cache extraction | ✅ | `transmitter_network_cache.h/.cpp`, `transmitter_manager.h/.cpp` | Network ownership/persistence state and logic in `TransmitterManager` | M1 |
| 19. Identity cache extraction | ✅ | `transmitter_identity_cache.h/.cpp`, `transmitter_manager.h/.cpp` | Direct registered-MAC ownership fields in `TransmitterManager` | M1 |
| 20. NVS persistence extraction | ✅ | `transmitter_nvs_persistence.h/.cpp`, `transmitter_manager.h/.cpp` | Timer/debounce/persistence internals in `TransmitterManager` | M1 |
| 21. ESP-NOW peer registry extraction | ✅ | `transmitter_peer_registry.h/.cpp`, `transmitter_manager.cpp` | Direct ESP-NOW peer registration logic in `TransmitterManager` | M1 |
| 22. Event-log type unification | ✅ | `transmitter_event_log_types.h`, `transmitter_event_log_cache.h`, `transmitter_manager.h/.cpp` | Duplicate event-log type definitions and conversion loop in `TransmitterManager` | M1 |
| 23. MAC registration workflow extraction | ✅ | `transmitter_mac_registration.h/.cpp`, `transmitter_manager.cpp` | Direct MAC-registration orchestration in `TransmitterManager::registerMAC(...)` | M1 |
| 24. Battery-spec sync workflow extraction | ✅ | `transmitter_battery_spec_sync.h/.cpp`, `transmitter_manager.cpp` | Direct battery-spec/settings sync orchestration in `TransmitterManager::storeBatterySpecs(...)` | M1 |
| 25. Write-through helper extraction | ✅ | `transmitter_write_through.h/.cpp`, `transmitter_manager.cpp` | Repeated inline notify/save orchestration in `TransmitterManager` | M1 |
| 26. Runtime-status workflow extraction | ✅ | `transmitter_runtime_status_update.h/.cpp`, `transmitter_manager.cpp` | Direct runtime-status update orchestration in `TransmitterManager::updateRuntimeStatus(...)` | M1 |
| 27. Active MAC resolver extraction | ✅ | `transmitter_active_mac_resolver.h/.cpp`, `transmitter_manager.cpp` | Direct active-MAC fallback scan and ESPNow dependency in `TransmitterManager::getMAC()` | M1 |
| 28. MQTT-config workflow extraction | ✅ | `transmitter_mqtt_config_workflow.h/.cpp`, `transmitter_manager.cpp` | Direct MQTT-config workflow orchestration in `TransmitterManager::storeMqttConfig(...)` | M1 |
| 29. Network-store workflow extraction | ✅ | `transmitter_network_store_workflow.h/.cpp`, `transmitter_manager.cpp` | Direct network write workflow orchestration in `TransmitterManager::storeIPData(...)` and `storeNetworkConfig(...)` | M1 |
| 30. Settings-store workflow extraction | ✅ | `transmitter_settings_store_workflow.h/.cpp`, `transmitter_manager.cpp` | Direct settings write workflow orchestration in six `TransmitterManager` settings store methods | M1 |
| 31. Metadata-store workflow extraction | ✅ | `transmitter_metadata_store_workflow.h/.cpp`, `transmitter_manager.cpp` | Direct metadata write workflow orchestration in `TransmitterManager::storeMetadata(...)` | M1 |
| 32. Connection-state resolver extraction | ✅ | `transmitter_connection_state_resolver.h/.cpp`, `transmitter_manager.cpp` | Inline connection-state composition in `TransmitterManager::isTransmitterConnected()` | M1 |
| 33. MAC-query helper extraction | ✅ | `transmitter_mac_query_helper.h/.cpp`, `transmitter_manager.cpp` | Inline MAC known-state and MAC string composition in `TransmitterManager` | M1 |
| 34. Time-status update workflow extraction | ✅ | `transmitter_time_status_update_workflow.h/.cpp`, `transmitter_manager.cpp` | Inline time/status runtime update composition in `TransmitterManager::updateTimeData(...)` and `updateSendStatus(...)` | M1 |
| 42. Logging standardization (workflow helpers) | ✅ | `utils/sse_notifier.cpp`, `utils/transmitter_*_workflow.cpp`, `utils/transmitter_peer_registry.cpp`, `utils/transmitter_nvs_persistence.cpp` | Direct `Serial.println/printf` calls in helper/workflow modules | Functional review pending item |
| 43. Logging standardization (cache + API network) | ✅ | `utils/{cell_data,transmitter_network,transmitter_event_log,transmitter_spec,transmitter_status}_cache.cpp`, `api/api_network_handlers.cpp` | Remaining direct `Serial.println/printf` calls in cache modules and network API handlers | Functional review pending item |

---

## 6. Files Reviewed

| File | Lines | Role |
|------|-------|------|
| `webserver.cpp` | 260 | Server init, handler registration |
| `webserver.h` | 67 | Public API |
| `page_definitions.cpp/.h` | 90 | Page registry (19 entries; `PAGE_COUNT` derived correctly) |
| `common/page_generator.cpp` | 385 | Shared JS helpers + `renderPage()` |
| `common/page_generator.h` | 20 | `PageRenderOptions`, `renderPage` |
| `common/nav_buttons.cpp/.h` | 30 | Nav button generator |
| `common/common_styles.h` | 188 | Shared CSS |
| `utils/transmitter_manager.h` | 341 | God class — 15-domain cache facade |
| `utils/transmitter_manager.cpp` | 1265 | Cache implementation + NVS persistence |
| `utils/sse_notifier.h/.cpp` | ~50 | SSE event group wrapper |
| `utils/telemetry_snapshot_utils.h` | 35 | Shared telemetry helper |
| `api/api_handlers.cpp/.h` | 100 | Handler registration table |
| `api/api_request_utils.h` | 120 | Request parsing helpers |
| `api/api_response_utils.cpp/.h` | 65 | Response helpers |
| `api/api_telemetry_handlers.cpp` | 713 | Data/version/metrics handlers |
| `api/api_network_handlers.cpp` | 408 | Network config handlers |
| `api/api_settings_handlers.cpp` | 215 | Battery settings + dead set_data_source stub |
| `api/api_control_handlers.cpp` | 278 | Reboot + OTA upload proxy |
| `api/api_sse_handlers.cpp` | 273 | SSE stream handlers |
| `api/api_modular_handlers.cpp` | 110 | Test mode + event log sub |
| `api/api_led_handlers.cpp` | 110 | LED status + resync |
| `api/api_type_selection_handlers.cpp` | 368 | Component apply + type catalogue |
| `pages/dashboard_page.cpp` | 652 | Dashboard |
| `pages/battery_settings_page.cpp` | 569 | Battery settings |
| `pages/inverter_settings_page.cpp` | 298 | Inverter settings |
| `pages/monitor_page.cpp` | 115 | Polling monitor |
| `pages/network_config_page.cpp` | 696 | Receiver network config |
| `pages/debug_page.cpp` | 130 | Debug level — non-conforming pattern (Step 3) |
| `pages/ota_page.cpp` | 575 | OTA upload |

---

## 7. Post-Plan Completions (Steps 35–43 + Dead-Include Cleanup)

These steps were completed after the original 34-step plan was fully executed.

| Step | Status | Files Created/Modified | What was done |
|------|--------|------------------------|---------------|
| 35. Status query helper | ✅ | `transmitter_status_query_helper.h/.cpp`, `transmitter_manager.cpp` | Extracted Ethernet/beacon/uptime/time-source query methods (6 functions) — replaces `TransmitterStatusCache::` direct calls |
| 36. Spec storage workflow | ✅ | `transmitter_spec_storage_workflow.h/.cpp`, `transmitter_manager.cpp` | Extracted battery/charger/inverter/system spec store/get/has (11 functions) — replaces `TransmitterSpecCache::` direct calls |
| 37. Event logs workflow | ✅ | `transmitter_event_logs_workflow.h/.cpp`, `transmitter_manager.cpp` | Extracted event log store/query methods (5 functions) — replaces `TransmitterEventLogCache::` direct calls |
| 38. Metadata query helper | ✅ | `transmitter_metadata_query_helper.h/.cpp`, `transmitter_manager.cpp` | Extracted metadata validity/env/device/version query methods (7 functions) — replaces direct identity/metadata cache calls |
| 39. Network query helper | ✅ | `transmitter_network_query_helper.h/.cpp`, `transmitter_manager.cpp` | Extracted IP/gateway/subnet/DNS/port query methods (14 functions) — replaces `TransmitterNetworkCache::` direct calls |
| 40. Settings query helper | ✅ | `transmitter_settings_query_helper.h/.cpp`, `transmitter_manager.cpp` | Extracted get/has methods for all 6 settings types (12 functions) — replaces `TransmitterSettingsCache::` direct calls |
| 41. MQTT query helper | ✅ | `transmitter_mqtt_query_helper.h/.cpp`, `transmitter_manager.cpp` | Extracted MQTT server/port/credentials/status query methods (10 functions) — replaces `TransmitterMqttCache::` direct calls |
| 42. Logging standardization (workflow/helpers) | ✅ | `sse_notifier.cpp`, `transmitter_nvs_persistence.cpp`, `transmitter_mac_registration.cpp`, `transmitter_mqtt_config_workflow.cpp`, `transmitter_runtime_status_update.cpp`, `transmitter_settings_store_workflow.cpp`, `transmitter_peer_registry.cpp`, `transmitter_battery_spec_sync.cpp` | Replaced direct `Serial` logging in helper/workflow modules with shared `LOG_*` macros from `lib/webserver/logging.h` |
| 43. Logging standardization (cache + API network) | ✅ | `cell_data_cache.cpp`, `transmitter_network_cache.cpp`, `transmitter_event_log_cache.cpp`, `transmitter_spec_cache.cpp`, `transmitter_status_cache.cpp`, `api_network_handlers.cpp` | Replaced remaining direct `Serial` logging with shared `LOG_*` macros and removed duplicate Serial debug prints in network API handlers |

### Dead-Include Cleanup (post Step 41) ✅

After Steps 35–41 eliminated all direct cache namespace calls from `transmitter_manager.cpp`, the following 9 `#include` lines were confirmed dead by namespace-use grep and removed:

```cpp
// REMOVED — no longer referenced directly in transmitter_manager.cpp
#include "transmitter_status_cache.h"
#include "cell_data_cache.h"
#include "transmitter_spec_cache.h"
#include "transmitter_event_log_cache.h"
#include "transmitter_settings_cache.h"
#include "transmitter_mqtt_cache.h"
#include "transmitter_network_cache.h"
#include "transmitter_identity_cache.h"
#include "transmitter_battery_spec_sync.h"
```

**Build validation**: Both `receiver_tft` ✅ and `receiver_lvgl` ✅ — SUCCESS with no new warnings.
- RAM: 28.7% / 28.2% — unchanged
- Flash: 19.2% / 21.4% — unchanged

`transmitter_manager.cpp` is now a pure delegation facade with zero direct cache namespace calls.

### Step 42 completion note ✅

- Standardized helper/workflow logging in transmitter webserver utility modules by replacing direct `Serial.println/printf` calls with shared `LOG_INFO/LOG_ERROR` macros.
- Added `../logging.h` includes in the migrated modules and preserved existing log message content/tags.
- This advances the receiver functional-review deferred item for logging-style consistency while keeping behavior unchanged.

**Build validation**: Both `receiver_tft` ✅ and `receiver_lvgl` ✅ — SUCCESS.

### Step 43 completion note ✅

- Standardized logging in core cache modules by replacing the remaining direct `Serial.println/printf` calls with `LOG_WARN/LOG_INFO/LOG_ERROR` macros from `lib/webserver/logging.h`.
- Removed duplicate Serial debug prints in `api_network_handlers.cpp` and retained the existing structured `LOG_INFO` traces.
- This further advances the deferred logging-style consistency item from the receiver functional review while preserving runtime behavior.

**Build validation**: Both `receiver_tft` ✅ and `receiver_lvgl` ✅ — SUCCESS (RAM/Flash unchanged from baseline).
