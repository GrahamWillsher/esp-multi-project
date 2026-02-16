# ESP-NOW Connection State Management Architecture
## Complete System Proposal: Transmitter & Receiver

## Date: 2026-02-12
## Status: ARCHITECTURAL REDESIGN REQUIRED
## Document Version: 2.0 - Complete System Coverage
## Last Updated: 2026-02-12
## Scope: Transmitter + Receiver + Shared Common Code

---

## Executive Summary

The ESP-NOW system currently lacks a **comprehensive connection state machine** to coordinate all subsystems on **both transmitter and receiver**. This manifests most critically as **race conditions** where subsystems attempt to send data **before the WiFi channel is fully locked**, resulting in `ESP_ERR_ESPNOW_ARG` errors.

**Root Cause:** There is NO centralized connection lifecycle management - only scattered binary flags that don't adequately represent the true state of the ESP-NOW connection on either device.

**Scope:** This document proposes a complete ESP-NOW connection state machine architecture that:
- **Applies to BOTH transmitter and receiver** devices
- Manages the full connection lifecycle (discovery → connection → operation → degradation → recovery)
- Includes proper channel hopping and locking states (transmitter)
- Includes proper ACK response and handshake states (receiver)
- **Provides shared common code** in `esp32common` library for both devices
- Coordinates all subsystems (discovery, heartbeat, config sync, data transmission)
- Provides clear state-based behavior policies for both roles

**Impact:** Critical - affects system reliability during discovery, reconnection, and degraded network conditions on both transmitter and receiver.

**Architecture:** Shared base classes in `esp32common/espnow_common_utils/` with device-specific extensions in transmitter and receiver projects.

---

## System Architecture Overview

### Device Roles & Responsibilities

**Transmitter (Active Discovery Role):**
- Initiates connection by broadcasting PROBE messages
- Performs active channel hopping (channels 1-14)
- Switches to receiver's channel when ACK received
- Manages complex state machine (17 states)
- Sends data to receiver (battery info, config, etc.)
- Monitors connection quality via heartbeat

**Receiver (Passive Response Role):**
- Listens on fixed WiFi channel
- Responds to PROBE with ACK (includes channel number)
- Waits for transmitter to complete channel lock
- Manages simpler state machine (10 states)
- Receives data from transmitter
- Sends requests/responses (CONFIG_REQUEST, etc.)
- Monitors connection via heartbeat reception

### Shared Common Code Architecture

**Location:** `esp32common/espnow_common_utils/`

**Shared Components (CLASS DEFINITIONS - Each device creates its own instances):**
```
esp32common/
├── espnow_common_utils/
│   ├── espnow_timing_config.h          // Timing constants (SHARED VALUES)
│   ├── espnow_connection_base.h        // Base class definition (SHARED CODE)
│   ├── espnow_connection_base.cpp      // Common method implementations (SHARED CODE)
│   ├── espnow_send_wrapper.h           // Safe send interface (SHARED CODE)
│   ├── espnow_send_wrapper.cpp         // Send retry logic (SHARED CODE)
│   ├── espnow_message_queue.h          // Queue CLASS definition (SHARED CODE)
│   ├── espnow_message_queue.cpp        // Queue implementation (SHARED CODE)
│   ├── espnow_metrics.h                // Metrics CLASS definition (SHARED CODE)
│   └── espnow_metrics.cpp              // Metrics methods (SHARED CODE)
```

**CRITICAL:** Each device creates its **own instances** of these classes:
- Transmitter has: `TransmitterConnectionManager` with its own queue, metrics, state
- Receiver has: `ReceiverConnectionManager` with its own queue, metrics, state
- **NO shared data** between devices - complete isolation

**Device-Specific Extensions:**
```
ESPnowtransmitter2/
└── src/espnow/
    ├── transmitter_connection_manager.h    // Extends espnow_connection_base
    ├── transmitter_connection_manager.cpp  // Transmitter-specific states
    └── discovery_task.cpp                  // Channel hopping logic

espnowreciever_2/
└── lib/espnow_receiver/
    ├── receiver_connection_manager.h       // Extends espnow_connection_base
    ├── receiver_connection_manager.cpp     // Receiver-specific states
    └── ack_handler.cpp                     // ACK response logic
```

### Inheritance Hierarchy

**IMPORTANT:** Shared code means shared CLASS definitions, NOT shared data!

```cpp
// Base class (esp32common) - SHARED CODE
class EspNowConnectionBase {
protected:
    // Common state management METHODS
    // Common metrics tracking METHODS
    // Common send wrapper METHODS
    // Common event callbacks METHODS
    
    // Each instance has its OWN data:
    uint8_t peer_mac_[6];              // Own peer
    uint32_t successful_sends_;         // Own counter
    std::vector<StateHistoryEntry> state_history_;  // Own history
};

// Transmitter-specific (ESPnowtransmitter2)
class TransmitterConnectionManager : public EspNowConnectionBase {
    // Discovery states
    // Channel hopping states
    // Transmitter-specific logic
    
    // Transmitter has its OWN queue instance:
    EspNowMessageQueue transmitter_queue_;  // Not shared!
};

// Receiver-specific (espnowreciever_2)
class ReceiverConnectionManager : public EspNowConnectionBase {
    // ACK response states
    // Request handling states
    // Receiver-specific logic
    
    // Receiver has its OWN queue instance:
    EspNowMessageQueue receiver_queue_;     // Not shared!
};
```

**Key Point:** 
- ✅ Shared: Class definitions, method implementations, logic
- ❌ NOT Shared: Data instances, queues, metrics, state
- ✅ Each device has completely independent data - NO cross-contamination

---

## Problem Timeline (from transmitter logs)

```
Timeline of Events:
===================
T+0ms:   Broadcasting PROBE on channel 10
T+50ms:  ✓ ACK received from receiver
T+55ms:  CONFIG_REQUEST_FULL received from receiver
T+60ms:  ❌ CONFIG: Failed to send fragment 0: ESP_ERR_ESPNOW_ARG  <-- PREMATURE SEND
T+65ms:  Channel in ACK: 11 (receiver's WiFi channel)
T+70ms:  ❌ Failed to send IP data: ESP_ERR_ESPNOW_ARG            <-- PREMATURE SEND
T+75ms:  ✓ Receiver found on channel 11
T+80ms:  ❌ Failed to send battery settings: ESP_ERR_ESPNOW_ARG   <-- PREMATURE SEND
T+85ms:  ✓ Receiver registered as peer
T+90ms:  ❌ Power profile transmission FAILED                      <-- PREMATURE SEND
T+100ms: ✓ Receiver discovered on channel 11
T+110ms: ❌ Version beacon send failed: ESP_ERR_ESPNOW_ARG        <-- PREMATURE SEND
T+120ms: Forcing channel lock to 11...
T+150ms: ✓ Channel locked and verified: 11
T+160ms: ❌ Still failing to send!                                 <-- PERSISTENT ISSUE
```

---

## Critical Discovery Sequence Analysis

### Current Flow (FLAWED)

```cpp
// File: discovery_task.cpp, active_channel_hop_scan()

1. ACK Received (Line 464-476)
   - ACK arrives from receiver
   - ack_received = true
   - ACK contains receiver's channel (11)
   - break; exits transmit loop
   
   ⚠️ PROBLEM: ACK message is ALSO processed by message_handler.cpp via handle_ack()
   ⚠️ This sets receiver_connected_ = true IMMEDIATELY
   ⚠️ All subsystems now think connection is established!

2. WiFi Channel Change (Line 485-486)
   - esp_wifi_set_channel(ack_channel, WIFI_SECOND_CHAN_NONE);
   - LOG_DEBUG("[DISCOVERY] WiFi channel set to %d", ack_channel);
   
   ⚠️ PROBLEM: WiFi channel change takes time (hardware operation)
   ⚠️ NO wait/verification after this critical operation

3. Peer Registration (Line 488-495)
   - memcpy(receiver_mac, ack_mac, 6);
   - EspnowPeerManager::add_peer(ack_mac, ack_channel);
   - LOG_INFO("[DISCOVERY] ✓ Receiver registered as peer");
   
   ⚠️ PROBLEM: Peer registration adds to ESP-NOW peer table
   ⚠️ WiFi driver needs time to process this

4. Delay for Stabilization (Line 497-500)
   - delay(200);  // Only delay AFTER peer registration
   - LOG_DEBUG("[DISCOVERY] Peer registration stabilized");
   
   ✓ GOOD: At least there's some delay
   ❌ BAD: Too late - damage already done!

5. Return Success (Line 502-503)
   - *discovered_channel = ack_channel;
   - return true;  // Discovery complete!
```

### Concurrent Message Processing (THE PROBLEM)

While discovery is executing steps 1-5 above, **the RX task continues to process messages:**

```cpp
// File: message_handler.cpp, rx_task_impl()

PARALLEL EXECUTION THREAD:
-------------------------
- RX queue is being processed continuously
- ACK message arrives → handle_ack() called
- CONFIG_REQUEST_FULL arrives → handle_config_request_full() called
- Both set receiver_connected_ = true
- Both trigger immediate ESP-NOW sends!

RACE CONDITION:
--------------
CONFIG request handler tries to send BEFORE:
  - WiFi channel change completes
  - Peer registration completes  
  - Channel lock verification happens
```

---

## Systems Attempting Premature Sends

### 1. Config Manager (handle_config_request_full)
**Location:** `message_handler.cpp` line 825
**Trigger:** Immediate response to CONFIG_REQUEST_FULL message
**Failure:** Tries to send config snapshot while channel is changing

```cpp
void EspnowMessageHandler::handle_config_request_full(const espnow_queue_msg_t& msg) {
    // ... prepare config snapshot ...
    
    // ❌ SENDS IMMEDIATELY - no check if channel is stable!
    ConfigManager::send_snapshot(...);  // FAILS with ESP_ERR_ESPNOW_ARG
}
```

### 2. IP Data Sender
**Location:** Multiple files responding to connection event
**Trigger:** `receiver_connected_` flag set to true
**Failure:** Sends network config before channel locked

### 3. Battery Settings Sender
**Location:** Settings manager responding to connection
**Trigger:** Connection established event
**Failure:** Sends settings immediately on connection

### 4. Power Profile Transmission
**Location:** Data transmission task
**Trigger:** ">>> Power profile transmission STARTED" message
**Failure:** Starts streaming data before channel stable

### 5. Version Beacon Manager
**Location:** `version_beacon_manager.cpp`
**Trigger:** Called from discovery completion
**Failure:** send_version_beacon() fails with channel error

---

## The "receiver_connected_" Flag Problem

### Current Implementation

```cpp
// File: message_handler.h
class EspnowMessageHandler {
private:
    volatile bool receiver_connected_{false};  // ⚠️ BINARY FLAG - NOT ENOUGH!
};
```

**Problems:**
1. **Binary state** - either connected or not, no "connecting" state
2. **Set immediately** when ACK received, before channel is locked
3. **No distinction** between "peer exists" and "channel is ready"
4. **Race condition** - multiple systems read this flag concurrently

### What Happens

```
Initial state:    receiver_connected_ = false ✓ (nothing sends)
ACK arrives:      receiver_connected_ = true  ❌ (EVERYTHING sends!)
Channel changing: WiFi in transition...        ❌ (sends fail!)
Peer registering: ESP-NOW table updating...    ❌ (sends still fail!)
Channel locked:   Finally stable               ✓ (sends would work now)
```

---

## Proposed Solution: Comprehensive Connection State Machine

### Complete Connection Lifecycle States

```cpp
enum class EspNowConnectionState : uint8_t {
    // === DISCONNECTED STATES ===
    DISCONNECTED,               // No connection, idle
    DISCOVERY_INIT,             // Starting discovery process
    
    // === CHANNEL HOPPING STATES ===
    CHANNEL_HOPPING,            // Broadcasting PROBE, hopping channels 1-14
    ACK_RECEIVED,               // ACK received, know target channel
    
    // === CHANNEL LOCKING STATES (Critical for race condition fix) ===
    CHANNEL_TRANSITION,         // WiFi hardware changing to target channel
    PEER_REGISTRATION,          // Adding peer to ESP-NOW driver
    CHANNEL_STABILIZING,        // Waiting for hardware to settle
    CHANNEL_LOCKED,             // Channel verified and stable
    
    // === HANDSHAKE & AUTHENTICATION STATES ===
    HANDSHAKING,                // Exchanging version beacons
    VERSION_VERIFIED,           // Version compatibility confirmed
    CONFIG_SYNCING,             // Initial configuration synchronization
    
    // === CONNECTED STATES ===
    CONNECTED_ACTIVE,           // ✓ Fully operational, data flowing
    CONNECTED_IDLE,             // Connected but no recent activity
    
    // === DEGRADED STATES ===
    CONNECTION_DEGRADED,        // Missed heartbeats, warning state
    CONNECTION_MARGINAL,        // Severe packet loss, near failure
    
    // === RECOVERY STATES ===
    RECONNECTING,               // Lost connection, attempting recovery
    FAST_RECONNECT,             // Quick reconnect (channel still known)
    FULL_REDISCOVERY            // Complete discovery restart required
};
```

### Transmitter State Machine - Full State Transition Flow

```
                    DISCONNECTED
                  : EspNowConnectionManager

```cpp
class EspNowConnectionManager {
public:
    static EspNowConnectionManager& instance();
    
    // ========== STATE QUERIES ==========
    
    // Critical: Can we send data right now?
    bool is_ready_to_send() const {
        return state_ >= EspNowConnectionState::CONNECTED_ACTIVE &&
               state_ <= EspNowConnectionState::CONNECTION_MARGINAL;
    }
    
    // Is connection established (any connected state)?
    bool is_connected() const {
        return state_ >= EspNowConnectionState::CONNECTED_ACTIVE &&
               state_ <= EspNowConnectionState::CONNECTED_IDLE;
    }
    
    // Are we in channel locking phase?
    bool is_channel_locking() const {
        return state_ >= EspNowConnectionState::CHANNEL_TRANSITION &&
               state_ <= EspNowConnectionState::CHANNEL_LOCKED;
    }
    
    // Are we in discovery/hopping phase?
    bool is_discovering() const {
        return state_ >= EspNowConnectionState::DISCOVERY_INIT &&
               state_ <= EspNowConnectionState::ACK_RECEIVED;
    }
    
    // Is connection degraded?
    bool is_degraded() const {
        return state_ >= EspNowConnectionState::CONNECTION_DEGRADED &&
               state_ <= EspNowConnectionState::CONNECTION_MARGINAL;
    }
    
    // Are we recovering?
    bool is_recovering() const {
        return state_ >= EspNowConnectionState::RECONNECTING &&
               state_ <= EspNowConnectionState::FULL_REDISCOVERY;
    }
    
    // Get current state
    EspNowConnectionState get_state() const { return state_; }
    const char* get_state_string() const;
    
    // ========== STATE TRANSITIONS ==========
    
    // Set new state (with validation and logging)
    bool set_state(EspNowConnectionState new_state);
    
    // Convenience state transitions
    void start_discovery();
    void start_channel_hopping();
    void ack_received();
    void begin_channel_lock();
    void channel_locked();
    void handshake_complete();
    void connection_established();
    void mark_active();
    void mark_idle();
    void degrade_connection(uint8_t severity);  // 1=degraded, 2=marginal
    void connection_lost();
    void start_reconnect(bool fast = false);
    
