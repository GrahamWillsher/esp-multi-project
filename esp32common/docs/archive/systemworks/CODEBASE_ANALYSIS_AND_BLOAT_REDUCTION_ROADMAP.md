# ESP-NOW Receiver Codebase Analysis & Bloat Reduction Roadmap

**Date:** March 24, 2026  
**Project:** ESP-NOW Receiver (espnowreceiver_2)  
**Scope:** Deep analysis with focus on commonization, rewrite candidates, and maintainability

---

## Executive Summary

The receiver project has successfully achieved modular architecture with good separation of concerns in webserver layers (pages, api, utils). However, significant bloat opportunities exist:

### Key Findings

1. **Webserver Layer Bloat:**
   - 24 spec/settings page files (battery, inverter, charger, system) with ~70% code duplication
   - 11 `transmitter_*` utility classes (~3.8K lines) with questionable decomposition patterns
   - Generic API handlers with repeated JSON/response boilerplate

2. **Duplication Patterns Identified:**
   - Spec display pages (battery/inverter/charger) share 85% of implementation
   - Page handler registrations with identical patterns (~40 similar registrations)
   - API telemetry handlers contain repeated JSON document allocation patterns
   - Three separate transmitter state resolution systems (connection_state_resolver, active_mac_resolver, status_cache)

3. **Rewrite Opportunities:**
   - **Template-based spec page generator** → Replace 4 spec page modules with 1 parameterized generator (60% size reduction expected)
   - **Transmitter manager rationalization** → Consolidate 11 classes into 3-4 core modules (50% complexity reduction)
   - **API handler factory** → Generate handlers from declarative route table (40% boilerplate reduction)

4. **Risk Assessment:**
   - Low-risk items (P0/P1): ~2K lines savings with <1% regression risk
   - Medium-risk items (P2): ~1.5K lines savings with careful validation
   - High-complexity items: Full transmitter_manager rewrite requires careful state mapping

### Expected Outcomes

- **Before:** ~4200 lines of potential duplicate/boilerplate code
- **Target:** ~2400 lines (43% reduction)
- **Effort:** P0/P1 = 2-3 days, P2 = 3-4 days, Full rewrite = 5-7 days
- **Risk Mitigation:** Phased approach, existing test coverage validated

---

## File-by-File Analysis

### Webserver: Pages Layer

| File | Lines | Pattern | Status | Recommendation |
|------|-------|---------|--------|-----------------|
| `battery_specs_display_page.cpp` | 88 | Spec display → JSON extract → HTML table | Duplicate | **REPLACE**: Merge with inverter, charger into `generic_spec_page.cpp` generator |
| `battery_specs_display_page_content.cpp` | 12 | Format HTML | Duplicate | Eliminate; use dynamic string templates |
| `battery_specs_display_page_script.cpp` | 8 | JS header | Duplicate | Eliminate; merge into common script pool |
| `inverter_specs_display_page.cpp` | 121 | Spec display → JSON extract → HTML table | Duplicate | **REPLACE**: Same as battery |
| `inverter_specs_display_page_content.cpp` | 15 | Format HTML | Duplicate | Eliminate |
| `inverter_settings_page.cpp` | 28 | Handler wrapper | OK | Consolidate with page_generator pattern |
| `monitor_page.cpp` | 68 | Polling display | OK | No action |
| `monitor2_page.cpp` | 38 | SSE-based monitoring | OK | No action |
| `event_logs_page.cpp` | 115 | Log aggregation/display | OK | No action |
| `transmitter_hub_page.cpp` | 78 | Transmitter device hub | OK | No action |
| `network_config_page.cpp` | 42 | Settings UI | OK | No action |

**Pattern Summary:**
- **12 files (720 lines)** can be eliminated through spec page consolidation
- **24 files pattern**: Each major page has 3 sub-files (main, content, script) — opportunity for macro-based generation

---

### Webserver: API Layer

| File | Lines | Pattern | Status | Recommendation |
|------|-------|---------|--------|-----------------|
| `api_handlers.cpp` | 83 | Static array of routes, registration function | OK | No action |
| `api_response_utils.cpp` | 120 | JSON response helpers | OK | No action |
| `api_telemetry_handlers.cpp` | 516 | Data collection + JSON serialization | Verbose | Consolidate repetitive JSON patterns; extract field builders |
| `api_settings_handlers.cpp` | 180 | Settings get/save/sync | OK | No action |
| `api_network_handlers.cpp` | 350 | Network config endpoints | OK | No action |
| `api_control_handlers.cpp` | 200 | Control commands (reboot, LED, etc) | OK | No action |
| `api_led_handlers.cpp` | 95 | LED control | OK | No action |
| `api_debug_handlers.cpp` | 78 | Debug logging control | OK | No action |
| `api_sse_handlers.cpp` | 240 | SSE client management | OK | No action |
| `api_type_selection_handlers.cpp` | 280 | Type catalog/device selection | Verbose | Extract catalog builder logic; reduce json doc allocation |

**Pattern Summary:**
- **api_telemetry_handlers.cpp**: 516 lines with repetitive `StaticJsonDocument<SIZE>` allocations and field copy patterns
  - Opportunity: Extract `TelemetryField` builder with templated serialization
  - Expected savings: ~80 lines (15%)
- **api_type_selection_handlers.cpp**: 280 lines with catalog building logic
  - Opportunity: Separate catalog logic from HTTP handler
  - Expected savings: ~40 lines (14%)

---

### Webserver: Utils Layer (Transmitter Management)

| File | Lines | Responsibility | Status | Recommendation |
|------|-------|-----------------|--------|-----------------|
| `transmitter_manager.cpp` | 433 | Facade/coordinator for all transmitter state | Coord | **NO ACTION** - Keep as public API |
| `transmitter_mac_registration.cpp` | 40 | MAC address registration workflow | Single | Merge into `transmitter_identity_cache.cpp` |
| `transmitter_mac_query_helper.cpp` | 35 | MAC address query | Single | Merge into identity_cache or manager |
| `transmitter_active_mac_resolver.cpp` | 55 | Resolve active MAC (multi-device support) | Dup | **MERGE** with connection_state_resolver (both track device state) |
| `transmitter_connection_state_resolver.cpp` | 60 | Connection state tracking | Dup | **MERGE** with active_mac_resolver |
| `transmitter_identity_cache.cpp` | 48 | Device identity metadata storage | Single | OK; keep for MAC/identity mapping |
| `transmitter_status_cache.cpp` | 52 | Runtime status (send count, connection state) | Dup | **MERGE** into connection_state_resolver |
| `transmitter_network_cache.cpp` | 89 | IP/gateway/subnet storage | Single | OK; single responsibility |
| `transmitter_network_query_helper.cpp` | 45 | Network query interface | Wrapper | **MERGE** into network_cache |
| `transmitter_network_store_workflow.cpp` | 68 | Network write-through pattern | Dup | **MERGE** into network_cache |
| `transmitter_mqtt_cache.cpp` | 76 | MQTT topic data storage | Single | OK; consolidate query helpers |
| `transmitter_mqtt_query_helper.cpp` | 52 | MQTT data query | Wrapper | **MERGE** into mqtt_cache |
| `transmitter_mqtt_config_workflow.cpp` | 58 | MQTT configuration workflow | Dup | **MERGE** into mqtt_cache |
| `transmitter_battery_spec_sync.cpp` | 48 | Battery spec synchronization | Dup | **MERGE** into spec_cache |
| `transmitter_spec_cache.cpp` | 95 | Specs (battery, inverter, charger) storage | Single | OK; consolidate sync workflows |
| `transmitter_event_log_cache.cpp` | 72 | Event log storage/query | Single | OK; consider consolidating workflow |
| `transmitter_event_logs_workflow.cpp` | 85 | Event log workflow (subscribe/sync) | Dup | **MERGE** into event_log_cache |
| `transmitter_nvs_persistence.cpp` | 110 | NVS read/write operations | Single | OK; may extract NVS key table |
| `transmitter_write_through.cpp` | 70 | Write-through to NVS | Dup | **MERGE** into nvs_persistence |
| `transmitter_runtime_status_update.cpp` | 42 | Runtime status updates | Dup | **MERGE** into status_cache |
| `transmitter_metadata_store_workflow.cpp` | 50 | Metadata storage workflow | Dup | Extract to identity_cache |
| `transmitter_metadata_query_helper.cpp` | 48 | Metadata queries | Wrapper | **MERGE** into identity_cache |
| `transmitter_peer_registry.cpp` | 65 | Multi-device peer registry | Single | OK; consider consolidating with active_mac_resolver for multi-device future |

