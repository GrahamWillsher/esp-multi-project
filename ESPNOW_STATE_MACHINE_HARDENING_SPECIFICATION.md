# ESP-NOW State Machine Hardening - Comprehensive Analysis & Resolution

**Date:** March 9, 2026  
**Status:** CRITICAL ISSUES IDENTIFIED  
**Priority:** HIGH (Must resolve before production)

---

## Executive Summary

After thorough analysis of the ESP-NOW implementation across both receiver and transmitter codebases, **significant state management issues have been identified** that could lead to:

- ✗ **Message loss and duplication** (stale flags not properly consumed)
- ✗ **Race conditions** between ISR context and main task context
- ✗ **Infinite hangs** on incomplete fragment reassembly
- ✗ **Redundant/duplicate sends** due to unclear retry semantics
- ✗ **Unpredictable behavior** under network stress conditions

The root cause: **Flag-based state tracking instead of proper state machines**.

---

## Part 1: Detailed Findings

### Issue #1: Receiver - ISR-to-Task Communication Uses Fire-and-Forget Messaging

**File:** `espnowreciever_2/src/espnow/espnow_callbacks.cpp` (Lines 15-47)

**Current Implementation:**
```cpp
void on_data_recv(const uint8_t *mac, const uint8_t *data, int len) {
    // ISR context - MINIMAL WORK
    espnow_queue_msg_t queue_msg;
    memcpy(queue_msg.data, data, len);
    memcpy(queue_msg.mac, mac, 6);
    queue_msg.len = len;
    queue_msg.timestamp = millis();
    
    // Send to queue (fire-and-forget)
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(ESPNow::queue, &queue_msg, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

**Problem:**
- Message validity is NOT tracked beyond "queued"
- No way to know if consumer actually processed the message
- No acknowledgment mechanism for successful consumption
- Queue overflow silently drops messages (only logs every 2 seconds)
- No timeout if message sits in queue indefinitely

**Risk Level:** 🔴 **HIGH**

---

### Issue #2: Receiver - Message Handler Tasks Use Ad-Hoc Processing

**File:** `espnowreciever_2/src/espnow/espnow_tasks.cpp` (Lines 440-500)

**Current Implementation:**
```cpp
void handle_data_message(const espnow_queue_msg_t* msg) {
    if (msg->len < (int)sizeof(battery_data_t)) return;
    
    const battery_data_t* payload = reinterpret_cast<const battery_data_t*>(msg->data);
    
    // Validate checksum
    if (!EspnowPacketUtils::validate_checksum(msg->data, msg->len)) {
        LOG_WARN("[ESP-NOW] Invalid checksum");
        return;  // DROP - no retry, no tracking
    }
    
    // Update global state with battery data
    ESPNow::received_soc = payload->soc;
    ESPNow::received_power = payload->power;
    ESPNow::data_received = true;  // <-- FLAG BASED
    
    // Dirty flags for display
    ESPNow::dirty_flags.soc_changed = true;
    ESPNow::dirty_flags.power_changed = true;
    
    LOG_DEBUG("[ESP-NOW] Valid: SOC=%d%%, Power=%dW", payload->soc, payload->power);
}
```

**Problems:**
1. **`data_received` flag is stale** - Consumer never clears it
2. **No consumption state** - Can't distinguish between "new data" and "already processed"
3. **No per-message tracking** - Duplicate messages indistinguishable from new ones
4. **Dirty flags don't reset** - Display thread has to manage cleanup
5. **No error states** - Invalid checksums silently dropped with no tracking

**Risk Level:** 🔴 **CRITICAL**

---

### Issue #3: Receiver - Fragment Reassembly Has No State Machine

**File:** `espnowreciever_2/src/espnow/espnow_tasks.cpp` (Lines 250-350 - implied from code structure)

**Current State:**
- Fragment counter variables exist but are NOT formally managed
- No timeout detection for incomplete reassembly
- No way to abandon hung fragments
- No explicit IDLE → RECEIVING → COMPLETE states

**Example Scenario - THE BUG:**
```
Time 0ms:   Fragment 1/3 arrives → state = "receiving_fragments"
Time 50ms:  Fragment 2/3 arrives → state = "receiving_fragments"  
Time 100ms: Fragment 3/3 NEVER arrives → state STUCK = "receiving_fragments"
Time 150s:  System still waiting for Fragment 3/3 (PERMANENT HANG)
```

**Risk Level:** 🔴 **CRITICAL**

---

### Issue #4: Transmitter - TX Status Callback Uses Volatile Flags

**File:** `espnowtransmitter2/src/espnow/message_handler.cpp` (Lines 1-50)

**Current Implementation:**
```cpp
class EspnowMessageHandler {
private:
    volatile bool receiver_connected_{false};      // <-- FLAG BASED
    volatile bool transmission_active_{false};     // <-- FLAG BASED
    uint8_t receiver_mac_[6]{0};
};
```

**Problem:**
- `transmission_active_` is set by PROBE reception (async callback)
- `receiver_connected_` is set/cleared based on callback state
- **No formal state machine** to track connection lifecycle
- No CONNECTING → CONNECTED → AUTHENTICATED → TRANSMISSION_READY states

**Risk Level:** 🔴 **HIGH**

---

### Issue #5: Transmitter - Data Sender Task Polling Strategy is Inefficient

**File:** `espnowtransmitter2/src/espnow/data_sender.cpp` (Lines 18-45)

**Current Implementation:**
```cpp
void DataSender::task_impl(void* parameter) {
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t interval_ticks = pdMS_TO_TICKS(2000);  // 2 second interval
    
    while (true) {
        vTaskDelayUntil(&last_wake_time, interval_ticks);
        
        // POLLING approach - no state change notification
        if (EspnowMessageHandler::instance().is_transmission_active()) {
            LOG_TRACE("Sending test data (transmission active)");
            send_test_data_with_led_control();
        } else {
            LOG_TRACE("Skipping send (transmission inactive)");  // 1 log per 2 seconds!
        }
    }
}
```

**Problems:**
1. **Blind polling** every 2 seconds (inefficient)
2. **No event notification** when state changes
3. **No acknowledgment** if send fails
4. **Indeterminate retry behavior** (unclear what "active" means)
5. **No backoff on repeated failures**

**Risk Level:** 🟡 **MEDIUM**

---

### Issue #6: Both Codebases - Global Volatile Variables

**Files:**
- Receiver: `espnowreciever_2/src/globals.cpp` (Lines 31-46)
- Transmitter: `espnowtransmitter2/src/espnow/message_handler.h` (Lines 115+)

**Problem:**
```cpp
// Current approach (UNSAFE ACROSS TASKS)
namespace ESPNow {
    volatile uint8_t received_soc = 50;
    volatile int32_t received_power = 0;
    volatile bool data_received = false;  // <-- STALE FLAG
    volatile bool transmitter_connected = false;
    DirtyFlags dirty_flags;
}
```

**Issues:**
- `volatile` is NOT a synchronization mechanism for multi-task access
- Only 1 variable is read atomically; arrays and structs are NOT
- Race condition between ISR and main task context
- No version/sequence number to detect duplicates

**Risk Level:** 🔴 **HIGH**

---

## Part 2: State Machine Architecture (Proposed)

### Receiver: RX State Machine

```cpp
// === espnowreciever_2/src/espnow/espnow_rx_state.h (NEW FILE) ===

