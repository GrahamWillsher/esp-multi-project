# Step 14 — Spec Cache Extraction

**Date:** 2026-03-19  
**Status:** ✅ Complete  
**Addresses:** M1 (TransmitterManager decomposition — continued follow-through)

## Summary

Extracted spec JSON ownership from `TransmitterManager` into a dedicated `TransmitterSpecCache` module while preserving the existing `TransmitterManager` API as a compatibility facade.

## Changes

### New Module Created
- **File:** `lib/webserver/utils/transmitter_spec_cache.h`
- **File:** `lib/webserver/utils/transmitter_spec_cache.cpp`

**Ownership transferred to TransmitterSpecCache:**
- Combined static specs JSON from `BE/spec_data`
- Battery specs JSON
- Inverter specs JSON
- Charger specs JSON
- System specs JSON
- Known-state flag for combined static specs

### Public API

```cpp
namespace TransmitterSpecCache {
    void store_static_specs(const JsonObject& specs);
    void store_battery_specs(const JsonObject& specs);
    void store_inverter_specs(const JsonObject& specs);
    void store_charger_specs(const JsonObject& specs);
    void store_system_specs(const JsonObject& specs);

    bool has_static_specs();
    String get_static_specs_json();
    String get_battery_specs_json();
    String get_inverter_specs_json();
    String get_charger_specs_json();
    String get_system_specs_json();
}
```

### Files Modified

**transmitter_manager.h/cpp:**
- Removed `SpecCache` nested struct definition
- Removed `spec_cache_` static state
- Replaced spec JSON ownership with delegation to `TransmitterSpecCache`
- Kept `storeBatterySpecs(...)` battery-cell-count sync in place

## Legacy Removed

- `TransmitterManager::SpecCache`
- Combined/per-domain spec JSON backing state in `TransmitterManager`
- Spec serialization logic from `TransmitterManager`

## Compatibility

The public `TransmitterManager` API remains unchanged:
- `storeStaticSpecs(...)`
- `storeBatterySpecs(...)`
- `storeInverterSpecs(...)`
- `storeChargerSpecs(...)`
- `storeSystemSpecs(...)`
- `hasStaticSpecs()`
- `getStaticSpecsJson()` and per-domain getters

This keeps existing MQTT handlers, API handlers, and spec display pages working without further call-site changes.

## Important preserved behavior

`TransmitterManager::storeBatterySpecs(...)` still updates `settings_cache_.battery_settings.cell_count` when the MQTT payload includes `number_of_cells`. That side effect intentionally remains in `TransmitterManager` because it crosses cache-domain boundaries.

## Result

- Further reduces `TransmitterManager` ownership scope
- Centralizes spec JSON handling in one dedicated module
- Preserves behavior and API compatibility
- Keeps cross-domain coordination explicit instead of hiding it inside the new cache

## Build Validation

| Environment | Result |
|---|---|
| `receiver_tft` | ✅ SUCCESS |
| `receiver_lvgl` | ✅ SUCCESS |

---

**Next steps:** Event-log extraction or MAC/network decomposition cleanup would be the next likely candidates if further `TransmitterManager` reduction is desired.
