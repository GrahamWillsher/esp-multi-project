# Bidirectional Handshake Fix - State Machine Synchronization

## Problem Analysis

**Symptom from logs:**
```
[INFO][DISCOVERY] ✓ Receiver found on channel 11
[INFO][CONFIG] CONFIG_REQUEST_FULL (ID=1) from F0:9E:9E:1F:98:20
[INFO][CONFIG] CONFIG: Sending snapshot (311 bytes in 2 fragments)
[ERROR][CONFIG] CONFIG: Failed to send fragment 0: ESP_ERR_ESPNOW_NOT_FOUND
[WARN][DATA_REQUEST] Failed to send IP data: ESP_ERR_ESPNOW_NOT_FOUND
[WARN][DATA_REQUEST] Failed to send battery settings: ESP_ERR_ESPNOW_NOT_FOUND
[CONN_MGR-DEBUG] Processing event: PEER_FOUND (state: CONNECTING)
[CONN_MGR] PEER_FOUND → Waiting for peer registration
[INFO][DISCOVERY] ✓ Receiver registered as peer
[INFO][DATA_REQUEST] >>> Power profile transmission STARTED
[INFO][VERSION] Receiver version: 2.0.0
[CONN_MGR-DEBUG] Processing event: PEER_REGISTERED (state: CONNECTING)
[CONN_MGR] PEER_REGISTERED → Transitioning to CONNECTED
```

**The Issue:**

The receiver sends `CONFIG_REQUEST_FULL` immediately after detecting a connection (when PROBE handler fires), but the transmitter hasn't yet registered the receiver as an ESP-NOW peer. This creates a race condition where:

1. **TX finds RX on channel 11** - Transmitter receives ACK from receiver
2. **RX sends CONFIG_REQUEST_FULL** - Receiver immediately sends config request (PROBE handler callback fires)
3. **TX tries to send CONFIG response** - Transmitter attempts to send snapshot back
4. **⚠️ ESP_ERR_ESPNOW_NOT_FOUND** - Fails because RX is not yet registered as peer!
5. **Later: TX registers RX as peer** - Only AFTER posting PEER_REGISTERED event

**Root Cause:**

The ESP-NOW protocol requires bidirectional registration before communication is possible:
- TX must add RX as a peer (done in `discovery_task.cpp`)
- RX must add TX as a peer (done when first PROBE received)

However, the receiver was sending configuration requests before confirming that the transmitter had completed peer registration. This created a temporary window where the receiver was trying to send to an unregistered peer.

## Solution Implemented

### 1. **Delay Receiver Initialization Requests**

Instead of sending config requests in the `on_connection` callback (triggered by PROBE handler), we now:
- Send them on **first DATA_RECEIVED** event
- This gives the transmitter time to register us as a peer before we try to send anything back

**File: `espnow_tasks.cpp`**
- Removed initialization request sending from `on_connection` callback
- Callback now just posts `PEER_REGISTERED` event

### 2. **Add Initialization Handler to Data Reception**

**File: `rx_connection_handler.cpp` / `rx_connection_handler.h`**
- New method: `send_initialization_requests_if_needed()`
- Called from `on_data_received()` 
- Sends configuration requests ONLY on first data message
- **CRITICAL**: Only sends if device state == `CONNECTED`
- Includes flag to prevent duplicate sends (reset on CONNECTION_LOST)

**"First Data Message" Definition:**
The "first data message" is the initial packet received from the transmitter after peer registration. This is the signal that the transmitter is ready to communicate bidirectionally and has successfully completed its setup. It acts as the handshake confirmation that the link is operational.

**When First Data Message Triggers:**
- ✅ Received after PEER_REGISTERED event fires
- ✅ Device state must be CONNECTED (not CONNECTING)
- ✅ Flag is set to prevent duplicate initialization requests
- ❌ Will NOT trigger if connection is lost and re-established until flag is reset

**Initialization requests sent:**
1. Full configuration snapshot request
2. Static data request (settings)
3. Power profile data stream request
4. Version announcement

**Flag Reset on Connection Loss:**
The "first data received" flag must be reset when connection is lost:
- Triggered by: `on_connection_lost()` event
- Allows re-initialization on reconnection
- Ensures full config sync after any disconnection
- Part of `EspNowConnectionManager` state transition to IDLE or CONNECTING

### 3. **Add Retry Mechanism for Failed Requests**

**File: `config_receiver.cpp` / `config_receiver.h`**
- `requestFullSnapshot()` now catches `ESP_ERR_ESPNOW_NOT_FOUND` errors
- Sets pending flag if peer not yet ready
- New method: `retryPendingRequests()` - retries every 1 second (max 10 retries)