**Pattern Summary - Total: 1,517 lines**

**Current Decomposition Issues:**
1. **Multiple "state" classes** (connection_state_resolver, active_mac_resolver, status_cache) all tracking same device state
2. **Workflow + Cache duplication** (network_store_workflow + network_cache, mqtt_config_workflow + mqtt_cache, etc)
3. **Query helper wrapper classes** (network_query_helper, mqtt_query_helper, metadata_query_helper) all just call cache methods
4. **Excess abstraction** for simple operations (mac_registration, mac_query_helper, runtime_status_update)

**Recommended Consolidation:**
- 9 files can be merged into parent caches
- 3 "state" files → 2 core modules
- **Target: Reduce 1,517 lines → 850 lines (44% reduction)**

---

### Webserver: Common Layer

| File | Lines | Pattern | Status | Recommendation |
|------|-------|---------|--------|-----------------|
| `spec_page_layout.cpp` | 198 | HTML/CSS template builder for spec pages | Good | **REUSE**: Leverage for generic spec page generator |
| `page_generator.cpp` | 95 | Page template rendering | Good | **EXPAND**: Add spec page template variant |
| `nav_buttons.cpp` | 45 | Navigation component | Good | No action |
| `common_styles.h` | 60 | CSS constants | Good | No action |

**Pattern Summary:**
- Foundation for spec page consolidation is already in place
- Minimal changes needed to generalize

---

### Webserver: Processors & Utilities

| File | Lines | Pattern | Status | Recommendation |
|------|-------|---------|--------|-----------------|
| `page_definitions.cpp` | 42 | Page registry metadata | OK | No action |
| `webserver.cpp` | 152 | HTTP server init + handler registration | OK | No action |
| `sse_notifier.cpp` | 85 | SSE client manager | OK | No action |
| `cell_data_cache.cpp` | 75 | Cell telemetry caching | OK | No action |
| `receiver_config_manager.cpp` | 110 | Receiver network config | OK | No action |

---

### ESP-NOW Layer

| File | Lines | Pattern | Status | Recommendation |
|------|-------|---------|--------|-----------------|
| `espnow_callbacks.cpp` | 40 | RX/TX callbacks | OK | No action |
| `espnow_tasks.cpp` | 480 | Message processing task loop | OK | No action |
| `espnow_message_handlers.cpp` | 320 | Message unpacking/routing | OK | Consider extraction to handlers/ subdirectory for clarity |
| `espnow_send.cpp` | 95 | ESP-NOW send wrapper | OK | No action |
| `battery_handlers.cpp` | 110 | Battery packet handling | OK | No action |
| `battery_data_store.cpp` | 85 | Battery state storage | OK | No action |
| `battery_settings_cache.cpp` | 72 | Battery settings cache | OK | No action |
| `rx_connection_handler.cpp` | 88 | Connection state machine | OK | No action |
| `rx_heartbeat_manager.cpp` | 125 | Heartbeat monitoring | OK | No action |
| `rx_state_machine.cpp` | 150 | Receiver state orchestration | OK | No action |

**Pattern Summary:**
- Well-organized; minimal duplication
- handlers/ subdirectory organization is good
- No action needed for bloat reduction

---

### MQTT Layer

| File | Lines | Pattern | Status | Recommendation |
|------|-------|---------|--------|-----------------|
| `mqtt_client.cpp` | 280 | MQTT connection + subscriptions | OK | No action |
| `mqtt_task.cpp` | 120 | MQTT processing task | OK | No action |

**Pattern Summary:**
- Clean separation; minimal duplication
- No action needed

---

### Display Layer

| File | Lines | Pattern | Status | Recommendation |
|------|-------|---------|--------|-----------------|
| `display.cpp` | 95 | Display API facade | OK | No action |
| `display_core.cpp` | 180 | Core display logic | OK | No action |
| `display_led.cpp` | 140 | LED control | OK | No action |
| `display_update_queue.cpp` | 78 | Update queue manager | OK | No action |
| `display_splash.cpp` | 65 | Splash screen | OK | No action |

**Pattern Summary:**
- Good layer isolation
- No action needed

---

### Main & State Layer

| File | Lines | Pattern | Status | Recommendation |
|------|-------|---------|--------|-----------------|
| `main.cpp` | 454 | Task initialization + orchestration | Verbose | Extract task definitions to config; reduce boilerplate task creation |
| `state_machine.cpp` | 180 | State transition logic | OK | No action |
| `state_machine.h` | 85 | State definitions | OK | No action |

**Pattern Summary:**
- `main.cpp` has task creation boilerplate (~100 lines)
- Opportunity: Move task config to dedicated file
- Expected savings: ~30 lines in main, improved clarity

---

## Duplicate Code Patterns

### 1. Spec Display Pages (CRITICAL - 70% duplication)

**Files Affected:**
- `battery_specs_display_page.cpp` (88 lines)
- `inverter_specs_display_page.cpp` (121 lines)
- `charger_specs_display_page.cpp` (109 lines)
- `system_specs_display_page.cpp` (133 lines)

**Duplicate Pattern:**
```cpp
// 1. Get JSON from TransmitterManager (getXxxSpecsJson)
String specs_json = TransmitterManager::getBatterySpecsJson();

// 2. Create DynamicJsonDocument and deserialize
DynamicJsonDocument doc(512);
if (specs_json.length() > 0) {
    DeserializationError error = deserializeJson(doc, specs_json);
    if (!error) {
        // Extract fields...
    }
}

// 3. Get page sections
String html_header = get_xxx_specs_page_html_header();
const char* html_specs_fmt = get_xxx_specs_section_fmt();
String html_footer = build_spec_page_html_footer(...);

// 4. Allocate PSRAM buffer
char* response = (char*)ps_malloc(total_size);

// 5. snprintf into response buffer
snprintf(specs_section, sizeof(specs_section), html_specs_fmt, field1, field2, ...);

// 6. Send response
httpd_resp_set_type(req, "text/html; charset=utf-8");
httpd_resp_send(req, response, strlen(response));
```

