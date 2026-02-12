# SECTION 11: REVISED ARCHITECTURE - Transmitter-Active with Bidirectional Sync

**Date:** February 11, 2026  
**Status:** SUPERSEDES Section 10 (Receiver-Master) in ESPNOW_CHANNEL_COMPREHENSIVE_REVIEW.md  
**Rationale:** Simplifies architecture, leverages existing versioning system, provides bidirectional sync

---

## 11.1 Architecture Overview

**Key Principle**: Transmitter actively broadcasts PROBE channel-by-channel. Receiver (on correct WiFi channel) listens and responds when PROBE received. Both devices maintain versioned caches of static configuration and sync bidirectionally.

## 11.2 Core Architecture Changes

| Aspect | Section 10 (Receiver-Master) | **Section 11 (Transmitter-Active)** |
|--------|------------------------------|-------------------------------------|
| **Discovery Initiator** | Receiver broadcasts PROBE | **Transmitter broadcasts PROBE** |
| **Receiver Role** | Active broadcaster | **Passive listener** (on WiFi channel) |
| **Transmitter Role** | Passive scanner | **Active channel-by-channel broadcaster** |
| **Channel Hopping** | Transmitter scans 1-13 | **Transmitter broadcasts on 1-13** |
| **Boot Order** | No dependency | **No dependency** (same benefit) |
| **Discovery Speed** | 6s per channel (78s max) | **1s per channel (13s max)** |
| **Data Caching** | Static cache, flush on connect | **Versioned cache, sync on connect** |
| **Config Sync** | One-way (TX→RX) | **Bidirectional (TX↔RX)** |
| **Keep-alive** | Not specified | **Active monitoring with failure recovery** |

## 11.3 Detailed Architecture Flow

### 11.3.1 Receiver Boot Sequence

```
┌─────────────────────────────────────────────────────────────────┐
│ RECEIVER - Passive Discovery, Active Config Sync               │
│ ┌─────────────────────────────────────────────────────────────┐ │
│ │ 1. Boot → WiFi.begin(SSID, password)                        │ │
│ │ 2. WiFi connects → Get channel from router (e.g., ch 6)     │ │
│ │ 3. ESP-NOW init on channel 6 (same as WiFi STA)            │ │
│ │ 4. Start listening for PROBE broadcasts on channel 6        │ │
│ │ 5. Initialize versioned config cache (IP, MQTT, settings)   │ │
│ │ 6. When PROBE received:                                     │ │
│ │    ├─ Send ACK response to transmitter                      │ │
│ │    ├─ Register transmitter peer on channel 6                │ │
│ │    ├─ Start keep-alive monitoring (expect data every 30s)   │ │
│ │    └─ Begin bidirectional version sync                      │ │
│ │ 7. On config change (IP, MQTT, settings):                   │ │
│ │    ├─ Increment version number in cache                     │ │
│ │    └─ Send CONFIG_CHANGED message to transmitter           │ │
│ └─────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

### 11.3.2 Transmitter Boot Sequence

```
┌─────────────────────────────────────────────────────────────────┐
│ TRANSMITTER - Active Discovery, Parallel Services               │
│ ┌─────────────────────────────────────────────────────────────┐ │
│ │ PHASE 1: Network Initialization (0-10s)                     │ │
│ │ ├─ 1. Boot → Ethernet init → Get IP via DHCP/Static        │ │
│ │ ├─ 2. Start MQTT connection (if enabled)                    │ │
│ │ ├─ 3. Initialize OTA server                                 │ │
│ │ └─ 4. Initialize versioned config cache                     │ │
│ │                                                              │ │
│ │ PHASE 2: ESP-NOW Discovery (Parallel with Phase 1)         │ │
│ │ ├─ 1. WiFi.mode(WIFI_STA) → Start on channel 1             │ │
│ │ ├─ 2. ESP-NOW init                                          │ │
│ │ ├─ 3. Start active channel hopping task:                    │ │
│ │ │   FOR EACH channel in [1..13]:                            │ │
│ │ │   ├─ WiFi.setChannel(channel)                             │ │
│ │ │   ├─ Remove old broadcast peer (if exists)                │ │
│ │ │   ├─ Add broadcast peer on current channel (EXPLICIT)     │ │
│ │ │   ├─ Send PROBE (seq = random())                          │ │
│ │ │   ├─ Wait 1000ms for ACK                                  │ │
│ │ │   └─ If ACK received:                                     │ │
│ │ │       ├─ Lock to channel (g_lock_channel = channel)       │ │
│ │ │       ├─ Register receiver peer on locked channel         │ │
│ │ │       ├─ Flush versioned cache → receiver                 │ │
│ │ │       └─ START keep-alive monitoring → CONNECTED          │ │
│ │ │   ELSE:                                                    │ │
│ │ │       └─ Continue to next channel                         │ │
│ │ │   END FOR                                                  │ │
│ │ │   LOOP UNTIL CONNECTED (continuous channel hopping)       │ │
│ │ └─ 4. Once connected: Stop channel hopping task             │ │
│ │                                                              │ │
│ │ PHASE 3: Data Collection (During Phase 1 & 2)              │ │
│ │ ├─ Battery data collected every 1s                          │ │
│ │ ├─ Data added to versioned cache (if not connected)         │ │
│ │ └─ Data sent normally (if connected)                        │ │
│ │                                                              │ │
│ │ PHASE 4: Connected Operation                                │ │
│ │ ├─ Send battery data every 1s via ESP-NOW                   │ │
│ │ ├─ Send keep-alive heartbeat every 10s                      │ │
│ │ ├─ Monitor receiver response (timeout = 30s)                │ │
│ │ ├─ On config change: Send CONFIG_CHANGED message            │ │
│ │ └─ If keep-alive timeout:                                   │ │
│ │     ├─ Enter FAILURE_MODE (grace period = 60s)              │ │
│ │     ├─ Continue attempting ESP-NOW sends (retry)            │ │
│ │     └─ If still no response after 60s:                      │ │
│ │         ├─ Reset ESP-NOW connection                         │ │
│ │         └─ RESTART channel hopping (back to Phase 2)        │ │
│ └─────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

## 11.4 Versioned Cache System

### 11.4.1 Cache Structure

**NOTE**: This is the original cache structure. See Section 11.13.5 for enhanced cache-centric structure with transient/state data separation and 250-entry queue for dual battery support.

```cpp
struct VersionedConfigCache {
    // Static configuration items (incrementing versions)
    struct {
        uint16_t version;
        char ip[16];
        char gateway[16];
        char subnet[16];
        bool is_dhcp;
    } network_config;
    
    struct {
        uint16_t version;
        char server[64];
        uint16_t port;
        char username[32];
        char password[32];
        bool enabled;
        bool connected;
    } mqtt_config;
    
    struct {
        uint16_t version;
        uint32_t capacity_wh;
        uint16_t nominal_voltage;
        uint8_t cell_count;
        uint16_t max_voltage;
        uint16_t min_voltage;
        float max_charge_current;
        float max_discharge_current;
    } battery_settings;
    
    // Battery data queue (FIFO circular buffer)
    // SIZING: 250 entries for dual battery (192 cells = 96 × 2) + headroom
    struct {
        espnow_payload_t data[250];  // Increased for dual battery support
        uint8_t write_idx;
        uint8_t read_idx;
        uint8_t count;
    } data_queue;
};
```

### 11.4.2 Version Sync Protocol

**Message Type: CONFIG_CHANGED (0x30)**
```cpp
struct config_changed_t {
    uint8_t type;           // msg_config_changed = 0x30
    uint8_t config_type;    // 0=network, 1=mqtt, 2=battery
    uint16_t new_version;   // Version number after change
};
```

**Flow Example (Transmitter MQTT config changes)**:
```
T0: User changes MQTT server on transmitter web UI
T1: Transmitter increments mqtt_config.version (5 → 6)
T2: Transmitter persists to NVS (TX-only persistence)
T3: Transmitter sends CONFIG_CHANGED(type=1, version=6)
T4: Receiver receives CONFIG_CHANGED
T5: Receiver checks cached version (5) < received (6)
T6: Receiver sends REQUEST_CONFIG(type=1)
T7: Transmitter sends full mqtt_config (version=6)
T8: Receiver updates RAM cache → version 6 (NO NVS write)
```

