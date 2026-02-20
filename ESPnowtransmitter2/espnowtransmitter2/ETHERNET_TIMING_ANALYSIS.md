# Ethernet Timing Analysis & State Machine Refactoring Proposal

**Date**: February 19, 2026  
**Device**: Olimex ESP32-POE-ISO (Transmitter)  
**Status**: Critical - Production Quality Improvement Required

---

## Executive Summary

Your transmitter firmware demonstrates **industry-level architecture patterns** (connection state machines for ESP-NOW, modular components, proper async task separation) but the **Ethernet initialization lacks equivalent sophistication**. The current design is:

- ✅ **GOOD**: Async initialization (non-blocking, correct ordering)
- ✅ **GOOD**: Proper event handlers (ARDUINO_EVENT_ETH_* callbacks)
- ❌ **PROBLEM**: No state machine (silent failures, no recovery)
- ❌ **PROBLEM**: No explicit connectivity verification
- ❌ **PROBLEM**: No visibility into initialization phases
- ❌ **PROBLEM**: No timing coordination with dependent services

**Result**: MQTT, OTA, and time sync remain disabled even after Ethernet connects due to timing race conditions and lack of state verification.

---

## Problem Analysis

### Issue #1: Silent Initialization Failures (Most Critical)

**Current Flow** (`main.cpp` lines 121-125):
```cpp
LOG_INFO("ETHERNET", "Initializing Ethernet...");
if (!EthernetManager::instance().init()) {
    LOG_ERROR("ETHERNET", "Ethernet initialization failed!");
}
// Continues execution regardless ← PROBLEM
```

**What Happens**:
- `EthernetManager::init()` returns `true` even if hardware init succeeds but DHCP/Static config fails
- Hardware reset + `ETH.begin()` succeeds → returns `true`
- `ETH.config()` (DHCP/Static setup) fails silently → return value not checked separately
- Main code assumes success and schedules dependent services

**Observed in Your Logs**:
```
[0d 00h 00m 03s] [info][ETH] IP Address: 192.168.1.40       ← ETH.begin() worked
[INFO][ETH] Ethernet initialization started (async)           ← Misleading message
[0d 00h 00m 04s] [info][ETH] ======================================================
[0d 00h 00m 06s] [WARN][ETHERNET] Ethernet not connected    ← Event handler shows disconnection
```

**Root Cause**: `EthernetManager::init()` at [ethernet_manager.cpp:56-108] does **not explicitly wait for or verify** `ARDUINO_EVENT_ETH_GOT_IP`. It returns immediately after calling `ETH.begin()`, which is async.

---

### Issue #2: Race Condition Between WiFi and Ethernet

**Current Sequence** (`main.cpp` lines 105-115):
```cpp
// Line 105-109: WiFi init FIRST
LOG_INFO("WIFI", "Initializing WiFi for ESP-NOW...");
WiFi.mode(WIFI_STA);
WiFi.disconnect();
delay(100);

// Line 121: Ethernet init SECOND
LOG_INFO("ETHERNET", "Initializing Ethernet...");
if (!EthernetManager::instance().init()) {
    LOG_ERROR("ETHERNET", "Ethernet initialization failed!");
}
```

**The Comments Mentioned**:
Your previous comments suggested:
- "Start WiFi BEFORE Ethernet" ✓ (Already done correctly)
- "Ensure WiFi is fully configured BEFORE starting Ethernet" ✓ (Already done correctly)

**Actual Problem** (not what comments suggested):
The 100ms delay after WiFi disconnect is **insufficient**. WiFi radio needs time to stabilize:
- `WiFi.mode(WIFI_STA)` = init radio subsystem
- `WiFi.disconnect()` = disable connectivity features
- 100ms = Radio not fully settled when Ethernet starts
- ESP-IDF may see both radio subsystems competing for resources

**Better Approach**:
```cpp
WiFi.mode(WIFI_STA);
WiFi.disconnect();
esp_wifi_stop();  // ← Explicitly stop WiFi radio after disconnect
delay(500);       // ← Longer delay allows radio to power down completely
```

---

### Issue #3: Dependent Services Start Before Ethernet Fully Ready

**Current Sequence** (`main.cpp` lines 294-305):
```cpp
// Line 121: Ethernet init (returns immediately, async)
if (!EthernetManager::instance().init()) {
    LOG_ERROR("ETHERNET", "Ethernet initialization failed!");
}

// ... [other inits: CAN, BMS, ESP-NOW, discovery] ...

// Line 294-305: MQTT/OTA start while Ethernet still connecting
if (EthernetManager::instance().is_connected()) {  // ← Returns FALSE here!
    LOG_INFO("ETHERNET", "Ethernet connected: %s", ...);
    OtaManager::instance().init_http_server();
    if (config::features::MQTT_ENABLED) {
        MqttManager::instance().init();
    }
} else {
    LOG_WARN("ETHERNET", "Ethernet not connected, network features disabled");  // ← You see this
}
```

**Timeline from Your Logs**:
```
[0d 00h 00m 04s] [info][ETH] Ethernet initialization started (async)
[0d 00h 00m 04s] [info][ETH] Link Speed: 100 Mbps                      ← CONNECTED event
[0d 00h 00m 06s] [WARN][ETHERNET] Ethernet not connected              ← Check fails here (2s later!)
[0d 00h 00m 06s] [info][DISCOVERY] Starting ACTIVE channel hopping
```

