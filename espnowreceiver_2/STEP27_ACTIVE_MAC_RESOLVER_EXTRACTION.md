# Step 27 — Active MAC resolver extraction

Date: 2026-03-19

## Summary

Extracted active MAC resolution logic from `TransmitterManager::getMAC()` into a dedicated helper module.

## Changes

- Added `lib/webserver/utils/transmitter_active_mac_resolver.h`
- Added `lib/webserver/utils/transmitter_active_mac_resolver.cpp`
- Updated `lib/webserver/utils/transmitter_manager.cpp` so `getMAC()` delegates to `TransmitterActiveMacResolver::get_active_mac()`

## Behavior preserved

- Resolution still prefers registered MAC from `TransmitterIdentityCache`
- Fallback still checks ESPNow global MAC for non-zero bytes
- Returns `nullptr` when neither source has a valid MAC

## Legacy removed from `TransmitterManager`

- Direct active-MAC source-of-truth/fallback scan in `getMAC()`
- Direct `ESPNow::transmitter_mac` dependency in `transmitter_manager.cpp`

## Validation

- IDE diagnostics clean on modified files.
- `pio run -e receiver_tft -j 12` ✅
- `pio run -e receiver_lvgl -j 12` ✅