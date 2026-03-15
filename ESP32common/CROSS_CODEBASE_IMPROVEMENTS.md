# Cross-Codebase Improvements
## Shared Enhancements for ESP-NOW Receiver + Transmitter

**Date**: February 26, 2026  
**Scope**: Improvements that benefit **both** the receiver and transmitter codebases  
**Note**: These improvements require coordinated changes across both codebases

---

## Overview

Both the receiver and transmitter share certain architectural patterns and challenges. This document identifies improvements that should be made to **both codebases together** to ensure consistency, prevent code duplication, and maintain architectural coherence across the system.

---

## 1. Magic Number Centralization (Shared Constants)

### Priority: 🟡 **MEDIUM**
**Effort**: 1 day combined (coordinated across both codebases)  
**Blocking**: No - independent improvement

### Current State

**Receiver**:
```cpp
// display_core.cpp
const int CHAR_WIDTH = 120;
const int CHAR_HEIGHT = 16;

// api_handlers.cpp
const uint32_t API_TIMEOUT_MS = 5000;
```

**Transmitter**:
```cpp
// discovery_task.cpp
vTaskDelay(pdMS_TO_TICKS(50));
vTaskDelay(pdMS_TO_TICKS(100));

// mqtt_manager.cpp
const uint32_t MQTT_TIMEOUT_MS = 10000;
```

### Unified Solution

Create shared configuration in ESP32 Common:

```
ESP32Common/
├── include/
│   └── config/
│       ├── timing_config.h        (Shared timing constants)
│       ├── network_config.h       (Network timeouts)
│       ├── espnow_config.h        (ESP-NOW settings)
│       └── feature_flags.h        (Feature toggles)
└── docs/
    └── CONFIG_README.md           (Configuration guide)
```

**Note**: Display configuration (display_config.h) is **not** included in ESP32Common since only the receiver has a display. Display-specific constants remain in the receiver codebase.

### Shared Timing Configuration

```cpp
// ESP32Common/include/config/timing_config.h
#pragma once

namespace TimingConfig {
    // ========== ESP-NOW Protocol Timing ==========
    
    // Discovery phase (applies to both RX discovery of TX, and TX discovery of RX)
    constexpr uint32_t ESPNOW_DISCOVERY_PROBE_INTERVAL_MS = 50;
    constexpr uint32_t ESPNOW_DISCOVERY_CHANNEL_STABILIZATION_MS = 100;
    constexpr uint32_t ESPNOW_DISCOVERY_ACK_TIMEOUT_MS = 500;
    constexpr uint32_t ESPNOW_DISCOVERY_TOTAL_TIMEOUT_MS = 15000;
    
    // Data transmission
    constexpr uint32_t ESPNOW_TX_INTERVAL_MS = 1000;          // Send data every second
    constexpr uint32_t ESPNOW_TX_TIMEOUT_MS = 500;            // Wait 500ms for delivery
    constexpr uint32_t ESPNOW_ACK_TIMEOUT_MS = 1000;          // Wait 1s for ACK
    
    // Heartbeat (connection health check)
    constexpr uint32_t ESPNOW_HEARTBEAT_INTERVAL_MS = 30000;  // Send every 30 seconds
    constexpr uint32_t ESPNOW_HEARTBEAT_TIMEOUT_MS = 90000;   // Consider disconnected after 90s
    constexpr uint32_t ESPNOW_HEARTBEAT_ACK_TIMEOUT_MS = 1000;
    
    // ========== Network Timing (Receiver & Transmitter) ==========
    
    // Ethernet (Transmitter only, but should be consistent if Receiver adds Ethernet)
    constexpr uint32_t ETHERNET_DHCP_TIMEOUT_MS = 30000;      // 30 second DHCP timeout
    constexpr uint32_t ETHERNET_LINK_DETECTION_MS = 1000;     // Check link every 1 second
    
    // MQTT (Transmitter publishes, Receiver subscribes - should share timeout)
    constexpr uint32_t MQTT_CONNECT_TIMEOUT_MS = 10000;
    constexpr uint32_t MQTT_INITIAL_RECONNECT_DELAY_MS = 5000;
    constexpr uint32_t MQTT_MAX_RECONNECT_DELAY_MS = 300000;  // 5 minutes max
    
    // SSE (Receiver web interface)
    constexpr uint32_t SSE_RECONNECT_INITIAL_DELAY_MS = 1000;
    constexpr uint32_t SSE_RECONNECT_MAX_DELAY_MS = 30000;
    
    // ========== Application Timing ==========
    
    // Display updates (Receiver)
    constexpr uint32_t DISPLAY_PAGE_TRANSITION_MS = 5000;     // Show each page 5 seconds
    constexpr uint32_t DISPLAY_UPDATE_INTERVAL_MS = 500;      // Refresh display 2x/second
    
    // Data staleness detection (both codebases)
    constexpr uint32_t DATA_STALE_TIMEOUT_MS = 10000;         // 10 second timeout for stale data
    
    // System initialization
    constexpr uint32_t BOOT_TIMEOUT_MS = 30000;               // 30 second boot timeout
    constexpr uint32_t INITIALIZATION_ATTEMPT_TIMEOUT_MS = 5000;
    
    // OTA updates
    constexpr uint32_t OTA_DOWNLOAD_TIMEOUT_MS = 60000;       // 60 second timeout
    
    // ========== Validation ==========
    
    // Sanity check: ensure critical timeouts make sense
    static_assert(ESPNOW_HEARTBEAT_TIMEOUT_MS > ESPNOW_HEARTBEAT_INTERVAL_MS * 2,
                  "Heartbeat timeout must be > 2x interval");
    
    static_assert(MQTT_MAX_RECONNECT_DELAY_MS >= MQTT_INITIAL_RECONNECT_DELAY_MS,
                  "Max reconnect delay must be >= initial");
    
    static_assert(ESPNOW_HEARTBEAT_TIMEOUT_MS > ESPNOW_DISCOVERY_TOTAL_TIMEOUT_MS,
                  "Heartbeat timeout must be > discovery timeout");
}
```

### Benefits for Both Codebases

- ✅ **Consistency**: Both devices use same timing
- ✅ **Documentation**: Clear constants with comments
- ✅ **Tunability**: Can adjust system-wide timing from one place
- ✅ **Validation**: Static assertions catch configuration errors
- ✅ **Maintainability**: No scattered magic numbers