**What's Happening**:
1. t=0s: `EthernetManager::init()` returns immediately (line 121)
2. t=0s-4s: Hardware initializes, DHCP boots, IP arrives async
3. t=4s: `ARDUINO_EVENT_ETH_GOT_IP` fires → `connected_ = true` (inside event handler)
4. t=6s: Main code checks `is_connected()` → **Returns FALSE because event handler hasn't fired yet**

**The Real Problem**: There's a 2-second gap between when you check `is_connected()` and when the IP actually arrives!

---

### Issue #4: Duplicate Event Messages

**In Your Logs**:
```
[INFO][ETH] IP Address: 192.168.1.40
[INFO][ETH] Gateway: 192.168.1.1
[0d 00h 00m 04s] [info][ETH] Gateway: 192.168.1.1
[INFO][ETH] Link Speed: 100 Mbps
[0d 00h 00m 04s] [info][ETH] Link Speed: 100 Mbps
```

**Possible Causes**:
1. Event handler fired multiple times for same event (WiFi stack bug - unlikely)
2. Events being queued and processed with delay
3. ARDUINO_EVENT_ETH_GOT_IP firing multiple times (device picked up IP, lost it, got it again)

**Indicates**: **Ethernet connection is unstable** - link is flapping or getting re-assigned.

---

---

## CRITICAL: ESP-NOW State Machine Mismatch Analysis

### Current State Machine Implementations

Your codebase has **significant asymmetry** between transmitter and receiver ESP-NOW connection managers:

| Component | States | Implementation |
|-----------|--------|-----------------|
| **Transmitter ESP-NOW** | **17 states** | Full discovery, channel locking, reconnection |
| **Receiver ESP-NOW** | **10 states** | Simplified: passive listening, ACK response |
| **Ethernet (Current)** | **1 state** (bool) | No state machine |

### Transmitter States (17) - Why So Many?

**The 17-state transmitter machine** (`src/espnow/transmitter_connection_manager.h:22-44`):
```cpp
enum class EspNowConnectionState : uint8_t {
    // Initialization (2 states)
    UNINITIALIZED = 0,           // Initial state before ESP-NOW init
    INITIALIZING = 1,            // ESP-NOW being initialized
    
    // Discovery (4 states)
    IDLE = 2,                    // Ready but no peer
    DISCOVERING = 3,             // Broadcasting PROBE messages
    WAITING_FOR_ACK = 4,         // Waiting for receiver ACK
    ACK_RECEIVED = 5,            // ACK received, preparing to register
    
    // Channel Locking (4 states) ← CRITICAL: Prevents race condition
    CHANNEL_TRANSITION = 6,      // Switching to receiver's channel
    PEER_REGISTRATION = 7,       // Adding peer to ESP-NOW
    CHANNEL_STABILIZING = 8,     // Waiting for channel stability
    CHANNEL_LOCKED = 9,          // Channel locked and stable
    
    // Connected (2 states)
    CONNECTED = 10,              // Peer registered, ready to send
    DEGRADED = 11,               // Connected but poor quality
    
    // Disconnection (2 states)
    DISCONNECTING = 12,          // Graceful disconnect in progress
    DISCONNECTED = 13,           // Clean disconnect complete
    
    // Error/Recovery (3 states)
    CONNECTION_LOST = 14,        // Unexpected connection loss
    RECONNECTING = 15,           // Attempting to reconnect
    ERROR_STATE = 16             // Unrecoverable error
};
```

**Why Channel Locking Requires 4 States**:

ESP-NOW has a critical timing requirement: when the transmitter discovers the receiver's channel, it must:
1. Stop broadcasting on all channels (DISCOVERING → ACK_RECEIVED)
2. Switch to receiver's channel (CHANNEL_TRANSITION state)
3. Register the peer on that channel (PEER_REGISTRATION state)
4. Wait for hardware to stabilize (~400ms) (CHANNEL_STABILIZING state)
5. Confirm lock successful (CHANNEL_LOCKED state)

**Without these states**, the transmitter might:
- Attempt to register peer while still on wrong channel ❌
- Send data before peer registration complete ❌
- Have peer de-registered due to channel mismatch ❌
- Create intermittent connection failures ❌

These 4 states are **not optional** - they're a race condition prevention mechanism.

### Receiver States (10) - Simplified Version

**The 10-state receiver machine** (`src/espnow/receiver_connection_manager.h:20-35`):
```cpp
enum class ReceiverConnectionState : uint8_t {
    // Initialization (2 states)
    UNINITIALIZED = 0,           // Initial state before ESP-NOW init
    INITIALIZING = 1,            // ESP-NOW being initialized
    
    // Listening (3 states)
    LISTENING = 2,               // Waiting for PROBE from transmitter
    PROBE_RECEIVED = 3,          // PROBE received, preparing ACK
    SENDING_ACK = 4,             // Sending ACK to transmitter
    
    // Transmitter Lock Wait (1 state)
    TRANSMITTER_LOCKING = 5,     // Waiting for transmitter to lock (~450ms)
    
    // Connected (2 states)
    CONNECTED = 6,               // Transmitter registered, active connection
    DEGRADED = 7,                // Connected but poor quality
    
    // Disconnection (2 states)
    CONNECTION_LOST = 8,         // Transmitter lost (timeout)
    ERROR_STATE = 9              // Unrecoverable error
};
```

**Why Receiver Can Be Simpler** (Passive Design):
- Doesn't initiate discovery (waits for PROBE)
- Doesn't manage channel locking (transmitter does)
- Doesn't handle reconnection (just waits for next PROBE)
- Only responds, doesn't manage complex state sequences

### The Mismatch Problem

