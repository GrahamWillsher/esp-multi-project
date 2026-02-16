# ESP-NOW State Machine Redesign - Complete Architecture

**Status:** REDESIGN PROPOSAL  
**Date:** 2026-02-13  
**Focus:** Simple, Reliable, Maintainable, FreeRTOS-Native  
**Framework:** FreeRTOS-Based ESP32

---

## Executive Summary

**Problem:** Current ESP-NOW connection managers are over-engineered (10-17 states), poorly integrated with FreeRTOS, and have no working discovery trigger.

**Solution:** Complete redesign using:
1. **Event-Driven Architecture** - State changes triggered by actual events (callbacks)
2. **Simplified States** - 3 core states instead of 10-17
3. **FreeRTOS Native** - Uses queues and tasks, not polling
4. **Message-Based Communication** - State transitions via message queue
5. **Proven Patterns** - Based on working system state machine approach

---

## Part 1: Logging System Review & Improvements

### Current Logging Issues Found

**Issue #1: Dual Logging Path Overhead**
```cpp
// Current (in logging_config.h)
#define LOG_INFO(tag, fmt, ...) if (current_log_level >= LOG_INFO) { \
    Serial.printf("[INFO][%s] " fmt "\n", tag, ##__VA_ARGS__); \
    MQTT_LOG_INFO(tag, fmt, ##__VA_ARGS__);  // Double logging
}
```

**Problem:** Every log is sent twice (Serial + MQTT). On high-throughput code, this causes:
- Serial buffer congestion
- MQTT client slowdown
- Dropped messages

**Status:** ✓ FUNCTIONAL but inefficient

**Recommendation:** Add buffering and rate limiting (see Section 5.2 below)

---

### Issue #2: No Log Level Synchronization

**Current State:**
- MQTT can change log levels dynamically
- But Serial logging always uses `current_log_level`
- They can be out of sync during updates

**Code Location:** `message_handler.cpp::handle_debug_control()`
```cpp
void EspnowMessageHandler::handle_debug_control(const espnow_queue_msg_t& msg) {
    // ... code that changes MqttLogger level ...
    MqttLogger::instance().set_level(pkt->level);
    // But current_log_level (Serial) not updated!
}
```

**Status:** ⚠️ PARTIALLY BROKEN - Only MQTT level changes, serial unaffected

**Fix:** Sync both log levels
```cpp
// When MQTT level changes
MqttLogger::instance().set_level(pkt->level);
current_log_level = (LogLevel)pkt->level;  // ADD THIS
save_debug_level(pkt->level);
```

---

### Issue #3: Message Router Over-Engineered

**Current:** Uses `std::function<>` callbacks (small overhead but unnecessary)

**Better:** Static function table (zero overhead, same functionality)

```cpp
// Current (in transmitter message_handler.cpp)
router.register_route(msg_probe, [](const espnow_queue_msg_t* msg, void* ctx) {
    static_cast<EspnowMessageHandler*>(ctx)->handle_probe(*msg);
}, 0xFF, this);

// Proposed (simpler, faster)
router.register_route(msg_probe, EspnowStandardHandlers::handle_probe, 0xFF, nullptr);
```

**Status:** ✓ WORKING but unnecessarily complex

---

## Part 2: Simplified Connection State Machine

### Core Principle

**Old Approach:** Passive state tracker with 17 states  
**New Approach:** Event-driven state machine with 3 states  
**Code Reuse Principle:** All core logic in esp32common, device-specific wrappers only

### 3-State Model (COMMON CODE)

```cpp
// FILE: esp32common/espnow_common_utils/connection_manager.h
// COMPLETELY GENERIC - USED BY BOTH TRANSMITTER AND RECEIVER

enum class EspNowConnectionState : uint8_t {
    IDLE = 0,       // Not connected, ready to start
    CONNECTING = 1, // Active connection process in progress
    CONNECTED = 2   // Actively connected to peer
};

// Generic event types (work for both TX and RX)
enum class EspNowEvent : uint8_t {
    CONNECTION_START,       // Start connection attempt
    PEER_FOUND,            // Peer discovered/received
    PEER_REGISTERED,       // Peer added to ESP-NOW
    DATA_RECEIVED,         // Data received from peer
    CONNECTION_LOST,       // Connection timeout
    RESET_CONNECTION       // Manual reset
};

// Generic manager class - NO DEVICE-SPECIFIC CODE
class EspNowConnectionManager {
public:
    static EspNowConnectionManager& instance();
    
    bool init();
    void post_event(EspNowEvent event, const uint8_t* mac = nullptr);
    void process_events();
    
    // Queries only - no side effects
    EspNowConnectionState get_state() const;
    bool is_connected() const;
    bool is_connecting() const;
    bool is_idle() const;
    const char* get_state_string() const;
    
    // Metrics tracking (local to manager)
    uint32_t get_connected_time_ms() const;
    
private:
    // ONLY DEVICE-AGNOSTIC STATE
    EspNowConnectionState current_state_;
    uint32_t state_enter_time_;
    QueueHandle_t event_queue_;
    
    void handle_event(const EspNowStateChange& event);
    void transition_to_state(EspNowConnectionState new_state);
};
```

**Why 3 States?**
- **IDLE**: Device is ready. No connection attempt in progress.
- **CONNECTING**: Connection process active (discovery details hidden)
- **CONNECTED**: Peer registered and actively communicating

---

