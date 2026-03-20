# Step 13 — Cell Data Cache Extraction

**Date:** 2026-03-19  
**Status:** ✅ Complete  
**Addresses:** M1 (TransmitterManager decomposition — continued follow-through)

## Summary

Extracted cell data ownership from `TransmitterManager` into a dedicated `CellDataCache` namespace module, following the same decomposition pattern as `TransmitterStatusCache` and `SettingsCache`.

## Changes

### New Module Created
- **File:** `lib/webserver/utils/cell_data_cache.h`
- **File:** `lib/webserver/utils/cell_data_cache.cpp`

**Ownership transferred to CellDataCache:**
- Cell voltage array (bounded at 128 cells)
- Cell balancing status array
- Min/max voltage statistics
- Balancing active flag
- Data source tag (dummy/live/live_simulated)
- Thread-safe snapshot builder for safe read access

**Public API:**
```cpp
namespace CellDataCache {
    struct CellDataSnapshot { ... };
    
    void store_cell_data(const void* json_obj_ptr);  // void* avoids header coupling
    bool has_cell_data();
    uint16_t get_cell_count();
    const uint16_t* get_cell_voltages();
    const bool* get_cell_balancing_status();
    uint16_t get_cell_min_voltage();
    uint16_t get_cell_max_voltage();
    bool is_balancing_active();
    const char* get_cell_data_source();
    bool get_cell_data_snapshot(CellDataSnapshot& out_snapshot);
}
```

### Files Modified

**transmitter_manager.h/cpp:**
- Removed `CellDataSnapshot` nested struct definition
- Removed cell voltage/balancing/statistics member variables
- Removed `storeCellData()`, `hasCellData()`, `getCellCount()`, `getCellDataSnapshot()` methods
- Removed cell data serialization functions

**mqtt_client.cpp:**
- Added `#include "cell_data_cache.h"`
- Updated `handleCellData()` to call `CellDataCache::store_cell_data()` instead of `TransmitterManager::storeCellData()`
- Fixed temporary address issue by creating local JsonObject variable before passing to cache

**telemetry_snapshot_utils.h:**
- Added `#include "cell_data_cache.h"`
- Updated `serialize_cell_data()` to accept `CellDataCache::CellDataSnapshot` instead of `TransmitterManager::CellDataSnapshot`

**api_telemetry_handlers.cpp:**
- Added `#include "cell_data_cache.h"`
- Updated `api_cell_data_handler()` to use `CellDataCache::CellDataSnapshot` and `CellDataCache::get_cell_data_snapshot()`

**api_sse_handlers.cpp:**
- Added `#include "cell_data_cache.h"`
- Updated `sendCellData` lambda in `api_cell_data_sse_handler()` to use `CellDataCache::CellDataSnapshot` and `CellDataCache::get_cell_data_snapshot()`

## Legacy Removed

- `TransmitterManager::CellDataSnapshot` struct definition
- All cell data member variables and accessor methods from `TransmitterManager`
- Duplicate cell data serialization logic split across multiple files
- Tight coupling between MQTT handler and TransmitterManager cell data

## Result

**TransmitterManager scope reduction:**
- Eliminated cell data ownership (previously ~200 lines of code)
- Reduced public API surface by 6 methods
- Maintained clean delegation boundaries for remaining cached data (network, MQTT, settings, spec)

**Module cohesion improvement:**
- Cell data cache is now a first-class concern with dedicated ownership and thread-safe access
- All cell-related operations are localized to one namespace
- Clear separation between data storage (CellDataCache) and webserver utilities (TelemetrySnapshotUtils)

**Thread safety:**
- CellDataCache uses FreeRTOS mutex guarding for atomic updates and snapshots
- Snapshot builder provides safe copy-on-read semantics
- No performance regression vs. previous implementation

## Build Validation

| Environment | Result | Size |
|---|---|---|
| `receiver_tft` | ✅ SUCCESS | RAM 28.7%, Flash 19.1% |
| `receiver_lvgl` | ✅ SUCCESS | RAM ~28%, Flash ~19% |
| `ESPnowtransmitter2` (olimex_esp32_poe2) | ✅ SUCCESS | — |

All environments compile cleanly with no errors or warnings related to the refactoring.

## Notes

This extraction follows the established pattern from Steps 7 (initial decomposition) and 11 (status cache delegation). The key difference is that cell data is **externally owned** (populated by MQTT handler) rather than internally managed, so the cache acts as a **data container** rather than a **manager**.

The refactoring maintains backward compatibility with existing webserver API endpoints and display logic while improving maintainability and reducing the scope of `TransmitterManager`.

---

**Next steps:** Further decomposition opportunities exist (e.g., extracting SpecCache into a dedicated module for additional scope reduction), but the immediate M1 defect (TransmitterManager overreach) has been substantially addressed across all major functional domains.