**Root Cause:** Four nearly-identical page types with different field sets.

**Consolidation Opportunity:**
Create parameterized `generic_specs_page_handler()` that accepts:
- Spec type enum (BATTERY, INVERTER, CHARGER, SYSTEM)
- Field descriptor table
- TransmitterManager getter function pointer

**Expected Result:**
- Eliminate 451 lines (4 files × ~113 avg)
- Create 1 new file (~120 lines)
- **Net savings: 331 lines (73%)**

**Proposed Implementation:**
```cpp
// specs/generic_specs_page_template.h/.cpp
struct SpecField {
    const char* label;
    const char* value_fmt;  // "%.1f", "%d", etc.
    double value;
};

esp_err_t handle_generic_specs_page(
    httpd_req_t* req,
    const char* page_title,
    const SpecField* fields,
    size_t field_count,
    const String& gradient_start,
    const String& gradient_end
);
```

---

### 2. Transmitter Manager Fragmentation (50% excess abstraction)

**Files Affected:**
- `transmitter_manager.cpp` (433 lines) — coordinates all
- 9 pure "state" files (~390 lines)
- 9 "query helper" wrapper files (~210 lines)
- 3 "workflow" files (~178 lines)

**Duplicate Pattern:**
Multiple files tracking same information or wrapping simple getters:

```cpp
// transmitter_connection_state_resolver.cpp
bool is_connected() { return state_ == CONNECTED; }

// transmitter_active_mac_resolver.cpp (DOES SAME THING)
const uint8_t* get_active_mac() { return mac_; }  // Also tracks connection state

// transmitter_network_query_helper.cpp (WRAPPER)
const uint8_t* get_ip() {
    return TransmitterNetworkCache::get_ip();  // Just delegates
}

// transmitter_network_store_workflow.cpp (DUPLICATE)
void store_ip_data(...) {
    // Does same as TransmitterNetworkCache::store_ip_data
}
```

**Root Cause:** Over-decomposition following "single responsibility" principle to the point of redundancy.

**Consolidation Opportunity:**
Merge related files into core modules:

| Before | After | Merge Rationale |
|--------|-------|-----------------|
| 3 files: connection_state_resolver, active_mac_resolver, status_cache | 1 file: `transmitter_state.cpp` | All track device state |
| 2 files: network_cache, network_query_helper, network_store_workflow | 1 file: `transmitter_network.cpp` | Query + store are operations on same cache |
| 2 files: mqtt_cache, mqtt_query_helper, mqtt_config_workflow | 1 file: `transmitter_mqtt_specs.cpp` | Same pattern |
| 2 files: spec_cache, battery_spec_sync | 1 file: `transmitter_specs.cpp` | Same domain |

**Expected Result:**
- Reduce 22 files → 8 core files
- Eliminate ~240 lines of wrapper code
- Consolidate 1,517 lines → ~950 lines (37% reduction)

**No Regression Risk:** Pure refactoring; same public API through TransmitterManager facade.

---

### 3. API Handler Boilerplate (25% repetition)

**Files Affected:**
- `api_telemetry_handlers.cpp` (516 lines)
- `api_network_handlers.cpp` (350 lines)
- `api_settings_handlers.cpp` (180 lines)

**Duplicate Pattern:**
```cpp
// Pattern 1: Repetitive JSON doc allocation
StaticJsonDocument<512> doc;
doc["field1"] = value1;
doc["field2"] = value2;
String json;
json.reserve(256);
serializeJson(doc, json);
return HttpJsonUtils::send_json(req, json.c_str());

// Pattern 2: Repetitive field extraction from TransmitterManager
const uint8_t* ip = TransmitterManager::getIP();
char ip_str[16];
snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
doc["ip"] = ip_str;

// Pattern 3: Error checking boilerplate
if (!value) {
    return ApiResponseUtils::send_error_with_status(req, "400 Bad Request", "Field missing");
}
```

**Root Cause:** Low-level HTTP/JSON details repeated across many handlers.

**Consolidation Opportunity:**

Extract helper functions:
```cpp
// api/api_field_builders.h
namespace ApiFieldBuilders {
    void add_ipv4_field(JsonDocument& doc, const char* key, const uint8_t* ip);
    void add_mac_field(JsonDocument& doc, const char* key, const uint8_t* mac);
    void add_voltage_deci_volt_field(JsonDocument& doc, const char* key, uint16_t dv);
    StaticJsonDocument<SIZE> create_status_response(bool success, const char* msg);
}
```

**Expected Result:**
- Eliminate ~120 lines from telemetry handlers
- Eliminate ~60 lines from network handlers
- **Net savings: ~180 lines (12%)**

---

### 4. Page Registration Pattern (40 similar registrations)

**Pattern:**
Every page file has identical registration code:
```cpp
esp_err_t register_xxx_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/xxx",
        .method    = HTTP_GET,
        .handler   = xxx_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
```

**Opportunity:**
Use macro or table-driven registration:
```cpp
// Macro approach
#define REGISTER_PAGE(name, uri, method) \
    httpd_uri_t uri_##name = {.uri = uri, .method = method, .handler = name##_handler, .user_ctx = NULL}; \
    httpd_register_uri_handler(server, &uri_##name);

// Table approach  
static const PageRegistration pages[] = {
    {"battery_specs.html", "/battery_specs.html", battery_specs_page_handler},
    {"inverter_specs.html", "/inverter_specs.html", inverter_specs_page_handler},
    // ...
};
for (auto& p : pages) {
    register_page(server, p);
}
```

**Expected Result:**
- Eliminate 40 × 8 = 320 lines of boilerplate
- Create 1 table (~20 lines) + 1 helper (~15 lines)
- **Net savings: ~285 lines (89%)**

**Note:** Current codebase already uses `register_all_api_handlers()` pattern; apply same to pages.

---

## Full Rewrite Candidates

### 1. Spec Pages → Generic Template System (HIGH CONFIDENCE)

**Current:** 4 nearly identical page modules (451 lines)  
**Proposed:** 1 parameterized template (120 lines)  
**Rewrite Gain:** 331 lines (73% reduction)  
**Risk:** LOW — spec pages are isolated; existing test URLs remain the same

**Implementation Plan:**
1. Extract common pattern into `generic_specs_page_handler()`
2. Define spec descriptor tables for battery, inverter, charger, system
3. Update `page_definitions.cpp` to use new template
4. Verify URLs and rendering in browser

**Acceptance Criteria:**
- All 4 spec pages render correctly
- No change in HTML structure or styling
- Performance ≤ current (likely improves due to reduced allocations)

---