**Flow Example (Receiver initiates MQTT config change)**:
```
T0: User changes MQTT password on receiver web UI
T1: Receiver increments mqtt_config.version (5 → 6) in RAM
T2: Receiver sends CONFIG_CHANGED(type=1, version=6) to TX
T3: Transmitter receives CONFIG_CHANGED, checks version (5 < 6)
T4: Transmitter sends REQUEST_CONFIG(type=1)
T5: Receiver sends full mqtt_config (version=6)
T6: Transmitter updates cache → version 6
T7: Transmitter persists to NVS (TX-only persistence)
T8: Both synchronized: RX in RAM (v6), TX in NVS+RAM (v6)
```

## 11.5 Keep-Alive Mechanism

### 11.5.1 Heartbeat Messages

**Transmitter → Receiver (every 10s)**:
```cpp
struct heartbeat_t {
    uint8_t type;           // msg_heartbeat = 0x31
    uint32_t timestamp;     // millis() timestamp
    uint8_t services;       // Bitfield: bit0=MQTT, bit1=Ethernet
};
```

**Receiver → Transmitter (every 10s)**:
```cpp
struct heartbeat_ack_t {
    uint8_t type;           // msg_heartbeat_ack = 0x32
    uint32_t timestamp;     // Echo transmitter timestamp
    uint8_t services;       // Bitfield: bit0=WiFi connected
};
```

### 11.5.2 Failure Detection & Recovery

**State Machine**:
```
CONNECTED ──(no heartbeat for 30s)──> DEGRADED
    │                                      │
    │                                      │(retry 6x, wait 10s each)
    │                                      │
    │                                      ▼
    │                                  FAILURE_MODE
    │                                      │
    │                                      │(grace period 60s)
    │                                      │
    │                                      ▼
    │                                  DISCONNECTED
    │                                      │
    │                                      │(restart channel hopping)
    │                                      │
    └──────(ACK received)────────────────┘
```

**Timing Parameters**:
- **Heartbeat interval**: 10s (both devices)
- **Missed heartbeat threshold**: 3 consecutive (30s total)
- **Degraded mode retries**: 6 attempts × 10s = 60s grace period
- **Disconnection trigger**: No response after 90s total (30s + 60s)
- **Channel hop restart**: Resume 1s-per-channel PROBE broadcasts

### 11.5.3 Receiver Reboot Detection

**Scenario**: Receiver reboots while transmitter continues running

```
T0:  Receiver reboots → WiFi reconnects → Channel 6
T1:  Receiver ESP-NOW init → Listening on channel 6
T2:  Transmitter sends heartbeat → No ACK (receiver not ready)
T3:  ... (retries 30s, no response)
T4:  Transmitter enters DEGRADED mode
T5:  ... (retries 60s, still no response)
T6:  Transmitter enters DISCONNECTED → Restart channel hopping
T7:  Transmitter broadcasts PROBE on ch 1, 2, 3, 4, 5...
T8:  Transmitter broadcasts PROBE on ch 6 → Receiver ACKs ✓
T9:  Connection re-established → Flush cached data
```

### 11.5.4 Router Channel Change Detection

**Scenario**: WiFi router changes from channel 6 → channel 11

```
T0:  Router admin changes WiFi channel 6 → 11
T1:  Receiver WiFi disconnects (old channel 6 invalid)
T2:  Receiver WiFi reconnects → New channel 11
T3:  Receiver ESP-NOW updates to channel 11 automatically
T4:  Transmitter still on channel 6 → Heartbeat fails
T5:  Transmitter enters DEGRADED (30s missed heartbeats)
T6:  Transmitter enters FAILURE_MODE (60s retries)
T7:  Transmitter enters DISCONNECTED → Restart channel hopping
T8:  Transmitter broadcasts PROBE on ch 1, 2, 3...
T9:  Transmitter broadcasts PROBE on ch 11 → Receiver ACKs ✓
T10: Connection re-established on channel 11
```

**Recovery Time**: Max 90s (failure detection) + 11s (channel hopping to ch 11) = **~100s total**

## 11.6 Boot Order Scenarios

### Scenario A: Receiver Boots First ✅
```
T0:  Receiver boots → WiFi channel 6 → Listening
T10: Transmitter boots → Starts channel hopping on ch 1
T11: Transmitter ch 2, ch 3, ch 4, ch 5 (no response)
T15: Transmitter ch 6 → Sends PROBE → Receiver ACKs ✓
T16: Connection established (5s discovery time)
```

### Scenario B: Transmitter Boots First ✅
```
T0:  Transmitter boots → Ethernet/MQTT start → Channel hopping begins
T1:  Transmitter ch 1 → PROBE (no receiver) → ch 2
T2:  Transmitter ch 2 → PROBE (no receiver) → ch 3
...
T13: Transmitter ch 13 → PROBE (no receiver) → LOOP ch 1
T14: Transmitter ch 1 → PROBE (no receiver) → ch 2
...
T30: Receiver boots → WiFi channel 6 → Listening
T31: Transmitter on ch 4 (hopping) → ch 5
T36: Transmitter ch 6 → Sends PROBE → Receiver ACKs ✓
T37: Connection established (36s discovery time - acceptable)
```

### Scenario C: Both Boot Simultaneously ✅
```
T0:  Both devices boot
T5:  Receiver WiFi connected → channel 6 → Listening
T5:  Transmitter still initializing Ethernet
T10: Transmitter starts channel hopping → ch 1
T15: Transmitter ch 6 → Sends PROBE → Receiver ACKs ✓
T16: Connection established (16s discovery time)
```

## 11.7 Implementation Changes Required

### 11.7.1 Transmitter Changes

**File: `discovery_task.cpp`** (MODIFY existing)
```cpp
// Change from passive scanning to active channel hopping
void DiscoveryTask::start_active_channel_hopping() {
    LOG_INFO("[DISCOVERY] Starting ACTIVE channel hopping");
    
    xTaskCreate(
        active_channel_hopping_task,
        "channel_hop",
        4096,
        this,
        task_config::PRIORITY_NORMAL,
        &task_handle_
    );
}

void DiscoveryTask::active_channel_hopping_task(void* param) {
    const uint8_t channels[] = {1,2,3,4,5,6,7,8,9,10,11,12,13};
    const TickType_t CHANNEL_DELAY = pdMS_TO_TICKS(1000);  // 1s per channel
    
    while (!EspnowMessageHandler::instance().is_receiver_connected()) {
        for (uint8_t ch : channels) {
            // Set channel
            WiFi.setChannel(ch);
            
            // CRITICAL: Remove old broadcast peer before adding new one
            if (esp_now_is_peer_exist(BROADCAST_MAC)) {
                esp_now_del_peer(BROADCAST_MAC);
            }
            
            // Add broadcast peer with EXPLICIT channel
            esp_now_peer_info_t peer{};
            memcpy(peer.peer_addr, BROADCAST_MAC, 6);
            peer.channel = ch;  // EXPLICIT CHANNEL
            peer.ifidx = WIFI_IF_STA;
            esp_now_add_peer(&peer);
            
            // Send PROBE
            probe_t probe = { msg_probe, (uint32_t)esp_random() };
            esp_now_send(BROADCAST_MAC, (uint8_t*)&probe, sizeof(probe));
            
            LOG_DEBUG("[DISCOVERY] PROBE sent on channel %d", ch);
            
            // Wait for ACK (with timeout)
            vTaskDelay(CHANNEL_DELAY);
            
            // If ACK received, exit loop (checked via message handler)
            if (EspnowMessageHandler::instance().is_receiver_connected()) {
                g_lock_channel = ch;
                LOG_INFO("[DISCOVERY] ✓ Connected on channel %d", ch);
                
                // Flush cached data
                DataCache::instance().flush();
                return;  // Task done
            }
        }
        LOG_DEBUG("[DISCOVERY] Full channel sweep complete, restarting...");
    }
}
```

**File: `data_cache.cpp`** (MODIFY for versioning)
```cpp
// Add versioning support to cache
void DataCache::add_versioned(const espnow_payload_t& data, uint16_t version) {
    // Store data with version number
    cache_[write_idx_].data = data;
    cache_[write_idx_].version = version;
    // ... FIFO logic
}

void DataCache::flush() {
    // Send all cached data in order, including version info
    for (size_t i = 0; i < count_; i++) {
        // Send with version metadata for receiver validation
        esp_now_send(receiver_mac, (uint8_t*)&cache_[read_idx_], sizeof(cache_[read_idx_]));
        vTaskDelay(pdMS_TO_TICKS(50));  // Rate limit
    }
}
```

