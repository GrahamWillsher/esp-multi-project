# ESP-NOW Redesign - Implementation Roadmap

**Document Version:** 1.0  
**Status:** Ready for Implementation  
**Date:** 2026-02-13  
**Estimated Time:** 8-12 hours  
**Priority:** HIGH (Blocks full system integration)

---

## Quick Start

1. **Read** `ESPNOW_REDESIGN_COMPLETE_ARCHITECTURE.md` (architectural overview)
2. **Read** `ESPNOW_STATE_MACHINE_ARCHITECTURE_REVIEW.md` (problem analysis)
3. **Follow** this roadmap (step-by-step implementation)

---

## Phase 1: Foundation (2-3 hours)

### Goal
Create core event-driven infrastructure without modifying existing code.

### Step 1.1: Create Event Types
**File:** `esp32common/espnow_common_utils/connection_event.h`

```cpp
#pragma once

#include <cstdint>
#include <cstring>

enum class EspNowEvent : uint8_t {
    // Discovery events
    DISCOVERY_START = 0,
    DISCOVERY_PROBE_SENT = 1,
    ACK_RECEIVED = 2,
    
    // Connection events
    PEER_REGISTERED = 3,
    CONNECTION_ESTABLISHED = 4,
    
    // Reception events
    PROBE_RECEIVED = 5,
    DATA_RECEIVED = 6,
    
    // Error/Recovery events
    CONNECTION_LOST = 7,
    ERROR_OCCURRED = 8,
    RESET_CONNECTION = 9
};

struct EspNowStateChange {
    EspNowEvent event;
    uint8_t peer_mac[6];
    uint32_t timestamp;
    uint8_t data[64];  // For event data
    uint8_t data_len;
};
```

**Test:** Compile check

---

### Step 1.2: Create Connection Manager Header
**File:** `esp32common/espnow_common_utils/connection_manager.h`

```cpp
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "connection_event.h"

enum class EspNowConnectionState : uint8_t {
    IDLE = 0,
    CONNECTING = 1,
    CONNECTED = 2
};

class EspNowConnectionManager {
public:
    static EspNowConnectionManager& instance();
    
    bool init();
    
    void post_event(EspNowEvent event, const uint8_t* mac = nullptr);
    void process_events();  // Call from task
    
    EspNowConnectionState get_state() const { return current_state_; }
    bool is_idle() const { return current_state_ == EspNowConnectionState::IDLE; }
    bool is_connecting() const { return current_state_ == EspNowConnectionState::CONNECTING; }
    bool is_connected() const { return current_state_ == EspNowConnectionState::CONNECTED; }
    bool is_ready_to_send() const { return is_connected(); }
    
    const char* get_state_string() const;
    uint32_t get_connected_time_ms() const;
    const uint8_t* get_peer_mac() const { return peer_mac_; }
    
private:
    EspNowConnectionManager();
    
    EspNowConnectionState current_state_;
    uint8_t peer_mac_[6];
    uint32_t state_enter_time_;
    QueueHandle_t event_queue_;
    
    void handle_event(const EspNowStateChange& event);
    void transition_to_state(EspNowConnectionState new_state);
};

extern QueueHandle_t g_connection_event_queue;
inline bool post_connection_event(EspNowEvent event, const uint8_t* mac = nullptr) {
    return EspNowConnectionManager::instance().post_event(event, mac);
}
```

**Test:** Compile check

---

### Step 1.3: Create Connection Manager Implementation
**File:** `esp32common/espnow_common_utils/connection_manager.cpp`