    // ========== BLOCKING OPERATIONS ==========
    
    // Wait until ready to send (with timeout)
    bool wait_until_ready(uint32_t timeout_ms);
    
    // Wait for specific state
    bool wait_for_state(EspNowConnectionState target, uint32_t timeout_ms);
    
    // ========== METRICS & DIAGNOSTICS ==========
    
    uint32_t get_time_in_state() const;  // milliseconds in current state
    uint32_t get_total_reconnects() const;
    uint32_t get_total_discoveries() const;
    uint8_t get_connection_quality() const;  // 0-100%
    
    // Additional diagnostic methods
    uint32_t get_successful_sends() const;
    uint32_t get_failed_sends() const;
    uint32_t get_total_state_changes() const;
    uint32_t get_uptime_connected_ms() const;     // Total time spent in connected states
    uint32_t get_average_reconnect_time_ms() const;
    float get_send_success_rate() const;          // Percentage (0.0-100.0)
    
    // State history (for debugging)
    struct StateHistoryEntry {
        EspNowConnectionState state;
        uint32_t timestamp_ms;
        uint32_t duration_ms;
    };
    const std::vector<StateHistoryEntry>& get_state_history() const; // Last N state changes
    
    // Generate diagnostic report
    String generate_diagnostic_report() const;
    void log_diagnostics() const;
    
    // Reset metrics (for testing)
    void reset_metrics();
    
    // ========== SUBSYSTEM COORDINATION ==========
    
    // Should subsystem X operate in current state?
    bool should_send_heartbeat() const;
    bool should_send_version_beacon() const;
    bool should_sync_config() const;
    bool should_transmit_data() const;
    bool should_accept_incoming() const;
    
    // ========== SAFE SEND WRAPPER ==========
    
    // Thread-safe send with state checking and retry logic
    esp_err_t safe_send(
        const uint8_t* peer_addr,
        const uint8_t* data,
        size_t len,
        const char* context = "DATA",
        uint8_t priority = 1  // 0=low, 1=normal, 2=high
    );
    
    // Batch send multiple messages
    struct SendBatch {
        const uint8_t* data;
        size_t len;
        const char* context;
        uint8_t priority;
    };
    esp_err_t safe_send_batch(
        const uint8_t* peer_addr,
        const std::vector<SendBatch>& messages
    );
    
    // ========== EVENT CALLBACKS (for integration) ==========
    
    // Register callbacks for state changes
    using StateChangeCallback = std::function<void(EspNowConnectionState old_state, 
                                                     EspNowConnectionState new_state)>;
    void register_state_change_callback(StateChangeCallback callback);
    
    // Register callbacks for connection events
    using ConnectionEventCallback = std::function<void(const char* event, const char* details)>;
    void register_event_callback(ConnectionEventCallback callback);
    
    // ========== HELPER UTILITIES ==========
    
    // Convert state enum to human-readable string
    static const char* state_to_string(EspNowConnectionState state);
    
    // Get state category
    enum class StateCategory {
        DISCONNECTED,
        DISCOVERING,
        CHANNEL_LOCKING,
        HANDSHAKING,
        CONNECTED,
        DEGRADED,
        RECOVERING
    };
    static StateCategory get_state_category(EspNowConnectionState state);
    
    // Check if state allows specific operations
    static bool state_allows_send(EspNowConnectionState state);
    static bool state_allows_discovery(EspNowConnectionState state);
    static bool state_requires_watchdog(EspNowConnectionState state);
    
private:
    volatile EspNowConnectionState state_{EspNowConnectionState::DISCONNECTED};
    uint32_t state_enter_time_{0};
    SemaphoreHandle_t state_change_sem_{nullptr};
    SemaphoreHandle_t send_mutex_{nullptr};
    
    // Metrics
    uint32_t total_reconnects_{0};
    uint32_t total_discoveries_{0};
    uint8_t missed_heartbeats_{0};
    uint32_t successful_sends_{0};
    uint32_t failed_sends_{0};
    uint32_t total_state_changes_{0};
    uint32_t total_connected_time_ms_{0};
    uint32_t last_connected_timestamp_{0};
    
    // State history (ring buffer, last 20 states)
    static constexpr size_t STATE_HISTORY_SIZE = 20;
    std::vector<StateHistoryEntry> state_history_;
    size_t state_history_index_{0};
    
    // Reconnection metrics
    struct ReconnectMetrics {
        uint32_t start_time_ms;
        uint32_t end_time_ms;
        bool successful;
        bool was_fast_reconnect;
    };
    std::vector<ReconnectMetrics> reconnect_history_; // Last 10 reconnects
    
    // Callbacks
    std::vector<StateChangeCallback> state_change_callbacks_;
    std::vector<ConnectionEventCallback> event_callbacks_;
    
    // Last log time (for throttling)
    uint32_t last_state_log_time_{0};
    
    // State transition validation
    bool is_valid_transition(EspNowConnectionState from, EspNowConnectionState to) const;
    void log_state_change(EspNowConnectionState from, EspNowConnectionState to);
    void record_state_history(EspNowConnectionState state, uint32_t duration_ms);
    void notify_state_change_callbacks(EspNowConnectionState old_state, EspNowConnectionState new_state);
    void notify_event_callbacks(const char* event, const char* details);
    
    // Helper methods
    void update_connected_time();
    void record_reconnect(bool successful, bool was_fast); beacon exchange)
                         ↓
                  VERSION_VERIFIED
                  Connection Manager Header

**File:** `src/espnow/espnow_connection_manager.h` (NEW)
- Define EspNowConnectionState enum
- Define EspNowConnectionManager class
- Add state transition validation
- Add subsystem coordination methods═══════╝
                         ↓
                  CONNECTED_ACTIVE ◄───────┐
                         ↓                 │ is_ready_to_send() = TRUE
                    [No activity]          │ Normal operations
                         ↓                 │
                  CONNECTED_IDLE           │
                         │                 │
                    [Activity] ────────────┘
                         │
                  [Missed heartbeats]
                         ↓
              ╔══════════════════════╗
              ║ DEGRADED PHASE       ║
              ╚══════════════════════╝
                         ↓
               CONNECTION_DEGRADED (1-2 missed)
                         ↓
               CONNECTION_MARGINAL (3-4 missed)
                         ↓
              ╔══════════════════════╗
              ║ RECOVERY PHASE       ║
              ╚══════════════════════╝
                         ↓
                   RECONNECTING
                    ↙        ↘
         [Channel known]    [Channel lost]
                ↓                  ↓
          FAST_RECONNECT    FULL_REDISCOVERY
                ↓                  ↓
          (Skip hopping)    (Return to CHANNEL_HOPPING)
```

---

### Receiver State Machine - Full State Transition Flow

**File:** `espnowreciever_2/lib/espnow_receiver/receiver_connection_manager.h`

```cpp
enum class ReceiverConnectionState : uint8_t {
    // === DISCONNECTED STATES ===
    DISCONNECTED,               // No connection, idle, listening for PROBE
    
    // === DISCOVERY RESPONSE STATES ===
    PROBE_RECEIVED,             // PROBE message detected from transmitter
    ACK_SENDING,                // Preparing ACK response with channel info
    ACK_SENT,                   // ACK sent, waiting for transmitter to lock channel
    TRANSMITTER_LOCKING,        // Transmitter is locking to our channel (passive wait)
    
    // === HANDSHAKE & AUTHENTICATION STATES ===
    HANDSHAKING,                // Exchanging version beacons with transmitter
    VERSION_VERIFIED,           // Version compatibility confirmed
    CONFIG_REQUESTED,           // Sent CONFIG_REQUEST_FULL to transmitter
    CONFIG_RECEIVING,           // Receiving configuration from transmitter
    
    // === CONNECTED STATES ===
    CONNECTED_ACTIVE,           // ✓ Fully operational, receiving data
    CONNECTED_IDLE,             // Connected but no recent data
    
    // === DEGRADED STATES ===
    CONNECTION_DEGRADED,        // Missed heartbeats from transmitter, warning
    CONNECTION_MARGINAL,        // Severe packet loss, near failure
    
