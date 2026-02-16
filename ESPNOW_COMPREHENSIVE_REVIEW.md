# ESP-NOW System Comprehensive Review
**Date:** February 15, 2026  
**Scope:** Complete lifecycle analysis of ESP-NOW connection management across Transmitter and Receiver

---

## Executive Summary

The ESP-NOW implementation has **multiple architectural issues** causing state synchronization problems, race conditions, and unreliable reconnection. The system works for the **happy path** (initial connection) but **fails under stress** (connection loss, state changes, reconnection scenarios).

### Critical Findings

1. **GLOBAL VARIABLE DEPENDENCY** - Heartbeat system relies on `receiver_mac` global that isn't synchronized with state machine
2. **STATE MACHINE FRAGMENTATION** - 3+ separate state tracking systems creating inconsistencies
3. **CHANNEL MANAGEMENT CHAOS** - Multiple entities changing WiFi channel without coordination
4. **PEER REGISTRATION RACE CONDITIONS** - Peers registered before/after connection state transitions
5. **NO RECONNECTION STRATEGY** - Connection loss → IDLE without proper cleanup/re-initialization

---

## 1. Architecture Overview

### 1.1 Current State Management Systems

The system has **FOUR** independent state tracking mechanisms that should be **ONE**:

```
┌─────────────────────────────────────────────────────┐
│ 1. EspNowConnectionManager (Common)                 │
│    States: IDLE → CONNECTING → CONNECTED            │
│    Location: esp32common/connection_manager.cpp     │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│ 2. receiver_mac[6] Global Variable (Transmitter)    │
│    States: {0xFF...} (broadcast) → {MAC} (known)    │
│    Location: esp32common/espnow_transmitter.cpp     │
│    PROBLEM: Not synchronized with #1                │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│ 3. TransmitterManager::isTransmitterConnected()     │
│    Logic: connection_manager.is_connected()          │
│    Location: receiver/transmitter_manager.cpp       │
│    RECENTLY FIXED but relies on #1                  │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│ 4. RecoveryState (Discovery Task)                   │
│    States: NORMAL, CHANNEL_MISMATCH, RESTARTING...  │
│    Location: transmitter/discovery_task.cpp         │
│    Independent from #1-3                            │
└─────────────────────────────────────────────────────┘
```

**RECOMMENDATION:** Consolidate into single source of truth (EspNowConnectionManager) with proper callbacks for dependent systems.

---

## 2. Connection Lifecycle Analysis

### 2.1 Initial Connection (Happy Path)

**TRANSMITTER:**
```
1. Start → TransmitterConnectionHandler::start_discovery()
2. Post CONNECTION_START → State: IDLE → CONNECTING
3. DiscoveryTask::start_active_channel_hopping()
4. Sweep channels 1-13, send PROBE on each
5. Receiver sends ACK with channel info
6. on_ack_received() → Post PEER_FOUND
7. EspnowPeerManager::add_peer(receiver_mac)
8. on_peer_registered() → Post PEER_REGISTERED → State: CONNECTED
9. ✅ receiver_mac global updated (RECENT FIX)
10. HeartbeatManager starts sending (every 10s)
```

**RECEIVER:**
```
1. Start → ReceiverConnectionHandler::init()
2. Post CONNECTION_START → State: IDLE → CONNECTING
3. Wait for PROBE messages (passive listening)
4. on_probe_received() → Post PEER_FOUND
5. Send ACK back to transmitter
6. EspnowPeerManager::add_peer(transmitter_mac)
7. on_peer_registered() → Post PEER_REGISTERED → State: CONNECTED
8. RxHeartbeatManager starts timeout monitoring (30s timeout)
```

### 2.2 Connection Loss Detection

**TRANSMITTER SIDE:**
```
Heartbeat Timeout (HeartbeatManager::tick):
- Check: unacked heartbeats > MAX_UNACKED_HEARTBEATS (3)
- Action: Post CONNECTION_LOST event
- State: CONNECTED → IDLE
- Problem: No cleanup of receiver_mac global ❌
- Problem: No restart of discovery ❌
```

**RECEIVER SIDE:**
```
Heartbeat Timeout (RxHeartbeatManager::tick):
- Check: time_since_last > HEARTBEAT_TIMEOUT_MS (30000)
- Action: Post CONNECTION_LOST event
- State: CONNECTED → IDLE
- Action: ReceiverConnectionHandler::on_connection_lost()
- Problem: No re-initialization of discovery ❌
```

### 2.3 Reconnection (BROKEN)

**Current Behavior:**
1. Connection lost → Both devices → IDLE state
2. Transmitter: HeartbeatManager stops (state != CONNECTED)
3. Transmitter: Discovery task suspended (is_connected callback returns false)
4. Receiver: Heartbeat timeout monitoring stops (state != CONNECTED)
5. **BOTH DEVICES STUCK** - No reconnection mechanism ❌

**What Should Happen:**
1. Connection lost → Both devices → IDLE state
2. **Transmitter: Restart active channel hopping**
3. **Receiver: Resume passive listening**
4. Repeat initial connection handshake
5. State: IDLE → CONNECTING → CONNECTED

---

## 3. Critical Issues by Component

### 3.1 Connection State Machine (connection_manager.cpp)