### Event-Driven Transitions (COMMON LOGIC)

All devices follow the same state machine:

```
IDLE
  ↓ [post_event(CONNECTION_START)]
  CONNECTING
    ↓ [post_event(PEER_FOUND)]
    ↓ [post_event(PEER_REGISTERED)]
    CONNECTED
      ↓ [post_event(CONNECTION_LOST)]
      IDLE

Device-specific behavior: HOW each event is triggered
- Transmitter: CONNECTION_START → broadcast discovery probes
- Receiver: CONNECTION_START → listen for probes
- Both: PEER_FOUND → same event, different how we found it
```

---

## Part 3: Architecture Design (Common Code First)

### 3.1 Core Connection Manager (COMMON - esp32common)

**File:** `esp32common/espnow_common_utils/connection_manager.h`

**Philosophy:** Zero device-specific code. Pure state machine logic.

```cpp
/**
 * @file connection_manager.h
 * @brief Generic ESP-NOW connection manager (common code)
 * 
 * - 3 core states (IDLE, CONNECTING, CONNECTED)
 * - Event-driven transitions
 * - FreeRTOS-native (uses queues)
 * - Completely device-agnostic
 * 
 * DEVICE-SPECIFIC CODE: Only posts events, doesn't implement logic
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstdint>

enum class EspNowConnectionState : uint8_t {
    IDLE = 0,
    CONNECTING = 1,
    CONNECTED = 2
};

enum class EspNowEvent : uint8_t {
    CONNECTION_START,       // Generic: start connection (TX: discovery, RX: listening)
    PEER_FOUND,            // Generic: peer discovered/received
    PEER_REGISTERED,       // Generic: peer added to ESP-NOW
    DATA_RECEIVED,         // Generic: data from peer received
    CONNECTION_LOST,       // Generic: connection timeout
    RESET_CONNECTION       // Generic: reset to IDLE
};

struct EspNowStateChange {
    EspNowEvent event;
    uint8_t peer_mac[6];
    uint32_t timestamp;
};

class EspNowConnectionManager {
public:
    static EspNowConnectionManager& instance();
    
    bool init();
    void post_event(EspNowEvent event, const uint8_t* mac = nullptr);
    void process_events();  // Call from task (common)
    
    // State queries (const, no side effects)
    EspNowConnectionState get_state() const { return current_state_; }
    bool is_idle() const { return current_state_ == EspNowConnectionState::IDLE; }
    bool is_connecting() const { return current_state_ == EspNowConnectionState::CONNECTING; }
    bool is_connected() const { return current_state_ == EspNowConnectionState::CONNECTED; }
    bool is_ready_to_send() const { return is_connected(); }
    const char* get_state_string() const;
    uint32_t get_connected_time_ms() const;
    
private:
    // COMMON STATE ONLY - No device-specific code
    EspNowConnectionState current_state_;
    uint32_t state_enter_time_;
    QueueHandle_t event_queue_;
    
    EspNowConnectionManager();
    
    void handle_event(const EspNowStateChange& event);
    void transition_to_state(EspNowConnectionState new_state);
};

// Global queue (created by common code)
extern QueueHandle_t g_connection_event_queue;

// Helper for ISR context (common utility)
inline bool post_connection_event(EspNowEvent event, const uint8_t* mac = nullptr) {
    EspNowStateChange change;
    change.event = event;
    change.timestamp = millis();
    if (mac) memcpy(change.peer_mac, mac, 6);
    else memset(change.peer_mac, 0, 6);
    
    if (g_connection_event_queue != nullptr) {
        return xQueueSendFromISR(g_connection_event_queue, &change, nullptr) == pdTRUE;
    }
    return false;
}
```

**Implementation:** `esp32common/espnow_common_utils/connection_manager.cpp`
- Generic event handler
- State transitions
- Logging
- No device-specific code whatsoever

---

### 3.2 Transmitter Device Layer (LOCAL - transmitter)

**File:** `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_connection_handler.h`

**Philosophy:** ONLY handles transmitter-specific operations. Uses common manager.

```cpp
/**
 * @file tx_connection_handler.h
 * @brief Transmitter-specific connection logic (local only)
 * 
 * Handles:
 * - LOCAL: Discovery probe broadcasting
 * - LOCAL: When to start discovery
 * - LOCAL: ACK reception from receiver
 * - LOCAL: Peer registration with receiver MAC/channel
 * - DELEGATE: State machine to EspNowConnectionManager (common)
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class TransmitterConnectionHandler {
public:
    static TransmitterConnectionHandler& instance();
    
    // LOCAL: Start discovery (TX-specific implementation)
    bool start_discovery();
    void stop_discovery();
    
    // LOCAL: Called from ESP-NOW callback when ACK received
    void on_ack_received(const uint8_t* receiver_mac, uint8_t channel);
    
    // LOCAL: Called from ESP-NOW callback when data received
    void on_data_received(const uint8_t* receiver_mac);
    
    // LOCAL: Queries for transmitter state
    const uint8_t* get_receiver_mac() const { return receiver_mac_; }
    uint8_t get_receiver_channel() const { return receiver_channel_; }
    
    // DELEGATE: Use common manager
    bool is_connected() const;
    
private:
    // LOCAL STATE - DEVICE SPECIFIC
    uint8_t receiver_mac_[6];
    uint8_t receiver_channel_;
    TaskHandle_t discovery_task_handle_;
    uint32_t discovery_start_time_;
    
    TransmitterConnectionHandler();
    
    // LOCAL: Discovery broadcast loop
    void discovery_task_loop();
    static void discovery_task_wrapper(void* param);
};
```

