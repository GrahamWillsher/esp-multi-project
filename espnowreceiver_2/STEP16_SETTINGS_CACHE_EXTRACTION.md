# Step 16 — Settings Cache Extraction

**Date:** 2026-03-19  
**Status:** ✅ Complete  
**Addresses:** M1 (TransmitterManager decomposition — continued follow-through)

## Summary

Extracted battery/emulator/power/inverter/CAN/contactor settings ownership and settings NVS persistence from `TransmitterManager` into a dedicated `TransmitterSettingsCache` module.

## Changes

### New Modules Created
- `lib/webserver/utils/transmitter_settings_types.h`
- `lib/webserver/utils/transmitter_settings_cache.h`
- `lib/webserver/utils/transmitter_settings_cache.cpp`

### TransmitterManager changes
- Replaced internal settings state ownership with delegation to `TransmitterSettingsCache`.
- Replaced settings load/save blocks in NVS paths with:
  - `TransmitterSettingsCache::load_from_prefs(...)`
  - `TransmitterSettingsCache::save_to_prefs(...)`
- Delegated settings APIs:
  - `store/get/has` for Battery, BatteryEmulator, Power, Inverter, CAN, Contactor
- Preserved existing `TransmitterManager` API signatures (facade compatibility).
- Preserved battery settings logging in `storeBatterySettings(...)`.
- Preserved battery cell-count sync from incoming specs via cache helper:
  - `TransmitterSettingsCache::update_battery_cell_count(...)`

### Legacy cleanup included
- Removed `TransmitterManager::SettingsCache` struct/state.
- Removed obsolete `TransmitterManager` mutex scaffolding that was no longer used after prior extractions:
  - `data_mutex_`
  - local `ScopedMutex` helper

## Build Validation

| Environment | Result |
|---|---|
| `receiver_tft` | ✅ SUCCESS |
| `receiver_lvgl` | ✅ SUCCESS |

## Notes

This step substantially reduces `TransmitterManager` state ownership and centralizes settings defaults, mutation, and persistence in one module without changing external call sites.
