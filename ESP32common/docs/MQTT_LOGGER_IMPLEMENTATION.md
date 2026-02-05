# MQTT Debug Logger Implementation Summary

## Files Created

### Common Files (esp32common)

1. **`logging_utilities/mqtt_logger.h`**
   - MqttLogger singleton class
   - Log level enum (EMERG to DEBUG)
   - Convenience macros (MQTT_LOG_*, LOG_E/W/I/D)

2. **`logging_utilities/mqtt_logger.cpp`**
   - Full implementation with MQTT publishing
   - Circular buffer for offline messages
   - Serial fallback for critical errors
   - JSON payload format with metadata

3. **`logging_utilities/library.json`**
   - PlatformIO library metadata

### Packet Definitions

4. **Updated `espnow_packet_utils.h`**
   - Added `PACKET_TYPE_DEBUG_CONTROL` (0x11)
   - Added `debug_control_packet` structure
   - Added `PACKET_TYPE_DEBUG_ACK` (0x12)
   - Added `debug_ack_packet` structure

## Files Modified (Transmitter)

### Message Handler

5. **`src/espnow/message_handler.h`**
   - Added `handle_debug_control()` method
   - Added `send_debug_ack()` method
   - Added `save_debug_level()` method
   - Added `load_debug_level()` public method
   - Added `receiver_mac_` storage for ACK responses

6. **`src/espnow/message_handler.cpp`**
   - Included `mqtt_logger.h` and `Preferences.h`
   - Registered debug control handler in message router
   - Implemented `handle_debug_control()` - validates, applies, saves level
   - Implemented `send_debug_ack()` - sends ACK via ESP-NOW
   - Implemented `save_debug_level()` - persists to NVS
   - Implemented `load_debug_level()` - loads from NVS with default

### Main Application

7. **`src/main.cpp`**
   - Included `mqtt_logger.h`
   - Loads saved debug level from NVS
   - Initializes MqttLogger with MQTT client
   - Sets initial debug level
   - Added test log messages to verify functionality

### MQTT Manager

8. **`src/network/mqtt_manager.h`**
   - Added `get_client()` method to expose PubSubClient pointer

## How It Works

### Initialization Flow

```
1. setup() loads saved debug level from NVS
2. MqttLogger initialized with MQTT client & device ID
3. Debug level set from saved value (default: INFO)
4. Test messages published to verify connectivity
```

### MQTT Topics

```
transmitter/{device_id}/debug/emerg      (retained, critical)
transmitter/{device_id}/debug/alert      (retained, critical)
transmitter/{device_id}/debug/crit       (QoS high)
transmitter/{device_id}/debug/error      (QoS medium)
transmitter/{device_id}/debug/warning    (QoS low)
transmitter/{device_id}/debug/notice     (QoS low)
transmitter/{device_id}/debug/info       (QoS low)
transmitter/{device_id}/debug/debug      (QoS low)
transmitter/{device_id}/debug/level      (retained, current level)
transmitter/{device_id}/debug/status     (retained, JSON status)
```

### Message Format

```json
{
  "tag": "SYSTEM",
  "msg": "ESP-NOW Transmitter started",
  "uptime": 12345,
  "heap": 234567
}
```

### Debug Level Control (Future - Receiver Side)

```
1. User selects new level in web interface
2. Receiver sends PACKET_TYPE_DEBUG_CONTROL via ESP-NOW
3. Transmitter receives, validates, and applies new level
4. New level saved to NVS for persistence
5. Transmitter sends PACKET_TYPE_DEBUG_ACK confirmation
6. All future log messages use new level
```

## Usage Examples

### In Application Code

```cpp
// Function-based logging with tag
MQTT_LOG_INFO("ETH", "Ethernet connected: %s", ip.toString().c_str());
MQTT_LOG_ERROR("MQTT", "Connection failed, retry in 5s");
MQTT_LOG_DEBUG("ESPNOW", "Received packet type: %d", pkt_type);

// Auto-tagged (uses function name)
LOG_I("System initialized, heap: %u", ESP.getFreeHeap());
LOG_E("Failed to send message: %d", error_code);
LOG_D("Processing packet %lu", seq_num);
```

### Monitoring (MQTT Subscriber)

```bash
# Subscribe to all debug messages
mosquitto_sub -h broker -t "transmitter/+/debug/#"

# Subscribe only to errors and above
mosquitto_sub -h broker -t "transmitter/+/debug/error" \
                         -t "transmitter/+/debug/crit" \
                         -t "transmitter/+/debug/alert"

# Check current debug level
mosquitto_sub -h broker -t "transmitter/+/debug/level"
```

## Testing Checklist

- [x] Code compiles successfully
- [ ] MQTT connection established
- [ ] Debug messages appear on MQTT broker
- [ ] Messages include correct JSON metadata
- [ ] Different log levels filter correctly
- [ ] Saved level persists across reboots
- [ ] Serial fallback works when MQTT disconnected
- [ ] Buffer flushes when MQTT reconnects
- [ ] ESP-NOW debug control message handling (when receiver implemented)

## Next Steps (Receiver Side)

1. Add web API endpoints for debug level control
2. Add web interface dropdown for level selection
3. Implement ESP-NOW message sending for debug control
4. Handle debug ACK response
5. Update web UI to show current level

## Benefits

✅ All debug output centralized via MQTT
✅ No USB serial cable needed for debugging
✅ Remote monitoring from any MQTT client
✅ Historical logging via MQTT broker
✅ Easy integration with Node-RED, Grafana, etc.
✅ Runtime level control (when receiver side implemented)
✅ Persistent configuration across reboots
✅ Minimal memory overhead (~2KB buffer)
✅ Serial fallback for critical errors