**Responsibilities:**
- ✓ Broadcast PROBE packets
- ✓ Handle ACK reception
- ✓ Register peer with ESP-NOW
- ✓ Manage discovery timeout
- ✓ Post events to common manager
- ✗ Do NOT implement state machine (use common manager)
- ✗ Do NOT track state (common manager does this)

---

### 3.3 Receiver Device Layer (LOCAL - receiver)

**File:** `espnowreciever_2/src/espnow/rx_connection_handler.h`

**Philosophy:** ONLY handles receiver-specific operations. Uses common manager.

```cpp
/**
 * @file rx_connection_handler.h
 * @brief Receiver-specific connection logic (local only)
 * 
 * Handles:
 * - LOCAL: Listening for PROBE
 * - LOCAL: When probe received, send ACK
 * - LOCAL: Data reception tracking
 * - LOCAL: Connection timeout monitoring
 * - DELEGATE: State machine to EspNowConnectionManager (common)
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class ReceiverConnectionHandler {
public:
    static ReceiverConnectionHandler& instance();
    
    bool init();
    
    // LOCAL: Called from ESP-NOW callback when PROBE received
    void on_probe_received(const uint8_t* transmitter_mac, uint8_t channel);
    
    // LOCAL: Called from ESP-NOW callback when data received
    void on_data_received(const uint8_t* transmitter_mac);
    
    // LOCAL: Timeout monitoring (runs periodically)
    void check_timeout();
    
    // LOCAL: Queries for receiver state
    const uint8_t* get_transmitter_mac() const { return transmitter_mac_; }
    
    // DELEGATE: Use common manager
    bool is_connected() const;
    
private:
    // LOCAL STATE - DEVICE SPECIFIC
    uint8_t transmitter_mac_[6];
    uint8_t transmitter_channel_;
    uint32_t last_data_time_;
    TaskHandle_t timeout_task_handle_;
    
    ReceiverConnectionHandler();
    
    // LOCAL: Timeout monitor loop
    void timeout_check_loop();
    static void timeout_check_wrapper(void* param);
};
```

**Responsibilities:**
- ✓ Listen for PROBE packets
- ✓ Send ACK responses
- ✓ Register peer with ESP-NOW
- ✓ Monitor connection timeout
- ✓ Post events to common manager
- ✗ Do NOT implement state machine (use common manager)
- ✗ Do NOT track state (common manager does this)

---

### 3.4 Event Processing Task (COMMON UTILITY)

**File:** `esp32common/espnow_common_utils/connection_event_processor.h`

```cpp
/**
 * @file connection_event_processor.h
 * @brief Common event processor task wrapper
 * 
 * Both TX and RX use identical code to process events.
 * Just call this from both projects.
 */

#pragma once

#include <freertos/FreeRTOS.h>

// Common event processor task (both projects use this)
void connection_event_processor_task(void* param);

// Helper to create the task (common)
inline TaskHandle_t create_connection_event_processor(uint8_t priority, uint8_t core) {
    TaskHandle_t handle = nullptr;
    xTaskCreatePinnedToCore(
        connection_event_processor_task,
        "ConnEvents",
        2048,
        nullptr,
        priority,
        &handle,
        core
    );
    return handle;
}
```

**Implementation (common):**
```cpp
void connection_event_processor_task(void* param) {
    while (true) {
        EspNowConnectionManager::instance().process_events();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

This is 100% identical for both transmitter and receiver!

---

## Part 4: Code Organization Summary

### What Goes in esp32common

```
esp32common/espnow_common_utils/
├── connection_manager.h              [NEW] Generic state machine
├── connection_manager.cpp            [NEW] Implementation
├── connection_event.h                [NEW] Event types
├── connection_event_processor.h      [NEW] Task wrapper
├── connection_event_processor.cpp    [NEW] Task implementation
└── espnow_message_router.h           [EXISTING] Message routing
```

**Principle:** PURE COMMON CODE - No device knowledge

---

### What Goes in Transmitter (LOCAL)

```
ESPnowtransmitter2/espnowtransmitter2/src/espnow/
├── tx_connection_handler.h           [NEW] TX-specific
├── tx_connection_handler.cpp         [NEW] TX implementation
├── discovery_task.h                  [EXISTING] Modified to post events
├── discovery_task.cpp                [EXISTING] Modified to post events
└── message_handler.cpp               [EXISTING] Posts events on ACK

Plus in main.cpp:
├── Initialize EspNowConnectionManager
├── Initialize TransmitterConnectionHandler
├── Create event processor task
└── Call start_discovery()
```

**Principle:** ONLY how to broadcast discovery, when to register peer, local metrics

---

### What Goes in Receiver (LOCAL)

```
espnowreciever_2/src/espnow/
├── rx_connection_handler.h           [NEW] RX-specific
├── rx_connection_handler.cpp         [NEW] RX implementation
├── espnow_tasks.cpp                  [EXISTING] Posts events on PROBE
└── espnow_callbacks.cpp              [EXISTING] Posts events

