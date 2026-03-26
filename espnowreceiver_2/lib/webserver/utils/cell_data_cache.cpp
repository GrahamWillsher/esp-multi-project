#include "cell_data_cache.h"
#include "sse_notifier.h"
#include "../logging.h"
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>
#include <algorithm>

namespace {
    struct ScopedMutex {
        explicit ScopedMutex(SemaphoreHandle_t mutex)
            : mutex_(mutex), locked_(false) {
            if (mutex_ != nullptr) {
                locked_ = (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE);
            }
        }

        ~ScopedMutex() {
            if (locked_) {
                xSemaphoreGive(mutex_);
            }
        }

        bool locked() const { return locked_; }

    private:
        SemaphoreHandle_t mutex_;
        bool locked_;
    };

    static SemaphoreHandle_t cell_data_mutex_ = nullptr;
    static uint16_t cell_voltages_mV_[CellDataCache::MAX_CELL_COUNT] = {0};
    static bool cell_balancing_status_[CellDataCache::MAX_CELL_COUNT] = {false};
    static uint16_t cell_count_ = 0;
    static uint16_t cell_min_voltage_mV_ = 0;
    static uint16_t cell_max_voltage_mV_ = 0;
    static bool balancing_active_ = false;
    static bool cell_data_known_ = false;
    static char cell_data_source_[32] = "unknown";

    void ensure_mutex() {
        if (cell_data_mutex_ == nullptr) {
            cell_data_mutex_ = xSemaphoreCreateMutex();
        }
    }
}

namespace CellDataCache {

void store_cell_data(const void* json_obj_ptr) {
    ensure_mutex();

    if (json_obj_ptr == nullptr) return;
    const JsonObject& cell_data = *static_cast<const JsonObject*>(json_obj_ptr);

    if (!cell_data.containsKey("number_of_cells")) {
        LOG_ERROR("CELL_CACHE", "Invalid cell data: missing number_of_cells");
        return;
    }

    const uint16_t requested_count = cell_data["number_of_cells"];
    const uint16_t new_cell_count = std::min<uint16_t>(requested_count, MAX_CELL_COUNT);

    ScopedMutex guard(cell_data_mutex_);
    if (!guard.locked()) {
        LOG_WARN("CELL_CACHE", "Failed to lock cell data mutex");
        return;
    }

    cell_count_ = new_cell_count;

    // Parse cell voltages
    if (cell_data.containsKey("cell_voltages_mV")) {
        JsonArray voltages = cell_data["cell_voltages_mV"];
        uint16_t count = std::min<uint16_t>(static_cast<uint16_t>(voltages.size()), cell_count_);
        for (uint16_t i = 0; i < count; i++) {
            cell_voltages_mV_[i] = voltages[i];
        }
    }

    // Parse balancing status
    if (cell_data.containsKey("cell_balancing_status")) {
        JsonArray balancing = cell_data["cell_balancing_status"];
        uint16_t count = std::min<uint16_t>(static_cast<uint16_t>(balancing.size()), cell_count_);
        for (uint16_t i = 0; i < count; i++) {
            cell_balancing_status_[i] = balancing[i];
        }
    }

    // Parse statistics
    if (cell_data.containsKey("cell_min_voltage_mV")) {
        cell_min_voltage_mV_ = cell_data["cell_min_voltage_mV"];
    }
    if (cell_data.containsKey("cell_max_voltage_mV")) {
        cell_max_voltage_mV_ = cell_data["cell_max_voltage_mV"];
    }
    if (cell_data.containsKey("balancing_active")) {
        balancing_active_ = cell_data["balancing_active"];
    }

    // Parse data_source field (dummy/live/live_simulated)
    if (cell_data.containsKey("data_source")) {
        const char* source = cell_data["data_source"].as<const char*>();
        if (source) {
            strncpy(cell_data_source_, source, sizeof(cell_data_source_) - 1);
            cell_data_source_[sizeof(cell_data_source_) - 1] = '\0';
        } else {
            strncpy(cell_data_source_, "unknown", sizeof(cell_data_source_) - 1);
            cell_data_source_[sizeof(cell_data_source_) - 1] = '\0';
        }
    } else {
        strncpy(cell_data_source_, "unknown", sizeof(cell_data_source_) - 1);
        cell_data_source_[sizeof(cell_data_source_) - 1] = '\0';
    }

    cell_data_known_ = true;

    if (requested_count > MAX_CELL_COUNT) {
        LOG_WARN("CELL_CACHE", "Clamped cell data from %u to %u cells", requested_count, MAX_CELL_COUNT);
    }

    LOG_INFO("CELL_CACHE", "Stored cell data: %d cells, min=%dmV, max=%dmV, source=%s",
             cell_count_, cell_min_voltage_mV_, cell_max_voltage_mV_, cell_data_source_);

    SSENotifier::notifyCellDataUpdated();
}

bool has_cell_data() {
    ensure_mutex();
    ScopedMutex guard(cell_data_mutex_);
    if (!guard.locked()) {
        return false;
    }
    return cell_data_known_;
}

uint16_t get_cell_count() {
    ensure_mutex();
    ScopedMutex guard(cell_data_mutex_);
    if (!guard.locked()) {
        return 0;
    }
    return cell_count_;
}

const uint16_t* get_cell_voltages() {
    return cell_voltages_mV_;
}

const bool* get_cell_balancing_status() {
    return cell_balancing_status_;
}

uint16_t get_cell_min_voltage() {
    ensure_mutex();
    ScopedMutex guard(cell_data_mutex_);
    if (!guard.locked()) {
        return 0;
    }
    return cell_min_voltage_mV_;
}

uint16_t get_cell_max_voltage() {
    ensure_mutex();
    ScopedMutex guard(cell_data_mutex_);
    if (!guard.locked()) {
        return 0;
    }
    return cell_max_voltage_mV_;
}

bool is_balancing_active() {
    ensure_mutex();
    ScopedMutex guard(cell_data_mutex_);
    if (!guard.locked()) {
        return false;
    }
    return balancing_active_;
}

const char* get_cell_data_source() {
    return cell_data_source_;
}

bool get_cell_data_snapshot(CellDataSnapshot& snapshot) {
    ensure_mutex();

    snapshot.known = false;
    snapshot.cell_count = 0;
    snapshot.min_voltage_mV = 0;
    snapshot.max_voltage_mV = 0;
    snapshot.balancing_active = false;
    snapshot.voltages_mV.clear();
    snapshot.balancing_status.clear();
    strncpy(snapshot.data_source, "unknown", sizeof(snapshot.data_source) - 1);
    snapshot.data_source[sizeof(snapshot.data_source) - 1] = '\0';

    ScopedMutex guard(cell_data_mutex_);
    if (!guard.locked()) {
        return false;
    }

    snapshot.known = cell_data_known_;
    snapshot.cell_count = cell_count_;
    snapshot.min_voltage_mV = cell_min_voltage_mV_;
    snapshot.max_voltage_mV = cell_max_voltage_mV_;
    snapshot.balancing_active = balancing_active_;
    strncpy(snapshot.data_source, cell_data_source_, sizeof(snapshot.data_source) - 1);
    snapshot.data_source[sizeof(snapshot.data_source) - 1] = '\0';

    snapshot.voltages_mV.reserve(cell_count_);
    snapshot.balancing_status.reserve(cell_count_);
    for (uint16_t i = 0; i < cell_count_; ++i) {
        snapshot.voltages_mV.push_back(cell_voltages_mV_[i]);
        snapshot.balancing_status.push_back(cell_balancing_status_[i]);
    }

    return snapshot.known;
}

} // namespace CellDataCache
