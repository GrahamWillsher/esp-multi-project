#include "message_handler.h"
#include "discovery_task.h"
#include "version_beacon_manager.h"
#include "heartbeat_manager.h"
#include "control_handlers.h"
#include "component_catalog_handlers.h"
#include "mqtt_config_handlers.h"
#include "network_config_handlers.h"
#include "request_data_handlers.h"
#include "tx_state_machine.h"
#include "tx_send_guard.h"
#include "../network/mqtt_manager.h"
#include "../network/ethernet_manager.h"
#include "../config/task_config.h"
#include "../config/logging_config.h"
#include "../config/network_config.h"
#include "../settings/settings_manager.h"
#include "../test_data/test_data_config.h"
#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <espnow_transmitter.h>
#include <espnow_peer_manager.h>
#include <esp32common/espnow/message_router.h>
#include <esp32common/espnow/standard_handlers.h>
#include <esp32common/espnow/packet_utils.h>
#include <esp32common/espnow/connection_manager.h>
#include <channel_manager.h>
#include <mqtt_manager.h>
#include <ethernet_config.h>

EspnowMessageHandler& EspnowMessageHandler::instance() {
    static EspnowMessageHandler instance;
    return instance;
}

EspnowMessageHandler::EspnowMessageHandler() {
    TxStateMachine::instance().init();
    setup_message_routes();
}

void EspnowMessageHandler::start_rx_task(QueueHandle_t queue) {
    // Create main RX task
    xTaskCreate(
        rx_task_impl,
        "espnow_rx",
        task_config::STACK_SIZE_ESPNOW_RX,
        (void*)queue,
        task_config::PRIORITY_CRITICAL,
        nullptr
    );
    LOG_DEBUG("MSG_HANDLER", "ESP-NOW RX task started");
    
    // Create network config processing queue and task
    network_config_queue_ = xQueueCreate(task_config::NETWORK_CONFIG_QUEUE_SIZE, sizeof(espnow_queue_msg_t));
    if (network_config_queue_ == nullptr) {
        LOG_ERROR("MSG_HANDLER", "Failed to create network config queue");
    } else {
        xTaskCreate(
            network_config_task_impl,
            "net_config",
            task_config::STACK_SIZE_NETWORK_CONFIG,
            nullptr,
            task_config::PRIORITY_NETWORK_CONFIG,
            &network_config_task_handle_
        );
        LOG_DEBUG("MSG_HANDLER", "Network config task started (priority=%d)", task_config::PRIORITY_NETWORK_CONFIG);
    }
}

void EspnowMessageHandler::init() {
    // Tx connection/device state transitions are owned by TransmitterConnectionHandler.
    // Keep init() for symmetry and future extension, but avoid registering a duplicate
    // callback here because it causes duplicate CONNECTED/IDLE transitions in TxStateMachine.
    LOG_DEBUG("MSG_HANDLER", "Tx state transitions owned by tx_connection_handler");
}

void EspnowMessageHandler::rx_task_impl(void* parameter) {
    QueueHandle_t queue = (QueueHandle_t)parameter;
    auto& handler = instance();
    auto& router = EspnowMessageRouter::instance();
    
    LOG_DEBUG("MSG_HANDLER", "Message RX task running");
    espnow_queue_msg_t msg;
    
    // Note: Connection timeout is handled by EspNowConnectionManager, not here.
    // This task only routes messages - no legacy timeout checking needed.
    
    while (true) {
        if (xQueueReceive(queue, &msg, portMAX_DELAY) == pdTRUE) {
            // Route message using common router
            if (!router.route_message(msg)) {
                // Message not handled by any route
                uint8_t msg_type = (msg.len > 0) ? msg.data[0] : 0;
                LOG_WARN("MSG_HANDLER", "Unknown message type: %u from %02X:%02X:%02X:%02X:%02X:%02X",
                             msg_type, msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
            }
        }
    }
}

void EspnowMessageHandler::handle_request_data(const espnow_queue_msg_t& msg) {
    TxRequestDataHandlers::handle_request_data(msg);
}

void EspnowMessageHandler::handle_abort_data(const espnow_queue_msg_t& msg) {
    TxRequestDataHandlers::handle_abort_data(msg);
}

void EspnowMessageHandler::handle_reboot(const espnow_queue_msg_t& msg) {
    TxControlHandlers::handle_reboot(msg);
}

void EspnowMessageHandler::handle_ota_start(const espnow_queue_msg_t& msg) {
    TxControlHandlers::handle_ota_start(msg);
}

// ═══════════════════════════════════════════════════════════════════════
// Helper function: Send IP configuration to receiver
// Called automatically when Ethernet connects
// ═══════════════════════════════════════════════════════════════════════

void send_ip_to_receiver() {
    if (!EthernetManager::instance().is_connected()) return;
    
    // Check if receiver is connected
    if (!EspNowConnectionManager::instance().is_connected()) {
        LOG_DEBUG("ETH", "Receiver not connected yet, will send IP later");
        return;
    }
    
    // Get receiver MAC from connection manager
    const uint8_t* peer_mac = EspNowConnectionManager::instance().get_peer_mac();
    
    // Check if receiver peer exists
    if (!esp_now_is_peer_exist(peer_mac)) {
        LOG_DEBUG("ETH", "Receiver peer not registered, skipping IP send");
        return;
    }
    
    IPAddress local_ip = EthernetManager::instance().get_local_ip();
    IPAddress gateway = EthernetManager::instance().get_gateway_ip();
    IPAddress subnet = EthernetManager::instance().get_subnet_mask();
    
    // Create proper espnow_packet_t structure
    espnow_packet_t packet;
    packet.type = msg_packet;
    packet.subtype = subtype_network_config;
    packet.seq = esp_random();
    packet.frag_index = 0;
    packet.frag_total = 1;
    packet.payload_len = 12;  // IP[4] + Gateway[4] + Subnet[4]
    
    // Pack IP address bytes into payload
    for (int i = 0; i < 4; i++) {
        packet.payload[i] = local_ip[i];       // IP at offset 0
        packet.payload[4 + i] = gateway[i];    // Gateway at offset 4
        packet.payload[8 + i] = subnet[i];     // Subnet at offset 8
    }
    
    // Calculate checksum
    packet.checksum = EspnowPacketUtils::calculate_checksum(packet.payload, packet.payload_len);
    
    // Send IP data via ESP-NOW
    esp_err_t result = TxSendGuard::send_to_receiver_guarded(
        peer_mac,
        (const uint8_t*)&packet,
        sizeof(packet),
        "eth_ip_push"
    );
    
    if (result == ESP_OK) {
        LOG_INFO("ETH", "Sent IP configuration to receiver: %s", local_ip.toString().c_str());
    } else {
        LOG_WARN("ETH", "Failed to send IP to receiver: %s", esp_err_to_name(result));
    }
}

void EspnowMessageHandler::handle_debug_control(const espnow_queue_msg_t& msg) {
    TxControlHandlers::handle_debug_control(msg, receiver_mac_);
}

void EspnowMessageHandler::handle_heartbeat_ack(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(heartbeat_ack_t)) {
        LOG_WARN("HEARTBEAT", "Invalid heartbeat_ack packet size: %d", msg.len);
        return;
    }
    
    const heartbeat_ack_t* ack = reinterpret_cast<const heartbeat_ack_t*>(msg.data);
    
    // Forward to heartbeat manager
    HeartbeatManager::instance().on_heartbeat_ack(ack);
}

