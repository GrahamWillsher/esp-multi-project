#include "message_handler.h"
#include "discovery_task.h"
#include "version_beacon_manager.h"
#include "heartbeat_manager.h"
#include "../network/ethernet_manager.h"
#include "../network/mqtt_manager.h"
#include "../system_settings.h"
#include "../datalayer/static_data.h"
#include "../config/task_config.h"
#include "../config/logging_config.h"
#include "../config/network_config.h"
#include "../settings/settings_manager.h"
#include "../test_data/test_data_config.h"
#if CONFIG_CAN_ENABLED
#include "../battery/battery_manager.h"
#include "../battery_emulator/battery/Battery.h"
#include "../battery_emulator/inverter/InverterProtocol.h"
#endif
#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <espnow_transmitter.h>
#include <espnow_peer_manager.h>
#include <espnow_message_router.h>
#include <espnow_standard_handlers.h>
#include <espnow_packet_utils.h>
#include <connection_manager.h>
#include <mqtt_logger.h>
#include <mqtt_manager.h>
#include <Preferences.h>
#include <ethernet_config.h>
#include <firmware_version.h>
#include <firmware_metadata.h>

// Note: STRINGIFY macro is defined in firmware_version.h

EspnowMessageHandler& EspnowMessageHandler::instance() {
    static EspnowMessageHandler instance;
    return instance;
}

EspnowMessageHandler::EspnowMessageHandler() {
    setup_message_routes();
}