    // === RECOVERY STATES ===
    TRANSMITTER_LOST            // Transmitter connection lost, return to DISCONNECTED
};
```

```
                    DISCONNECTED
                         ↓
                  [Listening on fixed channel X]
                         ↓
              ╔══════════════════════════╗
              ║ DISCOVERY RESPONSE PHASE ║
              ╚══════════════════════════╝
                         ↓
              [PROBE message received]
                         ↓
                  PROBE_RECEIVED
                         ↓
           [Prepare ACK with channel info]
                         ↓
                    ACK_SENDING
                         ↓
           [Send ACK to transmitter's MAC]
                         ↓
                     ACK_SENT
                         ↓
              [Wait for transmitter to lock]
              [Est. ~450ms for channel lock]
                         ↓
               TRANSMITTER_LOCKING ────────┐
                         ↓                 │ is_ready_to_send() = FALSE
        [First message from transmitter]   │ Queue requests until connected
                         ↓                 │
              ╔══════════════════════════╗ │
              ║ HANDSHAKE PHASE          ║ │
              ╚══════════════════════════╝ │
                         ↓                 │
                   HANDSHAKING             │
                         ↓                 │
              [Version beacon received]    │
                         ↓                 │
                  VERSION_VERIFIED         │
                         ↓                 │
          [Send CONFIG_REQUEST_FULL] ◄────┘
                         ↓
                  CONFIG_REQUESTED
                         ↓
          [Receiving config fragments]
                         ↓
                  CONFIG_RECEIVING
                         ↓
           [All config received & applied]
                         ↓
              ╔══════════════════════════╗
              ║ OPERATIONAL PHASE        ║
              ╚══════════════════════════╝
                         ↓
                  CONNECTED_ACTIVE ◄──────┐
                         ↓                │ is_ready_to_send() = TRUE
                  [No data for 10s]       │ Normal operations
                         ↓                │
                  CONNECTED_IDLE          │
                         │                │
                    [Data arrives] ───────┘
                         │
          [Missed heartbeats from TX]
                         ↓
              ╔══════════════════════════╗
              ║ DEGRADED PHASE           ║
              ╚══════════════════════════╝
                         ↓
               CONNECTION_DEGRADED (1-2 missed)
                         ↓
               CONNECTION_MARGINAL (3-4 missed)
                         ↓
              ╔══════════════════════════╗
              ║ RECOVERY PHASE           ║
              ╚══════════════════════════╝
                         ↓
                  TRANSMITTER_LOST
                         ↓
                   DISCONNECTED
                   (Return to listening)
```

---

## Shared Code Architecture

### Shared Base Class Implementation

**File:** `esp32common/espnow_common_utils/espnow_connection_base.h`

This base class provides common functionality for both transmitter and receiver connection managers:

```cpp
#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <esp_err.h>
#include <vector>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "espnow_timing_config.h"

// Base class shared by both transmitter and receiver
class EspNowConnectionBase {
public:
    virtual ~EspNowConnectionBase() {
        if (send_mutex_) {
            vSemaphoreDelete(send_mutex_);
        }
        if (state_mutex_) {
            vSemaphoreDelete(state_mutex_);
        }
    }
    
    // ========== COMMON STATE QUERIES ==========
    
    // Must be implemented by derived classes
    virtual bool is_ready_to_send() const = 0;
    virtual bool is_connected() const = 0;
    virtual const char* get_state_string() const = 0;
    
    // Common helper methods
    bool has_peer() const { return peer_registered_; }
    const uint8_t* get_peer_mac() const { return peer_mac_; }
    uint8_t get_current_channel() const { return current_channel_; }
    
    // ========== COMMON METRICS ==========
    
    uint32_t get_successful_sends() const { return successful_sends_; }
    uint32_t get_failed_sends() const { return failed_sends_; }
    uint32_t get_total_state_changes() const { return total_state_changes_; }
    uint32_t get_uptime_connected_ms() const;
    float get_send_success_rate() const;
    uint8_t get_connection_quality() const;  // 0-100%
    
    // State history (for debugging)
    struct StateHistoryEntry {
        uint8_t state;           // Generic state number
        const char* state_name;  // Human-readable name
        uint32_t timestamp_ms;
        uint32_t duration_ms;
    };
    const std::vector<StateHistoryEntry>& get_state_history() const;
    
    // ========== COMMON SAFE SEND ==========
    
    // Thread-safe send with state checking and retry logic
    esp_err_t safe_send(
        const uint8_t* peer_addr,
        const uint8_t* data,
        size_t len,
        const char* context = "DATA",
        uint8_t priority = 1  // 0=low, 1=normal, 2=high
    );
    
    // ========== COMMON EVENT CALLBACKS ==========
    
    using StateChangeCallback = std::function<void(uint8_t old_state, uint8_t new_state, const char* description)>;
    using ConnectionEventCallback = std::function<void(const char* event, const char* details)>;
    
    void register_state_change_callback(StateChangeCallback callback);
    void register_event_callback(ConnectionEventCallback callback);
    
    // ========== DIAGNOSTICS ==========
    
    String generate_diagnostic_report() const;
    void log_diagnostics() const;
    void reset_metrics();
    
protected:
    // Constructor (protected - only derived classes can create)
    EspNowConnectionBase() {
        send_mutex_ = xSemaphoreCreateMutex();
        state_mutex_ = xSemaphoreCreateMutex();
        if (!send_mutex_ || !state_mutex_) {
            // Critical error - mutexes required
            ESP_LOGE("ESPNOW_BASE", "Failed to create mutexes!");
        }
    }
    
    // Queue message - implemented by derived class to use its own queue
    virtual esp_err_t queue_message(
        const uint8_t* peer_addr,
        const uint8_t* data,
        size_t len,
        const char* context,
        uint8_t priority
    ) = 0;
    
    // Common state tracking helpers
    void record_state_change(uint8_t new_state, const char* state_name);
    void track_send_success();
    void track_send_failure();
    void update_connected_time();
    void notify_callbacks(uint8_t old_state, uint8_t new_state, const char* description);
    
    // Common member variables
    uint8_t peer_mac_[6]{0};
    uint8_t current_channel_{0};
    bool peer_registered_{false};
    
    uint32_t successful_sends_{0};
    uint32_t failed_sends_{0};
    uint32_t total_state_changes_{0};
    uint32_t total_connected_time_ms_{0};
    uint32_t last_connected_timestamp_{0};
    
    // State history (ring buffer, last 20 states)
    static constexpr size_t STATE_HISTORY_SIZE = 20;
    std::vector<StateHistoryEntry> state_history_;
    size_t state_history_index_{0};
    
    // Callbacks
    std::vector<StateChangeCallback> state_change_callbacks_;
    std::vector<ConnectionEventCallback> event_callbacks_;
    
    // Thread safety
    SemaphoreHandle_t send_mutex_{nullptr};
    SemaphoreHandle_t state_mutex_{nullptr};
};
```

---

### Transmitter-Specific Implementation

**File:** `ESPnowtransmitter2/src/espnow/transmitter_connection_manager.h`

The transmitter extends the base class with its 17-state machine:

```cpp
#pragma once

#include "espnow_common_utils/espnow_connection_base.h"
#include "espnow_common_utils/espnow_message_queue.h"
#include "espnow_common_utils/espnow_timing_config.h"

// Transmitter connection states (17 states)
enum class EspNowConnectionState : uint8_t {
    DISCONNECTED = 0,
    DISCOVERY_INIT,
    CHANNEL_HOPPING,
    ACK_RECEIVED,
    CHANNEL_TRANSITION,
    PEER_REGISTRATION,
    CHANNEL_STABILIZING,
    CHANNEL_LOCKED,
    HANDSHAKING,
    VERSION_VERIFIED,
    CONFIG_SYNCING,
    CONNECTED_ACTIVE,
    CONNECTED_IDLE,
    CONNECTION_DEGRADED,
    CONNECTION_MARGINAL,
    RECONNECTING,
    FAST_RECONNECT,
    FULL_REDISCOVERY
};

class TransmitterConnectionManager : public EspNowConnectionBase {
public:
    static TransmitterConnectionManager& instance();
    
    // ========== TRANSMITTER'S OWN DATA (not shared with receiver) ==========
private:
    EspNowMessageQueue message_queue_;  // Transmitter's OWN queue
    // All other data is in base class, but each instance is separate
    
public:
    // ========== STATE QUERIES (transmitter-specific) ==========
    
    bool is_ready_to_send() const override {
        return state_ >= EspNowConnectionState::CONNECTED_ACTIVE &&
               state_ <= EspNowConnectionState::CONNECTION_MARGINAL;
    }
    
    bool is_connected() const override {
        return state_ >= EspNowConnectionState::CONNECTED_ACTIVE &&
               state_ <= EspNowConnectionState::CONNECTED_IDLE;
    }
    
    // Transmitter-specific state queries
    bool is_channel_locking() const {
        return state_ >= EspNowConnectionState::CHANNEL_TRANSITION &&
               state_ <= EspNowConnectionState::CHANNEL_LOCKED;
    }
    
    bool is_discovering() const {
        return state_ >= EspNowConnectionState::DISCOVERY_INIT &&
               state_ <= EspNowConnectionState::ACK_RECEIVED;
    }
    
    bool is_degraded() const {
        return state_ >= EspNowConnectionState::CONNECTION_DEGRADED &&
               state_ <= EspNowConnectionState::CONNECTION_MARGINAL;
    }
    
    bool is_recovering() const {
        return state_ >= EspNowConnectionState::RECONNECTING &&
               state_ <= EspNowConnectionState::FULL_REDISCOVERY;
    }
    
    // Get current state
    EspNowConnectionState get_state() const { return state_; }
    const char* get_state_string() const override;
    
    // ========== TRANSMITTER STATE TRANSITIONS ==========
    
    bool set_state(EspNowConnectionState new_state);
    
    // Convenience state transitions
    void start_discovery();
    void start_channel_hopping();
    void ack_received();
    void begin_channel_lock();
    void channel_locked();
    void handshake_complete();
    void connection_established();
    void mark_active();
    void mark_idle();
    void degrade_connection(uint8_t severity);  // 1=degraded, 2=marginal
    void connection_lost();
    void start_reconnect(bool fast = false);
    
    // ========== TRANSMITTER-SPECIFIC OPERATIONS ==========
    
    // Wait until ready to send (with timeout)
    bool wait_until_ready(uint32_t timeout_ms);
    
    // Wait for specific state
    bool wait_for_state(EspNowConnectionState target, uint32_t timeout_ms);
    
    // Transmitter metrics
    uint32_t get_total_reconnects() const;
    uint32_t get_total_discoveries() const;
    uint32_t get_average_reconnect_time_ms() const;
    
    // Queue access (for diagnostics)
    size_t get_queue_size() const { return message_queue_.get_queue_size(); }
    
    // Subsystem coordination
    bool should_send_heartbeat() const;
    bool should_send_version_beacon() const;
    bool should_sync_config() const;
    bool should_transmit_data() const;
    bool should_accept_incoming() const;
    
protected:
    // Queue message using transmitter's own queue
    esp_err_t queue_message(
        const uint8_t* peer_addr,
        const uint8_t* data,
        size_t len,
        const char* context,
        uint8_t priority
    ) override {
        return message_queue_.enqueue(peer_addr, data, len, context, priority);
    }
    
private:
    volatile EspNowConnectionState state_{EspNowConnectionState::DISCONNECTED};
    uint32_t state_enter_time_{0};
    uint32_t total_reconnects_{0};
    uint32_t total_discoveries_{0};
    
    // State transition validation
    bool is_valid_transition(EspNowConnectionState from, EspNowConnectionState to) const;
    void log_state_change(EspNowConnectionState from, EspNowConnectionState to);
};
```

---

### Receiver-Specific Implementation

**File:** `espnowreciever_2/lib/espnow_receiver/receiver_connection_manager.h`

The receiver extends the base class with its simpler 10-state machine:

```cpp
#pragma once

#include "espnow_common_utils/espnow_connection_base.h"
#include "espnow_common_utils/espnow_message_queue.h"
#include "espnow_common_utils/espnow_timing_config.h"

// Receiver connection states (10 states)
enum class ReceiverConnectionState : uint8_t {
    DISCONNECTED = 0,
    PROBE_RECEIVED,
    ACK_SENDING,
    ACK_SENT,
    TRANSMITTER_LOCKING,
    HANDSHAKING,
    VERSION_VERIFIED,
    CONFIG_REQUESTED,
    CONFIG_RECEIVING,
    CONNECTED_ACTIVE,
    CONNECTED_IDLE,
    CONNECTION_DEGRADED,
    CONNECTION_MARGINAL,
    TRANSMITTER_LOST
};

class ReceiverConnectionManager : public EspNowConnectionBase {
public:
    static ReceiverConnectionManager& instance();
    
    // ========== RECEIVER'S OWN DATA (not shared with transmitter) ==========
private:
    EspNowMessageQueue message_queue_;  // Receiver's OWN queue
    // All other data is in base class, but each instance is separate
    
public:
    // ========== STATE QUERIES (receiver-specific) ==========
    
    bool is_ready_to_send() const override {
        // Receiver can send requests/responses when transmitter is connected
        return state_ >= ReceiverConnectionState::HANDSHAKING &&
               state_ <= ReceiverConnectionState::CONNECTION_MARGINAL;
    }
    
    bool is_connected() const override {
        return state_ >= ReceiverConnectionState::CONNECTED_ACTIVE &&
               state_ <= ReceiverConnectionState::CONNECTED_IDLE;
    }
    
    // Receiver-specific state queries
    bool is_waiting_for_transmitter() const {
        return state_ >= ReceiverConnectionState::ACK_SENT &&
               state_ <= ReceiverConnectionState::TRANSMITTER_LOCKING;
    }
    
    bool is_in_handshake() const {
        return state_ >= ReceiverConnectionState::HANDSHAKING &&
               state_ <= ReceiverConnectionState::CONFIG_RECEIVING;
    }
    
    bool is_degraded() const {
        return state_ >= ReceiverConnectionState::CONNECTION_DEGRADED &&
               state_ <= ReceiverConnectionState::CONNECTION_MARGINAL;
    }
    
    // Get current state
    ReceiverConnectionState get_state() const { return state_; }
    const char* get_state_string() const override;
    
    // ========== RECEIVER STATE TRANSITIONS ==========
    
    bool set_state(ReceiverConnectionState new_state);
    
    // Convenience state transitions
    void probe_received(const uint8_t* transmitter_mac);
    void ack_sent();
    void transmitter_message_received();  // First message after ACK
    void handshake_complete();
    void config_requested();
    void config_received();
    void connection_established();
    void mark_active();
    void mark_idle();
    void degrade_connection(uint8_t severity);  // 1=degraded, 2=marginal
    void transmitter_lost();
    
    // ========== RECEIVER-SPECIFIC OPERATIONS ==========
    
    // Wait for transmitter to complete channel lock
    bool wait_for_transmitter_ready(uint32_t timeout_ms);
    
    // Receiver metrics
    uint32_t get_total_connections() const;
    uint32_t get_total_probes_received() const;
    uint32_t get_average_connection_time_ms() const;
    
    // Queue access (for diagnostics)
    size_t get_queue_size() const { return message_queue_.get_queue_size(); }
    
    // Subsystem coordination
    bool should_send_ack() const;
    bool should_request_config() const;
    bool should_accept_data() const;
    bool should_update_display() const;
    
protected:
    // Queue message using receiver's own queue
    esp_err_t queue_message(
        const uint8_t* peer_addr,
        const uint8_t* data,
        size_t len,
        const char* context,
        uint8_t priority
    ) override {
        return message_queue_.enqueue(peer_addr, data, len, context, priority);
    }
    
private:
    volatile ReceiverConnectionState state_{ReceiverConnectionState::DISCONNECTED};
    uint32_t state_enter_time_{0};
    uint32_t total_connections_{0};
    uint32_t total_probes_received_{0};
    
    // State transition validation
    bool is_valid_transition(ReceiverConnectionState from, ReceiverConnectionState to) const;
    void log_state_change(ReceiverConnectionState from, ReceiverConnectionState to);
};
```

---

### Shared Support Components

These components are used by both transmitter and receiver:

#### 1. Safe Send Wrapper (SHARED)

**File:** `esp32common/espnow_common_utils/espnow_send_wrapper.cpp`

```cpp
esp_err_t EspNowConnectionBase::safe_send(
    const uint8_t* peer_addr,
    const uint8_t* data,
    size_t len,
    const char* context,
    uint8_t priority
) {
    // Shared logic for both transmitter and receiver
    
    // 1. Check if we're ready to send (device-specific)
    if (!is_ready_to_send()) {
        LOG_WARN("[ESPNOW] Cannot send %s - not ready (state: %s)", 
                 context, get_state_string());
        
        // Queue for later (THIS device's queue - not shared!)
        // Note: message_queue_ is owned by the derived class (transmitter or receiver)
        return queue_message(peer_addr, data, len, context, priority);
    }
    
    // 2. Acquire send mutex
    if (!send_mutex_) {
        LOG_ERROR("[ESPNOW] Send mutex not initialized!");
        return ESP_FAIL;
    }
    
    if (!xSemaphoreTake(send_mutex_, pdMS_TO_TICKS(1000))) {
        LOG_ERROR("[ESPNOW] Send mutex timeout for %s", context);
        return ESP_ERR_TIMEOUT;
    }
    
    // 3. Retry logic with exponential backoff
    esp_err_t result = ESP_FAIL;
    for (uint8_t attempt = 0; attempt < EspNowTiming::MAX_SEND_RETRIES; attempt++) {
        result = esp_now_send(peer_addr, data, len);
        
        if (result == ESP_OK) {
            track_send_success();
            xSemaphoreGive(send_mutex_);
            if (EspNowDebug::LOG_SEND_OPERATIONS) {
                LOG_DEBUG("[ESPNOW] ✓ Sent %s (attempt %d)", context, attempt + 1);
            }
            return ESP_OK;
        }
        
        // Retry with backoff
        if (attempt < EspNowTiming::MAX_SEND_RETRIES - 1) {
            uint32_t backoff = EspNowTiming::SEND_RETRY_DELAY_MS * (attempt + 1);
            delay(backoff);
            LOG_WARN("[ESPNOW] Retry %d for %s after %dms", 
                     attempt + 1, context, backoff);
        }
    }
    
    // All retries failed
    track_send_failure();
    xSemaphoreGive(send_mutex_);
    LOG_ERROR("[ESPNOW] Failed to send %s after %d attempts: %s", 
              context, EspNowTiming::MAX_SEND_RETRIES, esp_err_to_name(result));
    
    return result;
}
```

#### 2. Message Queue (SHARED CLASS - Each device has its own instance)

**File:** `esp32common/espnow_common_utils/espnow_message_queue.h`

```cpp
#pragma once

#include <Arduino.h>
#include <esp_err.h>
#include <vector>
#include <queue>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "espnow_timing_config.h"

// Queue CLASS definition (shared code)
// Each device creates its OWN instance - NO singleton!
class EspNowMessageQueue {
public:
    EspNowMessageQueue() {
        queue_mutex_ = xSemaphoreCreateMutex();
        if (!queue_mutex_) {
            ESP_LOGE("MSG_QUEUE", "Failed to create queue mutex!");
        }
    }
    
    ~EspNowMessageQueue() {
        clear();  // Clear any remaining messages
        if (queue_mutex_) {
            vSemaphoreDelete(queue_mutex_);
        }
    }
    
    struct QueuedMessage {
        uint8_t peer_addr[6];
        std::vector<uint8_t> data;
        const char* context;
        uint32_t queued_time;
        uint8_t priority;  // 0=low, 1=normal, 2=high
    };
    
    // Queue a message if connection not ready
    esp_err_t enqueue(const uint8_t* peer_addr, const uint8_t* data, 
                      size_t len, const char* context, uint8_t priority = 1);
    
    // Flush all queued messages (called when connection established)
    void flush(std::function<bool()> is_ready_check);
    
    // Clear all queued messages
    void clear();
    
    // Get queue statistics
    size_t get_queue_size() const;
    size_t get_queue_capacity() const;
    uint32_t get_total_queued() const;
    uint32_t get_total_flushed() const;
    uint32_t get_total_dropped() const;
    
private:
    std::queue<QueuedMessage> queue_;      // This device's queue ONLY
    size_t total_queued_{0};                // This device's counter
    size_t total_flushed_{0};               // This device's counter
    size_t total_dropped_{0};               // This device's counter
    SemaphoreHandle_t queue_mutex_{nullptr};  // This device's mutex
};

// Usage in device-specific code:
// Transmitter: has its own EspNowMessageQueue instance
// Receiver: has its own EspNowMessageQueue instance
// NO shared queue between devices!
```

#### 3. Diagnostics (SHARED)

**File:** `esp32common/espnow_common_utils/espnow_metrics.cpp`

```cpp
// Shared metrics implementation used by both devices
uint8_t EspNowConnectionBase::get_connection_quality() const {
    if (successful_sends_ + failed_sends_ == 0) return 100;
    
    // Calculate success rate
    float success_rate = (float)successful_sends_ / 
                         (successful_sends_ + failed_sends_) * 100.0f;
    
    // Penalize for consecutive failures
    if (failed_sends_ > 0) {
        uint8_t recent_failures = 0;
        // Check recent history...
        if (recent_failures >= 5) success_rate *= 0.5f;
        else if (recent_failures >= 3) success_rate *= 0.7f;
    }
    
    return (uint8_t)constrain(success_rate, 0.0f, 100.0f);
}

String EspNowConnectionBase::generate_diagnostic_report() const {
    String report;
    report += "=== ESP-NOW Connection Diagnostics ===\n";
    report += "Current State: " + String(get_state_string()) + "\n";
    report += "Peer Registered: " + String(peer_registered_ ? "Yes" : "No") + "\n";
    report += "Current Channel: " + String(current_channel_) + "\n";
    report += "Successful Sends: " + String(successful_sends_) + "\n";
    report += "Failed Sends: " + String(failed_sends_) + "\n";
    report += "Success Rate: " + String(get_send_success_rate(), 2) + "%\n";
    report += "Connection Quality: " + String(get_connection_quality()) + "%\n";
    report += "Total State Changes: " + String(total_state_changes_) + "\n";
    report += "Connected Time: " + String(get_uptime_connected_ms() / 1000) + "s\n";
    report += "\nRecent State History:\n";
    for (const auto& entry : state_history_) {
        report += "  " + String(entry.state_name) + " (" + 
                  String(entry.duration_ms) + "ms)\n";
    }
    return report;
}
```

---

## Code Integration Examples

---

## Required Code Changes

### Change 1: Add Timing Configuration Header

**File:** `src/espnow/espnow_timing_config.h` (NEW)

```cpp
#pragma once

#include <Arduino.h>
#include <esp_log.h>

// ========================================================
// LOGGING MACROS (if not already defined in project)
// ========================================================
#ifndef LOG_DEBUG
    #define LOG_DEBUG(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
#endif
#ifndef LOG_INFO
    #define LOG_INFO(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#endif
#ifndef LOG_WARN
    #define LOG_WARN(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
#endif
#ifndef LOG_ERROR
    #define LOG_ERROR(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#endif

// ========================================================
// ESP-NOW Connection Timing Configuration
// ========================================================
// These values control the timing of channel locking and
// connection establishment. They may be exposed in the
// transmitter settings UI for runtime adjustment.
// ========================================================

namespace EspNowTiming {
    // ====================================
    // CHANNEL LOCKING TIMING
    // ====================================
    constexpr uint32_t CHANNEL_TRANSITION_DELAY_MS = 50;   // WiFi hardware channel switching
    constexpr uint32_t PEER_REGISTRATION_DELAY_MS = 100;   // ESP-NOW driver peer table update
    constexpr uint32_t CHANNEL_STABILIZING_DELAY_MS = 300; // Safety margin for busy WiFi environments
    
    // Calculated total (for logging/diagnostics)
    constexpr uint32_t TOTAL_CHANNEL_LOCK_TIME_MS = 
        CHANNEL_TRANSITION_DELAY_MS + 
        PEER_REGISTRATION_DELAY_MS + 
        CHANNEL_STABILIZING_DELAY_MS;  // = 450ms
    
    // ====================================
    // DISCOVERY TIMING
    // ====================================
    constexpr uint32_t PROBE_BROADCAST_INTERVAL_MS = 100;   // Time between PROBE broadcasts
    constexpr uint32_t ACK_WAIT_TIMEOUT_MS = 500;           // Wait for ACK on each channel
    constexpr uint32_t DISCOVERY_TOTAL_TIMEOUT_MS = 30000;  // Total discovery timeout (30s)
    constexpr uint8_t MAX_DISCOVERY_RETRIES = 3;            // Max full discovery attempts before giving up
    constexpr uint8_t DISCOVERY_START_CHANNEL = 1;          // First WiFi channel to scan
    constexpr uint8_t DISCOVERY_END_CHANNEL = 14;           // Last WiFi channel to scan (US/EU: 1-13, JP: 1-14)
    
    // ====================================
    // CONNECTION QUALITY TIMING
    // ====================================
    constexpr uint32_t HEARTBEAT_INTERVAL_MS = 5000;            // Heartbeat send interval (5s)
    constexpr uint32_t HEARTBEAT_WARNING_TIMEOUT_MS = 10000;    // 1 missed = warning (10s)
    constexpr uint32_t HEARTBEAT_DEGRADED_TIMEOUT_MS = 20000;   // 2 missed = degraded (20s)
    constexpr uint32_t HEARTBEAT_MARGINAL_TIMEOUT_MS = 30000;   // 3 missed = marginal (30s)
    constexpr uint32_t HEARTBEAT_CRITICAL_TIMEOUT_MS = 40000;   // 4 missed = lost (40s)
    
    // Heartbeat thresholds (for UI display)
    constexpr uint8_t HEARTBEAT_MISSED_WARNING = 1;     // Warning level
    constexpr uint8_t HEARTBEAT_MISSED_DEGRADED = 2;    // Degraded level
    constexpr uint8_t HEARTBEAT_MISSED_MARGINAL = 3;    // Marginal level
    constexpr uint8_t HEARTBEAT_MISSED_CRITICAL = 4;    // Critical/lost level
    
    // ====================================
    // RECONNECTION TIMING
    // ====================================
    constexpr uint32_t FAST_RECONNECT_THRESHOLD_MS = 10000; // Try fast reconnect if < 10s since disconnect
    constexpr uint32_t RECONNECT_RETRY_DELAY_MS = 2000;     // Delay between reconnect attempts (2s)
    constexpr uint8_t MAX_FAST_RECONNECT_ATTEMPTS = 3;      // Max fast reconnect tries before full rediscovery
    constexpr uint32_t RECONNECT_BACKOFF_MULTIPLIER = 2;    // Exponential backoff multiplier
    constexpr uint32_t MAX_RECONNECT_DELAY_MS = 30000;      // Max backoff delay (30s)
    
    // ====================================
    // MESSAGE QUEUE CONFIGURATION
    // ====================================
    constexpr size_t MESSAGE_QUEUE_MAX_SIZE = 20;                    // Max queued messages
    constexpr uint32_t QUEUED_MESSAGE_STALE_TIMEOUT_MS = 30000;      // Drop queued msgs older than 30s
    constexpr uint32_t QUEUE_FLUSH_INTER_MESSAGE_DELAY_MS = 10;      // Delay between sends when flushing (10ms)
    constexpr size_t PRIORITY_QUEUE_RESERVED_SLOTS = 5;              // Reserved slots for high-priority messages
    
    // ====================================
    // CHANNEL VERIFICATION
    // ====================================
    constexpr uint8_t CHANNEL_VERIFY_MAX_ATTEMPTS = 10;      // Max attempts to verify channel
    constexpr uint32_t CHANNEL_VERIFY_RETRY_DELAY_MS = 50;   // Delay between verification attempts
    constexpr uint32_t CHANNEL_VERIFY_TOTAL_TIMEOUT_MS = 
        CHANNEL_VERIFY_MAX_ATTEMPTS * CHANNEL_VERIFY_RETRY_DELAY_MS; // = 500ms
    
    // ====================================
    // RETRY LIMITS & ERROR HANDLING
    // ====================================
    constexpr uint8_t MAX_SEND_RETRIES = 3;                  // Max retries for failed sends
    constexpr uint32_t SEND_RETRY_DELAY_MS = 50;             // Delay between send retries
    constexpr uint8_t CONSECUTIVE_FAILURE_THRESHOLD = 5;     // Failures before connection marked bad
    
    // ====================================
    // STATE TRANSITION TIMEOUTS
    // ====================================
    constexpr uint32_t HANDSHAKE_TIMEOUT_MS = 5000;          // Max time for handshake phase
    constexpr uint32_t CONFIG_SYNC_TIMEOUT_MS = 10000;       // Max time for config synchronization
    constexpr uint32_t STATE_TRANSITION_MAX_TIME_MS = 2000;  // Max time for any state transition
    
    // ====================================
    // LOGGING & DIAGNOSTICS
    // ====================================
    constexpr uint32_t STATE_CHANGE_LOG_THROTTLE_MS = 1000;  // Min time between duplicate state logs
    constexpr uint32_t METRICS_UPDATE_INTERVAL_MS = 10000;   // How often to update connection metrics (10s)
    constexpr uint32_t TELEMETRY_REPORT_INTERVAL_MS = 60000; // How often to report telemetry (1min)
}

// ========================================================
// Future Enhancement: Runtime Configuration
// ========================================================
// These values can be exposed in TransmitterSettings for
// runtime adjustment via web UI without recompilation.
//
// Recommended implementation:
// 1. Add "Advanced ESP-NOW Settings" section in web UI
// 2. Store values in NVS (non-volatile storage)
// 3. Load on boot, fall back to defaults if not set
// 4. Validate ranges (min/max) before applying
// 5. Provide "Reset to Defaults" button
//
// Example settings structure:
//
// struct EspNowTimingSettings {
//     // Channel locking
//     uint32_t channel_transition_ms = 50;      // Range: 20-200ms
//     uint32_t peer_registration_ms = 100;      // Range: 50-500ms
//     uint32_t channel_stabilizing_ms = 300;    // Range: 100-1000ms
//     
//     // Heartbeat
//     uint32_t heartbeat_interval_ms = 5000;    // Range: 1000-30000ms
//     uint32_t heartbeat_critical_ms = 40000;   // Range: 10000-120000ms
//     
//     // Discovery
//     uint8_t max_discovery_retries = 3;        // Range: 1-10
//     uint32_t discovery_timeout_ms = 30000;    // Range: 10000-120000ms
//     
//     // Message queue
//     size_t message_queue_max_size = 20;       // Range: 5-100
//     uint32_t stale_message_timeout_ms = 30000; // Range: 5000-300000ms
//     
//     // Methods
//     bool validate() const;     // Validate all ranges
//     void save_to_nvs();        // Persist to NVS
//     void load_from_nvs();      // Load from NVS
//     void reset_to_defaults();  // Reset all values
//     
//     // Getters with fallback to compile-time defaults
//     uint32_t get_channel_transition_delay() const {
//         return (channel_transition_ms >= 20 && channel_transition_ms <= 200)
//                ? channel_transition_ms 
//                : EspNowTiming::CHANNEL_TRANSITION_DELAY_MS;
//     }
//     // ... similar getters for all settings
// };
//
// Usage in code:
// delay(EspNowTimingSettings::instance().get_channel_transition_delay());
//
// Web UI JSON API:
// GET  /api/espnow/timing - Get current timing settings
// POST /api/espnow/timing - Update timing settings (with validation)
// POST /api/espnow/timing/reset - Reset to defaults
// ========================================================

} // namespace EspNowTiming