void EspnowMessageHandler::handle_component_config(const espnow_queue_msg_t& msg) {
    TxComponentCatalogHandlers::handle_component_config(msg);
}

void EspnowMessageHandler::handle_component_interface(const espnow_queue_msg_t& msg) {
    TxComponentCatalogHandlers::handle_component_interface(msg);
}

void EspnowMessageHandler::handle_request_battery_types(const espnow_queue_msg_t& msg) {
    TxComponentCatalogHandlers::handle_request_battery_types(msg, receiver_mac_);
}

void EspnowMessageHandler::handle_request_inverter_types(const espnow_queue_msg_t& msg) {
    TxComponentCatalogHandlers::handle_request_inverter_types(msg, receiver_mac_);
}

void EspnowMessageHandler::handle_request_inverter_interfaces(const espnow_queue_msg_t& msg) {
    TxComponentCatalogHandlers::handle_request_inverter_interfaces(msg, receiver_mac_);
}

void EspnowMessageHandler::handle_request_type_catalog_versions(const espnow_queue_msg_t& msg) {
    TxComponentCatalogHandlers::handle_request_type_catalog_versions(msg, receiver_mac_);
}

void EspnowMessageHandler::save_debug_level(uint8_t level) {
    TxControlHandlers::save_debug_level(level);
}

uint8_t EspnowMessageHandler::load_debug_level() {
    return TxControlHandlers::load_debug_level();
}

// =============================================================================
// Network Configuration Handler Implementation
// =============================================================================

// Static member initialization
TaskHandle_t EspnowMessageHandler::network_config_task_handle_ = nullptr;
QueueHandle_t EspnowMessageHandler::network_config_queue_ = nullptr;

void EspnowMessageHandler::handle_network_config_request(const espnow_queue_msg_t& msg) {
    TxNetworkConfigHandlers::handle_network_config_request(msg, receiver_mac_);
}

void EspnowMessageHandler::handle_network_config_update(const espnow_queue_msg_t& msg) {
    TxNetworkConfigHandlers::handle_network_config_update(msg, receiver_mac_, network_config_queue_);
}

void EspnowMessageHandler::send_network_config_ack(bool success, const char* message) {
    TxNetworkConfigHandlers::send_network_config_ack(receiver_mac_, success, message);
}

void EspnowMessageHandler::network_config_task_impl(void* parameter) {
    LOG_INFO("NET_CFG", "Background processing task started");
    
    espnow_queue_msg_t msg;
    
    while (true) {
        if (xQueueReceive(network_config_queue_, &msg, portMAX_DELAY) == pdTRUE) {
            TxNetworkConfigHandlers::process_network_config_update(msg);
        }
    }
}

// =============================================================================
// MQTT Configuration Message Handlers
// =============================================================================

void EspnowMessageHandler::handle_mqtt_config_request(const espnow_queue_msg_t& msg) {
    TxMqttConfigHandlers::handle_mqtt_config_request(msg, receiver_mac_);
}

void EspnowMessageHandler::handle_mqtt_config_update(const espnow_queue_msg_t& msg) {
    TxMqttConfigHandlers::handle_mqtt_config_update(msg, receiver_mac_);
}

void EspnowMessageHandler::send_mqtt_config_ack(bool success, const char* message) {
    TxMqttConfigHandlers::send_mqtt_config_ack(receiver_mac_, success, message);
}