```cpp
#include "connection_manager.h"
#include <Arduino.h>

static const char* LOG_TAG = "CONN_MGR";
QueueHandle_t g_connection_event_queue = nullptr;

EspNowConnectionManager& EspNowConnectionManager::instance() {
    static EspNowConnectionManager instance;
    return instance;
}

EspNowConnectionManager::EspNowConnectionManager()
    : current_state_(EspNowConnectionState::IDLE),
      state_enter_time_(millis()),
      event_queue_(nullptr) {
    memset(peer_mac_, 0, 6);
}

bool EspNowConnectionManager::init() {
    LOG_INFO(LOG_TAG, "Initializing connection manager");
    
    if (event_queue_ == nullptr) {
        event_queue_ = xQueueCreate(10, sizeof(EspNowStateChange));
        g_connection_event_queue = event_queue_;
        if (event_queue_ == nullptr) {
            LOG_ERROR(LOG_TAG, "Failed to create event queue!");
            return false;
        }
    }
    
    current_state_ = EspNowConnectionState::IDLE;
    state_enter_time_ = millis();
    
    LOG_INFO(LOG_TAG, "Connection manager initialized - state: IDLE");
    return true;
}

void EspNowConnectionManager::post_event(EspNowEvent event, const uint8_t* mac) {
    if (event_queue_ == nullptr) {
        LOG_WARN(LOG_TAG, "Event queue not initialized!");
        return;
    }
    
    EspNowStateChange change;
    change.event = event;
    change.timestamp = millis();
    
    if (mac != nullptr) {
        memcpy(change.peer_mac, mac, 6);
    } else {
        memset(change.peer_mac, 0, 6);
    }
    
    xQueueSendFromISR(event_queue_, &change, nullptr);
}

void EspNowConnectionManager::process_events() {
    EspNowStateChange event;
    
    while (xQueueReceive(event_queue_, &event, 0) == pdTRUE) {
        handle_event(event);
    }
}

void EspNowConnectionManager::handle_event(const EspNowStateChange& event) {
    LOG_DEBUG(LOG_TAG, "Event received: %u, State: %s", (uint8_t)event.event, get_state_string());
    
    switch (event.event) {
        case EspNowEvent::DISCOVERY_START:
            if (current_state_ == EspNowConnectionState::IDLE) {
                transition_to_state(EspNowConnectionState::CONNECTING);
            }
            break;
            
        case EspNowEvent::ACK_RECEIVED:
            if (current_state_ == EspNowConnectionState::CONNECTING) {
                memcpy(peer_mac_, event.peer_mac, 6);
                LOG_INFO(LOG_TAG, "ACK received from peer");
            }
            break;
            
        case EspNowEvent::PEER_REGISTERED:
            if (current_state_ == EspNowConnectionState::CONNECTING) {
                transition_to_state(EspNowConnectionState::CONNECTED);
            }
            break;
            
        case EspNowEvent::PROBE_RECEIVED:
            if (current_state_ == EspNowConnectionState::IDLE) {
                transition_to_state(EspNowConnectionState::CONNECTING);
                memcpy(peer_mac_, event.peer_mac, 6);
            }
            break;
            
        case EspNowEvent::DATA_RECEIVED:
            if (current_state_ == EspNowConnectionState::CONNECTING) {
                transition_to_state(EspNowConnectionState::CONNECTED);
            }
            break;
            
        case EspNowEvent::CONNECTION_LOST:
            LOG_WARN(LOG_TAG, "Connection lost");
            transition_to_state(EspNowConnectionState::IDLE);
            break;
            
        case EspNowEvent::RESET_CONNECTION:
            transition_to_state(EspNowConnectionState::IDLE);
            break;
            
        default:
            LOG_DEBUG(LOG_TAG, "Unhandled event: %u", (uint8_t)event.event);
    }
}

void EspNowConnectionManager::transition_to_state(EspNowConnectionState new_state) {
    if (new_state == current_state_) return;
    
    current_state_ = new_state;
    state_enter_time_ = millis();
    
    LOG_INFO(LOG_TAG, "State changed to: %s", get_state_string());
}

const char* EspNowConnectionManager::get_state_string() const {
    switch (current_state_) {
        case EspNowConnectionState::IDLE: return "IDLE";
        case EspNowConnectionState::CONNECTING: return "CONNECTING";
        case EspNowConnectionState::CONNECTED: return "CONNECTED";
        default: return "UNKNOWN";
    }
}

uint32_t EspNowConnectionManager::get_connected_time_ms() const {
    if (!is_connected()) return 0;
    return millis() - state_enter_time_;
}
```

**Test:** Compile check, verify core logic

---

## Phase 2: Transmitter Integration (2-3 hours)