### Implementation

1. **Create `ESP32Common/include/config/timing_config.h`**
2. **Update Receiver**:
   - Replace hardcoded constants in `display_core.cpp`, `mqtt_client.cpp`
   - Include `#include "config/timing_config.h"`
3. **Update Transmitter**:
   - Replace hardcoded constants in `discovery_task.cpp`, `mqtt_manager.cpp`
   - Include `#include "config/timing_config.h"`

---

## 2. Dynamic Battery & Inverter Type Discovery via ESP-NOW

### Priority: 🔴 **HIGH - CRITICAL MAINTENANCE**
**Effort**: 2-3 days combined (transmitter + receiver changes)  
**Blocking**: No - Independent of other improvements  
**Complexity**: Medium - New ESP-NOW message types + protocol handling

### Problem Statement

**Current State**:
- Receiver maintains **hardcoded static arrays** of 46+ battery types and 22+ inverter types in `api_type_selection_handlers.cpp`
- Transmitter defines authoritative type mappings in `BATTERIES.cpp` and inverter protocol definitions
- **No synchronization mechanism** - Arrays must be manually updated whenever Battery Emulator adds/removes types
- **No build-time validation** - Out-of-sync arrays cause silent failures (mismatched dropdown menus)
- **Already maintenance burden** - Every Battery Emulator version update requires manual receiver code changes

**Why This is Critical**:
1. Battery Emulator is constantly evolving - new batteries/inverters added regularly
2. Receiver web UI dropdown menus source from these hardcoded arrays
3. User selects battery type that doesn't match transmitter's type enum → configuration error
4. Debugging becomes nightmare (type mismatch not obvious)
5. Receiver codebase becomes out-of-date after every Battery Emulator release

**Current Arrays** (must be manually kept in sync):
```cpp
// lib/webserver/api/api_type_selection_handlers.cpp

static TypeEntry battery_types[] = {
    {0, "None"}, {2, "BMW i3"}, {3, "BMW iX"}, {4, "Bolt/Ampera"}, {5, "BYD Atto 3"},
    {6, "Cellpower BMS"}, {7, "CHAdeMO"}, {8, "CMFA EV"}, {9, "Foxess"},
    // ... 37 more manually duplicated entries ...
    {46, "Maxus EV80"}
};

static TypeEntry inverter_types[] = {
    {0, "None"}, {1, "Afore battery over CAN"}, {2, "BYD Battery-Box Premium HVS over CAN Bus"},
    // ... 19 more manually duplicated entries ...
    {21, "Sungrow SBRXXX emulation over CAN bus"}
};
```

### Recommended Solution: Option A - ESP-NOW Dynamic Discovery

**Architecture**:
Transmitter queries Battery Emulator and responds with live type lists. Receiver caches results and uses them for web UI dropdowns.

```
┌─────────────────┐                    ┌──────────────────┐
│    Receiver     │                    │   Transmitter    │
│                 │                    │                  │
│ On startup:     │  REQUEST TYPES     │ BATTERIES.cpp    │
│ Request types ──┼───────────────────>│ (authoritative)  │
│                 │                    │                  │
│ Receive &       │<──────────────────┤ Return:          │
│ cache types     │  RESPONSE TYPES    │ [{id, name}, ..] │
│                 │                    │                  │
│ Web UI uses     │                    │                  │
│ cached data     │                    │                  │
└─────────────────┘                    └──────────────────┘
```

**Transmitter-Side Changes** (`ESPnowtransmitter2`):

```cpp
// src/espnow/espnow_handlers.cpp

#include "../battery_emulator/battery/BATTERIES.h"

void handle_type_request(const uint8_t *mac_addr, const uint8_t *data, int len) {
    if (len < 1) return;
    
    uint8_t request_type = data[0];
    
    if (request_type == MSG_REQUEST_BATTERY_TYPES) {
        send_battery_types_response(mac_addr);
    } 
    else if (request_type == MSG_REQUEST_INVERTER_TYPES) {
        send_inverter_types_response(mac_addr);
    }
}

void send_battery_types_response(const uint8_t *mac_addr) {
    // Iterate through BATTERIES.h includes and BATTERIES.cpp mappings
    // Build response with all available battery types
    
    for (int id = 0; id < MAX_BATTERY_TYPE; id++) {
        const char* name = name_for_battery_type((BatteryType)id);
        if (name && strcmp(name, "Unknown") != 0) {
            // Queue type entry for transmission
            send_type_entry(mac_addr, id, name);
        }
    }
}

void send_inverter_types_response(const uint8_t *mac_addr) {
    // Build response with all supported inverter types
    for (uint8_t id = 0; id < NUM_INVERTER_PROTOCOLS; id++) {
        const char* name = get_inverter_protocol_name(id);
        if (name) {
            send_type_entry(mac_addr, id, name);
        }
    }
}
```

**Receiver-Side Changes** (`espnowreciever_2`):

```cpp
// src/espnow/type_discovery.h
#pragma once

#include <vector>
#include "../common.h"

struct TypeEntry {
    uint8_t id;
    const char* name;
};

namespace TypeDiscovery {
    // Request type lists from transmitter
    void request_battery_types();
    void request_inverter_types();
    
    // Cache management
    const std::vector<TypeEntry>& get_battery_types();
    const std::vector<TypeEntry>& get_inverter_types();
    
    // Called when response received
    void handle_battery_types_response(const TypeEntry* types, uint8_t count);
    void handle_inverter_types_response(const TypeEntry* types, uint8_t count);
    
    // Check if discovery complete
    bool is_discovery_complete();
}

// src/espnow/type_discovery.cpp
#include "type_discovery.h"

static std::vector<TypeEntry> g_battery_types_cache;
static std::vector<TypeEntry> g_inverter_types_cache;
static bool g_discovery_complete = false;

void TypeDiscovery::request_battery_types() {
    uint8_t request[] = {MSG_REQUEST_BATTERY_TYPES};
    send_espnow_request(request, sizeof(request));
    LOG_INFO("[TypeDiscovery] Requesting battery types from transmitter");
}

void TypeDiscovery::request_inverter_types() {
    uint8_t request[] = {MSG_REQUEST_INVERTER_TYPES};
    send_espnow_request(request, sizeof(request));
    LOG_INFO("[TypeDiscovery] Requesting inverter types from transmitter");
}

void TypeDiscovery::handle_battery_types_response(const TypeEntry* types, uint8_t count) {
    g_battery_types_cache.assign(types, types + count);
    LOG_INFO("[TypeDiscovery] Received %d battery types from transmitter", count);
}

const std::vector<TypeEntry>& TypeDiscovery::get_battery_types() {
    return g_battery_types_cache;
}

// Similar for inverter types...

// src/main.cpp - Boot sequence
void setup() {
    // ... other initialization ...
    
    // Request type lists from transmitter
    TypeDiscovery::request_battery_types();
    TypeDiscovery::request_inverter_types();
    
    // Web server starts after discovery (or with timeout)
    // ...
}

// lib/webserver/api/api_type_selection_handlers.cpp - Use dynamic cache
static esp_err_t api_get_battery_types_handler(httpd_req_t *req) {
    const auto& cached_types = TypeDiscovery::get_battery_types();
    
    if (cached_types.empty()) {
        // Fallback if discovery not complete (shouldn't happen)
        const char* json = "{\"error\":\"Type discovery in progress, try again\"}";
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    // Use cached_types instead of static battery_types array
    String json = generate_sorted_type_json(
        const_cast<TypeEntry*>(cached_types.data()),
        cached_types.size()
    );
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}
```