**Current Architecture**:
- Transmitter: Active, complex 17-state handshake
- Receiver: Passive, simplified 10-state response

**Result**: Code reads like **two different implementations** of the same protocol:
- ❌ Harder to maintain (two different state machines to understand)
- ❌ Harder to debug (transmitter has states receiver doesn't model)
- ❌ Harder to extend (changing protocol requires updating both differently)
- ❌ Inconsistent patterns (learning one doesn't help with the other)

### Analysis: Can the Transmitter Be Simplified to 10 States?

**Attempting to consolidate transmitter from 17 → 10 states**:

**Option 1: Merge Channel Locking (4 states → 1)**
```
Current: CHANNEL_TRANSITION → PEER_REGISTRATION → CHANNEL_STABILIZING → CHANNEL_LOCKED
Proposed: Single CHANNEL_LOCKING state
```
**Result**: ❌ **NOT FEASIBLE** - Would introduce race condition bugs
- Can't verify timing of each step
- Can't detect failures in specific phase
- Creates mysterious timeouts when one phase hangs

**Option 2: Merge Discovery (4 states → 2)**
```
Current: DISCOVERING → WAITING_FOR_ACK → ACK_RECEIVED
Proposed: Single DISCOVERING state (collapse ACK_RECEIVED into WAITING_FOR_ACK)
```
**Result**: ⚠️ **PARTIALLY FEASIBLE** - Would lose timeout granularity
- Can merge DISCOVERING and WAITING_FOR_ACK
- Can merge ACK_RECEIVED into immediate → CHANNEL_TRANSITION
- Cost: Harder to distinguish why discovery failed
- **Assessment**: Not recommended

**Option 3: Merge Disconnection (2 states → 1)**
```
Current: DISCONNECTING → DISCONNECTED
Proposed: Just DISCONNECTING (delete DISCONNECTED)
```
**Result**: ✅ **FEASIBLE** - Minor simplification
- DISCONNECTED state is rarely used
- Could go DISCONNECTING → IDLE directly
- Cost: Lose ability to track graceful vs forced disconnect
- **Assessment**: Minor improvement, not worth churn

**Conclusion**: **Transmitter cannot be safely simplified to 10 states without losing race condition protection.**

### Why the Receiver Doesn't Need Channel Locking States

**Receiver's perspective**:
1. LISTENING: Waiting for PROBE
2. PROBE_RECEIVED: Got PROBE, ready to ACK
3. SENDING_ACK: Sending ACK response
4. TRANSMITTER_LOCKING: **Receiver just waits** (one state - no active management)
5. CONNECTED: Ready to receive messages

**Transmitter's perspective**:
1. DISCOVERING: Actively broadcasting PROBE
2. WAITING_FOR_ACK: Waiting for response
3. ACK_RECEIVED: Got response, now MUST channel lock
4. CHANNEL_TRANSITION: Starting channel switch
5. PEER_REGISTRATION: Adding peer to ESP-NOW
6. CHANNEL_STABILIZING: Waiting for stability
7. CHANNEL_LOCKED: Confirmed stable
8. CONNECTED: Ready to send

**The asymmetry is intentional**: Transmitter actively manages complex sequence, receiver passively waits.

### Recommendation: Keep Transmitter at 17 States

**After detailed analysis**:
1. **Channel locking requires 4 distinct states** for race condition prevention
2. **Discovery phase could theoretically merge** but loses debugging granularity
3. **Current 17-state design is industry standard** for active peer discovery in mesh protocols
4. **Receiver's 10-state design is correct** for passive peer (not a "simplification" but different role)

**Better Approach**: Document the **WHY** and create a unified architecture guide explaining:
- Why transmitter has 17 states (active, complex handshake)
- Why receiver has 10 states (passive, simple response)
- How this aligns with mesh networking best practices
- When you might simplify (only discovery phase)

---

## Solution: Ethernet State Machine (Industry Grade)

### Architecture Pattern: Align with Your ESP-NOW Design

**Decision**: Match **Ethernet** state complexity to role:
- Ethernet is **active** (initiates connection, manages retries) → More complex like transmitter
- But simpler than ESP-NOW (no channel management) → **9 states, not 17**

**Proposed Ethernet State Machine** aligns with transmitter's active nature:
```cpp
enum class EthernetConnectionState : uint8_t {
    // Initialization (3 states - mimics transmitter's UNINITIALIZED/INITIALIZING/IDLE)
    UNINITIALIZED = 0,          // Power-on, before init()
    PHY_RESET = 1,              // Physical layer reset, ETH.begin()
    CONFIG_APPLYING = 2,        // Applying DHCP or static config
    
    // Connection Phase (2 states - mimics ACK_RECEIVED/CHANNEL_TRANSITION)
    LINK_ACQUIRING = 3,         // Waiting for physical link UP
    IP_ACQUIRING = 4,           // Waiting for IP assignment
    
    // Connected (1 state - like CONNECTED)
    CONNECTED = 5,              // Fully ready (link + IP + gateway)
    
    // Disconnection/Error (3 states - like CONNECTION_LOST/RECONNECTING/ERROR_STATE)
    LINK_LOST = 6,              // Link down, attempting recovery
    RECOVERING = 7,             // Retry sequence in progress
    ERROR_STATE = 8             // Unrecoverable failure
};
```

**Proposed**: Create equivalent **EthernetConnectionState** with appropriate phases for Ethernet:

```cpp
// PROPOSED: Ethernet Connection State Machine (9 states)
enum class EthernetConnectionState : uint8_t {
    // Initialization Phase
    UNINITIALIZED = 0,          // Power-on, before init()
    PHY_RESET = 1,              // Physical layer reset, ETH.begin()
    CONFIG_APPLYING = 2,        // Applying DHCP or static config
    
    // Connection Phase
    LINK_ACQUIRING = 3,         // Waiting for link UP (ARDUINO_EVENT_ETH_CONNECTED)
    IP_ACQUIRING = 4,           // Waiting for IP (ARDUINO_EVENT_ETH_GOT_IP)
    CONNECTED = 5,              // Fully ready (link + IP + gateway)
    
    // Disconnection/Error Phase
    LINK_LOST = 6,              // Link down, attempting recovery
    ERROR_STATE = 7,            // Unrecoverable failure (config error, hardware dead)
    RECOVERING = 8              // Retry sequence in progress
};
```

### Proposed EthernetManager Refactoring

**File**: `src/network/ethernet_manager.h`

**New Public Interface**:
```cpp
class EthernetManager {
public:
    // ... existing public methods ...
    
    // NEW: State Machine Interface (parallels ESP-NOW)
    
    /**
     * @brief Get current connection state
     * @return Current state enum value
     */
    EthernetConnectionState get_state() const { return current_state_; }
    
    /**
     * @brief Get human-readable state string
     * @return State name for logging
     */
    const char* get_state_string() const;
    
    /**
     * @brief Check if Ethernet is ready for network operations
     * 
     * More specific than is_connected():
     * - Requires both LINK and IP
     * - Returns false during initialization
     * @return true only in CONNECTED state
     */
    bool is_fully_ready() const { return current_state_ == EthernetConnectionState::CONNECTED; }
    
    /**
     * @brief Manual state update (call from main loop)
     * 
     * Allows waiting for specific states:
     * - Timeout detection (state stuck too long)
     * - Graceful degradation
     * - Logging of state transitions
     */
    void update_state_machine();
    
    /**
     * @brief Get milliseconds since last state change
     * @return Time in milliseconds
     */
    uint32_t get_state_age_ms() const;
    
    // NEW: Event Handlers Registration (for better testability)
    /**
     * @brief Register callback for state change events
     * @param callback Function to call on state change
     */
    void register_state_callback(std::function<void(EthernetConnectionState, EthernetConnectionState)> callback);
    
    // NEW: Diagnostics
    /**
     * @brief Get detailed connection diagnostics
     * @return JSON-formatted string with connection details
     */
    std::string get_diagnostics() const;
};
```

---

## Critical Services: Keep-Alive, NTP, MQTT, and Edge Cases

### Service Dependencies and Timing

Your transmitter has multiple services that depend on Ethernet reaching CONNECTED state:

| Service | Dependency | Requirement | Timing Impact |
|---------|-----------|-------------|---------------|
| **NTP (Time Sync)** | Ethernet IP | DNS working, UDP 123 open | Must sync before logging (RTC needed) |
| **MQTT** | Ethernet IP + NTP | Broker reachable, credentials valid | Telemetry backlog if delayed |
| **OTA** | Ethernet IP | HTTP server port open | Available for remote updates |
| **Battery Data** | CAN Bus | Independent of Ethernet | Continues if network fails |
| **Keep-Alive** | Both ESP-NOW + Ethernet | Heartbeat to receiver | Critical for connection stability |

### Proposed Timing Sequence with State Machine

```
INITIALIZATION PHASE (t=0 to t=30s):
┌─────────────────────────────────────────────────────────────────┐
│ T0: Ethernet in UNINITIALIZED                                   │
│     ↓ EthernetManager::init()                                   │
│ T0+100ms: Ethernet in PHY_RESET (ETH.begin() called)            │
│     ↓ Hardware initializing                                      │
│ T0+500ms: Ethernet in CONFIG_APPLYING (ETH.config() called)     │
│     ↓ DHCP boots, link negotiation                              │
│ T0+2s: Ethernet in LINK_ACQUIRING                               │
│     ↓ ARDUINO_EVENT_ETH_CONNECTED fired                         │
│ T0+4s: Ethernet in IP_ACQUIRING                                 │
│     ↓ DHCP response arrives                                     │
│ T0+5s: Ethernet in CONNECTED ← CRITICAL GATE POINT              │
│     ↓ All dependent services can initialize                     │
│ T0+5s: NTP sync begins (requires IP + DNS)                      │
│ T0+6s: MQTT connects (requires IP + resolved broker)            │
│ T0+6s: OTA server starts listening (requires IP)                │
│ T0+6s: Keep-Alive begins sending heartbeats                     │
└─────────────────────────────────────────────────────────────────┘

DISCONNECTION PHASE (Example: Cable unplugged):
┌─────────────────────────────────────────────────────────────────┐
│ T0: CONNECTED                                                    │
│ T0+500ms: Ethernet in LINK_LOST                                 │
│     ↓ Event handler detects link down                           │
│ T0+500ms: Keep-Alive stops (no bridge to receiver)              │
│ T0+1s: MQTT detects no response, starts retry                   │
│ T0+2s: Ethernet in RECOVERING (auto-retry sequence)             │
│ T0+30s: If no recovery, mark as ERROR_STATE                     │
│     ↓ Or recover and return to CONNECTED                        │
└─────────────────────────────────────────────────────────────────┘
```

### Keep-Alive Handler Updates

**Current keep-alive** (`src/network/keep_alive_manager.cpp`):
```cpp
// Current: Sends heartbeat every 5 seconds regardless
void KeepAliveManager::update() {
    uint32_t now = millis();
    if (now - last_heartbeat_time_ > HEARTBEAT_INTERVAL_MS) {
        send_heartbeat();
        last_heartbeat_time_ = now;
    }
}
```

**Proposed: Gate on Ethernet state**:
```cpp
void KeepAliveManager::update() {
    // NEW: Only send keep-alive if Ethernet is ready AND ESP-NOW is connected
    if (!EthernetManager::instance().is_fully_ready()) {
        // Ethernet disconnected - don't try to send bridged heartbeats
        // Keep-alive waits for recovery or local ESP-NOW timeout handles it
        return;
    }
    
    if (!TransmitterConnectionManager::instance().is_connected()) {
        // ESP-NOW disconnected - can't reach receiver anyway
        return;
    }
    
    uint32_t now = millis();
    if (now - last_heartbeat_time_ > HEARTBEAT_INTERVAL_MS) {
        send_heartbeat();
        last_heartbeat_time_ = now;
    }
}
```

### NTP Synchronization Handler

**New NTP lifecycle** (triggered when Ethernet reaches CONNECTED):
```cpp
class NtpManager {
private:
    enum class NtpState {
        UNINITIALIZED,
        WAITING_FOR_NETWORK,    // Ethernet not ready
        SYNCING,                // Actively requesting time from NTP server
        SYNCHRONIZED,           // Time set, periodic re-sync
        ERROR                   // NTP server unreachable
    };
    
    NtpState state_ = NtpState::UNINITIALIZED;
    
public:
    /**
     * @brief Called when Ethernet reaches CONNECTED state
     * Initiates NTP synchronization
     */
    void on_ethernet_connected() {
        LOG_INFO("NTP", "Ethernet ready, initiating time sync...");
        state_ = NtpState::SYNCING;
        
        // Set timezone (if not already set)
        configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
        
        // This will block briefly while syncing
        // In production, prefer async version or timeout
    }
    
    /**
     * @brief Called when Ethernet drops
     * Continues with cached time if available
     */
    void on_ethernet_disconnected() {
        LOG_WARN("NTP", "Ethernet disconnected, using cached system time");
        state_ = NtpState::WAITING_FOR_NETWORK;
        // System time continues running from local RTC
    }
    
    /**
     * @brief Periodic update for re-sync detection
     * Call from main loop every 60 seconds
     */
    void update() {
        if (state_ == NtpState::SYNCHRONIZED) {
            // Periodic re-sync every 24 hours
            static uint32_t last_sync_time = 0;
            uint32_t now = millis();
            
            if (now - last_sync_time > 86400000) {  // 24 hours
                on_ethernet_connected();
                last_sync_time = now;
            }
        }
    }
};
```

### MQTT Connection Recovery

**New MQTT lifecycle**:
```cpp
class MqttManager {
private:
    enum class MqttState {
        UNINITIALIZED,
        WAITING_FOR_NETWORK,    // Ethernet not ready
        CONNECTING,             // Trying to connect to broker
        CONNECTED,              // Connected and publishing
        DISCONNECTED,           // Lost connection, waiting for retry
        ERROR                   // Broker unreachable
    };
    
public:
    /**
     * @brief Called when Ethernet reaches CONNECTED state
     * Initiates MQTT connection attempt
     */
    void on_ethernet_connected() {
        LOG_INFO("MQTT", "Ethernet ready, connecting to broker %s:%d...",
                 config::mqtt::BROKER_HOST, config::mqtt::BROKER_PORT);
        
        if (!client_.connect(config::mqtt::CLIENT_ID,
                            config::mqtt::USERNAME,
                            config::mqtt::PASSWORD)) {
            LOG_ERROR("MQTT", "Failed to connect to broker");
            state_ = MqttState::ERROR;
            return;
        }
        
        state_ = MqttState::CONNECTED;
        subscribe_to_topics();
    }
    
    /**
     * @brief Called when Ethernet drops
     * Cleanly disconnects without re-attempting
     */
    void on_ethernet_disconnected() {
        LOG_WARN("MQTT", "Ethernet disconnected, stopping MQTT");
        
        if (client_.connected()) {
            client_.disconnect();
        }
        
        state_ = MqttState::WAITING_FOR_NETWORK;
    }
    
    /**
     * @brief Periodic update (call from main loop every 1 second)
     * Maintains connection and retries if needed
     */
    void update() {
        if (state_ == MqttState::CONNECTED && client_.connected()) {
            client_.loop();  // Process incoming messages
        }
    }
};
```

### Edge Case Handling

**Edge Case 1: Ethernet recovers while MQTT is disconnected**
```
Solution: Ethernet LINK_LOST → RECOVERING → CONNECTED automatically
triggers on_ethernet_connected() → MQTT reconnects
```

**Edge Case 2: NTP sync takes too long (> 5 seconds)**
```
Solution: Set timeout on configTime():
configTime(0, 0, "pool.ntp.org", "time.nist.gov");
// If timeout after 5s, continue with system time
// Retry in background every 24 hours
```

**Edge Case 3: DHCP server slow (takes 10+ seconds)**
```
Solution: Handled automatically by state machine
- LINK_ACQUIRING state: Waiting for link (usually <1s)
- IP_ACQUIRING state: Waiting for IP (can be 5-30s depending on server)
- Timeout protection: If > 30s, mark as ERROR_STATE
- Can recover: EVENT: `if (auto_retry_) recover_connection();`
```

**Edge Case 4: Gateway unreachable but IP assigned**
```
Solution: NTP will timeout, MQTT will timeout
- Both have their own timeout/retry logic
- Ethernet stays in CONNECTED (has IP)
- Main loop can detect MQTT/NTP failures via status checks
- Manual recovery: Unplug/replug ethernet
```

**Edge Case 5: Static IP configuration wrong**
```
Solution: Handled in CONFIG_APPLYING state
- If ETH.config() fails or times out
- Detected in update_state_machine() timeout handler
- Transition to ERROR_STATE after 30s
- User must fix config and power cycle
```

**Edge Case 6: Keep-alive flooding if Ethernet unstable (flapping)**
```
Solution: Add debounce to state transitions
- Don't trigger on_ethernet_connected() on first CONNECTED event
- Wait 2 seconds in CONNECTED state before starting services
- Prevents rapid connect/disconnect cycles from restarting everything

Implementation:
```cpp
void update_state_machine() {
    // ... existing code ...
    
    if (current_state_ == CONNECTED && !services_initialized_) {
        uint32_t time_in_connected = millis() - state_enter_time_ms_;
        
        if (time_in_connected >= 2000) {  // 2 second debounce
            // Now safe to trigger on_ethernet_connected() callbacks
            trigger_callbacks();
            services_initialized_ = true;
        }
    }
}
```

---

## Implementation Plan: Production-Grade Refactoring

### Phase 1: Create State Machine Infrastructure

**File**: `src/network/ethernet_state_machine.h` (NEW)

```cpp
#pragma once
#include <cstdint>
#include <Arduino.h>

enum class EthernetConnectionState : uint8_t {
    UNINITIALIZED = 0,
    PHY_RESET = 1,
    CONFIG_APPLYING = 2,
    LINK_ACQUIRING = 3,
    IP_ACQUIRING = 4,
    CONNECTED = 5,
    LINK_LOST = 6,
    ERROR_STATE = 7,
    RECOVERING = 8
};

struct EthernetStateMetrics {
    uint32_t phy_reset_time_ms;           // Time spent in PHY_RESET
    uint32_t config_apply_time_ms;        // Time spent in CONFIG_APPLYING
    uint32_t link_acquire_time_ms;        // Time spent in LINK_ACQUIRING
    uint32_t ip_acquire_time_ms;          // Time spent in IP_ACQUIRING
    uint32_t total_initialization_ms;     // Total init time
    
    uint32_t state_transitions;           // How many times state changed
    uint32_t recoveries_attempted;        // How many times tried to recover
    uint32_t link_flaps;                  // How many times link dropped
};
```

### Phase 2: Modify EthernetManager

**File**: `src/network/ethernet_manager.h` - Add to private section:

```cpp
private:
    EthernetConnectionState current_state_{EthernetConnectionState::UNINITIALIZED};
    EthernetConnectionState previous_state_{EthernetConnectionState::UNINITIALIZED};
    
    uint32_t state_enter_time_ms_{0};
    uint32_t last_link_time_ms_{0};
    uint32_t last_ip_time_ms_{0};
    
    EthernetStateMetrics metrics_;
    
    // State machine update logic
    void update_state_machine();
    void check_state_timeout();
    void handle_state_timeout();
    
    // Event handler now updates state (not just connected_ bool)
    static void event_handler(WiFiEvent_t event);
    
    // Callback system
    std::vector<std::function<void(EthernetConnectionState, EthernetConnectionState)>> state_callbacks_;
```

### Phase 3: Modify main.cpp Initialization Sequence

**Current** (main.cpp lines 105-125):
```cpp
LOG_INFO("WIFI", "Initializing WiFi for ESP-NOW...");
WiFi.mode(WIFI_STA);
WiFi.disconnect();
delay(100);

LOG_INFO("ETHERNET", "Initializing Ethernet...");
if (!EthernetManager::instance().init()) {
    LOG_ERROR("ETHERNET", "Ethernet initialization failed!");
}
```

**Proposed** (Industry Grade):
```cpp
// === SECTION 1: WiFi Radio Initialization (ESP-NOW requirement) ===
LOG_INFO("WIFI", "Initializing WiFi radio for ESP-NOW...");
WiFi.mode(WIFI_STA);
WiFi.disconnect();
esp_wifi_stop();  // ← Explicitly power down WiFi radio
delay(500);       // ← Allow radio to fully stabilize (ESP-IDF best practice)

uint8_t mac[6];
WiFi.macAddress(mac);
LOG_DEBUG("WIFI", "WiFi MAC: %02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

// === SECTION 2: Ethernet Initialization (Begins async) ===
LOG_INFO("ETHERNET", "Initializing Ethernet driver...");
if (!EthernetManager::instance().init()) {
    LOG_ERROR("ETHERNET", "Ethernet initialization failed - hardware error!");
    // For now continue, but flag this as critical for diagnostics
}

// === SECTION 3: Wait for Ethernet to reach CONNECTED state ===
// This is the KEY CHANGE: We now explicitly WAIT before continuing
LOG_INFO("ETHERNET", "Waiting for Ethernet connection (timeout: 30s)...");
uint32_t ethernet_timeout_ms = 30000;  // 30 second timeout
uint32_t ethernet_wait_start = millis();

while (true) {
    EthernetManager::instance().update_state_machine();  // Update state
    
    EthernetConnectionState eth_state = EthernetManager::instance().get_state();
    
    if (eth_state == EthernetConnectionState::CONNECTED) {
        LOG_INFO("ETHERNET", "✓ Connected: %s, Gateway: %s, Link Speed: %d Mbps",
                 EthernetManager::instance().get_local_ip().toString().c_str(),
                 EthernetManager::instance().get_gateway_ip().toString().c_str(),
                 ETH.linkSpeed());
        break;  // Ethernet ready, proceed
    }
    
    uint32_t elapsed = millis() - ethernet_wait_start;
    if (elapsed > ethernet_timeout_ms) {
        LOG_WARN("ETHERNET", "Timeout waiting for Ethernet (30s elapsed)");
        LOG_WARN("ETHERNET", "Current state: %s", EthernetManager::instance().get_state_string());
        LOG_WARN("ETHERNET", "Continuing with partial functionality (no MQTT/OTA/NTP)");
        // Note: Ethernet may still connect later, recovery built in below
        break;
    }
    
    // Log progress every 5 seconds during wait
    if (elapsed % 5000 == 0) {
        LOG_INFO("ETHERNET", "Waiting... state=%s, elapsed=%lu ms",
                 EthernetManager::instance().get_state_string(),
                 elapsed);
    }
    
    delay(100);  // Small delay to prevent busy-waiting
}

// ... rest of initialization ...
```

### Phase 4: Modify MQTT/OTA Startup (Lines 294-305)

**Current**:
```cpp
if (EthernetManager::instance().is_connected()) {
    LOG_INFO("ETHERNET", "Ethernet connected: %s", ...);
    OtaManager::instance().init_http_server();
    if (config::features::MQTT_ENABLED) {
        MqttManager::instance().init();
    }
} else {
    LOG_WARN("ETHERNET", "Ethernet not connected, network features disabled");
}
```

**Proposed** (Clearer state distinction):
```cpp
// Check if Ethernet is FULLY READY (not just "initialized")
if (EthernetManager::instance().is_fully_ready()) {
    LOG_INFO("ETHERNET", "✓ Network ready: IP=%s, Gateway=%s, DNS=%s",
             EthernetManager::instance().get_local_ip().toString().c_str(),
             EthernetManager::instance().get_gateway_ip().toString().c_str(),
             ETH.dnsIP().toString().c_str());
    
    // Initialize OTA
    LOG_INFO("OTA", "Starting OTA server...");
    OtaManager::instance().init_http_server();
    
    // Initialize MQTT
    if (config::features::MQTT_ENABLED) {
        LOG_INFO("MQTT", "Starting MQTT client...");
        MqttManager::instance().init();
    }
} else {
    LOG_WARN("ETHERNET", "Network features delayed (state: %s)",
             EthernetManager::instance().get_state_string());
    LOG_INFO("ETHERNET", "Will retry MQTT/OTA when Ethernet fully connected");
}
```

### Phase 5: Add State Machine Update to Main Loop

**In `loop()` function**:
```cpp
void loop() {
    // ... existing code ...
    
    // NEW: Update Ethernet state machine (lightweight, called frequently)
    static uint32_t last_ethernet_check = 0;
    uint32_t now = millis();
    
    if (now - last_ethernet_check > 1000) {  // Every 1 second
        EthernetManager::instance().update_state_machine();
        
        // Optionally: Log state transitions
        static EthernetConnectionState last_logged_state = EthernetConnectionState::UNINITIALIZED;
        EthernetConnectionState current_state = EthernetManager::instance().get_state();
        
        if (current_state != last_logged_state) {
            LOG_INFO("ETHERNET", "State transition: %s → %s",
                     EthernetManager::instance().get_state_string(),
                     state_to_string(current_state));
            last_logged_state = current_state;
            
            // NEW: If Ethernet recovers after being offline, restart dependent services
            if (current_state == EthernetConnectionState::CONNECTED &&
                last_logged_state != EthernetConnectionState::CONNECTED) {
                LOG_INFO("ETHERNET", "Connection recovered - reinitializing MQTT/OTA");
                if (config::features::MQTT_ENABLED) {
                    MqttManager::instance().reconnect();
                }
            }
        }
        
        last_ethernet_check = now;
    }
    
    // ... rest of loop ...
}
```

---

## Expected Improvements

### Before (Current Implementation):
```
[0d 00h 00m 04s] [info][ETH] Ethernet initialization started (async)
[0d 00h 00m 04s] [info][ETH] Link Speed: 100 Mbps
[0d 00h 00m 06s] [WARN][ETHERNET] Ethernet not connected, network features disabled
   ↑
   Race condition: Checked before IP arrived
   
[MQTT not initialized]
[OTA not initialized]
```

### After (Proposed State Machine):
```
[0d 00h 00m 00s] [info][ETHERNET] Initializing Ethernet driver...
[0d 00h 00m 00s] [info][ETHERNET] WiFi radio stabilized (500ms)
[0d 00h 00m 00s] [info][ETHERNET] State: UNINITIALIZED → PHY_RESET
[0d 00h 00m 00s] [info][ETHERNET] Waiting for Ethernet connection (timeout: 30s)...
[0d 00h 00m 01s] [info][ETHERNET] State: PHY_RESET → CONFIG_APPLYING
[0d 00h 00m 02s] [info][ETHERNET] State: CONFIG_APPLYING → LINK_ACQUIRING
[0d 00h 00m 04s] [info][ETHERNET] State: LINK_ACQUIRING → IP_ACQUIRING
[0d 00h 00m 05s] [info][ETHERNET] ✓ State: IP_ACQUIRING → CONNECTED
[0d 00h 00m 05s] [info][ETHERNET] ✓ Connected: 192.168.1.40, Gateway: 192.168.1.1, Link Speed: 100 Mbps
[0d 00h 00m 05s] [info][OTA] Starting OTA server...
[0d 00h 00m 05s] [info][MQTT] Starting MQTT client...
   ↑
   Clear progression, no race conditions
```

---

## Implementation Complexity & Effort Estimate

| Component | Effort | Priority |
|-----------|--------|----------|
| Create `ethernet_state_machine.h` | 30 min | HIGH |
| Refactor `EthernetManager` (add state tracking) | 1-2 hrs | HIGH |
| Update `main.cpp` initialization | 45 min | HIGH |
| Add state update to `loop()` | 15 min | MEDIUM |
| Add recovery logic (reconnect MQTT) | 30 min | MEDIUM |
| Add diagnostics/logging methods | 30 min | LOW |
| **Total** | **4-5 hours** | - |

**Testing Effort**:
- Power cycle tests (5 cycles minimum)
- Network disconnect/reconnect scenarios
- Static IP vs DHCP transitions
- Timeout edge cases (DHCP server slow, bad network)
- Integration with MQTT/OTA

---

## Industry Grade Checklist

- [ ] **State Machine Pattern**: Matches your ESP-NOW design
- [ ] **Explicit Waiting**: No more race conditions (wait for actual connection)
- [ ] **Timeout Handling**: 30s timeout prevents infinite hangs
- [ ] **Recovery Logic**: Auto-restart MQTT if Ethernet recovers
- [ ] **State Visibility**: Every transition logged for debugging
- [ ] **Metrics**: Track initialization timing for production monitoring
- [ ] **Callback System**: Allows other components to react to state changes
- [ ] **Graceful Degradation**: System continues if Ethernet unavailable
- [ ] **Thread-Safe Event Handling**: Event handler updates state atomically
- [ ] **Comprehensive Logging**: Diagnostics for troubleshooting

---

## Recommendations for Implementation

### 1. Start with Minimal State Machine (Quick Win - 1-2 hours)

**Option A - Lightweight** (Recommended first pass):
- Create `ethernet_state_machine.h` with enum only
- Add 2 new members to `EthernetManager`: `current_state_` and `state_enter_time_ms_`
- Update event handler to set state on events
- Add `get_state()`, `get_state_string()`, `is_fully_ready()` methods
- Modify `main.cpp` to wait for CONNECTED state before starting MQTT/OTA

**Benefits**:
- Minimal disruption to existing code
- Clear improvement in logging and diagnostics
- Fixes the race condition immediately
- Prepare foundation for more sophisticated state handling later

### 2. Expand to Full State Machine (Full Implementation - 3-4 hours)

After Option A is working:
- Add complete state transitions (RECOVERING, LINK_LOST, etc.)
- Add timeout detection in `update_state_machine()`
- Add metrics tracking
- Add recovery callbacks
- Add diagnostics JSON endpoint

### 3. Testing Strategy

**Phase 1 - Functional Testing** (1 hour):
```
1. Power cycle device 5 times - verify ethernet reaches CONNECTED state
2. Unplug ethernet cable - verify state transitions to LINK_LOST
3. Replug ethernet cable - verify recovery to CONNECTED
4. Check MQTT/OTA status after recovery
```

**Phase 2 - Edge Cases** (1-2 hours):
```
5. Slow DHCP server (artificially delay DHCP response)
6. Network cable loose (intermittent)
7. Gateway unreachable (DNS server working, gateway down)
8. WiFi interference during initialization
```

**Phase 3 - Integration** (1 hour):
```
9. Verify ESP-NOW still works independently of Ethernet
10. Verify MQTT telemetry publishes immediately when ready
11. Verify OTA web server is accessible when Ethernet ready
```

---

## Why This Is Production Grade

1. **Pattern Consistency**: Matches your existing ESP-NOW state machine (17 states) - no new paradigms to learn
2. **Explicit Verification**: No silent failures - every state transition logged
3. **Timeout Protection**: 30s timeout prevents system hanging if Ethernet hardware fails
4. **Recovery Capability**: Auto-restart MQTT when Ethernet recovers (no manual reboot)
5. **Observability**: State transitions and timing visible in logs for debugging
6. **Scalability**: Easy to add more states if needed (e.g., DNS failure detection)
7. **Thread Safety**: Event handler uses atomic operations (inherited from esp-idf)
8. **Documentation**: State machine self-documenting via enum values
9. **Testing Friendly**: Easy to inject failures and verify recovery
10. **Performance**: Negligible overhead (few microseconds per state check)

---

## References

- **Your ESP-NOW Implementation**: `src/espnow/transmitter_connection_manager.h` - excellent pattern to follow
- **Your Event Handling**: `src/network/ethernet_manager.cpp` event_handler - already well-structured
- **Your Main Loop**: `src/main.cpp` - good organization, just needs explicit Ethernet wait

---

## Next Steps

1. **Review this analysis** - confirm alignment with your architectural goals
2. **Choose implementation approach** (Lightweight vs Full)
3. **Create feature branch** for refactoring
4. **Implement Phase 1** (state machine enum + minimal tracking)
5. **Test power cycles and basic scenarios**
6. **Merge to main** when verified stable
7. **Plan Phase 2** (full state machine with recovery) for next sprint

---

## Questions & Discussion Points

1. Should we add a **watchdog timer** to auto-reboot if Ethernet stuck for >2 minutes?
2. Should we implement **MQTT reconnection** automatically when Ethernet recovers?
3. Should we expose **state transitions** as an event that other components can subscribe to?
4. Should we add **static IP validation** as part of the state machine (verify gateway reachable)?
5. Should we log Ethernet diagnostics periodically (every 5 minutes) for production monitoring?

---

**This analysis is ready for implementation. All recommendations follow industry best practices and align with your existing codebase architecture.**
