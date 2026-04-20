#include "espnow_send.h"
#include "../../include/common_lcd.h"
#include "rx_state_machine.h"
#include <esp32common/espnow/connection_manager.h>
#include <esp32common/espnow/tx_scheduler.h>
#include <esp_now.h>
#include <esp32common/espnow/common.h>
#include <esp32common/espnow/packet_utils.h>
#include <logging_utilities/logging_config.h>

static uint8_t last_debug_level_sent    = 6;  // Default to INFO
static uint8_t last_test_data_mode_sent = 0;  // Default to OFF

uint8_t get_last_debug_level()     { return last_debug_level_sent; }
uint8_t get_last_test_data_mode()  { return last_test_data_mode_sent; }

static bool has_transmitter_mac() {
    for (int i = 0; i < 6; i++) {
        if (ESPNow::peer_mac[i] != 0) return true;
    }
    return false;
}

static bool can_send_catalog_request() {
    const bool message_valid =
        (RxStateMachine::instance().message_state() == RxStateMachine::MessageState::VALID);
    const bool connected =
        (EspNowConnectionManager::instance().get_state() == EspNowConnectionState::CONNECTED);
    if (!(message_valid || connected)) {
        LOG_WARN("ESP-NOW", "Transmitter not ready - cannot send catalog request");
        return false;
    }
    if (!has_transmitter_mac()) {
        LOG_WARN("ESP-NOW", "Transmitter MAC not registered - cannot send catalog request");
        return false;
    }
    return true;
}

bool send_debug_level_control(uint8_t level) {
    if (level > 7) { LOG_ERROR("ESP-NOW", "Invalid debug level: %d", level); return false; }
    if (RxStateMachine::instance().message_state() != RxStateMachine::MessageState::VALID) {
        LOG_WARN("ESP-NOW", "Transmitter not connected - cannot send debug control");
        return false;
    }
    if (!has_transmitter_mac()) {
        LOG_WARN("ESP-NOW", "Transmitter MAC not registered - cannot send debug control");
        return false;
    }
    debug_control_t packet{};
    packet.type     = msg_debug_control;
    packet.level    = level;
    packet.flags    = 0;
    packet.checksum = 0;
    uint8_t* data = (uint8_t*)&packet;
    for (size_t i = 0; i < sizeof(packet) - 1; i++) packet.checksum ^= data[i];
    esp_err_t result = EspnowTxScheduler::send(ESPNow::peer_mac, &packet, sizeof(packet), "DEBUG_CONTROL");
    if (result == ESP_OK) { last_debug_level_sent = level; return true; }
    LOG_ERROR("ESP-NOW", "Failed to send debug control: %s", esp_err_to_name(result));
    return false;
}

bool send_component_apply_request(uint32_t request_id,
                                  uint8_t apply_mask,
                                  uint8_t battery_type,
                                  uint8_t inverter_type,
                                  uint8_t battery_interface,
                                  uint8_t inverter_interface) {
    if (apply_mask == 0) { LOG_ERROR("ESP-NOW", "apply_mask is 0"); return false; }
    if (RxStateMachine::instance().message_state() != RxStateMachine::MessageState::VALID) {
        LOG_WARN("ESP-NOW", "Transmitter not connected - cannot send component apply request");
        return false;
    }
    if (!has_transmitter_mac()) return false;
    component_apply_request_t packet{};
    packet.type              = msg_component_apply_request;
    packet.request_id        = request_id;
    packet.apply_mask        = apply_mask;
    packet.battery_type      = battery_type;
    packet.inverter_type     = inverter_type;
    packet.battery_interface = battery_interface;
    packet.inverter_interface= inverter_interface;
    packet.checksum          = 0;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&packet);
    for (size_t i = 0; i < sizeof(component_apply_request_t) - sizeof(packet.checksum); ++i)
        packet.checksum += bytes[i];
    esp_err_t result = EspnowTxScheduler::send(ESPNow::peer_mac, &packet, sizeof(packet), "COMPONENT_APPLY_REQ");
    if (result == ESP_OK) return true;
    LOG_ERROR("ESP-NOW", "Failed to send component apply request: %s", esp_err_to_name(result));
    return false;
}

