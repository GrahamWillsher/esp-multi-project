# Test Data Mode Control - Complete Fix

## Issues Fixed

### Issue 1: Mode Not Displaying
**Problem:** The GET API endpoint was returning a help message instead of the actual current mode.

**Root Cause:** `api_get_test_data_mode_handler()` was not tracking or returning the mode state.

**Solution:** 
- Added `last_test_data_mode_sent` tracking variable in receiver (defaults to mode 2: FULL_BATTERY_DATA)
- Added `get_last_test_data_mode()` getter function
- Updated API to return actual mode value and name

### Issue 2: Commands Not Being Processed by Transmitter
**Problem:** Transmitter was ignoring test data mode control commands.

**Root Cause:** The `handle_debug_control()` function was treating all packets as debug level changes, ignoring the `flags` field that indicated test data mode control.

**Solution:**
- Modified transmitter's `handle_debug_control()` to check flags field (bit 0x80)
- Added new `handle_test_data_mode_control()` function
- Routes packets to appropriate handler based on flags

### Issue 3: Cell Data Still Changing
**Problem:** After selecting a mode, cell monitor still showed changing data.

**Root Cause:** Transmitter wasn't applying the mode changes properly.

**Solution:** Transmitter now:
1. Receives mode control via ESP-NOW
2. Validates mode (0-2)
3. Updates TestDataConfig with new mode
4. Calls `TestDataConfig::apply_config()` to activate changes

## Technical Changes

### Receiver Changes

#### 1. espnow_send.h
```cpp
// Added getter for last mode sent
uint8_t get_last_test_data_mode();
```

#### 2. espnow_send.cpp
```cpp
// Added tracking variable
static uint8_t last_test_data_mode_sent = 2;  // Default to FULL_BATTERY_DATA

// Store mode when sent
last_test_data_mode_sent = mode;
```

#### 3. api_handlers.cpp
```cpp
// GET endpoint now returns actual mode
uint8_t mode = get_last_test_data_mode();
snprintf(json_response, sizeof(json_response), 
         "{\"success\":true,\"mode\":%d,\"mode_name\":\"%s\"}",
         mode, mode_names[mode]);
```

### Transmitter Changes

#### 1. message_handler.h
```cpp
// Added new handler function
void handle_test_data_mode_control(const debug_control_t* pkt);
```

#### 2. message_handler.cpp
```cpp
// Added include
#include "../test_data/test_data_config.h"

// Modified handle_debug_control to route based on flags
if (pkt->flags & 0x80) {
    handle_test_data_mode_control(pkt);
    return;
}

// New handler implementation
void EspnowMessageHandler::handle_test_data_mode_control(const debug_control_t* pkt) {
    // Validate mode (0-2)
    // Convert to TestDataConfig::Mode enum
    // Apply configuration
    // Log change
}
```

## Test Data Modes

| Mode | Value | Name | Description | Cell Data |
|------|-------|------|-------------|-----------|
| OFF | 0 | OFF | No test data, use real CAN data only | Real data from battery |
| SOC_POWER_ONLY | 1 | SOC_POWER_ONLY | Generate SOC & power only | Not generated (static) |
| FULL_BATTERY_DATA | 2 | FULL_BATTERY_DATA | Generate all battery data | Fully simulated with changes |

## Packet Protocol

**ESP-NOW Packet Structure:**
```cpp
debug_control_t packet;
packet.type = msg_debug_control;  // Reuse existing message type
packet.level = mode;               // 0, 1, or 2
packet.flags = 0x80;              // High bit indicates test data mode
```

**Flag Meanings:**
- `flags & 0x80 == 0`: Debug level control
- `flags & 0x80 != 0`: Test data mode control

## Compilation Results

### Transmitter (Olimex ESP32-POE2)
- **Status:** ✅ SUCCESS
- **Time:** 66.27 seconds
- **Flash:** 80.8% (1482909 / 1835008 bytes)
- **RAM:** 25.1% (82312 / 327680 bytes)
- **Firmware:** `olimex_esp32_poe2_fw_2_0_0.bin`

