#include "espnow_send.h"
#include "../common.h"
#include "../espnow/rx_state_machine.h"
#include <esp32common/espnow/connection_manager.h>
#include <esp_now.h>
#include <esp32common/espnow/common.h>
#include <esp32common/espnow/packet_utils.h>

// Track the last debug level sent to transmitter
static uint8_t last_debug_level_sent = 6;  // Default to INFO level

// Track the last test data mode sent to transmitter
static uint8_t last_test_data_mode_sent = 2;  // Default to FULL_BATTERY_DATA

uint8_t get_last_debug_level() {
    return last_debug_level_sent;
}

uint8_t get_last_test_data_mode() {
    return last_test_data_mode_sent;
}

static bool has_transmitter_mac() {
    for (int i = 0; i < 6; i++) {
        if (ESPNow::transmitter_mac[i] != 0) {
            return true;
        }
    }
    return false;
}

static bool can_send_catalog_request() {
    const bool message_valid =
        (RxStateMachine::instance().message_state() == RxStateMachine::MessageState::VALID);
    const bool connected =
        (EspNowConnectionManager::instance().get_state() == EspNowConnectionState::CONNECTED);

    if (!(message_valid || connected)) {
        LOG_WARN("ESP-NOW", "Transmitter not ready - cannot send catalog request (state=%d, msg_valid=%d)",
                 (int)EspNowConnectionManager::instance().get_state(),
                 message_valid ? 1 : 0);
        return false;
    }

    if (!has_transmitter_mac()) {
        LOG_WARN("ESP-NOW", "Transmitter MAC not registered - cannot send catalog request");
        return false;
    }

    return true;
}

bool send_debug_level_control(uint8_t level) {
    // Validate level
    if (level > 7) {
        LOG_ERROR("ESP-NOW", "Invalid debug level: %d (must be 0-7)", level);
        return false;
    }
    
    // Check if transmitter is connected (more reliable than checking MAC)
    if (RxStateMachine::instance().message_state() != RxStateMachine::MessageState::VALID) {
        LOG_WARN("ESP-NOW", "Transmitter not connected - cannot send debug control");
        return false;
    }
    
    // Double-check MAC is not zero (defensive check)
    bool mac_is_zero = true;
    for (int i = 0; i < 6; i++) {
        if (ESPNow::transmitter_mac[i] != 0) {
            mac_is_zero = false;
            break;
        }
    }
    
    if (mac_is_zero) {
        LOG_WARN("ESP-NOW", "Transmitter MAC not registered - cannot send debug control");
        return false;
    }
    
    // Build debug_control_t
    debug_control_t packet;
    packet.type = msg_debug_control;
    packet.level = level;
    packet.flags = 0;  // Reserved for future use
    
    // Calculate checksum (simple XOR)
    packet.checksum = 0;
    uint8_t* data = (uint8_t*)&packet;
    for (size_t i = 0; i < sizeof(packet) - 1; i++) {
        packet.checksum ^= data[i];
    }
    
    // Send via ESP-NOW
    esp_err_t result = esp_now_send(ESPNow::transmitter_mac, (uint8_t*)&packet, sizeof(packet));
    
    if (result == ESP_OK) {
        // Store the level we just sent
        last_debug_level_sent = level;
        LOG_DEBUG("ESP-NOW", "Debug level control sent: level=%d to %02X:%02X:%02X:%02X:%02X:%02X",
                     level,
                     ESPNow::transmitter_mac[0], ESPNow::transmitter_mac[1], ESPNow::transmitter_mac[2],
                     ESPNow::transmitter_mac[3], ESPNow::transmitter_mac[4], ESPNow::transmitter_mac[5]);
        return true;
    } else {
        LOG_ERROR("ESP-NOW", "Failed to send debug control: %s", esp_err_to_name(result));
        return false;
    }
}
bool send_component_type_selection(uint8_t battery_type, uint8_t inverter_type) {
    // Check if transmitter is connected
    if (RxStateMachine::instance().message_state() != RxStateMachine::MessageState::VALID) {
        LOG_WARN("ESP-NOW", "Transmitter not connected - cannot send component type selection");
        return false;
    }
    
    // Double-check MAC is not zero (defensive check)
    bool mac_is_zero = true;
    for (int i = 0; i < 6; i++) {
        if (ESPNow::transmitter_mac[i] != 0) {
            mac_is_zero = false;
            break;
        }
    }
    
    if (mac_is_zero) {
        LOG_WARN("ESP-NOW", "Transmitter MAC not registered - cannot send component type selection");
        return false;
    }
    
    // Build component_config_msg_t
    component_config_msg_t packet;
    packet.type = msg_component_config;
    packet.battery_type = battery_type;
    packet.inverter_type = inverter_type;
    packet.bms_type = 0;                  // Keep unchanged (transmitter manages its own BMS)
    packet.secondary_bms_type = 0;        // Keep unchanged
    packet.charger_type = 0;              // Keep unchanged
    packet.shunt_type = 0;                // Keep unchanged
    packet.multi_battery_enabled = 0;     // Keep unchanged
    packet.config_version = 0;            // Version 0 indicates receiver-initiated update
    
    // Calculate checksum (simple sum of all bytes except checksum)
    packet.checksum = 0;
    uint8_t* data = (uint8_t*)&packet;
    for (size_t i = 0; i < sizeof(packet) - sizeof(packet.checksum); i++) {
        packet.checksum += data[i];
    }
    
    // Send via ESP-NOW
    esp_err_t result = esp_now_send(ESPNow::transmitter_mac, (uint8_t*)&packet, sizeof(packet));
    
    if (result == ESP_OK) {
        LOG_DEBUG("ESP-NOW", "Component type selection sent: battery_type=%d, inverter_type=%d to %02X:%02X:%02X:%02X:%02X:%02X",
                     battery_type, inverter_type,
                     ESPNow::transmitter_mac[0], ESPNow::transmitter_mac[1], ESPNow::transmitter_mac[2],
                     ESPNow::transmitter_mac[3], ESPNow::transmitter_mac[4], ESPNow::transmitter_mac[5]);
        return true;
    } else {
        LOG_ERROR("ESP-NOW", "Failed to send component type selection: %s", esp_err_to_name(result));
        return false;
    }
}