**Issues:**
- ✅ **GOOD:** Clean 3-state design
- ✅ **GOOD:** Event-driven architecture
- ❌ **BAD:** No callbacks for state transitions (dependent systems don't know when state changes)
- ❌ **BAD:** No automatic reconnection on CONNECTION_LOST
- ❌ **BAD:** peer_mac_ stored but not used by dependent systems

**Recommendations:**
```cpp
class EspNowConnectionManager {
    // ADD: State change callback registration
    void register_state_callback(std::function<void(EspNowConnectionState old, EspNowConnectionState new)> callback);
    
    // ADD: Automatic reconnection on connection loss
    void handle_connected_event(const EspNowStateChange& event) {
        switch (event.event) {
            case EspNowEvent::CONNECTION_LOST:
                transition_to_state(EspNowConnectionState::IDLE);
                // NEW: Trigger reconnection
                post_event(EspNowEvent::CONNECTION_START);
                break;
        }
    }
};
```

### 3.2 Heartbeat System

**TRANSMITTER (heartbeat_manager.cpp):**

**Issues:**
```cpp
void HeartbeatManager::send_heartbeat() {
    // ❌ CRITICAL: Checks global receiver_mac instead of connection manager state
    bool is_broadcast = true;
    for (int i = 0; i < 6; i++) {
        if (receiver_mac[i] != 0xFF) {
            is_broadcast = false;
            break;
        }
    }
    if (is_broadcast) {
        LOG_WARN("HEARTBEAT", "Cannot send heartbeat - receiver MAC not yet discovered (broadcast)");
        return; // ❌ Silent failure - state machine says CONNECTED but heartbeat won't send
    }
```

**Problem:** If `receiver_mac` isn't updated (race condition), heartbeats fail even when state is CONNECTED.

**Fix Applied:** `tx_connection_handler.cpp::on_peer_registered()` now updates global `receiver_mac`.

**Better Solution:** Get MAC from connection manager:
```cpp
void HeartbeatManager::send_heartbeat() {
    const uint8_t* peer_mac = EspNowConnectionManager::instance().get_peer_mac();
    if (peer_mac[0] == 0 && peer_mac[1] == 0) {
        LOG_WARN("HEARTBEAT", "No peer MAC in connection manager");
        return;
    }
    esp_err_t result = esp_now_send(peer_mac, (uint8_t*)&hb, sizeof(hb));
}
```

**RECEIVER (rx_heartbeat_manager.cpp):**

**Issues:**
- ✅ **GOOD:** Proper CRC validation
- ✅ **GOOD:** Sequence number tracking
- ❌ **BAD:** Timeout only checks after first heartbeat received (could miss connection loss if first heartbeat never arrives)
- ❌ **BAD:** No recovery mechanism after timeout (just posts CONNECTION_LOST)

**Recommendations:**
```cpp
void RxHeartbeatManager::tick() {
    // Current: Only timeout if m_heartbeats_received > 0
    // Problem: If first heartbeat fails, connection assumed good forever
    
    // ADD: Grace period for first heartbeat
    if (m_heartbeats_received == 0) {
        uint32_t time_since_connected = EspNowConnectionManager::instance().get_connected_time_ms();
        if (time_since_connected > FIRST_HEARTBEAT_GRACE_PERIOD_MS) {
            LOG_ERROR("HEARTBEAT", "No heartbeat received within grace period");
            EspNowConnectionManager::instance().post_event(EspNowEvent::CONNECTION_LOST);
        }
    }
}
```

### 3.3 Channel Management

**CHAOS IDENTIFIED:**

Multiple entities can change WiFi channel:
1. **DiscoveryTask::active_channel_hopping_task** - Sweeps channels 1-13 during discovery
2. **DiscoveryTask::force_and_verify_channel** - Locks to discovered channel
3. **WiFi library** - May auto-change channel for AP connections

**Issues:**
- ❌ No mutex/lock protecting channel changes
- ❌ Peer registration with channel=0 (use current) vs explicit channel number inconsistency
- ❌ Channel mismatch between WiFi interface and ESP-NOW peers causes send failures

**Example of Failure:**
```
1. Discovery finds receiver on channel 11
2. Locks WiFi to channel 11 ✅
3. Adds broadcast peer with channel=0 (uses current = 11) ✅
4. Adds receiver peer with channel=11 explicitly ✅
5. **CONNECTION LOST** → IDLE
6. Discovery restarts → Starts sweeping channels again
7. Changes WiFi to channel 1 for scanning
8. Broadcast peer still has channel=0 → NOW USES CHANNEL 1 ✅
9. Receiver peer still has channel=11 → MISMATCH ❌
10. esp_now_send() fails: "Peer channel is not equal to the home channel"
```

**Recommendations:**
```cpp
class ChannelManager {
private:
    uint8_t locked_channel_;
    bool is_locked_;
    SemaphoreHandle_t mutex_;
    
public:
    // Only one place to change channel
    bool set_channel(uint8_t channel, bool lock = false);
    
    // Get current locked channel
    uint8_t get_locked_channel() { return is_locked_ ? locked_channel_ : 0; }
    
    // Clear channel lock (for reconnection)
    void unlock_channel();
};
```

### 3.4 Peer Registration

**TRANSMITTER (espnow_tasks.cpp equivalent in TX):**

**Issues:**
```cpp
// Current: Peer registration triggered on every message
if (!EspnowPeerManager::is_peer_registered(queue_msg.mac)) {
    if (EspnowPeerManager::add_peer(queue_msg.mac, 0)) {
        // ❌ Can be called in ANY state (IDLE, CONNECTING, CONNECTED)
        on_peer_registered(queue_msg.mac);
    }
}
```

**RECEIVER (espnow_tasks.cpp:518-532):**

**Recently Fixed:**
```cpp
// Only trigger peer_registered events when in CONNECTING state
auto current_state = EspNowConnectionManager::instance().get_state();
bool is_connecting = (current_state == EspNowConnectionState::CONNECTING);

if (!EspnowPeerManager::is_peer_registered(queue_msg.mac)) {
    if (EspnowPeerManager::add_peer(queue_msg.mac, 0)) {
        if (is_connecting) {  // ✅ NEW: State check
            ReceiverConnectionHandler::instance().on_peer_registered(queue_msg.mac);
        }
    }
}
```

**Remaining Issues:**
- ❌ Peer added to ESP-NOW subsystem even when state is IDLE (shouldn't accept peers when not connecting)
- ❌ No cleanup of stale peers when connection is lost

**Recommendations:**
```cpp
// Only register peers when actively connecting
if (current_state == EspNowConnectionState::CONNECTING) {
    if (!EspnowPeerManager::is_peer_registered(queue_msg.mac)) {
        if (EspnowPeerManager::add_peer(queue_msg.mac, 0)) {
            on_peer_registered(queue_msg.mac);
        }
    }
} else if (current_state == EspNowConnectionState::IDLE) {
    // Ignore peer registration attempts when idle
    LOG_DEBUG(kLogTag, "Ignoring peer registration in IDLE state");
}
```

### 3.5 Discovery Task (Transmitter)

**Active Channel Hopping:**

**Issues:**
```cpp
void DiscoveryTask::active_channel_hopping_task(void* parameter) {
    // Sweeps channels 1-13, sends PROBE on each
    for (uint8_t attempt = 1; attempt <= max_attempts; attempt++) {
        for (uint8_t ch : k_channels) {
            set_channel(ch);  // ❌ Changes WiFi channel globally
            send_probe_on_channel(ch);
            delay(1000);  // 1s per channel
            
            // ❌ PROBLEM: No check if ACK received during this 1s window
            // If ACK arrives after 900ms, we switch to next channel anyway
        }
    }
}
```

**Race Condition:**
```
Time 0ms:   Set WiFi channel 10, send PROBE
Time 500ms: Receiver receives PROBE, sends ACK
Time 800ms: ACK arrives at transmitter
Time 1000ms: Transmitter switches to channel 11 ❌
Time 1100ms: Transmitter tries to add receiver peer on channel 10
Time 1200ms: esp_now_send() fails (channel mismatch)
```

**Recommendations:**
```cpp
// Check for ACK in polling loop instead of fixed delay
for (uint32_t elapsed = 0; elapsed < 1000; elapsed += 10) {
    delay(10);
    // Check discovery queue for ACK
    if (xQueuePeek(espnow_discovery_queue, &msg, 0) == pdTRUE) {
        if (msg.data[0] == msg_ack) {
            // ACK received! Lock to this channel immediately
            LOG_INFO("DISCOVERY", "ACK received on channel %d - locking", ch);
            g_lock_channel = ch;
            return true;  // Exit immediately
        }
    }
}
```

**Suspension Logic:**

**Issues:**
```cpp
EspnowDiscovery::instance().start(
    []() -> bool {
        return EspNowConnectionManager::instance().is_connected();
    },
    timing::ANNOUNCEMENT_INTERVAL_MS,
    task_config::PRIORITY_LOW,
    task_config::STACK_SIZE_ANNOUNCEMENT
);

// In task_impl:
if (config->is_connected && config->is_connected()) {
    LOG_INFO("DISCOVERY", "Peer connected - suspending announcements");
    instance().suspended_ = true;
    continue;  // ✅ Keeps task alive
}
```

**Problem:** When connection is lost:
1. State changes: CONNECTED → IDLE
2. `is_connected()` returns false
3. Discovery resumes... **BUT ONLY FOR PASSIVE ANNOUNCEMENTS** ❌
4. Active channel hopping task is separate and **NOT RESTARTED** ❌

**Recommendation:**
```cpp
// Register callback for connection state changes
EspNowConnectionManager::instance().register_state_callback(
    [](EspNowConnectionState old_state, EspNowConnectionState new_state) {
        if (old_state == EspNowConnectionState::CONNECTED && 
            new_state == EspNowConnectionState::IDLE) {
            // Connection lost - restart discovery
            DiscoveryTask::instance().restart_active_channel_hopping();
        }
    }
);
```

---

## 4. State Machine Timing Analysis

### 4.1 Transmitter Timeline (Initial Connection)

```
T+0ms       Start
T+10ms      CONNECTION_START event
T+20ms      State: IDLE → CONNECTING
T+30ms      Active channel hopping starts
T+50ms      Channel 1, send PROBE
T+1050ms    Channel 2, send PROBE
...
T+10050ms   Channel 11, send PROBE
T+10150ms   Receiver sends ACK
T+10200ms   ACK received
T+10250ms   PEER_FOUND event
T+10300ms   add_peer(receiver_mac, channel=11)
T+10350ms   PEER_REGISTERED event
T+10400ms   State: CONNECTING → CONNECTED
T+10450ms   receiver_mac global updated ✅
T+10500ms   HeartbeatManager::tick() - first heartbeat sent
T+20500ms   Second heartbeat
T+30500ms   Third heartbeat
```

**Timing Issues:**
- Discovery can take up to 13 seconds (worst case: receiver on channel 13)
- No timeout for CONNECTING state (could stay there forever if ACK lost)

### 4.2 Receiver Timeline (Initial Connection)

```
T+0ms       Start
T+10ms      CONNECTION_START event
T+20ms      State: IDLE → CONNECTING
T+30ms      Passive listening on channel 11
T+10050ms   PROBE received from transmitter
T+10100ms   PEER_FOUND event
T+10150ms   Send ACK with channel=11
T+10200ms   add_peer(transmitter_mac, channel=0)
T+10250ms   PEER_REGISTERED event
T+10300ms   State: CONNECTING → CONNECTED
T+10350ms   send_initialization_requests()
T+10500ms   First heartbeat arrives
T+10550ms   updateTimeData(uptime, unix_time, time_source)
T+20500ms   Second heartbeat
```

**Timing Issues:**
- Relies on transmitter finding it (passive role)
- No timeout for CONNECTING state
- Initialization requests sent before confirming transmitter is ready

### 4.3 Connection Loss Timeline (Current - BROKEN)

```
TRANSMITTER:
T+0ms       Last heartbeat ACK received
T+10000ms   Send heartbeat #1 - no ACK
T+20000ms   Send heartbeat #2 - no ACK
T+30000ms   Send heartbeat #3 - no ACK
T+40000ms   Send heartbeat #4 - no ACK (unacked=4 > MAX=3)
T+40050ms   CONNECTION_LOST event posted
T+40100ms   State: CONNECTED → IDLE
T+40150ms   HeartbeatManager::tick() - stops sending (state != CONNECTED)
T+40200ms   Discovery task: suspended = false (is_connected() = false)
T+40250ms   ❌ **STUCK** - passive announcements resume but active hopping doesn't restart

RECEIVER:
T+0ms       Last heartbeat received
T+30000ms   Heartbeat timeout (30s)
T+30050ms   CONNECTION_LOST event posted
T+30100ms   State: CONNECTED → IDLE
T+30150ms   RxHeartbeatManager::tick() - stops monitoring (state != CONNECTED)
T+30200ms   ❌ **STUCK** - waiting for PROBE but not actively listening for reconnection
```

### 4.4 Reconnection Timeline (Proposed - FIXED)

```
TRANSMITTER:
T+40000ms   CONNECTION_LOST event posted
T+40100ms   State: CONNECTED → IDLE
T+40150ms   State callback triggered
T+40200ms   restart_active_channel_hopping() called
T+40250ms   receiver_mac reset to broadcast ✅
T+40300ms   g_lock_channel reset to 0 ✅
T+40350ms   Old receiver peer deleted ✅
T+40400ms   Channel hopping starts from channel 1
T+41400ms   Channel 2
...
T+50400ms   Channel 11 - PROBE sent
T+50500ms   Receiver sends ACK
T+50550ms   ACK received, PEER_FOUND
T+50600ms   PEER_REGISTERED
T+50650ms   State: CONNECTING → CONNECTED ✅

RECEIVER:
T+30000ms   CONNECTION_LOST event posted
T+30100ms   State: CONNECTED → IDLE
T+30150ms   State callback triggered
T+30200ms   restart_passive_listening() called ✅
T+30250ms   Old transmitter peer deleted ✅
T+30300ms   Waiting for PROBE...
T+50400ms   PROBE received
T+50450ms   PEER_FOUND
T+50500ms   Send ACK
T+50550ms   PEER_REGISTERED
T+50600ms   State: CONNECTING → CONNECTED ✅
```

---

## 5. Detailed Recommendations

### 5.1 Connection Manager Enhancements

**File:** `esp32common/espnow_common_utils/connection_manager.h`

```cpp
class EspNowConnectionManager {
public:
    // ADD: Callback registration for state changes
    using StateChangeCallback = std::function<void(EspNowConnectionState old_state, EspNowConnectionState new_state)>;
    void register_state_callback(StateChangeCallback callback);
    
    // ADD: Timeout monitoring for CONNECTING state
    void set_connecting_timeout_ms(uint32_t timeout_ms) { connecting_timeout_ms_ = timeout_ms; }
    
    // ADD: Automatic reconnection on connection loss
    void set_auto_reconnect(bool enable) { auto_reconnect_ = enable; }
    
    // ADD: Get peer MAC from state machine (eliminate global variable)
    const uint8_t* get_peer_mac() const { return peer_mac_; }
    
private:
    std::vector<StateChangeCallback> state_callbacks_;
    uint32_t connecting_timeout_ms_ = 15000;  // 15s default
    bool auto_reconnect_ = true;
    
    void transition_to_state(EspNowConnectionState new_state) {
        if (new_state == current_state_) return;
        
        EspNowConnectionState old_state = current_state_;
        current_state_ = new_state;
        state_enter_time_ = millis();
        
        // Notify all registered callbacks
        for (auto& callback : state_callbacks_) {
            callback(old_state, new_state);
        }
        
        // Auto-reconnect logic
        if (auto_reconnect_ && new_state == EspNowConnectionState::IDLE && 
            old_state == EspNowConnectionState::CONNECTED) {
            post_event(EspNowEvent::CONNECTION_START);
        }
    }
    
    // ADD: Process events with timeout check
    void process_events() {
        // Check CONNECTING timeout
        if (current_state_ == EspNowConnectionState::CONNECTING) {
            if (get_state_time_ms() > connecting_timeout_ms_) {
                LOG_ERROR("CONN_MGR", "CONNECTING timeout - returning to IDLE");
                transition_to_state(EspNowConnectionState::IDLE);
            }
        }
        
        // Process queue events...
    }
};
```

### 5.2 Heartbeat System Refactor

**File:** `ESPnowtransmitter2/src/espnow/heartbeat_manager.cpp`

```cpp
void HeartbeatManager::send_heartbeat() {
    // ✅ Get MAC from connection manager instead of global variable
    const uint8_t* peer_mac = EspNowConnectionManager::instance().get_peer_mac();
    
    // Check if peer MAC is valid (not all zeros)
    bool mac_valid = false;
    for (int i = 0; i < 6; i++) {
        if (peer_mac[i] != 0) {
            mac_valid = true;
            break;
        }
    }
    
    if (!mac_valid) {
        LOG_WARN("HEARTBEAT", "Cannot send heartbeat - no peer MAC in connection manager");
        return;
    }
    
    heartbeat_t hb;
    hb.type = msg_heartbeat;
    hb.seq = ++m_heartbeat_seq;
    hb.uptime_ms = millis();
    hb.unix_time = TimeManager::instance().get_unix_time();
    hb.time_source = static_cast<uint8_t>(TimeManager::instance().get_time_source());
    hb.state = static_cast<uint8_t>(EspNowConnectionManager::instance().get_state());
    hb.rssi = 0;
    hb.flags = 0;
    hb.checksum = calculate_crc16(&hb, sizeof(hb) - sizeof(hb.checksum));
    
    esp_err_t result = esp_now_send(peer_mac, (uint8_t*)&hb, sizeof(hb));
    
    if (result == ESP_OK) {
        LOG_DEBUG("HEARTBEAT", "Sent heartbeat seq=%u to %02X:%02X:%02X:%02X:%02X:%02X", 
                  hb.seq, peer_mac[0], peer_mac[1], peer_mac[2], 
                  peer_mac[3], peer_mac[4], peer_mac[5]);
    } else {
        LOG_ERROR("HEARTBEAT", "Failed to send heartbeat seq=%u: %s", hb.seq, esp_err_to_name(result));
    }
}

// ✅ ADD: Reset on connection loss
void HeartbeatManager::on_connection_state_changed(EspNowConnectionState old_state, EspNowConnectionState new_state) {
    if (old_state == EspNowConnectionState::CONNECTED && 
        new_state == EspNowConnectionState::IDLE) {
        reset();  // Clear heartbeat sequence numbers
    }
}
```

**File:** `espnowreciever_2/src/espnow/rx_heartbeat_manager.cpp`

```cpp
void RxHeartbeatManager::tick() {
    if (!m_initialized) return;
    
    EspNowConnectionState state = EspNowConnectionManager::instance().get_state();
    
    // Only check timeout when connected
    if (state != EspNowConnectionState::CONNECTED) {
        m_last_rx_time_ms = millis();
        return;
    }

    uint32_t now = millis();
    uint32_t time_since_last = now - m_last_rx_time_ms;
    
    // ✅ ADD: Grace period for first heartbeat
    if (m_heartbeats_received == 0) {
        uint32_t time_since_connected = EspNowConnectionManager::instance().get_connected_time_ms();
        if (time_since_connected > FIRST_HEARTBEAT_GRACE_PERIOD_MS) {
            LOG_ERROR("HEARTBEAT", "No heartbeat received within grace period (%u ms)", FIRST_HEARTBEAT_GRACE_PERIOD_MS);
            ReceiverConnectionHandler::instance().on_connection_lost();
            EspNowConnectionManager::instance().post_event(EspNowEvent::CONNECTION_LOST);
            return;
        }
    }
    
    // Check for heartbeat timeout (after first heartbeat received)
    if (m_heartbeats_received > 0 && time_since_last > HEARTBEAT_TIMEOUT_MS) {
        LOG_ERROR("HEARTBEAT", "Connection lost: No heartbeat for %u ms (timeout: %u ms, total received: %u)",
                  time_since_last, HEARTBEAT_TIMEOUT_MS, m_heartbeats_received);
        ReceiverConnectionHandler::instance().on_connection_lost();
        EspNowConnectionManager::instance().post_event(EspNowEvent::CONNECTION_LOST);
    }
}

// ✅ ADD: Constants
private:
    static constexpr uint32_t FIRST_HEARTBEAT_GRACE_PERIOD_MS = 15000;  // 15s
```

### 5.3 Channel Manager (New Component)

**File:** `esp32common/espnow_common_utils/channel_manager.h` (NEW)

```cpp
#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class ChannelManager {
public:
    static ChannelManager& instance() {
        static ChannelManager instance;
        return instance;
    }
    
    bool init();
    
    // Set WiFi channel (thread-safe)
    bool set_channel(uint8_t channel, bool lock = false);
    
    // Get current WiFi channel
    uint8_t get_current_channel() const;
    
    // Get locked channel (0 if not locked)
    uint8_t get_locked_channel() const { return is_locked_ ? locked_channel_ : 0; }
    
    // Check if channel is locked
    bool is_locked() const { return is_locked_; }
    
    // Unlock channel (for reconnection)
    void unlock_channel();
    
private:
    ChannelManager();
    
    uint8_t locked_channel_;
    bool is_locked_;
    SemaphoreHandle_t mutex_;
};
```

**File:** `esp32common/espnow_common_utils/channel_manager.cpp` (NEW)

```cpp
#include "channel_manager.h"

ChannelManager::ChannelManager()
    : locked_channel_(0), is_locked_(false), mutex_(nullptr) {}

bool ChannelManager::init() {
    mutex_ = xSemaphoreCreateMutex();
    return mutex_ != nullptr;
}

bool ChannelManager::set_channel(uint8_t channel, bool lock) {
    if (mutex_ == nullptr) return false;
    
    xSemaphoreTake(mutex_, portMAX_DELAY);
    
    // Check if channel is locked to different value
    if (is_locked_ && locked_channel_ != channel) {
        Serial.printf("[CHANNEL_MGR] Cannot change channel - locked to %d\n", locked_channel_);
        xSemaphoreGive(mutex_);
        return false;
    }
    
    // Set WiFi channel
    esp_err_t result = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (result != ESP_OK) {
        Serial.printf("[CHANNEL_MGR] Failed to set channel %d: %s\n", channel, esp_err_to_name(result));
        xSemaphoreGive(mutex_);
        return false;
    }
    
    // Lock if requested
    if (lock) {
        locked_channel_ = channel;
        is_locked_ = true;
        Serial.printf("[CHANNEL_MGR] Channel locked to %d\n", channel);
    }
    
    xSemaphoreGive(mutex_);
    return true;
}

uint8_t ChannelManager::get_current_channel() const {
    uint8_t ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&ch, &second);
    return ch;
}

void ChannelManager::unlock_channel() {
    if (mutex_ == nullptr) return;
    
    xSemaphoreTake(mutex_, portMAX_DELAY);
    is_locked_ = false;
    locked_channel_ = 0;
    Serial.printf("[CHANNEL_MGR] Channel unlocked\n");
    xSemaphoreGive(mutex_);
}
```

### 5.4 Discovery Task Reconnection Fix

**File:** `ESPnowtransmitter2/src/espnow/discovery_task.cpp`

```cpp
class DiscoveryTask {
public:
    // ✅ ADD: Restart active hopping for reconnection
    void restart_active_channel_hopping() {
        LOG_INFO("DISCOVERY", "Restarting active channel hopping for reconnection");
        
        // 1. Reset channel lock
        ChannelManager::instance().unlock_channel();
        g_lock_channel = 0;
        
        // 2. Reset receiver MAC to broadcast
        memset(receiver_mac, 0xFF, 6);
        
        // 3. Cleanup old peers
        cleanup_all_peers();
        
        // 4. Restart hopping task
        if (task_handle_ != nullptr) {
            vTaskDelete(task_handle_);
            task_handle_ = nullptr;
        }
        
        start_active_channel_hopping();
    }
    
    // ✅ MODIFY: Register state change callback in start()
    void start() {
        // Register callback for automatic restart on connection loss
        EspNowConnectionManager::instance().register_state_callback(
            [](EspNowConnectionState old_state, EspNowConnectionState new_state) {
                if (old_state == EspNowConnectionState::CONNECTED && 
                    new_state == EspNowConnectionState::IDLE) {
                    DiscoveryTask::instance().restart_active_channel_hopping();
                }
            }
        );
        
        // ... existing start logic
    }
};
```

### 5.5 Peer Management Cleanup

**File:** `espnowreciever_2/src/espnow/espnow_tasks.cpp`

```cpp
// ✅ MODIFY: Only register peers when in CONNECTING state
static void handle_espnow_message_routing(const espnow_queue_msg_t& queue_msg) {
    auto& router = EspnowMessageRouter::instance();
    auto current_state = EspNowConnectionManager::instance().get_state();
    
    // Route the message through registered handlers
    router.route_message(&queue_msg);
    
    // Peer registration logic with strict state checking
    bool is_connecting = (current_state == EspNowConnectionState::CONNECTING);
    bool is_connected = (current_state == EspNowConnectionState::CONNECTED);
    
    if (is_connecting) {
        // Only register new peers when actively connecting
        if (!EspnowPeerManager::is_peer_registered(queue_msg.mac)) {
            if (EspnowPeerManager::add_peer(queue_msg.mac, 0)) {
                ReceiverConnectionHandler::instance().on_peer_registered(queue_msg.mac);
            }
        }
    } else if (is_connected) {
        // Already connected - refresh peer registration if needed
        if (!EspnowPeerManager::is_peer_registered(queue_msg.mac)) {
            LOG_WARN(kLogTag, "Peer not registered but state is CONNECTED - re-registering");
            EspnowPeerManager::add_peer(queue_msg.mac, 0);
        }
    } else {
        // IDLE state - ignore peer registration
        LOG_DEBUG(kLogTag, "Received message in IDLE state - ignoring peer registration");
    }
}

// ✅ ADD: Cleanup stale peers on connection loss
void cleanup_peers_on_connection_lost() {
    LOG_INFO(kLogTag, "Cleaning up stale ESP-NOW peers");
    
    // Remove transmitter peer if it exists
    const uint8_t* tx_mac = ReceiverConnectionHandler::instance().get_transmitter_mac();
    if (esp_now_is_peer_exist(tx_mac)) {
        esp_err_t result = esp_now_del_peer(tx_mac);
        if (result == ESP_OK) {
            LOG_INFO(kLogTag, "Removed transmitter peer");
        } else {
            LOG_ERROR(kLogTag, "Failed to remove transmitter peer: %s", esp_err_to_name(result));
        }
    }
}
```

---

## 6. Testing Plan

### 6.1 Initial Connection Test

**Test Case 1.1: Happy Path**
```
1. Power on transmitter → verify state transitions: IDLE → CONNECTING
2. Power on receiver → verify state transitions: IDLE → CONNECTING
3. Verify channel hopping starts on transmitter
4. Verify receiver sends ACK
5. Verify both devices transition to CONNECTED
6. Verify heartbeats start flowing (transmitter → receiver)
7. Verify ACKs returned (receiver → transmitter)
8. Verify time data appears on dashboard within 15 seconds
```

**Expected Logs:**
```
TRANSMITTER:
[CONN_MGR] CONNECTION_START → Transitioning to CONNECTING
[DISCOVERY] Broadcasting PROBE on channel 11 for 1000ms...
[MSG_HANDLER] Receiver connected via ACK
[CONN_MGR] PEER_REGISTERED → Transitioning to CONNECTED
[TX_CONN] Updated global receiver_mac: F0:9E:9E:1F:98:20
[HEARTBEAT] Sent heartbeat seq=1, uptime=12345 ms

RECEIVER:
[CONN_MGR] CONNECTION_START → Transitioning to CONNECTING
[RX_CONN] PROBE received from 5C:01:3B:53:2F:18
[CONN_MGR] PEER_REGISTERED → Transitioning to CONNECTED
[HEARTBEAT] Received heartbeat seq=1 (total: 1), TX uptime=12345 ms
[TX_MGR] Time data updated: uptime=12345 ms, unix_time=1739567890
```

### 6.2 Connection Loss Test

**Test Case 2.1: Power Cycle Transmitter**
```
1. Establish connection (both CONNECTED)
2. Power off transmitter
3. Wait 30 seconds
4. Verify receiver detects timeout → transitions to IDLE
5. Power on transmitter
6. Verify automatic reconnection
7. Verify both devices transition back to CONNECTED
8. Verify heartbeats resume
```

**Expected Behavior:**
```
RECEIVER (after TX power off):
T+0s:  Last heartbeat received
T+30s: [HEARTBEAT] Connection lost: No heartbeat for 30000 ms
       [CONN_MGR] CONNECTION_LOST → Transitioning to IDLE
       [RX_CONN] on_connection_lost() called
       
TRANSMITTER (after power on):
T+0s:  [CONN_MGR] CONNECTION_START → Transitioning to CONNECTING
T+1s:  [DISCOVERY] Restarting active channel hopping for reconnection
T+13s: [MSG_HANDLER] Receiver connected via ACK
T+14s: [CONN_MGR] PEER_REGISTERED → Transitioning to CONNECTED
T+15s: [HEARTBEAT] Sent heartbeat seq=1

RECEIVER (after TX reconnects):
T+13s: [RX_CONN] PROBE received
T+14s: [CONN_MGR] PEER_REGISTERED → Transitioning to CONNECTED
T+15s: [HEARTBEAT] Received heartbeat seq=1
```

**Test Case 2.2: Network Interference**
```
1. Establish connection
2. Place metal barrier between devices
3. Verify connection loss detection
4. Remove barrier
5. Verify automatic reconnection
```

### 6.3 State Machine Stress Test

**Test Case 3.1: Rapid Connect/Disconnect**
```
1. Establish connection
2. Repeat 10 times:
   a. Power off transmitter for 5 seconds
   b. Power on transmitter
   c. Wait for reconnection
   d. Verify CONNECTED state achieved
   e. Wait 10 seconds
3. Verify no memory leaks
4. Verify no zombie tasks
5. Verify no stale peers
```

### 6.4 Channel Hopping Test

**Test Case 4.1: Multi-Channel Discovery**
```
1. Configure receiver on channel 1
2. Power on transmitter → verify discovery on channel 1
3. Power cycle both devices
4. Configure receiver on channel 13
5. Power on transmitter → verify discovery on channel 13 (worst case: 13s delay)
6. Verify no channel mismatch errors
```

**Test Case 4.2: Channel Lock Persistence**
```
1. Establish connection on channel 11
2. Verify g_lock_channel == 11
3. Simulate connection loss
4. Verify channel unlocked for rediscovery
5. Verify reconnection works on different channel
```

---

## 7. Implementation Priority

### Phase 1: Critical Fixes (Immediate - 1-2 days)

1. **Connection Manager State Callbacks** ✅ HIGH
   - Add `register_state_callback()` method
   - Implement notification on state transitions
   - **Impact:** Enables automatic reconnection

2. **Discovery Task Reconnection** ✅ HIGH
   - Implement `restart_active_channel_hopping()`
   - Register for CONNECTION_LOST callback
   - **Impact:** Fixes stuck connection after loss

3. **Heartbeat System MAC Source** ✅ HIGH
   - Use `connection_manager.get_peer_mac()` instead of global
   - **Impact:** Eliminates race condition

4. **Channel Manager** ✅ MEDIUM
   - Implement centralized channel control
   - Add mutex protection
   - **Impact:** Prevents channel mismatch errors

### Phase 2: Robustness (3-5 days)

5. **CONNECTING Timeout** ✅ MEDIUM
   - Add 15s timeout for CONNECTING state
   - Auto-transition to IDLE if timeout
   - **Impact:** Prevents stuck in CONNECTING

6. **Peer Cleanup on Connection Loss** ✅ MEDIUM
   - Remove stale peers when transitioning to IDLE
   - **Impact:** Clean reconnection state

7. **Heartbeat Grace Period** ✅ LOW
   - Add 15s grace period for first heartbeat
   - **Impact:** Catches heartbeat failures earlier

### Phase 3: Optimization (5-7 days)

8. **Discovery ACK Polling** ✅ LOW
   - Replace fixed 1s delay with 10ms polling loop
   - **Impact:** Faster discovery (potential 900ms improvement)

9. **State Machine Consolidation** ✅ LOW
   - Eliminate `RecoveryState` in favor of connection manager
   - **Impact:** Reduces code complexity

10. **Comprehensive Logging** ✅ LOW
    - Add debug logs for all state transitions
    - **Impact:** Easier troubleshooting

---

## 8. Risk Assessment

### High Risk Areas

1. **Global Variable Dependencies**
   - `receiver_mac` used in multiple places without synchronization
   - **Mitigation:** Move to connection manager singleton

2. **Channel Management**
   - Multiple threads can change WiFi channel simultaneously
   - **Mitigation:** ChannelManager with mutex protection

3. **Peer Registration Timing**
   - Peers can be registered in wrong state
   - **Mitigation:** Strict state checking before add_peer()

### Medium Risk Areas

4. **State Machine Event Ordering**
   - Events can arrive out of order (queue delays)
   - **Mitigation:** State validation in event handlers

5. **Memory Leaks on Reconnection**
   - Peer structs may not be freed properly
   - **Mitigation:** Explicit cleanup in CONNECTION_LOST handler

### Low Risk Areas

6. **Heartbeat Sequence Wrapping**
   - uint32_t sequence can wrap after ~49 days
   - **Mitigation:** Already handled with `seq < last_seq` check

7. **Discovery Queue Overflow**
   - High message rate could fill queue
   - **Mitigation:** Queue size = 10 (sufficient for discovery)

---

## 9. Conclusion

The ESP-NOW implementation has **fundamental architectural issues** that prevent reliable reconnection. The primary problems are:

1. **Fragmented state management** - Multiple systems tracking connection state inconsistently
2. **Missing reconnection logic** - Connection loss → IDLE with no automatic recovery
3. **Global variable dependencies** - `receiver_mac` not synchronized with state machine
4. **Channel management chaos** - No coordination between WiFi channel changes
5. **Timing race conditions** - Peer registration, discovery, and state transitions can conflict

**The good news:** The core components (state machine, heartbeat, discovery) are well-designed. The issues are **integration problems**, not fundamental design flaws.

**Recommended Approach:**
1. Implement **Phase 1 fixes first** (connection callbacks + reconnection logic)
2. Test thoroughly with connection loss scenarios
3. Proceed to **Phase 2** (timeouts + cleanup)
4. Validate with stress testing
5. Optimize with **Phase 3** improvements

**Estimated Timeline:**
- Phase 1: 2 days
- Testing: 1 day
- Phase 2: 3 days
- Testing: 1 day
- Phase 3: 5 days
- Final testing: 2 days

**Total: ~14 days for complete implementation and validation**

---

## Appendix A: Code Files Reviewed

### Common Libraries (esp32common)
- `espnow_common_utils/connection_manager.h` (221 lines)
- `espnow_common_utils/connection_manager.cpp` (209 lines)
- `espnow_common_utils/espnow_discovery.h`
- `espnow_common_utils/espnow_discovery.cpp`
- `espnow_transmitter/espnow_transmitter.cpp` (354 lines)

### Transmitter (ESPnowtransmitter2)
- `src/espnow/tx_connection_handler.cpp` (150 lines)
- `src/espnow/discovery_task.cpp` (581 lines)
- `src/espnow/heartbeat_manager.cpp` (143 lines)
- `src/espnow/message_handler.cpp` (1155 lines)

### Receiver (espnowreciever_2)
- `src/espnow/rx_connection_handler.cpp` (181 lines)
- `src/espnow/rx_heartbeat_manager.cpp` (123 lines)
- `src/espnow/espnow_tasks.cpp` (898 lines)
- `lib/webserver/utils/transmitter_manager.cpp` (666 lines)

**Total Lines Analyzed:** ~4,680 lines of ESP-NOW code

---

*End of Comprehensive Review*
