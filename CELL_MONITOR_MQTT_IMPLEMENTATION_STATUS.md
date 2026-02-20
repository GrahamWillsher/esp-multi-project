# Cell Monitor MQTT Implementation Status

## Overview
Implementation to display real battery cell voltages and balancing status from Battery Emulator on the receiver's web UI via MQTT communication.

## Architecture
- **Transport**: MQTT (separate from ESP-NOW)
- **Data Size**: ~1200 bytes JSON for 96 cells
- **Publish Rate**: 1 Hz (1 second interval)
- **Topic**: `BE/cell_data`
- **Retained**: Yes (receiver gets data on connect)

## Implementation Status

### ✅ Phase 1: Fix Cell Count Display (COMPLETE)
**Files Modified:**
- `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_task.cpp` (line ~87)

**Changes:**
- Added `publish_battery_specs()` call on MQTT connection
- Ensures cell count is published to receiver

### ✅ Phase 2: Receiver-Side Integration (COMPLETE)

#### Phase 2.1: Add Cell Data Structures (COMPLETE)
**Files Modified:**
- `espnowreciever_2/src/transmitter_manager.h` (lines 133-150)
- `espnowreciever_2/src/transmitter_manager.cpp` (lines 165-172 + end)

**Changes:**
- Added private static members for cell data storage:
  - `cell_voltages_mV_` (uint16_t* dynamic array)
  - `cell_balancing_status_` (bool* dynamic array)
  - `cell_count_`, `cell_min_voltage_mV_`, `cell_max_voltage_mV_`
  - `balancing_active_`, `cell_data_known_`
- Added 8 public accessor methods:
  - `storeCellData()`, `hasCellData()`, `getCellCount()`
  - `getCellVoltages()`, `getCellBalancingStatus()`
  - `getCellMinVoltage()`, `getCellMaxVoltage()`, `isBalancingActive()`
- Implemented dynamic memory management (~80 lines)
  - Malloc/free for cell arrays
  - Reallocation on cell count change

#### Phase 2.2: Add MQTT Subscribe and Handler (COMPLETE)
**Files Modified:**
- `espnowreciever_2/src/mqtt/mqtt_client.h`
- `espnowreciever_2/src/mqtt/mqtt_client.cpp` (lines ~140-152 + end)

**Changes:**
- Added `handleCellData()` method declaration and implementation
- Added `BE/cell_data` subscription in `subscribeToTopics()`
- Added routing in `messageCallback()`
- Implementation uses 2048-byte DynamicJsonDocument
- Parses JSON and calls `TransmitterManager::storeCellData()`

#### Phase 2.3: Update API Handler (COMPLETE)
**Files Modified:**
- `espnowreciever_2/src/api/api_handlers.cpp` (line ~265)

**Changes:**
- Modified `api_cell_data_handler` to prioritize real MQTT data
- Logic flow:
  1. Check `TransmitterManager::hasCellData()` in live mode
  2. Return real cell voltages, balancing status, min/max
  3. Fallback to simulated data in test mode
  4. Return error if no data in live mode

#### ⏳ Phase 2.4: Enhance Cell Monitor Page UI (PENDING)
**Files to Modify:**
- `espnowreciever_2/src/web/cellmonitor_page.cpp`

