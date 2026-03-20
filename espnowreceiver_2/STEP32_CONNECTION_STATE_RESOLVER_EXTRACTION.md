# Step 32 — Connection-state resolver extraction

Date: 2026-03-19

## Summary

Extracted transmitter connection-state resolution from `TransmitterManager` into a focused helper module.

## Changes

- Added `lib/webserver/utils/transmitter_connection_state_resolver.h`
- Added `lib/webserver/utils/transmitter_connection_state_resolver.cpp`
- Updated `lib/webserver/utils/transmitter_manager.cpp`:
  - `isTransmitterConnected()` now delegates to `TransmitterConnectionStateResolver::is_transmitter_connected()`

## Behavior preserved

- Connection state still requires both:
  - live link state from `TransmitterStatusCache`
  - known active MAC via active-MAC resolution

## Legacy removed from `TransmitterManager`

- Inline connection-state composition logic in `isTransmitterConnected()`

## Validation

- IDE diagnostics clean on modified files.
- Build validation complete:
  - `pio run -e receiver_tft -j 12` ✅ (RAM 28.7%, Flash 19.2%)
  - `pio run -e receiver_lvgl -j 12` ✅ (RAM 28.2%, Flash 21.4%)