**Benefits**:
- ✅ **Zero maintenance** - Receiver always in sync with Battery Emulator
- ✅ **Single source of truth** - Transmitter owns all type definitions
- ✅ **Automatic updates** - New battery types appear without receiver code changes
- ✅ **Runtime flexibility** - Works with custom Battery Emulator forks
- ✅ **Runtime discovery** - Detects what battery system transmitter supports

**Potential Concern - Message Size**:
- **Battery types array**: ~46 entries × ~30 bytes (id + name) = ~1.4 KB
- **Inverter types array**: ~22 entries × ~30 bytes = ~660 bytes  
- **ESP-NOW MTU**: 250 bytes per message
- **Solution**: Requires **multi-message fragmentation** (split across multiple ESP-NOW packets)

**If ESP-NOW payload is too large**: Fall back to MQTT for type discovery
- Transmitter publishes type lists to MQTT topic
- Receiver subscribes to type lists topic
- More reliable but requires MQTT connection

**Message Type Additions** (in `espnow_common/espnow_common.h`):
```cpp
enum ESPNowMessageType {
    // ... existing types ...
    MSG_REQUEST_BATTERY_TYPES = 0x50,
    MSG_RESPONSE_BATTERY_TYPES_PARTIAL = 0x51,  // Multi-message support
    MSG_REQUEST_INVERTER_TYPES = 0x52,
    MSG_RESPONSE_INVERTER_TYPES_PARTIAL = 0x53,
    // ... etc ...
};

// Fragmentation support for large responses
struct TypeListFragment {
    uint8_t msg_type;          // MSG_RESPONSE_BATTERY_TYPES_PARTIAL
    uint8_t fragment_num;      // 0, 1, 2, ...
    uint8_t total_fragments;   // Total expected
    uint8_t entries_in_fragment;
    // Followed by TypeEntry[] data
};
```

**Implementation Plan**:
1. Add type request/response message types to ESP-NOW protocol
2. Implement request handling in transmitter (`send_battery_types_response`, etc.)
3. Implement caching in receiver (`TypeDiscovery` namespace)
4. Integrate type discovery into boot sequence
5. Update web API handlers to use cached types
6. **Remove hardcoded type arrays from receiver**
7. Add fallback for discovery timeout (use empty dropdown with "Loading..." message)

### Investigation Required

Before implementation, **validate message size constraints**:

1. **Measure actual payload**:
   - Count all battery types in BATTERIES.cpp
   - Count all inverter types
   - Calculate bytes needed (id=1 byte + name=variable)
   - Compare against ESP-NOW 250-byte limit

2. **If payload fits in 1 message**:
   - ✅ Use simple ESP-NOW discovery (2-3 messages total)
   - Implementation straightforward

3. **If payload > 250 bytes**:
   - Fragment across multiple messages (add fragment_num, total_fragments)
   - More complex but still manageable
   - Similar to how OTA updates handle large payloads

4. **If fragmentation adds too much complexity**:
   - Use **MQTT fallback** for type discovery
   - Transmitter publishes `/types/batteries` and `/types/inverters` on MQTT
   - Receiver subscribes after MQTT connection
   - Adds MQTT dependency for type discovery

**Recommendation**: Implement with **dual support**:
- Try ESP-NOW first (faster)
- Fall back to MQTT if available
- Have static fallback as last resort (same as today, but with deprecation notice)

### Timeline

**Phase 1** (This session): Investigation
- Measure actual message sizes
- Determine if fragmentation needed
- Decide on MQTT fallback approach

**Phase 2** (Next session): Implementation
- Add ESP-NOW message types to protocol
- Implement transmitter discovery handlers
- Implement receiver type discovery + caching
- Update web API handlers
- Remove hardcoded arrays
- Add fallback handling for timeouts

---

## 3. Hardware Abstraction Layer (HAL) Pattern

### Priority: 🟡 **MEDIUM**
**Effort**: 2 days combined  
**Blocking**: Partially - Receiver's display HAL should model transmitter's battery emulator HAL

### Current State

**Receiver**:
- Has global `TFT_eSPI tft` object
- Display functions call TFT methods directly (no HAL)
- Tight coupling to hardware

**Transmitter**:
- Has battery emulator integration through CAN
- Could benefit from formal HAL for non-blocking patterns
- Interface-based design (partially implicit)

### Shared HAL Pattern

Create interface-based hardware abstraction that both can follow:

```cpp
// ESP32Common/include/hal/idevice_interface.h
class IDevice {
public:
    virtual ~IDevice() = default;
    
    virtual bool init() = 0;
    virtual bool is_available() const = 0;
    virtual void update() = 0;
    
    virtual std::string get_error_message() const = 0;
};

// Receiver Display HAL
class IDisplayDevice : public IDevice {
public:
    virtual void clear_screen() = 0;
    virtual void set_text_color(uint16_t fg, uint16_t bg) = 0;
    virtual void draw_text(uint16_t x, uint16_t y, const char* text) = 0;
    // ... etc
};

// Transmitter Battery Emulator HAL (already implicit)
class IBatteryDevice : public IDevice {
public:
    virtual bool get_battery_data(BatteryData& data) = 0;
    virtual void publish_battery_state(const BatteryData& data) = 0;
};

// Transmitter Ethernet HAL
class IEthernetDevice : public IDevice {
public:
    virtual bool get_local_ip(uint32_t& ip) = 0;
    virtual bool is_link_up() const = 0;
    virtual void reset() = 0;
};
```

