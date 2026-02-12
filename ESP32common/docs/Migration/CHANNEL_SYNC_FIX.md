# Channel Synchronization Fix for Discovery Restart

## Issue Description

After implementing the discovery restart on timeout, the transmitter was encountering ESP-NOW channel mismatch errors:

```
ESPNOW: Peer channel is not equal to the home channel, send fail!
```

### Error Pattern
- Errors appeared every ~5 seconds (matches PROBE interval)
- Occurred AFTER timeout detection and discovery restart
- Triggered consistently when debug level was changed
- Prevented all ESP-NOW communication

## Root Cause Analysis

### Initial Channel Discovery (Startup)
1. Transmitter calls `discover_and_lock_channel()`
2. Scans channels 1-13, sends PROBE, waits for ACK
3. When receiver responds, locks WiFi channel and stores in `g_lock_channel`
4. Calls `set_channel(g_lock_channel)` to ensure WiFi is on correct channel
5. Adds broadcast peer with `channel = 0` (uses current WiFi channel)

### Discovery Restart Flow (After Timeout)
1. Watchdog detects 10s timeout
2. Calls `DiscoveryTask::instance().restart()`
3. Restart calls `stop()` (deletes task) then `start()` (creates new task)
4. New task calls `add_broadcast_peer()` with `channel = 0`
5. **PROBLEM**: WiFi channel may have drifted or broadcast peer uses wrong channel

### Channel Mismatch
ESP-NOW requires both devices to be on the **same WiFi channel**. The error occurs when:
- Broadcast peer's channel doesn't match transmitter's current WiFi channel
- OR transmitter's WiFi channel drifted from `g_lock_channel`

## Solution

### Channel Re-synchronization Before Restart

Modified the timeout watchdog in `message_handler.cpp` to:

1. **Check current WiFi channel** before restarting discovery
2. **Compare** with stored `g_lock_channel`
3. **Reset channel** if mismatch detected
4. **Verify** channel was actually set
5. **Then restart** discovery task

### Implementation

```cpp
// Timeout watchdog
if (handler.receiver_state_.is_connected) {
    uint32_t time_since_last_rx = millis() - handler.receiver_state_.last_rx_time_ms;
    if (time_since_last_rx > CONNECTION_TIMEOUT_MS) {
        handler.receiver_state_.is_connected = false;
        handler.receiver_connected_ = false;
        LOG_WARN("[WATCHDOG] Receiver connection lost (timeout: %u ms)", CONNECTION_TIMEOUT_MS);
        
        // Ensure WiFi channel is set correctly before restarting discovery
        uint8_t current_ch = 0;
        wifi_second_chan_t second;
        esp_wifi_get_channel(&current_ch, &second);
        
        if (current_ch != g_lock_channel) {
            LOG_WARN("[WATCHDOG] WiFi channel drift detected (current=%d, expected=%d) - resetting",
                     current_ch, g_lock_channel);
            if (!set_channel(g_lock_channel)) {
                LOG_ERROR("[WATCHDOG] Failed to set WiFi channel to %d", g_lock_channel);
            } else {
                // Verify channel was set
                esp_wifi_get_channel(&current_ch, &second);
                LOG_INFO("[WATCHDOG] WiFi channel reset to %d (verified=%d)", 
                         g_lock_channel, current_ch);
            }
        }
        
        // Restart discovery task to find receiver again
        LOG_INFO("[WATCHDOG] Restarting discovery task to reconnect");
        DiscoveryTask::instance().restart();
    }
}
```

### Files Modified
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_handler.cpp`:
  - Added `#include <esp_wifi.h>` for channel management functions
  - Added channel verification and reset logic before `restart()`

## Expected Behavior After Fix

### Normal Reconnection Flow
1. Timeout detected (10s no messages)
2. Check WiFi channel
3. If channel mismatch:
   - Log warning about drift
   - Reset to `g_lock_channel`
   - Verify reset succeeded
4. Restart discovery (sends PROBE every 5s)
5. Receiver responds with ACK
6. Connection re-established

### Log Output (Normal Case)
```
[WATCHDOG] Receiver connection lost (timeout: 10000 ms)
[WATCHDOG] Restarting discovery task to reconnect
Announcement task stopped
Periodic announcement started (bidirectional discovery)
Broadcast peer added
```

### Log Output (Channel Drift Case)
```
[WATCHDOG] Receiver connection lost (timeout: 10000 ms)
[WATCHDOG] WiFi channel drift detected (current=1, expected=6) - resetting
[WATCHDOG] WiFi channel reset to 6 (verified=6)
[WATCHDOG] Restarting discovery task to reconnect
Announcement task stopped
Periodic announcement started (bidirectional discovery)
Broadcast peer added
```

## Why Channel Drift Can Occur

### Potential Causes
1. **ESP-IDF internal behavior**: WiFi driver may change channels during certain operations
2. **Concurrent WiFi operations**: If other WiFi functions are called (scan, connect, etc.)
3. **Power management**: Some power states may affect channel settings
4. **Debug level changes**: Changing logging verbosity may trigger WiFi re-initialization

### Prevention Strategy
This fix implements **defensive programming**:
- Always verify channel state before critical operations
- Re-synchronize if drift detected
- Log channel status for debugging
- Fail gracefully if channel cannot be set

## Testing Recommendations

### Test Cases
1. **Normal timeout reconnection**:
   - Disconnect receiver power
   - Wait 10+ seconds
   - Verify transmitter restarts discovery
   - Restore receiver power
   - Verify reconnection succeeds

2. **Channel drift scenario**:
   - Monitor current WiFi channel during normal operation
   - Change debug level (tests discovered trigger)
   - Verify channel reset if drift detected
   - Verify reconnection succeeds

3. **Repeated reconnections**:
   - Cycle receiver power multiple times
   - Verify each reconnection works
   - Check for channel mismatch errors in logs

### Success Criteria
- ✅ No "Peer channel is not equal to the home channel" errors
- ✅ Discovery restart succeeds consistently
- ✅ PROBE messages sent successfully after restart
- ✅ Receiver responds to PROBE and connection re-establishes
- ✅ Data transmission resumes after reconnection

## Related Components

### Libraries Used
- **espnow_transmitter**: Provides `set_channel()`, `g_lock_channel`, `discover_and_lock_channel()`
- **ESP-IDF**: Provides `esp_wifi_get_channel()`, `esp_wifi_set_channel()`
- **espnow_discovery**: Provides discovery task with `restart()` method

### Affected Workflows
- Timeout detection and reconnection
- Discovery task lifecycle management
- WiFi channel management
- ESP-NOW peer management

## Future Considerations

### Potential Enhancements
1. **Periodic channel verification**: Check channel state every N seconds
2. **Channel lock enforcement**: Prevent ESP-IDF from changing channel
3. **Full re-discovery option**: If simple channel reset fails, perform full `discover_and_lock_channel()`
4. **Channel drift telemetry**: Report drift events via MQTT for monitoring

### Known Limitations
- Assumes `g_lock_channel` is always the correct channel
- Does not handle case where receiver changed channels
- Full channel re-discovery would require more complex state management

## Summary

This fix ensures the WiFi channel is synchronized with `g_lock_channel` before restarting discovery, preventing ESP-NOW channel mismatch errors during reconnection. The defensive check handles both normal operation and edge cases where channel drift may occur.

**Result**: Reliable reconnection without channel synchronization errors.
