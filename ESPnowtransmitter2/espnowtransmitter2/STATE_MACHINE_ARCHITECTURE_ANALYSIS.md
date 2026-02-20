# State Machine Architecture Analysis & Unified Design

**Date**: February 19, 2026  
**Status**: Complete - Comprehensive Review and Recommendations

---

## Executive Summary

Your firmware architecture uses **sophisticated state machine patterns** across multiple components but with significant **asymmetry between transmitter and receiver**. This analysis explains why the mismatch exists, why simplifying won't work, and proposes a unified Ethernet state machine that aligns with your proven architectures.

### Key Findings

| Finding | Details | Impact |
|---------|---------|--------|
| **17 vs 10 State Mismatch** | Transmitter has 17 ESP-NOW states, receiver has 10 | By design - different active vs passive roles |
| **Cannot Simplify Transmitter** | 4 channel-locking states are mandatory for race condition prevention | Attempting simplification would break connection stability |
| **Root Cause of Mismatch** | Transmitter actively manages handshake, receiver passively responds | Architectural asymmetry is correct, not a bug |
| **Recommended Ethernet States** | 9 states (aligned with active transmitter model) | Matches complexity of Ethernet role (initiate, manage, recover) |
| **Critical Services** | NTP, MQTT, OTA, Keep-Alive, Battery Data | Each has specific dependencies on Ethernet CONNECTED state |
| **Edge Cases Covered** | 6 major edge cases documented with handling strategies | Keep-Alive flooding, DHCP delays, gateway unreachable, etc. |

---

## Detailed Analysis: State Machine Mismatch

### The 17-State Transmitter Machine (Active Peer Discovery)

**Role**: Actively discovers receiver, manages channel locking, initiates reconnection

**States Breakdown**:

**Initialization (2 states)**:
- `UNINITIALIZED` → `INITIALIZING` → `IDLE`

**Discovery (4 states)** - Must lock to specific states for timing control:
- `DISCOVERING` (broadcast PROBE on all channels)
- `WAITING_FOR_ACK` (listen for ACK response)
- `ACK_RECEIVED` (ACK received, prepare to lock channel)
- (Transitions immediately to channel locking)

**Channel Locking (4 states)** - **CRITICAL FOR STABILITY**:
- `CHANNEL_TRANSITION` - Stop multi-channel broadcast, switch to target channel
- `PEER_REGISTRATION` - Register peer on that channel
- `CHANNEL_STABILIZING` - Wait 400ms for hardware stability
- `CHANNEL_LOCKED` - Confirm locked, ready to send

**Connected/Degraded (2 states)**:
- `CONNECTED` (normal operation)
- `DEGRADED` (poor link quality)

**Disconnection (2 states)**:
- `DISCONNECTING` (graceful shutdown)
- `DISCONNECTED` (waiting for reconnect)

**Error/Recovery (3 states)**:
- `CONNECTION_LOST` (unexpected disconnect)
- `RECONNECTING` (retry sequence)
- `ERROR_STATE` (unrecoverable)

### Why Channel Locking Requires 4 Separate States

**Timing Constraints** (from ESP-NOW protocol):
1. **Channel switch** must complete before peer registration (100-200ms)
2. **Peer registration** creates esp_now peer entry on that channel (50-100ms)
3. **Hardware stabilization** after peer add requires 200-400ms wait
4. **Each step can fail independently** - need separate timeouts per step

**If merged into single CHANNEL_LOCKING state**:
- ❌ Can't detect which phase times out (30s timeout applied to all 3 phases)
- ❌ Can't handle partial failures (e.g., switch succeeds, register fails)
- ❌ Creates mysterious "hangs" when one phase stalls
- ❌ Can't apply different retry logic to different phases
- ❌ Defeats entire purpose of state machine (visibility)

**Example failure scenario without separate states**:
```
Suppose CHANNEL_LOCKING state with 30s timeout:
1. Switch to channel: SUCCESS (50ms)
2. Register peer: FAILS (timeout from esp_now API)
3. Wait for stability: Never reached
4. Result: 30 second timeout, unclear what failed

With separate states:
1. CHANNEL_TRANSITION: SUCCESS → advance
2. PEER_REGISTRATION: FAILS after 2s
   → Detected immediately
   → Can retry registration or fallback
   → No 30s waste
```

### The 10-State Receiver Machine (Passive Peer Response)