void EspnowMessageHandler::setup_message_routes() {
    auto& router = EspnowMessageRouter::instance();
    
    // Setup PROBE handler configuration
    probe_config_.send_ack_response = true;
    probe_config_.peer_mac_storage = receiver_mac_;
    probe_config_.on_connection = [](const uint8_t* mac, bool connected) {
        LOG_INFO("MSG_HANDLER", "Receiver connected via PROBE");
    };
    
    // Setup ACK handler configuration
    ack_config_.peer_mac_storage = receiver_mac_;
    ack_config_.expected_seq = &g_ack_seq;
    ack_config_.lock_channel = &g_lock_channel;
    ack_config_.ack_received_flag = &g_ack_received;  // For channel hopping discovery
    ack_config_.set_wifi_channel = false;              // Don't change channel in handler - let discovery complete first
    ack_config_.on_connection = [](const uint8_t* mac, bool connected) {
        LOG_INFO("MSG_HANDLER", "Receiver connected via ACK");
        // Note: Version announce already sent in PROBE handler - no need to duplicate
    };
    
    // Register standard message handlers
    router.register_route(msg_probe, 
        [](const espnow_queue_msg_t* msg, void* ctx) {
            auto* self = static_cast<EspnowMessageHandler*>(ctx);
            EspnowStandardHandlers::handle_probe(msg, &self->probe_config_);
        }, 
        0xFF, this);
    
    router.register_route(msg_ack,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            auto* self = static_cast<EspnowMessageHandler*>(ctx);
            EspnowStandardHandlers::handle_ack(msg, &self->ack_config_);
        },
        0xFF, this);
    
    // Register custom message handlers
    router.register_route(msg_request_data,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_request_data(*msg);
        },
        0xFF, this);
    
    router.register_route(msg_abort_data,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_abort_data(*msg);
        },
        0xFF, this);
    
    router.register_route(msg_reboot,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_reboot(*msg);
        },
        0xFF, this);
    
    router.register_route(msg_ota_start,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_ota_start(*msg);
        },
        0xFF, this);
    
    // Register debug control handler
    router.register_route(msg_debug_control,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_debug_control(*msg);
        },
        0xFF, this);
    
    // Register heartbeat ACK handler
    router.register_route(msg_heartbeat_ack,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_heartbeat_ack(*msg);
        },
        0xFF, this);
    
    // Note: Transmitter should NOT receive heartbeats from receiver
    // Heartbeat protocol: Transmitter SENDS, Receiver RECEIVES and ACKs
    // Receiver does NOT send heartbeats to transmitter
    // If heartbeats are received, they're either misconfigured or a routing error
    // We don't register a handler for msg_heartbeat on the transmitter side
    
    // Phase 2: Settings update handler
    router.register_route(msg_battery_settings_update,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            SettingsManager::instance().handle_settings_update(*msg);
        },
        0xFF, this);

    // Component configuration update handler (receiver → transmitter)
    router.register_route(msg_component_config,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_component_config(*msg);
        },
        0xFF, this);

    // Component interface update handler (receiver → transmitter)
    router.register_route(msg_component_interface,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_component_interface(*msg);
        },
        0xFF, this);

    // Event logs subscription control (receiver → transmitter)
    router.register_route(msg_event_logs_control,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            if (msg->len >= (int)sizeof(event_logs_control_t)) {
                const event_logs_control_t* control = reinterpret_cast<const event_logs_control_t*>(msg->data);
                if (control->action == 1) {
                    MqttManager::instance().increment_event_log_subscribers();
                } else {
                    MqttManager::instance().decrement_event_log_subscribers();
                }
            }
        },
        0xFF, this);
    
    // Network configuration request handler
    router.register_route(msg_network_config_request,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_network_config_request(*msg);
        },
        0xFF, this);
    
    // Network configuration update handler
    router.register_route(msg_network_config_update,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_network_config_update(*msg);
        },
        0xFF, this);
    
    // MQTT configuration request handler
    router.register_route(msg_mqtt_config_request,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_mqtt_config_request(*msg);
        },
        0xFF, this);
    
    // MQTT configuration update handler
    router.register_route(msg_mqtt_config_update,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_mqtt_config_update(*msg);
        },
        0xFF, this);
    
    // =========================================================================
    // PHASE 4: Version-Based Cache Synchronization
    // =========================================================================
    
    // Config section request handler (receiver → transmitter when version mismatch)
    router.register_route(msg_config_section_request,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            if (msg->len >= (int)sizeof(config_section_request_t)) {
                const config_section_request_t* request = reinterpret_cast<const config_section_request_t*>(msg->data);
                VersionBeaconManager::instance().handle_config_request(request, msg->mac);
            }
        },
        0xFF, this);
    
    // Register version exchange message handlers
    router.register_route(msg_version_announce,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            if (msg->len >= (int)sizeof(version_announce_t)) {
                const version_announce_t* announce = reinterpret_cast<const version_announce_t*>(msg->data);
                uint8_t rx_major = (announce->firmware_version / 10000);
                uint8_t rx_minor = (announce->firmware_version / 100) % 100;
                uint8_t rx_patch = announce->firmware_version % 100;
                
                LOG_INFO("VERSION", "Receiver version: %d.%d.%d", rx_major, rx_minor, rx_patch);
                
                if (!isVersionCompatible(announce->firmware_version)) {
                    LOG_WARN("VERSION", "Version incompatible: transmitter %d.%d.%d, receiver %d.%d.%d",
                             FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH,
                             rx_major, rx_minor, rx_patch);
                }
            }
        },
        0xFF, this);
    
    router.register_route(msg_version_request,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            if (msg->len >= (int)sizeof(version_request_t)) {
                // Respond with our version information
                version_response_t response;
                response.type = msg_version_response;
                response.firmware_version = FW_VERSION_NUMBER;
                response.protocol_version = PROTOCOL_VERSION;
                strncpy(response.device_type, DEVICE_NAME, sizeof(response.device_type) - 1);
                response.device_type[sizeof(response.device_type) - 1] = '\0';
                strncpy(response.build_date, __DATE__, sizeof(response.build_date) - 1);
                response.build_date[sizeof(response.build_date) - 1] = '\0';
                strncpy(response.build_time, __TIME__, sizeof(response.build_time) - 1);
                response.build_time[sizeof(response.build_time) - 1] = '\0';
                
                esp_err_t result = esp_now_send(msg->mac, (const uint8_t*)&response, sizeof(response));
                if (result == ESP_OK) {
                    LOG_DEBUG("VERSION", "Sent VERSION_RESPONSE to receiver");
                } else {
                    LOG_ERROR("VERSION", "Failed to send VERSION_RESPONSE: %s", esp_err_to_name(result));
                }
            }
        },
        0xFF, this);
    
    router.register_route(msg_version_response,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            if (msg->len >= (int)sizeof(version_response_t)) {
                const version_response_t* response = reinterpret_cast<const version_response_t*>(msg->data);
                LOG_DEBUG("VERSION", "Received VERSION_RESPONSE: %s %d.%d.%d",
                         response->device_type,
                         response->firmware_version / 10000,
                         (response->firmware_version / 100) % 100,
                         response->firmware_version % 100);
            }
        },
        0xFF, this);
    
    LOG_DEBUG("MSG_HANDLER", "Registered %d message routes", router.route_count());
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
    if (msg.len < (int)sizeof(request_data_t)) {
        LOG_WARN("DATA_REQUEST", "Packet too short: %d bytes", msg.len);
        return;
    }
    
    const request_data_t* req = reinterpret_cast<const request_data_t*>(msg.data);
    LOG_INFO("DATA_REQUEST", "REQUEST_DATA received (subtype=%d) from %02X:%02X:%02X:%02X:%02X:%02X",
                 req->subtype, msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
    
    // Check if receiver connection is in CONNECTED state before responding
    auto& conn_mgr = EspNowConnectionManager::instance();
    auto state = conn_mgr.get_state();
    
    if (state != EspNowConnectionState::CONNECTED) {
        LOG_WARN("DATA_REQUEST", "Cannot respond to data request - receiver state is %u (need CONNECTED)",
                 (uint8_t)state);
        return;
    }
    
    // Handle based on subtype
    switch (req->subtype) {
        case subtype_power_profile:
            transmission_active_ = true;
            LOG_INFO("DATA_REQUEST", ">>> Power profile transmission ACTIVATED <<<");
            break;
        
        case subtype_network_config: {
            // Send ONLY IP configuration (Phase 3 granular subtype)
            if (EthernetManager::instance().is_connected()) {
                IPAddress local_ip = EthernetManager::instance().get_local_ip();
                IPAddress gateway = EthernetManager::instance().get_gateway_ip();
                IPAddress subnet = EthernetManager::instance().get_subnet_mask();
                
                espnow_packet_t packet;
                packet.type = msg_packet;
                packet.subtype = subtype_network_config;
                packet.seq = esp_random();
                packet.frag_index = 0;
                packet.frag_total = 1;
                packet.payload_len = 12;  // IP[4] + Gateway[4] + Subnet[4]
                
                for (int i = 0; i < 4; i++) {
                    packet.payload[i] = local_ip[i];
                    packet.payload[4 + i] = gateway[i];
                    packet.payload[8 + i] = subnet[i];
                }
                
                packet.checksum = EspnowPacketUtils::calculate_checksum(packet.payload, packet.payload_len);
                esp_err_t result = esp_now_send(msg.mac, (const uint8_t*)&packet, sizeof(packet));
                
                if (result == ESP_OK) {
                    LOG_DEBUG("DATA_REQUEST", "Sent network config: %s, GW: %s, Subnet: %s",
                             local_ip.toString().c_str(), gateway.toString().c_str(), subnet.toString().c_str());
                } else {
                    LOG_WARN("DATA_REQUEST", "Failed to send network config: %s", esp_err_to_name(result));
                }
            } else {
                LOG_WARN("DATA_REQUEST", "Ethernet not connected, cannot send network config");
            }
            break;
        }
        
        case subtype_battery_config: {
            // Phase 3: Send ALL battery settings (including currents and SOC limits)
            LOG_DEBUG("DATA_REQUEST", ">>> Battery config request - sending FULL battery settings");
            
            battery_settings_full_msg_t settings_msg;
            settings_msg.type = msg_battery_info;
            settings_msg.capacity_wh = SettingsManager::instance().get_battery_capacity_wh();
            settings_msg.max_voltage_mv = SettingsManager::instance().get_battery_max_voltage_mv();
            settings_msg.min_voltage_mv = SettingsManager::instance().get_battery_min_voltage_mv();
            settings_msg.max_charge_current_a = SettingsManager::instance().get_battery_max_charge_current_a();
            settings_msg.max_discharge_current_a = SettingsManager::instance().get_battery_max_discharge_current_a();
            settings_msg.soc_high_limit = SettingsManager::instance().get_battery_soc_high_limit();
            settings_msg.soc_low_limit = SettingsManager::instance().get_battery_soc_low_limit();
            settings_msg.cell_count = SettingsManager::instance().get_battery_cell_count();
            settings_msg.chemistry = SettingsManager::instance().get_battery_chemistry();
            
            uint16_t sum = 0;
            const uint8_t* bytes = (const uint8_t*)&settings_msg;
            for (size_t i = 0; i < sizeof(settings_msg) - 2; i++) {
                sum += bytes[i];
            }
            settings_msg.checksum = sum;
            
            esp_err_t result = esp_now_send(msg.mac, (const uint8_t*)&settings_msg, sizeof(settings_msg));
            if (result == ESP_OK) {
                const char* chem[] = {"NCA", "NMC", "LFP", "LTO"};
                LOG_INFO("DATA_REQUEST", "Sent FULL battery settings: %dWh, %dS, %s, %.1fA/%.1fA, SOC:%d-%d%%", 
                        settings_msg.capacity_wh, settings_msg.cell_count, chem[settings_msg.chemistry],
                        settings_msg.max_charge_current_a, settings_msg.max_discharge_current_a,
                        settings_msg.soc_low_limit, settings_msg.soc_high_limit);
            } else {
                LOG_WARN("DATA_REQUEST", "Failed to send battery settings: %s", esp_err_to_name(result));
            }
            break;
        }
        
        case subtype_settings: {
            // DEPRECATED: Send BOTH IP and battery (backward compatibility)
            LOG_DEBUG("DATA_REQUEST", ">>> Settings request (legacy) - sending IP + battery data");
            
            // Send IP data if Ethernet is connected
            if (EthernetManager::instance().is_connected()) {
                IPAddress local_ip = EthernetManager::instance().get_local_ip();
                IPAddress gateway = EthernetManager::instance().get_gateway_ip();
                IPAddress subnet = EthernetManager::instance().get_subnet_mask();
                
                espnow_packet_t packet;
                packet.type = msg_packet;
                packet.subtype = subtype_settings;
                packet.seq = esp_random();
                packet.frag_index = 0;
                packet.frag_total = 1;
                packet.payload_len = 12;
                
                for (int i = 0; i < 4; i++) {
                    packet.payload[i] = local_ip[i];
                    packet.payload[4 + i] = gateway[i];
                    packet.payload[8 + i] = subnet[i];
                }
                
                packet.checksum = EspnowPacketUtils::calculate_checksum(packet.payload, packet.payload_len);
                esp_err_t result = esp_now_send(msg.mac, (const uint8_t*)&packet, sizeof(packet));
                
                if (result == ESP_OK) {
                    LOG_DEBUG("DATA_REQUEST", "Sent IP data (legacy subtype_settings)");
                } else {
                    LOG_WARN("DATA_REQUEST", "Failed to send IP data: %s", esp_err_to_name(result));
                }
            } else {
                // Send packet with all zeros to indicate no IP data available
                espnow_packet_t packet;
                packet.type = msg_packet;
                packet.subtype = subtype_settings;
                packet.seq = esp_random();
                packet.frag_index = 0;
                packet.frag_total = 1;
                packet.payload_len = 12;
                memset(packet.payload, 0, 12);  // All zeros = no IP yet
                packet.checksum = EspnowPacketUtils::calculate_checksum(packet.payload, packet.payload_len);
                
                esp_err_t result = esp_now_send(msg.mac, (const uint8_t*)&packet, sizeof(packet));
                if (result == ESP_OK) {
                    LOG_INFO("DATA_REQUEST", "Sent empty IP data (Ethernet not connected yet)");
                } else {
                    LOG_WARN("DATA_REQUEST", "Failed to send empty IP data: %s", esp_err_to_name(result));
                }
            }
            
            // V2: Always send battery_settings_full_msg_t (not legacy battery_info_msg_t)
            battery_settings_full_msg_t settings_msg;
            settings_msg.type = msg_battery_info;
            settings_msg.capacity_wh = SettingsManager::instance().get_battery_capacity_wh();
            settings_msg.max_voltage_mv = SettingsManager::instance().get_battery_max_voltage_mv();
            settings_msg.min_voltage_mv = SettingsManager::instance().get_battery_min_voltage_mv();
            settings_msg.max_charge_current_a = SettingsManager::instance().get_battery_max_charge_current_a();
            settings_msg.max_discharge_current_a = SettingsManager::instance().get_battery_max_discharge_current_a();
            settings_msg.soc_high_limit = SettingsManager::instance().get_battery_soc_high_limit();
            settings_msg.soc_low_limit = SettingsManager::instance().get_battery_soc_low_limit();
            settings_msg.cell_count = SettingsManager::instance().get_battery_cell_count();
            settings_msg.chemistry = SettingsManager::instance().get_battery_chemistry();
            
            uint16_t sum = 0;
            const uint8_t* bytes = (const uint8_t*)&settings_msg;
            for (size_t i = 0; i < sizeof(settings_msg) - 2; i++) {
                sum += bytes[i];
            }
            settings_msg.checksum = sum;
            
            esp_err_t result = esp_now_send(msg.mac, (const uint8_t*)&settings_msg, sizeof(settings_msg));
            if (result == ESP_OK) {
                const char* chem[] = {"NCA", "NMC", "LFP", "LTO"};
                LOG_INFO("DATA_REQUEST", "Sent battery settings: %dWh, %dS, %s, %.1f/%.1fA, SOC:%d-%d%%", 
                        settings_msg.capacity_wh, settings_msg.cell_count, chem[settings_msg.chemistry],
                        settings_msg.max_charge_current_a, settings_msg.max_discharge_current_a,
                        settings_msg.soc_low_limit, settings_msg.soc_high_limit);
            } else {
                LOG_WARN("DATA_REQUEST", "Failed to send battery settings: %s", esp_err_to_name(result));
            }
            break;
        }
        
        case subtype_charger_config:
        case subtype_inverter_config:
        case subtype_system_config:
        case subtype_events:
        case subtype_logs:
        case subtype_cell_info:
            LOG_DEBUG("DATA_REQUEST", "Subtype %d not implemented yet", req->subtype);
            break;
            
        default:
            LOG_WARN("DATA_REQUEST", "Unknown subtype: %d", req->subtype);
            break;
    }
}