### Example: Receiver Display HAL

```cpp
// Receiver - implement display interface
class TftEspiDisplay : public IDisplayDevice {
public:
    explicit TftEspiDisplay(TFT_eSPI& tft) : tft_(tft) {}
    
    bool init() override {
        tft_.init();
        tft_.setRotation(ROTATION);
        return true;
    }
    
    bool is_available() const override {
        return true;  // Assuming always available if initialized
    }
    
    void update() override {
        // Handle any periodic display updates
    }
    
    void clear_screen() override {
        tft_.fillScreen(TFT_BLACK);
    }
    
    void draw_text(uint16_t x, uint16_t y, const char* text) override {
        tft_.setTextColor(TFT_WHITE);
        tft_.setCursor(x, y);
        tft_.print(text);
    }
    
private:
    TFT_eSPI& tft_;
};

// Test implementation
class MockDisplay : public IDisplayDevice {
public:
    bool init() override { return true; }
    bool is_available() const override { return true; }
    void update() override {}
    void clear_screen() override { render_log.push_back("clear_screen"); }
    void draw_text(uint16_t x, uint16_t y, const char* text) override {
        render_log.push_back("draw_text");
    }
    
    std::vector<std::string> render_log;
};
```

### Example: Transmitter Battery HAL (Formalize Implicit)

```cpp
// Transmitter - formalize battery emulator interaction
class BatteryEmulatorDevice : public IBatteryDevice {
public:
    bool init() override;
    bool is_available() const override;
    void update() override;
    
    bool get_battery_data(BatteryData& data) override;
    void publish_battery_state(const BatteryData& data) override;
    
private:
    BatteryData last_data_;
    uint32_t last_update_time_;
};
```

### Benefits

- ✅ **Consistency**: Both codebases use same HAL pattern
- ✅ **Testability**: Can mock hardware for unit tests
- ✅ **Flexibility**: Can swap implementations
- ✅ **Documentation**: Clear hardware interface contracts
- ✅ **Separation**: Hardware details isolated from business logic

---

## 3. State Machine Best Practices

### Priority: 🟡 **MEDIUM**
**Effort**: 1.5 days combined  
**Blocking**: No - but recommended for consistency

### Current State

**Receiver**:
- `EspNowConnectionManager`: 3 states (basic)
- `SystemState`: 5 states (exists but underused)
- Simple state enum, limited transition logging

**Transmitter**:
- `TransmitterConnectionManager`: 17 states (sophisticated)
- Includes intermediate states for race condition prevention
- Better structured but no consistent logging

### Unified Pattern

Create shared state machine base class:

```cpp
// ESP32Common/include/state/state_machine.h
template<typename StateEnum>
class BaseStateMachine {
public:
    virtual ~BaseStateMachine() = default;
    
    StateEnum get_state() const { return current_state_; }
    uint32_t get_state_duration_ms() const {
        return millis() - state_entry_time_;
    }
    
    using StateChangeCallback = std::function<void(StateEnum old_state, StateEnum new_state)>;
    void on_state_changed(StateChangeCallback callback) {
        state_callbacks_.push_back(callback);
    }
    
protected:
    void transition_to(StateEnum new_state) {
        if (current_state_ == new_state) {
            return;  // No change
        }
        
        StateEnum old_state = current_state_;
        current_state_ = new_state;
        state_entry_time_ = millis();
        
        // Log transition
        log_transition(old_state, new_state);
        
        // Notify callbacks
        for (auto& callback : state_callbacks_) {
            callback(old_state, new_state);
        }
    }
    
    virtual void log_transition(StateEnum old_state, StateEnum new_state) {
        LOG_DEBUG("[STATE MACHINE] %d → %d", (int)old_state, (int)new_state);
    }
    
private:
    StateEnum current_state_;
    uint32_t state_entry_time_;
    std::vector<StateChangeCallback> state_callbacks_;
};
```

### Example: Unified Connection State Machine

Both Receiver and Transmitter could use a shared base pattern:

```cpp
// ESP32Common/include/esp32common/espnow/connection_event.h
enum class EspNowConnectionState {
    IDLE,
    CONNECTING,
    CONNECTED
};

class EspNowConnectionManager : public BaseStateMachine<EspNowConnectionState> {
public:
    virtual ~EspNowConnectionManager() = default;
    
    virtual void init() = 0;
    virtual void update() = 0;
    
    bool is_connected() const {
        return get_state() == EspNowConnectionState::CONNECTED;
    }
    
protected:
    void log_transition(EspNowConnectionState old_state, EspNowConnectionState new_state) override {
        static const char* names[] = {
            "IDLE", "CONNECTING", "CONNECTED"
        };
        
        LOG_INFO("[ESPNOW CONNECTION] %s → %s",
                names[(int)old_state], names[(int)new_state]);
    }
};

// Receiver implementation
class RxConnectionManager : public EspNowConnectionManager {
public:
    void init() override;
    void update() override;
    
private:
    // Receiver-specific implementation
};

// Transmitter implementation
class TxConnectionManager : public EspNowConnectionManager {
public:
    void init() override;
    void update() override;
    
private:
    // Transmitter-specific implementation
};
```

### Benefits

- ✅ **Consistency**: Both use same patterns
- ✅ **Unified logging**: Easier to debug state transitions
- ✅ **Code reuse**: Common base class
- ✅ **Testing**: Can test state machine logic independently
- ✅ **Maintainability**: Clear state transition semantics

---

## 4. Singleton vs Dependency Injection

### Priority: 🟡 **MEDIUM**
**Effort**: 2 days combined (long-term refactoring)  
**Blocking**: No - architectural improvement

### Current State

**Both Receiver and Transmitter**:
- Heavy use of singletons (EthernetManager, MqttManager, etc.)
- Cannot test without actual hardware
- Tight coupling between components

### Problem with Singletons