Plus in main.cpp:
├── Initialize EspNowConnectionManager
├── Initialize ReceiverConnectionHandler
├── Create event processor task
└── Start listening
```

**Principle:** ONLY how to listen for probes, when to send ACK, local metrics

---

## Part 5: Code Reuse Comparison

### SHARED COMMON CODE (100% identical for both)

```cpp
// esp32common
EspNowConnectionManager:
    - get_state()
    - is_connected()
    - post_event()
    - process_events()
    - handle_event()
    - transition_to_state()
    - Metrics tracking
    - Event queue management

connection_event_processor_task()
    - Identical for both
    - Called from both main.cpp files

EspNowConnectionState enum
    - Same 3 states for both

EspNowEvent enum
    - Same event types for both
```

**Code Lines:** ~300 lines (common)  
**Reuse:** 100% (both projects)

---

### DEVICE-SPECIFIC CODE (Only device behavior)

```cpp
// Transmitter only
TransmitterConnectionHandler:
    - start_discovery()        [How to broadcast]
    - on_ack_received()        [Register peer]
    - discovery_task_loop()    [Broadcast logic]
    ~100 lines

// Receiver only
ReceiverConnectionHandler:
    - on_probe_received()      [Send ACK]
    - on_data_received()       [Update timeout]
    - check_timeout()          [Detect loss]
    - timeout_check_loop()     [Monitor loop]
    ~100 lines
```

**Code Lines:** ~200 lines (device-specific)  
**Reuse:** 0% (device-specific)

---

## Part 6: Implementation Pattern

### For Transmitter

```cpp
// main.cpp
void setup() {
    // ... ESP-NOW init ...
    
    // Initialize COMMON manager
    EspNowConnectionManager::instance().init();
    
    // Initialize LOCAL handler
    TransmitterConnectionHandler::instance();
    
    // Create event processor task (COMMON)
    create_connection_event_processor(3, 0);
    
    // Start discovery (LOCAL implementation)
    TransmitterConnectionHandler::instance().start_discovery();
}

// callback
void on_espnow_recv(const uint8_t* mac, const uint8_t* data, int len) {
    if (is_ack_packet(data)) {
        // LOCAL: Extract MAC and channel
        TransmitterConnectionHandler::instance().on_ack_received(mac, channel);
        
        // LOCAL: Register peer
        esp_now_add_peer(&peer_info);
        
        // COMMON: Post event to state machine
        post_connection_event(EspNowEvent::PEER_REGISTERED, mac);
    }
}
```

### For Receiver

```cpp
// main.cpp
void setup() {
    // ... ESP-NOW init ...
    
    // Initialize COMMON manager
    EspNowConnectionManager::instance().init();
    
    // Initialize LOCAL handler
    ReceiverConnectionHandler::instance().init();
    
    // Create event processor task (COMMON)
    create_connection_event_processor(3, 0);
}

// callback
void on_espnow_recv(const uint8_t* mac, const uint8_t* data, int len) {
    if (is_probe_packet(data)) {
        // LOCAL: Handle PROBE
        ReceiverConnectionHandler::instance().on_probe_received(mac, channel);
        
        // LOCAL: Send ACK
        send_ack_response(mac, channel);
        
        // LOCAL: Register peer
        esp_now_add_peer(&peer_info);
        
        // COMMON: Post event to state machine
        post_connection_event(EspNowEvent::PEER_FOUND, mac);
    }
}