#pragma once
#include <cstdint>
#include <cstring>

/**
 * @brief RX Message State Machine
 * 
 * Tracks the lifecycle of a single received message from ISR queuing
 * through consumption and acknowledgment.
 */
enum class RXMessageState : uint8_t {
    IDLE,                  // No message
    QUEUED,                // In ISR queue, waiting for handler task
    PROCESSING,            // Handler task is processing
    VALID,                 // Validation complete, data is good
    CONSUMED,              // Consumer acknowledged receipt
    ERROR,                 // Validation failed
    TIMEOUT                // Message timeout (stale)
};

/**
 * @brief RX Fragment State Machine
 * 
 * Tracks multi-fragment message reassembly lifecycle.
 */
enum class RXFragmentState : uint8_t {
    IDLE,                  // No reassembly in progress
    RECEIVING,             // Fragments arriving (waiting for more)
    COMPLETE,              // All fragments received
    ABANDONED,             // Timeout or explicit abort
    ERROR                  // CRC or format error
};

/**
 * @brief Receiver message context
 */
struct RXMessageContext {
    RXMessageState state = RXMessageState::IDLE;
    
    uint32_t sequence_number = 0;          // Message ID (for duplicate detection)
    uint32_t timestamp_received_ms = 0;    // When first queued
    uint32_t timestamp_processed_ms = 0;   // When processing started
    