```cpp
// Hard to test - depends on actual hardware
class DataSender {
    void send() {
        // Cannot mock these
        auto& cache = EnhancedCache::instance();
        auto& conn = ConnectionManager::instance();
        auto& mqtt = MqttManager::instance();
    }
};

// Test can't replace these with mocks
TEST(DataSenderTest, SendsWhenConnected) {
    DataSender sender;  // ← Real hardware dependencies
    sender.send();
}
```

### Recommended Pattern (Gradual Migration)

**Phase 1: Interfaces for New Code**

```cpp
// ESP32Common/include/interfaces/idata_sender.h
class IDataSender {
public:
    virtual ~IDataSender() = default;
    virtual bool send_data(const uint8_t* data, size_t length) = 0;
};

class IConnectionManager {
public:
    virtual ~IConnectionManager() = default;
    virtual bool is_connected() const = 0;
    virtual void connect() = 0;
};

class IDataCache {
public:
    virtual ~IDataCache() = default;
    virtual void update(const SensorData& data) = 0;
    virtual SensorData get_latest() const = 0;
};
```

**Phase 2: New Classes Use DI**

```cpp
// Receiver - new code uses DI
class MonitorPage {
public:
    MonitorPage(IConnectionManager& conn, IDataCache& cache)
        : conn_(conn), cache_(cache) {}
    
    void update() {
        if (conn_.is_connected()) {
            auto data = cache_.get_latest();
            render_data(data);
        }
    }
    
private:
    IConnectionManager& conn_;
    IDataCache& cache_;
};

// Can be tested easily
TEST(MonitorPageTest, ShowsDataWhenConnected) {
    MockConnectionManager mock_conn;
    MockDataCache mock_cache;
    
    EXPECT_CALL(mock_conn, is_connected()).WillOnce(Return(true));
    EXPECT_CALL(mock_cache, get_latest()).WillOnce(Return(test_data));
    
    MonitorPage page(mock_conn, mock_cache);
    page.update();
    
    ASSERT_TRUE(page.rendered_correctly());
}
```

**Phase 3: Singletons as Factory**

```cpp
// Gradually replace singleton access with DI
class AppFactory {
public:
    static AppFactory& instance();
    
    IConnectionManager& get_connection_manager();
    IDataCache& get_cache();
    IDataSender& get_data_sender();
    
private:
    // These are still singletons internally,
    // but accessed through factory
    EspNowConnectionManager conn_;
    EnhancedCache cache_;
    DataSender sender_;
};

// Old code
void legacy_function() {
    // Still works
    if (AppFactory::instance().get_connection_manager().is_connected()) {
        // ...
    }
}
```

### Benefits

- ✅ **Testability**: Can mock all dependencies
- ✅ **Flexibility**: Can swap implementations
- ✅ **Decoupling**: Components don't depend on singletons
- ✅ **Gradual migration**: Don't need to refactor all at once
- ✅ **Clearer dependencies**: Constructor shows what's needed

### Implementation Strategy

1. **Define interfaces in ESP32Common** (shared across both codebases)
2. **New code uses DI** (when adding new features)
3. **Existing code uses factory** (gradual migration)
4. **Eventually** most code will use DI, singletons become implementation detail

---

## 5. Non-Blocking Patterns

### Priority: 🟡 **MEDIUM**
**Effort**: 1.5 days combined  
**Blocking**: Partially - improves responsiveness

### Current State

**Receiver**:
- Display uses busy-waiting patterns
- Static variables in functions hide state
- Some blocking in MQTT operations

**Transmitter**:
- Better cache-first pattern (EnhancedCache)
- Some blocking discovery delays
- Could apply non-blocking pattern more broadly

### Unified Non-Blocking Pattern

Both codebases should adopt **"state-based non-blocking"** pattern:

```cpp
// BAD: Blocking approach (both codebases currently use this)
void connect_to_peer() {
    while (!connected) {
        try_to_connect();
        vTaskDelay(100);  // ← Blocks
    }
}

// GOOD: State-machine approach (transmitter has this, receiver should adopt)
class ConnectionHandler {
private:
    enum State { IDLE, CONNECTING, CONNECTED };
    State state_;
    uint32_t start_time_;
    static const uint32_t TIMEOUT_MS = 1000;
    
public:
    void update() {
        switch (state_) {
            case IDLE:
                if (should_connect) {
                    state_ = CONNECTING;
                    start_time_ = millis();
                    initiate_connection();
                }
                break;
                
            case CONNECTING:
                if (is_connected()) {
                    state_ = CONNECTED;
                } else if (millis() - start_time_ > TIMEOUT_MS) {
                    state_ = IDLE;  // Retry
                }
                break;
                
            case CONNECTED:
                if (!is_connected()) {
                    state_ = IDLE;
                }
                break;
        }
    }
};

// Non-blocking - can be called frequently without waste
// No delays, just state transitions
```

### Template for Both Codebases

```cpp
// ESP32Common/include/patterns/non_blocking_operation.h
template<typename Context>
class NonBlockingOperation {
public:
    enum State { IDLE, IN_PROGRESS, SUCCESS, FAILED };
    
    State get_state() const { return state_; }
    bool is_in_progress() const { return state_ == IN_PROGRESS; }
    bool is_complete() const { return state_ == SUCCESS || state_ == FAILED; }
    
    void reset() { state_ = IDLE; }
    
protected:
    void set_in_progress() { state_ = IN_PROGRESS; }
    void set_success() { state_ = SUCCESS; }
    void set_failed() { state_ = FAILED; }
    
    virtual void on_start() = 0;
    virtual void on_update() = 0;  // Called each cycle
    virtual void on_success() = 0;
    virtual void on_failed() = 0;
    
public:
    void update() {
        if (state_ == IDLE) {
            set_in_progress();
            on_start();
        } else if (state_ == IN_PROGRESS) {
            on_update();
        }
    }
    
private:
    State state_ = IDLE;
};

// Example: Non-blocking MQTT connection
class NonBlockingMqttConnect : public NonBlockingOperation<void> {
protected:
    void on_start() override {
        LOG_INFO("Starting MQTT connection...");
        mqtt_->async_connect();  // Non-blocking start
    }
    
    void on_update() override {
        if (mqtt_->is_connected()) {
            set_success();
            on_success();
        } else if (elapsed() > TIMEOUT_MS) {
            set_failed();
            on_failed();
        }
    }
    
    void on_success() override {
        LOG_INFO("MQTT connected");
    }
    
    void on_failed() override {
        LOG_WARN("MQTT connection failed");
    }
};
```