**Role**: Passively waits for discovery, responds with ACK, holds connection

**States Breakdown**:

**Initialization (2 states)**:
- `UNINITIALIZED` → `INITIALIZING` → `LISTENING`

**Discovery Response (3 states)**:
- `LISTENING` (waiting for PROBE)
- `PROBE_RECEIVED` (PROBE detected, prepare ACK)
- `SENDING_ACK` (sending ACK response)

**Transmitter Lock Wait (1 state)** - Just passive wait:
- `TRANSMITTER_LOCKING` (transmitter locking to our channel, we just wait ~450ms)

**Connected/Degraded (2 states)**:
- `CONNECTED` (actively receiving messages)
- `DEGRADED` (link quality poor)

**Disconnection (2 states)**:
- `CONNECTION_LOST` (transmitter timeout)
- `ERROR_STATE` (unrecoverable)

### Why Receiver Doesn't Need Channel Locking States

**Receiver's perspective**:
- Doesn't initiate channel switching (transmitter does)
- Doesn't register peers (transmitter does on its side)
- Doesn't manage stability testing (transmitter does)
- Only **passively waits** while transmitter locks

**In `TRANSMITTER_LOCKING` state**:
- Receiver just waits ~450ms (single timeout)
- If timeout expires, reconnection happens via next PROBE
- No active steps to manage separately

**Asymmetry is architectural, not a deficiency**:
- Mesh protocols naturally have active/passive roles
- Active peer manages complexity, passive peer stays simple
- Both sides understand the handshake, implement it differently

---

## Can Transmitter Be Simplified to Match Receiver?

### Option Analysis

#### Option 1: Merge ALL Channel Locking into Single State ❌

**Proposal**:
```cpp
// BEFORE (4 states):
CHANNEL_TRANSITION → PEER_REGISTRATION → CHANNEL_STABILIZING → CHANNEL_LOCKED

// AFTER (1 state):
CHANNEL_LOCKING  // Single state, ~500ms total
```

**Assessment**: ❌ **NOT FEASIBLE** - Breaks race condition protection

