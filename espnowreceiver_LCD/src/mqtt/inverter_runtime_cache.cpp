#include "inverter_runtime_cache.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
SemaphoreHandle_t g_mutex = nullptr;
InverterRuntimeCache::Snapshot g_snapshot{};

bool ensure_mutex() {
    if (g_mutex != nullptr) {
        return true;
    }
    g_mutex = xSemaphoreCreateMutex();
    return g_mutex != nullptr;
}

bool lock(uint32_t timeout_ms = 20) {
    if (!ensure_mutex()) {
        return false;
    }
    return xSemaphoreTake(g_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void unlock() {
    if (g_mutex != nullptr) {
        xSemaphoreGive(g_mutex);
    }
}
}  // namespace

namespace InverterRuntimeCache {

void update(uint16_t ac_voltage_V,
            uint16_t ac_frequency_dHz,
            int16_t ac_current_dA,
            int32_t power_W,
            uint8_t inverter_status) {
    if (!lock()) {
        return;
    }
    g_snapshot.known = true;
    g_snapshot.ac_voltage_V = ac_voltage_V;
    g_snapshot.ac_frequency_dHz = ac_frequency_dHz;
    g_snapshot.ac_current_dA = ac_current_dA;
    g_snapshot.power_W = power_W;
    g_snapshot.inverter_status = inverter_status;
    g_snapshot.last_update_ms = millis();
    unlock();
}

bool read_snapshot(Snapshot& out_snapshot) {
    if (!lock()) {
        return false;
    }
    out_snapshot = g_snapshot;
    unlock();
    return true;
}

}  // namespace InverterRuntimeCache