### Benefits

- ✅ **Responsive**: Never blocks the scheduler
- ✅ **Deterministic**: Timing is predictable
- ✅ **Testable**: Each state transition can be tested
- ✅ **Efficient**: CPU time used efficiently
- ✅ **Cascadable**: Can compose multiple non-blocking operations

---

## 6. Connection State Notifications

### Priority: 🟡 **MEDIUM**
**Effort**: 8 hours combined  
**Blocking**: No - improves modularity

### Current State

**Receiver**:
- Uses global `volatile bool transmitter_connected`
- No notifications on state change
- Multiple places poll the global variable

**Transmitter**:
- Multiple connection managers
- No unified notification system
- Components poll instead of react

### Unified Pattern

Create event-driven notifications:

```cpp
// ESP32Common/include/events/connection_events.h
class IConnectionListener {
public:
    virtual ~IConnectionListener() = default;
    
    virtual void on_transmitter_connected() {}
    virtual void on_transmitter_disconnected() {}
    virtual void on_data_received(const DataFrame& data) {}
    virtual void on_error(const ConnectionError& error) {}
};

class ConnectionEventManager {
public:
    static ConnectionEventManager& instance();
    
    void register_listener(IConnectionListener* listener);
    void unregister_listener(IConnectionListener* listener);
    
    // Called by connection managers
    void notify_connected();
    void notify_disconnected();
    void notify_data_received(const DataFrame& data);
    void notify_error(const ConnectionError& error);
    
private:
    std::vector<IConnectionListener*> listeners_;
};

// Receiver: Monitor page subscribes to events
class MonitorPage : public IConnectionListener {
public:
    MonitorPage() {
        ConnectionEventManager::instance().register_listener(this);
    }
    
    ~MonitorPage() {
        ConnectionEventManager::instance().unregister_listener(this);
    }
    
    void on_transmitter_connected() override {
        show_connected_indicator();
    }
    
    void on_transmitter_disconnected() override {
        show_disconnected_indicator();
    }
    
    void on_data_received(const DataFrame& data) override {
        update_display_with_data(data);
    }
};

// Transmitter: LED indicator subscribes to events
class StatusLed : public IConnectionListener {
public:
    StatusLed() {
        ConnectionEventManager::instance().register_listener(this);
    }
    
    void on_transmitter_connected() override {
        set_led_color(GREEN);
    }
    
    void on_transmitter_disconnected() override {
        set_led_color(RED);
    }
};
```

### Benefits

- ✅ **Decoupling**: Components don't depend on each other
- ✅ **Scalability**: Easy to add new listeners
- ✅ **Reactivity**: Components react to changes instead of polling
- ✅ **Testability**: Can simulate events for testing

---

## 7. Debug Logging Consolidation

### Priority: 🟡 **MEDIUM**
**Effort**: 8 hours combined  
**Blocking**: No - quality improvement

### Current State

**Both Codebases**:
- Inconsistent logging patterns
- Mix of `Serial.printf`, `LOG_DEBUG`, `LOG_INFO`
- Hard to enable/disable logging by module
- No timestamp or level control

### Unified Logging Framework

Create centralized logging in ESP32Common:

```cpp
// ESP32Common/include/logging/logger.h
enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    CRITICAL = 4,
    NONE = 5
};

class Logger {
public:
    static Logger& instance();
    
    void set_global_level(LogLevel level);
    void set_module_level(const char* module, LogLevel level);
    
    void log(const char* module, LogLevel level, const char* format, ...);
    
    // Convenience macros
    void log_debug(const char* module, const char* format, ...);
    void log_info(const char* module, const char* format, ...);
    void log_warn(const char* module, const char* format, ...);
    void log_error(const char* module, const char* format, ...);
    
private:
    LogLevel global_level_;
    std::map<std::string, LogLevel> module_levels_;
};

// Macros for easy use
#define LOG_DEBUG(format, ...) \
    Logger::instance().log_debug(__MODULE__, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) \
    Logger::instance().log_info(__MODULE__, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) \
    Logger::instance().log_warn(__MODULE__, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) \
    Logger::instance().log_error(__MODULE__, format, ##__VA_ARGS__)
```

### Usage in Both Codebases

```cpp
// receiver/display/display_core.cpp
#define __MODULE__ "DISPLAY"

void DisplayCore::init() {
    LOG_INFO("Initializing display...");
    // ...
}

// transmitter/network/mqtt_manager.cpp
#define __MODULE__ "MQTT"

void MqttManager::connect() {
    LOG_INFO("Connecting to server...");
    // ...
}
```

### Benefits

- ✅ **Consistency**: All logs use same format
- ✅ **Control**: Enable/disable by module or level
- ✅ **Timestamps**: All logs include timing
- ✅ **Filtering**: Can see logs from specific module
- ✅ **Production-ready**: Can reduce logs at runtime

---

## 8. OTA Firmware Update Coordination

### Priority: 🟡 **MEDIUM**
**Effort**: 1.5 days combined  
**Blocking**: No - but improves reliability

### Current State

**Receiver**:
- Supports OTA updates via web UI
- No coordination with transmitter

**Transmitter**:
- Supports OTA updates via **HTTP** (not MQTT)
- ⚠️ **TO BE VERIFIED**: Confirm OTA mechanism (HTTP vs MQTT) in transmitter codebase
- No coordination with receiver

### Problem

If receiver updates while transmitter is sending data, connection is lost. If transmitter updates, receiver loses connection.

⚠️ **VERIFICATION REQUIRED**: Current implementation needs to be reviewed:
- Receiver: OTA via web UI (HTTP) - **confirmed**
- Transmitter: OTA mechanism needs verification (believed to be HTTP-based, not MQTT)
- Check `src/network/ota_manager.cpp` in transmitter for actual implementation

### Solution

Coordinate updates using connection state:

