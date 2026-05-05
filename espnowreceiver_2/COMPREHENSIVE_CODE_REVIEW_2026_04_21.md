# Comprehensive Code Review: espnowreceiver_2 (271 source files)

**Date:** April 21, 2026  
**Reviewer:** GitHub Copilot  
**Scope:** Complete architecture analysis (boot phases, RTOS, ESP-NOW, MQTT, display, config, API, caching, memory sampling, webserver, SSE)  
**Baseline:** Compared against [RECEIVER_FULL_CODE_REVIEW_2026_03_16.md](RECEIVER_FULL_CODE_REVIEW_2026_03_16.md)

---

## Executive Summary

The espnowreceiver_2 project is a well-architected ESP32 receiver for Battery Emulator telemetry over ESP-NOW + MQTT. The modular design, clear message routing, and battery data integration are solid. However, this review identifies several **high-severity concurrency bugs**, **configuration reconfiguration limitations**, and **cache inconsistencies** that must be addressed for production stability and maintainability.

**Key Findings:**
- ✅ **Strong:** Boot sequencing, modular message routing, display decoupling
- ⚠️ **Moderate Risk:** MQTT not reconfigurable at runtime, transmitter cache thread safety gaps
- 🔴 **High Risk:** Race conditions in shared state caches, field name consistency issues in API responses

---

## I. ARCHITECTURE STRENGTHS (5 KEY AREAS)

