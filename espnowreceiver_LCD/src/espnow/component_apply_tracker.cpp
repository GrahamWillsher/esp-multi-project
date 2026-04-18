#include "component_apply_tracker.h"

#include <cstring>
#include <esp32common/logging/logging_config.h>

namespace {
constexpr uint32_t kApplyTimeoutMs = 15000;
using Coordinator = Esp32Common::Patterns::SaveApplyCoordinator;
SemaphoreHandle_t g_mutex = nullptr;
ComponentApplyTracker::Snapshot g_snapshot{};
Coordinator g_coordinator(kApplyTimeoutMs);
bool g_initialized = false;
constexpr const char* kLogTag = "COMP_APPLY_TRACKER";

struct ScopedLock {
    ScopedLock() : locked(false) {
        if (g_mutex != nullptr) {
            locked = (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE);
        }
    }

    ~ScopedLock() {
        if (locked) {
            xSemaphoreGive(g_mutex);
        }
    }

    bool locked;
};

void sync_snapshot_from_coordinator(const Coordinator::Snapshot& source) {
    g_snapshot.request_id = source.request_id;
    g_snapshot.state = source.state;
    g_snapshot.started_ms = source.started_ms;
    g_snapshot.updated_ms = source.updated_ms;
    g_snapshot.apply_mask = source.requested_mask;
    g_snapshot.persisted_mask = source.persisted_mask;
    g_snapshot.success = source.success;
    g_snapshot.reboot_required = source.reboot_required;
    g_snapshot.ready_for_reboot = source.ready_for_reboot;
    g_snapshot.settings_version = source.settings_version;
    strncpy(g_snapshot.message, source.message, sizeof(g_snapshot.message) - 1);
    g_snapshot.message[sizeof(g_snapshot.message) - 1] = '\0';
}

} // namespace

namespace ComponentApplyTracker {

void init() {
    if (g_mutex == nullptr) {
        g_mutex = xSemaphoreCreateMutex();
        if (g_mutex == nullptr) {
            return;
        }
    }

    if (g_initialized) {
        return;
    }

    ScopedLock lock;
    if (!lock.locked) {
        return;
    }

    if (g_initialized) {
        return;
    }

    g_snapshot = Snapshot{};
    g_coordinator.reset();
    sync_snapshot_from_coordinator(g_coordinator.snapshot(millis()));
    g_initialized = true;
}

bool start_transaction(uint32_t request_id,
                       uint8_t apply_mask,
                       uint8_t battery_type,
                       uint8_t inverter_type,
                       uint8_t battery_interface,
                       uint8_t inverter_interface) {
    init();
    ScopedLock lock;
    if (!lock.locked) {
        return false;
    }

    const uint32_t now = millis();
    g_snapshot = Snapshot{};
    if (!g_coordinator.start_transaction(request_id, apply_mask, now)) {
        return false;
    }

    sync_snapshot_from_coordinator(g_coordinator.snapshot(now));
    g_snapshot.apply_mask = apply_mask;
    g_snapshot.battery_type = battery_type;
    g_snapshot.inverter_type = inverter_type;
    g_snapshot.battery_interface = battery_interface;
    g_snapshot.inverter_interface = inverter_interface;
    return true;
}

void mark_failed(uint32_t request_id, const char* message) {
    init();
    ScopedLock lock;
    if (!lock.locked) {
        return;
    }

    if (g_snapshot.request_id != request_id) {
        return;
    }

    const uint32_t now = millis();
    if (g_coordinator.mark_failed(request_id, now, message)) {
        sync_snapshot_from_coordinator(g_coordinator.snapshot(now));
    }
}

void on_ack(const component_apply_ack_t& ack) {
    init();
    ScopedLock lock;
    if (!lock.locked) {
        LOG_WARN(kLogTag, "ACK ignored: tracker lock unavailable (request_id=%lu)",
                 static_cast<unsigned long>(ack.request_id));
        return;
    }

    // Recovery path: if no active transaction snapshot is present but a valid
    // ACK arrives, bind snapshot to this ACK so status polling can proceed.
    if (g_snapshot.request_id == 0) {
        LOG_WARN(kLogTag,
                 "ACK recovery: binding empty snapshot to request_id=%lu",
                 static_cast<unsigned long>(ack.request_id));
        const uint32_t now = millis();
        g_coordinator.bind_recovery_request(ack.request_id, now);
        sync_snapshot_from_coordinator(g_coordinator.snapshot(now));
    }

    const uint32_t now = millis();
    if (!g_coordinator.apply_result(ack.request_id,
                                    ack.apply_mask,
                                    ack.persisted_mask,
                                    (ack.success != 0),
                                    (ack.reboot_required != 0),
                                    (ack.ready_for_reboot != 0),
                                    ack.settings_version,
                                    now,
                                    ack.message)) {
        LOG_WARN(kLogTag,
                 "ACK ignored: request mismatch (snapshot=%lu ack=%lu)",
                 static_cast<unsigned long>(g_snapshot.request_id),
                 static_cast<unsigned long>(ack.request_id));
        return;
    }

    sync_snapshot_from_coordinator(g_coordinator.snapshot(now));
    g_snapshot.battery_type = ack.battery_type;
    g_snapshot.inverter_type = ack.inverter_type;
    g_snapshot.battery_interface = ack.battery_interface;
    g_snapshot.inverter_interface = ack.inverter_interface;
    LOG_INFO(kLogTag,
             "ACK applied: request_id=%lu state=%u success=%u ready=%u persisted=0x%02X apply=0x%02X",
             static_cast<unsigned long>(g_snapshot.request_id),
             static_cast<unsigned>(g_snapshot.state),
             static_cast<unsigned>(g_snapshot.success),
             static_cast<unsigned>(g_snapshot.ready_for_reboot),
             static_cast<unsigned>(g_snapshot.persisted_mask),
             static_cast<unsigned>(g_snapshot.apply_mask));
}

Snapshot get_snapshot() {
    init();
    ScopedLock lock;
    if (!lock.locked) {
        Snapshot fallback{};
        fallback.state = State::failed;
        strncpy(fallback.message, "Tracker lock unavailable", sizeof(fallback.message) - 1);
        fallback.message[sizeof(fallback.message) - 1] = '\0';
        return fallback;
    }

    sync_snapshot_from_coordinator(g_coordinator.snapshot(millis()));

    return g_snapshot;
}

} // namespace ComponentApplyTracker