**File: `keep_alive_manager.cpp`** (NEW)
```cpp
class KeepAliveManager {
public:
    void start();
    void send_heartbeat();
    void on_heartbeat_ack_received();
    bool is_connection_healthy();
    
private:
    enum State { CONNECTED, DEGRADED, FAILURE_MODE, DISCONNECTED };
    State state_ = DISCONNECTED;
    uint32_t last_ack_time_ = 0;
    uint8_t missed_heartbeats_ = 0;
    
    static const uint32_t HEARTBEAT_INTERVAL_MS = 10000;  // 10s
    static const uint8_t MISSED_THRESHOLD = 3;            // 30s
    static const uint32_t GRACE_PERIOD_MS = 60000;        // 60s
    
    void check_connection_health();
    void handle_failure();
};
```

### 11.7.2 Receiver Changes

**File: `espnow_tasks.cpp`** (MODIFY)
```cpp
// Remove active PROBE broadcasting (EspnowDiscovery)
// Receiver is now PASSIVE - just listens and responds to transmitter PROBE

// Keep ACK response logic
probe_config.on_probe_received = [](const uint8_t* mac, uint32_t seq) {
    LOG_DEBUG("[DISCOVERY] PROBE received from TX, sending ACK");
    
    // Send ACK
    ack_t ack = { msg_ack, seq, WiFi.channel() };
    esp_now_send(mac, (uint8_t*)&ack, sizeof(ack));
    
    // Register transmitter peer
    EspnowPeerManager::add_peer(mac, WiFi.channel());
    
    // Start keep-alive monitoring
    KeepAliveManager::instance().on_connection_established();
};
```

**File: `config_sync_manager.cpp`** (NEW)
```cpp
class ConfigSyncManager {
public:
    void on_config_changed_received(uint8_t config_type, uint16_t version);
    void send_config_changed(uint8_t config_type);
    void request_config(uint8_t config_type);
    
private:
    VersionedConfigCache cache_;
    
    void handle_version_mismatch(uint8_t config_type, uint16_t remote_version);
};
```

## 11.8 Message Protocol Extensions

### 11.8.1 ESP-NOW Non-Blocking Architecture

**CRITICAL REQUIREMENT**: ALL ESP-NOW operations must NOT block or interfere with Battery Emulator's critical control code (CAN communication, BMS monitoring, charger control, safety checks).

**Battery Emulator Critical Functions** (MUST NOT be blocked):
- CAN bus communication (battery status, control messages)
- BMS communication (cell voltage, temperature monitoring)
- Charger communication (charge current, voltage limits)
- Safety checks (over-voltage, under-voltage, over-temperature)
- Emergency shutdown logic
- Contactor control

**ESP-NOW Isolation Strategy**:

```cpp
// 1. LOW PRIORITY TASKS - ESP-NOW runs in background
#define TASK_PRIORITY_CRITICAL_CONTROL  5  // CAN, BMS, Safety (Battery Emulator)
#define TASK_PRIORITY_NORMAL_CONTROL    4  // Charger, Contactors
#define TASK_PRIORITY_ESPNOW_LOW        2  // ESP-NOW tasks (background only)
#define TASK_PRIORITY_ESPNOW_CLEANUP    1  // Cache cleanup (idle only)

// Discovery task - runs at low priority, yields to control code
xTaskCreatePinnedToCore(
    active_channel_hopping_task,
    "espnow_disc",
    4096,
    nullptr,
    TASK_PRIORITY_ESPNOW_LOW,  // LOW - doesn't preempt control code
    &discovery_task_,
    1  // Pin to Core 1 (Core 0 reserved for critical control)
);

// Data transmission task - low priority, non-blocking
xTaskCreatePinnedToCore(
    cache_transmission_task,
    "espnow_tx",
    4096,
    nullptr,
    TASK_PRIORITY_ESPNOW_LOW,  // LOW - yields to CAN/BMS
    &data_tx_task_,
    1  // Pin to Core 1
);
```

**2. NON-BLOCKING ESP-NOW SENDS**:
```cpp
// WRONG: Blocking send (waits for ACK, delays control code)
esp_err_t result = esp_now_send(mac, data, len);
while (!ack_received) {  // ❌ BLOCKS control code!
    vTaskDelay(1);
}

// CORRECT: Fire-and-forget send (non-blocking)
void send_battery_data_nonblocking(const espnow_payload_t& data) {
    // Just add to cache - return immediately
    cache.add(data, millis(), seq++);
    
    // Background task handles actual transmission
    // Control code continues without delay
}

// Background task sends when ready (doesn't block main)
void cache_transmission_task() {
    while (true) {
        if (cache.has_data() && is_connected()) {
            CacheEntry* entry = cache.peek();
            
            // Non-blocking send (returns immediately)
            esp_now_send(receiver_mac, &entry->data, sizeof(entry->data));
            
            cache.mark_sent(entry->seq);
            // Don't wait for ACK here - callback handles it
        }
        
        // Yield to higher priority tasks (CAN, BMS)
        vTaskDelay(pdMS_TO_TICKS(50));  // 20 Hz max send rate
    }
}
```

**3. FAST ESP-NOW CALLBACKS** (No blocking in ISR context):
```cpp
// ESP-NOW receive callback - MUST be fast (ISR context)
void on_espnow_recv(const uint8_t* mac, const uint8_t* data, int len) {
    // ✅ CORRECT: Quick copy to queue, return immediately
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(espnow_rx_queue, data, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    
    // ❌ WRONG: Don't do heavy processing here
    // ❌ WRONG: Don't call esp_now_send() from callback
    // ❌ WRONG: Don't use delays or blocking calls
}

// ESP-NOW send callback - MUST be fast
void on_espnow_send(const uint8_t* mac, esp_now_send_status_t status) {
    // ✅ CORRECT: Just set flag, return immediately
    if (status == ESP_NOW_SEND_SUCCESS) {
        xSemaphoreGiveFromISR(ack_semaphore, nullptr);
    }
    
    // Processing done in background task, not callback
}
```

**4. THREAD-SAFE CACHE** (Non-blocking for control code):
```cpp
// Cache uses mutex with timeout (doesn't block indefinitely)
bool EnhancedCache::add(const espnow_payload_t& data, uint32_t timestamp, uint32_t seq) {
    // Try to acquire mutex with timeout
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        // Quick add operation
        transient_queue_[write_idx_] = {data, timestamp, seq};
        write_idx_ = (write_idx_ + 1) % 250;
        count_++;
        
        xSemaphoreGive(mutex_);
        return true;
    }
    
    // If can't get mutex quickly, drop data (don't block control code)
    LOG_WARN("[CACHE] Mutex timeout, data dropped (control code priority)");
    return false;
}
```

**5. MAIN LOOP INTEGRATION** (Battery Emulator):
```cpp
void loop() {
    // CRITICAL: Control code runs first, always
    update_CAN_communication();        // High priority
    update_BMS_readings();             // High priority
    check_safety_limits();             // High priority
    control_contactors();              // High priority
    
    // ESP-NOW data collection (non-blocking)
    collect_battery_data_for_espnow(); // Just writes to cache (fast)
    
    // NO esp_now_send() in main loop!
    // Background task handles transmission
}

void collect_battery_data_for_espnow() {
    espnow_payload_t data;
    
    // Populate from Battery Emulator variables (fast)
    data.voltage = BATTERY.Voltage;
    data.current = BATTERY.Current;
    data.soc = BATTERY.SOC;
    // ... copy data fields
    
    // Add to cache (non-blocking, fast)
    cache.add(data, millis(), seq++);
    
    // Return immediately - control code continues
}
```

**6. CORE PINNING** (Isolate ESP-NOW from critical control):
```cpp
// Core 0: Critical Battery Emulator control code
// - CAN communication (runs on Core 0)
// - BMS monitoring (runs on Core 0)
// - Safety checks (runs on Core 0)

// Core 1: ESP-NOW communication (isolated)
xTaskCreatePinnedToCore(espnow_task, "espnow", 4096, nullptr, 2, nullptr, 1);

// This prevents ESP-NOW from interfering with CAN/BMS timing
```

**Guaranteed Control Code Priority**:
- ✅ **ESP-NOW tasks run at low priority** (priority 2 vs control priority 5)
- ✅ **No blocking sends in main loop** (fire-and-forget to cache)
- ✅ **Fast callbacks** (< 1ms, return to ISR immediately)
- ✅ **Non-blocking mutex** (10ms timeout, drops data if busy)
- ✅ **Core isolation** (ESP-NOW on Core 1, Control on Core 0)
- ✅ **Rate limiting** (Max 20 msg/sec, prevents CPU saturation)
- ✅ **CAN/BMS always preempt ESP-NOW** (FreeRTOS priority scheduling)

