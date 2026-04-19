#include "espnow/espnow_settings_sync.h"

#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <esp32common/espnow/packet_utils.h>

#include "logging_config.h"
#include "espnow/battery_settings_cache.h"
#include "espnow/component_apply_tracker.h"
#include <esp_now.h>

namespace {

// ---------------------------------------------------------------------------
// Category re-fetch helper
// ---------------------------------------------------------------------------

static void request_category_refresh(const uint8_t* mac, uint8_t category, const char* reason) {
    esp_err_t result = ESP_OK;

    switch (category) {
        case SETTINGS_BATTERY:
        case SETTINGS_POWER:
        case SETTINGS_CAN:
        case SETTINGS_CONTACTOR: {
            LOG_INFO("SETTINGS", "Requesting battery/hardware settings refresh %s (category=%u)",
                     reason, static_cast<unsigned>(category));
            request_data_t req = {msg_request_data, subtype_battery_config};
            result = esp_now_send(mac, reinterpret_cast<const uint8_t*>(&req), sizeof(req));
            break;
        }
        case SETTINGS_CHARGER:
            LOG_WARN("SETTINGS", "Charger settings refresh not yet implemented");
            return;
        case SETTINGS_INVERTER:
            LOG_WARN("SETTINGS", "Inverter settings refresh not yet implemented");
            return;
        case SETTINGS_SYSTEM:
            LOG_WARN("SETTINGS", "System settings refresh not yet implemented");
            return;
        default:
            LOG_ERROR("SETTINGS", "Unknown settings category: %u", static_cast<unsigned>(category));
            return;
    }

    if (result != ESP_OK) {
        LOG_WARN("SETTINGS", "Failed to request category %u refresh: %s",
                 static_cast<unsigned>(category), esp_err_to_name(result));
    }
}


SemaphoreHandle_t g_mutex = nullptr;
EspnowSettingsSync::Snapshot g_snapshot;

bool ensure_mutex() {
    if (g_mutex != nullptr) {
        return true;
    }
    g_mutex = xSemaphoreCreateMutex();
    return g_mutex != nullptr;
}

bool lock_state(uint32_t timeout_ms = 20) {
    if (!ensure_mutex()) {
        return false;
    }
    return xSemaphoreTake(g_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void unlock_state() {
    if (g_mutex) {
        xSemaphoreGive(g_mutex);
    }
}

}  // namespace

namespace EspnowSettingsSync {

bool handle_settings_update_ack(const espnow_queue_msg_t* msg) {
    if (!msg || msg->len < static_cast<int>(sizeof(settings_update_ack_msg_t))) {
        LOG_WARN("SETTINGS", "Settings update ACK too short: %d bytes", msg ? msg->len : -1);
        return false;
    }

    const auto* ack = reinterpret_cast<const settings_update_ack_msg_t*>(msg->data);

    if (!EspnowPacketUtils::verify_message_crc32(ack)) {
        LOG_WARN("SETTINGS", "ACK CRC32 mismatch: stored=0x%08lX",
                 static_cast<unsigned long>(ack->checksum));
        return false;
    }

    if (!lock_state()) {
        return false;
    }

    g_snapshot.last_category = ack->category;
    g_snapshot.last_field_id = ack->field_id;
    g_snapshot.last_update_success = ack->success;
    g_snapshot.settings_version = ack->new_version;

    unlock_state();

    if (ack->success) {
        LOG_INFO("SETTINGS", "ACK ok: category=%u field=%u version=%lu",
                 static_cast<unsigned>(ack->category),
                 static_cast<unsigned>(ack->field_id),
                 static_cast<unsigned long>(ack->new_version));
    } else {
        LOG_WARN("SETTINGS", "ACK fail: category=%u field=%u err=%s",
                 static_cast<unsigned>(ack->category),
                 static_cast<unsigned>(ack->field_id),
                 ack->error_msg);
    }

    if (ack->success && ack->category == SETTINGS_BATTERY) {
        BatterySettingsCache::instance().mark_updated(ack->new_version);
    }

    // Granular re-fetch so receiver converges with transmitter truth
    const char* refetch_reason = ack->success ? "after successful update" : "to verify state after failure";
    request_category_refresh(msg->mac, ack->category, refetch_reason);

    return true;
}

bool handle_settings_changed(const espnow_queue_msg_t* msg) {
    if (!msg || msg->len < static_cast<int>(sizeof(settings_changed_msg_t))) {
        LOG_WARN("SETTINGS", "Settings changed too short: %d bytes", msg ? msg->len : -1);
        return false;
    }

    const auto* change = reinterpret_cast<const settings_changed_msg_t*>(msg->data);

    if (!EspnowPacketUtils::verify_message_crc32(change)) {
        LOG_WARN("SETTINGS", "Changed CRC32 mismatch: stored=0x%08lX",
                 static_cast<unsigned long>(change->checksum));
        return false;
    }

    if (!lock_state()) {
        return false;
    }

    g_snapshot.changed_category = change->category;
    g_snapshot.changed_version = change->new_version;
    g_snapshot.settings_version = change->new_version;

    unlock_state();

    LOG_INFO("SETTINGS", "Changed: category=%u version=%lu",
             static_cast<unsigned>(change->category),
             static_cast<unsigned long>(change->new_version));

    // Update local version so freshness checks remain accurate
    BatterySettingsCache::instance().mark_updated(change->new_version);

    return true;
}

bool handle_component_apply_ack(const espnow_queue_msg_t* msg) {
    if (!msg || msg->len < static_cast<int>(sizeof(component_apply_ack_t))) {
        LOG_WARN("SETTINGS", "Component apply ACK too short: %d bytes", msg ? msg->len : -1);
        return false;
    }

    const auto* ack = reinterpret_cast<const component_apply_ack_t*>(msg->data);

    if (!EspnowPacketUtils::verify_message_crc32(ack)) {
        LOG_WARN("SETTINGS", "Component apply ACK CRC32 mismatch: stored=0x%08lX",
                 static_cast<unsigned long>(ack->checksum));
        return false;
    }

    if (!lock_state()) {
        return false;
    }

    g_snapshot.last_component_apply_request_id = ack->request_id;
    g_snapshot.component_apply_success = ack->success != 0;
    g_snapshot.component_apply_reboot_required = ack->reboot_required != 0;
    g_snapshot.component_settings_version = ack->settings_version;

    unlock_state();

    LOG_INFO("SETTINGS", "Component apply ACK: request_id=%lu success=%u reboot_required=%u",
             static_cast<unsigned long>(ack->request_id),
             static_cast<unsigned>(ack->success),
             static_cast<unsigned>(ack->reboot_required));

    ComponentApplyTracker::on_ack(*ack);

    return true;
}

bool read_snapshot(Snapshot& out_snapshot) {
    if (!lock_state()) {
        return false;
    }

    out_snapshot = g_snapshot;
    unlock_state();
    return true;
}

}  // namespace EspnowSettingsSync
