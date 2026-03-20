#ifndef CELL_DATA_CACHE_H
#define CELL_DATA_CACHE_H

#include <Arduino.h>
#include <vector>

// =============================================================================
// CellDataCache
// =============================================================================
// Owns cell monitor data and snapshot management:
//
//   - Cell voltage arrays (compile-time bounded at 128 cells)
//   - Cell balancing status
//   - Min/max voltage statistics
//   - Balancing active flag
//   - Data source tag (dummy/live/live_simulated)
//   - Snapshot builder for safe read access
//
// Thread-safety: all public methods use critical sections (FreeRTOS mutex)
// for atomic updates and snapshots. Read-only methods defer to caller locking.
// =============================================================================

namespace CellDataCache {

static constexpr uint16_t MAX_CELL_COUNT = 128;

struct CellDataSnapshot {
    bool known;
    uint16_t cell_count;
    uint16_t min_voltage_mV;
    uint16_t max_voltage_mV;
    bool balancing_active;
    std::vector<uint16_t> voltages_mV;
    std::vector<bool> balancing_status;
    char data_source[32];
};

// --- Cell data storage and retrieval ---

/// Store parsed cell data from JSON object (from MQTT or other source).
/// Bounds-checks cell count against MAX_CELL_COUNT.
void store_cell_data(const void* json_obj_ptr);  // void* JsonObject avoids header coupling

/// True if cell data has been successfully received.
bool has_cell_data();

/// Get current cell count (thread-safe).
uint16_t get_cell_count();

/// Get cell voltage array (caller must not modify; use snapshot for thread-safe copy).
const uint16_t* get_cell_voltages();

/// Get cell balancing status array (caller must not modify; use snapshot for thread-safe copy).
const bool* get_cell_balancing_status();

/// Get min cell voltage (thread-safe).
uint16_t get_cell_min_voltage();

/// Get max cell voltage (thread-safe).
uint16_t get_cell_max_voltage();

/// True if any cell is currently balancing (thread-safe).
bool is_balancing_active();

/// Get data source tag (e.g., "dummy", "live", "live_simulated").
const char* get_cell_data_source();

/// Build a thread-safe snapshot of all cell data.
bool get_cell_data_snapshot(CellDataSnapshot& out_snapshot);

} // namespace CellDataCache

#endif // CELL_DATA_CACHE_H