    uint8_t sender_mac[6] = {0};
    uint8_t message_type = 0;
    uint8_t data[250] = {0};
    size_t data_length = 0;
    
    // Error tracking
    const char* error_reason = nullptr;
    uint8_t retry_count = 0;
    
    // Convenience
    bool is_valid() const { return state == RXMessageState::VALID; }
    bool is_consumed() const { return state == RXMessageState::CONSUMED; }
    uint32_t age_ms() const { return millis() - timestamp_received_ms; }
    bool is_stale(uint32_t max_age_ms = 5000) const { 
        return age_ms() > max_age_ms && !is_consumed();
    }
};

/**
 * @brief Receiver fragment reassembly context
 */
struct RXFragmentContext {
    RXFragmentState state = RXFragmentState::IDLE;
    
    uint32_t sequence_number = 0;          // Fragment sequence
    uint8_t total_fragments = 0;           // Total expected fragments
    uint8_t received_fragments = 0;        // Fragments received so far
    uint32_t timestamp_started_ms = 0;     // When first fragment arrived
    
    // Reassembly buffer
    static constexpr size_t MAX_REASSEMBLY_SIZE = 1024;
    uint8_t reassembly_buffer[MAX_REASSEMBLY_SIZE] = {0};
    size_t buffer_offset = 0;
    
    // Fragment tracking
    uint8_t received_fragment_mask = 0;    // Bitmask for up to 8 fragments
    
    // Error tracking
    const char* error_reason = nullptr;
    
    // Convenience
    bool is_complete() const { return state == RXFragmentState::COMPLETE; }
    bool is_receiving() const { return state == RXFragmentState::RECEIVING; }
    uint32_t age_ms() const { return millis() - timestamp_started_ms; }
    bool is_timeout(uint32_t timeout_ms = 5000) const { 
        return is_receiving() && age_ms() > timeout_ms;
    }
    
    void reset() {
        state = RXFragmentState::IDLE;
        sequence_number = 0;
        total_fragments = 0;
        received_fragments = 0;
        timestamp_started_ms = 0;
        buffer_offset = 0;
        received_fragment_mask = 0;
        error_reason = nullptr;
    }
};
```

---

### Transmitter: TX State Machine

```cpp
// === espnowtransmitter2/src/espnow/espnow_tx_state.h (NEW FILE) ===

#pragma once
#include <cstdint>

/**
 * @brief TX Message State Machine
 * 
 * Tracks the lifecycle of a transmitted message from queuing through
 * ACK reception or timeout.
 */
enum class TXMessageState : uint8_t {
    IDLE,                  // Ready to send
    QUEUED,                // Waiting for transmission slot
    SENDING,               // Transmission in progress (ESP-NOW busy)
    WAITING_ACK,           // TX complete, waiting for ACK
    ACK_RECEIVED,          // ACK received from peer
    FAILED,                // Max retries exceeded
    TIMEOUT                // No ACK received within timeout
};

/**
 * @brief TX Connection State Machine
 * 
 * Tracks the lifecycle of the connection to receiver.
 */
enum class TXConnectionState : uint8_t {
    DISCONNECTED,          // No receiver detected
    DISCOVERING,           // Scanning for receiver (sending PROBE)
    CONNECTED,             // Receiver acknowledged our PROBE
    AUTHENTICATED,         // Exchange complete, ready for data
    TRANSMISSION_ACTIVE,   // Receiver requested data transmission
    ERROR,                 // Connection error
    RECONNECTING           // Attempting to re-establish connection
};

