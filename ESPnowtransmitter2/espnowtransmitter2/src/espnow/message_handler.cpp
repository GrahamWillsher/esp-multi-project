#include "message_handler.h"
#include "../network/ethernet_manager.h"
#include "../config/task_config.h"
#include "../config/logging_config.h"
#include "../config/network_config.h"
#include <Arduino.h>
#include <esp_now.h>
#include <espnow_transmitter.h>
#include <espnow_peer_manager.h>
#include <espnow_message_router.h>
#include <espnow_standard_handlers.h>
#include <espnow_packet_utils.h>
#include <mqtt_logger.h>
#include <Preferences.h>
#include <ethernet_config.h>
#include <firmware_version.h>
#include <firmware_metadata.h>

// Stringify macro for build flags
#define STRINGIFY_IMPL(x) #x
#define STRINGIFY(x) STRINGIFY_IMPL(x)

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
    probe_config_.connection_flag = &receiver_connected_;
    probe_config_.peer_mac_storage = receiver_mac;
    probe_config_.on_connection = [](const uint8_t* mac, bool connected) {
        LOG_INFO("Receiver connected via PROBE");
    };
    
    // Setup ACK handler configuration
    ack_config_.connection_flag = &receiver_connected_;
    ack_config_.peer_mac_storage = receiver_mac;
    ack_config_.expected_seq = &g_ack_seq;
    ack_config_.lock_channel = &g_lock_channel;
    ack_config_.ack_received_flag = &g_ack_received;  // For channel hopping discovery
    ack_config_.set_wifi_channel = false;              // Don't change channel in handler - let discovery complete first
    ack_config_.on_connection = [](const uint8_t* mac, bool connected) {
        LOG_INFO("Receiver connected via ACK");
        
        // Send version information (static data, sent once on connection)
        version_announce_t announce;
        announce.type = msg_version_announce;
        announce.firmware_version = FW_VERSION_NUMBER;
        announce.protocol_version = PROTOCOL_VERSION;
        strncpy(announce.device_type, DEVICE_NAME, sizeof(announce.device_type) - 1);
        announce.device_type[sizeof(announce.device_type) - 1] = '\0';
        strncpy(announce.build_date, __DATE__, sizeof(announce.build_date) - 1);
        announce.build_date[sizeof(announce.build_date) - 1] = '\0';
        strncpy(announce.build_time, __TIME__, sizeof(announce.build_time) - 1);
        announce.build_time[sizeof(announce.build_time) - 1] = '\0';
        announce.min_compatible_version = MIN_COMPATIBLE_VERSION;
        announce.uptime_seconds = millis() / 1000;
        
        // Detailed logging of STATIC DATA being sent
        LOG_INFO("=== STATIC DATA VERSION ANNOUNCEMENT ===");
        LOG_INFO("  Message Type: %d (msg_version_announce)", announce.type);
        LOG_INFO("  Firmware Version: %u (v%d.%d.%d)", announce.firmware_version, 
                 FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
        LOG_INFO("  Protocol Version: %u", announce.protocol_version);
        LOG_INFO("  Min Compatible: %u", announce.min_compatible_version);
        LOG_INFO("  Device Type: '%s'", announce.device_type);
        LOG_INFO("  Build Date: '%s'", announce.build_date);
        LOG_INFO("  Build Time: '%s'", announce.build_time);
        LOG_INFO("  Uptime: %u seconds", announce.uptime_seconds);
        LOG_INFO("  Packet Size: %u bytes", sizeof(announce));
        LOG_INFO("=======================================");
        
        esp_err_t result = esp_now_send(mac, (const uint8_t*)&announce, sizeof(announce));
        if (result == ESP_OK) {
            LOG_INFO("✓ Version announcement sent successfully");
        } else {
            LOG_ERROR("✗ Failed to send version announcement: %s", esp_err_to_name(result));
        }
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
    
    // Register metadata request handler
    router.register_route(msg_metadata_request,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_metadata_request(*msg);
        },
        0xFF, this);
    
    // Register config request handler
    router.register_route(msg_config_request_full,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_config_request_full(*msg);
        },
        0xFF, this);
    
    // Register handlers for config messages we don't use (transmitter doesn't receive config)
    // but need to handle to avoid "unknown message" warnings
    router.register_route(msg_config_snapshot,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            LOG_DEBUG("Received CONFIG_SNAPSHOT (transmitter doesn't process these)");
        },
        0xFF, this);
    
    router.register_route(msg_config_update_delta,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            LOG_DEBUG("Received CONFIG_UPDATE_DELTA (transmitter doesn't process these)");
        },
        0xFF, this);
    
    router.register_route(msg_config_ack,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            LOG_DEBUG("Received CONFIG_ACK (transmitter doesn't process these)");
        },
        0xFF, this);
    
    router.register_route(msg_config_request_resync,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            LOG_DEBUG("Received CONFIG_REQUEST_RESYNC (transmitter doesn't process these)");
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
                
                LOG_INFO("Receiver version: v%d.%d.%d", rx_major, rx_minor, rx_patch);
                
                if (!isVersionCompatible(announce->firmware_version)) {
                    LOG_WARN("Version incompatible: transmitter v%d.%d.%d, receiver v%d.%d.%d",
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
                    LOG_DEBUG("Sent VERSION_RESPONSE to receiver");
                } else {
                    LOG_ERROR("Failed to send VERSION_RESPONSE: %s", esp_err_to_name(result));
                }
            }
        },
        0xFF, this);
    
    router.register_route(msg_version_response,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            if (msg->len >= (int)sizeof(version_response_t)) {
                const version_response_t* response = reinterpret_cast<const version_response_t*>(msg->data);
                LOG_DEBUG("Received VERSION_RESPONSE: %s v%d.%d.%d",
                         response->device_type,
                         response->firmware_version / 10000,
                         (response->firmware_version / 100) % 100,
                         response->firmware_version % 100);
            }
        },
        0xFF, this);
    
    LOG_DEBUG("Registered %d message routes", router.route_count());
}