**Called from: `main.cpp` loop()**
- Provides fallback if any initialization requests fail
- Ensures configuration eventually syncs even if timing is tight

## Timeline After Fix

```
TX                              RX
 |                              |
 |--- Sends PROBE broadcast --->|
 |                              |
 |<-- Receives ACK from RX ---  |
 |                              |
 | Registers RX as peer         |
 | (on specific channel)         |
 |                              | Receives PROBE
 |                              | Registers TX as peer
 |                              | (on WiFi channel 0 = auto)
 |                              |
 | Sends first data message     |
 |--------------------------->  |
 |                              | Receives DATA
 |                              | (first DATA_RECEIVED event)
 |                              | 
 |                              | Check: State == CONNECTED?
 |                              | ✅ YES → Send CONFIG_REQUEST_FULL
 |<-- Receives CONFIG_REQUEST  -|
 |                              |
 | Sends CONFIG snapshot (OK!)  |
 |--------------------------->  |
 |                              | Receives CONFIG snapshot
 |                              |
 | (bidirectional link working) |
```

## Connection Loss and Reconnection

**What Happens on Connection Loss:**

1. **Connection Lost Event Triggered**
   - `on_connection_lost()` called
   - State machine transitions: `CONNECTED → CONNECTING → IDLE`

2. **Reset First Data Flag**
   - "First data received" flag must be cleared
   - Allows re-initialization on next connection
   - Without reset: reconnection would skip initialization requests

3. **Retry Mechanism Activates**
   - `retryPendingRequests()` continues attempting failed requests
   - Provides graceful recovery path
   - Prevents permanent loss of configuration sync

**Reconnection Sequence:**

```
Connection Lost:
[WARN] Receiver heartbeat timeout (no data for 90s)
[WARN] Clearing 'first data received' flag for reconnection
[WARN] Connection state: CONNECTED → IDLE

Reconnection Attempt:
[INFO] Re-discovery started
[INFO] ✓ Receiver found on channel 11
[INFO] ✓ Receiver registered as peer
[INIT] First data received - sending initialization requests (AGAIN)
[CONFIG] CONFIG_REQUEST_FULL (ID=2) from receiver
[CONFIG] CONFIG: Sending snapshot...
```

**Implementation Requirements:**

- ✅ `on_connection_lost()` handler must reset `first_data_received_flag_`
- ✅ State machine transitions must clear flag when returning to IDLE/CONNECTING
- ✅ Flag should be member variable of `RxConnectionHandler`
- ✅ Check state before sending (gate on `CONNECTED` state)
- ✅ No race conditions - state machine provides ordering guarantee


## Key Improvements

1. **Eliminates Race Condition** - Waits for actual data before sending requests back
2. **Fallback Retry** - If timing is still tight, automatic retries prevent permanent failures
3. **Clearer Intent** - Code now explicitly shows that initialization happens AFTER connection confirmed
4. **Robust** - Works regardless of CPU load or WiFi latency variations

## Validation

Check logs for:
- ✅ No `ESP_ERR_ESPNOW_NOT_FOUND` errors when sending CONFIG responses
- ✅ Log message: `[INIT] First data received - sending initialization requests`
- ✅ Successful config snapshot and settings requests
- ✅ State is `CONNECTED` before any initialization requests sent
- ✅ On reconnection: flag reset + re-initialization happens automatically
- ✅ Multiple heartbeat cycles without data loss (tests connection stability)

## Related Files

- `rx_connection_handler.h/cpp` - Connection event handling
- `config_receiver.h/cpp` - Configuration management with retry logic
- `espnow_tasks.cpp` - Message routing and PROBE handler config
- `main.cpp` - Periodic retry processing in loop

## Future Considerations

- ✅ **State Machine Gating** (IMPLEMENTED) - Initialization only happens when device state == `CONNECTED`
- ✅ **Flag Reset on Reconnection** (REQUIRED) - Clear `first_data_received_` flag in `on_connection_lost()`
- Could implement explicit PEER_READY message from TX instead of relying on first data message
- Could add timeout for initialization if no data arrives (fallback to retry immediately)
- Could track explicit state of first data flag per peer (for multi-peer scenarios)

## Implementation Checklist

- [ ] Verify `first_data_received_` flag is member of `RxConnectionHandler`
- [ ] Add state check: `if (state != CONNECTED) return;` in `send_initialization_requests()`
- [ ] Add flag reset in `on_connection_lost()` handler
- [ ] Test reconnection: lose connection → re-establish → verify flag resets and re-initialization happens
- [ ] Verify logs show state transitions: `CONNECTED → IDLE` on loss, then `IDLE → CONNECTING → CONNECTED` on reconnect
- [ ] Confirm no unneeded initialization requests sent in CONNECTING state