### 2. Transmitter Manager → 3-4 Core Modules (MEDIUM CONFIDENCE)

**Current:** 22 files (1,517 lines) with fragmented responsibilities  
**Proposed:** 8 files (950 lines)  
**Rewrite Gain:** 567 lines (37% reduction)  
**Risk:** MEDIUM — refactoring is low-risk, but requires careful state mapping

**Core Module Consolidation:**

| New Module | Responsibility | Merged Files | Lines |
|-----------|---|---|---|
| `transmitter_identity.cpp` | MAC, device identity metadata | mac_registration, mac_query_helper, identity_cache, metadata_store_workflow, metadata_query_helper | 160 |
| `transmitter_state.cpp` | Connection state, active device tracking | connection_state_resolver, active_mac_resolver, status_cache, runtime_status_update | 200 |
| `transmitter_network.cpp` | Network config storage + queries | network_cache, network_query_helper, network_store_workflow | 130 |
| `transmitter_mqtt_specs.cpp` | MQTT cache (battery/inverter/charger/system specs) | mqtt_cache, mqtt_query_helper, mqtt_config_workflow, battery_spec_sync, spec_cache | 180 |
| `transmitter_event_logs.cpp` | Event log cache + workflow | event_log_cache, event_logs_workflow | 140 |
| `transmitter_nvs.cpp` | NVS persistence | nvs_persistence, write_through | 160 |
| `transmitter_manager.cpp` | Public facade (unchanged) | transmitter_manager | 433 |
| `transmitter_peer_registry.cpp` | Multi-device support | peer_registry | 65 |

**Implementation Stages:**

**Stage 1: Identity Module** (Safest first)
- Merge 5 MAC/identity files into 1
- All getters/setters unchanged
- Verify web UI shows correct MAC

**Stage 2: Network Module**
- Merge network_cache, query_helper, store_workflow
- Verify IP display and storage in web UI

**Stage 3: State Module**
- Merge connection tracking files
- Verify connection status indicators

**Stage 4: MQTT/Specs Module**
- Merge cache + workflow files
- Verify spec display pages still render

**Acceptance Criteria:**
- Public API (`TransmitterManager::`) unchanged
- All web endpoints return identical data
- No increase in build time or binary size
- State consistency in multi-device scenarios

---

### 3. API Response Boilerplate → Helper Library (LOW COMPLEXITY)

**Current:** Repeated JSON patterns across api_telemetry_handlers, api_network_handlers (180 lines duplication)  
**Proposed:** Centralized field builder helpers (45 lines)  
**Rewrite Gain:** 135 lines (75% reduction in duplicated patterns)  
**Risk:** LOW — isolated helper extraction

**New Module: `api/api_field_builders.h/.cpp`**

```cpp
namespace ApiFieldBuilders {
    // IP address conversions
    void add_ipv4_field(JsonDocument& doc, const char* key, const uint8_t* ipv4);
    void add_ipv4_string(char* buf, size_t buflen, const uint8_t* ipv4);
    
    // MAC address conversions
    void add_mac_field(JsonDocument& doc, const char* key, const uint8_t* mac);
    void add_mac_string(char* buf, size_t buflen, const uint8_t* mac);
    
    // Voltage/current conversions
    void add_voltage_deci_field(JsonDocument& doc, const char* key, uint16_t dv);
    void add_current_field(JsonDocument& doc, const char* key, float amps);
    
    // Response templates
    esp_err_t send_status_response(httpd_req_t* req, bool success, const char* msg = "OK");
    esp_err_t send_telemetry_response(httpd_req_t* req, JsonDocument& doc);
}
```

**Expected Result:**
- Reduce api_telemetry_handlers from 516 → 440 lines
- Reduce api_network_handlers from 350 → 300 lines
- **Net savings: ~126 lines (12% aggregate)**

---

### 4. Page Registration Factory (MECHANICAL)

**Current:** 24 page registration functions with identical boilerplate  
**Proposed:** Declarative table + generic registration helper  
**Rewrite Gain:** 285 lines (89% of registration code)  
**Risk:** VERY LOW — mechanical transformation

**New Approach:**

```cpp
// pages/page_registry.h
struct PageRegistrationEntry {
    const char* uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t*);
};

// pages/page_registry.cpp
static const PageRegistrationEntry PAGE_ROUTES[] = {
    {"/battery_settings.html", HTTP_GET, battery_specs_page_handler},
    {"/inverter_settings.html", HTTP_GET, inverter_specs_page_handler},
    // ... all 24 pages
};

int register_all_pages(httpd_handle_t server) {
    int count = 0;
    for (const auto& entry : PAGE_ROUTES) {
        httpd_uri_t uri = {
            .uri = entry.uri,
            .method = entry.method,
            .handler = entry.handler,
            .user_ctx = NULL
        };
        if (httpd_register_uri_handler(server, &uri) == ESP_OK) {
            count++;
        }
    }
    return count;
}
```

**Acceptance Criteria:**
- All 24 pages accessible via web UI
- Build succeeds without warning
- No performance degradation

---

## Proposed New Shared Helper Modules

### 1. **specs/generic_specs_page.h/.cpp** (Spec Pages)

**Responsibility:** Generic spec page rendering for battery, inverter, charger, system specs

**Public Interface:**
```cpp
namespace GenericSpecsPage {
    struct SpecField {
        const char* label;
        const char* format;  // "%.1f", "%d", "%s"
        const char* unit;
        union {
            float fval;
            int32_t ival;
            const char* sval;
        } value;
    };

    struct SpecPageConfig {
        const char* page_title;
        const char* page_heading;
        const char* source_topic;
        const char* gradient_start;
        const char* gradient_end;
        const char* accent_color;
        const SpecField* fields;
        size_t field_count;
    };

    esp_err_t handle_spec_page(httpd_req_t* req, const SpecPageConfig& config);
}
```

**Files to Remove:** battery_specs_display_page.*, inverter_specs_display_page.*, charger_specs_display_page.*, system_specs_display_page.*

**Expected Savings:** ~330 lines

---

### 2. **webserver/api/api_field_builders.h/.cpp** (API Response Helpers)

**Responsibility:** Reusable field extraction and JSON population helpers

**Public Interface:**
```cpp
namespace ApiFieldBuilders {
    void add_ipv4_field(JsonDocument& doc, const char* key, const uint8_t* ipv4);
    void add_mac_field(JsonDocument& doc, const char* key, const uint8_t* mac);
    void add_voltage_field(JsonDocument& doc, const char* key, uint16_t dv);
    void add_power_field(JsonDocument& doc, const char* key, int32_t watts);
    String format_ipv4_string(const uint8_t* ipv4);
    String format_mac_string(const uint8_t* mac);
}
```

**Expected Savings:** ~90 lines

---

### 3. **webserver/pages/page_registry.h/.cpp** (Page Registration Factory)

**Responsibility:** Centralized page handler registration

**Public Interface:**
```cpp
namespace PageRegistry {
    int register_all_pages(httpd_handle_t server);
    int expected_page_count();
}
```

**Expected Savings:** ~280 lines

---

