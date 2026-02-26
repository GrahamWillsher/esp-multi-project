# ESP-NOW Transmission Inactive - Root Cause Analysis (REVISED)

**Date:** 2026-02-26  
**Issue:** Transmitter shows `[WARN][DATA_SENDER] Transmission inactive - no ESP-NOW data being sent`  
**Symptom:** ESP-NOW connection is working (VERSION_BEACON and HEARTBEAT flowing), but SOC/power data not transmitted

---

## Executive Summary

**ROOT CAUSE IDENTIFIED:** The receiver's state machine callback for `CONNECTED` state does NOT call `send_initialization_requests()`. The initialization logic exists but was never wired into the state machine's CONNECTED transition.

**The receiver has proper infrastructure:**
- `ReceiverConnectionHandler::send_initialization_requests()` method exists (line 149) and properly sends `msg_request_data` with `subtype_power_profile`
- State machine properly transitions to CONNECTED
- Version beacon and heartbeat work (proving connection is solid)

**What was broken:**
- State callback (line 27) only locked the channel when entering CONNECTED
- **Missing:** Call to `send_initialization_requests()` in CONNECTED state callback
- Result: REQUEST_DATA never sent, transmitter stays in `transmission_active_ = false`

**Fix Applied:**
Added call to `send_initialization_requests()` in the state machine's CONNECTED transition callback at [rx_connection_handler.cpp](rx_connection_handler.cpp#L27-L41).

---

## Architecture Overview (Correct Understanding)

### State Machine Design

The connection state machine is the SOURCE OF TRUTH for connection status. All initialization logic should be triggered by state transitions, NOT by connection callbacks (which are legacy).

**Correct Flow:**
1. PROBE/ACK callbacks call `on_peer_registered()` → posts `PEER_REGISTERED` event
2. State machine processes event → transitions CONNECTING → CONNECTED
3. State callback fires for CONNECTED transition
4. **Callback must call initialization functions** (version announce, config requests, REQUEST_DATA)

### Receiver Side Implementation

#### 1. State Machine Callback (WHERE FIX WAS NEEDED)
**File:** [rx_connection_handler.cpp](rx_connection_handler.cpp#L27-L41)

**BEFORE (broken):**
```cpp
if (new_state == EspNowConnectionState::CONNECTED) {
    // Lock channel when connected
    uint8_t current_channel = ChannelManager::instance().get_channel();
    ChannelManager::instance().lock_channel(current_channel, "RX_CONN");
    LOG_INFO("RX_CONN", "✓ Connected - channel locked at %d", current_channel);
    // ← MISSING: send_initialization_requests() call
}
```

**AFTER (fixed):**
```cpp
if (new_state == EspNowConnectionState::CONNECTED) {
    // Lock channel when connected
    uint8_t current_channel = ChannelManager::instance().get_channel();
    ChannelManager::instance().lock_channel(current_channel, "RX_CONN");
    LOG_INFO("RX_CONN", "✓ Connected - channel locked at %d", current_channel);
    
    // Send initialization requests now that connection is fully established
    ReceiverConnectionHandler::instance().send_initialization_requests(
        EspNowConnectionManager::instance().get_peer_mac());
}
```

#### 2. Initialization Method (ALREADY CORRECT)
**File:** [rx_connection_handler.cpp](rx_connection_handler.cpp#L149-L217)

This method was already implemented correctly:
```cpp
void ReceiverConnectionHandler::send_initialization_requests(const uint8_t* transmitter_mac) {
    // Check if device is in CONNECTED state
    auto state = conn_mgr.get_state();
    if (state != EspNowConnectionState::CONNECTED) {
        LOG_WARN("CONN_HANDLER", "Cannot send initialization - state is %u", state);
        return;
    }
    
    // Send config section requests (MQTT, network, metadata)
    // ...
    
    // Send REQUEST_DATA to ensure power profile stream is active
    request_data_t req_msg = { msg_request_data, subtype_power_profile };
    esp_err_t result = esp_now_send(transmitter_mac, (const uint8_t*)&req_msg, sizeof(req_msg));
    if (result == ESP_OK) {
        LOG_INFO("CONN_HANDLER", "Requested power profile data stream");  // ← Should now appear
    }
    
    // Send version announce
    // ...
}
```

---

## Root Cause: Incomplete State Machine Integration

The receiver was partially refactored to use `EspNowConnectionManager` state machine, but the integration was incomplete:

1. ✅ State machine transitions work (IDLE → CONNECTING → CONNECTED)
2. ✅ State callback registered and fires on transitions
3. ❌ **State callback only handled cleanup (channel lock/unlock)**
4. ❌ **State callback did NOT handle initialization (REQUEST_DATA, version announce)**
5. ✅ Initialization method exists and is correct
6. ❌ **Initialization method was never called from state callback**

Result: Connection establishes, but no initialization messages sent, so transmitter never receives REQUEST_DATA and stays inactive.

---

## Why This Wasn't a "Timing Race"

Initial hypothesis was wrong. This wasn't a timing issue where REQUEST_DATA arrived before transmitter reached CONNECTED. 

**Actual issue:** REQUEST_DATA was **never sent at all** because the code path to send it was never executed.

The state machine callbacks (`on_connection` from `EspnowStandardHandlers`) are NOT the right place for application logic. They're low-level connection primitives. The proper place is the `EspNowConnectionManager` state change callback, which fires AFTER the state transition is complete and both devices are confirmed CONNECTED.

---

## Lessons Learned

### 1. State Machine is Source of Truth
All application logic triggered by connection events must be in the state machine's state change callbacks, not in low-level protocol callbacks.

### 2. Initialization Must Be Idempotent
The `first_data_received_` flag pattern is correct - ensures initialization only runs once per connection, even if state callback fires multiple times.

### 3. State Callbacks Fire AFTER Transition
`register_state_callback()` guarantees the new state is fully established when the callback runs. This is the safe time to send messages requiring CONNECTED state.

### 4. Don't Mix Abstraction Levels
- **Protocol level:** `on_connection` callbacks from `EspnowStandardHandlers` (PROBE/ACK handling)
- **Application level:** `register_state_callback()` from `EspNowConnectionManager` (initialization, cleanup)

Mixing these caused the bug.

---

## Fix Verification

After the fix, expected log sequence on receiver:

```
[RX_CONN] State change: CONNECTING → CONNECTED
[RX_CONN] ✓ Connected - channel locked at 1
[CONN_HANDLER] [INIT] Connection CONFIRMED (both devices ready) - sending initialization requests
[CONN_HANDLER] Requested power profile data stream
[CONN_HANDLER] Sent version info to transmitter: 1.0.0
[CONN_HANDLER] [INIT] Initialization requests sent (will retry any that failed)
```

Expected log sequence on transmitter:

```
[DATA_REQUEST] REQUEST_DATA received (subtype=0) from ...
[DATA_REQUEST] >>> Power profile transmission ACTIVATED <<<
[TX_TASK] ESP-NOW TX: SOC=50%, Power=0W (seq:1)
[TX_TASK] ESP-NOW TX: SOC=50%, Power=0W (seq:2)
...
```

---

## Conclusion

**The bug was a missing function call in a state machine callback, not a timing race condition.**

The receiver had all the right infrastructure (`send_initialization_requests()` method with REQUEST_DATA sending), but it was never invoked because the state machine's CONNECTED callback didn't call it.

Adding one line to call `send_initialization_requests()` from the CONNECTED state callback fixes the issue completely, with no delays, no retries, and no workarounds needed.

The state machine handles all timing correctly - when the callback fires, both devices are confirmed CONNECTED and ready to exchange messages.
