# Step 20 — NVS persistence extraction

Date: 2026-03-19

## Summary
Extracted NVS persistence orchestration (debounced timer + load/save routing) from `TransmitterManager` into a dedicated module:

- `lib/webserver/utils/transmitter_nvs_persistence.h`
- `lib/webserver/utils/transmitter_nvs_persistence.cpp`

`TransmitterManager` now delegates:
- `init()` → `TransmitterNvsPersistence::init()`
- `loadFromNVS()` → `TransmitterNvsPersistence::loadFromNVS()`
- `saveToNVS()` → `TransmitterNvsPersistence::saveToNVS()`

## What moved
From `TransmitterManager` to `TransmitterNvsPersistence`:
- Debounced FreeRTOS timer ownership for NVS writes
- Timer callback and immediate persistence path
- NVS namespace open/close and cache load/save fan-out

## Behavior preserved
- Public `TransmitterManager` API remains unchanged.
- NVS write-through call sites still use `TransmitterManager::saveToNVS()`.
- Debounce behavior and persistence targets remain unchanged.

## Validation
- ✅ `pio run -e receiver_tft -j 12`
- ✅ `pio run -e receiver_lvgl -j 12`

Both environments built successfully.