```cpp
// ESP32Common/include/ota/ota_coordinator.h
// NOTE: Both receiver and transmitter use HTTP-based OTA (web UI for receiver, HTTP client for transmitter)
class OtaCoordinator {
public:
    static OtaCoordinator& instance();
    
    enum class DeviceType { RECEIVER, TRANSMITTER };
    
    bool request_update(DeviceType device_type, const char* url);
    bool cancel_update();
    
    bool is_update_in_progress() const;
    bool is_device_available(DeviceType device_type) const;
    
private:
    bool notify_peer_before_update();
    bool wait_for_peer_to_prepare();
};

// Receiver usage
void handle_ota_request() {
    // Ask transmitter to prepare for update
    if (!OtaCoordinator::instance().request_update(DeviceType::RECEIVER, url)) {
        return;  // Transmitter not ready
    }
    
    // Perform update while transmitter is silent
}

// Transmitter usage (HTTP-based OTA)
void check_for_updates() {
    // Before starting HTTP-based update, notify receiver
    if (!OtaCoordinator::instance().request_update(DeviceType::TRANSMITTER, url)) {
        return;  // Receiver not ready
    }
    
    // Perform HTTP update while receiver is waiting
    // Note: Transmitter uses HTTP client for OTA, not MQTT
}
```

### Benefits

- ✅ **Coordinated**: Both devices aware of updates
- ✅ **Reliable**: Updates don't drop connection
- ✅ **Safe**: Can validate updates completed
- ✅ **Graceful**: Can schedule updates when convenient

---

## Summary

### Implementation Roadmap

**Phase 1** (Essential - Week 1-2):
- ✅ Centralize timing constants
- ✅ Create logging framework

**Phase 2** (Important - Week 3-4):
- ✅ Define HAL pattern
- ✅ Create connection event system
- ✅ Establish non-blocking patterns

**Phase 3** (Long-term - Week 5-6):
- ✅ Create interface definitions for DI
- ✅ Migrate new code to use DI
- ✅ Coordinate OTA updates

### Total Effort

- **Phase 1**: 1.5 days
- **Phase 2**: 2 days
- **Phase 3**: 2.5 days (long-term)

**Total**: ~6 days for shared improvements

### Key Principles

1. **Single Source of Truth**: Shared constants in ESP32Common
2. **Interface-Based**: Both codebases depend on abstractions, not implementations
3. **Event-Driven**: Components react to state changes
4. **Non-Blocking**: No blocking operations in critical paths
5. **Testable**: All components can be tested independently
6. **Maintainable**: Clear separation of concerns

### Architecture Benefits

After implementing these changes:

- Both receiver and transmitter will have consistent architecture
- Shared code lives in ESP32Common
- Codebase-specific improvements are isolated
- New developers can understand patterns across both devices
- Testing is possible without hardware
- System is more resilient and maintainable

---

## ⚠️ Phase 0 Implementation Status

**Important**: See [ESPNOW_STATE_MACHINE_PHASE0_FINDINGS.md](./ESPNOW_STATE_MACHINE_PHASE0_FINDINGS.md) for critical findings about the Phase 0 implementation.

Phase 0 (Robust Reconnection with heartbeat monitoring and exponential backoff) has exposed **fundamental architectural issues** in the connection state machine that require a more comprehensive redesign. While the individual components work, the integrated system does not gracefully handle disconnections, timeouts, and reconnections.

**Key Issues**:
- Multiple independent timeout checkers causing race conditions
- State-dependent behavior scattered across multiple handler classes
- Message routing filtering causing data staleness detection failures
- transmission_active_ flag not properly managed by state machine

See the linked document for detailed analysis and recommendations for Phase 1 redesign.

---

## 📌 March 13, 2026 Status Refresh (What Still Needs to Be Done in ESP32Common)

This section reflects the current cross-codebase state after recent receiver/transmitter migrations and reconnect hardening work.

### What is already in place in ESP32Common

- ✅ Shared ESP-NOW utility layer exists (`espnow_common_utils/`) including connection manager, event processing, routing, queue, peer/channel helpers.
- ✅ Reconnect/heartbeat primitives exist (`espnow_phase0/`) and are integrated into current architecture.
- ✅ Shared logging utilities exist (`logging_utilities/logging_config.h`, MQTT logger integration).
- ✅ Shared protocol header has grown substantially (`espnow_transmitter/espnow_common.h`) for config sync, heartbeat, versioning, component config, etc.
- ✅ Shared config sync module exists (`config_sync/`) and is documented as implemented.
- ✅ Shared packet parsing helper exists (`espnow_common_utils/espnow_packet_utils.h`).

### Remaining work for the **common** codebase

1. [x] **Create a stable public include surface for common modules**
    - Current design intent in this document references `include/config`, `include/state`, `include/events`, `include/interfaces`, etc.
    - Actual implementation is spread across `espnow_common_utils/`, `espnow_phase0/`, `logging_utilities/`, `config_sync/`.
        - ✅ Implemented canonical wrapper paths under `include/esp32common/*`.
        - Canonical paths include:
            - `esp32common/espnow/common.h`
            - `esp32common/espnow/connection_manager.h`
            - `esp32common/espnow/connection_event.h`
            - `esp32common/espnow/connection_event_processor.h`
            - `esp32common/espnow/message_router.h`
            - `esp32common/espnow/message_queue.h`
            - `esp32common/espnow/standard_handlers.h`
            - `esp32common/espnow/packet_utils.h`
            - `esp32common/espnow/timing_config.h`
            - `esp32common/espnow/heartbeat_monitor.h`
            - `esp32common/espnow/reconnection_backoff.h`
            - `esp32common/logging/logging_config.h`
        - ✅ TX/RX include migration completed: active transmitter/receiver code now uses canonical `esp32common/...` include paths.
        - ✅ Common-source cleanup completed: remaining active `esp32common` source that previously referenced root shim headers now uses canonical public paths or direct internal implementation headers as appropriate.
        - ✅ Deprecated root shim headers removed from `include/` after migration verification; the old include surface is no longer present in the codebase.

2. [x] **Finish timing centralization at common level**
    - `esp32common/espnow_common_utils/espnow_timing_config.h` exists (ESP-NOW focused).
    - Transmitter previously had its own `src/config/timing_config.h` include path usage.
    - ✅ Added canonical shared contract: `include/esp32common/config/timing_config.h`.
    - ✅ Removed the temporary timing compatibility wrappers after migration verification:
        - `include/config/timing_config.h`
        - `ESPnowtransmitter2/src/config/timing_config.h`
    - ✅ Migrated transmitter active source include usage to canonical `#include <esp32common/config/timing_config.h>`.
    - Result: timing values now have a single authoritative source in ESP32Common for cross-device domains (discovery, heartbeat, MQTT, OTA, task loop timing).