bool send_test_data_mode_control(uint8_t mode) {
    if (mode > 2) { LOG_ERROR("ESP-NOW", "Invalid test data mode: %d", mode); return false; }

    // Always cache the operator-selected mode locally for UI behavior.
    last_test_data_mode_sent = mode;

    if (RxStateMachine::instance().message_state() != RxStateMachine::MessageState::VALID) {
        LOG_WARN("ESP-NOW", "Transmitter not connected - cannot send test data mode control");
        return false;
    }
    if (!has_transmitter_mac()) return false;
    debug_control_t packet{};
    packet.type     = msg_debug_control;
    packet.level    = mode;
    packet.flags    = 0x80;  // High bit = test data control
    packet.checksum = 0;
    uint8_t* data = (uint8_t*)&packet;
    for (size_t i = 0; i < sizeof(packet) - 1; i++) packet.checksum ^= data[i];
    esp_err_t result = EspnowTxScheduler::send(ESPNow::peer_mac, &packet, sizeof(packet), "TEST_DATA_CONTROL");
    if (result == ESP_OK) { return true; }
    LOG_ERROR("ESP-NOW", "Failed to send test data mode control: %s", esp_err_to_name(result));
    return false;
}

bool send_event_logs_control(bool subscribe) {
    if (RxStateMachine::instance().message_state() != RxStateMachine::MessageState::VALID) {
        LOG_WARN("ESP-NOW", "Transmitter not connected - cannot send event logs control");
        return false;
    }
    if (!has_transmitter_mac()) return false;
    event_logs_control_t packet{};
    packet.type   = msg_event_logs_control;
    packet.action = subscribe ? EVENT_LOGS_ACTION_SUBSCRIBE : EVENT_LOGS_ACTION_UNSUBSCRIBE;
    esp_err_t result = EspnowTxScheduler::send(ESPNow::peer_mac, &packet, sizeof(packet), "EVENT_LOGS_CONTROL");
    if (result == ESP_OK) return true;
    LOG_ERROR("ESP-NOW", "Failed to send event logs control: %s", esp_err_to_name(result));
    return false;
}

bool send_event_logs_clear_request() {
    if (RxStateMachine::instance().message_state() != RxStateMachine::MessageState::VALID) {
        LOG_WARN("ESP-NOW", "Transmitter not connected - cannot send event logs clear");
        return false;
    }
    if (!has_transmitter_mac()) return false;
    event_logs_control_t packet{};
    packet.type   = msg_event_logs_control;
    packet.action = EVENT_LOGS_ACTION_CLEAR;
    esp_err_t result = EspnowTxScheduler::send(ESPNow::peer_mac, &packet, sizeof(packet), "EVENT_LOGS_CLEAR");
    if (result == ESP_OK) return true;
    LOG_ERROR("ESP-NOW", "Failed to send event logs clear: %s", esp_err_to_name(result));
    return false;
}

bool send_battery_types_request() {
    if (!can_send_catalog_request()) return false;
    type_catalog_request_t req{};
    req.type = msg_request_battery_types;
    esp_err_t r = EspnowTxScheduler::send(ESPNow::peer_mac, &req, sizeof(req), "REQUEST_BATTERY_TYPES");
    return r == ESP_OK;
}

bool send_inverter_types_request() {
    if (!can_send_catalog_request()) return false;
    type_catalog_request_t req{};
    req.type = msg_request_inverter_types;
    esp_err_t r = EspnowTxScheduler::send(ESPNow::peer_mac, &req, sizeof(req), "REQUEST_INVERTER_TYPES");
    return r == ESP_OK;
}

bool send_inverter_interfaces_request() {
    if (!can_send_catalog_request()) return false;
    type_catalog_request_t req{};
    req.type = msg_request_inverter_interfaces;
    esp_err_t r = EspnowTxScheduler::send(ESPNow::peer_mac, &req, sizeof(req), "REQUEST_INVERTER_INTERFACES");
    return r == ESP_OK;
}

bool send_type_catalog_versions_request() {
    if (!can_send_catalog_request()) return false;
    type_catalog_versions_request_t req{};
    req.type = msg_request_type_catalog_versions;
    esp_err_t r = EspnowTxScheduler::send(ESPNow::peer_mac, &req, sizeof(req), "REQUEST_CATALOG_VERSIONS");
    return r == ESP_OK;
}

bool send_led_state_request() {
    if (!can_send_catalog_request()) return false;
    led_state_request_t packet{};
    packet.type = msg_led_state_request;
    esp_err_t r = EspnowTxScheduler::send(ESPNow::peer_mac, &packet, sizeof(packet), "LED_STATE_REQUEST");
    return r == ESP_OK;
}