/**
 * @brief TX message context
 */
struct TXMessageContext {
    TXMessageState state = TXMessageState::IDLE;
    
    uint32_t sequence_number = 0;          // Message ID
    uint32_t timestamp_created_ms = 0;     // When queued for TX
    uint32_t timestamp_sent_ms = 0;        // When ESP-NOW sent completed
    
    uint8_t receiver_mac[6] = {0};
    uint8_t message_type = 0;
    uint8_t data[250] = {0};
    size_t data_length = 0;
    
    // Retry tracking
    uint8_t max_retries = 3;
    uint8_t retry_count = 0;
    uint32_t last_retry_ms = 0;
    uint32_t retry_backoff_ms = 100;
    
    // Error tracking
    const char* error_reason = nullptr;
    
    // Convenience
    bool needs_retry() const {
        return retry_count < max_retries;
    }
    uint32_t age_ms() const { return millis() - timestamp_created_ms; }
    bool is_stale(uint32_t max_age_ms = 10000) const { 
        return age_ms() > max_age_ms && 
               state != TXMessageState::ACK_RECEIVED &&
               state != TXMessageState::FAILED;
    }
};

/**
 * @brief TX connection context
 */
struct TXConnectionContext {
    TXConnectionState state = TXConnectionState::DISCONNECTED;
    
    uint8_t receiver_mac[6] = {0};
    uint32_t timestamp_last_probe_ms = 0;
    uint32_t timestamp_last_ack_ms = 0;
    
    uint8_t probe_count = 0;
    uint8_t ack_count = 0;
    
    // State transition reason
    const char* state_reason = nullptr;
    
    // Convenience
    bool is_connected() const { 
        return state >= TXConnectionState::CONNECTED;
    }
    bool is_ready_for_transmission() const {
        return state == TXConnectionState::TRANSMISSION_ACTIVE;
    }
    uint32_t ms_since_last_ack() const { 
        return millis() - timestamp_last_ack_ms;
    }
    bool is_heartbeat_lost(uint32_t heartbeat_timeout_ms = 10000) const {
        return is_connected() && ms_since_last_ack() > heartbeat_timeout_ms;
    }
};
```

---

## Part 3: Implementation Steps

### Step 1: Create Receiver RX State Machine Handler

**File:** `espnowreciever_2/src/espnow/espnow_rx_state_machine.h` (NEW)

```cpp
#pragma once
#include "espnow_rx_state.h"
#include <queue>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * @brief Manages RX message state transitions
 * 
 * Singleton that coordinates:
 * 1. Message state lifecycle (QUEUED → PROCESSING → VALID/ERROR → CONSUMED)
 * 2. Fragment reassembly state machine
 * 3. Timeout detection and recovery
 * 4. Duplicate detection via sequence numbers
 */
class EspnowRXStateMachine {
public:
    static EspnowRXStateMachine& instance();
    
    /**
     * @brief Handle ISR callback - new message queued
     * @param ctx Message context with raw data
     */
    void on_message_queued(const RXMessageContext& ctx);
    
    /**
     * @brief Begin processing queued message
     * @return Context for processing, or null if none available
     */
    RXMessageContext* begin_processing();
    
    /**
     * @brief Complete processing - message is valid
     * @param ctx Message context
     */
    void on_message_valid(const RXMessageContext& ctx);
    
    /**
     * @brief Message processing failed
     * @param ctx Message context with error details
     */
    void on_message_error(const RXMessageContext& ctx);
    
    /**
     * @brief Consumer acknowledged receipt
     * @param sequence Message sequence number
     */
    void on_message_consumed(uint32_t sequence);
    
    /**
     * @brief Begin fragment reassembly
     * @param sequence Fragment sequence
     * @param total Total fragments expected
     */
    void begin_fragment_reassembly(uint32_t sequence, uint8_t total);
    
    /**
     * @brief Add fragment to reassembly buffer
     * @param sequence Reassembly sequence
     * @param fragment_num Which fragment (1-based)
     * @param data Fragment data
     * @param len Fragment length
     * @return true if reassembly complete
     */
    bool add_fragment(uint32_t sequence, uint8_t fragment_num, 
                      const uint8_t* data, size_t len);
    