3. [x] **Implement dynamic battery/inverter type discovery protocol in common header**
    - Receiver still uses static type arrays in web API handlers.
    - `espnow_common.h` does not yet define the dedicated type-list request/response message family proposed in this doc.
        - ✅ Added protocol messages and wire structures in `esp32common/espnow_transmitter/espnow_common.h`:
            - `msg_request_battery_types`, `msg_battery_types_fragment`
            - `msg_request_inverter_types`, `msg_inverter_types_fragment`
            - `type_catalog_request_t`, `type_catalog_fragment_t`, `type_catalog_entry_t`
        - ✅ Implemented transmitter handlers to source names from authoritative Battery Emulator mappings and send fragmented catalogs.
        - ✅ Implemented receiver cache/assembly for catalog fragments and wired routes in ESP-NOW task router.
        - ✅ Updated receiver web API handlers to use dynamic cache and trigger discovery when cache is empty.
        - ✅ Removed old hardcoded receiver `battery_types[]` and `inverter_types[]` arrays from API handlers.

4. [x] **Resolve state-model duplication in common architecture**
    - ✅ Canonical shared connection lifecycle model is now the existing 3-state `EspNowConnectionState` in `espnow_common_utils/connection_event.h` (`IDLE`, `CONNECTING`, `CONNECTED`).
    - ✅ The separate expanded experimental state model has been removed from active code/docs for now.
    - ✅ Rationale: richer state granularity is currently unnecessary complexity relative to active runtime needs and increases ambiguity across callbacks/logging.
    - Future option: reintroduce additional diagnostic-only substates later if concrete production requirements emerge.

5. [x] **Fix packaging/build exposure gaps in ESP32Common**
    - ✅ Root `library.json` `srcFilter` now explicitly includes `espnow_phase0/` alongside `espnow_common_utils/`.
    - ✅ Common manager dependencies on heartbeat/backoff phase0 components are now explicitly compiled as part of the packaged common library.
    - ✅ Result: TX/RX consumption of `esp32common` is self-contained and no longer depends on implicit/nested-library discovery behavior.

6. [x] **Deliver planned generic abstraction layers not yet materialized in common**
    - ✅ Added shared public interface contracts under `include/esp32common/interfaces/`:
        - `iconnection_manager.h`
        - `idata_sender.h`
        - `idata_cache.h`
    - ✅ Added shared non-blocking pattern base under `include/esp32common/patterns/`:
        - `non_blocking_operation.h`
    - ✅ Added formal connection event-notification API under `include/esp32common/events/`:
        - `connection_events.h` (`IConnectionListener`, `ConnectionEventManager`)
    - ✅ Added shared OTA coordination abstraction under `include/esp32common/ota/`:
        - `ota_coordinator.h`
    - Result: planned abstraction APIs are now materialized as a unified public include surface in ESP32Common.

7. [ ] **Add shared automated validation for common cross-device behavior**
    - Current tests in `esp32common/tests/` are minimal and do not cover reconnect matrix, event sequencing, or protocol compatibility.
    - Action: add host/unit/integration tests for state transitions, heartbeat timeout behavior, backoff, and message schema compatibility.

### Suggested execution order for common-code completion

1. Public include surface + packaging cleanup
2. Timing contract unification
3. Canonical state model decision
4. Dynamic type-discovery message family
5. Missing abstraction APIs (or explicit de-scope)
6. Common automated test matrix

### ✅ Mandatory cleanup rule (applies to every step)

For each implementation step, completion means:

1. New approach is implemented and validated.
2. **All old/redundant code for that step is removed** (no permanent dual paths).
3. Temporary compatibility shims are tracked with explicit removal follow-up and then deleted once migration is complete.

Current Step 1 status: this rule is fully satisfied. Active source paths were migrated first, then the deprecated root shim headers were deleted from the repository so the old include surface cannot regress back into use.

---

## Version History

| Date | Author | Change |
|------|--------|--------|
| Feb 26, 2026 | Original | Initial document creation |
| March 5, 2026 | AI Assistant | Added Item #2 (Dynamic Type Discovery via ESP-NOW) with investigation notes, fallback to MQTT option, and implementation plan |
| March 5, 2026 | AI Assistant | Added Phase 0 findings reference and link to detailed analysis document |
| March 13, 2026 | AI Assistant | Added status refresh section identifying outstanding ESP32Common implementation gaps after recent TX/RX migration work |
| March 13, 2026 | AI Assistant | Implemented Step 1 stable public include surface (`include/esp32common/*`) with legacy compatibility shims |
| March 13, 2026 | AI Assistant | Implemented Step 2 timing centralization with canonical `esp32common/config/timing_config.h` and removed duplicated transmitter timing constants |
| March 13, 2026 | AI Assistant | Implemented Step 3 dynamic battery/inverter type discovery over ESP-NOW with fragmented catalog responses; removed hardcoded receiver type arrays |
| March 15, 2026 | AI Assistant | Completed TX/RX canonical include-path migration cleanup (`esp32common/...`) and verified both transmitter and receiver builds pass |
| March 15, 2026 | AI Assistant | Completed final common-source include cleanup pass and removed deprecated root shim headers from `include/`; both transmitter and receiver builds still pass |
| March 15, 2026 | AI Assistant | Fixed receiver `/transmitter/hardware` and `/transmitter/battery` save-button dirty-state behavior to match `/transmitter/inverter` (disabled when unchanged, enabled on edits, stale saved-status cleared on new edits) |
| March 15, 2026 | AI Assistant | Removed references to the unused expanded ESP-NOW connection model and documented the decision to keep the canonical 3-state model for now due to unnecessary complexity |
| March 15, 2026 | AI Assistant | Closed Item #5 packaging gap by adding `espnow_phase0/` to root `esp32common/library.json` `srcFilter`, ensuring phase0 heartbeat/backoff sources are explicitly built with the common library |
| March 15, 2026 | AI Assistant | Implemented Item #6 abstraction-layer API surface under `include/esp32common/interfaces`, `patterns`, `events`, and `ota` (DI contracts, non-blocking base, connection-event manager, OTA coordinator) |
| March 15, 2026 | AI Assistant | Removed the final temporary timing/include compatibility wrappers and deleted obsolete receiver manual type-mapping docs so the cleanup rule is now satisfied without dual paths |