### 4. **common/transmitter_state_core.h/.cpp** (Consolidates transmitter utils)

**Responsibility:** Unified state management for transmitter tracking

**Consolidates:** 5 separate "state" modules into 1

**Expected Savings:** ~120 lines

---

## Prioritized Implementation Roadmap

## Implementation Status (Updated: 2026-03-24)

### Completed Steps

1. ✅ P0.1 Extract API field builders
2. ✅ P0.2 Consolidate transmitter identity module
3. ✅ P0.3 Create page registration factory
4. ✅ P1.1 Consolidate transmitter state modules
5. ✅ P1.2 Consolidate transmitter network module
6. ✅ P1.3 Create generic spec page template and migrate battery/inverter/charger/system pages
7. ✅ P2.1 Consolidate transmitter MQTT/specs modules (`TransmitterMqttSpecs`)
8. ✅ P2.2 Consolidate transmitter NVS persistence (write-through moved into `TransmitterNvsPersistence`)
9. ✅ P2.3 Extract main task configuration (LED renderer task config moved to `task_config.h`)
10. ✅ P2.4 Extract runtime primitive/task startup from `main.cpp` to `src/config/runtime_task_startup.*`
11. ✅ P2.5 Convert metadata/settings helper workflows to shim wrappers (duplicate implementation removed)
12. ✅ P2.6 Refactor runtime task startup to descriptor-driven task table with task creation error checks
13. ✅ P2.7 Consolidate event logs workflow helper into cache wrapper (duplicate implementation removed)
14. ✅ P2.8 Consolidate identity cache helper into `TransmitterIdentity` wrapper (duplicate implementation removed)
15. ✅ P2.9 Physically delete all 49 shim/wrapper files from `lib/webserver/utils` — utils directory reduced from 75+ files to 27 canonical modules
16. ✅ P3.1 **[BOTH DEVICES]** Delete transmitter legacy forwarding shim `src/runtime/bootstrap_phase_runner.h` (was forwarding to `esp32common`)
17. ✅ P3.2 **[BOTH DEVICES]** Migrate receiver `setup()` from monolithic function to `BootstrapPhaseRunner::run_phases()` — aligns receiver boot architecture with transmitter (7 named phases: hardware, display, filesystem, services, display_content, tasks, espnow_state)
18. ✅ P3.3 **[BOTH DEVICES]** Consolidate duplicated OTA setup health-gate logic into shared `esp32common/runtime_common_utils/setup_health_gate.*` and migrate both `main.cpp` files to use it
19. ✅ P3.4 **[BOTH DEVICES]** Remove duplicated ESP-NOW connecting timeout constant: define `TimingConfig::ESPNOW_CONNECTING_TIMEOUT_MS` in `esp32common` and migrate receiver/transmitter to use it
20. ✅ P3.5 **[BOTH DEVICES]** Remove 4 duplicate timing constants from transmitter `task_config.h` (`timing` namespace) and receiver `task_config.h` that were already defined in `TimingConfig`: `ESPNOW_SEND_INTERVAL_MS`, `ANNOUNCEMENT_INTERVAL_MS`, `MQTT_PUBLISH_INTERVAL_MS`, `MQTT_RECONNECT_INTERVAL_MS` — added `ANNOUNCEMENT_INTERVAL_MS` to shared `esp32common/config/timing_config.h`; migrated all 5 consumer files (`data_sender.cpp`, `discovery_task.cpp`, `mqtt_task.cpp`, `ethernet_manager.cpp`, `runtime_task_startup.cpp`)
21. ✅ P3.6 **[BOTH DEVICES]** Remove duplicate timing literals inside shared ESP-NOW common utilities by aliasing `EspNowTiming` constants to canonical `TimingConfig` values (`PEER_REGISTRATION_DELAY_MS`, `DISCOVERY_RETRY_DELAY_MS`, `HEARTBEAT_INTERVAL_MS`) in `esp32common/espnow_common_utils/espnow_timing_config.h`
22. ✅ P3.7 **[TX]** Remove two remaining transmitter-local duplicate timing literals by aliasing class constants to `TimingConfig` (`HeartbeatManager::HEARTBEAT_INTERVAL_MS`, `TimeManager::NTP_RESYNC_INTERVAL_MS`)
23. ✅ P3.8 **[TX]** Remove redundant class-level alias constants after `TimingConfig` migration: delete `HeartbeatManager::HEARTBEAT_INTERVAL_MS` and unused `TimeManager::NTP_RESYNC_INTERVAL_MS`; use `TimingConfig::HEARTBEAT_INTERVAL_MS` directly in implementation
24. ✅ P3.9 **[BOTH DEVICES]** Remove one more duplicate shared ESP-NOW timing literal by aliasing `EspNowTiming::CHANNEL_STABILIZING_DELAY_MS` to canonical `TimingConfig::CHANNEL_SWITCHING_DELAY_MS` in `esp32common/espnow_common_utils/espnow_timing_config.h`
25. ✅ P3.10 **[TX→SHARED]** Move 8 transmitter runtime intervals from local `task_config.h` (`timing` namespace) to shared `TimingConfig` and migrate all consumers (`main.cpp`, `mqtt_task.cpp`, `ethernet_manager.cpp`): `MQTT_STATS_LOG_INTERVAL_MS`, `MQTT_CELL_PUBLISH_INTERVAL_MS`, `MQTT_EVENT_PUBLISH_INTERVAL_MS`, `ETH_STATE_MACHINE_UPDATE_INTERVAL_MS`, `CAN_STATS_LOG_INTERVAL_MS`, `STATE_VALIDATION_INTERVAL_MS`, `METRICS_REPORT_INTERVAL_MS`, `PEER_AUDIT_INTERVAL_MS`
26. ✅ P3.11 **[TX→SHARED]** Migrate final active transmitter `timing` constants to shared `TimingConfig` (`TX_HEARTBEAT_TIMEOUT_MS`, `DEFERRED_DISCOVERY_POLL_MS`) and update `tx_connection_handler.cpp`; remove now-empty `timing` namespace and dead component-config timing constants from transmitter `task_config.h`
27. ✅ P3.12 **[TX]** Post-migration cleanup: remove obsolete `task_config.h` include from `network/ethernet_manager.cpp` (no remaining `task_config` symbols used after timing consolidation)
28. ✅ P4.1 **[TX]** Begin “Task configuration factory” opportunity: extract transmitter runtime task startup orchestration from `main.cpp` into dedicated `config/runtime_task_startup.h/.cpp` module; preserve startup order and task parameters, add MQTT task creation result check
29. ✅ P4.2 **[TX]** Implement state-machine DSL transition table in `tx_state_machine.cpp`: replace implicit transition behavior with declarative `kTransitionRules` table and enforced transition validation for `set_state()`/`on_connected()`
30. ✅ P4.3 **[RX]** Implement config-driven page content generation for spec-page navigation links: add `build_spec_page_nav_links()` and migrate battery/inverter/charger/system spec page nav sections from hardcoded HTML strings to `SpecPageNavLink` tables
31. ✅ P4.4 **[RX→SHARED]** Extract spec-page layout generator to shared `esp32common/webserver_common_utils/spec_page_layout.*`; keep receiver API compatibility wrapper in `lib/webserver/common/spec_page_layout.h/.cpp`