### Step 2.1: Create Transmitter Connection Class
**File:** `ESPnowtransmitter2/espnowtransmitter2/src/espnow/transmitter_connection.h`

```cpp
#pragma once

#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class TransmitterConnection {
public:
    static TransmitterConnection& instance();
    
    bool start_discovery();
    void stop_discovery();
    
    void on_ack_received(const uint8_t* receiver_mac, uint8_t channel);
    void on_peer_registered();
    void on_send_failed();
    
    const uint8_t* get_receiver_mac() const { return receiver_mac_; }
    uint8_t get_receiver_channel() const { return receiver_channel_; }
    
private:
    uint8_t receiver_mac_[6];
    uint8_t receiver_channel_;
    TaskHandle_t discovery_task_handle_;
    uint32_t discovery_start_time_;
    
    TransmitterConnection();
    
    void discovery_loop();
    static void discovery_task_wrapper(void* param);
};
```

**Test:** Compile check

---

### Step 2.2: Implement Transmitter Connection
**File:** `ESPnowtransmitter2/espnowtransmitter2/src/espnow/transmitter_connection.cpp`

```cpp
#include "transmitter_connection.h"
#include "connection_manager.h"  // From esp32common
#include "../config/logging_config.h"
#include <Arduino.h>
#include <esp_wifi.h>

static const char* LOG_TAG = "TX_CONN";
static const uint32_t DISCOVERY_TIMEOUT_MS = 30000;
static const uint32_t PROBE_INTERVAL_MS = 200;

TransmitterConnection& TransmitterConnection::instance() {
    static TransmitterConnection instance;
    return instance;
}

TransmitterConnection::TransmitterConnection()
    : discovery_task_handle_(nullptr),
      receiver_channel_(0),
      discovery_start_time_(0) {
    memset(receiver_mac_, 0, 6);
}

bool TransmitterConnection::start_discovery() {
    if (discovery_task_handle_ != nullptr) {
        LOG_WARN(LOG_TAG, "Discovery already running");
        return false;
    }
    
    LOG_INFO(LOG_TAG, "Starting discovery");
    post_connection_event(EspNowEvent::DISCOVERY_START);
    
    discovery_start_time_ = millis();
    
    xTaskCreatePinnedToCore(
        discovery_task_wrapper,
        "TxDiscovery",
        3072,
        this,
        2,  // Priority
        &discovery_task_handle_,
        0   // Core
    );
    
    return discovery_task_handle_ != nullptr;
}

void TransmitterConnection::discovery_task_wrapper(void* param) {
    TransmitterConnection* self = static_cast<TransmitterConnection*>(param);
    self->discovery_loop();
}

void TransmitterConnection::discovery_loop() {
    uint32_t last_probe = millis();
    uint8_t probe_count = 0;
    
    while (true) {
        uint32_t now = millis();
        
        // Check timeout
        if (now - discovery_start_time_ > DISCOVERY_TIMEOUT_MS) {
            LOG_WARN(LOG_TAG, "Discovery timeout after %u probes", probe_count);
            stop_discovery();
            return;
        }
        
        // Send PROBE periodically
        if (now - last_probe > PROBE_INTERVAL_MS) {
            // TODO: Broadcast PROBE packet
            post_connection_event(EspNowEvent::DISCOVERY_PROBE_SENT);
            last_probe = now;
            probe_count++;
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void TransmitterConnection::stop_discovery() {
    if (discovery_task_handle_ != nullptr) {
        vTaskDelete(discovery_task_handle_);
        discovery_task_handle_ = nullptr;
        LOG_INFO(LOG_TAG, "Discovery stopped");
    }
}

void TransmitterConnection::on_ack_received(const uint8_t* receiver_mac, uint8_t channel) {
    memcpy(receiver_mac_, receiver_mac, 6);
    receiver_channel_ = channel;
    
    LOG_INFO(LOG_TAG, "ACK received - registering peer on channel %u", channel);
    
    // Register peer
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(esp_now_peer_info_t));
    peer.channel = channel;
    peer.ifidx = ESP_IF_WIFI_STA;
    memcpy(peer.peer_addr, receiver_mac, 6);
    
    if (esp_now_add_peer(&peer) == ESP_OK) {
        post_connection_event(EspNowEvent::PEER_REGISTERED);
        stop_discovery();
    } else {
        LOG_ERROR(LOG_TAG, "Failed to register peer");
    }
}

void TransmitterConnection::on_send_failed() {
    LOG_WARN(LOG_TAG, "Send failed - attempting recovery");
    // Could trigger reconnection logic here
}
```

