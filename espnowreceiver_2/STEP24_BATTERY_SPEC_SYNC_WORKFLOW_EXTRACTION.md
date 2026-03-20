# Step 24 — Battery-spec sync workflow extraction

Date: 2026-03-19

## Summary

Extracted the remaining cross-domain battery-spec synchronization workflow from `TransmitterManager::storeBatterySpecs(...)` into a dedicated helper module.

## Changes

- Added `lib/webserver/utils/transmitter_battery_spec_sync.h`
- Added `lib/webserver/utils/transmitter_battery_spec_sync.cpp`
- Updated `lib/webserver/utils/transmitter_manager.cpp` so `storeBatterySpecs(...)` delegates to `TransmitterBatterySpecSync::store_battery_specs(...)`

## Behavior preserved

- Battery specs are still stored via `TransmitterSpecCache`
- `number_of_cells` is still parsed from MQTT battery specs when present
- `TransmitterSettingsCache::update_battery_cell_count(...)` still runs for valid cell counts (`> 0`)
- Existing log output for cell-count sync is unchanged

## Legacy removed from `TransmitterManager`

- Direct battery-spec to settings sync orchestration inside `storeBatterySpecs(...)`

## Validation

- IDE diagnostics clean on modified files
- `pio run -e receiver_tft -j 12` ✅
- `pio run -e receiver_lvgl -j 12` ✅