### Legacy/Redundant Code Cleanup Verification

- ✅ Active runtime code paths now use consolidated modules (`TransmitterIdentity`, `TransmitterState`, `TransmitterNetwork`, `TransmitterMqttSpecs`, `TransmitterNvsPersistence`).
- ✅ `TransmitterManager` no longer depends on legacy helper/cache/workflow wrappers.
- ✅ NVS load/save now targets consolidated modules directly.
- ✅ Runtime primitive/task startup boilerplate extracted from `main.cpp` into a dedicated startup module.
- ✅ Runtime task creation is now descriptor-driven in one table (`runtime_task_startup.cpp`).
- ✅ Event logs workflow and identity cache duplicate implementation logic removed.
- ✅ No `Legacy compatibility translation unit` / `DEPRECATED: Use` markers remain in `lib/webserver/utils`.
- ✅ All 49 shim/wrapper `.h` and `.cpp` files physically deleted from `lib/webserver/utils`.
- ✅ `lib/webserver/utils` now contains only 27 files (13 canonical module pairs + 1 types header + 1 snapshot utils header).
- ✅ `receiver_tft` build validated successfully after each completed step.
- ✅ Cross-device runtime alignment complete: both receiver and transmitter now use shared `BootstrapPhaseRunner` startup structure and shared setup health-gate helper from `esp32common`.
- ✅ Both receiver and transmitter builds validated successfully after P3 cross-device steps.
- ✅ ESP-NOW connecting timeout now comes from one shared constant (`TimingConfig::ESPNOW_CONNECTING_TIMEOUT_MS`) with local transmitter duplicate removed.
- ✅ P3.5: 4 further duplicate timing constants (`ESPNOW_SEND_INTERVAL_MS`, `ANNOUNCEMENT_INTERVAL_MS`, `MQTT_PUBLISH_INTERVAL_MS`, `MQTT_RECONNECT_INTERVAL_MS`) removed from local `task_config.h` files on both devices; all consumers now reference `TimingConfig::` from `esp32common`.
- ✅ P3.6: Shared `espnow_common_utils/espnow_timing_config.h` now aliases overlapping constants to `TimingConfig`, preventing future value drift between common ESP-NOW internals and cross-project timing policy.
- ✅ P3.7: Transmitter `heartbeat_manager.h` and `time_manager.h` now source heartbeat interval and NTP resync interval from `TimingConfig`, removing duplicated class-local literals.
- ✅ P3.8: Removed remaining class-level alias members in transmitter timing paths so implementations consume `TimingConfig` directly (one less indirection, no dead alias members).
- ✅ P3.9: `EspNowTiming::CHANNEL_STABILIZING_DELAY_MS` now aliases shared `TimingConfig::CHANNEL_SWITCHING_DELAY_MS`, eliminating another duplicated literal in common ESP-NOW timing policy.
- ✅ P3.10: 8 more transmitter runtime interval constants now come from shared `TimingConfig`; local duplicates removed from transmitter `task_config.h`, with all call sites migrated.
- ✅ P3.11: Final active transmitter timing constants migrated to `TimingConfig`; transmitter-local `timing` namespace fully removed from `task_config.h` (including unused/dead component-config interval constants).
- ✅ P3.12: Obsolete include dependency removed from transmitter `ethernet_manager.cpp`, reflecting completed `task_config` timing decoupling.
- ✅ P4.1: Transmitter task startup sequence now isolated in `config/runtime_task_startup.*`, reducing `main.cpp` orchestration bloat while preserving functional startup order.
- ✅ P4.2: Transmitter connection state transitions are now declarative and validated against a transition table, reducing hidden/implicit transition paths.
- ✅ P4.3: Spec-page navigation HTML is now generated from small config tables (`SpecPageNavLink`) instead of hand-authored repeated anchor blocks.
- ✅ P4.4: Spec-page layout generation has been extracted to shared `esp32common/webserver_common_utils`, establishing reusable cross-project webserver layout infrastructure.

### **P0: Quick Wins (Low Risk, High Impact)**

**Target:** 2-3 days, ~400 lines reduction

1. **Extract API field builders** [Day 1 AM]
   - Create `api/api_field_builders.h/.cpp` with field helpers
   - Update `api_telemetry_handlers.cpp` to use builders
   - Verify no functional change
   - **Savings:** 90 lines
   - **Risk:** Very Low

2. **Consolidate transmitter identity module** [Day 1 PM]
   - Merge mac_registration, mac_query_helper, identity_cache into 1 file
   - Keep public API unchanged
   - Verify MAC display in web UI
   - **Savings:** 140 lines
   - **Risk:** Very Low

3. **Create page registration factory** [Day 1 PM]
   - Convert 24 page registrations to table-driven approach
   - Create `pages/page_registry.cpp` with centralized registration
   - Verify all page URLs still work
   - **Savings:** 280 lines
   - **Risk:** Very Low (mechanical)

**Acceptance Criteria for P0:**
- No functional regressions in browser test
- Build compiles without warnings
- Binary size ≤ current (likely smaller)

---

### **P1: Medium-Risk, High-Value** (3-4 days, ~600 lines reduction)

**Target:** Following P0 completion

1. **Consolidate transmitter state modules** [Days 2-3]
   - Merge connection_state_resolver, active_mac_resolver, status_cache, runtime_status_update into `transmitter_state.cpp`
   - Verify connection status indicators work correctly
   - **Savings:** 200 lines
   - **Risk:** Low-Medium (state machine correctness)

2. **Consolidate transmitter network module** [Day 3]
   - Merge network_cache, network_query_helper, network_store_workflow
   - Verify IP storage and retrieval
   - **Savings:** 130 lines
   - **Risk:** Low

3. **Create generic spec page template** [Day 4 AM]
   - Extract common pattern from 4 spec pages
   - Create `specs/generic_specs_page.cpp` handler
   - Update page definitions to use new handler
   - Verify battery/inverter/charger/system specs pages render correctly
   - **Savings:** 330 lines
   - **Risk:** Low-Medium (HTML generation, but isolated)

**Acceptance Criteria for P1:**
- All state transitions work (heartbeat, connection detection, LED feedback)
- Spec pages render with identical output
- Network config persists correctly across reboots
- No performance degradation

---

### **P2: Complete Refactoring** (4-5 days, ~500 lines reduction)

**Target:** After P0/P1 validated

1. **Consolidate transmitter MQTT/specs modules** [Days 5-6]
   - Merge mqtt_cache, mqtt_query_helper, mqtt_config_workflow, battery_spec_sync, spec_cache
   - Verify all MQTT subscriptions still work
   - Verify spec caching behavior
   - **Savings:** 180 lines
   - **Risk:** Medium (MQTT subscription coordination)

2. **Consolidate transmitter NVS persistence** [Day 6]
   - Merge nvs_persistence, write_through
   - Verify NVS data persists correctly
   - **Savings:** 110 lines
   - **Risk:** Low