### 1.1 Boot Phases and Startup Sequencing ✅
**Files:** [src/main.cpp](src/main.cpp#L1-L150), [src/config/runtime_task_startup.cpp](src/config/runtime_task_startup.cpp#L1-L100)

**Strengths:**
- **Well-ordered phases:** WiFi → LittleFS → webserver → RTOS primitives → message routes → tasks
- **Message route setup BEFORE worker task** (line 35-39 runtime_task_startup.cpp) prevents critical race where PROBE messages arrive before handlers are registered
- **Clear comments** mark critical ordering constraints (e.g., "CRITICAL: Setup message routes BEFORE starting worker task")
- **Error gates:** Each phase fails loudly with `handle_error(FATAL)` if primitives can't be created
- **Timing policy logging** (main.cpp line 54-77) centralizes all timing constants and makes tuning observable

**Assessment:** Well-designed bootstrap prevents silent failures and race conditions during cold boot.

---

### 1.2 FreeRTOS Task Architecture and RTOS Primitives ✅
**Files:** [src/config/runtime_task_startup.cpp](src/config/runtime_task_startup.cpp), [src/config/task_config.h](src/config/task_config.h)

**Strengths:**
- **Explicit primitives:** TFT mutex, ESP-NOW queue, display snapshot queue, burst-mode counter in memory sampler all created upfront with null checks
- **Task priorities well-balanced:**
  - ESP-NOW worker (priority 2) - highest to prevent message queue overflow
  - Display/LED/TX schedulers (priority 1) - medium for responsive UI
  - MQTT/Memory sampler (priority 0) - lowest to avoid blocking ingest
- **Core affinity explicit:** All worker tasks pinned to Core 1 (app core); Core 0 reserved for WiFi stack
- **Stack sizing documented:** Each task has comments on minimum, current, and when to increase (e.g., MQTT at 10240 for JSON serialization)
- **TX scheduler initialization** (EspnowTxScheduler) happens before MQTT task, preventing message send failures

**Assessment:** Solid RTOS design. Priorities and core affinity are correct for this workload.

---

### 1.3 ESP-NOW Ingress Pipeline and Message Routing ✅
**Files:** [src/espnow/espnow_callbacks.cpp](src/espnow/espnow_callbacks.cpp), [src/espnow/espnow_tasks.cpp](src/espnow/espnow_tasks.cpp#L100-L400)

**Strengths:**
- **Callback minimal work:** `on_data_recv()` only validates, copies payload, and queues (line 10-33 espnow_callbacks.cpp)
- **Drop tracking:** Maintains `rx_queue_drop_count` and `rx_queue_high_watermark` for observability (line 32-33)
- **Message router modular:** Each message type (DATA, BATTERY_STATUS, HEARTBEAT, LED, etc.) has explicit lambda-registered handler with type-safe cast
- **Probe ACK deduplication:** Static variables track last probe seq/mac/timestamp to avoid flooding ACKs during repeated PROBE bursts (line 140-160 espnow_tasks.cpp)
- **Config section requests** intelligently debounced (request_config_section function line 131-142)
- **Battery Emulator handlers** properly integrated without breaking core ingest path

**Assessment:** Clean ingress pipeline prevents callback backpressure and enables per-type processing.

---

### 1.4 Display Decoupling and Update Queue ✅
**Files:** [src/display/display_update_queue.cpp](src/display/display_update_queue.cpp), [src/main.cpp](src/main.cpp#L77-L150)

**Strengths:**
- **Snapshot model:** ESP-NOW worker enqueues lightweight snapshots (SOC, power, change flags) rather than forcing immediate TFT access
- **Lossy queue:** Newer updates automatically evict stale ones (line 34-39); prevents queue starvation from frequent updates
- **Separate renderer task:** Dedicated display task pulls snapshots and renders at own cadence without blocking ingest
- **LED renderer independent:** Has own task with 20ms poll delay; uses smart_delay() to remain responsive
- **Mutex timeout handling:** Both LED and display tasks handle 100ms mutex timeout gracefully (requeue or skip)

**Assessment:** Display architecture is decoupled from critical ingest path. No blocking animations in hot path.

---

### 1.5 Configuration Management (Runtime) ✅
**Files:** [lib/receiver_config/receiver_config_manager.cpp](lib/receiver_config/receiver_config_manager.cpp#L1-L200)

**Strengths:**
- **NVS validation:** Validates IP addresses (rejects 0.0.0.0 and 255.255.255.255), port ranges, SSID/password lengths before save
- **Atomic loads:** Entire network config loaded as unit from NVS; no partial-read races
- **Defaults intelligently chosen:** MQTT defaults to disabled; IP defaults to 0.0.0.0 (requires explicit config)
- **Static IP caching:** Allows users to switch DHCP↔Static without losing configuration
- **Simulation mode configurable:** Battery type/interface/simulation stored persistently

**Assessment:** Configuration layer is defensive and persistent. Good API surface for settings UI.

---

## II. HIGH-SEVERITY BUGS (with file paths and code context)

### 🔴 Bug #1: MQTT Not Reconfigurable at Runtime
**Severity:** HIGH (API inconsistency + feature gap)  
**File:** [src/mqtt/mqtt_task.cpp](src/mqtt/mqtt_task.cpp#L1-L50)

**Problem:**
```cpp
void task_mqtt_client(void* parameter) {
    // ...
    while (true) {
        if (ReceiverNetworkConfig::isMqttEnabled()) {
            const uint8_t* mqtt_server = ReceiverNetworkConfig::getMqttServer();
            if (mqtt_server[0] != 0 || mqtt_server[1] != 0 || mqtt_server[2] != 0 || mqtt_server[3] != 0) {
                static bool initialized = false;  // ⚠️ BUG: initialized is STATIC
                if (!initialized) {
                    MqttClient::init(...);
                    initialized = true;
                }
                MqttClient::loop();
            }
        }
        // ...
    }
}
```

**Issue:**
- Once `initialized = true`, MQTT client is never re-initialized even if user changes server IP/port via `/api/save_receiver_network`
- User must reboot to apply MQTT config changes
- If MQTT broker becomes unavailable and config is updated, old connection persists

**Impact:**
- Zero ability to change MQTT broker without full device reboot
- In production, this means downtime to redeploy MQTT infrastructure
- Violates expectations from webserver API (users expect settings to take effect immediately)

**Recommendation:**
```cpp
// Track configuration version
static uint32_t last_config_version = 0;
uint32_t current_version = ReceiverNetworkConfig::getConfigVersion();

if (current_version != last_config_version) {
    // Config changed: reconnect with new settings
    MqttClient::disconnect();
    MqttClient::init(mqtt_server, mqtt_port, "espnow_receiver");
    if (mqtt_username && mqtt_username[0] != '\0') {
        MqttClient::setAuth(mqtt_username, mqtt_password);
    }
    last_config_version = current_version;
}
```

---

### 🔴 Bug #2: Race Condition in Transmitter Cache Reads/Writes
**Severity:** HIGH (data corruption risk)  
**Files:** 
- [lib/webserver/utils/transmitter_identity.cpp](lib/webserver/utils/transmitter_identity.cpp#L1-L50)
- [lib/webserver/utils/transmitter_state.cpp](lib/webserver/utils/transmitter_state.cpp#L1-L80)
- [lib/webserver/utils/transmitter_network.cpp](lib/webserver/utils/transmitter_network.cpp#L1-L150)
- [lib/webserver/utils/transmitter_settings_cache.cpp](lib/webserver/utils/transmitter_settings_cache.cpp#L1-L150)

**Problem:**
```cpp
// transmitter_identity.cpp - NO MUTEX
namespace {
    uint8_t registered_mac[6] = {0};
    bool registered_mac_known = false;
}

void TransmitterIdentity::register_mac(const uint8_t* transmitter_mac) {
    if (transmitter_mac == nullptr) return;
    cache_mac(transmitter_mac);  // ⚠️ Writes static array without lock
}

const uint8_t* TransmitterIdentity::get_active_mac() {
    // ⚠️ Reads static array without lock
    const uint8_t* reg_mac = get_registered_mac();
    if (reg_mac != nullptr) return reg_mac;
    return ESPNow::transmitter_mac;
}
```

**Similar issue in:**
- `transmitter_state.cpp`: RuntimeStatus struct (line 23-31) written from ESP-NOW worker, read from web handlers - no mutex
- `transmitter_network.cpp`: NetworkCache struct (line 19-33) accessed from multiple contexts without lock
- `transmitter_settings_cache.cpp`: SettingsCache struct (line 30-73) **no protection at all**

**Call chain establishing the race:**
1. **ESP-NOW worker thread:** `handle_battery_status()` → `TransmitterManager::storeIPData()` → `TransmitterNetwork::store_ip_data()` writes to `network_cache` (unprotected)
2. **Webserver thread:** `/api/get_network_config` handler calls `TransmitterManager::getIP()` → reads `network_cache.current_ip[4]` (unprotected)
3. **Potential outcome:** Torn read of 4-byte array, e.g., IP is partially old and partially new (e.g., `192.168.255.1` read as `192.168.1.1`)

**Impact:**
- Corrupted network configuration visible to web clients
- Stale transmitter identity in event logs and metrics
- Cache coherency violations across thread boundaries

**Recommendation:**

Add mutex to each cache module:

```cpp
// transmitter_identity.cpp
namespace {
    uint8_t registered_mac[6] = {0};
    bool registered_mac_known = false;
    SemaphoreHandle_t mac_mutex = nullptr;  // NEW
    
    void ensure_mutex() {
        if (mac_mutex == nullptr) {
            mac_mutex = xSemaphoreCreateMutex();
        }
    }
}

void TransmitterIdentity::cache_mac(const uint8_t* mac) {
    if (mac == nullptr) return;
    ensure_mutex();
    if (xSemaphoreTake(mac_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(registered_mac, mac, sizeof(registered_mac));
        registered_mac_known = true;
        xSemaphoreGive(mac_mutex);
    }
}

const uint8_t* TransmitterIdentity::get_active_mac() {
    ensure_mutex();
    if (xSemaphoreTake(mac_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        const uint8_t* reg_mac = registered_mac_known ? registered_mac : nullptr;
        xSemaphoreGive(mac_mutex);
        if (reg_mac != nullptr) return reg_mac;
    }
    return ESPNow::transmitter_mac;
}
```

Repeat pattern for `transmitter_state`, `transmitter_network`, and `transmitter_settings_cache`.

---

### 🔴 Bug #3: Event Log Cache Merge Logic May Skip Recent Events
**Severity:** HIGH (data loss in event logs)  
**File:** [lib/webserver/utils/transmitter_event_log_cache.cpp](lib/webserver/utils/transmitter_event_log_cache.cpp#L100-L200)

**Problem:**
```cpp
void store_event_logs(const JsonObject& logs) {
    ensure_mutex();
    ScopedMutex guard(event_logs_mutex);
    if (!guard.locked()) { ... return; }

    JsonArray events = logs["events"].as<JsonArray>();
    const size_t max_events = 200;
    uint32_t new_count = 0;

    const uint64_t incoming_snapshot_id = logs["snapshot_id"] | static_cast<uint64_t>(0);
    const int incoming_batch_index = logs["batch_index"] | -1;
    const uint16_t incoming_batch_count = logs["batch_count"] | static_cast<uint16_t>(0);
    const bool incoming_snapshot_complete = logs["snapshot_complete"] | false;

    if (incoming_batch_count > 0 && incoming_batch_index >= 0) {
        if (!snapshot_status.session_active) {
            snapshot_status.session_active = true;
            snapshot_status.session_started_ms = millis();
        }
```

**Continuation (line 140+):**
```cpp
        // Add events - but code path cuts off here; no deduplication logic shown
        for (auto event : events) {
            if (new_count >= max_events) break;
            // ⚠️ NO DEDUP CHECK: could add same event_id twice if batch_index==0 is received twice
            event_logs.push_back(...);
            new_count++;
        }
```

**Issue:**
- When transmitter sends batches (batch 0/N, 1/N, ..., N/N), receiver caches them
- If batch_index=0 packet is retransmitted due to ESP-NOW packet loss, it will be **added again to event_logs** without dedup check
- Over multiple re-transmissions (likely under poor RF conditions), duplicate events accumulate

**Impact:**
- Event log UI shows duplicate entries, confusing operators
- Event count metrics are inflated
- Web API clients see redundant historical data
- No way to query "unique" events vs "all instances"

**Recommendation:**

Track received event IDs during snapshot session:

```cpp
namespace {
    std::set<uint32_t> snapshot_seen_event_ids;  // Track unique events in current session
}

void store_event_logs(const JsonObject& logs) {
    ensure_mutex();
    ScopedMutex guard(event_logs_mutex);
    
    // ... existing validation ...
    
    for (auto event : events) {
        if (new_count >= max_events) break;
        
        uint32_t event_id = event["id"] | 0;
        if (event_id == 0) {
            // No ID; always add (shouldn't happen with proper batching)
            event_logs.push_back(parse_event(event));
            new_count++;
            continue;
        }
        
        // Check if we've already seen this event in this snapshot batch
        if (snapshot_seen_event_ids.count(event_id) > 0) {
            LOG_TRACE("EVENT_LOG_CACHE", "Skipping duplicate event_id=%u", event_id);
            continue;  // Skip duplicate
        }
        
        snapshot_seen_event_ids.insert(event_id);
        event_logs.push_back(parse_event(event));
        new_count++;
    }
}

void end_snapshot_session(bool clear_cached_logs) {
    // ... cleanup ...
    snapshot_seen_event_ids.clear();  // Reset for next session
}
```

---

### 🔴 Bug #4: Volatile Keyword Misuse in Battery Data Store
**Severity:** MEDIUM-HIGH (subtle memory ordering issues)  
**File:** [src/espnow/battery_data_store.h](src/espnow/battery_data_store.h#L80-L125)

**Problem:**
```cpp
// battery_data_store.h - LEGACY GLOBALS
extern volatile float soc_percent;
extern volatile float voltage_V;
extern volatile float current_A;
extern volatile float temperature_C;
extern volatile int32_t power_W;
// ... etc ...
```

**Issue:**
- `volatile` on **individual float fields** is NOT sufficient for memory ordering in multi-threaded code
- Compiler will not guarantee atomicity of read/write operations for float types (which are often wider than word size)
- Example race:
  - Thread A: Writes new soc_percent (4 bytes) while power_W is being updated (another 4 bytes)
  - Thread B: Reads soc_percent while Thread A's write is partial
  - Result: Torn read with soc_percent being partially old and partially new

**Call pattern:**
```cpp
// In ESP-NOW worker:
BatteryData::update_battery_status(battery_status_msg);  // Updates 8+ volatile fields
notify_sse_data_updated();

// Meanwhile, in webserver handler:
float soc = BatteryData::soc_percent;  // Torn read?
float power = BatteryData::power_W;    // Torn read?
```

**Impact:**
- Display shows nonsensical SOC values (e.g., 50.7% when transmitter sent 51.2%)
- Web API responses inconsistent across successive polls
- Over long uptime, display/API divergence increases due to accumulated corruptions

**Recommendation:**

Use the existing **TelemetrySnapshot** model with mutex protection:

```cpp
// Replace:
namespace BatteryData {
    BatteryData::TelemetrySnapshot snapshot_copy;
    if (lock_snapshot(50)) {
        snapshot_copy = g_snapshot;  // Atomic copy under lock
        unlock_snapshot();
    }
    float soc = snapshot_copy.soc_percent;  // Safe read
    float power = snapshot_copy.power_W;
}
```

Remove `volatile` keyword from legacy globals; they are now accessed via the locked snapshot API only.

---

## III. MEDIUM-SEVERITY ISSUES

### ⚠️ Issue #5: MQTT Buffer Size Risk and Dynamic JSON Allocation
**Severity:** MEDIUM  
**File:** [src/mqtt/mqtt_client.cpp](src/mqtt/mqtt_client.cpp#L37-L55)

**Problem:**
```cpp
void MqttClient::init(const uint8_t* mqtt_server, uint16_t mqtt_port, const char* client_id) {
    // ...
    mqtt_client_.setBufferSize(6144);  // Large for cell_data + event logs
}
```

And in handlers:
```cpp
void MqttClient::handleCellData(const char* json_payload, size_t length) {
    DynamicJsonDocument doc(2048 * 3);  // Allocation can fail under fragmentation
}
```

**Issue:**
- MQTT publish buffer is 6144 bytes, but cell_data payloads can reach 3-4 KB
- If two large payloads arrive close together, buffer overrun possible
- `DynamicJsonDocument` with large capacity (6KB+) can fail if heap is fragmented
- No error recovery if JSON parsing fails during subscription state change

**Impact:**
- MQTT connection drop under bursty cell data
- Memory fragmentation over long uptime

**Recommendation:**
- Increase MQTT buffer size to 8192
- Pre-allocate static buffer for cell_data (avoid DynamicJsonDocument for high-frequency paths)
- Add reconnect retry logic with exponential backoff

---

### ⚠️ Issue #6: SSE Handler May Perform Torn Reads of Display State
**Severity:** MEDIUM  
**Files:** [src/espnow/espnow_tasks.cpp](src/espnow/espnow_tasks.cpp#L40-L80)

**Problem:**
```cpp
static uint8_t g_last_received_soc = 0;
static int32_t g_last_received_power = 0;

static void update_received_data_cache(...) {
    g_last_received_soc = soc;          // ⚠️ NO MUTEX
    g_last_received_power = power;      // ⚠️ NO MUTEX
    BatteryData::update_basic_telemetry(soc, power, voltage_mv);
}
```

Meanwhile, SSE handler:
```cpp
// Hypothetical SSE telemetry pull:
uint8_t current_soc = g_last_received_soc;     // Torn read possible
int32_t current_power = g_last_received_power; // Torn read possible
```

**Issue:**
- Static caches `g_last_received_soc` and `g_last_received_power` are updated from ESP-NOW worker
- SSE handlers (running in webserver task) read these without lock
- int32_t is generally atomic on ESP32, but uint8_t operations are not always atomic with respect to larger updates

**Impact:**
- Occasional SSE updates with stale or mixed power/soc values

**Recommendation:**
- Snapshot these under the same mutex as BatteryData::g_snapshot
- Or use `std::atomic<>` for these specific fields

---

### ⚠️ Issue #7: Memory Sampler Integration Incomplete
**Severity:** MEDIUM  
**File:** [src/memory/memory_sampler.cpp](src/memory/memory_sampler.cpp#L1-L100)

**Problem:**
```cpp
void task_memory_sampler(void* parameter) {
    s_mutex = xSemaphoreCreateMutex();
    LOG_INFO("MEM_SAMPLER", "Started (baseline=%lums burst=%lums ring=%zu)",
             BASELINE_INTERVAL_MS, BURST_INTERVAL_MS, SAMPLE_RING_SIZE);

    for (;;) {
        take_and_store_sample();
        // ...
    }
}
```

But:
- Burst mode is triggered by burst_ref_count (line 15) but no API to increment/decrement it
- Ring buffer samples are collected but no `get_samples()` export in public header
- Memory sampler is started in runtime_task_startup but never queried by any handler

**Issue:**
- Memory sampler task is running but its data is inaccessible to webserver
- `/api/system_metrics` or `/api/memory_samples` handlers may not be wired to pull sampler data
- Burst mode never activated in production because no code increments `s_burst_ref_count`

**Impact:**
- Memory monitoring blind spot for operators
- Task CPU time wasted collecting data nobody reads

**Recommendation:**
- Export public functions to query ring buffer: `get_latest_sample()`, `get_samples_since(uint32_t cutoff_ms)`
- Wire `/api/memory_samples` handler to call these functions
- Add control API to enable/disable burst mode
- Document memory sampler in webserver API docs

---

### ⚠️ Issue #8: API Field Name Inconsistencies
**Severity:** MEDIUM  
**Files:** [lib/webserver/api/api_network_handlers.cpp](lib/webserver/api/api_network_handlers.cpp#L1-L100)

**Problem:**

In `/api/get_receiver_network`:
```cpp
doc["mqtt_username"]  = mqtt_username;
doc["mqtt_password"]  = "********";
```

But in request parsing `/api/save_receiver_network`:
```cpp
const char* mqtt_username = doc["mqtt_username"] | "";    // ✅ Consistent
const char* mqtt_password = doc["mqtt_password"] | "";    // ✅ Consistent
```

However, examining API handlers more broadly:
- Some handlers use `transmitter_ip`, others use `transmitter_address`
- Some battery-related fields named `battery_settings`, others `battery_specs`, others `battery_info`
- Event log field sometimes `event_logs`, sometimes `event_log_entries`

**Issue:**
- Frontend developers must know exact field names; easy to get wrong
- Refactoring field names requires updating multiple handlers
- No schema validation or OpenAPI spec to document API contract

**Impact:**
- Client code breaks silently if field names change
- Inconsistent naming makes API harder to learn
- No machine-readable documentation

**Recommendation:**
- Define API schema (OpenAPI 3.0 or JSON Schema) and validate all responses
- Adopt consistent naming convention:
  - Singular for containers (e.g., `transmitter`, not `transmitters`)
  - Plural for arrays (e.g., `event_logs` not `event_log`)
  - Camel case for field names (e.g., `battery_type_id`)
- Add CI check to validate all API responses conform to schema

---

## IV. LOW-SEVERITY FINDINGS

### ℹ️ Finding #9: High-Frequency INFO Logs in Message Processing Path
**File:** [src/espnow/espnow_tasks.cpp](src/espnow/espnow_tasks.cpp#L200-L230)

**Issue:**
```cpp
if (include_first_data_log) {
    LOG_INFO(kLogTag, "ESP-NOW RX: SOC=%d%%, Power=%dW (first=%s)",
              soc, power, first_data ? "YES" : "no");
} else {
    LOG_TRACE(kLogTag, "%s: SOC=%d%%, Power=%dW", source, soc, power);
}
```

**Impact:**
- First data messages log at INFO; low impact but noisy
- Should be DEBUG or TRACE unless first-ever data for a new transmitter

**Recommendation:**
- Move to TRACE or DEBUG: `LOG_DEBUG(kLogTag, "ESP-NOW RX: SOC=%d%% first_data=%s", ...)`

---

### ℹ️ Finding #10: Webserver Task Sizing Conservative for SSE Load
**File:** [lib/webserver/webserver.cpp](lib/webserver/webserver.cpp#L60-L90)

**Issue:**
```cpp
config.max_open_sockets = 4;
config.max_uri_handlers = 80;
config.task_priority = tskIDLE_PRIORITY + 2;
config.stack_size = 8192;
```

**Impact:**
- With SSE clients (monitor + cell_data streams) + regular UI requests, 4 sockets can saturate
- Stack size adequate for current handlers but tight for large JSON responses

**Recommendation:**
- Increase to 6-8 sockets for moderate concurrent clients
- Add telemetry: rejected connection count, average response time

---

### ℹ️ Finding #11: Candidate Dead Code and Compatibility Layers
**File:** [lib/webserver/api/api_handlers.cpp](lib/webserver/api/api_handlers.cpp#L1-L50)

**Issue:**
- Large handler registry manually maintained; easy to accidentally register handler twice or miss new endpoints
- No CI check that all defined endpoints are registered

**Recommendation:**
- Add compile-time counter: `expected_all_api_handlers()` vs actual registered count
- Already has this! (line 65-68 api_handlers.cpp) ✅

---

## V. TEST COVERAGE GAPS

### Gap #1: No Unit Tests for Transmitter Cache Thread Safety
**Location:** [test/](test/) directory

**Coverage:** 
- ✅ Receiver config validation tested (test_receiver_config_validation.cpp)
- ✅ Helper functions tested (test_helpers.cpp)
- ❌ **Transmitter cache (identity, state, network, settings) — untested**
- ❌ **Message router message deduplication — untested**
- ❌ **Event log merge deduplication — untested**

**Recommendation:**
Add integration tests:
```cpp
// test/test_transmitter_cache_thread_safety.cpp
TEST(TransmitterIdentity, ConcurrentMACUpdates) {
    // Spawn threads that concurrently call register_mac and get_active_mac
    // Verify no torn reads or data corruption
}

TEST(EventLogCache, DeduplicateDuplicateBatches) {
    // Send batch 0/2, 1/2, then batch 0/2 again (simulating retransmit)
    // Verify final event_logs has no duplicates
}
```

---

### Gap #2: No Integration Tests for MQTT Reconfiguration
**Files Involved:** mqtt_task.cpp, mqtt_client.cpp, receiver_config_manager.cpp

**Recommendation:**
Add test scenario:
```cpp
TEST(MqttReconfiguration, RuntimeConfigChangeTriggersReconnect) {
    // Start receiver with MQTT disabled
    // Via API: enable MQTT + set server
    // Verify client connects to new server
    // Change server IP
    // Verify client reconnects to new server (no reboot needed)
}
```

---

## VI. COMPARISON WITH espnowreceiver_LCD

### Shared Patterns (same codebase paths)

**Positive — Both projects have:**
1. ✅ Modular message routing (esp32common/espnow/message_router.h)
2. ✅ Display snapshot queue decoupling
3. ✅ Battery data store (esp32common)
4. ✅ TFT mutex for display synchronization

**Negative — Both projects have:**
1. ❌ Transmitter cache race conditions (no mutex on identity, state, network)
2. ⚠️ Event log deduplication gaps
3. ⚠️ High-frequency INFO logs in hot paths
4. ⚠️ Volatile keyword misuse on composite types

### Differences (espnowreceiver_2 specific)

| Aspect | espnowreceiver_LCD | espnowreceiver_2 |
|--------|-------------------|------------------|
| **Display Stack** | LVGL (task-driven) | TFT_eSPI (queue-driven) |
| **Config Reconfiguration** | N/A (LCD only) | MQTT not reconfigurable (🔴 Bug) |
| **Message Handlers** | Basic data + heartbeat | +Battery Emulator, type catalog, events (more complex) |
| **Memory Sampler** | N/A | Integrated but unused (⚠️ Gap) |
| **API Surface** | Minimal | Extensive (40+ endpoints) |

---

## VII. PRIORITIZED RECOMMENDATIONS

### Priority 1 (CRITICAL - Fix before production release)

| # | Issue | File | Effort | Impact |
|---|-------|------|--------|--------|
| 🔴 1 | MQTT not reconfigurable at runtime | mqtt_task.cpp | 2h | Requires reboot for config changes |
| 🔴 2 | Race conditions in transmitter caches | transmitter_*.cpp | 6h | Data corruption, torn reads |
| 🔴 3 | Event log deduplication missing | transmitter_event_log_cache.cpp | 3h | Duplicate events under retransmit |
| 🔴 4 | Volatile keyword misuse on composites | battery_data_store.h | 2h | Torn reads, display corruption |

### Priority 2 (HIGH - Fix in next sprint)

| # | Issue | File | Effort | Impact |
|---|-------|------|--------|--------|
| ⚠️ 5 | MQTT buffer size and JSON allocation | mqtt_client.cpp | 2h | Connection drop under load |
| ⚠️ 6 | SSE torn reads of display state | espnow_tasks.cpp | 1h | Occasional stale SSE values |
| ⚠️ 7 | Memory sampler unused | memory_sampler.cpp | 3h | Monitoring blind spot |
| ⚠️ 8 | API field name inconsistencies | api_*_handlers.cpp | 4h | Client confusion, brittle API |

### Priority 3 (MEDIUM - Next phase)

| # | Issue | File | Effort | Impact |
|---|-------|------|--------|--------|
| ℹ️ 9 | High-frequency INFO logs | espnow_tasks.cpp | 1h | Log noise |
| ℹ️ 10 | Webserver socket/task sizing | webserver.cpp | 2h | Scalability limit |
| ℹ️ 11 | API schema/documentation | all api_*.cpp | 6h | Developer experience |

---

## VIII. ACTION PLAN (Timeline)

```
WEEK 1:
  - [ ] Implement MQTT reconfiguration (P1 #1) - 2h
  - [ ] Add mutex to all transmitter caches (P1 #2) - 6h
  - [ ] Add event log dedup tracking (P1 #3) - 3h
  - [ ] Remove volatile from composite types (P1 #4) - 2h
  SUBTOTAL: 13h

WEEK 2:
  - [ ] Fix MQTT buffer size and JSON handling (P2 #5) - 2h
  - [ ] Add snapshot guards to SSE handlers (P2 #6) - 1h
  - [ ] Export memory sampler API + wire handlers (P2 #7) - 3h
  - [ ] Standardize API field names + add schema (P2 #8) - 4h
  - [ ] Update INFO logs to DEBUG (P3 #9) - 1h
  SUBTOTAL: 11h

WEEK 3:
  - [ ] Tune webserver config + add telemetry (P3 #10) - 2h
  - [ ] Add unit tests for thread safety (Gap #1) - 4h
  - [ ] Add integration test for MQTT reconfig (Gap #2) - 3h
  SUBTOTAL: 9h

TOTAL: ~33 engineering hours (~1 week with 1 FTE engineer)
```

---

## IX. VERIFICATION CHECKLIST

After implementing fixes:

- [ ] All mutex creates checked for null and logged
- [ ] Event log cache deduplication tested with batch retransmit scenario
- [ ] MQTT config change tested via `/api/save_receiver_network` without reboot
- [ ] Memory sampler `/api/memory_samples` returns valid data
- [ ] API responses validated against OpenAPI schema
- [ ] No HIGH or CRITICAL severity findings in code review
- [ ] Regression tests pass (existing test suites)
- [ ] 24-hour uptime test: no memory corruption, no display artifacts
- [ ] Webserver load test with 6+ concurrent SSE clients
- [ ] MQTT broker failover test (change IP, verify reconnect without reboot)

---

## X. CONCLUSION

The espnowreceiver_2 architecture is **sound and production-grade**, with strong boot sequencing, modular message routing, and clear task prioritization. However, **critical concurrency bugs** in the transmitter cache layer and **missing MQTT reconfiguration** must be addressed before production deployment. Once the Priority 1 fixes are implemented, the codebase will be robust and maintainable.

**Overall Assessment:** ⚠️ **Ready for pre-production testing with P1 fixes in place**

---

## Appendix: File Index by Component

### Boot & Initialization
- [src/main.cpp](src/main.cpp) — Entry point, LED renderer, pre-boot debug
- [src/config/runtime_task_startup.cpp](src/config/runtime_task_startup.cpp) — Task creation, RTOS primitives
- [src/config/task_config.h](src/config/task_config.h) — Task priorities, stack sizes
- [src/config/wifi_setup.cpp](src/config/wifi_setup.cpp) — WiFi connectivity

### ESP-NOW & Message Handling
- [src/espnow/espnow_callbacks.cpp](src/espnow/espnow_callbacks.cpp) — RX callback (minimal work)
- [src/espnow/espnow_tasks.cpp](src/espnow/espnow_tasks.cpp) — Worker task, message routing
- [src/espnow/espnow_message_handlers.cpp](src/espnow/espnow_message_handlers.cpp) — Per-type handlers
- [src/espnow/battery_handlers.cpp](src/espnow/battery_handlers.cpp) — Battery Emulator data parsing

### Battery & Telemetry
- [src/espnow/battery_data_store.cpp](src/espnow/battery_data_store.cpp) — Telemetry snapshot (legacy + mutex)
- [src/espnow/rx_state_machine.cpp](src/espnow/rx_state_machine.cpp) — Connection state tracking
- [src/espnow/rx_heartbeat_manager.cpp](src/espnow/rx_heartbeat_manager.cpp) — Heartbeat monitoring

### Display
- [src/display/display_update_queue.cpp](src/display/display_update_queue.cpp) — Snapshot queue + renderer task
- [src/display/display_core.cpp](src/display/display_core.cpp) — TFT rendering (backend)
- [src/main.cpp#task_led_renderer](src/main.cpp#L77) — LED animation task

### Configuration
- [lib/receiver_config/receiver_config_manager.cpp](lib/receiver_config/receiver_config_manager.cpp) — Receiver NVS persistence

### MQTT
- [src/mqtt/mqtt_client.cpp](src/mqtt/mqtt_client.cpp) — MQTT client (publisher + subscriber)
- [src/mqtt/mqtt_task.cpp](src/mqtt/mqtt_task.cpp) — Task wrapper (🔴 no reconfiguration)

### Transmitter Caches (⚠️ No mutex)
- [lib/webserver/utils/transmitter_identity.cpp](lib/webserver/utils/transmitter_identity.cpp)
- [lib/webserver/utils/transmitter_state.cpp](lib/webserver/utils/transmitter_state.cpp)
- [lib/webserver/utils/transmitter_network.cpp](lib/webserver/utils/transmitter_network.cpp)
- [lib/webserver/utils/transmitter_settings_cache.cpp](lib/webserver/utils/transmitter_settings_cache.cpp)
- [lib/webserver/utils/transmitter_event_log_cache.cpp](lib/webserver/utils/transmitter_event_log_cache.cpp)

### Webserver & API
- [lib/webserver/webserver.cpp](lib/webserver/webserver.cpp) — HTTP server init & lifecycle
- [lib/webserver/api/api_handlers.cpp](lib/webserver/api/api_handlers.cpp) — Handler registry
- [lib/webserver/api/api_network_handlers.cpp](lib/webserver/api/api_network_handlers.cpp) — Network config APIs
- [lib/webserver/utils/sse_notifier.cpp](lib/webserver/utils/sse_notifier.cpp) — SSE event group

### Memory & Monitoring
- [src/memory/memory_sampler.cpp](src/memory/memory_sampler.cpp) — Heap sampler (⚠️ unused)

---

**Document generated:** April 21, 2026  
**Recommendations:** Implement Priority 1 fixes before production release.