void EspnowMessageHandler::handle_abort_data(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(abort_data_t)) return;
    
    const abort_data_t* abort = reinterpret_cast<const abort_data_t*>(msg.data);
    LOG_DEBUG("DATA_ABORT", "ABORT_DATA (subtype=%d) from %02X:%02X:%02X:%02X:%02X:%02X",
                 abort->subtype, msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
    
    // Handle based on subtype
    switch (abort->subtype) {
        case subtype_power_profile:
            transmission_active_ = false;
            LOG_INFO("DATA_ABORT", ">>> Power profile transmission STOPPED");
            break;
        
        case subtype_settings:
        case subtype_events:
        case subtype_logs:
        case subtype_cell_info:
            LOG_DEBUG("DATA_ABORT", "Subtype %d not implemented yet", abort->subtype);
            break;
            
        default:
            LOG_WARN("DATA_ABORT", "Unknown subtype: %d", abort->subtype);
            break;
    }
}

void EspnowMessageHandler::handle_reboot(const espnow_queue_msg_t& msg) {
    LOG_INFO("CMD", "REBOOT command from %02X:%02X:%02X:%02X:%02X:%02X",
                 msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
    LOG_INFO("CMD", ">>> Rebooting in 1 second...");
    Serial.flush();
    
    // Disconnect MQTT gracefully to prevent socket errors on reboot
    MqttManager::instance().disconnect();
    
    delay(1000);
    ESP.restart();
}

void EspnowMessageHandler::handle_ota_start(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(ota_start_t)) return;
    
    const ota_start_t* ota = reinterpret_cast<const ota_start_t*>(msg.data);
    LOG_INFO("CMD", "OTA_START command (size=%u bytes) from %02X:%02X:%02X:%02X:%02X:%02X",
                 ota->size, msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
    LOG_INFO("CMD", ">>> OTA mode ready - waiting for HTTP POST...");
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
    packet.subtype = subtype_settings;
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
    esp_err_t result = esp_now_send(peer_mac, (const uint8_t*)&packet, sizeof(packet));
    
    if (result == ESP_OK) {
        LOG_INFO("ETH", "Sent IP configuration to receiver: %s", local_ip.toString().c_str());
    } else {
        LOG_WARN("ETH", "Failed to send IP to receiver: %s", esp_err_to_name(result));
    }
}

void EspnowMessageHandler::handle_debug_control(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(debug_control_t)) {
        LOG_WARN("DEBUG_CTRL", "Invalid debug_control packet size: %d", msg.len);
        return;
    }
    
    const debug_control_t* pkt = reinterpret_cast<const debug_control_t*>(msg.data);
    
    // Store receiver MAC for ACK
    memcpy(receiver_mac_, msg.mac, 6);
    
    // Check if this is test data mode control (flags & 0x80)
    if (pkt->flags & 0x80) {
        handle_test_data_mode_control(pkt);
        return;
    }
    
    // Otherwise, handle as debug level control
    LOG_INFO("DEBUG_CTRL", "Received debug level change request: %u", pkt->level);
    
    // Validate level
    if (pkt->level > MQTT_LOG_DEBUG) {
        LOG_WARN("DEBUG_CTRL", "Invalid debug level: %u", pkt->level);
        send_debug_ack(pkt->level, MQTT_LOG_DEBUG, 1);
        return;
    }
    
    // Store previous level
    MqttLogLevel previous = MqttLogger::instance().get_level();
    
    // Apply new level
    MqttLogger::instance().set_level((MqttLogLevel)pkt->level);
    
    // Save to preferences for persistence
    save_debug_level(pkt->level);
    
    LOG_INFO("DEBUG_CTRL", "Debug level changed: %s → %s", 
             MqttLogger::instance().level_to_string(previous),
             MqttLogger::instance().level_to_string((MqttLogLevel)pkt->level));
    
    // Send acknowledgment
    send_debug_ack(pkt->level, (uint8_t)previous, 0);
}