3. **Extract main.cpp task configuration** [Day 7 AM]
   - Move task creation boilerplate to dedicated config file
   - Reduce main.cpp cognitive load
   - **Savings:** 30 lines in main, improved clarity
   - **Risk:** Very Low

**Acceptance Criteria for P2:**
- MQTT subscriptions work (battery specs, inverter specs, events)
- Data persists to NVS and reloads correctly
- All tasks initialize and run normally
- Memory usage ≤ current

---

### **P3: Future Opportunities** (Not in current roadmap)

1. **Task configuration factory** — Extract all task creation boilerplate to DSL
2. **State machine DSL** — Replace hardcoded transitions with declarative table
3. **Page content generation** — Auto-generate HTML from configuration
4. **Common webserver codebase** — Extract to esp32common for cross-project reuse

### **P3: Completed Cross-Device Alignment**

1. ✅ **[TX] Delete legacy bootstrap_phase_runner shim** — `src/runtime/bootstrap_phase_runner.h` was a forwarding stub to `esp32common`; deleted.
2. ✅ **[RX] Receiver setup() → BootstrapPhaseRunner** — Receiver `main.cpp` setup() decomposed into 7 named phase functions and wired to `BootstrapPhaseRunner::run_phases()` matching the transmitter pattern exactly.

---

## Summary Table

| Phase | Item | Effort | Savings | Risk | Priority | Status |
|-------|------|--------|---------|------|----------|--------|
| P0 | API field builders | 2h | 90 lines | Very Low | 1 | ✅ Completed |
| P0 | Transmitter identity consolidation | 2h | 140 lines | Very Low | 2 | ✅ Completed |
| P0 | Page registration factory | 1.5h | 280 lines | Very Low | 3 | ✅ Completed |
| P1 | Transmitter state consolidation | 4h | 200 lines | Low | 4 | ✅ Completed |
| P1 | Transmitter network consolidation | 3h | 130 lines | Low | 5 | ✅ Completed |
| P1 | Generic spec page template | 4h | 330 lines | Low | 6 | ✅ Completed |
| P2 | Transmitter MQTT/specs consolidation | 5h | 180 lines | Medium | 7 | ✅ Completed |
| P2 | Transmitter NVS consolidation | 2h | 110 lines | Low | 8 | ✅ Completed |
| P2 | Extract main.cpp config | 1.5h | 30 lines | Very Low | 9 | ✅ Completed |
| P2 | Extract runtime task startup module | 1.5h | 60 lines | Very Low | 10 | ✅ Completed |
| P2 | Remove duplicate metadata/settings helper implementations | 1h | 70 lines | Very Low | 11 | ✅ Completed |
| P2 | Descriptor-driven runtime task creation | 0.5h | 25 lines | Very Low | 12 | ✅ Completed |
| P2 | Remove duplicate event logs workflow implementation | 0.5h | 20 lines | Very Low | 13 | ✅ Completed |
| P2 | Remove duplicate identity cache implementation | 0.5h | 20 lines | Very Low | 14 | ✅ Completed |
| P2 | Physical deletion of all 49 shim/compat files | 0.5h | ~1,200 lines | Very Low | 15 | ✅ Completed |
| P3 | [TX] Delete legacy bootstrap_phase_runner shim | 0.1h | 5 lines | Very Low | 16 | ✅ Completed |
| P3 | [RX] Migrate setup() to BootstrapPhaseRunner phases | 1h | ~50 lines | Very Low | 17 | ✅ Completed |
| P3 | [RX+TX] Shared setup health-gate helper (`setup_health_gate.*`) | 0.5h | 0 lines (duplication removed across devices) | Very Low | 18 | ✅ Completed |
| P3 | [RX+TX] Shared ESPNOW connecting timeout constant | 0.2h | 1 duplicate constant removed | Very Low | 19 | ✅ Completed |
| P3 | [RX+TX] Remove 4 more duplicate timing constants (ESPNOW_SEND, ANNOUNCEMENT, MQTT_PUBLISH, MQTT_RECONNECT) | 0.3h | 4 duplicate constants removed | Very Low | 20 | ✅ Completed |
| P3 | [RX+TX] Alias 3 shared ESP-NOW timing literals to `TimingConfig` (PEER_REGISTRATION, DISCOVERY_RETRY, HEARTBEAT_INTERVAL) | 0.1h | 3 duplicate literals removed | Very Low | 21 | ✅ Completed |
| P3 | [TX] Alias heartbeat + NTP resync class constants to `TimingConfig` | 0.1h | 2 duplicate literals removed | Very Low | 22 | ✅ Completed |
| P3 | [TX] Remove redundant class alias constants after TimingConfig migration | 0.1h | 2 alias members removed | Very Low | 23 | ✅ Completed |
| P3 | [RX+TX] Alias channel stabilization delay in shared ESP-NOW timing to `TimingConfig` | 0.1h | 1 duplicate literal removed | Very Low | 24 | ✅ Completed |
| P3 | [TX→SHARED] Move 8 runtime interval constants to `TimingConfig` and migrate consumers | 0.3h | 8 duplicate constants removed | Very Low | 25 | ✅ Completed |
| P3 | [TX→SHARED] Migrate final active `timing` constants; remove `timing` namespace from TX task config | 0.2h | 4 constants removed/migrated (2 active + 2 dead) | Very Low | 26 | ✅ Completed |
| P3 | [TX] Remove obsolete `task_config.h` include from ethernet manager | 0.05h | 1 include dependency removed | Very Low | 27 | ✅ Completed |
| P4 | [TX] Extract runtime task startup orchestration to `config/runtime_task_startup.*` | 0.3h | main.cpp task-launch boilerplate extracted | Very Low | 28 | ✅ Completed |
| P4 | [TX] State machine DSL transition table (`kTransitionRules`) for connection transitions | 0.3h | transition rules centralized/declarative | Very Low | 29 | ✅ Completed |
| P4 | [RX] Config-driven spec-page nav generation (`SpecPageNavLink` tables) | 0.2h | repeated nav HTML blocks removed | Very Low | 30 | ✅ Completed |
| P4 | [RX→SHARED] Extract spec-page layout generator to `esp32common/webserver_common_utils` | 0.4h | shared webserver layout code established | Very Low | 31 | ✅ Completed |
| **TOTAL** | | **~28.65h** | **~1,390 lines** | **Low-Med** | |

---

## Detailed Top 10 Target Files

### 1. **battery_specs_display_page.cpp** (88 lines)
- **Current:** Standalone spec page handler
- **Action:** Merge into generic spec page template
- **Savings:** 85 lines (96%)
- **Risk:** Low
- **Acceptance:** Spec page renders identically at `/battery_settings.html`

### 2. **inverter_specs_display_page.cpp** (121 lines)
- **Current:** Standalone spec page handler
- **Action:** Merge into generic spec page template
- **Savings:** 115 lines (95%)
- **Risk:** Low
- **Acceptance:** Spec page renders identically at `/inverter_settings.html`

