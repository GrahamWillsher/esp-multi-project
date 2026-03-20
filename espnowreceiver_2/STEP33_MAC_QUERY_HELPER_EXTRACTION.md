# Step 33 — MAC-query helper extraction

Date: 2026-03-19

## Summary

Extracted MAC identity query composition from `TransmitterManager` into a focused helper module.

## Changes

- Added `lib/webserver/utils/transmitter_mac_query_helper.h`
- Added `lib/webserver/utils/transmitter_mac_query_helper.cpp`
- Updated `lib/webserver/utils/transmitter_manager.cpp`:
  - `getMAC()` now delegates to `TransmitterMacQueryHelper::get_active_mac()`
  - `isMACKnown()` now delegates to `TransmitterMacQueryHelper::is_mac_known()`
  - `getMACString()` now delegates to `TransmitterMacQueryHelper::get_mac_string()`

## Behavior preserved

- Active MAC resolution still uses `TransmitterActiveMacResolver`
- MAC formatting still uses `TransmitterIdentityCache::format_mac(...)`
- `isMACKnown()` semantics remain unchanged (`active_mac != nullptr`)

## Legacy removed from `TransmitterManager`

- Inline MAC known-state composition
- Inline MAC string composition

## Validation

- IDE diagnostics clean on modified files.
- Build validation complete:
  - `pio run -e receiver_tft -j 12` ✅ (RAM 28.7%, Flash 19.2%)
  - `pio run -e receiver_lvgl -j 12` ✅ (RAM 28.2%, Flash 21.4%)