void EspnowMessageHandler::handle_test_data_mode_control(const debug_control_t* pkt) {
    const char* mode_names[] = {"OFF", "SOC_POWER_ONLY", "FULL_BATTERY_DATA"};
    
    LOG_INFO("TEST_DATA_CTRL", "Received test data mode change request: %u (%s)", 
             pkt->level, pkt->level < 3 ? mode_names[pkt->level] : "INVALID");
    
    // Validate mode
    if (pkt->level > 2) {
        LOG_WARN("TEST_DATA_CTRL", "Invalid test data mode: %u (must be 0-2)", pkt->level);
        return;
    }
    
    // Get current configuration
    TestDataConfig::Config config = TestDataConfig::get_config();
    TestDataConfig::Mode previous_mode = config.mode;
    
    // Convert mode number to enum
    TestDataConfig::Mode new_mode;
    switch (pkt->level) {
        case 0: new_mode = TestDataConfig::Mode::OFF; break;
        case 1: new_mode = TestDataConfig::Mode::SOC_POWER_ONLY; break;
        case 2: new_mode = TestDataConfig::Mode::FULL_BATTERY_DATA; break;
        default: return;  // Should never happen after validation
    }
    
    // Update configuration
    config.mode = new_mode;
    TestDataConfig::set_config(config);
    TestDataConfig::apply_config();
    
    LOG_INFO("TEST_DATA_CTRL", "Test data mode changed: %s → %s", 
             TestDataConfig::mode_to_string(previous_mode),
             TestDataConfig::mode_to_string(new_mode));
    
    // TODO: Send acknowledgment back to receiver if needed
}

