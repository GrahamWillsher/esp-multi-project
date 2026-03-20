# Step 25 — Write-through helper extraction

Date: 2026-03-19

## Summary

Extracted repeated write-through side-effect orchestration from `TransmitterManager` into a dedicated helper module.

## Changes

- Added `lib/webserver/utils/transmitter_write_through.h`
- Added `lib/webserver/utils/transmitter_write_through.cpp`
- Updated `lib/webserver/utils/transmitter_manager.cpp` to delegate repeated side effects to:
  - `TransmitterWriteThrough::persist_to_nvs()`
  - `TransmitterWriteThrough::notify_and_persist()`

## Behavior preserved

- All write-through NVS persistence still occurs for the same mutating paths.
- Network data/config write paths still notify SSE and then persist.
- Other settings/metadata/MQTT write paths still persist to NVS.

## Legacy removed from `TransmitterManager`

- Repeated inline `SSENotifier::notifyDataUpdated(); saveToNVS();` sequences.
- Repeated inline `saveToNVS();` write-through sequences.

## Validation

- IDE diagnostics clean on modified files.
- `pio run -e receiver_tft -j 12` ✅
- `pio run -e receiver_lvgl -j 12` ✅