    /**
     * @brief Check and recover from timeouts
     * @return Number of abandoned/recovered timeouts
     */
    uint8_t check_timeouts();
    
    /**
     * @brief Get statistics
     */
    struct Stats {
        uint32_t total_messages = 0;
        uint32_t valid_messages = 0;
        uint32_t failed_messages = 0;
        uint32_t stale_messages = 0;
        uint32_t fragment_timeouts = 0;
    };
    Stats get_stats() const { return stats_; }
    
private:
    EspnowRXStateMachine();
    ~EspnowRXStateMachine();
    
    EspnowRXStateMachine(const EspnowRXStateMachine&) = delete;
    EspnowRXStateMachine& operator=(const EspnowRXStateMachine&) = delete;
    
    // Message queue (max 16 messages in flight)
    std::queue<RXMessageContext> message_queue_;
    SemaphoreHandle_t queue_mutex_;
    
    // Fragment reassembly (only 1 active reassembly at a time)
    RXFragmentContext active_reassembly_;
    
    // Statistics
    Stats stats_;
    
    // Sequence number tracking for duplicate detection
    uint32_t last_sequence_ = 0;
};
```

---

### Step 2: Create Transmitter TX State Machine Handler

**File:** `espnowtransmitter2/src/espnow/espnow_tx_state_machine.h` (NEW)

```cpp
#pragma once
#include "espnow_tx_state.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * @brief Manages TX message and connection state machines
 * 
 * Singleton that coordinates:
 * 1. Message state lifecycle (QUEUED → SENDING → ACK_RECEIVED)
 * 2. Connection state transitions with proper entry/exit
 * 3. Retry logic with exponential backoff
 * 4. Heartbeat timeout detection
 */
class EspnowTXStateMachine {
public:
    static EspnowTXStateMachine& instance();
    
    // === Connection State Management ===
    
    /**
     * @brief Transition connection to new state
     * @param new_state New connection state
     * @param reason Human-readable reason for transition
     */
    void set_connection_state(TXConnectionState new_state, const char* reason);
    
    /**
     * @brief Get current connection state
     */
    TXConnectionState get_connection_state() const;
    
    /**
     * @brief Is connection ready for transmission?
     */
    bool is_transmission_ready() const;
    
    /**
     * @brief Handle PROBE ACK received
     */
    void on_probe_ack(const uint8_t* receiver_mac);
    
    /**
     * @brief Handle heartbeat message received
     */
    void on_heartbeat_received();
    
    /**
     * @brief Check and handle connection timeout
     * @return true if connection was lost
     */
    bool check_connection_timeout();
    
    // === Message State Management ===
    
    /**
     * @brief Queue message for transmission
     * @param ctx Message context
     */
    void queue_message(const TXMessageContext& ctx);
    
    /**
     * @brief Get next message to send
     * @return Message context or nullptr if none ready
     */
    TXMessageContext* get_next_to_send();
    
    /**
     * @brief Mark message as sent (ESP-NOW returned)
     * @param sequence Message sequence number
     */
    void on_send_complete(uint32_t sequence, bool success);
    
    /**
     * @brief ACK received from peer
     * @param sequence Message sequence number
     */
    void on_ack_received(uint32_t sequence);
    
    /**
     * @brief Handle send timeout/failure
     * @param sequence Message sequence number
     * @return true if retrying, false if max retries exceeded
     */
    bool on_send_failed(uint32_t sequence);
    
    /**
     * @brief Check and handle message timeouts
     * @return Number of failed messages
     */
    uint8_t check_message_timeouts();
    
    // === Statistics ===
    
    struct Stats {
        uint32_t total_sends = 0;
        uint32_t successful_sends = 0;
        uint32_t failed_sends = 0;
        uint32_t retried_sends = 0;
        uint32_t connection_resets = 0;
    };
    Stats get_stats() const { return stats_; }
    
private:
    EspnowTXStateMachine();
    ~EspnowTXStateMachine();
    
