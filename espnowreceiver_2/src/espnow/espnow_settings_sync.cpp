/**
 * @file espnow_settings_sync.cpp
 * @brief ESP-NOW settings synchronisation handlers (espnowreceiver_2).
 *
 * Implements EspnowSettingsSync namespace (see espnow_settings_sync.h).
 *
 * Compared with espnowreceiver_LCD this implementation additionally:
 *  - Triggers a granular category re-fetch after every ACK (success or failure)
 *  - Updates BatterySettingsCache version on settings_changed notifications
 *  - Forwards component_apply_ack to ComponentApplyTracker
 */

#include "espnow_settings_sync.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "battery_settings_cache.h"
#include "component_apply_tracker.h"
#include "../config/logging_config.h"
#include <esp32common/espnow/packet_utils.h>
#include <esp_now.h>

namespace {

SemaphoreHandle_t g_mutex    = nullptr;
EspnowSettingsSync::Snapshot g_snapshot;

bool ensure_mutex() {
    if (g_mutex) return true;
    g_mutex = xSemaphoreCreateMutex();
    return g_mutex != nullptr;
}

bool lock_state(uint32_t timeout_ms = 20) {
    if (!ensure_mutex()) return false;
    return xSemaphoreTake(g_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void unlock_state() {
    if (g_mutex) xSemaphoreGive(g_mutex);
}

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

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace EspnowSettingsSync {

bool handle_settings_update_ack(const espnow_queue_msg_t* msg) {
    if (!msg || msg->len < static_cast<int>(sizeof(settings_update_ack_msg_t))) {
        LOG_WARN("SETTINGS", "Settings update ACK too short: %d bytes", msg ? msg->len : -1);
        return false;
    }

    const auto* ack = reinterpret_cast<const settings_update_ack_msg_t*>(msg->data);

    if (!lock_state()) return false;
    g_snapshot.last_category       = ack->category;
    g_snapshot.last_field_id        = ack->field_id;
    g_snapshot.last_update_success  = ack->success;
    g_snapshot.settings_version     = ack->new_version;
    unlock_state();

    const char* cat = (ack->category == SETTINGS_BATTERY)   ? "BATTERY"   :
                      (ack->category == SETTINGS_CHARGER)   ? "CHARGER"   :
                      (ack->category == SETTINGS_INVERTER)  ? "INVERTER"  :
                      (ack->category == SETTINGS_POWER)     ? "POWER"     :
                      (ack->category == SETTINGS_CAN)       ? "CAN"       :
                      (ack->category == SETTINGS_CONTACTOR) ? "CONTACTOR" : "UNKNOWN";

    if (ack->success) {
        LOG_INFO("SETTINGS", "✓ ACK ok: category=%s field=%u version=%lu",
                 cat,
                 static_cast<unsigned>(ack->field_id),
                 static_cast<unsigned long>(ack->new_version));

        if (ack->category == SETTINGS_BATTERY) {
            BatterySettingsCache::instance().mark_updated(ack->new_version);
        }

        // Granular re-fetch so receiver converges with transmitter truth
        request_category_refresh(msg->mac, ack->category, "after successful update");
    } else {
        LOG_WARN("SETTINGS", "✗ ACK fail: category=%s field=%u err=%s",
                 cat,
                 static_cast<unsigned>(ack->field_id),
                 ack->error_msg);

        // Re-fetch current state after failure to verify we are still in sync
        request_category_refresh(msg->mac, ack->category, "to verify state after failure");
    }

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

    if (!lock_state()) return false;
    g_snapshot.changed_category  = change->category;
    g_snapshot.changed_version   = change->new_version;
    g_snapshot.settings_version  = change->new_version;
    unlock_state();

    LOG_INFO("SETTINGS", "⚡ Changed: category=%u version=%lu",
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

    // Additive checksum verification
    uint16_t calculated = 0;
    const auto* bytes = reinterpret_cast<const uint8_t*>(ack);
    for (size_t i = 0; i < sizeof(component_apply_ack_t) - sizeof(ack->checksum); ++i) {
        calculated = static_cast<uint16_t>(calculated + bytes[i]);
    }
    if (calculated != ack->checksum) {
        LOG_WARN("SETTINGS", "Component apply ACK checksum mismatch: calc=%u recv=%u",
                 static_cast<unsigned>(calculated),
                 static_cast<unsigned>(ack->checksum));
        return false;
    }

    if (!lock_state()) return false;
    g_snapshot.last_component_apply_request_id = ack->request_id;
    g_snapshot.component_apply_success          = ack->success != 0;
    g_snapshot.component_apply_reboot_required  = ack->reboot_required != 0;
    g_snapshot.component_settings_version       = ack->settings_version;
    unlock_state();

    LOG_INFO("SETTINGS",
             "Component apply ACK: request_id=%lu success=%u reboot_required=%u "
             "ready=%u mask=0x%02X persisted=0x%02X msg=%s",
             static_cast<unsigned long>(ack->request_id),
             static_cast<unsigned>(ack->success),
             static_cast<unsigned>(ack->reboot_required),
             static_cast<unsigned>(ack->ready_for_reboot),
             static_cast<unsigned>(ack->apply_mask),
             static_cast<unsigned>(ack->persisted_mask),
             ack->message);

    ComponentApplyTracker::on_ack(*ack);

    return true;
}

bool read_snapshot(Snapshot& out_snapshot) {
    if (!lock_state()) return false;
    out_snapshot = g_snapshot;
    unlock_state();
    return true;
}

}  // namespace EspnowSettingsSync