### 3. **transmitter_manager.cpp** (433 lines)
- **Current:** Facade for 22 underlying modules
- **Action:** Keep as-is; consolidate sub-modules
- **Savings:** Not direct (enables 567 line reduction in utils)
- **Risk:** Very Low (no change to public API)
- **Acceptance:** No change in external behavior

### 4. **api_telemetry_handlers.cpp** (516 lines)
- **Current:** Data aggregation + JSON serialization with repetitive patterns
- **Action:** Extract field builders; consolidate JSON patterns
- **Savings:** 80 lines (16%)
- **Risk:** Low
- **Acceptance:** All telemetry endpoints return identical JSON

### 5. **transmitter_connection_state_resolver.cpp** (60 lines)
- **Current:** Tracks device connection state (duplicates active_mac_resolver)
- **Action:** Merge into unified `transmitter_state.cpp`
- **Savings:** 45 lines (75% when merged)
- **Risk:** Very Low
- **Acceptance:** Connection state detection still works

### 6. **transmitter_active_mac_resolver.cpp** (55 lines)
- **Current:** Resolves active device MAC (duplicates connection_state_resolver)
- **Action:** Merge into unified `transmitter_state.cpp`
- **Savings:** 40 lines (73% when merged)
- **Risk:** Very Low
- **Acceptance:** Multi-device detection still works

### 7. **transmitter_network_query_helper.cpp** (45 lines)
- **Current:** Wrapper delegates to transmitter_network_cache
- **Action:** Merge into network_cache
- **Savings:** 40 lines (89%)
- **Risk:** Very Low (pure wrapper elimination)
- **Acceptance:** Network queries still work

### 8. **transmitter_mqtt_query_helper.cpp** (52 lines)
- **Current:** Wrapper delegates to transmitter_mqtt_cache
- **Action:** Merge into mqtt_cache
- **Savings:** 45 lines (87%)
- **Risk:** Very Low
- **Acceptance:** MQTT queries still work

### 9. **charger_specs_display_page.cpp** (109 lines)
- **Current:** Standalone spec page handler
- **Action:** Merge into generic spec page template
- **Savings:** 105 lines (96%)
- **Risk:** Low
- **Acceptance:** Spec page renders identically

### 10. **transmitter_network_store_workflow.cpp** (68 lines)
- **Current:** Duplicates network_cache storage logic
- **Action:** Merge into network_cache
- **Savings:** 60 lines (88%)
- **Risk:** Low
- **Acceptance:** Network configuration persists correctly

---

## Expected Outcomes

### Before Optimization
- **Total Codebase:** ~12,500 lines (webserver + espnow + mqtt + display)
- **Identified Bloat:** ~1,400 lines (11%)
- **Test Coverage:** Good (isolated modules)
- **Build Time:** ~45 seconds (baseline)

### After P0+P1 Completion
- **Total Codebase:** ~11,100 lines (1,400 line reduction)
- **Bloat Remaining:** ~300 lines (minor)
- **Test Coverage:** Maintained or improved
- **Build Time:** ~42 seconds (-7%, fewer files)
- **Maintainability:** 📈 Significantly improved (fewer redundant modules)
- **Binary Size:** ~8KB smaller (fewer duplicate code paths)

### After P2 Completion
- **Total Codebase:** ~10,600 lines (1,900 line reduction)
- **Maintainability:** 📈📈 Excellent (consolidated state management)
- **Code Duplication:** <3% (down from 11%)
- **Future Extensibility:** ✅ Template systems in place for spec pages and pages

---

## ⚠️ CRITICAL: HTTP Stack & JSON Document Size Constraints

**IMPORTANT NOTE FOR ALL IMPLEMENTATION PHASES:**

Changes to the HTTP stack or JSON document sizes must be carefully considered and thoroughly tested. **Previous changes to JSON document allocations and HTTP response handling have caused device panics and memory corruption.**

**Specific Areas of Concern:**
- `StaticJsonDocument<SIZE>` allocations in API handlers — exceeding available heap can cause panics
- HTTP response buffer sizes — undersizing causes buffer overflow; oversizing exhausts heap
- Page content string sizes — large inline HTML/JS can fragment memory
- Concurrent request handling — multiple large documents simultaneously can exceed available RAM

**Mandatory Validation for HTTP/JSON Changes:**
1. Monitor heap usage during implementation (use `esp_get_free_heap_size()` logging)
2. Test with concurrent requests to multiple endpoints
3. Verify device stability after extended runtime (minimum 1 hour)
4. Check for memory fragmentation patterns using heap dump analysis
5. Never increase `StaticJsonDocument` size without explicit device testing
6. Document the maximum concurrent requests and typical heap usage for each endpoint

**When in doubt, test on hardware first before merging.**

---

## Risk Mitigation Strategy

### Testing Plan
1. **Unit Tests:** Verify individual helper function behavior
2. **Integration Tests:** Ensure all webserver endpoints return correct data
3. **Regression Tests:** Run on actual device with browser testing
4. **State Machine Tests:** Verify connection detection, heartbeats, LED feedback

### Rollback Plan
- Maintain git commits after each phase
- Preserve original module names/locations during consolidation (use aliases if needed)
- Test on staging device before merging to main

### Validation Checklist

**P0 Sign-Off:**
- [x] API telemetry handler return identical JSON
- [x] All 24 pages load without errors
- [x] Build size ≤ current

**P1 Sign-Off:**
- [x] Connection status updates work
- [x] Spec pages render identically
- [x] Network config persists across reboot

**P2 Sign-Off:**
- [x] MQTT subscriptions active
- [x] Event logs display correctly
- [x] All tasks initialize successfully

---

## No-Action Items (For Reference)

These modules are well-designed and should NOT be modified:

| Module | Reason |
|--------|--------|
| `espnow_tasks.cpp` | Well-isolated message processing |
| `battery_data_store.cpp` | Single responsibility, clear interface |
| `mqtt_client.cpp` | Clean MQTT abstraction |
| `display_core.cpp` | Good HAL abstraction |
| `rx_heartbeat_manager.cpp` | Critical for connection tracking |
| `rx_state_machine.cpp` | Well-structured state machine |
| `page_generator.cpp` | Flexible template system (foundation for spec pages) |
| `nav_buttons.cpp` | Small, focused component |
| `event_logs_page.cpp` | Clear event aggregation logic |
| `sse_notifier.cpp` | Efficient SSE client management |

---

## Conclusion

The receiver project has a solid foundation with good modularization in the webserver layer. However, significant bloat exists from:
1. **Over-decomposition** of transmitter utilities (22 files → 8 viable)
2. **Spec page duplication** (4 nearly-identical modules → 1 template)
3. **Wrapper class proliferation** (query helpers, workflows)
4. **Boilerplate duplication** (page registration, JSON patterns)

**Recommended approach:**
- **Phase P0/P1** (3-4 days): Focus on low-risk consolidations (890 lines saved)
- **Phase P2** (4-5 days): Complete transmitter manager refactoring (500 lines saved)
- **Expected Result:** 1,390 lines eliminated (11% codebase reduction) with improved maintainability

All identified issues can be addressed with minimal risk through careful refactoring and comprehensive testing.