    EspnowTXStateMachine(const EspnowTXStateMachine&) = delete;
    EspnowTXStateMachine& operator=(const EspnowTXStateMachine&) = delete;
    
    TXConnectionContext connection_;
    std::queue<TXMessageContext> message_queue_;
    SemaphoreHandle_t queue_mutex_;
    Stats stats_;
};
```

---

## Part 4: Integration Steps

### Phase 2C: State Machine Hardening (NEW PHASE)

**Timeline:** 2-3 weeks (depends on testing)

#### 4.1: Receiver Integration

1. **Create state machine files:**
   - `src/espnow/espnow_rx_state.h`
   - `src/espnow/espnow_rx_state_machine.h`
   - `src/espnow/espnow_rx_state_machine.cpp`

2. **Refactor espnow_callbacks.cpp:**
   ```cpp
   void on_data_recv(const uint8_t *mac, const uint8_t *data, int len) {
       // Create context
       RXMessageContext ctx;
       ctx.state = RXMessageState::QUEUED;
       ctx.sequence_number = get_next_sequence();  // NEW
       ctx.timestamp_received_ms = millis();
       memcpy(ctx.sender_mac, mac, 6);
       // ... copy data
       
       // Notify state machine instead of blindly queueing
       EspnowRXStateMachine::instance().on_message_queued(ctx);
   }
   ```

3. **Refactor espnow_tasks.cpp:**
   - Remove flag-based tracking
   - Use state machine for message lifecycle
   - Implement timeout detection
   - Remove global volatile flags (migrate to state machine)

4. **Update globals.cpp:**
   - Remove: `volatile bool data_received`
   - Remove: `DirtyFlags` (use state-based approach)
   - Keep: `received_soc`, `received_power` (but make atomic)

#### 4.2: Transmitter Integration

1. **Create state machine files:**
   - `src/espnow/espnow_tx_state.h`
   - `src/espnow/espnow_tx_state_machine.h`
   - `src/espnow/espnow_tx_state_machine.cpp`

2. **Refactor message_handler.cpp:**
   ```cpp
   void EspnowMessageHandler::handle_request_data(const espnow_queue_msg_t& msg) {
       // OLD: transmission_active_ = true;
       
       // NEW: Transition connection state
       EspnowTXStateMachine::instance().set_connection_state(
           TXConnectionState::TRANSMISSION_ACTIVE,
           "REQUEST_DATA received from receiver"
       );
   }
   ```

3. **Refactor data_sender.cpp:**
   - Replace polling with **event notification**
   - Use state machine to check transmission readiness
   - Implement proper retry with backoff

4. **Update message_handler.h:**
   - Replace volatile flags with state machine methods
   - Remove `receiver_connected_` and `transmission_active_` flags

---

## Part 5: Cleanup & Redundancy Removal

### 5.1: Files to Remove/Consolidate

**Receiver:**
- ❌ Old flag-based message handling code (lines ~440-700 in espnow_tasks.cpp)
- ❌ Manual dirty flag management in globals.cpp
- ❌ Redundant timeout checks (consolidate to state machine)
- ❌ Duplicate connection state tracking (use singular state machine)

**Transmitter:**
- ❌ Ad-hoc retry logic (consolidate to state machine)
- ❌ Manual `transmission_active_` flag tracking
- ❌ Polling-based data sender (replace with event-driven)
- ❌ Duplicate connection state in message_handler.h

### 5.2: Consolidation Plan

**Before (Messy):**
```
Message handling scattered across:
- espnow_callbacks.cpp (queuing)
- espnow_tasks.cpp (processing + state)
- globals.cpp (flag tracking)
- message_handler.h (different approach in transmitter)
```

**After (Clean):**
```
Unified architecture:
- RX callbacks → RX State Machine (singular source of truth)
- TX callbacks → TX State Machine (singular source of truth)
- State transitions centralized
- No redundant flag variables
```

### 5.3: Code Cleanup Checklist

- [ ] Remove all `volatile bool` flags (except atomic operations)
- [ ] Remove all ad-hoc timeout checks (use state machine)
- [ ] Remove all manual retry counters (use state machine)
- [ ] Remove all "dirty flags" pattern (use state notification)
- [ ] Consolidate connection state (one state machine, not scattered)
- [ ] Remove redundant queue/buffer management
- [ ] Remove blind polling loops (replace with event notification)

---

## Part 6: Testing Strategy

### Unit Tests

```cpp
// === espnowreciever_2/test/test_rx_state_machine.cpp (NEW) ===

