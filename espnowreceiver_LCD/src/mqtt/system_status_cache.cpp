#include "system_status_cache.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
SemaphoreHandle_t g_mutex = nullptr;
SystemStatusCache::Snapshot g_snapshot{};

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

namespace SystemStatusCache {

void update(uint8_t contactor_state,
            uint8_t error_flags,
            uint8_t warning_flags,
            uint32_t uptime_seconds) {
    if (!lock()) {
        return;
    }
    g_snapshot.known = true;
    g_snapshot.contactor_state = contactor_state;
    g_snapshot.error_flags = error_flags;
    g_snapshot.warning_flags = warning_flags;
    g_snapshot.uptime_seconds = uptime_seconds;
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

}  // namespace SystemStatusCache