// ========================================================
// DEBUG & DIAGNOSTIC CONFIGURATION
// ========================================================

namespace EspNowDebug {
    // Enable/disable detailed logging for each subsystem
    constexpr bool LOG_STATE_TRANSITIONS = true;     // Log every state change
    constexpr bool LOG_CHANNEL_OPERATIONS = true;    // Log channel changes
    constexpr bool LOG_SEND_OPERATIONS = false;      // Log every send (verbose!)
    constexpr bool LOG_QUEUE_OPERATIONS = true;      // Log queue add/flush
    constexpr bool LOG_HEARTBEAT = false;            // Log heartbeat sends (verbose!)
    constexpr bool LOG_METRICS = true;               // Log metrics updates
    
    // Performance tracking
    constexpr bool TRACK_STATE_TIMING = true;        // Track time in each state
    constexpr bool TRACK_SEND_SUCCESS_RATE = true;   // Track send success/failure
    constexpr bool TRACK_RECONNECT_STATS = true;     // Track reconnection attempts
    
    // Assertions (disable in production for performance)
    constexpr bool ENABLE_STATE_VALIDATION = true;   // Validate state transitions
    constexpr bool ENABLE_PARAMETER_CHECKS = true;   // Validate function parameters
    
    // Test/diagnostic modes
    constexpr bool SIMULATE_CHANNEL_ERRORS = false;  // For testing error handling
    constexpr bool FORCE_SLOW_DISCOVERY = false;     // Add delays for debugging
    constexpr uint32_t ARTIFICIAL_DELAY_MS = 0;      // Extra delay if FORCE_SLOW_DISCOVERY
}

#endif // ESPNOW_TIMING_CONFIG_H
```

### Change 2: Add Connection Manager Header

**File:** `src/espnow/espnow_connection_manager.h` (NEW)
- Define EspNowConnectionState enum
- Define EspNowConnectionManager class
- Add state transition validation
- Add subsystem coordination methods
- Include espnow_timing_config.h

### Change 3: Modify Discovery Task with Full State Progression

**File:** `discovery_task.cpp`

```cpp
#include "espnow/espnow_timing_config.h"  // Timing constants

