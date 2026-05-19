#include "charger_runtime_cache.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
SemaphoreHandle_t g_mutex = nullptr;
ChargerRuntimeCache::Snapshot g_snapshot{};

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

namespace ChargerRuntimeCache {

void update(float hv_voltage_V,
            float hv_current_A,
            float lv_voltage_V,
            float lv_current_A,
            uint16_t ac_voltage_V,
            float ac_current_A,
            uint16_t power_W,
            uint8_t charger_status) {
    if (!lock()) {
        return;
    }
    g_snapshot.known = true;
    g_snapshot.hv_voltage_V = hv_voltage_V;
    g_snapshot.hv_current_A = hv_current_A;
    g_snapshot.lv_voltage_V = lv_voltage_V;
    g_snapshot.lv_current_A = lv_current_A;
    g_snapshot.ac_voltage_V = ac_voltage_V;
    g_snapshot.ac_current_A = ac_current_A;
    g_snapshot.power_W = power_W;
    g_snapshot.charger_status = charger_status;
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

}  // namespace ChargerRuntimeCache
