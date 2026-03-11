# State Machine Implementation - Specific Code Locations & Changes

**Document Purpose:** Technical reference showing exactly where changes need to be made

---

## Part A: Receiver Code Locations & Changes

### Location 1: `espnowreciever_2/src/espnow/espnow_callbacks.cpp`

**Current Code (Lines 15-47):**
```cpp
void on_data_recv(const uint8_t *mac, const uint8_t *data, int len) {
    // MINIMAL ISR WORK - just validate and queue the raw message
    if (!data || len < 1 || len > 250) return;
    
    // Prepare queue message with raw data
    espnow_queue_msg_t queue_msg;
    memcpy(queue_msg.data, data, len);
    memcpy(queue_msg.mac, mac, 6);
    queue_msg.len = len;
    queue_msg.timestamp = millis();
    
    // Queue message for processing (non-blocking, from ISR context)
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xQueueSendFromISR(ESPNow::queue, &queue_msg, &xHigherPriorityTaskWoken) != pdTRUE) {
        // Queue full - message dropped
        static uint32_t last_drop_log_ms = 0;
        uint32_t now = millis();
        if (now - last_drop_log_ms > 2000) {
            last_drop_log_ms = now;
            Serial.println("[ESP-NOW] RX queue full - message dropped");
        }
    }
    
    // Yield to higher priority task if woken
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

**Required Changes:**
- [ ] Replace with state machine notification instead of queue send
- [ ] Increment sequence number for each message
- [ ] Call `EspnowRXStateMachine::instance().on_message_queued(ctx)`
- [ ] Remove manual queue management
- [ ] Add timestamp for timeout detection

**New Code Pattern:**
```cpp
void on_data_recv(const uint8_t *mac, const uint8_t *data, int len) {
    if (!data || len < 1 || len > 250) return;
    
    // Create state context
    RXMessageContext ctx;
    ctx.state = RXMessageState::QUEUED;
    ctx.sequence_number = EspnowRXStateMachine::get_next_sequence();  // NEW
    ctx.timestamp_received_ms = millis();  // For timeout tracking
    memcpy(ctx.sender_mac, mac, 6);
    ctx.message_type = data[0];
    memcpy(ctx.data, data, len);
    ctx.data_length = len;
    
    // Notify state machine (threadsafe)
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    EspnowRXStateMachine::instance().on_message_queued_from_isr(ctx, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

---

### Location 2: `espnowreciever_2/src/espnow/espnow_tasks.cpp` (Lines 440-500)

**Current Code - handle_data_message():**
```cpp
void handle_data_message(const espnow_queue_msg_t* msg) {
    if (msg->len < (int)sizeof(battery_data_t)) return;
    
    const battery_data_t* payload = reinterpret_cast<const battery_data_t*>(msg->data);
    
    // Validate checksum
    if (!EspnowPacketUtils::validate_checksum(msg->data, msg->len)) {
        LOG_WARN("[ESP-NOW] Invalid checksum");
        return;  // DROP - no tracking
    }
    
    // Update global state
    ESPNow::received_soc = payload->soc;
    ESPNow::received_power = payload->power;
    ESPNow::data_received = true;  // <-- STALE FLAG PROBLEM
    
    // Dirty flags
    ESPNow::dirty_flags.soc_changed = true;
    ESPNow::dirty_flags.power_changed = true;
}
```

**Required Changes:**
- [ ] Remove `ESPNow::data_received = true` flag
- [ ] Remove `ESPNow::dirty_flags` manipulation
- [ ] Update state machine with validation result
- [ ] Call `on_message_valid()` for success or `on_message_error()` for failure
- [ ] Extract battery data and store in state machine (not global)
- [ ] Notify interested consumers via callback system

**New Code Pattern:**
```cpp
void handle_data_message(const RXMessageContext* ctx) {
    if (ctx->data_length < (int)sizeof(battery_data_t)) {
        EspnowRXStateMachine::instance().on_message_error({
            .sequence = ctx->sequence_number,
            .error_reason = "Message too short"
        });
        return;
    }
    
    const battery_data_t* payload = 
        reinterpret_cast<const battery_data_t*>(ctx->data);
    
    // Validate
    if (!EspnowPacketUtils::validate_checksum(ctx->data, ctx->data_length)) {
        EspnowRXStateMachine::instance().on_message_error({
            .sequence = ctx->sequence_number,
            .error_reason = "Invalid checksum"
        });
        return;
    }
    
    // Mark valid and provide data to consumers
    RXMessageContext validated = *ctx;
    validated.state = RXMessageState::VALID;
    EspnowRXStateMachine::instance().on_message_valid(validated);
    
    // Notify consumers (e.g., display, web server)
    BatteryDataHandler::instance().on_battery_data_received({
        .soc = payload->soc,
        .power = payload->power,
        .voltage_mv = estimate_voltage_mv(payload->soc),
        .timestamp_ms = ctx->timestamp_received_ms
    });
}
```

---

### Location 3: `espnowreciever_2/src/globals.cpp` (Lines 31-46)

**Current Code:**
```cpp
namespace ESPNow {
    volatile uint8_t received_soc = 50;
    volatile int32_t received_power = 0;
    volatile bool data_received = false;  // <-- REMOVE THIS
    
    LEDColor current_led_color = LED_ORANGE;
    
    DirtyFlags dirty_flags;  // <-- REMOVE THIS
    volatile int wifi_channel = 1;
    volatile bool transmitter_connected = false;
    uint8_t transmitter_mac[6] = {0};
    QueueHandle_t queue = NULL;
}
```

**Required Changes:**
- [ ] Remove `volatile bool data_received`
- [ ] Remove `DirtyFlags dirty_flags`
- [ ] Keep `received_soc` and `received_power` but access via state machine
- [ ] Update all references to these removed variables

**New Code Pattern:**
```cpp
namespace ESPNow {
    // Battery data (accessed via state machine, not directly)
    volatile uint8_t received_soc = 50;
    volatile int32_t received_power = 0;
    
    LEDColor current_led_color = LED_ORANGE;
    
    // Connection state (managed by state machine, not volatile flags)
    volatile int wifi_channel = 1;
    uint8_t transmitter_mac[6] = {0};
    
    // Old queue no longer used - replaced by state machine
    // QueueHandle_t queue = NULL;  <-- REMOVE
}
```

---

### Location 4: `espnowreciever_2/src/espnow/espnow_tasks.cpp` (Lines 300-450)

**Current Code - setup_message_routes():**
```cpp
void setup_message_routes() {
    auto& router = EspnowMessageRouter::instance();
    
    // ... route registration code ...
    
    router.register_route(msg_data,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_data_message(msg);  // <-- No state tracking
        },
        0xFF, nullptr);
    
    // ... more routes ...
}
```

**Required Changes:**
- [ ] Routes now call state machine methods instead of handlers directly
- [ ] Handlers receive RXMessageContext from state machine
- [ ] Add state machine tick() calls for timeout checking
- [ ] Initialize state machine at task startup

**New Code Pattern:**
```cpp
void task_espnow_worker(void *parameter) {
    // Initialize state machine
    EspnowRXStateMachine::instance().init();
    LOG_DEBUG("RX State Machine initialized");
    
    // ... rest of task ...
    
    for (;;) {
        // Check for stale messages and timeouts
        uint8_t recovered = EspnowRXStateMachine::instance().check_timeouts();
        if (recovered > 0) {
            LOG_WARN("Recovered %d stale messages/fragments", recovered);
        }
        
        // Get next message to process
        RXMessageContext* ctx = EspnowRXStateMachine::instance().begin_processing();
        if (ctx) {
            // Route based on message type
            switch (ctx->message_type) {
                case msg_data:
                    handle_data_message(ctx);
                    break;
                case msg_battery_status:
                    handle_battery_status(ctx);
                    break;
                // ... other message types ...
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));  // Reduced polling frequency
    }
}
```

---

## Part B: Transmitter Code Locations & Changes

### Location 1: `espnowtransmitter2/src/espnow/message_handler.h` (Lines 39-120)

**Current Code:**
```cpp
class EspnowMessageHandler {
public:
    static EspnowMessageHandler& instance();
    void start_rx_task(QueueHandle_t queue);
    bool is_receiver_connected() const;  // Query flag
    bool is_transmission_active() const;  // Query flag

private:
    volatile bool receiver_connected_{false};  // <-- REMOVE
    volatile bool transmission_active_{false};  // <-- REMOVE
    uint8_t receiver_mac_[6]{0};
};
```

**Required Changes:**
- [ ] Remove `volatile bool receiver_connected_`
- [ ] Remove `volatile bool transmission_active_`
- [ ] Replace queries with state machine methods
- [ ] Add reference to TX state machine

**New Code Pattern:**
```cpp
class EspnowMessageHandler {
public:
    static EspnowMessageHandler& instance();
    void start_rx_task(QueueHandle_t queue);
    
    // State machine queries (not flags)
    bool is_receiver_connected() const {
        return EspnowTXStateMachine::instance().is_transmission_ready();
    }
    bool is_transmission_active() const {
        return EspnowTXStateMachine::instance().get_connection_state() 
            == TXConnectionState::TRANSMISSION_ACTIVE;
    }

private:
    // TX State Machine handles all connection state
    // No need for local flags
    uint8_t receiver_mac_[6]{0};  // Still cache MAC for optimization
};
```

---

### Location 2: `espnowtransmitter2/src/espnow/message_handler.cpp` (Lines 271-330)

**Current Code - handle_request_data():**
```cpp
void EspnowMessageHandler::handle_request_data(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(request_data_t)) return;
    
    const request_data_t* req = reinterpret_cast<const request_data_t*>(msg.data);
    LOG_DEBUG("REQUEST_DATA (subtype=%d) from ...", req->subtype);
    
    switch (req->subtype) {
        case subtype_power_profile:
            transmission_active_ = true;  // <-- FLAG SET
            LOG_INFO(">>> Power profile transmission STARTED");
            break;
        // ...
    }
}
```

**Required Changes:**
- [ ] Remove flag assignment
- [ ] Call state machine transition instead
- [ ] Use `EspnowTXStateMachine::instance().set_connection_state()`

**New Code Pattern:**
```cpp
void EspnowMessageHandler::handle_request_data(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(request_data_t)) return;
    
    const request_data_t* req = reinterpret_cast<const request_data_t*>(msg.data);
    LOG_DEBUG("REQUEST_DATA (subtype=%d)", req->subtype);
    
    switch (req->subtype) {
        case subtype_power_profile:
            // State machine transition (not flag)
            EspnowTXStateMachine::instance().set_connection_state(
                TXConnectionState::TRANSMISSION_ACTIVE,
                "Receiver requested power profile transmission"
            );
            LOG_INFO(">>> Power profile transmission STARTED");
            break;
    }
}
```

---

### Location 3: `espnowtransmitter2/src/espnow/data_sender.cpp` (Lines 18-45)

**Current Code - task_impl():**
```cpp
void DataSender::task_impl(void* parameter) {
    LOG_DEBUG("Data sender task running");
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t interval_ticks = pdMS_TO_TICKS(2000);  // POLLING
    
    while (true) {
        vTaskDelayUntil(&last_wake_time, interval_ticks);
        
        // BLIND POLLING
        if (EspnowMessageHandler::instance().is_transmission_active()) {
            LOG_TRACE("Sending test data");
            send_test_data_with_led_control();
        } else {
            LOG_TRACE("Skipping send (transmission inactive)");
        }
    }
}
```

**Required Changes:**
- [ ] Replace fixed polling with event notification
- [ ] Use state machine to check readiness
- [ ] Implement exponential backoff on failure
- [ ] Add proper error handling

**New Code Pattern:**
```cpp
void DataSender::task_impl(void* parameter) {
    LOG_DEBUG("Data sender task running");
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t interval_ticks = pdMS_TO_TICKS(2000);
    
    auto& state_machine = EspnowTXStateMachine::instance();
    uint8_t consecutive_failures = 0;
    
    while (true) {
        vTaskDelayUntil(&last_wake_time, interval_ticks);
        
        // Check if ready via state machine (not flag)
        if (!state_machine.is_transmission_ready()) {
            LOG_TRACE("Transmission not ready (state=%d)", 
                     (int)state_machine.get_connection_state());
            continue;
        }
        
        // Send test data
        TXMessageContext ctx;
        ctx.sequence_number = get_next_sequence();
        ctx.timestamp_created_ms = millis();
        ctx.message_type = msg_data;
        // ... populate ctx.data ...
        
        bool success = send_test_data_with_led_control(&ctx);
        
        if (success) {
            state_machine.queue_message(ctx);
            consecutive_failures = 0;
            LOG_TRACE("Data sent (seq=%u)", ctx.sequence_number);
        } else {
            consecutive_failures++;
            if (consecutive_failures > 5) {
                LOG_WARN("Repeated send failures - may indicate connection issue");
                state_machine.check_connection_timeout();
            }
        }
    }
}
```

---

### Location 4: `espnowtransmitter2/src/espnow/message_handler.cpp` (Lines 100-180)

**Current Code - setup_message_routes():**
```cpp
void EspnowMessageHandler::setup_message_routes() {
    auto& router = EspnowMessageRouter::instance();
    
    // Connection callbacks
    probe_config.on_connection = [](const uint8_t* mac, bool connected) {
        // ... handle connection ...
        // OLD: receiver_connected_ = connected;  <-- FLAG SET
    };
}
```

**Required Changes:**
- [ ] Remove flag assignments
- [ ] Call state machine state transitions
- [ ] Add logging for state transitions

**New Code Pattern:**
```cpp
void EspnowMessageHandler::setup_message_routes() {
    auto& router = EspnowMessageRouter::instance();
    auto& sm = EspnowTXStateMachine::instance();
    
    probe_config.on_connection = [](const uint8_t* mac, bool connected) {
        if (connected) {
            sm.set_connection_state(
                TXConnectionState::CONNECTED,
                "PROBE ACK received"
            );
            sm.on_probe_ack(mac);
        } else {
            sm.set_connection_state(
                TXConnectionState::DISCONNECTED,
                "Connection lost"
            );
        }
    };
}
```

---

## Part C: New Files to Create

### File 1: `espnowreciever_2/src/espnow/espnow_rx_state.h`

**Contents:** RX enums and data structures
**Size:** ~250 lines
**Key Types:**
- `enum RXMessageState`
- `enum RXFragmentState`
- `struct RXMessageContext`
- `struct RXFragmentContext`

### File 2: `espnowreciever_2/src/espnow/espnow_rx_state_machine.h`

**Contents:** RX state machine interface
**Size:** ~200 lines
**Key Methods:**
- `on_message_queued()`
- `begin_processing()`
- `on_message_valid()`
- `on_message_consumed()`
- `check_timeouts()`
- `begin_fragment_reassembly()`
- `add_fragment()`

### File 3: `espnowreciever_2/src/espnow/espnow_rx_state_machine.cpp`

**Contents:** RX state machine implementation
**Size:** ~400 lines
**Implementation:**
- State transition logic
- Queue management with mutex
- Timeout detection
- Fragment reassembly
- Duplicate detection via sequence numbers

### File 4: `espnowtransmitter2/src/espnow/espnow_tx_state.h`

**Contents:** TX enums and data structures
**Size:** ~200 lines
**Key Types:**
- `enum TXMessageState`
- `enum TXConnectionState`
- `struct TXMessageContext`
- `struct TXConnectionContext`

### File 5: `espnowtransmitter2/src/espnow/espnow_tx_state_machine.h`

**Contents:** TX state machine interface
**Size:** ~200 lines
**Key Methods:**
- `set_connection_state()`
- `get_connection_state()`
- `is_transmission_ready()`
- `on_probe_ack()`
- `check_connection_timeout()`
- `queue_message()`
- `get_next_to_send()`
- `on_send_complete()`
- `on_ack_received()`
- `check_message_timeouts()`

### File 6: `espnowtransmitter2/src/espnow/espnow_tx_state_machine.cpp`

**Contents:** TX state machine implementation
**Size:** ~400 lines
**Implementation:**
- Connection state transitions with logging
- Message queue management with mutex
- Retry logic with exponential backoff
- Timeout detection
- Heartbeat monitoring

---

## Part D: Compilation & Include Changes

### Receiver - Add to includes:

**In `espnowreciever_2/src/espnow/espnow_tasks.cpp`:**
```cpp
#include "espnow_rx_state.h"
#include "espnow_rx_state_machine.h"
```

**In `espnowreciever_2/src/espnow/espnow_callbacks.cpp`:**
```cpp
#include "espnow_rx_state.h"
#include "espnow_rx_state_machine.h"
```

### Transmitter - Add to includes:

**In `espnowtransmitter2/src/espnow/message_handler.h`:**
```cpp
#include "espnow_tx_state.h"
#include "espnow_tx_state_machine.h"
```

**In `espnowtransmitter2/src/espnow/data_sender.cpp`:**
```cpp
#include "espnow_tx_state.h"
#include "espnow_tx_state_machine.h"
```

---

## Part E: Compilation Verification Steps

After implementing changes:

1. **Receiver compilation:**
   ```bash
   cd espnowreciever_2
   pio run -e receiver_tft
   # Should compile with NO errors
   # Should have warnings about removed globals (expected)
   ```

2. **Transmitter compilation:**
   ```bash
   cd espnowtransmitter2
   pio run -e lilygo-t-display-s3
   # Should compile with NO errors
   ```

3. **Check for remaining flag usage:**
   ```bash
   grep -r "data_received" espnowreciever_2/src/
   grep -r "transmission_active_" espnowtransmitter2/src/espnow/
   # Should return ZERO results (all replaced by state machine)
   ```

---

## Summary of All Changes

| File | Change Type | Lines Affected | Action |
|------|------------|-----------------|--------|
| espnow_callbacks.cpp | **Refactor** | 15-47 | Replace queue send with state machine |
| espnow_tasks.cpp | **Refactor** | 440-500 | Remove flag tracking, use state machine |
| globals.cpp | **Delete** | 31-46 | Remove volatile flags |
| message_handler.h | **Refactor** | 39-120 | Remove volatile flags, add state machine ref |
| message_handler.cpp | **Refactor** | 271-330 | Replace flag sets with state transitions |
| data_sender.cpp | **Refactor** | 18-45 | Replace polling with event-driven |
| **NEW** | espnow_rx_state.h | N/A | Create RX state structures |
| **NEW** | espnow_rx_state_machine.h | N/A | Create RX state machine interface |
| **NEW** | espnow_rx_state_machine.cpp | N/A | Create RX state machine impl |
| **NEW** | espnow_tx_state.h | N/A | Create TX state structures |
| **NEW** | espnow_tx_state_machine.h | N/A | Create TX state machine interface |
| **NEW** | espnow_tx_state_machine.cpp | N/A | Create TX state machine impl |

**Total Changes:** 6 files refactored + 6 files created = **12 files total**  
**Total New Code:** ~1,200 lines  
**Total Removed Code:** ~150 lines (redundant flags)  
**Net Change:** +1,050 lines (investment in quality)