void EspnowMessageHandler::send_debug_ack(uint8_t applied, uint8_t previous, uint8_t status) {
    debug_ack_t ack = {
        .type = msg_debug_ack,
        .applied = applied,
        .previous = previous,
        .status = status
    };
    
    // Send to receiver (stored peer address)
    esp_err_t result = esp_now_send(receiver_mac_, (uint8_t*)&ack, sizeof(ack));
    
    if (result == ESP_OK) {
        LOG_DEBUG("DEBUG_CTRL", "Debug ACK sent (applied=%u, status=%u)", applied, status);
    } else {
        LOG_WARN("DEBUG_CTRL", "Failed to send debug ACK: %s", esp_err_to_name(result));
    }
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
    if (msg.len < (int)sizeof(component_config_msg_t)) {
        LOG_WARN("COMP_CFG", "Invalid component config packet size: %d", msg.len);
        return;
    }

    const component_config_msg_t* config = reinterpret_cast<const component_config_msg_t*>(msg.data);

    uint16_t calculated = 0;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(config);
    for (size_t i = 0; i < sizeof(component_config_msg_t) - sizeof(config->checksum); i++) {
        calculated += data[i];
    }

    if (calculated != config->checksum) {
        LOG_WARN("COMP_CFG", "Checksum mismatch: calc=%u, recv=%u", calculated, config->checksum);
        return;
    }

    SystemSettings& settings = SystemSettings::instance();
    bool battery_updated = false;
    bool inverter_updated = false;

    if (config->battery_type <= 46) {
        if (settings.get_battery_profile_type() != config->battery_type) {
            settings.set_battery_profile_type(config->battery_type);
            battery_updated = true;
        }

#if CONFIG_CAN_ENABLED
        user_selected_battery_type = static_cast<BatteryType>(config->battery_type);
        if (!BatteryManager::instance().is_primary_battery_initialized()) {
            BatteryManager::instance().init_primary_battery(static_cast<BatteryType>(config->battery_type));
        } else {
            LOG_WARN("COMP_CFG", "Battery already initialized - change will apply on reboot");
        }
#endif
    } else {
        LOG_WARN("COMP_CFG", "Invalid battery type: %u", config->battery_type);
    }

    if (config->inverter_type <= 21) {
        if (settings.get_inverter_type() != config->inverter_type) {
            settings.set_inverter_type(config->inverter_type);
            inverter_updated = true;
        }

#if CONFIG_CAN_ENABLED
        user_selected_inverter_protocol = static_cast<InverterProtocolType>(config->inverter_type);
        if (!BatteryManager::instance().is_inverter_initialized()) {
            BatteryManager::instance().init_inverter(static_cast<InverterProtocolType>(config->inverter_type));
        } else {
            LOG_WARN("COMP_CFG", "Inverter already initialized - change will apply on reboot");
        }
#endif
    } else {
        LOG_WARN("COMP_CFG", "Invalid inverter type: %u", config->inverter_type);
    }

    if (battery_updated) {
        StaticData::update_battery_specs(config->battery_type);
    }

    if (inverter_updated) {
        StaticData::update_inverter_specs(config->inverter_type);
    }

    if (battery_updated || inverter_updated) {
        if (MqttManager::instance().is_connected()) {
            if (battery_updated) {
                MqttManager::instance().publish_battery_specs();
            }
            if (inverter_updated) {
                MqttManager::instance().publish_inverter_specs();
            }
            MqttManager::instance().publish_static_specs();
        }
    }

    LOG_INFO("COMP_CFG", "Applied component selection: battery=%u inverter=%u",
             config->battery_type, config->inverter_type);

    if (battery_updated || inverter_updated) {
        LOG_WARN("COMP_CFG", ">>> Rebooting to apply component selection...");
        Serial.flush();
        
        // Disconnect MQTT gracefully to prevent socket errors on reboot
        MqttManager::instance().disconnect();
        
        delay(1000);
        ESP.restart();
    }
}

void EspnowMessageHandler::handle_component_interface(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(component_interface_msg_t)) {
        LOG_WARN("COMP_IF", "Invalid component interface packet size: %d", msg.len);
        return;
    }

    const component_interface_msg_t* config = reinterpret_cast<const component_interface_msg_t*>(msg.data);

    uint16_t calculated = 0;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(config);
    for (size_t i = 0; i < sizeof(component_interface_msg_t) - sizeof(config->checksum); i++) {
        calculated += data[i];
    }

    if (calculated != config->checksum) {
        LOG_WARN("COMP_IF", "Checksum mismatch: calc=%u, recv=%u", calculated, config->checksum);
        return;
    }

    if (config->battery_interface > 5) {
        LOG_WARN("COMP_IF", "Invalid battery interface: %u", config->battery_interface);
        return;
    }

    if (config->inverter_interface > 5) {
        LOG_WARN("COMP_IF", "Invalid inverter interface: %u", config->inverter_interface);
        return;
    }

    Preferences prefs;
    if (!prefs.begin("batterySettings", false)) {
        LOG_WARN("COMP_IF", "Failed to open NVS for interface save");
        return;
    }

    prefs.putUInt("BATTCOMM", config->battery_interface);
    prefs.putUInt("INVCOMM", config->inverter_interface);
    prefs.end();

    LOG_INFO("COMP_IF", "Applied component interface selection: battery_if=%u inverter_if=%u",
             config->battery_interface, config->inverter_interface);

    LOG_WARN("COMP_IF", ">>> Rebooting to apply component interface selection...");
    Serial.flush();

    // Disconnect MQTT gracefully to prevent socket errors on reboot
    MqttManager::instance().disconnect();

    delay(1000);
    ESP.restart();
}

void EspnowMessageHandler::save_debug_level(uint8_t level) {
    Preferences prefs;
    if (prefs.begin("debug", false)) {
        prefs.putUChar("log_level", level);
        prefs.end();
        LOG_DEBUG("DEBUG_CTRL", "Debug level saved to NVS: %u", level);
    } else {
        LOG_WARN("DEBUG_CTRL", "Failed to open preferences for debug level save");
    }
}