**Timing Impact Analysis**:
- Cache write: **< 100µs** (mutex + memory copy)
- Control code impact: **< 0.1ms per loop** (negligible)
- CAN timing: **Unaffected** (higher priority, different core)
- BMS timing: **Unaffected** (higher priority, different core)
- Safety checks: **Unaffected** (highest priority)

### 11.8.2 Message Type Definitions

**New Message Types**:
```cpp
// Add to espnow_common.h
enum espnow_message_type_t {
    // ... existing types
    msg_heartbeat = 0x31,           // Keep-alive heartbeat
    msg_heartbeat_ack = 0x32,       // Heartbeat acknowledgment
    msg_config_changed = 0x33,      // Config item changed notification
    msg_request_config = 0x34,      // Request full config
    msg_config_response = 0x35,     // Full config data response
};
```

## 11.9 Critical Issues & Solutions

### Issue 1: Broadcast Peer Channel Persistence ✅ SOLVED
**Problem**: Stale broadcast peer survives restart with wrong channel

**Solution**: Delete old broadcast peer before adding new one on each channel hop

### Issue 2: Cache Overflow ✅ SOLVED
**Problem**: Cache fills during long disconnection (>100 messages)

**Solution**: FIFO circular buffer with overflow handling (oldest messages dropped)

### Issue 3: Heartbeat Overhead ✅ ACCEPTABLE
**Impact**: 0.8% bandwidth overhead (20 bytes/10s)

**Verdict**: Negligible for battery monitoring application

### Issue 4: Channel Hopping Interference ✅ NO ISSUE
**Analysis**: Transmitter uses WiFi STA for ESP-NOW only (Ethernet for connectivity)

**Verdict**: No interference risk

### Issue 5: Keep-Alive Timeout Tuning ✅ CONFIGURABLE
**Recommendation**: Conservative defaults (90s), configurable via settings (30-120s range)

## 11.10 Testing Strategy

### Test Case 1: Boot Order Independence
- Boot receiver only (30s wait) → Boot transmitter → Connection within 13s ✓
- Both boot simultaneously → Connection within 16s ✓
- Transmitter boots first (30s wait) → Boot receiver → Connection within 36s ✓

### Test Case 2: Receiver Reboot Recovery
- Established connection → Reboot receiver only → Auto-reconnect within 103s ✓

### Test Case 3: Router Channel Change
- Connection on ch 6 → Router changes to ch 11 → Auto-reconnect on ch 11 within 103s ✓

### Test Case 4: Bidirectional Config Sync
- TX MQTT config change → RX receives CONFIG_CHANGED → RX requests update → Synced ✓
- RX network config change → TX receives CONFIG_CHANGED → TX requests update → Synced ✓

### Test Case 5: Cache Overflow
- Single battery: TX boots alone (120s, 120 messages) → Cache shows 120 messages → Boot RX → All 120 flushed ✓
- Dual battery: TX boots alone (120s, 240 messages) → Cache shows 240 messages → Boot RX → All 240 flushed ✓
- Extreme: TX boots alone (300s, 300 messages) → Cache shows 250 messages (FIFO overflow) → Boot RX → All 250 flushed ✓

### Test Case 6: Keep-Alive Resilience
- Established connection → Disconnect receiver → DEGRADED (30s) → FAILURE (60s) → DISCONNECTED (90s) → Channel hopping restart ✓

## 11.11 Implementation Roadmap

**Phase 1: Core Channel Hopping** (Week 1)
- Modify discovery_task.cpp → Active channel hopping (1s per channel)
- Fix broadcast peer persistence bug (delete before add)
- Update message_handler.cpp → ACK handling on any channel
- Test boot order scenarios A, B, C

**Phase 2: Versioned Cache** (Week 2)
- Implement VersionedConfigCache structure
- Add version tracking to network/MQTT/battery configs
- Implement CONFIG_CHANGED message handling
- Test bidirectional sync scenarios

**Phase 3: Keep-Alive System** (Week 3)
- Implement KeepAliveManager class
- Add heartbeat task (10s interval)
- Implement failure state machine (CONNECTED → DEGRADED → FAILURE → DISCONNECTED)
- Test receiver reboot & router channel change scenarios

**Phase 4: Integration & Testing** (Week 4)
- Integration testing (all components together)
- Field testing with real hardware (multiple boot cycles)
- Performance tuning (timeout values, cache size)
- Documentation update

## 11.12 Final Recommendation

**✅ IMPLEMENT SECTION 11 ARCHITECTURE**

**Rationale**:
1. **Simplest conceptual model** - Transmitter initiates (standard ESP-NOW pattern)
2. **Fastest discovery** - 1s per channel vs. 6s (13s max vs. 78s max)
3. **Most robust** - Graceful failure handling, bidirectional sync, keep-alive monitoring
4. **Easiest to implement** - Builds on existing code structure
5. **Production ready** - Handles all real-world failure scenarios
6. **Future proof** - Extensible for multi-device networks

**Migration from Section 10**:
- Section 10 implementation (passive scanning) was **partially completed**
- **Revert passive scanning changes** in transmitter
- **Implement active channel hopping** instead
- **Keep** the cache infrastructure (reuse with version tracking)
- **Add** keep-alive and bidirectional sync components

---

---

## 🆕 11.13 REFINED ARCHITECTURE: Cache-Centric Communication Pattern

**Enhancement Date:** February 11, 2026  
**Refinement:** ALL communication flows through local caches (no direct device-to-device writes)

**CRITICAL UPDATES (Latest Revision)**:
1. **Queue Size Increased**: 100 → **250 entries** to support dual battery systems (192 cells from 2×96)
2. **Bidirectional State Changes**: State data (IP, MQTT, settings) can originate from EITHER device, not just transmitter
   - TX boots → DHCP assigns IP → Syncs to RX
   - RX web UI → User changes MQTT password → Syncs to TX
3. **Conflict Resolution**: When both devices change same config simultaneously, **NEWEST_TIMESTAMP_WINS** (always)
   - Both devices compare timestamps of conflicting changes
   - Newer timestamp always wins (simple, deterministic)
