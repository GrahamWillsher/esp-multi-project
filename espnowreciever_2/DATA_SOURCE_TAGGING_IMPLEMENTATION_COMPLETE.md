# Data Source Tagging Implementation - Complete

## Implementation Summary

Successfully implemented 3-state data source tagging to distinguish between dummy test data, live CAN data, and simulated fallback data in the cell monitor.

## Changes Made

### Transmitter (ESPnowtransmitter2)

**File: `src/datalayer/static_data.cpp`**

1. **Added TestDataGenerator include** to check test mode state
2. **Implemented data_source computation logic** in `serialize_cell_data()`:
   - **dummy**: When `TestDataGenerator::is_enabled()` returns true (test mode ON)
   - **live**: When test mode OFF AND has real data AND CAN data fresh (< 5 seconds old)
   - **live_simulated**: When test mode OFF AND (no real data OR CAN data stale > 5 seconds)
3. **Added `data_source` field** to cell_data JSON output for both real and dummy data paths

**Logic Flow:**
```cpp
if (TestDataConfig::is_enabled()) {
    data_source = "dummy";
} else if (!has_real_data) {
    data_source = "live_simulated";
} else {
    // CAN_battery_still_alive starts at 60, decrements every second
    if (datalayer.battery.status.CAN_battery_still_alive < 55) {
        data_source = "live_simulated";
    } else {
        data_source = "live";
    }
}
```

### Receiver (espnowreciever_2)

**File: `lib/webserver/utils/transmitter_manager.h`**

1. **Added private member**: `static char cell_data_source_[32]` to store data source tag
2. **Added public getter**: `static const char* getCellDataSource()` to retrieve current source

**File: `lib/webserver/utils/transmitter_manager.cpp`**

1. **Initialized storage variable**: `char TransmitterManager::cell_data_source_[32] = "unknown"`
2. **Added parsing in `storeCellData()`**:
   - Extracts `data_source` field from cell_data JSON
   - Safely copies to local buffer with null termination
   - Defaults to "unknown" if field not present
3. **Enhanced logging**: Serial output now includes data source

**File: `lib/webserver/api/api_handlers.cpp`**

1. **Updated SSE handler**: Replaced hardcoded `"mode":"live"` with `TransmitterManager::getCellDataSource()`
2. **Updated single-request handler**: Also uses actual data source instead of hardcoded value

## Data Flow

```
Transmitter:
1. serialize_cell_data() checks TestDataGenerator mode + CAN freshness
2. Sets data_source = "dummy" | "live" | "live_simulated"
3. Adds "data_source" field to cell_data JSON
4. Publishes to BE/cell_data MQTT topic

Receiver:
1. mqtt_client receives BE/cell_data message
2. Parses JSON and passes to TransmitterManager::storeCellData()
3. Extracts and stores data_source in cell_data_source_
4. SSE handler calls getCellDataSource() for mode field
5. UI displays actual data source to user
```

## Verification

✅ All files compile without errors  
✅ Transmitter build: SUCCESS (80.8% flash, 25.1% RAM)  
✅ Receiver build: SUCCESS (17.7% flash, 16.9% RAM)  
✅ Transmitter logic uses `CAN_battery_still_alive` counter (existing field)  
✅ CAN staleness threshold: < 55 (5+ seconds without CAN messages)  
✅ Receiver properly stores and retrieves data source  
✅ SSE output includes actual source instead of hardcoded "live"

## Testing Scenarios

To verify the implementation works correctly:

### Test 1: Dummy Mode (Test Data ON)
1. Enable test mode on transmitter (set to FULL_BATTERY_DATA)
2. Open /cellmonitor on receiver
3. **Expected**: Header shows "DUMMY DATA" (mode: dummy)

### Test 2: Live Mode (Real CAN Data)
1. Disable test mode on transmitter (set to OFF)
2. Ensure CAN bus is active with fresh data
3. Open /cellmonitor on receiver
4. **Expected**: Header shows "LIVE DATA" (mode: live)

### Test 3: Simulated Mode (Stale CAN Data)
1. Disable test mode on transmitter (set to OFF)
2. Wait 6+ seconds with no CAN messages
3. Open /cellmonitor on receiver
4. **Expected**: Header shows "LIVE DATA (SIMULATED)" (mode: live_simulated)

### Test 4: Mode Transitions
1. Start in test mode (dummy)
2. Switch to OFF mode with active CAN (live)
3. Disconnect CAN and wait 6s (live_simulated)
4. Reconnect CAN (live)
5. **Expected**: UI updates in real-time as mode changes

## Benefits

1. **Transparency**: Users can now see actual data source in cell monitor
2. **Debugging**: Easier to identify when system is using fallback/test data
3. **Confidence**: Clear indication when viewing real battery telemetry vs simulated
4. **Diagnostics**: Can detect CAN communication issues (stale data → live_simulated)

## Related Documentation

- Original plan: `DATA_SOURCE_TAGGING_PLAN.md`
- UI already supports all 3 modes (no changes needed)
- MQTT subscription optimization verified working (30s grace period)

## Implementation Date

January 2025 - Completed in single session