uint8_t EspnowMessageHandler::load_debug_level() {
    Preferences prefs;
    uint8_t level = MQTT_LOG_INFO;  // Default
    
    if (prefs.begin("debug", true)) {
        level = prefs.getUChar("log_level", MQTT_LOG_INFO);
        prefs.end();
        LOG_INFO("DEBUG_CTRL", "Debug level loaded from NVS: %u", level);
    } else {
        LOG_INFO("DEBUG_CTRL", "No saved debug level, using default: INFO");
    }
    
    return level;
}

// =============================================================================
// Network Configuration Handler Implementation
// =============================================================================

// Static member initialization
TaskHandle_t EspnowMessageHandler::network_config_task_handle_ = nullptr;
QueueHandle_t EspnowMessageHandler::network_config_queue_ = nullptr;

void EspnowMessageHandler::handle_network_config_request(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(network_config_request_t)) {
        LOG_ERROR("NET_CFG", "Invalid request message size: %d bytes", msg.len);
        return;
    }
    
    // Check if receiver connection is in CONNECTED state before responding
    auto& conn_mgr = EspNowConnectionManager::instance();
    auto state = conn_mgr.get_state();
    
    if (state != EspNowConnectionState::CONNECTED) {
        LOG_WARN("NET_CFG", "Cannot respond to network config request - receiver state is %u (need CONNECTED)",
                 (uint8_t)state);
        return;
    }
    
    // Store receiver MAC for ACK response
    memcpy(receiver_mac_, msg.mac, 6);
    
    LOG_INFO("NET_CFG", "Received network config request from receiver");
    
    // Send current configuration as ACK
    send_network_config_ack(true, "Current configuration");
}

void EspnowMessageHandler::handle_network_config_update(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(network_config_update_t)) {
        LOG_ERROR("NET_CFG", "Invalid message size: %d bytes", msg.len);
        return;
    }
    
    // Check if receiver connection is in CONNECTED state before responding
    auto& conn_mgr = EspNowConnectionManager::instance();
    auto state = conn_mgr.get_state();
    
    if (state != EspNowConnectionState::CONNECTED) {
        LOG_WARN("NET_CFG", "Cannot respond to network config update - receiver state is %u (need CONNECTED)",
                 (uint8_t)state);
        return;
    }
    
    const network_config_update_t* config = reinterpret_cast<const network_config_update_t*>(msg.data);
    
    // Store receiver MAC for ACK response
    memcpy(receiver_mac_, msg.mac, 6);
    
    LOG_INFO("NET_CFG", "Received network config update:");
    LOG_INFO("NET_CFG", "  Mode: %s", config->use_static_ip ? "Static" : "DHCP");
    
    if (config->use_static_ip) {
        LOG_INFO("NET_CFG", "  IP: %d.%d.%d.%d", 
                 config->ip[0], config->ip[1], config->ip[2], config->ip[3]);
        LOG_INFO("NET_CFG", "  Gateway: %d.%d.%d.%d",
                 config->gateway[0], config->gateway[1], config->gateway[2], config->gateway[3]);
        LOG_INFO("NET_CFG", "  Subnet: %d.%d.%d.%d",
                 config->subnet[0], config->subnet[1], config->subnet[2], config->subnet[3]);
        LOG_INFO("NET_CFG", "  DNS Primary: %d.%d.%d.%d",
                 config->dns_primary[0], config->dns_primary[1], config->dns_primary[2], config->dns_primary[3]);
        LOG_INFO("NET_CFG", "  DNS Secondary: %d.%d.%d.%d",
                 config->dns_secondary[0], config->dns_secondary[1], config->dns_secondary[2], config->dns_secondary[3]);
        
        // Quick validation (< 1ms) - more validation in background task
        if (config->ip[0] == 0) {
            LOG_ERROR("NET_CFG", "Invalid static IP (cannot be 0.0.0.0)");
            send_network_config_ack(false, "Invalid IP address");
            return;
        }
    }
    
    // Queue message for background processing (non-blocking)
    if (network_config_queue_ && xQueueSend(network_config_queue_, &msg, 0) == pdTRUE) {
        LOG_DEBUG("NET_CFG", "Message queued for background processing");
    } else {
        LOG_ERROR("NET_CFG", "Failed to queue message (queue full or not initialized)");
        send_network_config_ack(false, "Processing queue full");
    }
}