void test_rx_message_lifecycle() {
    // Create message
    RXMessageContext ctx;
    ctx.sequence_number = 1;
    ctx.state = RXMessageState::IDLE;
    
    // Transition: IDLE → QUEUED
    EspnowRXStateMachine::instance().on_message_queued(ctx);
    ASSERT_EQ(ctx.state, RXMessageState::QUEUED);
    
    // Transition: QUEUED → PROCESSING
    auto processing = EspnowRXStateMachine::instance().begin_processing();
    ASSERT_NE(processing, nullptr);
    ASSERT_EQ(processing->state, RXMessageState::PROCESSING);
    
    // Transition: PROCESSING → VALID
    EspnowRXStateMachine::instance().on_message_valid(*processing);
    ASSERT_EQ(processing->state, RXMessageState::VALID);
    
    // Transition: VALID → CONSUMED
    EspnowRXStateMachine::instance().on_message_consumed(1);
    ASSERT_EQ(processing->state, RXMessageState::CONSUMED);
}

void test_fragment_timeout() {
    EspnowRXStateMachine& sm = EspnowRXStateMachine::instance();
    
    // Start fragment reassembly
    sm.begin_fragment_reassembly(100, 3);
    
    // Add fragment 1
    uint8_t data[100] = {0};
    sm.add_fragment(100, 1, data, 100);
    
    // Advance time 6 seconds (beyond 5s timeout)
    advanced_millis(6000);
    
    // Check timeouts
    uint8_t abandoned = sm.check_timeouts();
    ASSERT_EQ(abandoned, 1);
    ASSERT_EQ(sm.active_reassembly_.state, RXFragmentState::ABANDONED);
}

void test_duplicate_detection() {
    EspnowRXStateMachine& sm = EspnowRXStateMachine::instance();
    
    // First message
    RXMessageContext ctx1;
    ctx1.sequence_number = 42;
    sm.on_message_queued(ctx1);
    
    // Duplicate message (same sequence)
    RXMessageContext ctx2;
    ctx2.sequence_number = 42;
    sm.on_message_queued(ctx2);  // Should be ignored or marked as duplicate
    
    ASSERT_EQ(sm.get_stats().total_messages, 1);  // Only one unique message
}
```

### Integration Tests

1. **Stale Flag Elimination Test**
   - Send message
   - Verify state machine tracks consumption
   - Verify old flag approach would fail

2. **Fragment Timeout Test**
   - Send fragments 1/3 and 2/3
   - Wait for timeout
   - Verify automatic recovery (not hanging)

3. **Duplicate Message Test**
   - Send same message twice
   - Verify second is rejected
   - Verify display updates only once

4. **Connection State Test**
   - Disconnect transmitter
   - Verify TX state machine detects timeout
   - Verify clean reconnection

### Regression Tests

- All existing message types still work
- Display updates correct
- LED indicators correct
- Battery data cached properly
- Config sync still works

---

## Part 7: Risk Assessment & Mitigation

| Risk | Impact | Mitigation | Priority |
|------|--------|-----------|----------|
| **Race condition on state transitions** | Data corruption | Use semaphores for state access | 🔴 HIGH |
| **Breaking change for consumers** | System crashes | Version 2.1 breaking change (documented) | 🔴 HIGH |
| **Fragment timeout too aggressive** | Lost messages | Configure timeout at compile-time (5s default) | 🟡 MEDIUM |
| **State machine bugs in first impl** | Subtle hangs | Extensive unit + integration testing | 🔴 HIGH |
| **Performance impact of new tracking** | High latency | Profile and optimize hot paths | 🟡 MEDIUM |

---

## Part 8: Migration Path

### Version 2.0 → 2.1 (This Work)

**BREAKING CHANGES:**
1. Remove `ESPNow::data_received` flag
2. Remove `ESPNow::dirty_flags` structure  
3. Change message handling callback signatures
4. State machine is now THE source of truth (not flags)

**DEPRECATION PERIOD:**
- None (clean break, well-documented)

**MIGRATION GUIDE FOR CONSUMERS:**

Before:
```cpp
if (ESPNow::data_received) {
    uint8_t soc = ESPNow::received_soc;
    int32_t power = ESPNow::received_power;
    ESPNow::data_received = false;  // Consumer must clear flag
}
```

After:
```cpp
// Option 1: Poll state machine
auto* msg = EspnowRXStateMachine::instance().begin_processing();
if (msg) {
    uint8_t soc = extract_soc_from_message(msg);
    int32_t power = extract_power_from_message(msg);
    EspnowRXStateMachine::instance().on_message_consumed(msg->sequence_number);
}