4. **NVS Persistence**: Only **TX persists to NVS** (RX doesn't need to save state data)
   - TX writes state to NVS (survives TX reboot)
   - RX uses state in RAM only (gets fresh copy from TX on boot)
5. **ESP-NOW Isolation**: ALL ESP-NOW operations run at low priority, never block Battery Emulator control code
   - ESP-NOW tasks: Priority 2 (background)
   - Battery Emulator (CAN/BMS/Safety): Priority 5 (critical)
   - Non-blocking sends (fire-and-forget to cache)
   - Fast callbacks (< 1ms, return immediately)
   - Core isolation (ESP-NOW on Core 1, Control on Core 0)

### 11.13.1 Cache-Centric Design Principle

**Current Section 11**: Battery data sent directly when connected, only cached when disconnected  
**REFINED**: **All data written to local cache first, then transmitted from cache**

### 11.13.2 Benefits of Cache-Centric Architecture

✅ **Decoupling**: Devices don't need to know peer connection state  
✅ **Reliability**: All data preserved locally before transmission  
✅ **Consistency**: Single source of truth (local cache)  
✅ **Recovery**: Easy resynchronization after reconnection  
✅ **Testing**: Inspectable cache state for debugging  
✅ **Atomicity**: Write-to-cache is atomic, transmission is async  
✅ **Ordering**: Natural FIFO ordering preserved  
✅ **Resilience**: Network failures don't lose data  
✅ **Non-Blocking**: ESP-NOW doesn't interfere with Battery Emulator CAN/BMS/safety code  
✅ **Low Priority**: ESP-NOW tasks yield to critical control code (CAN, BMS, charger)

### 11.13.3 Refined Data Flow

**OLD (Direct + Cache)**:
```
[Battery Data] ──(if connected)──> [ESP-NOW Send] ──> [Receiver]
               └─(if disconnected)─> [Cache] ──(on reconnect)──> [Flush] ──> [Receiver]
```

**NEW (Cache-Centric with Priority Separation)**:
```
                    ┌─ CRITICAL (PROBE, ACK, HEARTBEAT)
                    │  └─> Direct ESP-NOW Send (bypass cache) ──> [Receiver]
                    │
[Messages] ─────────┼─ HIGH (CONFIG_CHANGED, REQUEST_CONFIG)
                    │  └─> Direct ESP-NOW Send (bypass cache) ──> [Receiver]
                    │
                    └─ NORMAL (Battery Data)
                       └─> [Cache Queue] ──(background task, rate-limited)──> [ESP-NOW Send] ──> [Receiver]
                                │
                                └──> [Metadata: seq, timestamp, version]
```

### 11.13.4 Implementation Changes

#### Write Path (Producer)
```cpp
// OLD: Direct send when connected
void send_battery_data() {
    if (is_receiver_connected()) {
        esp_now_send(receiver_mac, &data, sizeof(data));  // DIRECT
    } else {
        cache.add(data);  // Cache only when disconnected
    }
}

// NEW: Always write to cache first
void send_battery_data() {
    // ALWAYS write to local cache (single code path)
    cache.add(data, millis(), next_seq++);
    // Background task handles transmission
}
```

#### Transmission Task (Consumer)
```cpp
void cache_transmission_task() {
    while (true) {
        if (is_receiver_connected() && cache.has_data()) {
            // Transmit from cache (not direct from source)
            CacheEntry entry = cache.peek();
            
            esp_err_t result = esp_now_send(receiver_mac, &entry.data, sizeof(entry.data));
            
            if (result == ESP_OK) {
                cache.mark_sent(entry.seq);  // Mark as transmitted
                // Wait for receiver ACK before removing from cache
            } else {
                // Transmission failed, keep in cache, retry later
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

#### Receiver Path
```cpp
void on_espnow_receive(const uint8_t* data, int len) {
    // Receiver ALSO writes to local cache first
    receiver_cache.add(data, millis());
    
    // Send ACK to transmitter (confirm receipt)
    send_ack(data->seq);
    
    // Background task processes from cache → display/MQTT/web
}
```

### 11.13.5 Cache Structure (Enhanced)

**CRITICAL DISTINCTION**: Two types of cached data with different retention policies

```cpp
enum CacheDataType {
    TRANSIENT_DATA,     // Battery readings - delete after ACK
    STATE_DATA          // IP, MQTT, settings - keep with version tracking
};

struct CacheEntry {
    CacheDataType data_type;    // Determines retention policy
    
    // Data payload (union for different types)
    union {
        espnow_payload_t battery_data;      // Transient
        network_config_t network_config;    // State
        mqtt_config_t mqtt_config;          // State
        battery_settings_t battery_settings; // State
    } payload;
    
    // Metadata (all entries)
    uint32_t seq;               // Sequence number (for ordering/dedup)
    uint32_t timestamp;         // Local timestamp when cached
    uint8_t retry_count;        // Transmission retry counter
    bool sent;                  // Transmitted to peer
    bool acked;                 // Received ACK from peer
    
    // State data specific (ignored for transient)
    uint16_t version;           // Version number (for state data only)
    bool is_latest;             // True if this is current version
};

class EnhancedCache {
public:
    // ═══════════════════════════════════════════════════════════
    // WRITE OPERATIONS
    // ═══════════════════════════════════════════════════════════
    
    // Transient data (battery readings)
    // - Added to FIFO queue (250 entries for dual battery: 192 cells + headroom)
    // - Removed after ACK
    bool add_transient(const espnow_payload_t& data, uint32_t timestamp, uint32_t seq);
    
    // State data (config items)
    // - Stored in versioned slots (NOT FIFO)
    // - Never removed, only updated with new version
    // - Old versions kept for sync validation
    bool add_state(CacheDataType type, const void* data, uint16_t version);
    
    // ═══════════════════════════════════════════════════════════
    // READ OPERATIONS (non-destructive peek)
    // ═══════════════════════════════════════════════════════════
    CacheEntry* peek_next_unsent();        // Get next unsent entry (any type)
    CacheEntry* peek_transient(size_t index);  // Access transient queue
    CacheEntry* peek_state(CacheDataType type); // Access state by type
    
    // ═══════════════════════════════════════════════════════════
    // STATE MANAGEMENT
    // ═══════════════════════════════════════════════════════════
    void mark_sent(uint32_t seq);          // Mark entry as transmitted
    void mark_acked(uint32_t seq);         // Mark entry as acknowledged
    
    // CRITICAL: Different cleanup policies
    void cleanup_acked_transient();        // Remove ONLY transient data after ACK
    void update_state_version(CacheDataType type, uint16_t new_version);
    
    // ═══════════════════════════════════════════════════════════
    // QUERY OPERATIONS
    // ═══════════════════════════════════════════════════════════
    size_t transient_unsent_count();       // Unsent battery readings
    size_t transient_unacked_count();      // Sent but not acked
    uint16_t get_state_version(CacheDataType type);  // Current version
    bool has_unsent_data();                // Any data to transmit
    
    // ═══════════════════════════════════════════════════════════
    // STATISTICS
    // ═══════════════════════════════════════════════════════════
    CacheStats get_stats();
    
private:
    // Separate storage for transient vs state
    // SIZING: 250 entries to handle dual battery scenarios:
    //   - Single battery: 96 cells
    //   - Dual battery: 192 cells (96 × 2)
    //   - Headroom: 58 entries for brief disconnections (~58 seconds at 1Hz)
    CacheEntry transient_queue_[250];      // FIFO for battery data (dual battery support)
    size_t transient_write_idx_;
    size_t transient_read_idx_;
    size_t transient_count_;
    
    // Fixed slots for state data (one per config type)
    CacheEntry state_network_;             // Network config (versioned)
    CacheEntry state_mqtt_;                // MQTT config (versioned)
    CacheEntry state_battery_settings_;    // Battery settings (versioned)
    
    SemaphoreHandle_t mutex_;
};
```

### 11.13.6 Bidirectional State Changes

**CRITICAL CLARIFICATION**: State data changes can originate from EITHER device, not just transmitter

#### Example Scenarios:

**Scenario 1: Transmitter Network Config Update (Originating from TX)**
```
TX boots → Ethernet initializes → DHCP assigns IP 192.168.1.50
         → TX writes to state_network_ (version 1)
         → TX sends CONFIG_CHANGED(type=network, version=1) to RX
         → RX receives notification (has no local version)
         → RX requests CONFIG_RESPONSE(type=network)
         → TX sends full network_config (v1)
         → RX writes to state_network_ (version 1)
         → RX persists to NVS
         → RX sends ACK
         
Result: Both devices synchronized at network_config version 1
```

**Scenario 2: Receiver MQTT Password Change (Originating from RX)**
```
RX web UI → User changes MQTT password → RX writes to state_mqtt_ (version 5, RAM only)
                                      → RX sends CONFIG_CHANGED(type=mqtt, version=5) to TX
                                      → TX receives notification
                                      → TX checks local version (has v4)
                                      → Version mismatch: remote v5 > local v4
                                      → TX requests CONFIG_RESPONSE(type=mqtt)
                                      → RX sends full mqtt_config (v5)
                                      → TX writes to state_mqtt_ (version 5)
                                      → TX persists v5 to NVS (ONLY TX persists)
                                      → TX sends ACK
                                      → TX reconnects to MQTT broker with new password
                                      
Result: Both devices synchronized at mqtt_config v5 (RX in RAM, TX in RAM + NVS)
```

**Scenario 3: Simultaneous Changes - Conflict Resolution**
```
TX changes network_config to v10 at timestamp 1000ms
RX changes network_config to v11 at timestamp 1005ms (different change)

Both devices send CONFIG_CHANGED messages
Both devices receive notifications
Both devices detect conflict (local change + remote change)

--- Conflict Resolution: NEWEST_TIMESTAMP_WINS (always) ---

Both devices compare timestamps:
  RX v11 (timestamp=1005ms) is newer than TX v10 (timestamp=1000ms)
  
TX: "I have v10 at t=1000ms, RX reports v11 at t=1005ms, RX is newer"
  → TX discards local v10 (not yet sent)
  → TX requests CONFIG_RESPONSE(type=network)  
  → RX sends v11 (full config)
  → TX writes v11 to state_network_
  → TX persists v11 to NVS
  → TX sends ACK to RX
  
RX: "I have v11 at t=1005ms, TX reports v10 at t=1000ms, mine is newer"
  → RX keeps local v11 (already in cache)
  → RX waits for TX to request config
  → RX sends v11 to TX
  → RX receives ACK from TX
  → Both synchronized at v11
  
Result: Both devices now have network_config v11 (RX in RAM, TX in RAM + NVS)
```

**Key Points**:
1. **State data is NOT unidirectional** (not TX→RX only)
2. **Either device can initiate changes** (web UI, DHCP, auto-discovery, user settings)
3. **Version tracking enables conflict detection** (both devices compare versions)
4. **Conflict resolution is timestamp-based** (NEWEST_TIMESTAMP_WINS always)
5. **Persistence is TX-only** (only TX writes to NVS, RX uses RAM copy)
6. **ACK confirms sync** (but doesn't delete state data - it persists forever on TX)

**Conflict Resolution Policy (All Config Types)**:
- `NEWEST_TIMESTAMP_WINS`: Always use the entry with the latest timestamp
  - Network config (IP/gateway/DNS): Newest timestamp wins
  - MQTT config (credentials/server): Newest timestamp wins
  - Battery display settings: Newest timestamp wins
  - Simple, deterministic, no special cases

### 11.13.7 Retention Policy: Transient vs State

**TRANSIENT DATA (Battery Readings)**:
```cpp
void cleanup_acked_transient() {
    // Remove from queue ONLY after ACK received
    for (size_t i = 0; i < transient_count_; i++) {
        CacheEntry* entry = &transient_queue_[transient_read_idx_];
        
        if (entry->acked) {
            // Safe to remove - peer confirmed receipt
            transient_read_idx_ = (transient_read_idx_ + 1) % 250;  // Dual battery queue size
            transient_count_--;
            LOG_DEBUG("[CACHE] Removed acked transient (seq=%u)", entry->seq);
        } else {
            break;  // Keep unsent/unacked entries
        }
    }
}
```

**STATE DATA (IP, MQTT, Settings)**:
```cpp
void add_state(CacheDataType type, const void* data, uint16_t version) {
    CacheEntry* state_slot = get_state_slot(type);
    
    // Update state slot (NEVER REMOVE)
    memcpy(&state_slot->payload, data, get_size(type));
    state_slot->version = version;
    state_slot->is_latest = true;
    state_slot->sent = false;
    state_slot->acked = false;
    
    // Mark old version as non-latest (but keep for history)
    // This allows rollback if needed
    
    LOG_INFO("[CACHE] State updated: type=%d, version=%u", type, version);
}

// State data is NEVER cleaned up after ACK
// Only updated with new versions
```

### 11.13.8 Bidirectional State Sync with Version Tracking

**Scenario**: Transmitter IP address changes (No Conflict)

```
T0:  User changes TX IP on web UI: 192.168.1.40 → 192.168.1.50
T1:  TX increments network_config.version: 5 → 6
T2:  TX updates local cache (state_network_ slot)
     ├─ ip = "192.168.1.50"
     ├─ version = 6
     ├─ timestamp = millis()
     ├─ is_latest = true
     └─ sent = false, acked = false
     
T3:  Background task detects unsent state data
T4:  TX sends CONFIG_CHANGED(type=network, version=6, timestamp=T2) to RX
T5:  RX receives CONFIG_CHANGED
     ├─ RX checks local cache: network_config.version = 5
     ├─ Remote version (6) > Local version (5) → OUT OF SYNC (no conflict)
     └─ RX sends REQUEST_CONFIG(type=network) to TX
     
T6:  TX receives REQUEST_CONFIG
     └─ TX sends full network_config (version=6, timestamp=T2) to RX
     
T7:  RX receives full config
     ├─ RX updates local cache (state_network_ slot)
     │   ├─ ip = "192.168.1.50"
     │   ├─ version = 6
     │   ├─ timestamp = T2
     │   └─ is_latest = true
     ├─ RX sends ACK(seq, type=network, version=6)
     └─ RX updates display/web UI with new IP
     
T8:  TX receives ACK
     └─ TX marks state_network_.acked = true
     └─ TX persists to NVS (TX-only persistence)
     ⚠️  BUT DOES NOT REMOVE FROM CACHE (state data persists)
```

**Key Point**: Both devices now have `network_config.version = 6` with same timestamp in their caches (RX in RAM, TX in RAM+NVS)

### 11.13.9 Cache Cleanup Logic

```cpp
void cache_cleanup_task() {
    while (true) {
        xSemaphoreTake(cache_mutex_, portMAX_DELAY);
        
        // ═══════════════════════════════════════════════════════
        // TRANSIENT DATA: Remove after ACK
        // ═══════════════════════════════════════════════════════
        size_t removed = 0;
        while (transient_count_ > 0) {
            CacheEntry* entry = &transient_queue_[transient_read_idx_];
            
            if (entry->acked) {
                // Battery reading delivered and confirmed - REMOVE
                transient_read_idx_ = (transient_read_idx_ + 1) % 250;  // Dual battery queue size
                transient_count_--;
                removed++;
            } else {
                break;  // Stop at first unacked entry
            }
        }
        
        if (removed > 0) {
            LOG_INFO("[CACHE] Cleaned up %u acked transient entries", removed);
        }
        
        // ═══════════════════════════════════════════════════════
        // STATE DATA: NEVER REMOVE, only update versions
        // ═══════════════════════════════════════════════════════
        // Network config - persistent (version tracked)
        if (state_network_.acked) {
            LOG_DEBUG("[CACHE] Network config v%u synced (kept in cache)", 
                     state_network_.version);
            // DO NOT REMOVE - just mark as synced
            state_network_.sent = true;
            state_network_.acked = true;
        }
        
        // MQTT config - persistent (version tracked)
        if (state_mqtt_.acked) {
            LOG_DEBUG("[CACHE] MQTT config v%u synced (kept in cache)", 
                     state_mqtt_.version);
            // DO NOT REMOVE - just mark as synced
        }
        
        // Battery settings - persistent (version tracked)
        if (state_battery_settings_.acked) {
            LOG_DEBUG("[CACHE] Battery settings v%u synced (kept in cache)", 
                     state_battery_settings_.version);
            // DO NOT REMOVE - just mark as synced
        }
        
        xSemaphoreGive(cache_mutex_);
        
        vTaskDelay(pdMS_TO_TICKS(5000));  // Cleanup every 5s
    }
}
```

### 11.13.10 Version Conflict Resolution

**Scenario**: Both devices modify same config item simultaneously

```
T0:  TX changes MQTT server: v5 → v6 (server="192.168.1.100")
T0:  RX changes MQTT server: v5 → v6 (server="192.168.1.200")  ⚠️ CONFLICT
     
T1:  TX sends CONFIG_CHANGED(mqtt, v6) to RX
T1:  RX sends CONFIG_CHANGED(mqtt, v6) to TX
     
T2:  TX receives CONFIG_CHANGED from RX
     ├─ Remote version (6) == Local version (6) → CONFLICT DETECTED
     └─ Apply conflict resolution rule
     
T3:  RX receives CONFIG_CHANGED from TX
     ├─ Remote version (6) == Local version (6) → CONFLICT DETECTED
     └─ Apply conflict resolution rule
```

**Conflict Resolution Rule (Simple & Deterministic)**:

```cpp
// ALWAYS use newest timestamp - no special cases
void resolve_version_conflict(CacheDataType type, 
                              const CacheEntry& local,
                              const CacheEntry& remote) {
    // Simple: newest timestamp always wins
    if (remote.timestamp > local.timestamp) {
        // Remote is newer - use remote version
        apply_remote_version(type, remote);
        LOG_WARN("[CACHE] Conflict resolved: remote newer (t=%u > %u)", 
                remote.timestamp, local.timestamp);
        
        // If we're TX, persist the remote version to NVS
        if (device_role == TRANSMITTER) {
            persist_state_to_nvs(type, remote);
        }
    } else if (local.timestamp > remote.timestamp) {
        // Local is newer - keep local, remote will sync to us
        LOG_WARN("[CACHE] Conflict resolved: local newer (t=%u > %u)", 
                local.timestamp, remote.timestamp);
        
        // If we're TX, persist the local version to NVS
        if (device_role == TRANSMITTER) {
            persist_state_to_nvs(type, local);
        }
    } else {
        // Same timestamp (rare) - use sequence number as tiebreaker
        if (remote.seq > local.seq) {
            apply_remote_version(type, remote);
            LOG_WARN("[CACHE] Conflict: same timestamp, higher seq wins");
        }
    }
}
```

**Applied to All Config Types**:
- **Network config** (IP/gateway/DNS): Newest timestamp wins
- **MQTT config** (credentials/server): Newest timestamp wins  
- **Battery settings**: Newest timestamp wins
- **All state data**: Newest timestamp wins (consistent, simple)

### 11.13.11 State Persistence Across Reboots

**CRITICAL**: Only **TX persists state to NVS**, RX uses RAM-only cache

**Rationale**:
- TX is the configuration authority (has Ethernet, MQTT connection, battery data)
- TX needs to remember state across reboots (IP settings, MQTT credentials)
- RX is display-only device (gets fresh config from TX on boot)
- RX doesn't need persistence (saves NVS wear, simpler code)

**Transmitter (NVS Persistence)**:
```cpp
// TX persists state to NVS
void persist_state_to_nvs() {
    nvs_handle_t nvs;
    nvs_open("cache_state", NVS_READWRITE, &nvs);
    
    // Persist network config
    nvs_set_blob(nvs, "net_cfg", &state_network_, sizeof(CacheEntry));
    
    // Persist MQTT config
    nvs_set_blob(nvs, "mqtt_cfg", &state_mqtt_, sizeof(CacheEntry));
    
    // Persist battery settings
    nvs_set_blob(nvs, "bat_cfg", &state_battery_settings_, sizeof(CacheEntry));
    
    nvs_commit(nvs);
    nvs_close(nvs);
    
    LOG_INFO("[CACHE] State persisted to NVS");
}

void restore_state_from_nvs() {
    nvs_handle_t nvs;
    nvs_open("cache_state", NVS_READONLY, &nvs);
    
    size_t size = sizeof(CacheEntry);
    
    // Restore network config
    if (nvs_get_blob(nvs, "net_cfg", &state_network_, &size) == ESP_OK) {
        LOG_INFO("[CACHE] Network config v%u restored", state_network_.version);
    }
    
    // Restore MQTT config
    if (nvs_get_blob(nvs, "mqtt_cfg", &state_mqtt_, &size) == ESP_OK) {
        LOG_INFO("[CACHE] MQTT config v%u restored", state_mqtt_.version);
    }
    
    // Restore battery settings
    if (nvs_get_blob(nvs, "bat_cfg", &state_battery_settings_, &size) == ESP_OK) {
        LOG_INFO("[CACHE] Battery settings v%u restored", state_battery_settings_.version);
    }
    
    nvs_close(nvs);
}
```

**Receiver (No NVS Persistence)**:
```cpp
// RX does NOT persist state to NVS
// - RX uses RAM-only cache
// - Gets fresh config from TX on every boot
// - No NVS operations needed in receiver code

void on_config_received_from_tx(const config_t& config) {
    // Update RAM cache only - NO NVS write
    update_state_cache(config.type, config.data, config.version);
    
    // Apply to system (display, web UI, etc.)
    apply_config(config);
    
    // Send ACK to TX
    send_ack(config.seq);
    
    // RX NEVER calls persist_state_to_nvs()
}
```

**Boot Sequence with State Restoration**:

**Transmitter Boot (Restores from NVS)**:
```
T0: TX boots
T1: TX restores state from NVS (persisted before shutdown)
    ├─ network_config v6 (last known IP/gateway/DNS)
    ├─ mqtt_config v12 (credentials/server)
    └─ battery_settings v8 (thresholds/alerts)
T2: TX starts active channel hopping
T3: TX discovers RX via PROBE/ACK
T4: TX sends CONFIG_CHANGED for all state (v6, v12, v8)
T5: RX syncs to TX versions
T6: Both synchronized (TX in NVS+RAM, RX in RAM only)
```

**Receiver Boot (Gets Fresh from TX)**:
```
T0: RX boots
T1: RX initializes empty RAM cache (NO NVS restore)
T2: RX waits passively on WiFi channel
T3: TX discovers RX via active channel hopping
T4: TX sends CONFIG_CHANGED messages (versions v6, v12, v8)
T5: RX has no local versions → Requests full configs
T6: TX sends all state data to RX
T7: RX populates RAM cache with TX versions
T8: Both synchronized (TX persisted to NVS, RX in RAM only)
```

### 11.13.12 Summary: Transient vs State Data

| Aspect | Transient Data | State Data |
|--------|----------------|------------|
| **Examples** | Battery readings, power, SOC | IP address, MQTT config, battery settings |
| **Storage** | FIFO queue (250 entries, dual battery) | Fixed slots (one per config type) |
| **Versioning** | Sequence numbers only | Version numbers + seq |
| **Retention** | Delete after ACK | **Never delete, only update** |
| **Persistence** | RAM only (lost on reboot) | **TX only**: NVS (survive reboot)<br>**RX only**: RAM (fresh from TX on boot) |
| **Sync Frequency** | Every 1s (continuous, 96-192 cells) | On change only |
| **Conflict Resolution** | N/A (one-way stream) | **NEWEST_TIMESTAMP_WINS** (always) |
| **Cleanup** | Remove after ACK | Never remove (TX keeps in NVS, RX keeps in RAM) |
| **Purpose** | Real-time telemetry | System configuration |

**Critical Implementation Note**:
```cpp
// ❌ WRONG: Cleanup removes ALL acked entries
void cleanup_acked() {
    for (auto& entry : cache) {
        if (entry.acked) {
            remove(entry);  // BAD: Removes state data too!
        }
    }
}

// ✅ CORRECT: Only remove transient data
void cleanup_acked() {
    for (auto& entry : transient_queue) {
        if (entry.acked) {
            remove(entry);  // OK: Only transient
        }
    }
    
    // State data never removed, just mark synced
    if (state_network_.acked) {
        state_network_.sent = true;  // Mark synced, KEEP in cache
    }
}
```

### 11.13.13 Message Flow with Cache Metadata

**Transmitter → Receiver (Data Message)**:
```cpp
struct cached_data_t {
    uint8_t type;                  // msg_cached_data = 0x36
    uint32_t seq;                  // Sequence number
    uint32_t timestamp;            // Transmitter timestamp
    uint16_t version;              // Data version
    espnow_payload_t payload;      // Actual battery data
};
```

**Receiver → Transmitter (ACK with Cache Info)**:
```cpp
struct data_ack_t {
    uint8_t type;                  // msg_data_ack = 0x37
    uint32_t seq;                  // Sequence being acked
    uint32_t last_seq_received;    // Last seq successfully cached
    uint16_t cache_free_space;     // Receiver cache available slots
};
```

### 11.13.14 Flow Control & Back-Pressure

**Problem**: Transmitter sends faster than receiver can process

**Solution**: Receiver advertises cache free space in ACK
```cpp
void cache_transmission_task() {
    while (true) {
        // Check receiver cache capacity before sending
        if (receiver_cache_free_space < 10) {
            LOG_WARN("[CACHE] Receiver cache nearly full, throttling sends");
            vTaskDelay(pdMS_TO_TICKS(500));  // Back off
            continue;
        }
        
        // Safe to send
        if (cache.has_data()) {
            send_next_cached_entry();
        }
    }
}
```

### 11.13.15 Advantages Over Direct Send

| Aspect | Direct Send | Cache-Centric |
|--------|-------------|---------------|
| **Data Loss Risk** | High (if send fails) | Zero (always cached) |
| **Code Complexity** | Conditional logic | Single write path |
| **Testing** | Hard (timing-dependent) | Easy (inspect cache) |
| **Ordering Guarantee** | No (race conditions) | Yes (FIFO cache) |
| **Recovery** | Manual retry logic | Automatic (cache flush) |
| **Back-Pressure** | None | Natural (cache capacity) |
| **Debugging** | Lost data invisible | Cache state visible |
| **Atomicity** | No (partial sends) | Yes (cache writes) |

### 11.13.16 Additional Architectural Suggestions

#### Suggestion 1: Persistent Cache (Survive Reboot)
**Current**: Cache is RAM-only (lost on reboot)  
**Enhancement**: Write critical cache entries to NVS/LittleFS

```cpp
class PersistentCache : public EnhancedCache {
    void persist_to_nvs();              // Write cache to NVS
    void restore_from_nvs();            // Restore cache on boot
    void set_persistence_threshold(size_t count);  // Persist when N entries
};
```

**Use Case**: Transmitter reboots with 50 unsent messages → Restored from NVS → Sent after reconnection

#### Suggestion 2: Cache Priority Levels
**Current**: All data treated equally (FIFO)  
**Enhancement**: Priority queue for critical vs. non-critical data

```cpp
enum CachePriority {
    CRITICAL = 0,      // Config changes, alarms (never drop)
    HIGH = 1,          // Battery data, events
    NORMAL = 2,        // Periodic updates
    LOW = 3            // Debug, telemetry (drop first on overflow)
};

// Multiple queues, service CRITICAL first
class PriorityCache {
    EnhancedCache queues_[4];  // One per priority
    CacheEntry* peek();        // Returns highest priority entry
};
```

#### Suggestion 3: Compression for Large Data
**Current**: Raw battery data (~250 bytes/msg)  
**Enhancement**: Compress repetitive data (deltas, run-length encoding)

```cpp
struct compressed_data_t {
    uint8_t type;              // msg_compressed_data = 0x38
    uint8_t compression;       // 0=none, 1=delta, 2=RLE
    uint16_t original_size;
    uint16_t compressed_size;
    uint8_t payload[200];      // Compressed data
};
```

**Savings**: If SOC/power change slowly, delta encoding saves 50-70%

#### Suggestion 4: Deduplicate Cache Entries
**Current**: Redundant data (SOC=50% sent 100 times)  
**Enhancement**: Detect duplicates, send once with "no change" flag

```cpp
bool is_duplicate(const espnow_payload_t& new_data) {
    if (cache.empty()) return false;
    
    CacheEntry* last = cache.peek_at(cache.count() - 1);
    return (last->data.soc == new_data.soc &&
            last->data.power == new_data.power &&
            /* ... other fields ... */);
}

void add_data(const espnow_payload_t& data) {
    if (is_duplicate(data)) {
        LOG_DEBUG("[CACHE] Duplicate data, skipping");
        return;  // Don't cache identical data
    }
    cache.add(data, millis(), next_seq++);
}
```

#### Suggestion 5: Time-Based Cache Expiration
**Current**: Cache persists indefinitely  
**Enhancement**: Expire old entries (stale data)

```cpp
void cache_cleanup_task() {
    while (true) {
        uint32_t now = millis();
        
        // Remove entries older than 5 minutes (stale)
        for (size_t i = 0; i < cache.count(); i++) {
            CacheEntry* entry = cache.peek_at(i);
            if (now - entry->timestamp > 300000) {  // 5 min
                LOG_WARN("[CACHE] Removing stale entry (seq=%u, age=%us)", 
                         entry->seq, (now - entry->timestamp) / 1000);
                cache.remove(entry->seq);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(60000));  // Check every minute
    }
}
```

#### Suggestion 6: Cache Synchronization Protocol
**Problem**: Receiver cache and transmitter cache can diverge  
**Enhancement**: Periodic cache sync (like MQTT QoS 2)

```cpp
// Transmitter sends: "I have seq 1-100"
struct cache_manifest_t {
    uint8_t type;              // msg_cache_manifest = 0x39
    uint32_t first_seq;
    uint32_t last_seq;
    uint16_t count;
    uint8_t checksum[16];      // MD5 of all seq numbers
};

// Receiver responds: "I have seq 1-85, missing 86-100"
struct cache_status_t {
    uint8_t type;              // msg_cache_status = 0x3A
    uint32_t last_seq_received;
    uint32_t missing_seq[20];  // Gaps in sequence
    uint16_t missing_count;
};

// Transmitter resends missing entries
void resync_cache() {
    // Send manifest every 60s
    send_cache_manifest();
    
    // Receiver requests missing entries
    wait_for_cache_status();
    
    // Resend any missing entries
    resend_missing_entries();
}
```

#### Suggestion 7: Cache Metrics & Monitoring
**Enhancement**: Detailed cache health metrics

```cpp
struct CacheHealthMetrics {
    // Performance metrics
    uint32_t total_added;
    uint32_t total_sent;
    uint32_t total_acked;
    uint32_t total_dropped;
    uint32_t total_retried;
    
    // Capacity metrics
    size_t current_count;
    size_t max_count_reached;
    size_t current_unsent;
    size_t current_unacked;
    
    // Timing metrics
    uint32_t avg_cache_duration_ms;   // Time from add to ack
    uint32_t max_cache_duration_ms;
    uint32_t avg_retry_count;
    
    // Error metrics
    uint32_t overflow_events;
    uint32_t stale_removals;
    uint32_t corruption_detects;
    
    void log_summary();
    void reset();
};
```

#### Suggestion 8: Receiver-Side Processing Cache
**Enhancement**: Receiver also uses cache-centric pattern for processing

```
[ESP-NOW Receive] ──> [Receiver Cache] ──> [Processing Tasks]
                                              ├─> Display Update
                                              ├─> MQTT Publish
                                              ├─> Web SSE Notify
                                              └─> Storage (SD/LittleFS)
```

**Benefit**: Display/MQTT/Web don't block ESP-NOW receive handler

#### Suggestion 9: Watchdog for Stuck Cache
**Problem**: Cache transmission task crashes or stalls  
**Enhancement**: Watchdog monitors cache processing

```cpp
class CacheWatchdog {
    void start() {
        last_transmit_time_ = millis();
        
        xTaskCreate([](void* param) {
            while (true) {
                uint32_t now = millis();
                uint32_t idle_time = now - last_transmit_time_;
                
                if (idle_time > 60000 && cache.has_data()) {
                    LOG_ERROR("[WATCHDOG] Cache stuck! Idle=%us, Count=%u", 
                              idle_time/1000, cache.count());
                    
                    // Restart transmission task
                    restart_transmission_task();
                }
                
                vTaskDelay(pdMS_TO_TICKS(10000));  // Check every 10s
            }
        }, "cache_wdog", 2048, this, 1, nullptr);
    }
    
    void pet() { last_transmit_time_ = millis(); }
    
private:
    uint32_t last_transmit_time_;
};
```

#### Suggestion 10: Cache-to-Cache Direct Transfer (Future)
**Enhancement**: Support multiple transmitters with cache exchange

```
[Transmitter 1 Cache] ──┐
                         ├──> [Receiver Cache Aggregator] ──> [Processing]
[Transmitter 2 Cache] ──┘
```

**Use Case**: Multiple battery packs, receiver aggregates all caches

---

## 11.14 Final Refined Architecture Summary

### Core Principles
1. ✅ **Cache-First for Data**: Battery telemetry written to local cache before transmission
2. ✅ **Non-Blocking ESP-NOW**: ALL ESP-NOW operations run at low priority, never block Battery Emulator
3. ✅ **Async Transmission**: Background task sends cached data (rate-limited, 50ms intervals)
4. ✅ **ACK-Based Cleanup**: Remove from cache only after peer ACK
5. ✅ **Flow Control**: Receiver advertises cache capacity
6. ✅ **Metadata Rich**: Seq numbers, timestamps, versions on all entries
7. ✅ **Fast Callbacks**: ESP-NOW ISR handlers < 1ms (no blocking)
8. ✅ **Core Isolation**: ESP-NOW on Core 1, Battery Emulator control on Core 0

### Task Priority Allocation

| Task | FreeRTOS Priority | Core | Purpose | Blocks |
|------|------------------|------|---------|--------|
| **CAN Communication** | 5 (CRITICAL) | 0 | Battery control messages | Nothing |
| **BMS Monitoring** | 5 (CRITICAL) | 0 | Cell voltage, temperature | Nothing |
| **Safety Checks** | 5 (CRITICAL) | 0 | Over-voltage, under-voltage | Nothing |
| **Charger Control** | 4 (HIGH) | 0 | Charge current limits | ESP-NOW only |
| **ESP-NOW Discovery** | 2 (LOW) | 1 | Channel hopping, PROBE/ACK | Only cleanup |
| **ESP-NOW Data TX** | 2 (LOW) | 1 | Battery telemetry | Only cleanup |
| **Cache Cleanup** | 1 (IDLE) | 1 | Remove acked entries | Nothing |

**Guaranteed Battery Emulator Control Code Priority**:
- ✅ CAN communication runs first, always (Priority 5 > ESP-NOW Priority 2)
- ✅ BMS monitoring never delayed by ESP-NOW (different core + higher priority)
- ✅ Safety checks preempt ESP-NOW tasks (FreeRTOS scheduling)
- ✅ Cache write < 100µs (negligible impact on control loop)
- ✅ ESP-NOW callbacks < 1ms (fast return to control code)
- ✅ Core 0 dedicated to Battery Emulator, Core 1 for ESP-NOW (hardware isolation)

### Implementation Priority
**Phase 1 (Essential - MUST HAVE)**:
- **Non-blocking ESP-NOW architecture** (low priority tasks, Core 1 pinning)
- **Fast ISR callbacks** (< 1ms, queue-based processing)
- **Cache-first data collection** (< 100µs write time)
- Enhanced cache with seq/timestamp/ack tracking
- Background transmission task with rate limiting (50ms intervals)
- ACK-based cleanup
- Flow control (back-pressure)

**Phase 2 (Recommended)**:
- Cache deduplication
- Priority queues
- Watchdog monitoring

**Phase 3 (Advanced)**:
- Persistent cache (NVS)
- Compression
- Time-based expiration
- Cache synchronization protocol

**Phase 4 (Future)**:
- Multi-transmitter support
- Advanced analytics
- ML-based prediction (cache preload)

---

**Document Status**: APPROVED FOR IMPLEMENTATION (CACHE-CENTRIC REFINEMENT)  
**Next Steps**: 
1. Implement EnhancedCache with seq/timestamp/ack tracking
2. Create background transmission task
3. Add flow control with receiver capacity feedback
4. Begin Phase 1 development (Core Channel Hopping + Cache-Centric Pattern)