bool send_component_interface_selection(uint8_t battery_interface, uint8_t inverter_interface) {
    // Validate interfaces
    if (battery_interface > 5) {
        LOG_ERROR("ESP-NOW", "Invalid battery interface: %d (must be 0-5)", battery_interface);
        return false;
    }
    
    if (inverter_interface > 5) {
        LOG_ERROR("ESP-NOW", "Invalid inverter interface: %d (must be 0-5)", inverter_interface);
        return false;
    }
    
    // Check if transmitter is connected
    if (RxStateMachine::instance().message_state() != RxStateMachine::MessageState::VALID) {
        LOG_WARN("ESP-NOW", "Transmitter not connected - cannot send component interface selection");
        return false;
    }
    
    // Double-check MAC is not zero (defensive check)
    bool mac_is_zero = true;
    for (int i = 0; i < 6; i++) {
        if (ESPNow::transmitter_mac[i] != 0) {
            mac_is_zero = false;
            break;
        }
    }
    
    if (mac_is_zero) {
        LOG_WARN("ESP-NOW", "Transmitter MAC not registered - cannot send component interface selection");
        return false;
    }
    
    // Build component_interface_msg_t
    component_interface_msg_t packet;
    packet.type = msg_component_interface;
    packet.battery_interface = battery_interface;
    packet.inverter_interface = inverter_interface;
    
    // Calculate checksum (simple sum of all bytes except checksum)
    packet.checksum = 0;
    uint8_t* data = (uint8_t*)&packet;
    for (size_t i = 0; i < sizeof(component_interface_msg_t) - sizeof(packet.checksum); i++) {
        packet.checksum += data[i];
    }
    
    // Send via ESP-NOW
    esp_err_t result = esp_now_send(ESPNow::transmitter_mac, (uint8_t*)&packet, sizeof(packet));
    
    if (result == ESP_OK) {
        LOG_DEBUG("ESP-NOW", "Component interface selection sent: battery_if=%d, inverter_if=%d to %02X:%02X:%02X:%02X:%02X:%02X",
                     battery_interface, inverter_interface,
                     ESPNow::transmitter_mac[0], ESPNow::transmitter_mac[1], ESPNow::transmitter_mac[2],
                     ESPNow::transmitter_mac[3], ESPNow::transmitter_mac[4], ESPNow::transmitter_mac[5]);
        return true;
    } else {
        LOG_ERROR("ESP-NOW", "Failed to send component interface selection: %s", esp_err_to_name(result));
        return false;
    }
}

bool send_test_data_mode_control(uint8_t mode) {
    // Validate mode (0=OFF, 1=SOC_POWER_ONLY, 2=FULL_BATTERY_DATA)
    if (mode > 2) {
        LOG_ERROR("ESP-NOW", "Invalid test data mode: %d (must be 0-2)", mode);
        return false;
    }
    
    // Check if transmitter is connected
    if (RxStateMachine::instance().message_state() != RxStateMachine::MessageState::VALID) {
        LOG_WARN("ESP-NOW", "Transmitter not connected - cannot send test data mode control");
        return false;
    }
    
    // Double-check MAC is not zero (defensive check)
    bool mac_is_zero = true;
    for (int i = 0; i < 6; i++) {
        if (ESPNow::transmitter_mac[i] != 0) {
            mac_is_zero = false;
            break;
        }
    }
    
    if (mac_is_zero) {
        LOG_WARN("ESP-NOW", "Transmitter MAC not registered - cannot send test data mode control");
        return false;
    }
    
    // Build debug_control_t packet (reuse for test data mode - uses same message type)
    debug_control_t packet;
    packet.type = msg_debug_control;  // Reuse debug control message type
    packet.level = mode;  // 0=OFF, 1=SOC_POWER_ONLY, 2=FULL_BATTERY_DATA
    packet.flags = 0x80;  // Set high bit to indicate this is test data control (not debug level)
    
    // Calculate checksum (simple XOR)
    packet.checksum = 0;
    uint8_t* data = (uint8_t*)&packet;
    for (size_t i = 0; i < sizeof(packet) - 1; i++) {
        packet.checksum ^= data[i];
    }
    
    // Send via ESP-NOW
    esp_err_t result = esp_now_send(ESPNow::transmitter_mac, (uint8_t*)&packet, sizeof(packet));
    
    if (result == ESP_OK) {
        const char* mode_str[] = {"OFF", "SOC_POWER_ONLY", "FULL_BATTERY_DATA"};
        // Store the mode we just sent for local caching
        last_test_data_mode_sent = mode;
        LOG_DEBUG("ESP-NOW", "Test data mode control sent: mode=%s to %02X:%02X:%02X:%02X:%02X:%02X",
                     mode_str[mode],
                     ESPNow::transmitter_mac[0], ESPNow::transmitter_mac[1], ESPNow::transmitter_mac[2],
                     ESPNow::transmitter_mac[3], ESPNow::transmitter_mac[4], ESPNow::transmitter_mac[5]);
        return true;
    } else {
        LOG_ERROR("ESP-NOW", "Failed to send test data mode control: %s", esp_err_to_name(result));
        return false;
    }
}

