// ESP-NOW Phase 2 settings synchronisation handler implementations.
// Handles: settings update ACK, settings changed notification, component apply ACK.
// Companion translation unit to espnow_tasks.cpp.
// See espnow_tasks_internal.h for shared declarations.

#include "espnow_tasks_internal.h"
#include "battery_settings_cache.h"
#include "component_apply_tracker.h"
#include "../common.h"
#include <esp32common/espnow/packet_utils.h>

static constexpr const char* kLogTag = "ESPNOW";

// Helper to request a granular re-fetch of the given settings category.
// Defined before callers to avoid a forward declaration.
static void request_category_refresh(const uint8_t* mac, uint8_t category, const char* reason) {
    esp_err_t result = ESP_OK;
    
    switch (category) {
        case SETTINGS_BATTERY:
            // Request battery settings only (transmitter sends battery_settings_full_msg_t)
            LOG_INFO(kLogTag, "Requesting battery settings refresh %s", reason);
            {
                request_data_t req = { msg_request_data, subtype_battery_config };
                result = esp_now_send(mac, (const uint8_t*)&req, sizeof(req));
            }
            break;

        case SETTINGS_POWER:
        case SETTINGS_CAN:
        case SETTINGS_CONTACTOR:
            // These categories are transported via the same battery config/settings payload currently.
            LOG_INFO(kLogTag, "Requesting hardware settings refresh %s (category=%u)", reason, static_cast<unsigned>(category));
            {
                request_data_t req = { msg_request_data, subtype_battery_config };
                result = esp_now_send(mac, (const uint8_t*)&req, sizeof(req));
            }
            break;
            
        case SETTINGS_CHARGER:
            // TODO Phase 3: Request charger settings only
            // request_data_t req = { msg_request_data, subtype_charger_config };
            LOG_WARN(kLogTag, "Charger settings refresh not yet implemented");
            break;
            
        case SETTINGS_INVERTER:
            // TODO Phase 3: Request inverter settings only
            // request_data_t req = { msg_request_data, subtype_inverter };
            LOG_WARN(kLogTag, "Inverter settings refresh not yet implemented");
            break;
            
        case SETTINGS_SYSTEM:
            // TODO Phase 3: Request system settings only
            // request_data_t req = { msg_request_data, subtype_system };
            LOG_WARN(kLogTag, "System settings refresh not yet implemented");
            break;
            
        default:
            LOG_ERROR(kLogTag, "Unknown settings category: %d", category);
            return;
    }
    
    if (result != ESP_OK) {
        LOG_WARN(kLogTag, "Failed to request category %d refresh: %s", category, esp_err_to_name(result));
    }
}

void handle_settings_update_ack(const espnow_queue_msg_t* msg) {
    if (msg->len < sizeof(settings_update_ack_msg_t)) {
        LOG_WARN(kLogTag, "Settings update ACK message too short: %d bytes", msg->len);
        return;
    }
    
    const settings_update_ack_msg_t* ack = reinterpret_cast<const settings_update_ack_msg_t*>(msg->data);
    
    const char* category_str = (ack->category == SETTINGS_BATTERY) ? "BATTERY" :
                               (ack->category == SETTINGS_CHARGER) ? "CHARGER" :
                               (ack->category == SETTINGS_INVERTER) ? "INVERTER" :
                               (ack->category == SETTINGS_POWER) ? "POWER" :
                               (ack->category == SETTINGS_CAN) ? "CAN" :
                               (ack->category == SETTINGS_CONTACTOR) ? "CONTACTOR" : "UNKNOWN";
    
    if (ack->success) {
        LOG_INFO(kLogTag, "✓ Settings update ACK: category=%s, field=%d, version=%u", 
                 category_str, ack->field_id, ack->new_version);
        
        // Update our cached version for this category
        // Note: Currently only battery settings have version tracking
        // TODO Phase 3: Add version tracking for charger/inverter/system settings
        if (ack->category == SETTINGS_BATTERY) {
            BatterySettingsCache::instance().mark_updated(ack->new_version);
        }
        
        // GRANULAR REFRESH: Request ONLY the category that was updated
        // The ACK message doesn't contain the new value, so we need to re-request
        // the updated settings from the transmitter to refresh our local cache
        request_category_refresh(msg->mac, ack->category, "after successful update");
        
    } else {
        LOG_ERROR(kLogTag, "✗ Settings update FAILED: category=%s, field=%d, error=%s", 
                  category_str, ack->field_id, ack->error_msg);
        
        // On failure, request current settings to ensure we're in sync with transmitter
        request_category_refresh(msg->mac, ack->category, "to verify state after failure");
    }
}

void handle_settings_changed(const espnow_queue_msg_t* msg) {
    if (msg->len < sizeof(settings_changed_msg_t)) {
        LOG_WARN(kLogTag, "Settings changed message too short: %d bytes", msg->len);
        return;
    }
    
    const settings_changed_msg_t* change = reinterpret_cast<const settings_changed_msg_t*>(msg->data);
    
    // Verify checksum using common utility
    if (!EspnowPacketUtils::verify_message_checksum(change)) {
        uint16_t calc_checksum = EspnowPacketUtils::calculate_message_checksum(change);
        LOG_WARN(kLogTag, "Settings changed checksum mismatch: calc=%u, recv=%u", calc_checksum, change->checksum);
        return;
    }
    
    const char* category_str = (change->category == SETTINGS_BATTERY) ? "BATTERY" :
                               (change->category == SETTINGS_CHARGER) ? "CHARGER" :
                               (change->category == SETTINGS_INVERTER) ? "INVERTER" :
                               (change->category == SETTINGS_POWER) ? "POWER" :
                               (change->category == SETTINGS_CAN) ? "CAN" :
                               (change->category == SETTINGS_CONTACTOR) ? "CONTACTOR" : "UNKNOWN";
    
    LOG_INFO(kLogTag, "⚡ Settings changed notification: category=%s, new_version=%u", category_str, change->new_version);
    
    // Update our local version number to match transmitter
    // Note: Individual field updates are already handled via msg_battery_settings_update ACK
    // We don't need to request full settings - just update the version
    BatterySettingsCache::instance().mark_updated(change->new_version);
    LOG_DEBUG(kLogTag, "Version updated to %u (no full settings refresh needed)", change->new_version);
}

void handle_component_apply_ack_message(const espnow_queue_msg_t* msg) {
    if (msg->len < (int)sizeof(component_apply_ack_t)) {
        LOG_WARN(kLogTag, "Component apply ACK too short: %d bytes", msg->len);
        return;
    }

    const component_apply_ack_t* ack = reinterpret_cast<const component_apply_ack_t*>(msg->data);

    uint16_t calculated = 0;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(ack);
    for (size_t i = 0; i < sizeof(component_apply_ack_t) - sizeof(ack->checksum); ++i) {
        calculated += bytes[i];
    }

    if (calculated != ack->checksum) {
        LOG_WARN(kLogTag, "Component apply ACK checksum mismatch: calc=%u recv=%u", calculated, ack->checksum);
        return;
    }

    LOG_INFO(kLogTag,
             "Component apply ACK: request_id=%lu success=%u reboot_required=%u ready=%u mask=0x%02X persisted=0x%02X msg=%s",
             static_cast<unsigned long>(ack->request_id),
             static_cast<unsigned>(ack->success),
             static_cast<unsigned>(ack->reboot_required),
             static_cast<unsigned>(ack->ready_for_reboot),
             static_cast<unsigned>(ack->apply_mask),
             static_cast<unsigned>(ack->persisted_mask),
             ack->message);

    ComponentApplyTracker::on_ack(*ack);
}