void EspnowMessageHandler::send_network_config_ack(bool success, const char* message) {
    network_config_ack_t ack;
    memset(&ack, 0, sizeof(ack));
    
    auto& eth = EthernetManager::instance();
    
    ack.type = msg_network_config_ack;
    ack.success = success ? 1 : 0;
    ack.use_static_ip = eth.isStaticIP() ? 1 : 0;
    
    // Current network configuration (active IP - could be DHCP or Static)
    IPAddress current_ip = eth.get_local_ip();
    IPAddress current_gateway = eth.get_gateway_ip();
    IPAddress current_subnet = eth.get_subnet_mask();
    
    ack.current_ip[0] = current_ip[0];
    ack.current_ip[1] = current_ip[1];
    ack.current_ip[2] = current_ip[2];
    ack.current_ip[3] = current_ip[3];
    
    ack.current_gateway[0] = current_gateway[0];
    ack.current_gateway[1] = current_gateway[1];
    ack.current_gateway[2] = current_gateway[2];
    ack.current_gateway[3] = current_gateway[3];
    
    ack.current_subnet[0] = current_subnet[0];
    ack.current_subnet[1] = current_subnet[1];
    ack.current_subnet[2] = current_subnet[2];
    ack.current_subnet[3] = current_subnet[3];
    
    // Saved static configuration (from NVS - used when static mode is enabled)
    IPAddress static_ip = eth.getStaticIP();
    IPAddress static_gateway = eth.getGateway();
    IPAddress static_subnet = eth.getSubnetMask();
    IPAddress static_dns_primary = eth.getDNSPrimary();
    IPAddress static_dns_secondary = eth.getDNSSecondary();
    
    ack.static_ip[0] = static_ip[0];
    ack.static_ip[1] = static_ip[1];
    ack.static_ip[2] = static_ip[2];
    ack.static_ip[3] = static_ip[3];
    
    ack.static_gateway[0] = static_gateway[0];
    ack.static_gateway[1] = static_gateway[1];
    ack.static_gateway[2] = static_gateway[2];
    ack.static_gateway[3] = static_gateway[3];
    
    ack.static_subnet[0] = static_subnet[0];
    ack.static_subnet[1] = static_subnet[1];
    ack.static_subnet[2] = static_subnet[2];
    ack.static_subnet[3] = static_subnet[3];
    
    ack.static_dns_primary[0] = static_dns_primary[0];
    ack.static_dns_primary[1] = static_dns_primary[1];
    ack.static_dns_primary[2] = static_dns_primary[2];
    ack.static_dns_primary[3] = static_dns_primary[3];
    
    ack.static_dns_secondary[0] = static_dns_secondary[0];
    ack.static_dns_secondary[1] = static_dns_secondary[1];
    ack.static_dns_secondary[2] = static_dns_secondary[2];
    ack.static_dns_secondary[3] = static_dns_secondary[3];
    
    ack.config_version = eth.getNetworkConfigVersion();
    
    strncpy(ack.message, message, sizeof(ack.message) - 1);
    ack.message[sizeof(ack.message) - 1] = '\0';
    
    // Ensure receiver is registered as peer before sending
    if (!EspnowPeerManager::is_peer_registered(receiver_mac_)) {
        LOG_WARN("NET_CFG", "Receiver not registered as peer, adding now");
        if (!EspnowPeerManager::add_peer(receiver_mac_)) {
            LOG_ERROR("NET_CFG", "Failed to add receiver as peer");
            return;
        }
    }
    
    esp_err_t result = esp_now_send(receiver_mac_, (const uint8_t*)&ack, sizeof(ack));
    if (result == ESP_OK) {
        LOG_INFO("NET_CFG", "Sent ACK: %s (success=%d)", message, success);
        LOG_DEBUG("NET_CFG", "  Current: %d.%d.%d.%d", 
                  ack.current_ip[0], ack.current_ip[1], ack.current_ip[2], ack.current_ip[3]);
        LOG_DEBUG("NET_CFG", "  Static saved: %d.%d.%d.%d", 
                  ack.static_ip[0], ack.static_ip[1], ack.static_ip[2], ack.static_ip[3]);
    } else {
        LOG_ERROR("NET_CFG", "Failed to send ACK: %s", esp_err_to_name(result));
    }
}

void EspnowMessageHandler::network_config_task_impl(void* parameter) {
    LOG_INFO("NET_CFG", "Background processing task started");
    
    espnow_queue_msg_t msg;
    auto& eth = EthernetManager::instance();
    
    while (true) {
        if (xQueueReceive(network_config_queue_, &msg, portMAX_DELAY) == pdTRUE) {
            const network_config_update_t* config = reinterpret_cast<const network_config_update_t*>(msg.data);
            
            LOG_INFO("NET_CFG", "Processing configuration in background...");
            
            // Heavy operations here (won't block ESP-NOW or control loop)
            if (config->use_static_ip) {
                // 1. Comprehensive validation
                bool valid = true;
                
                // Check for broadcast address
                if (config->ip[0] == 255 && config->ip[1] == 255 && 
                    config->ip[2] == 255 && config->ip[3] == 255) {
                    LOG_ERROR("NET_CFG", "IP cannot be broadcast address");
                    instance().send_network_config_ack(false, "IP is broadcast");
                    continue;
                }
                
                // Check for multicast range
                if (config->ip[0] >= 224 && config->ip[0] <= 239) {
                    LOG_ERROR("NET_CFG", "IP cannot be multicast address");
                    instance().send_network_config_ack(false, "IP is multicast");
                    continue;
                }
                
                // Check IP and gateway are in same subnet
                bool same_subnet = true;
                for (int i = 0; i < 4; i++) {
                    if ((config->ip[i] & config->subnet[i]) != (config->gateway[i] & config->subnet[i])) {
                        same_subnet = false;
                        break;
                    }
                }
                if (!same_subnet) {
                    LOG_WARN("NET_CFG", "IP and gateway not in same subnet - may cause routing issues");
                }
                
                // Check subnet mask validity
                uint32_t subnet_val = (config->subnet[0] << 24) | (config->subnet[1] << 16) | 
                                     (config->subnet[2] << 8) | config->subnet[3];
                uint32_t inverted = ~subnet_val + 1;
                if ((inverted & (inverted - 1)) != 0 && inverted != 0) {
                    LOG_ERROR("NET_CFG", "Invalid subnet mask (not contiguous)");
                    instance().send_network_config_ack(false, "Invalid subnet mask");
                    continue;
                }
                
                // 2. Check for IP conflicts (500ms)
                if (eth.checkIPConflict(config->ip)) {
                    LOG_ERROR("NET_CFG", "IP address conflict detected");
                    instance().send_network_config_ack(false, "IP in use by active device");
                    continue;
                }
                
                // 3. Test gateway reachability (2-4s)
                if (!eth.testStaticIPReachability(config->ip, config->gateway, 
                                                   config->subnet, config->dns_primary)) {
                    LOG_ERROR("NET_CFG", "Gateway unreachable");
                    instance().send_network_config_ack(false, "Gateway unreachable");
                    continue;
                }
            }
            
            // All checks passed, save to NVS
            if (eth.saveNetworkConfig(config->use_static_ip, config->ip, config->gateway,
                                      config->subnet, config->dns_primary, config->dns_secondary)) {
                LOG_INFO("NET_CFG", "✓ Configuration saved to NVS");
                instance().send_network_config_ack(true, "OK - reboot required");
            } else {
                LOG_ERROR("NET_CFG", "✗ Failed to save configuration");
                instance().send_network_config_ack(false, "NVS save failed");
            }
        }
    }
}

// =============================================================================
// MQTT Configuration Message Handlers
// =============================================================================

