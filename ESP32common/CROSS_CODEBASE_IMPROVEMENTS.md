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

## 2. Hardware Abstraction Layer (HAL) Pattern

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
// ESP32Common/include/state/espnow_connection_state.h
enum class EspNowConnectionState {
    // Initialization
    UNINITIALIZED,
    INITIALIZING,
    
    // Discovery (both need this)
    IDLE,
    DISCOVERING,
    WAITING_FOR_ACK,
    ACK_RECEIVED,
    
    // Connection
    CONNECTED,
    DEGRADED,
    
    // Disconnection
    DISCONNECTING,
    DISCONNECTED,
    
    // Error/Recovery
    CONNECTION_LOST,
    RECONNECTING,
    ERROR_STATE
};

class EspNowConnectionManager : public BaseStateMachine<EspNowConnectionState> {
public:
    virtual ~EspNowConnectionManager() = default;
    
    virtual void init() = 0;
    virtual void update() = 0;
    
    bool is_connected() const {
        return get_state() == EspNowConnectionState::CONNECTED;
    }
    
    bool is_discovering() const {
        return get_state() >= EspNowConnectionState::DISCOVERING &&
               get_state() <= EspNowConnectionState::ACK_RECEIVED;
    }
    
protected:
    void log_transition(EspNowConnectionState old_state, EspNowConnectionState new_state) override {
        static const char* names[] = {
            "UNINITIALIZED", "INITIALIZING",
            "IDLE", "DISCOVERING", "WAITING_FOR_ACK", "ACK_RECEIVED",
            "CONNECTED", "DEGRADED",
            "DISCONNECTING", "DISCONNECTED",
            "CONNECTION_LOST", "RECONNECTING", "ERROR_STATE"
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