bool send_event_logs_control(bool subscribe) {
    // Check if transmitter is connected
    if (RxStateMachine::instance().message_state() != RxStateMachine::MessageState::VALID) {
        LOG_WARN("ESP-NOW", "Transmitter not connected - cannot send event logs control");
        return false;
    }

    // Double-check MAC is not zero (defensive check)
    bool mac_is_zero = true;
    for (int i = 0; i < 6; i++) {
        if (ESPNow::transmitter_mac[i] != 0) {
            mac_is_zero = false;
            break;
        }
    }

    if (mac_is_zero) {
        LOG_WARN("ESP-NOW", "Transmitter MAC not registered - cannot send event logs control");
        return false;
    }

    event_logs_control_t packet;
    packet.type = msg_event_logs_control;
    packet.action = subscribe ? 1 : 0;

    esp_err_t result = esp_now_send(ESPNow::transmitter_mac, (uint8_t*)&packet, sizeof(packet));
    if (result == ESP_OK) {
        LOG_DEBUG("ESP-NOW", "Event logs control sent: %s to %02X:%02X:%02X:%02X:%02X:%02X",
                     subscribe ? "subscribe" : "unsubscribe",
                     ESPNow::transmitter_mac[0], ESPNow::transmitter_mac[1], ESPNow::transmitter_mac[2],
                     ESPNow::transmitter_mac[3], ESPNow::transmitter_mac[4], ESPNow::transmitter_mac[5]);
        return true;
    }

    LOG_ERROR("ESP-NOW", "Failed to send event logs control: %s", esp_err_to_name(result));
    return false;
}

bool send_battery_types_request() {
    if (!can_send_catalog_request()) {
        return false;
    }

    type_catalog_request_t req{};
    req.type = msg_request_battery_types;

    esp_err_t result = esp_now_send(ESPNow::transmitter_mac, reinterpret_cast<uint8_t*>(&req), sizeof(req));
    if (result != ESP_OK) {
        LOG_WARN("ESP-NOW", "Failed to request battery types: %s", esp_err_to_name(result));
        return false;
    }

    LOG_INFO("ESP-NOW", "Requested battery type catalog");
    return true;
}

bool send_inverter_types_request() {
    if (!can_send_catalog_request()) {
        return false;
    }

    type_catalog_request_t req{};
    req.type = msg_request_inverter_types;

    esp_err_t result = esp_now_send(ESPNow::transmitter_mac, reinterpret_cast<uint8_t*>(&req), sizeof(req));
    if (result != ESP_OK) {
        LOG_WARN("ESP-NOW", "Failed to request inverter types: %s", esp_err_to_name(result));
        return false;
    }

    LOG_INFO("ESP-NOW", "Requested inverter type catalog");
    return true;
}

bool send_inverter_interfaces_request() {
    if (!can_send_catalog_request()) {
        return false;
    }

    type_catalog_request_t req{};
    req.type = msg_request_inverter_interfaces;

    esp_err_t result = esp_now_send(ESPNow::transmitter_mac, reinterpret_cast<uint8_t*>(&req), sizeof(req));
    if (result != ESP_OK) {
        LOG_WARN("ESP-NOW", "Failed to request inverter interfaces: %s", esp_err_to_name(result));
        return false;
    }

    LOG_INFO("ESP-NOW", "Requested inverter interface catalog");
    return true;
}

bool send_type_catalog_versions_request() {
    if (!can_send_catalog_request()) {
        return false;
    }

    type_catalog_versions_request_t req{};
    req.type = msg_request_type_catalog_versions;

    esp_err_t result = esp_now_send(ESPNow::transmitter_mac, reinterpret_cast<uint8_t*>(&req), sizeof(req));
    if (result != ESP_OK) {
        LOG_WARN("ESP-NOW", "Failed to request catalog versions: %s", esp_err_to_name(result));
        return false;
    }

    LOG_INFO("ESP-NOW", "Requested type catalog versions");
    return true;
}