void EspnowMessageHandler::start_rx_task(QueueHandle_t queue) {
    xTaskCreate(
        rx_task_impl,
        "espnow_rx",
        task_config::STACK_SIZE_ESPNOW_RX,
        (void*)queue,
        task_config::PRIORITY_CRITICAL,
        nullptr
    );
    LOG_DEBUG("ESP-NOW RX task started");
}

void EspnowMessageHandler::rx_task_impl(void* parameter) {
    QueueHandle_t queue = (QueueHandle_t)parameter;
    auto& handler = instance();
    auto& router = EspnowMessageRouter::instance();
    
    LOG_DEBUG("Message RX task running");
    espnow_queue_msg_t msg;
    
    while (true) {
        if (xQueueReceive(queue, &msg, portMAX_DELAY) == pdTRUE) {
            // Route message using common router
            if (!router.route_message(msg)) {
                // Message not handled by any route
                uint8_t msg_type = (msg.len > 0) ? msg.data[0] : 0;
                LOG_WARN("Unknown message type: %u from %02X:%02X:%02X:%02X:%02X:%02X",
                             msg_type, msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
            }
        }
    }
}

void EspnowMessageHandler::handle_request_data(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(request_data_t)) return;
    
    const request_data_t* req = reinterpret_cast<const request_data_t*>(msg.data);
    LOG_DEBUG("REQUEST_DATA (subtype=%d) from %02X:%02X:%02X:%02X:%02X:%02X",
                 req->subtype, msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
    
    // Handle based on subtype
    switch (req->subtype) {
        case subtype_power_profile:
            transmission_active_ = true;
            LOG_INFO(">>> Power profile transmission STARTED");
            break;
        
        case subtype_settings: {
            LOG_DEBUG(">>> Settings request - sending IP data");
            
            // Send Ethernet IP configuration to receiver
            if (EthernetManager::instance().is_connected()) {
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
                
                // Calculate checksum using common utility
                packet.checksum = EspnowPacketUtils::calculate_checksum(packet.payload, packet.payload_len);
                
                // Send IP data via ESP-NOW
                esp_err_t result = esp_now_send(msg.mac, (const uint8_t*)&packet, sizeof(packet));
                
                if (result == ESP_OK) {
                    // Log packet info using common utility
                    EspnowPacketUtils::PacketInfo info;
                    info.seq = packet.seq;
                    info.frag_index = packet.frag_index;
                    info.frag_total = packet.frag_total;
                    info.payload_len = packet.payload_len;
                    info.subtype = packet.subtype;
                    info.checksum = packet.checksum;
                    info.payload = packet.payload;
                    EspnowPacketUtils::print_packet_info(info, "SETTINGS (sent)");
                    
                    LOG_DEBUG("Sent IP data: %s, Gateway: %s, Subnet: %s",
                                 local_ip.toString().c_str(),
                                 gateway.toString().c_str(),
                                 subnet.toString().c_str());
                } else {
                    LOG_ERROR("Failed to send IP data: %s", esp_err_to_name(result));
                }
            } else {
                LOG_ERROR("Ethernet not connected, cannot send IP");
            }
            break;
        }
        
        case subtype_events:
        case subtype_logs:
        case subtype_cell_info:
            LOG_DEBUG("Subtype %d not implemented yet", req->subtype);
            break;
            
        default:
            LOG_WARN("Unknown subtype: %d", req->subtype);
            break;
    }
}

void EspnowMessageHandler::handle_abort_data(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(abort_data_t)) return;
    
    const abort_data_t* abort = reinterpret_cast<const abort_data_t*>(msg.data);
    LOG_DEBUG("ABORT_DATA (subtype=%d) from %02X:%02X:%02X:%02X:%02X:%02X",
                 abort->subtype, msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
    
    // Handle based on subtype
    switch (abort->subtype) {
        case subtype_power_profile:
            transmission_active_ = false;
            LOG_INFO(">>> Power profile transmission STOPPED");
            break;
        
        case subtype_settings:
        case subtype_events:
        case subtype_logs:
        case subtype_cell_info:
            LOG_DEBUG("Subtype %d not implemented yet", abort->subtype);
            break;
            
        default:
            LOG_WARN("Unknown subtype: %d", abort->subtype);
            break;
    }
}

void EspnowMessageHandler::handle_reboot(const espnow_queue_msg_t& msg) {
    LOG_INFO("REBOOT command from %02X:%02X:%02X:%02X:%02X:%02X",
                 msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
    LOG_INFO(">>> Rebooting in 1 second...");
    Serial.flush();
    delay(1000);
    ESP.restart();
}

void EspnowMessageHandler::handle_ota_start(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(ota_start_t)) return;
    
    const ota_start_t* ota = reinterpret_cast<const ota_start_t*>(msg.data);
    LOG_INFO("OTA_START command (size=%u bytes) from %02X:%02X:%02X:%02X:%02X:%02X",
                 ota->size, msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
    LOG_INFO(">>> OTA mode ready - waiting for HTTP POST...");
}

// ═══════════════════════════════════════════════════════════════════════
// Helper function: Send IP configuration to receiver
// Called automatically when Ethernet connects
// ═══════════════════════════════════════════════════════════════════════

void send_ip_to_receiver() {
    if (!EthernetManager::instance().is_connected()) return;
    
    // Get receiver MAC from espnow_transmitter library
    extern uint8_t receiver_mac[];
    
    // Safety check: Don't send if receiver is broadcast address (not discovered yet)
    bool is_broadcast = true;
    for (int i = 0; i < 6; i++) {
        if (receiver_mac[i] != 0xFF) {
            is_broadcast = false;
            break;
        }
    }
    
    if (is_broadcast) {
        LOG_DEBUG("[ETH] Receiver not discovered yet, will send IP later");
        return;
    }
    
    // Check if receiver peer exists
    if (!esp_now_is_peer_exist(receiver_mac)) {
        LOG_DEBUG("[ETH] Receiver peer not registered, skipping IP send");
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
    esp_err_t result = esp_now_send(receiver_mac, (const uint8_t*)&packet, sizeof(packet));
    
    if (result == ESP_OK) {
        LOG_INFO("[ETH] Sent IP configuration to receiver: %s", local_ip.toString().c_str());
    } else {
        LOG_WARN("[ETH] Failed to send IP to receiver: %s", esp_err_to_name(result));
    }
}

void EspnowMessageHandler::handle_debug_control(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(debug_control_t)) {
        LOG_WARN("Invalid debug_control packet size: %d", msg.len);
        return;
    }
    
    const debug_control_t* pkt = reinterpret_cast<const debug_control_t*>(msg.data);
    
    LOG_INFO("Received debug level change request: %u", pkt->level);
    
    // Store receiver MAC for ACK
    memcpy(receiver_mac_, msg.mac, 6);
    
    // Validate level
    if (pkt->level > MQTT_LOG_DEBUG) {
        LOG_WARN("Invalid debug level: %u", pkt->level);
        send_debug_ack(pkt->level, MQTT_LOG_DEBUG, 1);
        return;
    }
    
    // Store previous level
    MqttLogLevel previous = MqttLogger::instance().get_level();
    
    // Apply new level
    MqttLogger::instance().set_level((MqttLogLevel)pkt->level);
    
    // Save to preferences for persistence
    save_debug_level(pkt->level);
    
    LOG_INFO("Debug level changed: %s → %s", 
             MqttLogger::instance().level_to_string(previous),
             MqttLogger::instance().level_to_string((MqttLogLevel)pkt->level));
    
    // Send acknowledgment
    send_debug_ack(pkt->level, (uint8_t)previous, 0);
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
        LOG_DEBUG("Debug ACK sent (applied=%u, status=%u)", applied, status);
    } else {
        LOG_WARN("Failed to send debug ACK: %s", esp_err_to_name(result));
    }
}

void EspnowMessageHandler::save_debug_level(uint8_t level) {
    Preferences prefs;
    if (prefs.begin("debug", false)) {
        prefs.putUChar("log_level", level);
        prefs.end();
        LOG_DEBUG("Debug level saved to NVS: %u", level);
    } else {
        LOG_WARN("Failed to open preferences for debug level save");
    }
}

uint8_t EspnowMessageHandler::load_debug_level() {
    Preferences prefs;
    uint8_t level = MQTT_LOG_INFO;  // Default
    
    if (prefs.begin("debug", true)) {
        level = prefs.getUChar("log_level", MQTT_LOG_INFO);
        prefs.end();
        LOG_INFO("Debug level loaded from NVS: %u", level);
    } else {
        LOG_INFO("No saved debug level, using default: INFO");
    }
    
    return level;
}

void EspnowMessageHandler::handle_config_request_full(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(config_request_full_t)) return;
    
    const config_request_full_t* req = reinterpret_cast<const config_request_full_t*>(msg.data);
    LOG_INFO("CONFIG_REQUEST_FULL (ID=%u) from %02X:%02X:%02X:%02X:%02X:%02X",
             req->request_id, msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
    
    // Create configuration snapshot using actual config values
    FullConfigSnapshot config;
    
    // MQTT configuration - from network_config.h
    const auto& mqtt_cfg = config::get_mqtt_config();
    strncpy(config.mqtt.server, mqtt_cfg.server, sizeof(config.mqtt.server) - 1);
    config.mqtt.port = mqtt_cfg.port;
    strncpy(config.mqtt.username, mqtt_cfg.username, sizeof(config.mqtt.username) - 1);
    strncpy(config.mqtt.password, mqtt_cfg.password, sizeof(config.mqtt.password) - 1);
    strncpy(config.mqtt.client_id, mqtt_cfg.client_id, sizeof(config.mqtt.client_id) - 1);
    strncpy(config.mqtt.topic_prefix, mqtt_cfg.topics.data, sizeof(config.mqtt.topic_prefix) - 1);
    config.mqtt.enabled = config::features::MQTT_ENABLED;
    config.mqtt.timeout_ms = 5000;
    
    // Network configuration - from ethernet_config.h
    config.network.use_static_ip = EthernetConfig::Network::USE_STATIC_IP;
    if (EthernetConfig::Network::USE_STATIC_IP) {
        config.network.ip[0] = EthernetConfig::Network::STATIC_IP[0];
        config.network.ip[1] = EthernetConfig::Network::STATIC_IP[1];
        config.network.ip[2] = EthernetConfig::Network::STATIC_IP[2];
        config.network.ip[3] = EthernetConfig::Network::STATIC_IP[3];
        config.network.gateway[0] = EthernetConfig::Network::GATEWAY[0];
        config.network.gateway[1] = EthernetConfig::Network::GATEWAY[1];
        config.network.gateway[2] = EthernetConfig::Network::GATEWAY[2];
        config.network.gateway[3] = EthernetConfig::Network::GATEWAY[3];
        config.network.subnet[0] = EthernetConfig::Network::SUBNET[0];
        config.network.subnet[1] = EthernetConfig::Network::SUBNET[1];
        config.network.subnet[2] = EthernetConfig::Network::SUBNET[2];
        config.network.subnet[3] = EthernetConfig::Network::SUBNET[3];
        config.network.dns[0] = EthernetConfig::Network::DNS[0];
        config.network.dns[1] = EthernetConfig::Network::DNS[1];
        config.network.dns[2] = EthernetConfig::Network::DNS[2];
        config.network.dns[3] = EthernetConfig::Network::DNS[3];
    }
    strncpy(config.network.hostname, "espnow-transmitter", sizeof(config.network.hostname) - 1);
    
    // Battery configuration (example defaults - TODO: add to config file if needed)
    config.battery.pack_voltage_max = 58000;  // 58V in mV
    config.battery.pack_voltage_min = 46000;  // 46V in mV
    config.battery.cell_voltage_max = 3650;   // 3.65V in mV
    config.battery.cell_voltage_min = 2900;   // 2.9V in mV
    config.battery.double_battery = false;
    config.battery.use_estimated_soc = true;
    
    // Power configuration (example defaults - TODO: add to config file if needed)
    config.power.charge_power_w = 3000;
    config.power.discharge_power_w = 3000;
    config.power.max_precharge_ms = 15000;
    config.power.precharge_duration_ms = 100;
    
    // Inverter configuration (example defaults - TODO: add to config file if needed)
    config.inverter.total_cells = 16;
    config.inverter.modules = 1;
    config.inverter.cells_per_module = 16;
    config.inverter.voltage_level = 48;
    config.inverter.capacity_ah = 100;
    config.inverter.battery_type = 0;
    
    // CAN configuration (example defaults - TODO: add to config file if needed)
    config.can.frequency_khz = 500;
    config.can.fd_frequency_mhz = 8;
    config.can.sofar_id = 0;
    config.can.pylon_send_interval = 1000;
    
    // Contactor configuration (example defaults - TODO: add to config file if needed)
    config.contactor.control_enabled = false;
    config.contactor.nc_contactor = false;
    config.contactor.pwm_frequency = 1000;
    
    // System configuration
    config.system.web_enabled = true;
    config.system.log_level = 3;  // INFO
    config.system.led_mode = 0;
    
    // Calculate checksum
    config.checksum = calculateCRC32((uint8_t*)&config, sizeof(config) - sizeof(uint32_t));
    
    // Fragment and send the configuration (config is larger than ESP-NOW max payload)
    const size_t fragment_size = 230;  // Max payload per fragment
    const uint8_t* data_ptr = (const uint8_t*)&config;
    const size_t total_size = sizeof(config);
    const uint16_t total_fragments = (total_size + fragment_size - 1) / fragment_size;
    const uint32_t seq = esp_random();
    
    LOG_INFO("CONFIG: Sending snapshot (%u bytes in %u fragments)", total_size, total_fragments);
    
    for (uint16_t frag = 0; frag < total_fragments; frag++) {
        espnow_packet_t packet;
        packet.type = msg_config_snapshot;
        packet.subtype = 0;
        packet.seq = seq;
        packet.frag_index = frag;
        packet.frag_total = total_fragments;
        
        size_t offset = frag * fragment_size;
        size_t remaining = total_size - offset;
        packet.payload_len = (remaining > fragment_size) ? fragment_size : remaining;
        
        memcpy(packet.payload, data_ptr + offset, packet.payload_len);
        packet.checksum = 0;  // Fragment checksum not used for config (whole config has CRC32)
        
        // Calculate actual packet size (header + payload, not full 230-byte array)
        size_t packet_size = sizeof(espnow_packet_t) - sizeof(packet.payload) + packet.payload_len;
        
        esp_err_t result = esp_now_send(msg.mac, (uint8_t*)&packet, packet_size);
        
        if (result == ESP_OK) {
            LOG_DEBUG("CONFIG: Sent fragment %u/%u (%u bytes)", 
                     frag + 1, total_fragments, packet.payload_len);
        } else {
            LOG_ERROR("CONFIG: Failed to send fragment %u: %s", frag, esp_err_to_name(result));
            return;
        }
        
        // Small delay between fragments to avoid overwhelming receiver
        delay(10);
    }
    
    LOG_INFO("CONFIG: Snapshot sent successfully");
}

void EspnowMessageHandler::handle_metadata_request(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(metadata_request_t)) {
        LOG_WARN("METADATA_REQUEST: Invalid message length %d", msg.len);
        return;
    }
    
    const metadata_request_t* req = reinterpret_cast<const metadata_request_t*>(msg.data);
    LOG_INFO("METADATA_REQUEST: request_id=%u from receiver", req->request_id);
    
    // Read our firmware metadata
    metadata_response_t response;
    response.type = msg_metadata_response;
    response.request_id = req->request_id;
    response.valid = FirmwareMetadata::isValid(FirmwareMetadata::metadata);
    
    if (response.valid) {
        // Use metadata from .rodata
        const auto& meta = FirmwareMetadata::metadata;
        strncpy(response.env_name, meta.env_name, sizeof(response.env_name) - 1);
        response.env_name[sizeof(response.env_name) - 1] = '\0';
        
        strncpy(response.device_type, meta.device_type, sizeof(response.device_type) - 1);
        response.device_type[sizeof(response.device_type) - 1] = '\0';
        
        response.version_major = meta.version_major;
        response.version_minor = meta.version_minor;
        response.version_patch = meta.version_patch;
        
        strncpy(response.build_date, meta.build_date, sizeof(response.build_date) - 1);
        response.build_date[sizeof(response.build_date) - 1] = '\0';
        
        LOG_INFO("METADATA: Sending valid metadata ● %s %s v%d.%d.%d",
                 response.device_type, response.env_name,
                 response.version_major, response.version_minor, response.version_patch);
    } else {
        // Fallback to build flags
        strncpy(response.env_name, STRINGIFY(PIO_ENV_NAME), sizeof(response.env_name) - 1);
        response.env_name[sizeof(response.env_name) - 1] = '\0';
        
        strncpy(response.device_type, STRINGIFY(TARGET_DEVICE), sizeof(response.device_type) - 1);
        response.device_type[sizeof(response.device_type) - 1] = '\0';
        
        response.version_major = FW_VERSION_MAJOR;
        response.version_minor = FW_VERSION_MINOR;
        response.version_patch = FW_VERSION_PATCH;
        
        strcpy(response.build_date, __DATE__ " " __TIME__);
        
        LOG_INFO("METADATA: Sending fallback metadata * v%d.%d.%d",
                 response.version_major, response.version_minor, response.version_patch);
    }
    
    // Send response back to receiver
    esp_err_t result = esp_now_send(msg.mac, (uint8_t*)&response, sizeof(response));
    if (result == ESP_OK) {
        LOG_INFO("METADATA: Response sent successfully");
    } else {
        LOG_ERROR("METADATA: Failed to send response: %s", esp_err_to_name(result));
    }
}
