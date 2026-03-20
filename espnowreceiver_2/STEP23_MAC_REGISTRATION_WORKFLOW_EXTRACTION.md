# Step 23 — MAC registration workflow extraction

Date: 2026-03-19

## Summary

Extracted the remaining MAC-registration orchestration from `TransmitterManager::registerMAC(...)` into a dedicated helper module, preserving the facade API while further reducing cross-domain workflow logic in the manager.

## Changes

- Added `lib/webserver/utils/transmitter_mac_registration.h`
- Added `lib/webserver/utils/transmitter_mac_registration.cpp`
- Updated `lib/webserver/utils/transmitter_manager.cpp` to delegate `registerMAC(...)` to `TransmitterMacRegistration::register_mac(...)`

## Behavior preserved

- Null MAC guard remains intact
- Registered transmitter identity is still stored through `TransmitterIdentityCache`
- Existing log output remains unchanged
- SSE cache-update notification still fires after MAC registration
- ESP-NOW peer registration still runs via `TransmitterPeerRegistry`

## Legacy removed from `TransmitterManager`

- Direct MAC-registration workflow orchestration in `registerMAC(...)`

## Validation

- IDE diagnostics clean on modified files
- `pio run -e receiver_tft -j 12` ✅
- `pio run -e receiver_lvgl -j 12` ✅