**Test:** Compile check, verify discovery task creation

---

### Step 2.3: Add Event Processor Task
**File:** In `ESPnowtransmitter2/espnowtransmitter2/src/main.cpp`

```cpp
// Add this function before setup()
void connection_event_processor_task(void* param) {
    while (true) {
        EspNowConnectionManager::instance().process_events();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// In setup(), after creating other tasks:
xTaskCreatePinnedToCore(
    connection_event_processor_task,
    "ConnEvents",
    2048,
    NULL,
    3,  // Priority
    NULL,
    0   // Core
);

// Replace existing init with:
EspnowMessageHandler::instance().start_rx_task(espnow_message_queue);
delay(100);

EspNowConnectionManager::instance().init();
delay(1000);  // Let system stabilize

TransmitterConnection::instance().start_discovery();
```

**Test:** Compile and verify logs show discovery starting

---

## Phase 3: Receiver Integration (2-3 hours)

### Step 3.1: Create Receiver Connection Class
**File:** `espnowreciever_2/src/espnow/receiver_connection.h`

```cpp
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class ReceiverConnection {
public:
    static ReceiverConnection& instance();
    
    bool init();
    
    void on_probe_received(const uint8_t* transmitter_mac, uint8_t channel);
    void on_data_received(const uint8_t* transmitter_mac);
    void check_connection_timeout();
    
    const uint8_t* get_transmitter_mac() const { return transmitter_mac_; }
    
private:
    uint8_t transmitter_mac_[6];
    uint8_t transmitter_channel_;
    uint32_t last_data_time_;
    TaskHandle_t timeout_task_handle_;
    
    ReceiverConnection();
    
    void timeout_monitor_loop();
    static void timeout_monitor_wrapper(void* param);
};
```

**Test:** Compile check

---

### Step 3.2: Implement Receiver Connection
**File:** `espnowreciever_2/src/espnow/receiver_connection.cpp`

```cpp
#include "receiver_connection.h"
#include "connection_manager.h"
#include "../config/logging_config.h"
#include <Arduino.h>
#include <esp_now.h>

static const char* LOG_TAG = "RX_CONN";
static const uint32_t CONNECTION_TIMEOUT_MS = 10000;

ReceiverConnection& ReceiverConnection::instance() {
    static ReceiverConnection instance;
    return instance;
}

ReceiverConnection::ReceiverConnection()
    : transmitter_channel_(0),
      last_data_time_(0),
      timeout_task_handle_(nullptr) {
    memset(transmitter_mac_, 0, 6);
}

bool ReceiverConnection::init() {
    LOG_INFO(LOG_TAG, "Initializing receiver connection");
    
    // Start timeout monitor task
    xTaskCreatePinnedToCore(
        timeout_monitor_wrapper,
        "RxTimeout",
        2048,
        this,
        2,
        &timeout_task_handle_,
        0
    );
    
    return timeout_task_handle_ != nullptr;
}

void ReceiverConnection::on_probe_received(const uint8_t* transmitter_mac, uint8_t channel) {
    memcpy(transmitter_mac_, transmitter_mac, 6);
    transmitter_channel_ = channel;
    last_data_time_ = millis();
    
    LOG_INFO(LOG_TAG, "Probe received - registering peer on channel %u", channel);
    
    // Register peer
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(esp_now_peer_info_t));
    peer.channel = channel;
    peer.ifidx = ESP_IF_WIFI_STA;
    memcpy(peer.peer_addr, transmitter_mac, 6);
    
    if (esp_now_add_peer(&peer) == ESP_OK) {
        post_connection_event(EspNowEvent::PEER_REGISTERED);
    }
    
    post_connection_event(EspNowEvent::PROBE_RECEIVED, transmitter_mac);
}

void ReceiverConnection::on_data_received(const uint8_t* transmitter_mac) {
    last_data_time_ = millis();
    post_connection_event(EspNowEvent::DATA_RECEIVED, transmitter_mac);
}

void ReceiverConnection::timeout_monitor_wrapper(void* param) {
    ReceiverConnection* self = static_cast<ReceiverConnection*>(param);
    self->timeout_monitor_loop();
}

void ReceiverConnection::timeout_monitor_loop() {
    while (true) {
        if (last_data_time_ > 0) {
            uint32_t time_since_last = millis() - last_data_time_;
            
            if (time_since_last > CONNECTION_TIMEOUT_MS) {
                LOG_WARN(LOG_TAG, "Connection timeout - no data for %u ms", time_since_last);
                post_connection_event(EspNowEvent::CONNECTION_LOST);
                last_data_time_ = 0;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

**Test:** Compile check, verify timeout task creation

---

### Step 3.3: Update Receiver main.cpp
**File:** `espnowreciever_2/src/main.cpp`

```cpp
// Add at top
#include "espnow/receiver_connection.h"
#include "espnow/connection_manager.h"