void EspnowMessageHandler::handle_mqtt_config_request(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(mqtt_config_request_t)) {
        LOG_ERROR("MQTT_CFG", "Invalid request message size: %d bytes", msg.len);
        return;
    }
    
    // Check if receiver connection is in CONNECTED state before responding
    auto& conn_mgr = EspNowConnectionManager::instance();
    auto state = conn_mgr.get_state();
    
    if (state != EspNowConnectionState::CONNECTED) {
        LOG_WARN("MQTT_CFG", "Cannot respond to MQTT config request - receiver state is %u (need CONNECTED)",
                 (uint8_t)state);
        return;
    }
    
    // Store receiver MAC for ACK response
    memcpy(receiver_mac_, msg.mac, 6);
    
    LOG_INFO("MQTT_CFG", "Received MQTT config request from receiver");
    
    // Send current configuration as ACK
    send_mqtt_config_ack(true, "Current configuration");
}

void EspnowMessageHandler::handle_mqtt_config_update(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(mqtt_config_update_t)) {
        LOG_ERROR("MQTT_CFG", "Invalid message size: %d bytes", msg.len);
        return;
    }
    
    // Check if receiver connection is in CONNECTED state before responding
    auto& conn_mgr = EspNowConnectionManager::instance();
    auto state = conn_mgr.get_state();
    
    if (state != EspNowConnectionState::CONNECTED) {
        LOG_WARN("MQTT_CFG", "Cannot respond to MQTT config update - receiver state is %u (need CONNECTED)",
                 (uint8_t)state);
        return;
    }
    
    const mqtt_config_update_t* config = reinterpret_cast<const mqtt_config_update_t*>(msg.data);
    
    // Store receiver MAC for ACK response
    memcpy(receiver_mac_, msg.mac, 6);
    
    LOG_INFO("MQTT_CFG", "Received MQTT config update:");
    LOG_INFO("MQTT_CFG", "  Enabled: %s", config->enabled ? "YES" : "NO");
    LOG_INFO("MQTT_CFG", "  Server: %d.%d.%d.%d:%d", 
             config->server[0], config->server[1], config->server[2], config->server[3],
             config->port);
    LOG_INFO("MQTT_CFG", "  Username: %s", strlen(config->username) > 0 ? config->username : "(none)");
    LOG_INFO("MQTT_CFG", "  Client ID: %s", config->client_id);
    
    // Validate configuration
    if (config->enabled) {
        // Check server IP is not 0.0.0.0 or 255.255.255.255
        if ((config->server[0] == 0 && config->server[1] == 0 && 
             config->server[2] == 0 && config->server[3] == 0) ||
            (config->server[0] == 255 && config->server[1] == 255 && 
             config->server[2] == 255 && config->server[3] == 255)) {
            LOG_ERROR("MQTT_CFG", "Invalid MQTT server address");
            send_mqtt_config_ack(false, "Invalid server IP");
            return;
        }
        
        // Validate port
        if (config->port < 1 || config->port > 65535) {
            LOG_ERROR("MQTT_CFG", "Invalid port: %d", config->port);
            send_mqtt_config_ack(false, "Invalid port number");
            return;
        }
        
        // Check client ID is not empty
        if (strlen(config->client_id) == 0) {
            LOG_ERROR("MQTT_CFG", "Client ID cannot be empty");
            send_mqtt_config_ack(false, "Client ID required");
            return;
        }
    }
    
    // Save configuration to NVS
    IPAddress server(config->server[0], config->server[1], config->server[2], config->server[3]);
    if (MqttConfigManager::saveConfig(config->enabled, server, config->port,
                                config->username, config->password, config->client_id)) {
        LOG_INFO("MQTT_CFG", "✓ Configuration saved to NVS");
        
        // Apply configuration with hot-reload
        LOG_INFO("MQTT_CFG", "Applying configuration (hot-reload)");
        MqttConfigManager::applyConfig();
        
        // Wait a moment for connection attempt
        delay(1000);
        
        send_mqtt_config_ack(true, "Config saved and applied");
    } else {
        LOG_ERROR("MQTT_CFG", "✗ Failed to save configuration");
        send_mqtt_config_ack(false, "NVS save failed");
    }
}

void EspnowMessageHandler::send_mqtt_config_ack(bool success, const char* message) {
    mqtt_config_ack_t ack;
    memset(&ack, 0, sizeof(ack));
    
    ack.type = msg_mqtt_config_ack;
    ack.success = success ? 1 : 0;
    ack.enabled = MqttConfigManager::isEnabled() ? 1 : 0;
    
    // Current MQTT configuration
    IPAddress server = MqttConfigManager::getServer();
    ack.server[0] = server[0];
    ack.server[1] = server[1];
    ack.server[2] = server[2];
    ack.server[3] = server[3];
    
    ack.port = MqttConfigManager::getPort();
    
    strncpy(ack.username, MqttConfigManager::getUsername(), sizeof(ack.username) - 1);
    ack.username[sizeof(ack.username) - 1] = '\0';
    
    strncpy(ack.password, MqttConfigManager::getPassword(), sizeof(ack.password) - 1);
    ack.password[sizeof(ack.password) - 1] = '\0';
    
    strncpy(ack.client_id, MqttConfigManager::getClientId(), sizeof(ack.client_id) - 1);
    ack.client_id[sizeof(ack.client_id) - 1] = '\0';
    
    ack.connected = MqttConfigManager::isConnected() ? 1 : 0;
    ack.config_version = MqttConfigManager::getConfigVersion();
    
    strncpy(ack.message, message, sizeof(ack.message) - 1);
    ack.message[sizeof(ack.message) - 1] = '\0';
    
    ack.checksum = 0;  // TODO: Implement checksum if needed
    
    // Ensure receiver is registered as peer before sending
    if (!EspnowPeerManager::is_peer_registered(receiver_mac_)) {
        LOG_WARN("MQTT_CFG", "Receiver not registered as peer, adding now");
        if (!EspnowPeerManager::add_peer(receiver_mac_)) {
            LOG_ERROR("MQTT_CFG", "Failed to add receiver as peer");
            return;
        }
    }
    
    esp_err_t result = esp_now_send(receiver_mac_, (const uint8_t*)&ack, sizeof(ack));
    if (result == ESP_OK) {
        LOG_INFO("MQTT_CFG", "✓ ACK sent to receiver (success=%d, connected=%d)", 
                 ack.success, ack.connected);
    } else {
        LOG_ERROR("MQTT_CFG", "✗ Failed to send ACK: %s", esp_err_to_name(result));
    }
}