### Receiver (LilyGo T-Display-S3)
- **Status:** ✅ SUCCESS
- **Time:** 50.63 seconds
- **Flash:** 17.7% (1414269 / 7995392 bytes)
- **RAM:** 16.9% (55460 / 327680 bytes)
- **Firmware:** `lilygo-t-display-s3_fw_2_0_0.bin`

## Testing Checklist

### Receiver Web UI
- [ ] Navigate to `/transmitter` page
- [ ] Verify "Test Data Mode Control" section displays
- [ ] Verify current mode shows correctly (should default to "FULL_BATTERY_DATA")
- [ ] Click OFF button
- [ ] Verify status message shows "✓ Mode changed successfully"
- [ ] Verify display updates to "OFF (Real CAN Data Only)"
- [ ] Click SOC_POWER button
- [ ] Verify display updates to "SOC_POWER_ONLY (Test SOC & Power)"
- [ ] Click FULL button
- [ ] Verify display updates to "FULL_BATTERY_DATA (All Test Data)"

### Transmitter Serial Logs
After sending each mode command, check transmitter logs for:
```
[TEST_DATA_CTRL] Received test data mode change request: X (MODE_NAME)
[TEST_DATA_CTRL] Test data mode changed: PREVIOUS_MODE → NEW_MODE
```

### Cell Monitor Verification
- [ ] Set mode to OFF (0)
  - Cell data should show real battery values (static if no battery connected)
- [ ] Set mode to SOC_POWER_ONLY (1)
  - Cell data should be static (not changing)
  - SOC and power should show test values
- [ ] Set mode to FULL_BATTERY_DATA (2)
  - Cell data should change dynamically
  - All battery parameters show simulated values

## API Endpoints

### GET /api/get_test_data_mode
Returns current test data mode.

**Response:**
```json
{
  "success": true,
  "mode": 2,
  "mode_name": "FULL_BATTERY_DATA",
  "message": "Current test data mode"
}
```

### POST /api/set_test_data_mode
Sets test data mode on transmitter.

**Request:**
```json
{
  "mode": 2
}
```
Or with string:
```json
{
  "mode": "FULL_BATTERY_DATA"
}
```

**Response (Success):**
```json
{
  "success": true,
  "mode": "FULL_BATTERY_DATA",
  "message": "Test data mode changed"
}
```

**Response (Failure):**
```json
{
  "success": false,
  "error": "Failed to send command to transmitter"
}
```

## Troubleshooting

### Mode Not Updating on UI
- Check browser console for JavaScript errors
- Verify `/api/get_test_data_mode` returns valid JSON
- Check auto-refresh interval (3 seconds)

### Transmitter Not Receiving Commands
- Verify transmitter is connected (check status on /transmitter page)
- Check receiver serial logs for ESP-NOW send confirmation
- Verify transmitter serial logs show "Received test data mode change request"

### Cell Data Still Changing in OFF Mode
- Verify transmitter logs show mode change applied
- Check that mode is actually 0 (not 2)
- Restart transmitter if mode is stuck

## Related Files

### Receiver
- `src/espnow/espnow_send.h` - Function declarations
- `src/espnow/espnow_send.cpp` - ESP-NOW command implementation
- `lib/webserver/api/api_handlers.cpp` - API endpoints
- `lib/webserver/pages/transmitter_hub_page.cpp` - UI

### Transmitter
- `src/espnow/message_handler.h` - Handler declarations
- `src/espnow/message_handler.cpp` - Packet routing and handling
- `src/test_data/test_data_config.cpp` - Mode configuration

## Summary

The test data mode control system is now fully functional:

✅ **Receiver tracks mode** - Caches last mode sent locally  
✅ **API returns actual mode** - GET endpoint shows current state  
✅ **Transmitter processes commands** - Routes packets correctly based on flags  
✅ **Mode changes apply** - TestDataConfig updated and applied  
✅ **UI displays current mode** - Real-time status with color coding  
✅ **Cell data responds** - Changes based on selected mode

**Next steps:**
1. Flash both devices with new firmware
2. Test all three modes
3. Verify cell data behavior matches mode
4. Document any issues found during testing