// Add function before setup()
void connection_event_processor_task(void* param) {
    while (true) {
        EspNowConnectionManager::instance().process_events();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// In setup(), after all task creation:
xTaskCreatePinnedToCore(
    connection_event_processor_task,
    "ConnEvents",
    2048,
    NULL,
    3,
    NULL,
    0
);

// Initialize connection managers
EspNowConnectionManager::instance().init();
ReceiverConnection::instance().init();
```

**Test:** Compile and verify logs

---

## Phase 4: Testing & Cleanup (2-3 hours)

### Step 4.1: Test Discovery Flow
- Compile both projects
- Power both devices
- Monitor logs
- Expected: "ACK received", "Connected" logs appear

### Step 4.2: Test Data Flow
- Verify data transmits successfully
- Check no errors in logs
- Verify connection stays active

### Step 4.3: Test Recovery
- Disconnect receiver (power off)
- Check transmitter shows "Connection lost"
- Power on receiver
- Check automatic reconnection

### Step 4.4: Cleanup
- Remove old connection manager files (optional - can be gradual)
- Update any references to old classes
- Final compile and test

---

## Logging Fixes

### Fix #1: Sync Debug Levels (IMMEDIATE)
**File:** `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_handler.cpp`

Find `handle_debug_control()` and add:
```cpp
current_log_level = (LogLevel)pkt->level;  // ADD THIS LINE
```

### Fix #2: Rate Limit Logs (OPTIONAL, PERFORMANCE)
**File:** `esp32common/logging_utilities/logging_config.h`

Update macros to rate-limit high-frequency logs if needed.

---

## Summary

| Phase | Duration | Tasks | Status |
|-------|----------|-------|--------|
| 1: Foundation | 2-3h | Event types, Manager, Queue | ✓ Ready |
| 2: Transmitter | 2-3h | TX Connection, Discovery Task | ✓ Ready |
| 3: Receiver | 2-3h | RX Connection, Timeout Monitor | ✓ Ready |
| 4: Testing | 2-3h | Integration tests, Cleanup | ✓ Ready |
| Logging Fixes | 0.5h | Debug sync, Rate limiting | ✓ Easy |
| **TOTAL** | **8-12h** | Complete redesign | ✓ Doable |

---

## Success Criteria

When complete, you should see:

```
[INFO][TX_CONN] Starting discovery
[DEBUG][CONN_MGR] State changed to: CONNECTING
[INFO][RX_CONN] Probe received - registering peer
[DEBUG][CONN_MGR] State changed to: CONNECTING
[INFO][TX_CONN] ACK received - registering peer
[DEBUG][CONN_MGR] State changed to: CONNECTED
[INFO][RX_CONN] Data received
[DEBUG][CONN_MGR] State changed to: CONNECTED
```

No more hanging, no more "Initialization complete" without progression!

---

## Questions?

Refer back to `ESPNOW_REDESIGN_COMPLETE_ARCHITECTURE.md` for detailed architecture.