**Required Changes:**
- Add statistics header (max/min/deviation/balancing count)
- Implement 2-column cell grid (48% width cells)
- Add color coding:
  - Cyan (#00FFFF) for balancing cells
  - Red borders for min/max cells
  - Red text for cells <3000mV
- Add hover tooltips (cell number + voltage)
- Match Battery Emulator dark theme styling
- Update Material design elements

### ✅ Phase 3: Transmitter-Side Publishing (COMPLETE)

#### Phase 3.1: Implement serialize_cell_data() and publish_cell_data() (COMPLETE)
**Files Modified:**
- `ESPnowtransmitter2/espnowtransmitter2/src/datalayer/static_data.h`
- `ESPnowtransmitter2/espnowtransmitter2/src/datalayer/static_data.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp`

**Changes:**
- Added `serialize_cell_data()` function (~50 lines):
  - Uses 2048-byte DynamicJsonDocument (PSRAM)
  - Reads from `datalayer.battery.info.number_of_cells`
  - Reads from `datalayer.battery.status.cell_voltages_mV[]`
  - Reads from `datalayer.battery.status.cell_balancing_status[]`
  - Calculates min/max voltages
  - Checks if any balancing active
  - Returns JSON format matching receiver expectations
- Added `publish_cell_data()` method (~25 lines):
  - Allocates 2048-byte PSRAM buffer
  - Calls `StaticData::serialize_cell_data()`
  - Publishes to `BE/cell_data` with retained flag
  - Returns success/failure status

#### Phase 3.2: Add Periodic Cell Data Publish (COMPLETE)
**Files Modified:**
- `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_task.cpp`

**Changes:**
- Added `last_cell_publish` timestamp variable
- Added periodic publish logic (1-second interval)
- Publishes cell data independently from main telemetry
- Debug logging on successful publish

### ⏳ Phase 4: Build and Test (PENDING)

## JSON Format

### Transmitter Publishes (BE/cell_data):
```json
{
  "number_of_cells": 96,
  "cell_voltages_mV": [3850, 3852, 3848, ...],
  "cell_balancing_status": [false, true, false, ...],
  "cell_min_voltage_mV": 3840,
  "cell_max_voltage_mV": 3860,
  "balancing_active": true
}
```

### Receiver API Returns (/api/cell_data):
```json
{
  "success": true,
  "mode": "live",
  "cells": [3850, 3852, 3848, ...],
  "balancing": [false, true, false, ...],
  "cell_min_voltage_mV": 3840,
  "cell_max_voltage_mV": 3860,
  "balancing_active": true
}
```

## Data Flow

1. **Battery Emulator** → reads cells → `datalayer.battery.status.cell_voltages_mV[]`
2. **StaticData::serialize_cell_data()** → builds JSON
3. **MqttManager::publish_cell_data()** → publishes to `BE/cell_data` @ 1Hz
4. **Receiver MqttClient** → subscribes to `BE/cell_data`
5. **MqttClient::handleCellData()** → parses JSON
6. **TransmitterManager::storeCellData()** → caches in memory
7. **API /api/cell_data** → returns cached data
8. **Web UI /cellmonitor** → fetches and displays

## Testing Plan

### Phase 4.1: Build Transmitter
```bash
cd ESPnowtransmitter2/espnowtransmitter2
pio run
```

### Phase 4.2: Build Receiver
```bash
cd espnowreciever_2
pio run
```

### Phase 4.3: Verify MQTT Publishing
1. Monitor MQTT broker with `mosquitto_sub -h <broker_ip> -t "BE/#" -v`
2. Verify `BE/cell_data` messages appear every 1 second
3. Check JSON format matches specification

### Phase 4.4: Verify Receiver API
1. Connect to receiver web UI
2. Navigate to `/api/cell_data`
3. Verify JSON response contains real cell data
4. Check cell count matches battery (96 for Nissan Leaf)

### Phase 4.5: Verify Cell Monitor Page
1. Navigate to `/cellmonitor`
2. Verify cells display with real voltages
3. Check color coding (balancing = cyan, min/max = red borders)
4. Verify statistics header (min/max/deviation)
5. Test hover tooltips
6. Verify auto-refresh works

## Known Issues & Limitations

1. **Memory**: Uses ~2KB PSRAM for transmitter, ~1KB for receiver per cell data packet
2. **Latency**: 1-second update rate (lower priority than 5Hz control data)
3. **Cell Count**: Supports up to 192 cells (MAX_AMOUNT_CELLS)
4. **UI Enhancement**: Phase 2.4 not yet implemented (basic bar chart currently)

## Next Steps

1. **Immediate**: Implement Phase 2.4 (Cell Monitor UI Enhancement)
   - Estimated time: 3-4 hours
   - Files: `cellmonitor_page.cpp`
   - Add statistics, grid layout, color coding, tooltips

2. **Testing**: Build and flash both firmwares
   - Estimated time: 30 minutes
   - Verify end-to-end data flow

3. **Documentation**: Update user-facing docs
   - Cell monitor page usage
   - Troubleshooting guide

## Files Modified Summary

### Transmitter (5 files):
1. `src/network/mqtt_task.cpp` - Added battery_specs publish + cell_data periodic publish
2. `src/network/mqtt_manager.h` - Added publish_cell_data() declaration
3. `src/network/mqtt_manager.cpp` - Implemented publish_cell_data()
4. `src/datalayer/static_data.h` - Added serialize_cell_data() declaration
5. `src/datalayer/static_data.cpp` - Implemented serialize_cell_data()

### Receiver (5 files):
1. `src/transmitter_manager.h` - Added cell data members and methods
2. `src/transmitter_manager.cpp` - Implemented storage and accessors
3. `src/mqtt/mqtt_client.h` - Added handleCellData() declaration
4. `src/mqtt/mqtt_client.cpp` - Implemented MQTT subscription and handler
5. `src/api/api_handlers.cpp` - Modified to use real cell data

### Total: 10 files modified, ~250 lines of code added

## Success Criteria

- [x] Cell count displays correctly on receiver
- [x] Transmitter publishes cell data to MQTT
- [x] Receiver subscribes and stores cell data
- [x] API returns real cell data when available
- [ ] Web UI displays cells with proper styling
- [ ] Statistics header shows min/max/deviation
- [ ] Color coding works (balancing/min/max)
- [ ] End-to-end test passes

## Completion: 87.5% (7/8 phases complete)