// periodic task
void timeout_check_task(void* param) {
    while (true) {
        ReceiverConnectionHandler::instance().check_timeout();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

---

## Part 4: Event Flow & Integration

### 4.1 Transmitter Discovery Flow

```
main.cpp setup():
    ├─ EspnowMessageHandler::start_rx_task()
    ├─ esp_now_register_recv_cb(on_data_recv)
    ├─ esp_now_register_send_cb(on_espnow_sent)
    └─ TransmitterConnection::start_discovery() ← TRIGGER START
       ├─ Creates discovery_task
       └─ Posts DISCOVERY_START event

discovery_task (runs every 100ms):
    ├─ Broadcast PROBE
    └─ Wait for ACK

on_espnow_recv_callback (FreeRTOS ISR):
    ├─ Check if ACK packet
    └─ TransmitterConnection::on_ack_received() ← EVENT
       ├─ Extract receiver MAC & channel
       ├─ Post ACK_RECEIVED event to queue
       └─ Return (callback ends)

EspnowConnectionManager event processor (from some task):
    ├─ Receive ACK_RECEIVED event
    ├─ Register peer with receiver MAC + channel
    ├─ Post PEER_REGISTERED event
    └─ Transition to CONNECTED

Result: State machine progresses through events!
```

---

### 4.2 Receiver Detection Flow

```
main.cpp setup():
    ├─ ReceiverConnection::init()
    ├─ esp_now_register_recv_cb(on_data_recv)
    └─ Create timeout_check_task

on_espnow_recv_callback (FreeRTOS ISR):
    ├─ Check if PROBE packet
    └─ ReceiverConnection::on_probe_received() ← EVENT
       ├─ Extract transmitter MAC & channel
       ├─ Send ACK response
       ├─ Post PROBE_RECEIVED event to queue
       └─ Return

EspnowConnectionManager event processor:
    ├─ Receive PROBE_RECEIVED event
    ├─ Register transmitter as peer
    ├─ Transition to CONNECTING
    └─ Start waiting for data

timeout_check_task (runs every 1000ms):
    ├─ Check time since last data received
    ├─ If > 10 seconds: no data
    │  └─ Post CONNECTION_LOST event
    └─ Else: update last_data_time

Result: Receiver automatically detects transmitter!
```

---

## Part 5: Implementation Details

### 5.1 Event Queue Architecture

```cpp
/**
 * @file connection_event_queue.h
 * @brief FreeRTOS queue for connection events
 */

// Global queue (created in main.cpp setup)
extern QueueHandle_t g_connection_event_queue;

// Post event from callback (ISR-safe)
inline bool post_connection_event(EspNowEvent event, const uint8_t* mac = nullptr) {
    EspNowStateChange change;
    change.event = event;
    change.timestamp = millis();
    
    if (mac) {
        memcpy(change.peer_mac, mac, 6);
    } else {
        memset(change.peer_mac, 0, 6);
    }
    
    return xQueueSendFromISR(g_connection_event_queue, &change, nullptr) == pdTRUE;
}

// Process events (from task)
void process_connection_events() {
    EspNowStateChange event;
    
    while (xQueueReceive(g_connection_event_queue, &event, 0) == pdTRUE) {
        EspNowConnectionManager::instance().handle_event(event);
    }
}
```

---

### 5.2 Logging Improvements

**Fix #1: Dual Path Logging**
```cpp
// In logging_config.h - USE RATE LIMITING
#define LOG_INFO(tag, fmt, ...) do { \
    static uint32_t last_log = 0; \
    uint32_t now = millis(); \
    if (now - last_log > 100) {  /* Max 10 logs/sec */ \
        if (current_log_level >= LOG_INFO) { \
            Serial.printf("[INFO][%s] " fmt "\n", tag, ##__VA_ARGS__); \
            if (LOG_USE_MQTT) { \
                MQTT_LOG_INFO(tag, fmt, ##__VA_ARGS__); \
            } \
        } \
        last_log = now; \
    } \
} while(0)
```

**Fix #2: Synchronized Log Levels**
```cpp
// In message_handler.cpp
void EspnowMessageHandler::handle_debug_control(const espnow_queue_msg_t& msg) {
    const debug_control_t* pkt = reinterpret_cast<const debug_control_t*>(msg.data);
    
    // Validate
    if (pkt->level > MQTT_LOG_DEBUG) {
        LOG_WARN("DEBUG_CTRL", "Invalid debug level: %u", pkt->level);
        return;
    }
    
    // Update BOTH log levels
    MqttLogger::instance().set_level((MqttLogLevel)pkt->level);
    current_log_level = (LogLevel)pkt->level;  ← SYNC
    
    // Save to NVS
    save_debug_level(pkt->level);
    
    LOG_INFO("DEBUG_CTRL", "Log level changed to %u", pkt->level);
}
```

**Fix #3: Simplified Message Router**
```cpp
// Replace lambda callbacks with direct function pointers
router.register_route(msg_probe, 
    EspnowStandardHandlers::handle_probe, 
    0xFF, nullptr);

router.register_route(msg_ack,
    EspnowStandardHandlers::handle_ack,
    0xFF, nullptr);

router.register_route(msg_data,
    [](const espnow_queue_msg_t* msg, void* ctx) {
        TransmitterConnection::instance().on_data_received(msg);
    },
    0xFF, nullptr);
```

---

## Part 6: Code Structure

### File Organization

```
esp32common/
├── espnow_common_utils/
│   ├── connection_manager.h         [NEW] Core state machine
│   ├── connection_manager.cpp       [NEW] Impl
│   ├── connection_event.h           [NEW] Event types
│   ├── connection_event.cpp         [NEW] Event queue
│   └── espnow_message_router.h      [MODIFY] Simplify

transmitter:
├── src/espnow/
│   ├── transmitter_connection.h     [NEW] TX-specific
│   ├── transmitter_connection.cpp   [NEW] TX impl
│   ├── discovery_task.h             [MODIFY] Use events
│   ├── discovery_task.cpp           [MODIFY] Use events
│   ├── message_handler.cpp          [MODIFY] Log level sync
│   └── transmission_task.cpp        [NO CHANGE]

receiver:
├── src/espnow/
│   ├── receiver_connection.h        [NEW] RX-specific
│   ├── receiver_connection.cpp      [NEW] RX impl
│   ├── receiver_manager_task.h      [NEW] Timeout monitor
│   ├── receiver_manager_task.cpp    [NEW] Timeout impl
│   └── espnow_tasks.cpp             [MODIFY] Init connection
```

---

## Part 7: Migration Path (Common-First Strategy)

### Strategy: Build Foundation First, Then Device-Specific Wrappers

This approach ensures maximum code reuse and prevents code duplication.

---

### Phase A: Create Common Foundation in esp32common

**Duration:** 2-3 hours  
**Goal:** All TX/RX can use this identical code

#### Step 1: Create Event Definitions

**File:** `esp32common/espnow_common_utils/connection_event.h`

```cpp
#pragma once
#include <cstdint>

enum class EspNowEvent : uint8_t {
    CONNECTION_START,       // Start connection (generic)
    PEER_FOUND,            // Peer discovered
    PEER_REGISTERED,       // Peer added to ESP-NOW
    DATA_RECEIVED,         // Data from peer
    CONNECTION_LOST,       // Timeout detected
    RESET_CONNECTION       // Reset to IDLE
};

enum class EspNowConnectionState : uint8_t {
    IDLE = 0,
    CONNECTING = 1,
    CONNECTED = 2
};

struct EspNowStateChange {
    EspNowEvent event;
    uint8_t peer_mac[6];
    uint32_t timestamp;
};
```

#### Step 2: Create Connection Manager

**File:** `esp32common/espnow_common_utils/connection_manager.h`

- 3-state state machine (IDLE, CONNECTING, CONNECTED)
- Event queue processing
- State transition logic
- **ZERO device-specific code**

**File:** `esp32common/espnow_common_utils/connection_manager.cpp`

- Implement state machine logic
- Queue processing
- Logging (will use existing MQTT logger)

#### Step 3: Create Event Processor Task

**File:** `esp32common/espnow_common_utils/connection_event_processor.h/cpp`

- Single task function that both TX and RX use
- Calls `manager.process_events()` periodically
- 100% code reuse

#### Step 4: Verify esp32common Compiles

```bash
# In esp32common directory
platformio run -e esp32
```

**At this point:**
- ✅ Connection manager complete
- ✅ Event types defined
- ✅ No device-specific code
- ✅ Ready for TX/RX to use

---

### Phase B: Implement Transmitter Device Layer

**Duration:** 2-3 hours  
**Goal:** TX uses common manager, only implements discovery

#### Step 1: Create TX Handler

**File:** `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_connection_handler.h/cpp`

- `start_discovery()` - Launch PROBE broadcast loop
- `on_ack_received()` - Register peer with ESP-NOW
- All methods post events to common manager

#### Step 2: Integrate in main.cpp

```cpp
void setup() {
    // ... existing setup ...
    
    // NEW: Initialize common manager
    EspNowConnectionManager::instance().init();
    
    // NEW: Initialize TX handler
    TransmitterConnectionHandler::instance();
    
    // NEW: Create event processor task
    create_connection_event_processor(3, 0);
    
    // MODIFIED: Start discovery (now posts events)
    TransmitterConnectionHandler::instance().start_discovery();
}

void on_espnow_recv(const uint8_t* mac, const uint8_t* data, int len) {
    if (is_ack_packet(data)) {
        // NEW: Post to common manager
        post_connection_event(EspNowEvent::PEER_FOUND, mac);
        
        TransmitterConnectionHandler::instance().on_ack_received(mac, channel);
        
        // NEW: Post to common manager
        post_connection_event(EspNowEvent::PEER_REGISTERED, mac);
    }
}
```

#### Step 3: Verify Transmitter Compiles and Runs

```bash
# In ESPnowtransmitter2 directory
platformio run -e esp32_poe2 -t upload
```

**At this point:**
- ✅ Transmitter uses common manager
- ✅ Discovery still works
- ✅ State machine updates (check serial logs)
- ✅ No hangs

---

### Phase C: Implement Receiver Device Layer

**Duration:** 2-3 hours  
**Goal:** RX uses common manager, only implements probe/timeout

#### Step 1: Create RX Handler

**File:** `espnowreciever_2/src/espnow/rx_connection_handler.h/cpp`

- `on_probe_received()` - Handle PROBE, send ACK, register peer
- `check_timeout()` - Detect connection loss
- All methods post events to common manager

#### Step 2: Integrate in main.cpp

```cpp
void setup() {
    // ... existing setup ...
    
    // NEW: Initialize common manager
    EspNowConnectionManager::instance().init();
    
    // NEW: Initialize RX handler
    ReceiverConnectionHandler::instance().init();
    
    // NEW: Create event processor task
    create_connection_event_processor(3, 0);
}

void on_espnow_recv(const uint8_t* mac, const uint8_t* data, int len) {
    if (is_probe_packet(data)) {
        // NEW: Post to common manager
        post_connection_event(EspNowEvent::PEER_FOUND, mac);
        
        ReceiverConnectionHandler::instance().on_probe_received(mac, channel);
        
        // NEW: Post to common manager
        post_connection_event(EspNowEvent::PEER_REGISTERED, mac);
    } else {
        // NEW: Update timeout
        ReceiverConnectionHandler::instance().on_data_received(mac);
        
        // NEW: Post to common manager
        post_connection_event(EspNowEvent::DATA_RECEIVED, mac);
    }
}
```

#### Step 3: Verify Receiver Compiles and Runs

```bash
# In espnowreciever_2 directory
platformio run -e esp32-s3 -t upload
```

**At this point:**
- ✅ Receiver uses common manager
- ✅ PROBE reception works
- ✅ ACK sending works
- ✅ State machine updates (check webui dashboard)
- ✅ No hangs

---

### Phase D: Integration Testing

**Duration:** 2-3 hours  
**Goal:** TX and RX work together

#### Step 1: Run Side-by-Side Tests

1. Flash both transmitter and receiver
2. Open serial monitors
3. Trigger discovery (TX): Should see state progression IDLE → CONNECTING → CONNECTED
4. Send data from TX: Verify RX receives
5. Unplug RX: TX should detect loss within 10 seconds, transition to IDLE

#### Step 2: Webui Dashboard Check

- Receiver webui should show connection state
- Should show "CONNECTED" with uptime
- Should update in real-time

#### Step 3: Validate State Transitions

**Expected Transmitter:**
```
[ESP-NOW] State: IDLE
[ESP-NOW] Discovery started
[ESP-NOW] State: CONNECTING (waiting for ACK)
[ESP-NOW] ACK received from [MAC]
[ESP-NOW] Peer registered
[ESP-NOW] State: CONNECTED (uptime: 0s)
[ESP-NOW] State: CONNECTED (uptime: 1s)
... continues until disconnect ...
```

**Expected Receiver:**
```
[ESP-NOW] State: IDLE (listening)
[ESP-NOW] PROBE received from [MAC]
[ESP-NOW] State: CONNECTING
[ESP-NOW] ACK sent to transmitter
[ESP-NOW] Peer registered
[ESP-NOW] State: CONNECTED (uptime: 0s)
[ESP-NOW] State: CONNECTED (uptime: 1s)
... continues until disconnect ...
```

---

## Part 8: Code Reuse Summary (After Migration)

### Location of Code (Post-Migration)

| Component | Location | Shared? | Purpose |
|-----------|----------|---------|---------|
| **EspNowConnectionManager** | `esp32common` | YES - Both use identical | State machine (3 states) |
| **connection_event_processor_task** | `esp32common` | YES - Both use identical | Event processing loop |
| **EspNowEvent enum** | `esp32common` | YES - Both use identical | Event types |
| **EspNowConnectionState enum** | `esp32common` | YES - Both use identical | State types |
| **TransmitterConnectionHandler** | `ESPnowtransmitter2` | NO | TX-specific discovery/ACK |
| **ReceiverConnectionHandler** | `espnowreciever_2` | NO | RX-specific probe/timeout |
| **Message router** | `esp32common` | YES - Existing | Message handling |
| **Logging system** | `esp32common` | YES - Existing | MQTT/Serial logs |

---

### Code Metrics (After Migration)

```
COMMON CODE (esp32common):
├── connection_manager.h/cpp          ~200 lines  (used by both)
├── connection_event_processor.h/cpp  ~50 lines   (used by both)
├── connection_event.h                ~30 lines   (used by both)
├── integration headers               ~100 lines  (for both)
└── Total: ~380 lines common, 100% reuse

DEVICE-SPECIFIC CODE (TX):
├── tx_connection_handler.h/cpp       ~150 lines  (TX only)
├── main.cpp modifications            ~30 lines   (TX only)
└── Total: ~180 lines TX-specific

DEVICE-SPECIFIC CODE (RX):
├── rx_connection_handler.h/cpp       ~150 lines  (RX only)
├── main.cpp modifications            ~30 lines   (RX only)
└── Total: ~180 lines RX-specific

GRAND TOTAL: ~740 lines for both projects
REUSE RATIO: 51% common (used twice), 49% device-specific (used once)
```

---

### Comparison with Old Design

| Metric | Old Design | New Design | Improvement |
|--------|-----------|-----------|------------|
| TX States | 17 | 3 | 82% simpler |
| RX States | 10 | 3 | 70% simpler |
| Code Duplication | High (each impl states) | Low (manager is common) | ~300 lines saved |
| Reuse Ratio | <10% | 51% | 5x improvement |
| Maintainability | Poor (sync design in async context) | Good (FreeRTOS native) | Much easier |
| Device-Specific Code | ~400 lines | ~180 lines per device | 55% reduction |

---

### Files to Create

#### In esp32common/

```
esp32common/espnow_common_utils/
├── connection_manager.h              [NEW]
├── connection_manager.cpp            [NEW]
├── connection_event.h                [NEW]
├── connection_event_processor.h      [NEW]
├── connection_event_processor.cpp    [NEW]
└── README_ESPNOW_MANAGER.md          [NEW - Usage docs]
```

#### In ESPnowtransmitter2/

```
ESPnowtransmitter2/espnowtransmitter2/src/espnow/
├── tx_connection_handler.h           [NEW]
├── tx_connection_handler.cpp         [NEW]
└── README_TX_HANDLER.md              [NEW - TX-specific docs]
```

#### In espnowreciever_2/

```
espnowreciever_2/src/espnow/
├── rx_connection_handler.h           [NEW]
├── rx_connection_handler.cpp         [NEW]
└── README_RX_HANDLER.md              [NEW - RX-specific docs]
```

---

### Key Implementation Principles

#### 1. Maximize Common Code
- If both TX and RX need it → goes in esp32common
- State machine, event types, task wrapper → ALL COMMON
- Only device-specific behavior → goes in device folders

#### 2. Single Responsibility
- Connection manager: State transitions only
- TX handler: How to discover, how to register
- RX handler: How to respond, how to timeout
- Task: Process events (identical for both)

#### 3. Event-Driven
- Callbacks post events
- Task processes events
- Events drive state transitions
- Zero polling

#### 4. FreeRTOS Native
- Use queues, not polling
- Use tasks, not loops
- Synchronization via FreeRTOS primitives
- Proper priority levels

#### 5. No Code Duplication
- Even though TX and RX are different, state machine is identical
- Don't duplicate manager code
- Don't duplicate event types
- Don't duplicate task code

---

### Total Migration Time

- Phase A (Common foundation): 2-3 hours
- Phase B (TX integration): 2-3 hours
- Phase C (RX integration): 2-3 hours
- Phase D (Integration testing): 2-3 hours
- **Total: 8-12 hours**

### Success Criteria

- [ ] Common foundation builds with zero errors
- [ ] Transmitter initializes without hanging
- [ ] Receiver detects transmitter within 10 seconds
- [ ] Both show CONNECTED state
- [ ] Data transmits reliably
- [ ] Disconnection detected within 10 seconds
- [ ] Reconnection works after 30 seconds
- [ ] No duplicate code between TX/RX
- [ ] 50%+ code in esp32common
- [ ] State transitions logged correctly
- [ ] No blocked threads

---

### Thread-Safe Event Queue Implementation

```cpp
// All events posted from ISR callbacks (thread-safe)
void on_espnow_recv_callback(const uint8_t* mac, const uint8_t* data, int len) {
    espnow_queue_msg_t msg;
    // ... process message ...
    
    // Post connection event (ISR-safe)
    if (is_probe_packet(data)) {
        post_connection_event(EspNowEvent::PROBE_RECEIVED, mac);
    } else if (is_ack_packet(data)) {
        post_connection_event(EspNowEvent::ACK_RECEIVED, mac);
    }
}

// Main task processes events (COMMON CODE)
void process_events_task(void* param) {
    while (true) {
        EspNowStateChange event;
        if (xQueueReceive(g_connection_event_queue, &event, pdMS_TO_TICKS(100))) {
            EspNowConnectionManager::instance().handle_event(event);
        }
    }
}
```

---

### FreeRTOS Integration in main.cpp

```cpp
// In main.cpp setup()
void setup() {
    // ... existing setup ...
    
    // NEW: Create connection event queue (before any connection attempts)
    g_connection_event_queue = xQueueCreate(10, sizeof(EspNowStateChange));
    
    // NEW: Initialize common manager
    EspNowConnectionManager::instance().init();
    
    // NEW: Create event processor task (COMMON)
    xTaskCreatePinnedToCore(
        connection_event_processor_task,
        "ConnEvents",
        3072,
        NULL,
        4,  // Higher priority - process events quickly
        NULL,
        0   // Core 0
    );
    
    // Initialize ESP-NOW
    esp_now_register_recv_cb(on_espnow_recv_callback);
    esp_now_register_send_cb(on_espnow_sent_callback);
    
    // Start connection (DEVICE-SPECIFIC)
    if (IS_TRANSMITTER) {
        TransmitterConnectionHandler::instance().start_discovery();
    } else {
        ReceiverConnectionHandler::instance().init();
    }
}
```

---

## Part 9: Testing Strategy

### 9.1 Unit Tests (No Hardware)

```cpp
void test_state_transitions() {
    EspNowConnectionManager& mgr = EspNowConnectionManager::instance();
    
    // Start in IDLE
    assert(mgr.is_idle());
    
    // Simulate DISCOVERY_START event
    mgr.post_event(EspNowEvent::DISCOVERY_START);
    assert(mgr.is_connecting());
    
    // Simulate ACK_RECEIVED event
    mgr.post_event(EspNowEvent::ACK_RECEIVED);
    assert(mgr.is_connected());
    
    // Simulate CONNECTION_LOST event
    mgr.post_event(EspNowEvent::CONNECTION_LOST);
    assert(mgr.is_idle());
}
```

### 9.2 Integration Tests (Hardware Required)

```
1. Test Discovery (Transmitter)
   - Start transmitter
   - Check log shows: "Starting discovery..."
   - Check log shows: "ACK received"
   - Check log shows: "Connected!"

2. Test Reception (Receiver)
   - Start receiver
   - Start transmitter
   - Check receiver log shows: "Probe received"
   - Check receiver log shows: "Connected!"

3. Test Data Flow
   - Both connected
   - Transmit data from transmitter
   - Verify receiver gets data
   - Check no errors

4. Test Recovery
   - Both connected
   - Disconnect receiver (power off)
   - Check transmitter log shows: "Connection lost"
   - Power on receiver
   - Check transmitter shows: "Connection restored"
```

---

## Part 10: Benefits of This Design

| Aspect | Old Design | New Design |
|--------|-----------|-----------|
| **States** | 10-17 | 3 |
| **Complexity** | Very High | Simple |
| **Understanding** | Hard | Easy |
| **Debugging** | Difficult | Clear event logs |
| **FreeRTOS Aware** | No | Yes |
| **Thread-Safe** | Mutex-based | Queue-based |
| **Discovery Trigger** | Missing | Explicit call |
| **Event Driven** | No | Yes |
| **Maintainability** | Poor | Excellent |
| **Test Coverage** | None | Easy to test |

---

## Part 11: Summary of Changes

### What Stays the Same
- ✓ Message routing system (just simplified)
- ✓ Discovery broadcast mechanism
- ✓ Channel locking logic
- ✓ Peer registration with ESP-NOW
- ✓ Logging system (mostly, just add level sync)

### What Changes
- 🔄 State machine from 10-17 to 3 states
- 🔄 Polling-based to event-driven
- 🔄 Async transitions to immediate transitions
- 🔄 No more "stuck in state" bugs
- 🔄 Add explicit discovery trigger
- 🔄 Add explicit connection monitoring

### What's Removed
- ❌ Complex state handlers
- ❌ Timeout monitoring in state machine
- ❌ Mutex-based state protection (use queue instead)
- ❌ State history tracking (unnecessary)
- ❌ All "waiting" states (handled by tasks)

---

## Conclusion

This redesign **completely eliminates the initialization hang** by:

1. **Using events instead of polling** - State changes happen immediately when events occur
2. **3 states instead of 17** - Much simpler to understand and debug
3. **FreeRTOS native** - Uses queues and tasks properly
4. **Explicit triggers** - `start_discovery()` explicitly called, no mystery hangs
5. **Clear flow** - Easy to trace what's happening from logs

The result is a **simple, reliable, maintainable** connection manager that works **within the FreeRTOS framework** instead of fighting against it.