// Option 2: Event callback (preferred)
register_message_consumer([](const RXMessageContext& msg) {
    uint8_t soc = extract_soc_from_message(msg);
    int32_t power = extract_power_from_message(msg);
    // Callback framework auto-marks consumed
});
```

---

## Part 9: Detailed Implementation Order

### Week 1: Foundation

1. **Create RX state files** (4 hours)
   - `espnow_rx_state.h` with enums and structs
   
2. **Create RX state machine** (8 hours)
   - `espnow_rx_state_machine.h/cpp` implementation
   - Basic functionality (queue, timeout detection)

3. **Create TX state files** (4 hours)
   - `espnow_tx_state.h` with enums and structs

4. **Create TX state machine** (8 hours)
   - `espnow_tx_state_machine.h/cpp` implementation

### Week 2: Receiver Integration

5. **Refactor receiver callbacks** (4 hours)
   - Update `espnow_callbacks.cpp` to use state machine
   - Remove flag-based logic

6. **Refactor receiver tasks** (8 hours)
   - Update `espnow_tasks.cpp` message handlers
   - Remove global flag tracking
   - Implement state transitions

7. **Update receiver globals** (2 hours)
   - Remove deprecated flag variables
   - Update documentation

### Week 3: Transmitter Integration

8. **Refactor transmitter handlers** (6 hours)
   - Update `message_handler.cpp` state transitions
   - Remove volatile flag usage

9. **Refactor transmitter data sender** (4 hours)
   - Replace polling with event notification
   - Implement proper retry logic

10. **Testing & cleanup** (8 hours)
    - Unit tests for both state machines
    - Integration tests
    - Performance profiling
    - Documentation updates

---

## Part 10: Success Criteria

✅ **State machines properly manage all transitions**
- No more ad-hoc flag variables
- All state changes logged and traceable

✅ **Zero stale state issues**
- Messages properly consumed (no duplication)
- Fragments timeout properly (no hanging)
- Retries work with clear semantics

✅ **Clean codebase**
- No redundant flag tracking
- No global volatile variables (except atomic types)
- Centralized source of truth for each state

✅ **All tests pass**
- Unit tests for state machines
- Integration tests with real hardware
- Regression tests for all message types

✅ **Production-ready**
- Thorough documentation
- Clear error messages
- Performance acceptable
- No resource leaks

---

## Summary

The current ESP-NOW implementation has **critical state management issues** that must be resolved before production deployment:

| Component | Issue | Severity | Solution |
|-----------|-------|----------|----------|
| Receiver | Stale `data_received` flag | CRITICAL | State machine with consumption tracking |
| Receiver | Fragment timeout hang | CRITICAL | Fragment state machine with timeout |
| Receiver | Race conditions (volatile) | HIGH | Proper synchronization primitives |
| Transmitter | Ad-hoc connection state | HIGH | TX connection state machine |
| Transmitter | Polling-based data sender | MEDIUM | Event-driven with state checks |
| Both | Redundant flag variables | HIGH | Consolidate to state machines |

**Recommended Action:** Implement Phase 2C (State Machine Hardening) before any production deployment.

**Estimated Effort:** 3 weeks (2 developers) or 6 weeks (1 developer)

**Blocking:** Further feature development until completed
