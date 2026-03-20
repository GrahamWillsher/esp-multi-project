# Step 26 — Runtime-status workflow extraction

Date: 2026-03-19

## Summary

Extracted runtime-status update orchestration from `TransmitterManager::updateRuntimeStatus(...)` into a dedicated helper module.

## Changes

- Added `lib/webserver/utils/transmitter_runtime_status_update.h`
- Added `lib/webserver/utils/transmitter_runtime_status_update.cpp`
- Updated `lib/webserver/utils/transmitter_manager.cpp` so `updateRuntimeStatus(...)` delegates to `TransmitterRuntimeStatusUpdate::update_runtime_status(...)`

## Behavior preserved

- Runtime MQTT connection still updates via `TransmitterMqttCache::update_runtime_connection(...)`
- Runtime Ethernet status still updates via `TransmitterStatusCache::update_runtime_status(...)`
- Conditional runtime-status log output remains unchanged and still emits only when a value changes

## Legacy removed from `TransmitterManager`

- Direct runtime-status orchestration and change-detection logic in `updateRuntimeStatus(...)`

## Validation

- IDE diagnostics clean on modified files.
- `pio run -e receiver_tft -j 12` ✅
- `pio run -e receiver_lvgl -j 12` ✅