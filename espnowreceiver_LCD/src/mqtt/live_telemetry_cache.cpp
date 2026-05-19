#include "live_telemetry_cache.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
SemaphoreHandle_t g_snapshot_mutex = nullptr;
LiveTelemetryCache::Snapshot g_snapshot{};

bool ensure_mutex() {
    if (g_snapshot_mutex != nullptr) {
        return true;
    }
    g_snapshot_mutex = xSemaphoreCreateMutex();
    return g_snapshot_mutex != nullptr;
}

bool lock_snapshot(uint32_t timeout_ms = 20) {
    if (!ensure_mutex()) {
        return false;
    }
    return xSemaphoreTake(g_snapshot_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void unlock_snapshot() {
    if (g_snapshot_mutex != nullptr) {
        xSemaphoreGive(g_snapshot_mutex);
    }
}
}  // namespace

namespace LiveTelemetryCache {

void update_basic(uint8_t soc_percent, int32_t power_w, uint32_t voltage_mv) {
    if (!lock_snapshot()) {
        return;
    }

    g_snapshot.known = true;
    g_snapshot.soc_percent = soc_percent;
    g_snapshot.power_w = power_w;
    g_snapshot.voltage_mv = voltage_mv;
    g_snapshot.last_update_ms = millis();
    unlock_snapshot();
}

bool read_snapshot(Snapshot& out_snapshot) {
    if (!lock_snapshot()) {
        return false;
    }

    out_snapshot = g_snapshot;
    unlock_snapshot();
    return true;
}

}  // namespace LiveTelemetryCache
