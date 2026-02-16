# ESP-NOW Heartbeat Protocol - Debugging Guide

## Current Implementation Status

### Transmitter Side (ESPnowtransmitter2)
- **HeartbeatManager** (`src/espnow/heartbeat_manager.{h,cpp}`)
  - Sends heartbeat every 10 seconds (HEARTBEAT_INTERVAL_MS)
  - Only sends when connection state is CONNECTED
  - Uses global `receiver_mac` (from espnow_transmitter.cpp)
  - Tracks sequence numbers and unacked count
  - Detects connection loss after 3 consecutive unacked heartbeats
  - Called from `loop()` with `HeartbeatManager::instance().tick()`

### Receiver Side (espnowreciever_2)
- **RxHeartbeatManager** (`src/espnow/rx_heartbeat_manager.{h,cpp}`)
  - Receives heartbeat messages via message router
  - Validates CRC16 on all heartbeats
  - Sends heartbeat ACK back to transmitter
  - Tracks last received time and counts
  - Implements 90-second timeout (only checked after first heartbeat)
  - Called from `loop()` with `RxHeartbeatManager::instance().tick()`
  - Handler registered in `espnow_tasks.cpp` line 247-252

## Expected Log Output

### Transmitter Initialization
```
[INFO][HEARTBEAT] Heartbeat manager initialized (interval: 10000 ms)
```

Or if receiver MAC already discovered:
```
[INFO][HEARTBEAT] Initialized with receiver_mac: 5C:01:3B:53:2F:18
```

Or if receiver MAC not yet discovered:
```
[WARN][HEARTBEAT] Initialized but receiver_mac is BROADCAST - waiting for discovery
```

### Transmitter Sending Heartbeats
After connection established and ~10 seconds pass:
```
[DEBUG][HEARTBEAT] Interval elapsed - sending heartbeat to 5C:01:3B:53:2F:18
[DEBUG][HEARTBEAT] Sent heartbeat seq=1, uptime=5432 ms
```

Or if receiver_mac still broadcast:
```
[WARN][HEARTBEAT] Cannot send heartbeat - receiver MAC not yet discovered (broadcast)
```

### Receiver Heartbeat Reception
```
[INFO][HEARTBEAT] Received heartbeat seq=1 (total: 1), TX uptime=5432 ms, TX state=2
[DEBUG][HEARTBEAT] Sent ACK seq=1 to 5C:01:3B:53:2F:18
```

### Transmitter Receiving ACK
```
[DEBUG][HEARTBEAT] Received ACK seq=1 (prev=0), RX uptime=2100 ms, RX state=2
```

## Troubleshooting Checklist

### 1. Heartbeat Not Sending (Transmitter)
**Symptom:** Only init message, no "Interval elapsed" or "Sent heartbeat" messages

**Check:**
- [ ] Connection state is CONNECTED
  ```
  Look for: [CONN_MGR-DEBUG] state: CONNECTED
  ```
  
- [ ] receiver_mac is discovered (not broadcast)
  - If seeing: `[WARN][HEARTBEAT] Cannot send heartbeat - receiver MAC not yet discovered`
  - This means PROBE/ACK exchange hasn't completed
  - Check if PROBE/ACK messages are being exchanged
  
- [ ] 10 seconds have elapsed since initialization
  - First heartbeat may take up to 10 seconds after connection established

### 2. Heartbeat Not Received (Receiver)
**Symptom:** Transmitter logs show "Sent heartbeat" but receiver doesn't log reception

**Check:**
- [ ] Receiver message handler registered
  - Look for: `Registered Nheartbeat message routes` in receiver logs
  
- [ ] ESP-NOW queue is processing messages
  - Look for other messages being logged (DATA_RECEIVED, PROBE, etc.)
  
- [ ] Heartbeat messages reaching router
  - Check router message type - should see some DATA_RECEIVED or PEER_FOUND events
  
- [ ] CRC validation passing
  - If seeing: `[ERROR][HEARTBEAT] CRC validation failed` - packet corruption

### 3. Immediate Timeout (Fixed)
**Symptom:** CONNECTION_LOST immediately after CONNECTED

**Status:** FIXED
- Added guard: only check timeout if `m_heartbeats_received > 0`
- Added reset: `on_connection_established()` resets timer when peer registered
- Integration: called from `rx_connection_handler.cpp` on_peer_registered()

## Message Flow

```
Transmitter                          Receiver
    |                                   |
    |--- HeartbeatManager.tick() ---->  |
    |    (checks interval)              |
    |                                   |
    |--- esp_now_send heartbeat ------> | ESP-NOW queue
    |                                   |
    |                                   | Router dispatches to handler
    |                                   |
    |                                   | RxHeartbeatManager::on_heartbeat()
    |                                   | - Validates CRC
    |                                   | - Resets timeout
    |                                   | - Sends ACK
    |                                   |
    |<-- esp_now_send ACK -----------  |
    |                                   |
    |--- HeartbeatManager.on_ack() --   |
    |    (updates ack_seq)              |
```

## Debug Configuration

### Enable All Logs
Both projects should have logging enabled:
- Transmitter: Check `src/config/logging_config.h` - ensure LOG_LEVEL is set appropriately
- Receiver: Check common logging configuration

### Key Files to Monitor
**Transmitter:**
- `src/espnow/heartbeat_manager.cpp` - Main heartbeat logic
- `src/main.cpp` line ~330 - HeartbeatManager::instance().tick() call

**Receiver:**
- `src/espnow/rx_heartbeat_manager.cpp` - Heartbeat reception
- `src/espnow/espnow_tasks.cpp` line 247-252 - Handler registration
- `src/espnow/rx_connection_handler.cpp` - Connection state callbacks

## Recent Changes (Session 8)

1. Added receiver_mac broadcast check - prevents sending to `FF:FF:FF:FF:FF:FF`
2. Added debug logging to show receiver_mac at init
3. Enhanced tick() logging to show when intervals occur
4. Improved receive logging to show total count and state
5. Fixed false timeout by:
   - Only checking timeout after first heartbeat received
   - Resetting timer in on_connection_established() callback
   - Integrating callback into rx_connection_handler

## Next Steps If Still Not Working

1. **Check PROBE/ACK flow**
   - Are transmitter and receiver discovering each other?
   - Look for PROBE and ACK messages in logs

2. **Check connection state machine**
   - Verify state reaches CONNECTED
   - Monitor connection events

3. **Verify message queue**
   - Heartbeat messages must reach the ESP-NOW queue
   - Router must route them to the handler

4. **Check timestamps**
   - Ensure >10 seconds passes between heartbeat attempts
   - Verify clock is working properly