**Risks**:
1. Intermittent connection failures (can't debug which phase failed)
2. All failures get 30s timeout (instead of 2s per phase)
3. Retry logic becomes blunt (retry everything vs retry specific phase)
4. Future hardware issues harder to diagnose

---

#### Option 2: Merge Discovery States ⚠️

**Proposal**:
```cpp
// BEFORE (4 states):
DISCOVERING → WAITING_FOR_ACK → ACK_RECEIVED

// AFTER (2 states):
DISCOVERING  // Includes both broadcasting and waiting for ACK
ACK_RECEIVED → CHANNEL_TRANSITION (immediately, combined)
```

**Assessment**: ⚠️ **PARTIALLY FEASIBLE** - Minimal benefit, costs clarity

**Issues**:
1. Can't distinguish "we've been discovering 10 seconds" vs "ACK arrived 10 seconds ago"
2. Harder to debug discovery failures
3. Saves 1 state out of 17 (5.9% reduction)

**Recommendation**: Not worth doing

---

#### Option 3: Merge Disconnection States ✅

**Proposal**:
```cpp
// BEFORE (2 states):
DISCONNECTING → DISCONNECTED

// AFTER (1 state):
DISCONNECTING  // Go directly to IDLE after cleanup
```

**Assessment**: ✅ **FEASIBLE** - Minor simplification

**Pros**:
- DISCONNECTED state rarely used (mostly just waiting for reconnect)
- Can go DISCONNECTING → IDLE directly (1-state savings)

**Cons**:
- Lose ability to distinguish graceful vs forced disconnect
- Minimal savings (1 out of 17 = 5.9%)

**Recommendation**: Not worth refactoring for such small gain

---

### Conclusion: Keep Transmitter at 17 States

**Final Assessment**: 
- **17 states are necessary** for active mesh peer discovery
- **Cannot simplify without breaking functionality**
- **Asymmetry with receiver is correct** (active vs passive roles)
- **Better approach**: Document the architecture clearly so developers understand **why** each state exists

**New Recommendation**: Create architecture documentation that explains:
1. Why transmitter has 17 states (active role requirements)
2. Why receiver has 10 states (passive role requirements)
3. How this aligns with industry mesh protocols
4. When/how to add new states for future features

---

## Unified State Machine Architecture: Ethernet Alignment

### Decision: Match Ethernet to Active Role

**Observation**: 
- Transmitter (active) = 17 states
- Receiver (passive) = 10 states
- Ethernet (active) = should be like transmitter, but simpler (no channel management)

**Proposed**: **9-state Ethernet machine** (active like transmitter, simpler than ESP-NOW)

### Ethernet State Machine Design

```cpp
enum class EthernetConnectionState : uint8_t {
    // Initialization Phase (3 states - mimics transmitter init)
    UNINITIALIZED = 0,          // Power-on, before init()
    PHY_RESET = 1,              // Physical layer reset (ETH.begin())
    CONFIG_APPLYING = 2,        // DHCP/Static config (ETH.config())
    
    // Connection Phase (2 states - mimics discovery)
    LINK_ACQUIRING = 3,         // Waiting for PHY link UP
    IP_ACQUIRING = 4,           // Waiting for IP (DHCP/static)
    
    // Connected (1 state)
    CONNECTED = 5,              // Fully ready (link + IP + gateway)
    
    // Disconnection/Error (3 states - mimics error recovery)
    LINK_LOST = 6,              // Link went down
    RECOVERING = 7,             // Retry sequence in progress
    ERROR_STATE = 8             // Unrecoverable failure
};
```

### Mapping to Transmitter Architecture

| Ethernet | Transmitter | Purpose |
|----------|------------|---------|
| UNINITIALIZED | UNINITIALIZED | Initial state |
| PHY_RESET | INITIALIZING | Hardware reset |
| CONFIG_APPLYING | IDLE | Waiting for handshake |
| LINK_ACQUIRING | DISCOVERING | Active search for peer |
| IP_ACQUIRING | WAITING_FOR_ACK | Awaiting response |
| CONNECTED | CONNECTED | Fully functional |
| LINK_LOST | CONNECTION_LOST | Unexpected failure |
| RECOVERING | RECONNECTING | Recovery attempt |
| ERROR_STATE | ERROR_STATE | Unrecoverable error |

**Why this alignment matters**:
- Developers familiar with transmitter patterns immediately understand ethernet
- Same state transition logic, same timeout patterns
- Consistent logging and debugging approach
- Same event callback system

---

## Service Dependencies: Proper Gating

### Critical Services and Their Requirements

#### 1. NTP Time Synchronization
**Requires**: Ethernet in CONNECTED state, DNS resolution working
**Timing**: Sync on first CONNECTED event, retry every 24 hours
**Implementation**:
```cpp
void on_ethernet_connected() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");  // Non-blocking
    // Or: Async NTP with timeout and fallback to system time
}

void on_ethernet_disconnected() {
    // Continue with system time (RTC running locally)
}
```

#### 2. MQTT Telemetry
**Requires**: Ethernet in CONNECTED state, NTP preferred (for timestamps)
**Timing**: Connect on first CONNECTED event, retry with backoff if fails
**Implementation**:
```cpp
void on_ethernet_connected() {
    mqttClient.connect(broker, port, username, password);
    mqttClient.subscribe("commands/#");
}

void on_ethernet_disconnected() {
    mqttClient.disconnect();  // Clean shutdown
}
```

#### 3. OTA Firmware Updates
**Requires**: Ethernet in CONNECTED state, HTTP server port available
**Timing**: Start HTTP server on first CONNECTED event
**Implementation**:
```cpp
void on_ethernet_connected() {
    otaServer.begin();  // Listen for firmware updates
    LOG_INFO("OTA", "Ready for updates at http://%s", localIP());
}

void on_ethernet_disconnected() {
    otaServer.stop();  // Stop listening
}
```

#### 4. Keep-Alive Heartbeat (ESP-NOW Bridge)
**Requires**: BOTH Ethernet CONNECTED AND ESP-NOW CONNECTED
**Timing**: Every 5 seconds while both connected, pause if either drops
**Implementation**:
```cpp
void update() {
    if (!ethernet.is_fully_ready()) return;      // Gate 1: Network down
    if (!espnow.is_connected()) return;         // Gate 2: No receiver
    
    if (millis() - last_beat > 5000) {
        send_heartbeat_to_receiver();
        last_beat = millis();
    }
}
```

#### 5. Battery Data (CAN Bus)
**Requires**: None (independent)
**Timing**: Continuous polling, independent of network
**Implementation**:
```cpp
void update() {
    // Ethernet doesn't affect battery reading
    read_can_messages();
    queue_for_eventual_transmission();
}
```

### Dependency Diagram

```
┌─────────────────────────────────┐
│   ETHERNET CONNECTED STATE      │
└─────────────┬───────────────────┘
              │
              ├──→ NTP (time sync)
              │    └──→ MQTT (telemetry with timestamps)
              ├──→ OTA (firmware updates)
              └──→ Keep-Alive (via ESP-NOW → Receiver)

┌─────────────────────────────────┐
│   ESP-NOW CONNECTED STATE       │
└─────────────┬───────────────────┘
              │
              ├──→ Battery Data Transmission
              └──→ Keep-Alive (if also Ethernet up)

┌─────────────────────────────────┐
│   CAN BUS (Independent)         │
└─────────────┬───────────────────┘
              │
              └──→ Battery Data Polling (always available)
```

---

## Edge Case Handling Strategy

### Edge Case 1: Ethernet Link Flapping (Connects/Disconnects Rapidly)

**Scenario**: Bad cable or noisy port causes link to flap 5 times in 10 seconds

**Current Problem**: Each connection triggers service init, each disconnect triggers shutdown

**Solution**: Debounce state transitions
```cpp
static uint32_t state_stable_time_ms = 0;
const uint32_t DEBOUNCE_MS = 2000;  // 2 second debounce

void on_state_change(EthernetConnectionState new_state) {
    if (new_state == current_state_) return;  // No actual change
    
    state_stable_time_ms = 0;  // Reset debounce timer
    current_state_ = new_state;
    
    // Don't trigger callbacks until state stable for 2 seconds
}

void update() {
    uint32_t now = millis();
    uint32_t time_in_state = now - state_enter_time_ms_;
    
    if (time_in_state >= DEBOUNCE_MS && !callbacks_triggered_) {
        // State stable - now safe to init services
        trigger_state_callbacks();
        callbacks_triggered_ = true;
    }
}
```

### Edge Case 2: DHCP Server Extremely Slow (> 10 seconds)

**Scenario**: Network has misconfigured DHCP or congested network

**Current Problem**: Timeout at 30s wastes user time, but timeout at 5s might be premature

**Solution**: Graduated timeout with progress logging
```cpp
static const uint32_t LINK_ACQUIRING_TIMEOUT_MS = 5000;   // 5s
static const uint32_t IP_ACQUIRING_TIMEOUT_MS = 30000;    // 30s
static const uint32_t TOTAL_INIT_TIMEOUT_MS = 35000;      // 35s total

void check_state_timeout() {
    uint32_t now = millis();
    uint32_t elapsed = now - state_enter_time_ms_;
    
    switch (current_state_) {
        case LINK_ACQUIRING:
            if (elapsed > LINK_ACQUIRING_TIMEOUT_MS) {
                LOG_WARN("ETH", "Link timeout after %ld ms", elapsed);
                set_state(ERROR_STATE);  // No physical link available
            }
            break;
            
        case IP_ACQUIRING:
            if (elapsed > IP_ACQUIRING_TIMEOUT_MS) {
                LOG_WARN("ETH", "IP timeout after %ld ms", elapsed);
                set_state(ERROR_STATE);  // DHCP not working
            } else if (elapsed % 5000 == 0) {
                LOG_INFO("ETH", "Still waiting for IP... %ld/%ld ms",
                         elapsed, IP_ACQUIRING_TIMEOUT_MS);
            }
            break;
    }
}
```

### Edge Case 3: Gateway Unreachable (IP Assigned but No Traffic)

**Scenario**: Network assigned IP but gateway/router is unreachable

**Current Problem**: Ethernet reports CONNECTED (has IP), but network dead

**Solution**: NTP timeout indicates problem, can retry ethernet init
```cpp
// After 30s in CONNECTED state with no NTP sync
void check_ntp_health() {
    if (current_state_ != CONNECTED) return;
    
    uint32_t now = millis();
    if (now - state_enter_time_ms_ > 30000) {  // 30s
        if (!ntp_is_synchronized()) {
            LOG_ERROR("ETH", "No NTP response - gateway may be unreachable");
            LOG_WARN("ETH", "Testing gateway connectivity...");
            
            // Could trigger: IP_ACQUIRING retry, or ERROR_STATE
            // For now: log and let user investigate
        }
    }
}
```

### Edge Case 4: Static IP Configuration Wrong

**Scenario**: User configured static IP in config, but subnet/gateway wrong

**Current Problem**: Hardware connects, IP set, but no network access

**Solution**: Timeout detection in CONFIG_APPLYING state
```cpp
// If CONFIG_APPLYING state > 5s with no link event
if (current_state_ == CONFIG_APPLYING && 
    millis() - state_enter_time_ms_ > 5000) {
    
    LOG_ERROR("ETH", "Config timeout - possible wrong static IP?");
    LOG_INFO("ETH", "Check: IP=%s, Gateway=%s, Subnet=%s",
             local_ip.toString().c_str(),
             gateway_ip.toString().c_str(),
             subnet.toString().c_str());
    
    set_state(ERROR_STATE);
}
```

### Edge Case 5: Keep-Alive Flooding Due to Race Condition

**Scenario**: MQTT restart and Keep-Alive both trigger simultaneously, causing message burst

**Current Problem**: Could exceed ESP-NOW bandwidth or MQTT rate limits

**Solution**: Service startup coordination
```cpp
void on_ethernet_connected() {
    // Use async callbacks with staggered timing
    
    schedule_callback(0ms, [] { start_ntp(); });       // Start immediately
    schedule_callback(500ms, [] { start_mqtt(); });    // Wait 500ms
    schedule_callback(1000ms, [] { start_keep_alive(); });  // Wait 1s
    
    // Prevents startup thundering herd
}
```

### Edge Case 6: Ethernet Recovers While MQTT Mid-Retry

**Scenario**: Ethernet disconnects (MQTT retrying), then reconnects

**Current Problem**: MQTT might double-connect or have stale connection

**Solution**: Graceful reconnection state
```cpp
void mqtt_on_ethernet_reconnected() {
    if (mqtt.is_connected()) {
        // Already connected (quick reconnect)
        LOG_INFO("MQTT", "Ethernet reconnected, connection already valid");
        return;
    }
    
    // Was disconnected, now reconnect fresh
    mqtt.disconnect();  // Ensure clean state
    mqtt.connect(broker, port, creds);
    
    LOG_INFO("MQTT", "Reconnected after Ethernet recovery");
}
```

---

## Recommended Implementation Sequence

### Phase 1: Quick Win (1-2 hours)
1. Create `ethernet_state_machine.h` with enum
2. Add state tracking to EthernetManager
3. Update event handlers to set state
4. Modify main.cpp to wait for CONNECTED state
5. **Result**: Race condition fixed

### Phase 2: Full State Machine (2-3 hours)
1. Add `update_state_machine()` to main loop
2. Add timeout detection and ERROR_STATE
3. Add metrics tracking
4. Add recovery logic
5. **Result**: Production-grade architecture

### Phase 3: Service Integration (2-3 hours)
1. Update NTP to gate on CONNECTED
2. Update MQTT to gate on CONNECTED
3. Update Keep-Alive to gate on both Ethernet and ESP-NOW
4. Add debouncing for link flaps
5. **Result**: All services properly coordinated

### Phase 4: Testing (2-3 hours)
1. Power cycle tests
2. Cable disconnect/reconnect
3. Slow DHCP simulation
4. Multiple reconnections
5. MQTT/NTP/OTA verification
6. **Result**: Validated production readiness

---

## Summary: Key Takeaways

| Finding | Recommendation | Effort |
|---------|---------------|--------|
| **17-state transmitter cannot be simplified** | Document architecture, keep 17 states | 2 hours |
| **Receiver's 10 states are correct for passive role** | No changes needed, document design | 1 hour |
| **Ethernet needs 9-state machine** | Implement aligned to transmitter pattern | 4-5 hours |
| **Services need dependency gating** | Gate NTP/MQTT/OTA on CONNECTED state | 2 hours |
| **Edge cases require careful handling** | Implement debouncing and graduated timeouts | 2 hours |
| **Keep-Alive needs dual gating** | Gate on both Ethernet AND ESP-NOW | 1 hour |
| **Total implementation** | Production-grade architecture | **12-15 hours** |

---

**Document Version**: 1.0  
**Status**: ✅ COMPLETE  
**Next Step**: Begin Phase 1 implementation