void DiscoveryTask::active_channel_hopping_task(void* pvParameters) {
    auto* self = static_cast<DiscoveryTask*>(pvParameters);
    auto& conn_mgr = EspNowConnectionManager::instance();
    
    while (true) {
        // Wait for discovery signal
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        // STATE: Start discovery
        conn_mgr.start_discovery();
        LOG_INFO("[DISCOVERY] Starting discovery process");
        
        // STATE: Begin channel hopping
        conn_mgr.start_channel_hopping();
        
        uint8_t discovered_channel = 0;
        if (self->active_channel_hop_scan(&discovered_channel)) {
            // Channel hopping succeeded, ACK received
            // Now begin critical channel locking phase
            
            conn_mgr.begin_channel_lock();
            LOG_INFO("[DISCOVERY] Beginning channel lock sequence (est. %dms)", 
                     EspNowTiming::TOTAL_CHANNEL_LOCK_TIME_MS);
            
            // Lock the channel
            if (self->force_and_verify_channel(discovered_channel)) {
                conn_mgr.channel_locked();
                LOG_INFO("[DISCOVERY] ✓ Channel locked: %d", discovered_channel);
                
                // STATE: Begin handshake
                // Send version beacon
                if (VersionBeaconManager::instance().send_version_beacon()) {
                    conn_mgr.handshake_complete();
                    
                    // STATE: Sync initial config
                    // (This happens when receiver requests it)
                    
                    // STATE: Connection fully established!
                    conn_mgr.connection_established();
                    LOG_INFO("[DISCOVERY] ✓ ESP-NOW connection FULLY ESTABLISHED");
                } else {
                    LOG_ERROR("[DISCOVERY] Version beacon failed");
                    conn_mgr.connection_lost();
                }
            } else { with State-Aware Logic

**Create a safe send wrapper:**

```cpp
// File: espnow_connection_manager.cpp

esp_err_t EspNowConnectionManager::safe_send(
    const uint8_t* peer_addr,
    const uint8_t* data,
    size_t len,
    const char* context
) {
    // Check if we're in a state that allows sending
    if (!is_ready_to_send()) {
        LOG_WARN("[ESPNOW] Cannot send %s - state: %s", 
                 context, get_state_string());
        
        // Different behavior based on state
        if (is_channel_locking()) {
            LOG_DEBUG("[ESPNOW] Queuing %s until channel locked", context);
            return queue_for_later(peer_addr, data, len, context);
        } else if (is_discovering()) {
            LOG_WARN("[ESPNOW] Dropping %s - still discovering", context);
            return ESP_ERR_INVALID_STATE;
        } else if (is_degraded()) {
            ### Change 5: Integrate with Watchdog and Heartbeat

**File:** `keep_alive_manager.cpp`

```cpp
#include "espnow/espnow_timing_config.h"

void KeepAliveManager::watchdog_task(void* pvParameters) {
    auto& conn_mgr = EspNowConnectionManager::instance();
    
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        if (conn_mgr.is_connected()) {
            auto time_since_last = get_time_since_last_message();
            
            if (time_since_last > EspNowTiming::HEARTBEAT_CRITICAL_TIMEOUT_MS) {
                LOG_ERROR("[WATCHDOG] Connection lost!");
                conn_mgr.connection_lost();
                trigger_discovery();
                
            } else if (time_since_last > EspNowTiming::HEARTBEAT_MARGINAL_TIMEOUT_MS) {
                LOG_WARN("[WATCHDOG] Connection MARGINAL");
                conn_mgr.degrade_connection(2);  // Marginal
                
            } else if (time_since_last > EspNowTiming::HEARTBEAT_DEGRADED_TIMEOUT_MS) {
                LOG_WARN("[WATCHDOG] Connection degraded");
                conn_mgr.degrade_connection(1);  // Degraded
                
            } else {
                // Connection good - mark as active if we're receiving data
                if (time_since_last < EspNowTiming::HEARTBEAT_INTERVAL_MS) {
                    conn_mgr.mark_active();
                } else {
                    conn_mgr.mark_idle();
                }
            }
        }
    }
}

bool KeepAliveManager::send_heartbeat() {
    auto& conn_mgr = EspNowConnectionManager::instance();
    
    // Only send heartbeat in appropriate states
    if (!conn_mgr.should_send_heartbeat()) {
        LOG_DEBUG("[HEARTBEAT] Skipping - state: %s", conn_mgr.get_state_string());
        return false;
    }
    
    // Use safe send
    return conn_mgr.safe_send(receiver_mac, heartbeat_data, len, "HEARTBEAT") == ESP_OK;
}
```

**File:** `discovery_task.cpp`

```cpp
void DiscoveryTask::restart() {
    auto& conn_mgr = EspNowConnectionManager::instance();
    
    // Determine reconnect strategy
    if (conn_mgr.is_degraded() && last_channel_known_) {
        // Fast reconnect - channel might still be valid
        conn_mgr.start_reconnect(true);  // fast=true
        LOG_INFO("[DISCOVERY] Starting FAST reconnect on channel %d", last_channel_);
    } else {
        // Full rediscovery needed
        conn_mgr.start_reconnect(false);  // full rediscovery
        LOG_INFO("[DISCOVERY] Starting FULL rediscovery");
    }
    
    // ... existing cleanup code ...
}
```

---

### Receiver Code Changes

The receiver requires fewer changes due to its simpler passive role:

#### Change 1: Receiver ACK Response Handler

**File:** `espnowreciever_2/lib/espnow_receiver/ack_handler.cpp` (NEW)

```cpp
#include "espnow/receiver_connection_manager.h"
#include "espnow_common_utils/espnow_timing_config.h"

void EspNowReceiver::handle_probe_message(const uint8_t* sender_mac, const uint8_t* data, size_t len) {
    auto& conn_mgr = ReceiverConnectionManager::instance();
    
    // STATE: PROBE received
    conn_mgr.probe_received(sender_mac);
    LOG_INFO("[RECEIVER] PROBE received from transmitter");
    
    // STATE: Prepare ACK (include our channel)
    conn_mgr.set_state(ReceiverConnectionState::ACK_SENDING);
    
    // Send ACK with our channel number
    uint8_t ack_data[10];
    ack_data[0] = MSG_TYPE_ACK;
    ack_data[1] = WiFi.channel();  // Our fixed channel
    
    esp_err_t result = esp_now_send(sender_mac, ack_data, sizeof(ack_data));
    
    if (result == ESP_OK) {
        // STATE: ACK sent successfully
        conn_mgr.ack_sent();
        LOG_INFO("[RECEIVER] ACK sent, channel %d", WiFi.channel());
        
        // STATE: Now waiting for transmitter to lock channel
        // This takes ~450ms - receiver MUST NOT send anything yet!
        conn_mgr.set_state(ReceiverConnectionState::TRANSMITTER_LOCKING);
        LOG_INFO("[RECEIVER] Waiting for transmitter to complete channel lock (~%dms)", 
                 EspNowTiming::TOTAL_CHANNEL_LOCK_TIME_MS);
        
        // NOTE: We do NOT send CONFIG_REQUEST_FULL here!
        // We wait for the first message from transmitter to confirm ready.
        
    } else {
        LOG_ERROR("[RECEIVER] Failed to send ACK: %s", esp_err_to_name(result));
        conn_mgr.set_state(ReceiverConnectionState::DISCONNECTED);
    }
}
```

#### Change 2: Receiver Message Handler

**File:** `espnowreciever_2/lib/espnow_receiver/message_handler.cpp`

```cpp
void EspNowReceiver::handle_incoming_message(const uint8_t* sender_mac, const uint8_t* data, size_t len) {
    auto& conn_mgr = ReceiverConnectionManager::instance();
    
    // First message after ACK = transmitter is ready!
    if (conn_mgr.get_state() == ReceiverConnectionState::TRANSMITTER_LOCKING) {
        conn_mgr.transmitter_message_received();
        LOG_INFO("[RECEIVER] First message received - transmitter ready!");
        
        // Now we can transition to handshaking
        conn_mgr.set_state(ReceiverConnectionState::HANDSHAKING);
    }
    
    // Process message based on type
    uint8_t msg_type = data[0];
    
    switch (msg_type) {
        case MSG_TYPE_VERSION_BEACON:
            handle_version_beacon(sender_mac, data, len);
            if (verify_version_compatible()) {
                conn_mgr.handshake_complete();
                
                // Now we can safely request config
                conn_mgr.config_requested();
                send_config_request();  // Uses safe_send internally
            }
            break;
            
        case MSG_TYPE_CONFIG_RESPONSE:
            handle_config_response(sender_mac, data, len);
            conn_mgr.config_received();
            conn_mgr.connection_established();
            LOG_INFO("[RECEIVER] ✓ ESP-NOW connection FULLY ESTABLISHED");
            break;
            
        case MSG_TYPE_HEARTBEAT:
            // Mark connection as active
            conn_mgr.mark_active();
            last_heartbeat_time_ = millis();
            break;
            
        case MSG_TYPE_DATA:
            if (conn_mgr.is_connected()) {
                conn_mgr.mark_active();
                process_data_message(data, len);
            }
            break;
    }
}
```

#### Change 3: Receiver Safe Send Integration

**File:** `espnowreciever_2/lib/espnow_receiver/espnow_receiver.cpp`

```cpp
bool EspNowReceiver::send_config_request() {
    auto& conn_mgr = ReceiverConnectionManager::instance();
    
    // Check if we're ready to send (CRITICAL!)
    if (!conn_mgr.should_request_config()) {
        LOG_WARN("[RECEIVER] Cannot request config - state: %s", 
                 conn_mgr.get_state_string());
        return false;
    }
    
    // Build config request
    uint8_t request_data[2];
    request_data[0] = MSG_TYPE_CONFIG_REQUEST_FULL;
    request_data[1] = 0;  // Flags
    
    // Use safe send from base class
    esp_err_t result = conn_mgr.safe_send(
        transmitter_mac_,
        request_data,
        sizeof(request_data),
        "CONFIG_REQUEST"
    );
    
    return result == ESP_OK;
}

bool EspNowReceiver::send_display_update() {
    auto& conn_mgr = ReceiverConnectionManager::instance();
    
    // Only send updates when connected
    if (!conn_mgr.should_update_display()) {
        return false;
    }
    
    // ... build display data ...
    
    return conn_mgr.safe_send(
        transmitter_mac_,
        display_data,
        sizeof(display_data),
        "DISPLAY_UPDATE"
    ) == ESP_OK;
}
```

#### Change 4: Receiver Watchdog Integration

**File:** `espnowreciever_2/lib/espnow_receiver/connection_watchdog.cpp`

```cpp
void ReceiverConnectionWatchdog::monitor_task(void* pvParameters) {
    auto& conn_mgr = ReceiverConnectionManager::instance();
    
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        if (conn_mgr.is_connected()) {
            auto time_since_last = millis() - last_message_time_;
            
            if (time_since_last > EspNowTiming::HEARTBEAT_CRITICAL_TIMEOUT_MS) {
                LOG_ERROR("[RECEIVER] Transmitter LOST!");
                conn_mgr.transmitter_lost();
                
            } else if (time_since_last > EspNowTiming::HEARTBEAT_MARGINAL_TIMEOUT_MS) {
                LOG_WARN("[RECEIVER] Connection MARGINAL");
                conn_mgr.degrade_connection(2);  // Marginal
                
            } else if (time_since_last > EspNowTiming::HEARTBEAT_DEGRADED_TIMEOUT_MS) {
                LOG_WARN("[RECEIVER] Connection degraded");
                conn_mgr.degrade_connection(1);  // Degraded
                
            } else {
                // Connection good
                if (time_since_last < EspNowTiming::HEARTBEAT_INTERVAL_MS) {
                    conn_mgr.mark_active();
                } else {
                    conn_mgr.mark_idle();
                }
            }
        }
    }
}
```

#### Change 5: Receiver Web UI Integration

**File:** `espnowreciever_2/lib/webserver/api_handlers.cpp`

```cpp
void APIHandlers::handle_espnow_status(AsyncWebServerRequest* request) {
    auto& conn_mgr = ReceiverConnectionManager::instance();
    
    DynamicJsonDocument doc(1024);
    
    // State information
    doc["state"] = conn_mgr.get_state_string();
    doc["connected"] = conn_mgr.is_connected();
    doc["ready_to_send"] = conn_mgr.is_ready_to_send();
    doc["channel"] = WiFi.channel();
    
    // Transmitter information
    if (conn_mgr.has_peer()) {
        char mac_str[18];
        const uint8_t* mac = conn_mgr.get_peer_mac();
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        doc["transmitter_mac"] = mac_str;
    }
    
    // Metrics
    doc["successful_sends"] = conn_mgr.get_successful_sends();
    doc["failed_sends"] = conn_mgr.get_failed_sends();
    doc["success_rate"] = conn_mgr.get_send_success_rate();
    doc["connection_quality"] = conn_mgr.get_connection_quality();
    doc["uptime_connected_ms"] = conn_mgr.get_uptime_connected_ms();
    
    // Connection statistics
    doc["total_connections"] = conn_mgr.get_total_connections();
    doc["total_probes"] = conn_mgr.get_total_probes_received();
    doc["avg_connection_time_ms"] = conn_mgr.get_average_connection_time_ms();
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void APIHandlers::handle_espnow_diagnostics(AsyncWebServerRequest* request) {
    auto& conn_mgr = ReceiverConnectionManager::instance();
    
    String report = conn_mgr.generate_diagnostic_report();
    
    request->send(200, "text/plain", report);
}
```

---

## Web UI Integration Requirements

### Required API Endpoints & URI Handlers

Both transmitter and receiver need these endpoints for full ESP-NOW status visibility:

#### Transmitter API Endpoints

**File:** `ESPnowtransmitter2/src/api_handlers.cpp` (or equivalent)

1. **GET `/api/espnow/status`** - Real-time connection status
   ```cpp
   void handle_espnow_status(AsyncWebServerRequest* request) {
       auto& conn_mgr = TransmitterConnectionManager::instance();
       DynamicJsonDocument doc(2048);
       
       // State
       doc["state"] = conn_mgr.get_state_string();
       doc["state_code"] = static_cast<uint8_t>(conn_mgr.get_state());
       doc["connected"] = conn_mgr.is_connected();
       doc["ready_to_send"] = conn_mgr.is_ready_to_send();
       doc["is_discovering"] = conn_mgr.is_discovering();
       doc["is_channel_locking"] = conn_mgr.is_channel_locking();
       doc["is_degraded"] = conn_mgr.is_degraded();
       
       // Peer info
       doc["has_peer"] = conn_mgr.has_peer();
       doc["current_channel"] = conn_mgr.get_current_channel();
       if (conn_mgr.has_peer()) {
           char mac[18];
           const uint8_t* peer = conn_mgr.get_peer_mac();
           snprintf(mac, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                    peer[0], peer[1], peer[2], peer[3], peer[4], peer[5]);
           doc["peer_mac"] = mac;
       }
       
       // Metrics
       doc["successful_sends"] = conn_mgr.get_successful_sends();
       doc["failed_sends"] = conn_mgr.get_failed_sends();
       doc["success_rate"] = conn_mgr.get_send_success_rate();
       doc["connection_quality"] = conn_mgr.get_connection_quality();
       doc["total_state_changes"] = conn_mgr.get_total_state_changes();
       doc["uptime_connected_ms"] = conn_mgr.get_uptime_connected_ms();
       
       // Transmitter-specific
       doc["total_reconnects"] = conn_mgr.get_total_reconnects();
       doc["total_discoveries"] = conn_mgr.get_total_discoveries();
       doc["avg_reconnect_time_ms"] = conn_mgr.get_average_reconnect_time_ms();
       doc["queue_size"] = conn_mgr.get_queue_size();
       
       String response;
       serializeJson(doc, response);
       request->send(200, "application/json", response);
   }
   ```

2. **GET `/api/espnow/diagnostics`** - Detailed diagnostic report (shown above)

3. **GET `/api/espnow/history`** - State change history
   ```cpp
   void handle_espnow_history(AsyncWebServerRequest* request) {
       auto& conn_mgr = TransmitterConnectionManager::instance();
       const auto& history = conn_mgr.get_state_history();
       
       DynamicJsonDocument doc(4096);
       JsonArray states = doc.createNestedArray("history");
       
       for (const auto& entry : history) {
           JsonObject state = states.createNestedObject();
           state["state_name"] = entry.state_name;
           state["state_code"] = entry.state;
           state["timestamp_ms"] = entry.timestamp_ms;
           state["duration_ms"] = entry.duration_ms;
       }
       
       String response;
       serializeJson(doc, response);
       request->send(200, "application/json", response);
   }
   ```

4. **POST `/api/espnow/reset_metrics`** - Reset counters
   ```cpp
   void handle_espnow_reset_metrics(AsyncWebServerRequest* request) {
       auto& conn_mgr = TransmitterConnectionManager::instance();
       conn_mgr.reset_metrics();
       request->send(200, "text/plain", "Metrics reset");
   }
   ```

5. **GET `/api/espnow/timing`** - Get timing configuration (optional)
   ```cpp
   void handle_espnow_timing_get(AsyncWebServerRequest* request) {
       DynamicJsonDocument doc(2048);
       
       // Channel locking
       doc["channel_transition_ms"] = EspNowTiming::CHANNEL_TRANSITION_DELAY_MS;
       doc["peer_registration_ms"] = EspNowTiming::PEER_REGISTRATION_DELAY_MS;
       doc["channel_stabilizing_ms"] = EspNowTiming::CHANNEL_STABILIZING_DELAY_MS;
       doc["total_lock_time_ms"] = EspNowTiming::TOTAL_CHANNEL_LOCK_TIME_MS;
       
       // Heartbeat
       doc["heartbeat_interval_ms"] = EspNowTiming::HEARTBEAT_INTERVAL_MS;
       doc["heartbeat_critical_ms"] = EspNowTiming::HEARTBEAT_CRITICAL_TIMEOUT_MS;
       
       // Discovery
       doc["probe_interval_ms"] = EspNowTiming::PROBE_BROADCAST_INTERVAL_MS;
       doc["ack_timeout_ms"] = EspNowTiming::ACK_WAIT_TIMEOUT_MS;
       doc["discovery_timeout_ms"] = EspNowTiming::DISCOVERY_TOTAL_TIMEOUT_MS;
       
       String response;
       serializeJson(doc, response);
       request->send(200, "application/json", response);
   }
   ```

#### Receiver API Endpoints

Receiver needs the same endpoints, using `ReceiverConnectionManager::instance()` instead.

### URI Handler Registration

**Both devices must register handlers in their web server setup:**

#### Transmitter Registration
```cpp
// In transmitter web server setup (main.cpp or webserver init)
void setup_espnow_handlers() {
    server.on("/api/espnow/status", HTTP_GET, handle_espnow_status);
    server.on("/api/espnow/diagnostics", HTTP_GET, handle_espnow_diagnostics);
    server.on("/api/espnow/history", HTTP_GET, handle_espnow_history);
    server.on("/api/espnow/reset_metrics", HTTP_POST, handle_espnow_reset_metrics);
    server.on("/api/espnow/timing", HTTP_GET, handle_espnow_timing_get);
}
```

#### Receiver Registration
```cpp
// In receiver web server setup (webserver.cpp)
void setup_espnow_handlers() {
    server.on("/api/espnow/status", HTTP_GET, handle_espnow_status);
    server.on("/api/espnow/diagnostics", HTTP_GET, handle_espnow_diagnostics);
    server.on("/api/espnow/history", HTTP_GET, handle_espnow_history);
    server.on("/api/espnow/reset_metrics", HTTP_POST, handle_espnow_reset_metrics);
    server.on("/api/espnow/timing", HTTP_GET, handle_espnow_timing_get);
}
```

### Web UI Dashboard Widget

Both devices need a dashboard widget to display ESP-NOW status:

#### HTML/JavaScript Widget

```html
<!-- ESP-NOW Status Widget (add to dashboard) -->
<div class="card espnow-widget">
    <h3>ESP-NOW Connection</h3>
    <div id="espnow-status">
        <div class="status-row">
            <span class="label">State:</span>
            <span id="espnow-state" class="value">-</span>
        </div>
        <div class="status-row">
            <span class="label">Connection:</span>
            <span id="espnow-connected" class="value">-</span>
        </div>
        <div class="status-row">
            <span class="label">Quality:</span>
            <div class="quality-bar">
                <div id="espnow-quality-fill" class="quality-fill"></div>
                <span id="espnow-quality-text">-</span>
            </div>
        </div>
        <div class="status-row">
            <span class="label">Peer MAC:</span>
            <span id="espnow-peer-mac" class="value mono">-</span>
        </div>
        <div class="status-row">
            <span class="label">Channel:</span>
            <span id="espnow-channel" class="value">-</span>
        </div>
        <div class="status-row">
            <span class="label">Success Rate:</span>
            <span id="espnow-success-rate" class="value">-</span>
        </div>
        <div class="status-row">
            <span class="label">Queue:</span>
            <span id="espnow-queue" class="value">-</span>
        </div>
    </div>
    <button onclick="viewESPNowDetails()">View Details</button>
    <button onclick="resetESPNowMetrics()">Reset Metrics</button>
</div>

<script>
// Fetch and update ESP-NOW status
async function updateESPNowStatus() {
    try {
        const response = await fetch('/api/espnow/status');
        const data = await response.json();
        
        // Update state
        document.getElementById('espnow-state').textContent = data.state || 'Unknown';
        document.getElementById('espnow-state').className = 
            data.connected ? 'value status-good' : 'value status-warn';
        
        // Update connection
        document.getElementById('espnow-connected').textContent = 
            data.connected ? 'Connected' : 'Disconnected';
        document.getElementById('espnow-connected').className = 
            data.connected ? 'value status-good' : 'value status-bad';
        
        // Update quality bar
        const quality = data.connection_quality || 0;
        const qualityFill = document.getElementById('espnow-quality-fill');
        const qualityText = document.getElementById('espnow-quality-text');
        qualityFill.style.width = quality + '%';
        qualityFill.className = 'quality-fill ' + getQualityClass(quality);
        qualityText.textContent = quality + '%';
        
        // Update peer info
        document.getElementById('espnow-peer-mac').textContent = 
            data.peer_mac || data.transmitter_mac || 'No peer';
        document.getElementById('espnow-channel').textContent = 
            data.current_channel || '-';
        
        // Update metrics
        const successRate = data.success_rate ? data.success_rate.toFixed(1) : '0.0';
        document.getElementById('espnow-success-rate').textContent = successRate + '%';
        document.getElementById('espnow-queue').textContent = 
            data.queue_size + ' msg' + (data.queue_size !== 1 ? 's' : '');
        
    } catch (error) {
        console.error('Failed to update ESP-NOW status:', error);
        document.getElementById('espnow-state').textContent = 'Error';
    }
}

function getQualityClass(quality) {
    if (quality >= 90) return 'quality-excellent';
    if (quality >= 70) return 'quality-good';
    if (quality >= 50) return 'quality-fair';
    return 'quality-poor';
}

function viewESPNowDetails() {
    window.location = '/espnow.html';  // Detailed page
}

async function resetESPNowMetrics() {
    if (confirm('Reset ESP-NOW metrics?')) {
        try {
            await fetch('/api/espnow/reset_metrics', { method: 'POST' });
            updateESPNowStatus();
        } catch (error) {
            alert('Failed to reset metrics');
        }
    }
}

// Auto-update every 2 seconds
setInterval(updateESPNowStatus, 2000);
updateESPNowStatus();  // Initial load
</script>

<style>
.quality-bar {
    width: 100%;
    height: 20px;
    background: #333;
    border-radius: 10px;
    position: relative;
    overflow: hidden;
}

.quality-fill {
    height: 100%;
    transition: width 0.5s, background-color 0.5s;
    border-radius: 10px;
}

.quality-excellent { background: #0f0; }
.quality-good { background: #8f0; }
.quality-fair { background: #ff0; }
.quality-poor { background: #f80; }

.status-good { color: #0f0; }
.status-warn { background: #ff0; }
.status-bad { color: #f00; }

.mono { font-family: monospace; }
</style>
```

### Endpoint Summary

**Minimum Required (Both Devices):**
- ✅ `GET /api/espnow/status` - Real-time status JSON
- ✅ `GET /api/espnow/diagnostics` - Diagnostic text report

**Recommended (Both Devices):**
- ✅ `GET /api/espnow/history` - State history JSON
- ✅ `POST /api/espnow/reset_metrics` - Reset counters
- ✅ `GET /api/espnow/timing` - Timing configuration JSON

**Optional (Future Enhancement):**
- ⚪ `POST /api/espnow/timing` - Update timing configuration

**Total URI Handlers Required:**
- Minimum: 2 per device **(4 total across both)**
- Recommended: 5 per device **(10 total across both)**
- With optional: 6 per device **(12 total across both)**

**Registration Requirements:**
- Transmitter: Must call `setup_espnow_handlers()` in web server initialization
- Receiver: Must call `setup_espnow_handlers()` in web server initialization
- Both: Must ensure AsyncWebServer library supports all HTTP methods used

---

## Additional Improvements

### Critical Note: Instance Separation in Practice

**Example showing transmitter and receiver have separate queues:**

```cpp
// TRANSMITTER CODE (ESPnowtransmitter2)
void some_transmitter_function() {
    auto& tx_mgr = TransmitterConnectionManager::instance();
    
    // This uses transmitter's OWN queue
    tx_mgr.safe_send(receiver_mac, data, len, "TX_DATA");
    // Queue size: transmitter's queue only
    Serial.printf("TX Queue: %d\n", tx_mgr.get_queue_size());
}

// RECEIVER CODE (espnowreciever_2) - DIFFERENT DEVICE!
void some_receiver_function() {
    auto& rx_mgr = ReceiverConnectionManager::instance();
    
    // This uses receiver's OWN queue (completely separate!)
    rx_mgr.safe_send(transmitter_mac, response, len, "RX_RESPONSE");
    // Queue size: receiver's queue only
    Serial.printf("RX Queue: %d\n", rx_mgr.get_queue_size());
}

// IMPORTANT:
// - Transmitter queue and receiver queue are DIFFERENT memory locations
// - Queuing on transmitter does NOT affect receiver
// - Clearing transmitter queue does NOT clear receiver queue
// - Each device manages its own data independently
// - NO possibility of cross-contamination
```

**Memory Layout:**
```
Transmitter Device (Physical ESP32 #1):
├─ TransmitterConnectionManager instance
│  ├─ message_queue_ (EspNowMessageQueue)     ← Transmitter's queue
│  ├─ state_ (EspNowConnectionState)          ← Transmitter's state
│  ├─ successful_sends_ (uint32_t)            ← Transmitter's counter
│  └─ failed_sends_ (uint32_t)                ← Transmitter's counter

Receiver Device (Physical ESP32 #2):
├─ ReceiverConnectionManager instance
│  ├─ message_queue_ (EspNowMessageQueue)     ← Receiver's queue (DIFFERENT!)
│  ├─ state_ (ReceiverConnectionState)        ← Receiver's state (DIFFERENT!)
│  ├─ successful_sends_ (uint32_t)            ← Receiver's counter (DIFFERENT!)
│  └─ failed_sends_ (uint32_t)                ← Receiver's counter (DIFFERENT!)

Shared Code (esp32common - used by BOTH):
├─ espnow_timing_config.h                     ← Constants (read-only)
├─ EspNowConnectionBase class definition      ← Code (not data!)
├─ EspNowMessageQueue class definition        ← Code (not data!)
└─ Method implementations                     ← Code (not data!)
```

### 1. State-Based Subsystem Behavior Policies

**Different subsystems should behave differently based on connection state:**

```cpp
// File: espnow_connection_manager.cpp

bool EspNowConnectionManager::should_send_heartbeat() const {
    // Only send heartbeats when fully connected or degraded
    return state_ >= EspNowConnectionState::CONNECTED_ACTIVE &&
           state_ <= EspNowConnectionState::CONNECTION_MARGINAL;
}

bool EspNowConnectionManager::should_send_version_beacon() const {
    // Version beacons during handshake or when fully connected
    return state_ == EspNowConnectionState::HANDSHAKING ||
           state_ == EspNowConnectionState::CONNECTED_ACTIVE;
}

bool EspNowConnectionManager::should_sync_config() const {
    // Config sync during initial connection or when requested
    return state_ == EspNowConnectionState::CONFIG_SYNCING ||
           state_ >= EspNowConnectionState::CONNECTED_ACTIVE;
}

bool EspNowConnectionManager::should_transmit_data() const {
    // Data transmission only when fully operational
    return state_ == EspNowConnectionState::CONNECTED_ACTIVE ||
           state_ == EspNowConnectionState::CONNECTED_IDLE;
}

bool EspNowConnectionManager::should_accept_incoming() const {
    // Accept messages in most states (discovery needs to process ACKs!)
    return state_ != EspNowConnectionState::DISCONNECTED;
}
```

### 2. Message Queue System During Transition

**Queue messages during channel locking instead of failing:**

```cpp
class EspNowMessageQueue {
public:
    struct QueuedMessage {
        uint8_t peer_addr[6];
        std::vector<uint8_t> data;
        const char* context;
        uint32_t queued_time;
        uint8_t priority;  // 0=low, 1=normal, 2=high
    };
    
    // Queue a message if we're not ready
    esp_err_t enqueue(const uint8_t* peer_addr, const uint8_t* data, 
                      size_t len, const char* context, uint8_t priority = 1) {
        if (queue_.size() >= MAX_QUEUE_SIZE) {
            LOG_ERROR("[QUEUE] Queue full! Dropping oldest message");
            queue_.pop();
        }
        
        QueuedMessage msg;
        memcpy(msg.peer_addr, peer_addr, 6);
        msg.data.assign(data, data + len);
        msg.context = context;
        msg.queued_time = millis();
        msg.priority = priority;
        
        queue_.push(msg);
        LOG_DEBUG("[QUEUE] Queued %s (total: %d)", context, queue_.size());
        return ESP_OK;
    }
    
    // Flush all queued messages when connection ready
    void flush() {
        uint32_t sent = 0;
        uint32_t failed = 0;
        
        while (!queue_.empty()) {
            auto msg = queue_.front();
            
            // Check for stale messages
            if (millis() - msg.queued_time > EspNowTiming::QUEUED_MESSAGE_STALE_TIMEOUT_MS) {
                LOG_WARN("[QUEUE] Dropping stale message: %s", msg.context);
                queue_.pop();
                continue;
            }
            
            esp_err_t result = esp_now_send(msg.peer_addr, 
                                           msg.data.data(), 
                                           msg.data.size());
            
            if (result == ESP_OK) {
                sent++;
                LOG_DEBUG("[QUEUE] Sent queued %s", msg.context);
            } else {
                failed++;
                LOG_ERROR("[QUEUE] Failed to send queued %s", msg.context);
            }
            
            queue_.pop();
            delay(EspNowTiming::QUEUE_FLUSH_INTER_MESSAGE_DELAY_MS);
        }
        
        LOG_INFO("[QUEUE] Flush complete: %d sent, %d failed", sent, failed);
    }
    
private:
    std::queue<QueuedMessage> queue_;
    static constexpr size_t MAX_QUEUE_SIZE = 20;
};
```

### 3. Connection Quality Metrics

**Track connection health over time:**

```cpp
class ConnectionQualityTracker {
public:
    void track_send_success() {
        total_sends_++;
        successful_sends_++;
        consecutive_failures_ = 0;
    }
    
    void track_send_failure() {
        total_sends_++;
        consecutive_failures_++;
        
        if (consecutive_failures_ >= 5) {
            LOG_WARN("[QUALITY] 5 consecutive send failures!");
        }
    }
    
    void track_heartbeat_received() {
        last_heartbeat_ = millis();
        missed_heartbeats_ = 0;
    }
    
    void track_heartbeat_missed() {
        missed_heartbeats_++;
    }
    
    uint8_t get_connection_quality() const {
        if (total_sends_ == 0) return 100;
        
        // Calculate success rate
        float success_rate = (float)successful_sends_ / total_sends_ * 100.0f;
        return success_rate;
    }
};
```

---

## Implementation Priority & Phases

### ⚠️ CRITICAL IMPLEMENTATION NOTES - READ BEFORE CODING!

**These items are MANDATORY to avoid compilation/logic errors during implementation:**

#### 1. Include Guards & Required Headers
Every header file MUST start with:
```cpp
#pragma once
#include <Arduino.h>
#include <esp_now.h>
#include <esp_err.h>
#include <esp_log.h>
#include <vector>
#include <functional>
#include <queue>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
```

#### 2. State Enums MUST Be in Device-Specific Headers
- `EspNowConnectionState` enum → In `transmitter_connection_manager.h` (NOT base class!)
- `ReceiverConnectionState` enum → In `receiver_connection_manager.h` (NOT base class!)

#### 3. Mutex Initialization is CRITICAL
Constructors MUST create mutexes, destructors MUST delete them:
```cpp
EspNowConnectionBase::EspNowConnectionBase() {
    send_mutex_ = xSemaphoreCreateMutex();
    state_mutex_ = xSemaphoreCreateMutex();
    if (!send_mutex_ || !state_mutex_) {
        ESP_LOGE("ESPNOW", "FATAL: Mutex creation failed!");
    }
}

~EspNowConnectionBase() {
    if (send_mutex_) vSemaphoreDelete(send_mutex_);
    if (state_mutex_) vSemaphoreDelete(state_mutex_);
}
```

Same applies to `EspNowMessageQueue::queue_mutex_`!

#### 4. Logging Macros Required
Add to top of `espnow_timing_config.h`:
```cpp
#ifndef LOG_DEBUG
    #define LOG_DEBUG(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
    #define LOG_INFO(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
    #define LOG_WARN(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
    #define LOG_ERROR(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#endif
```

#### 5. Pure Virtual Methods - All 4 MUST Be Implemented
Derived classes MUST provide implementations:
```cpp
// In TransmitterConnectionManager and ReceiverConnectionManager:
bool is_ready_to_send() const override;
bool is_connected() const override;
const char* get_state_string() const override;
esp_err_t queue_message(...) override;
```

#### 6. Null Pointer Checks Everywhere
```cpp
if (!send_mutex_) {
    ESP_LOGE("ESPNOW", "Mutex not initialized!");
    return ESP_FAIL;
}
```

#### 7. Queue Access Methods
Add to both transmitter AND receiver managers:
```cpp
size_t get_queue_size() const { 
    return message_queue_.get_queue_size(); 
}
```

#### 8. Method Implementations in .cpp Files
These are DECLARED but need IMPLEMENTATION:
- `get_uptime_connected_ms()` - Calculate uptime
- `get_send_success_rate()` - Calculate percentage
- `get_connection_quality()` - Calculate 0-100%
- `generate_diagnostic_report()` - Format string report
- `log_diagnostics()` - Print to serial
- `reset_metrics()` - Clear counters
- `record_state_change()` - Update history
- `track_send_success()` - Increment counter
- `track_send_failure()` - Increment counter
- `update_connected_time()` - Track uptime
- `notify_callbacks()` - Call registered callbacks

#### 9. Thread-Safe State Transitions
```cpp
bool set_state(NewState new_state) {
    if (!state_mutex_) return false;
    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    auto old_state = state_;
    state_ = new_state;
    xSemaphoreGive(state_mutex_);
    notify_callbacks(old_state, new_state);
    return true;
}
```

#### 10. Pre-Compilation Checklist
Before first compile attempt:
- [ ] All header files have #pragma once
- [ ] All required #include statements present
- [ ] State enums defined BEFORE class declarations
- [ ] Mutexes created in ALL constructors
- [ ] Mutexes deleted in ALL destructors
- [ ] All 4 pure virtual methods implemented in derived classes
- [ ] Logging macros defined
- [ ] queue_message_() members declared in derived classes
- [ ] Null checks before all mutex operations
- [ ] Thread safety in all state changes

---

### PHASE 1: Core State Machine (CRITICAL - Fixes Race Condition)
**Estimated Time:** 4-6 hours
**Files to Create:**
1. `src/espnow/espnow_timing_config.h` - Centralized timing/debug configuration
2. `src/espnow/espnow_connection_manager.h` - State enum and manager class with diagnostics
3. `src/espnow/espnow_connection_manager.cpp` - State machine implementation

**Files to Modify in Transmitter:**
12. `discovery_task.cpp` - Integrate transmitter state machine, use shared timing constants
13. `message_handler.cpp` - Add is_ready_to_send() guards using transmitter manager
14. `keep_alive_manager.cpp` - Use shared timing constants for watchdog timeouts
15. `version_beacon_manager.cpp` - Replace esp_now_send() with safe_send() from base
16. `transmission_task.cpp` - Replace esp_now_send() with safe_send() from base

**Files to Modify in Receiver:**
17. `ack_handler.cpp` - Integrate receiver state machine, use shared timing constants
18. `message_handler.cpp` - Add is_ready_to_send() guards using receiver manager
19. `connection_watchdog.cpp` - Use shared timing constants for transmitter loss detection

**Validation:**
- ✅ All common code exists ONLY in esp32common
- ✅ Both transmitter and receiver successfully include shared headers
- ✅ Both devices compile and link correctly
- ✅ No duplicate timing constants in device-specific code
- ✅ No "Peer channel not equal" errors during discovery
- ✅ Clean state progression logs on both devices
- ✅ All initial messages sent successfully
- ✅ Timing values easily adjustable from shared config header
- ✅ State diagnostics available via serial logs on both devices

---

### PHASE 2: Message Queue + Web UI Integration (HIGH)
**Estimated Time:** 4-5 hours

**Files to Create in esp32common/espnow_common_utils/:**
1. `espnow_message_queue.h` - Queue class definition (SHARED)
2. `espnow_message_queue.cpp` - Queue implementation (SHARED)

**Files to Modify in Transmitter:**
3. `api_handlers.cpp` - Add `/api/espnow/status` and `/api/espnow/diagnostics` endpoints
4. `discovery_task.cpp` - Flush queue after connection established
5. Web UI dashboard - Add ESP-NOW status widget

**Files to Modify in Receiver:**
6. `api_handlers.cpp` - Add `/api/espnow/status` and `/api/espnow/diagnostics` endpoints
7. Web UI dashboard - Add ESP-NOW status widget

**Web UI Features:**
- Real-time connection state display
- Connection quality meter (0-100%)
- State transition history viewer
- Reconnect statistics
- Timing configuration editor (for advanced users)

**Validation:**
- All subsystems respect connection state
- Appropriate warnings when sends blocked
- No unexpected send failures
- Web UI shows accurate connection state
- Web UI updates in real-time

---

### PHASE 3: Advanced Features (MEDIUM)

The current system has a **fundamental architectural gap** - it lacks centralized connection lifecycle management. This manifests most critically as race conditions during channel locking, but affects the entire ESP-NOW subsystem.

### Current Problems

1. **No connection state machine** - only scattered binary flags
2. **Concurrent message processing** - RX task runs in parallel with discovery
3. **No channel hopping awareness** - subsystems don't know when discovery is active
4. **Insufficient delays** - hardware needs more time to stabilize during channel transitions
5. **No send guards** - nothing prevents premature sends during critical phases
6. **No degradation tracking** - can't distinguish between "good connection" and "barely connected"
7. **No reconnect strategy** - always does full rediscovery even when unnecessary

### Proposed Solution Benefits

The proposed **EspNowConnectionManager** with comprehensive state machine will:

✅ **Fix Race Condition** - Channel locking states prevent premature sends  
✅ **Track Full Lifecycle** - From discovery → connection → operation → degradation → recovery  
✅ **Coordinate Subsystems** - Clear policies on when each subsystem should operate  
✅ **Enable Channel Hopping** - Explicit states for probe/ack/channel-lock phases  
✅ **Queue Messages** - Buffer data during transitions instead of failing  
✅ **Monitor Quality** - Track connection health and degrade gracefully  
✅ **Smart Reconnect** - Fast reconnect when possible, full discovery when needed  
✅ **Better Debugging** - Clear state logs make issues obvious  
✅ **Future Proof** - Extensible for additional states/features

### Implementation Impact

**Effort:** ~15-20 hours total (can be phased)  
**Risk:** Low - additive changes, existing code continues working  
**Benefit:** High - eliminates critical race condition + improves entire ESP-NOW subsystem  

**Performance Overhead:**
- State checks: < 1μs per operation
- Additional delay: +200-300ms during connection only (one-time cost)
- Memory: ~200 bytes for state manager + queue
- Negligible impact on normal operation

### Immediate Recommendation

**START WITH PHASE 1** - Implement core state machine and fix critical race condition:
1. Create espnow_timing_config.h with all timing constants
2. Create EspNowConnectionManager class
3. Add channel locking states (CHANNEL_TRANSITION → PEER_REGISTRATION → CHANNEL_STABILIZING → CHANNEL_LOCKED)
4. Integrate with discovery_task.cpp using timing constants
5. Add guards to message_handler.cpp

This alone will **eliminate the "Peer channel not equal" errors** and provide foundation for future improvements.

**Future Enhancement:** Expose timing constants in transmitter settings UI for runtime adjustment without recompilation.

**Priority Level:** 🔴 **CRITICAL** - This race condition affects system stability during every connection/reconnection event
**Estimated Time:** 2-3 hours
**Files to Create:**
1. `src/espnow/message_queue.h` - Queue class definition
2. `src/espnow/message_queue.cpp` - Queue implementation

**Files to Modify:**
3. `espnow_connection_manager.cpp` - Integrate queue with safe_send()
4. `discovery_task.cpp` - Flush queue after connection established

**Validation:**
- Messages queued during channel lock
- Successful flush when connected
- No message loss during transition

---

### PHASE 4: Enhanced Diagnostics & Runtime Configuration (LOW)
**Estimated Time:** 3-4 hours

**Files to Modify in esp32common/espnow_common_utils/:**
1. `espnow_connection_base.cpp` - Enhanced diagnostic methods
2. `espnow_metrics.cpp` - Advanced quality calculations

**Files to Modify in Transmitter:**
3. `api_handlers.cpp` - Add timing configuration endpoints
4. Web UI - Timing configuration editor
5. Web UI - Connection quality percentage display

**Files to Modify in Receiver:**
6. `api_handlers.cpp` - Add timing configuration endpoints (if needed)
7. Web UI - Connection quality percentage display

**Validation:**
- ✅ Accurate quality metrics on both devices
- ✅ Degradation detection working on both devices
- ✅ Quality displayed on web UI for both devices
- ✅ Same metrics implementation used by both devices

---

### PHASE 5: Smart Reconnection (LOW - Optimization)
**Estimated Time:** 2-3 hours

**Files to Modify in Transmitter:**
1. `discovery_task.cpp` - Add fast reconnect logic (transmitter-specific)
2. `transmitter_connection_manager.cpp` - Add reconnect state handling

**Files to Modify in Receiver:**
3. `receiver_connection_manager.cpp` - Add transmitter loss recovery

**Validation:**
- ✅ Transmitter fast reconnect successful within 2s
- ✅ Fallback to full discovery works
- ✅ Receiver properly detects and recovers from transmitter loss
- ✅ No unnecessary full discoveries

---

## Final Code Separation Verification

Before considering implementation complete:

### Checklist

**Common Code (esp32common/espnow_common_utils/):**
- [ ] `espnow_timing_config.h` created with ALL timing constants
- [ ] `espnow_connection_base.h` created with base class interface
- [ ] `espnow_connection_base.cpp` implemented with common logic
- [ ] `espnow_send_wrapper.h/.cpp` implemented
- [ ] `espnow_message_queue.h/.cpp` implemented
- [ ] `espnow_metrics.h/.cpp` implemented
- [ ] NO device-specific code in common files
- [ ] All common code properly documented

**Transmitter Code (ESPnowtransmitter2/src/espnow/):**
- [ ] `transmitter_connection_manager.h/.cpp` extends EspNowConnectionBase
- [ ] Includes shared headers from esp32common
- [ ] NO duplicate timing constants
- [ ] NO duplicate send wrapper logic
- [ ] NO duplicate metrics tracking
- [ ] Only transmitter-specific 17-state machine
- [ ] Only channel hopping logic

**Receiver Code (espnowreciever_2/lib/espnow_receiver/):**
- [ ] `receiver_connection_manager.h/.cpp` extends EspNowConnectionBase
- [ ] Includes shared headers from esp32common
- [ ] NO duplicate timing constants
- [ ] NO duplicate send wrapper logic
- [ ] NO duplicate metrics tracking
- [ ] Only receiver-specific 10-state machine
- [ ] Only ACK response logic

**Both Devices:**
- [ ] Compile successfully
- [ ] Link successfully
- [ ] Use identical timing constants from shared header
- [ ] Use shared safe_send() wrapper
- [ ] Use shared message queue
- [ ] Use shared metrics tracking
- [ ] Generate consistent diagnostic reports
- [ ] Both can display connection status on web UIve_failures_ = 0;
    uint8_t missed_heartbeats_ = 0;
    uint32_t last_heartbeat_ = 0;
};
```

### 4. Centralized Timing Configuration

**All timing values defined in `espnow_timing_config.h`:**

```cpp
// Usage in code:
using namespace EspNowTiming;

delay(CHANNEL_TRANSITION_DELAY_MS);   // 50ms
delay(PEER_REGISTRATION_DELAY_MS);    // 100ms
delay(CHANNEL_STABILIZING_DELAY_MS);  // 300ms
// Total: TOTAL_CHANNEL_LOCK_TIME_MS  = 450ms

// Watchdog timeouts:
if (time_since_last > HEARTBEAT_CRITICAL_TIMEOUT_MS) {
    // Connection lost
}
```

**Benefits:**
- ✅ Single source of truth for all timing values
- ✅ Easy to adjust values for testing/optimization
- ✅ Clear documentation of what each value controls
- ✅ Prepared for future runtime configuration via settings UI
- ✅ No magic numbers scattered throughout code

### 5. Smart Reconnection Strategy

**Fast reconnect vs. full rediscovery:**

```cpp
void DiscoveryTask::determine_reconnect_strategy() {
    auto& conn_mgr = EspNowConnectionManager::instance();
    uint32_t time_since_disconnect = millis() - last_disconnect_time_;
    
    if (time_since_disconnect < 10000 && last_channel_valid_) {
        // Disconnected less than 10s ago - try fast reconnect
        LOG_INFO("[DISCOVERY] Fast reconnect on channel %d", last_channel_);
        conn_mgr.start_reconnect(true);
        
        // Try to reconnect on last known channel
        if (!try_fast_reconnect(last_channel_)) {
            // Fast reconnect failed - fall back to full discovery
            LOG_WARN("[DISCOVERY] Fast reconnect failed - starting full discovery");
            conn_mgr.set_state(EspNowConnectionState::FULL_REDISCOVERY);
            start_full_discovery();
        }
    } else {
        // Too long or channel unknown - full discovery required
        LOG_INFO("[DISCOVERY] Full rediscovery required");
        conn_mgr.set_state(EspNowConnectionState::FULL_REDISCOVERY);
        start_full_discovery();
    }
}  
    return result;
}
```

**Pattern to apply everywhere:**

```cpp
// BEFORE (unsafe):
esp_err_t result = esp_now_send(receiver_mac, data, len);

// AFTER (safe):
esp_err_t result = EspNowConnectionManager::instance().safe_send(
    receiver_mac, data, len, "CONFIG_SNAPSHOT"
);
```

**Files requiring updates:**
- `message_handler.cpp` - All handlers that send responses (CONFIG, MQTT, etc.)
- `version_beacon_manager.cpp` - send_version_beacon()
- `keep_alive_manager.cpp` - send_heartbeat()  
- `transmission_task.cpp` - All data transmission sends
- `data_sender.cpp` - send_battery_info(), send_ip_data
        // STATE: Begin channel transition
        conn_mgr.set_state(EspNowConnectionState::CHANNEL_TRANSITION);
        esp_wifi_set_channel(ack_channel, WIFI_SECOND_CHAN_NONE);
        delay(50);  // Wait for WiFi hardware
        LOG_DEBUG("[DISCOVERY] WiFi channel set to %d", ack_channel);
        
        // STATE: Peer registration
        conn_mgr.set_state(EspNowConnectionState::PEER_REGISTRATION);
        memcpy(receiver_mac, ack_mac, 6);
        EspnowPeerManager::add_peer(ack_mac, ack_channel);
        LOG_DEBUG("[DISCOVERY] Peer registered");
        
        // STATE: Stabilizing
        conn_mgr.set_state(EspNowConnectionState::CHANNEL_STABILIZING);
        delay(300);  // Increased stabilization delay
        LOG_DEBUG("[DISCOVERY] Channel stabilizing...");
        
        *discovered_channel = ack_channel;
        return true;
    }
    
    return false;   return true;
    }
}
```

### Change 3: Guard All ESP-NOW Sends

**Pattern to apply everywhere:**

```cpp
// BEFORE (unsafe):
esp_err_t result = esp_now_send(receiver_mac, data, len);

// AFTER (safe):
if (!ConnectionStateManager::instance().is_ready_to_send()) {
    LOG_WARN("[SUBSYSTEM] Cannot send - connection not ready (state: %s)",
             ConnectionStateManager::instance().get_state_string());
    return ESP_FAIL;
}
esp_err_t result = esp_now_send(receiver_mac, data, len);
```

**Files requiring guards:**
- `message_handler.cpp` - All handlers that send responses
- `version_beacon_manager.cpp` - send_version_beacon()
- `keep_alive_manager.cpp` - send_heartbeat()  
- `transmission_task.cpp` - All data sends
- `data_sender.cpp` - send_battery_info(), etc.
- Any other file calling esp_now_send()

### Change 4: Handle Connection Loss

```cpp
void DiscoveryTask::restart() {
    // Mark as disconnected immediately
    ConnectionStateManager::instance().set_state(ConnectionState::DISCONNECTED);
    
    // ... existing cleanup code ...
    
    // After successful restart
    ConnectionStateManager::instance().set_state(ConnectionState::RECONNECTING);
}
```

---

## Additional Improvements

### 1. Increase Stabilization Delay

Current: 200ms
Recommended: 300-500ms

**Rationale:** 
- WiFi channel change is hardware operation
- ESP-NOW peer table update requires driver processing
- Some margin for busy WiFi environments

### 2. Add Channel Verification Loop

```cpp
// Wait up to 500ms for channel to settle
for (int i = 0; i < 10; i++) {
    uint8_t actual_ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&actual_ch, &second);
    
    if (actual_ch == target_channel) {
        LOG_DEBUG("[DISCOVERY] Channel verified on attempt %d", i + 1);
        break;
    }
    
    if (i == 9) {
        LOG_ERROR("[DISCOVERY] Channel failed to settle after 500ms");
        return false;
    }
    
    delay(50);
}
```

### 3. Queue Messages During Transition

Instead of dropping messages, queue them:

```cpp
class TransitionQueue {
    std::queue<PendingMessage> pending_;
    
    void enqueue_if_transitioning(uint8_t type, const uint8_t* data, size_t len) {
        if (ConnectionStateManager::instance().is_transitioning()) {
            pending_.push({type, data, len});
            LOG_DEBUG("[QUEUE] Message queued during transition");
            return;
        }
        // else send immediately
    }
    
    void flush_when_ready() {
        if (!ConnectionStateManager::instance().is_ready_to_send()) return;
        
        while (!pending_.empty()) {
            auto msg = pending_.front();
            esp_now_send(receiver_mac, msg.data, msg.len);
            pending_.pop();
        }
        LOG_INFO("[QUEUE] Flushed %d queued messages", count);
    }
};
```

---

## Testing Plan

### Test 1: Verify No Premature Sends
**Expected:** No "Peer channel is not equal" errors during discovery
**Monitor:** Serial logs for ESP_ERR_ESPNOW_ARG
**Pass Criteria:** Zero ESP_ERR_ESPNOW_ARG errors during 10 connection cycles

### Test 2: Verify State Transitions
**Expected:** Clean progression through all states
**Monitor:** State transition logs
**Pass Criteria:** 
- All state transitions follow valid paths
- No stuck states (max 2s per transition state)
- State history logged correctly

### Test 3: Verify Message Queuing
**Expected:** Messages sent successfully after CONNECTED state
**Monitor:** Message counters and queue flush logs
**Pass Criteria:**
- All queued messages sent within 1s of connection
- No message loss during transition
- Queue doesn't overflow (< 80% capacity)

### Test 4: Connection Loss Recovery
**Expected:** State returns to DISCONNECTED, then reconnects cleanly
**Monitor:** Watchdog recovery sequence
**Pass Criteria:**
- Automatic reconnection within 5s
- Fast reconnect used when appropriate
- No data corruption after recovery

### Test 5: Timing Configuration Validation
**Expected:** All timing constants work correctly
**Monitor:** Connection establishment timing
**Pass Criteria:**
- Channel lock completes within TOTAL_CHANNEL_LOCK_TIME_MS ± 100ms
- Heartbeat timeouts trigger at correct intervals
- Fast reconnect threshold respected

### Test 6: Web UI Integration
**Expected:** Web UI shows accurate ESP-NOW status
**Monitor:** Web UI ESP-NOW widget
**Pass Criteria:**
- State updates in real-time (< 1s lag)
- Connection quality accurate
- Timing settings editable and persistent

### Test 7: Load Testing
**Expected:** System handles high message volume
**Monitor:** CPU usage, memory, send success rate
**Pass Criteria:**
- Sustains 10 msg/sec without failures
- Memory usage stable (no leaks)
- CPU usage < 30% during normal operation

---

## Performance Impact

**Estimated overhead (updated with comprehensive metrics):** 
- State checks: < 1μs per send (negligible)
- Mutex locks: < 5μs per send (safe_send wrapper)
- Additional delay: +200-300ms during connection only (one-time cost)
- Memory usage:
  - State manager: ~200 bytes
  - Message queue: ~2KB (20 messages × ~100 bytes)
  - State history: ~400 bytes (20 entries × 20 bytes)
  - Callbacks: ~100 bytes per registered callback
  - **Total: ~3KB maximum**

**Benefits:**
- ✅ Eliminates ALL channel hopping errors
- ✅ Prevents data loss during discovery
- ✅ Cleaner system architecture
- ✅ Easier debugging with diagnostics
- ✅ Web UI visibility into connection state
- ✅ Runtime configuration without recompilation
- ✅ Comprehensive metrics for monitoring

---

## Best Practices & Recommendations

### Configuration Management
1. **Always use timing constants** from `espnow_timing_config.h` - never hardcode delays
2. **Validate ranges** when exposing settings in web UI
3. **Store settings in NVS** for persistence across reboots
4. **Provide defaults** that work for 95% of use cases
5. **Document acceptable ranges** in code comments

### Code Maintainability
1. **Use helper methods** instead of direct state comparisons
   ```cpp
   // GOOD:
   if (conn_mgr.is_ready_to_send()) { ... }
   
   // BAD:
   if (conn_mgr.get_state() == EspNowConnectionState::CONNECTED_ACTIVE) { ... }
   ```

2. **Always use safe_send()** instead of raw esp_now_send()
   ```cpp
   // GOOD:
   conn_mgr.safe_send(mac, data, len, "CONFIG");
   
   // BAD:
   esp_now_send(mac, data, len);
   ```

3. **Log state changes** with context
   ```cpp
   LOG_INFO("[CONTEXT] State changed: %s → %s (reason: %s)",
            old_state_str, new_state_str, reason);
   ```

4. **Register callbacks** for critical state changes
   ```cpp
   conn_mgr.register_state_change_callback([](auto old_s, auto new_s) {
       if (new_s == EspNowConnectionState::CONNECTED_ACTIVE) {
           flush_pending_data();
       }
   });
   ```

### Debugging & Diagnostics
1. **Enable detailed logging** during development (EspNowDebug flags)
2. **Use diagnostic report** for troubleshooting
   ```cpp
   LOG_INFO("%s", conn_mgr.generate_diagnostic_report().c_str());
   ```
3. **Monitor state history** to identify patterns
4. **Track metrics** to detect degradation early
5. **Export telemetry** to web UI for remote monitoring

### Testing
1. **Test all state transitions** including error paths
2. **Simulate network failures** (SIMULATE_CHANNEL_ERRORS flag)
3. **Load test** with sustained message volume
4. **Verify timing** under different WiFi conditions
5. **Test recovery** from every failure mode

### Web UI Integration
1. **Show connection state** prominently on dashboard
2. **Display quality meter** (color-coded: green/yellow/red)
3. **Expose timing settings** in advanced section only
4. **Add diagnostics page** for state history and metrics
5. **Implement export** of connection logs for support

---

## Future Enhancements (Beyond Initial Implementation)

### Short Term (Next 3-6 months)
1. **Adaptive timing** - adjust delays based on observed network conditions
2. **Channel quality scoring** - remember best channels for faster rediscovery
3. **Predictive reconnection** - detect degradation early and preemptively reconnect
4. **Message prioritization** - ensure critical messages sent first
5. **Bandwidth management** - throttle data transmission during degraded states

### Medium Term (6-12 months)
1. **Multi-receiver support** - manage connections to multiple receivers
2. **Mesh networking** - relay messages through other transmitters
3. **Encrypted channels** - add security layer to ESP-NOW
4. **OTA updates via ESP-NOW** - update firmware without WiFi/Ethernet
5. **AI-based optimization** - machine learning for timing optimization

### Long Term (12+ months)
1. **ESP-NOW protocol v2** - enhanced with QoS guarantees
2. **Hybrid WiFi/ESP-NOW** - seamless switching between modes
3. **Cloud telemetry** - upload connection metrics for analytics
4. **Automated A/B testing** - test timing configurations in production
5. **Standards compliance** - align with emerging ESP-NOW standards

---

## Documentation & Support

### Required Documentation
1. **User Guide** - How to configure ESP-NOW settings in web UI
2. **Developer Guide** - How to use EspNowConnectionManager in code
3. **Troubleshooting Guide** - Common issues and solutions
4. **API Reference** - All public methods and callbacks
5. **Migration Guide** - How to upgrade from old connection logic

### Support Tools
1. **Log Analyzer** - Python script to parse and analyze connection logs
2. **Simulator** - Test environment for development without hardware
3. **Diagnostics Dashboard** - Real-time visualization of connection state
4. **Configuration Validator** - Check timing settings for validity
5. **Performance Profiler** - Identify bottlenecks in state machine

---

## Conclusion & Immediate Actions

The current system has **fundamental architectural gaps** that affect reliability, maintainability, and usability:

### Critical Issues (Fixed by this proposal)
1. ❌ **Race condition** during channel locking → ✅ Explicit state machine
2. ❌ **No timing configuration** → ✅ Centralized config header in esp32common
3. ❌ **Poor diagnostics** → ✅ Comprehensive metrics and logging
4. ❌ **No web UI visibility** → ✅ Status API and dashboard integration
5. ❌ **Hard to maintain** → ✅ Clean abstraction with helper methods
6. ❌ **Duplicate code** between transmitter and receiver → ✅ Shared base classes in esp32common
7. ❌ **No code reuse** → ✅ Common infrastructure shared by both devices

### Immediate Recommended Actions

**Week 1: Shared Foundation (CRITICAL)**
- [ ] Create `esp32common/espnow_common_utils/espnow_timing_config.h` with ALL timing constants
- [ ] Implement `esp32common/espnow_common_utils/espnow_connection_base.h/.cpp` base class
- [ ] Implement `esp32common/espnow_common_utils/espnow_send_wrapper.h/.cpp` safe send wrapper
- [ ] Implement `esp32common/espnow_common_utils/espnow_metrics.h/.cpp` metrics tracking
- [ ] Create `transmitter_connection_manager.h/.cpp` (extends base) in transmitter project
- [ ] Create `receiver_connection_manager.h/.cpp` (extends base) in receiver project
- [ ] **VERIFY:** NO duplicate code between transmitter and receiver
- [ ] **VERIFY:** ALL common code exists ONLY in esp32common

**Week 2: Device Integration (HIGH)**
- [ ] Add state transition logic to transmitter `discovery_task.cpp`
- [ ] Add state transition logic to receiver `ack_handler.cpp`
- [ ] Replace all `esp_now_send()` calls with `safe_send()` on transmitter
- [ ] Replace all `esp_now_send()` calls with `safe_send()` on receiver
- [ ] Add message queueing system (SHARED in esp32common)
- [ ] Test with realistic message loads on BOTH devices
- [ ] **VERIFY:** Both devices use SAME timing constants from esp32common
- [ ] **VERIFY:** Both devices use SAME safe_send() implementation

**Week 3: UI & Diagnostics (MEDIUM)**
- [ ] Add ESP-NOW status API endpoints on transmitter
- [ ] Add ESP-NOW status API endpoints on receiver
- [ ] Create web UI connection status widget on transmitter
- [ ] Create web UI connection status widget on receiver
- [ ] Implement diagnostic reporting (uses SHARED base class methods)
- [ ] Test web UI integration on BOTH devices
- [ ] **VERIFY:** Consistent diagnostic output from both devices

**Week 4: Testing & Validation (LOW)**
- [ ] Run all test plans on transmitter
- [ ] Run all test plans on receiver
- [ ] Test transmitter-receiver interaction
- [ ] Optimize timing values based on results (update SHARED config)
- [ ] Write user documentation
- [ ] **FINAL VERIFICATION:** Code separation checklist (see above)
- [ ] Prepare for production deployment

### Success Metrics
- **Zero** "Peer channel not equal" errors in 100 connection cycles
- **< 500ms** average connection establishment time
- **> 99%** send success rate under normal conditions
- **< 5s** automatic recovery from connection loss
- **Real-time** web UI updates (< 1s lag)

### Final Recommendation

**This is a CRITICAL architectural improvement that should be prioritized immediately.** The current race condition affects system reliability during every connection event. The proposed solution:

- ✅ Fixes the critical bug on both transmitter and receiver
- ✅ Improves maintainability dramatically through code reuse
- ✅ Enables future enhancements with shared infrastructure
- ✅ Provides excellent user visibility on both devices
- ✅ Adds negligible performance overhead
- ✅ Can be implemented incrementally (phased approach)
- ✅ **Eliminates code duplication** - common code shared via esp32common
- ✅ **Single source of truth** - timing constants defined once, used everywhere
- ✅ **Consistent behavior** - both devices use same state management logic

### Critical Requirement: Code Separation

**MANDATORY:** All common ESP-NOW infrastructure MUST be moved to `esp32common/espnow_common_utils/`. Only device-specific state machine logic should remain in transmitter and receiver projects.

**Before implementation is considered complete:**
1. All timing constants in `esp32common/espnow_common_utils/espnow_timing_config.h`
2. Base state machine class in `esp32common/espnow_common_utils/`
3. Safe send wrapper in `esp32common/espnow_common_utils/`
4. Message queue CLASS in `esp32common/espnow_common_utils/`
5. Metrics tracking CLASS in `esp32common/espnow_common_utils/`
6. NO duplicate code between transmitter and receiver
7. Both devices successfully compile using shared code
8. **CRITICAL:** Each device has its OWN queue instance (no shared data)
9. **CRITICAL:** Each device has its OWN metrics (no cross-contamination)
10. **CRITICAL:** Transmitter queue and receiver queue are completely independent

**Start with Phase 1 this week** to create the shared infrastructure and device-specific state machines, then proceed with remaining phases for integration and polish.

---

## Appendix: Configuration Examples

### Example 1: Conservative Settings (High Reliability)
```cpp
namespace EspNowTiming {
    constexpr uint32_t CHANNEL_STABILIZING_DELAY_MS = 500;  // Extra safety
    constexpr uint32_t HEARTBEAT_CRITICAL_TIMEOUT_MS = 60000; // More patient
    constexpr uint8_t MAX_SEND_RETRIES = 5;                  // More retries
}
```

### Example 2: Aggressive Settings (Fast Response)
```cpp
namespace EspNowTiming {
    constexpr uint32_t CHANNEL_STABILIZING_DELAY_MS = 200;  // Minimal delay
    constexpr uint32_t HEARTBEAT_CRITICAL_TIMEOUT_MS = 20000; // Quick timeout
    constexpr uint8_t MAX_SEND_RETRIES = 1;                  // Fail fast
}
```

### Example 3: Development/Debug Settings
```cpp
namespace EspNowDebug {
    constexpr bool LOG_STATE_TRANSITIONS = true;
    constexpr bool LOG_SEND_OPERATIONS = true;  // Verbose
    constexpr bool FORCE_SLOW_DISCOVERY = true; // Easier to observe
    constexpr uint32_t ARTIFICIAL_DELAY_MS = 1000; // Add 1s delays
}
```

**End of Document**
