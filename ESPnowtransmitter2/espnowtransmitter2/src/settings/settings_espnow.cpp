// settings_espnow.cpp
// Implements ESP-NOW send/receive for SettingsManager:
//   handle_settings_update  – receives and dispatches incoming settings messages
//   send_settings_ack       – sends acknowledgment back to the sender
//   send_settings_changed_notification – broadcasts a version-changed event
//
// Extracted from settings_manager.cpp to separate the ESP-NOW transport layer
// from NVS persistence and field-setter dispatch.

#include "settings_manager.h"
#include "../config/logging_config.h"
#include <esp_now.h>
#include <esp32common/espnow/connection_manager.h>
#include <esp32common/espnow/packet_utils.h>
#include <cstring>

void SettingsManager::handle_settings_update(const espnow_queue_msg_t& msg) {
    LOG_INFO("SETTINGS", "═══ Settings Update Message Received ═══");
    LOG_INFO("SETTINGS", "Message length: %d bytes (expected: %d bytes)",
             msg.len, sizeof(settings_update_msg_t));

    if (msg.len < (int)sizeof(settings_update_msg_t)) {
        LOG_ERROR("SETTINGS", "Invalid message size: %d", msg.len);
        send_settings_ack(msg.mac, 0, 0, false, 0, "Invalid message size");
        return;
    }

    const settings_update_msg_t* update =
        (const settings_update_msg_t*)msg.data;

    // Defensive copy: incoming transport strings are untrusted bytes and may
    // not be NUL-terminated.
    char safe_value_string[sizeof(update->value_string) + 1] = {0};
    memcpy(safe_value_string,
           update->value_string,
           sizeof(update->value_string));
    safe_value_string[sizeof(update->value_string)] = '\0';

    LOG_INFO("SETTINGS", "From: %02X:%02X:%02X:%02X:%02X:%02X",
             msg.mac[0], msg.mac[1], msg.mac[2],
             msg.mac[3], msg.mac[4], msg.mac[5]);
    LOG_INFO("SETTINGS", "Type=%d, Category=%d, Field=%d",
             update->type, update->category, update->field_id);
    LOG_INFO("SETTINGS", "Values - uint32=%u, float=%.2f, string='%s'",
             update->value_uint32, update->value_float, safe_value_string);
    LOG_INFO("SETTINGS", "Checksum: %u", update->checksum);

    // Verify XOR checksum
    uint8_t calculated_checksum = 0;
    const uint8_t* bytes = (const uint8_t*)update;
    for (size_t i = 0;
         i < sizeof(settings_update_msg_t) - sizeof(update->checksum); i++) {
        calculated_checksum ^= bytes[i];
    }

    if (calculated_checksum != update->checksum) {
        LOG_ERROR("SETTINGS",
                  "Checksum mismatch! Expected=%u, Got=%u",
                  calculated_checksum, update->checksum);
        send_settings_ack(msg.mac, update->category, update->field_id,
                          false, 0, "Checksum error");
        return;
    }
    LOG_INFO("SETTINGS", "✓ Checksum valid");

    bool success     = false;
    char error_msg[48] = "";
    uint32_t new_version = 0;

    switch (update->category) {
        case SETTINGS_BATTERY:
            success = save_battery_setting(update->field_id,
                                           update->value_uint32,
                                           update->value_float,
                                           safe_value_string);
            new_version = battery_settings_version_;
            if (!success) { strlcpy(error_msg, "Invalid value or NVS write failed", sizeof(error_msg)); }
            break;

        case SETTINGS_POWER:
            success = save_power_setting(update->field_id, update->value_uint32);
            new_version = power_settings_version_;
            if (!success) { strlcpy(error_msg, "Invalid value or NVS write failed", sizeof(error_msg)); }
            break;

        case SETTINGS_INVERTER:
            success = save_inverter_setting(update->field_id, update->value_uint32);
            new_version = inverter_settings_version_;
            if (!success) { strlcpy(error_msg, "Invalid value or NVS write failed", sizeof(error_msg)); }
            break;

        case SETTINGS_CAN:
            success = save_can_setting(update->field_id, update->value_uint32);
            new_version = can_settings_version_;
            if (!success) { strlcpy(error_msg, "Invalid value or NVS write failed", sizeof(error_msg)); }
            break;

        case SETTINGS_CONTACTOR:
            success = save_contactor_setting(update->field_id, update->value_uint32);
            new_version = contactor_settings_version_;
            if (!success) { strlcpy(error_msg, "Invalid value or NVS write failed", sizeof(error_msg)); }
            break;

        case SETTINGS_CHARGER:
        case SETTINGS_SYSTEM:
        case SETTINGS_MQTT:
        case SETTINGS_NETWORK:
            LOG_WARN("SETTINGS", "Category %d not yet implemented",
                     update->category);
            strlcpy(error_msg, "Category not implemented yet", sizeof(error_msg));
            break;

        default:
            LOG_ERROR("SETTINGS", "Unknown category: %d", update->category);
                strlcpy(error_msg, "Unknown settings category", sizeof(error_msg));
            break;
    }

    send_settings_ack(msg.mac, update->category, update->field_id,
                      success, new_version, error_msg);
}

void SettingsManager::send_settings_ack(const uint8_t* mac,
                                        uint8_t category,
                                        uint8_t field_id,
                                        bool success,
                                        uint32_t new_version,
                                        const char* error_msg) {
    settings_update_ack_msg_t ack{};
    ack.type       = msg_settings_update_ack;
    ack.category   = category;
    ack.field_id   = field_id;
    ack.success    = success;
    ack.new_version = new_version;

    if (error_msg && strlen(error_msg) > 0) {
        strncpy(ack.error_msg, error_msg, sizeof(ack.error_msg) - 1);
        ack.error_msg[sizeof(ack.error_msg) - 1] = '\0';
    } else {
        ack.error_msg[0] = '\0';
    }

    ack.checksum = EspnowPacketUtils::calculate_message_checksum(&ack);

    const esp_err_t result =
        esp_now_send(mac, (const uint8_t*)&ack, sizeof(ack));

    if (result == ESP_OK) {
        LOG_INFO("SETTINGS", "ACK sent: success=%d, version=%u",
                 success, new_version);
    } else {
        LOG_WARN("SETTINGS",
                 "Failed to send ACK (will retry if receiver requests): %s",
                 esp_err_to_name(result));
    }
}

void SettingsManager::send_settings_changed_notification(uint8_t category,
                                                         uint32_t new_version) {
    settings_changed_msg_t notification;
    notification.type        = msg_settings_changed;
    notification.category    = category;
    notification.new_version = new_version;

    notification.checksum =
        EspnowPacketUtils::calculate_message_checksum(&notification);

    const uint8_t* peer_mac =
        EspNowConnectionManager::instance().get_peer_mac();
    const esp_err_t result =
        esp_now_send(peer_mac, (const uint8_t*)&notification,
                     sizeof(notification));

    if (result == ESP_OK) {
        LOG_INFO("SETTINGS",
                 "Sent change notification: category=%d, version=%u",
                 category, new_version);
    } else {
        LOG_DEBUG("SETTINGS",
                  "Notification send failed (receiver may request update): %s",
                  esp_err_to_name(result));
    }
}
