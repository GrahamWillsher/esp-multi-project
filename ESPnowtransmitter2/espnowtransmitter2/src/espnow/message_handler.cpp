#include "message_handler.h"
#include "discovery_task.h"
#include "version_beacon_manager.h"
#include "keep_alive_manager.h"
#include "../network/ethernet_manager.h"
#include "../config/task_config.h"
#include "../config/logging_config.h"
#include "../config/network_config.h"
#include "../settings/settings_manager.h"
#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <espnow_transmitter.h>
#include <espnow_peer_manager.h>
#include <espnow_message_router.h>
#include <espnow_standard_handlers.h>
#include <espnow_packet_utils.h>
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
    
    // Register config section request handler (Phase 4: Version-based cache sync)
    router.register_route(msg_config_section_request,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            if (msg->len >= (int)sizeof(config_section_request_t)) {
                const config_section_request_t* request = reinterpret_cast<const config_section_request_t*>(msg->data);
                VersionBeaconManager::instance().handle_config_request(request, msg->mac);
            }
        },
        0xFF, this);
    
    router.register_route(msg_config_request_resync,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            LOG_DEBUG("Received CONFIG_REQUEST_RESYNC (transmitter doesn't process these)");
        },
        0xFF, this);
    
    // Register heartbeat handler (Section 11: Keep-alive)
    router.register_route(msg_heartbeat,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            // Heartbeat received from receiver - connection is alive
            LOG_DEBUG("[HEARTBEAT] Received from receiver");
            KeepAliveManager::instance().record_heartbeat_received();
        },
        0xFF, this);
    
    // Phase 2: Settings update handler
    router.register_route(msg_battery_settings_update,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            SettingsManager::instance().handle_settings_update(*msg);
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
                
                LOG_INFO("Receiver version: %d.%d.%d", rx_major, rx_minor, rx_patch);
                
                if (!isVersionCompatible(announce->firmware_version)) {
                    LOG_WARN("Version incompatible: transmitter %d.%d.%d, receiver %d.%d.%d",
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
                LOG_DEBUG("Received VERSION_RESPONSE: %s %d.%d.%d",
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
    // Create main RX task
    xTaskCreate(
        rx_task_impl,
        "espnow_rx",
        task_config::STACK_SIZE_ESPNOW_RX,
        (void*)queue,
        task_config::PRIORITY_CRITICAL,
        nullptr
    );
    LOG_DEBUG("ESP-NOW RX task started");
    
    // Create network config processing queue and task
    network_config_queue_ = xQueueCreate(task_config::NETWORK_CONFIG_QUEUE_SIZE, sizeof(espnow_queue_msg_t));
    if (network_config_queue_ == nullptr) {
        LOG_ERROR("Failed to create network config queue");
    } else {
        xTaskCreate(
            network_config_task_impl,
            "net_config",
            task_config::STACK_SIZE_NETWORK_CONFIG,
            nullptr,
            task_config::PRIORITY_NETWORK_CONFIG,
            &network_config_task_handle_
        );
        LOG_DEBUG("Network config task started (priority=%d)", task_config::PRIORITY_NETWORK_CONFIG);
    }
}

void EspnowMessageHandler::rx_task_impl(void* parameter) {
    QueueHandle_t queue = (QueueHandle_t)parameter;
    auto& handler = instance();
    auto& router = EspnowMessageRouter::instance();
    
    LOG_DEBUG("Message RX task running");
    espnow_queue_msg_t msg;
    const TickType_t timeout_ticks = pdMS_TO_TICKS(1000);  // Check timeout every second
    const uint32_t CONNECTION_TIMEOUT_MS = 10000;  // 10 second timeout (matches receiver)
    
    while (true) {
        if (xQueueReceive(queue, &msg, timeout_ticks) == pdTRUE) {
            // Update last RX time for ANY message from receiver
            if (memcmp(msg.mac, handler.receiver_mac_, 6) == 0) {
                handler.receiver_state_.last_rx_time_ms = millis();
                if (!handler.receiver_state_.is_connected) {
                    handler.receiver_state_.is_connected = true;
                    LOG_INFO("[WATCHDOG] Receiver connection restored");
                }
            }
            
            // Route message using common router
            if (!router.route_message(msg)) {
                // Message not handled by any route
                uint8_t msg_type = (msg.len > 0) ? msg.data[0] : 0;
                LOG_WARN("Unknown message type: %u from %02X:%02X:%02X:%02X:%02X:%02X",
                             msg_type, msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
            }
        }
        
        // Timeout watchdog (runs every second when queue empty or after each message)
        if (handler.receiver_state_.is_connected) {
            uint32_t time_since_last_rx = millis() - handler.receiver_state_.last_rx_time_ms;
            if (time_since_last_rx > CONNECTION_TIMEOUT_MS) {
                uint32_t downtime_start = millis();
                
                handler.receiver_state_.is_connected = false;
                handler.receiver_connected_ = false;
                LOG_WARN("[WATCHDOG] ═══ RECEIVER CONNECTION LOST (timeout: %u ms) ═══", CONNECTION_TIMEOUT_MS);
                
                // CRITICAL: Multi-layer channel enforcement before restart
                uint8_t current_ch = 0;
                wifi_second_chan_t second;
                esp_wifi_get_channel(&current_ch, &second);
                
                LOG_INFO("[WATCHDOG] Channel state: current=%d, locked=%d", current_ch, g_lock_channel);
                
                // ALWAYS force-set channel (defensive programming)
                if (!set_channel(g_lock_channel)) {
                    LOG_ERROR("[WATCHDOG] ✗ Failed to force-set WiFi channel to %d", g_lock_channel);
                } else {
                    LOG_INFO("[WATCHDOG] ✓ Channel force-set to %d", g_lock_channel);
                }
                
                // Industrial delay for WiFi stack stabilization
                delay(150);
                
                // Verify channel lock
                esp_wifi_get_channel(&current_ch, &second);
                if (current_ch != g_lock_channel) {
                    LOG_ERROR("[WATCHDOG] ✗ Channel verification failed: expected=%d, actual=%d", 
                             g_lock_channel, current_ch);
                } else {
                    LOG_INFO("[WATCHDOG] ✓ Channel verified and locked: %d", current_ch);
                }
                
                // Restart discovery task to find receiver again
                LOG_INFO("[WATCHDOG] Initiating discovery restart...");
                DiscoveryTask::instance().restart();
                
                // Track downtime for metrics
                uint32_t downtime_ms = millis() - downtime_start;
                DiscoveryMetrics& metrics = const_cast<DiscoveryMetrics&>(
                    DiscoveryTask::instance().get_metrics());
                if (downtime_ms > metrics.longest_downtime_ms) {
                    metrics.longest_downtime_ms = downtime_ms;
                }
                
                LOG_INFO("[WATCHDOG] ═══ WATCHDOG RECOVERY COMPLETE (%d ms) ═══", downtime_ms);
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
                    LOG_DEBUG("Sent network config: %s, GW: %s, Subnet: %s",
                             local_ip.toString().c_str(), gateway.toString().c_str(), subnet.toString().c_str());
                } else {
                    LOG_WARN("Failed to send network config: %s", esp_err_to_name(result));
                }
            } else {
                LOG_WARN("Ethernet not connected, cannot send network config");
            }
            break;
        }
        
        case subtype_battery_config: {
            // Phase 3: Send ALL battery settings (including currents and SOC limits)
            LOG_DEBUG(">>> Battery config request - sending FULL battery settings");
            
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
                LOG_INFO("Sent FULL battery settings: %dWh, %dS, %s, %.1fA/%.1fA, SOC:%d-%d%%", 
                        settings_msg.capacity_wh, settings_msg.cell_count, chem[settings_msg.chemistry],
                        settings_msg.max_charge_current_a, settings_msg.max_discharge_current_a,
                        settings_msg.soc_low_limit, settings_msg.soc_high_limit);
            } else {
                LOG_WARN("Failed to send battery settings: %s", esp_err_to_name(result));
            }
            break;
        }
        
        case subtype_settings: {
            // DEPRECATED: Send BOTH IP and battery (backward compatibility)
            LOG_DEBUG(">>> Settings request (legacy) - sending IP + battery data");
            
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
                    LOG_DEBUG("Sent IP data (legacy subtype_settings)");
                } else {
                    LOG_WARN("Failed to send IP data: %s", esp_err_to_name(result));
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
                    LOG_INFO("Sent empty IP data (Ethernet not connected yet)");
                } else {
                    LOG_WARN("Failed to send empty IP data: %s", esp_err_to_name(result));
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
                LOG_INFO("Sent battery settings: %dWh, %dS, %s, %.1f/%.1fA, SOC:%d-%d%%", 
                        settings_msg.capacity_wh, settings_msg.cell_count, chem[settings_msg.chemistry],
                        settings_msg.max_charge_current_a, settings_msg.max_discharge_current_a,
                        settings_msg.soc_low_limit, settings_msg.soc_high_limit);
            } else {
                LOG_WARN("Failed to send battery settings: %s", esp_err_to_name(result));
            }
            break;
        }
        
        case subtype_charger_config:
        case subtype_inverter_config:
        case subtype_system_config:
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
        
        LOG_INFO("METADATA: Sending valid metadata ● %s %s %d.%d.%d",
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
        
        LOG_INFO("METADATA: Sending fallback metadata * %d.%d.%d",
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

// =============================================================================
// Network Configuration Handler Implementation
// =============================================================================

// Static member initialization
TaskHandle_t EspnowMessageHandler::network_config_task_handle_ = nullptr;
QueueHandle_t EspnowMessageHandler::network_config_queue_ = nullptr;

void EspnowMessageHandler::handle_network_config_request(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(network_config_request_t)) {
        LOG_ERROR("[NET_CFG] Invalid request message size: %d bytes", msg.len);
        return;
    }
    
    // Store receiver MAC for ACK response
    memcpy(receiver_mac_, msg.mac, 6);
    
    LOG_INFO("[NET_CFG] Received network config request from receiver");
    
    // Send current configuration as ACK
    send_network_config_ack(true, "Current configuration");
}

void EspnowMessageHandler::handle_network_config_update(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(network_config_update_t)) {
        LOG_ERROR("[NET_CFG] Invalid message size: %d bytes", msg.len);
        return;
    }
    
    const network_config_update_t* config = reinterpret_cast<const network_config_update_t*>(msg.data);
    
    // Store receiver MAC for ACK response
    memcpy(receiver_mac_, msg.mac, 6);
    
    LOG_INFO("[NET_CFG] Received network config update:");
    LOG_INFO("[NET_CFG]   Mode: %s", config->use_static_ip ? "Static" : "DHCP");
    
    if (config->use_static_ip) {
        LOG_INFO("[NET_CFG]   IP: %d.%d.%d.%d", 
                 config->ip[0], config->ip[1], config->ip[2], config->ip[3]);
        LOG_INFO("[NET_CFG]   Gateway: %d.%d.%d.%d",
                 config->gateway[0], config->gateway[1], config->gateway[2], config->gateway[3]);
        LOG_INFO("[NET_CFG]   Subnet: %d.%d.%d.%d",
                 config->subnet[0], config->subnet[1], config->subnet[2], config->subnet[3]);
        LOG_INFO("[NET_CFG]   DNS Primary: %d.%d.%d.%d",
                 config->dns_primary[0], config->dns_primary[1], config->dns_primary[2], config->dns_primary[3]);
        LOG_INFO("[NET_CFG]   DNS Secondary: %d.%d.%d.%d",
                 config->dns_secondary[0], config->dns_secondary[1], config->dns_secondary[2], config->dns_secondary[3]);
        
        // Quick validation (< 1ms) - more validation in background task
        if (config->ip[0] == 0) {
            LOG_ERROR("[NET_CFG] Invalid static IP (cannot be 0.0.0.0)");
            send_network_config_ack(false, "Invalid IP address");
            return;
        }
    }
    
    // Queue message for background processing (non-blocking)
    if (network_config_queue_ && xQueueSend(network_config_queue_, &msg, 0) == pdTRUE) {
        LOG_DEBUG("[NET_CFG] Message queued for background processing");
    } else {
        LOG_ERROR("[NET_CFG] Failed to queue message (queue full or not initialized)");
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
        LOG_WARN("[NET_CFG] Receiver not registered as peer, adding now");
        if (!EspnowPeerManager::add_peer(receiver_mac_)) {
            LOG_ERROR("[NET_CFG] Failed to add receiver as peer");
            return;
        }
    }
    
    esp_err_t result = esp_now_send(receiver_mac_, (const uint8_t*)&ack, sizeof(ack));
    if (result == ESP_OK) {
        LOG_INFO("[NET_CFG] Sent ACK: %s (success=%d)", message, success);
        LOG_DEBUG("[NET_CFG]   Current: %d.%d.%d.%d", 
                  ack.current_ip[0], ack.current_ip[1], ack.current_ip[2], ack.current_ip[3]);
        LOG_DEBUG("[NET_CFG]   Static saved: %d.%d.%d.%d", 
                  ack.static_ip[0], ack.static_ip[1], ack.static_ip[2], ack.static_ip[3]);
    } else {
        LOG_ERROR("[NET_CFG] Failed to send ACK: %s", esp_err_to_name(result));
    }
}

void EspnowMessageHandler::network_config_task_impl(void* parameter) {
    LOG_INFO("[NET_CFG] Background processing task started");
    
    espnow_queue_msg_t msg;
    auto& eth = EthernetManager::instance();
    
    while (true) {
        if (xQueueReceive(network_config_queue_, &msg, portMAX_DELAY) == pdTRUE) {
            const network_config_update_t* config = reinterpret_cast<const network_config_update_t*>(msg.data);
            
            LOG_INFO("[NET_CFG] Processing configuration in background...");
            
            // Heavy operations here (won't block ESP-NOW or control loop)
            if (config->use_static_ip) {
                // 1. Comprehensive validation
                bool valid = true;
                
                // Check for broadcast address
                if (config->ip[0] == 255 && config->ip[1] == 255 && 
                    config->ip[2] == 255 && config->ip[3] == 255) {
                    LOG_ERROR("[NET_CFG] IP cannot be broadcast address");
                    instance().send_network_config_ack(false, "IP is broadcast");
                    continue;
                }
                
                // Check for multicast range
                if (config->ip[0] >= 224 && config->ip[0] <= 239) {
                    LOG_ERROR("[NET_CFG] IP cannot be multicast address");
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
                    LOG_WARN("[NET_CFG] IP and gateway not in same subnet - may cause routing issues");
                }
                
                // Check subnet mask validity
                uint32_t subnet_val = (config->subnet[0] << 24) | (config->subnet[1] << 16) | 
                                     (config->subnet[2] << 8) | config->subnet[3];
                uint32_t inverted = ~subnet_val + 1;
                if ((inverted & (inverted - 1)) != 0 && inverted != 0) {
                    LOG_ERROR("[NET_CFG] Invalid subnet mask (not contiguous)");
                    instance().send_network_config_ack(false, "Invalid subnet mask");
                    continue;
                }
                
                // 2. Check for IP conflicts (500ms)
                if (eth.checkIPConflict(config->ip)) {
                    LOG_ERROR("[NET_CFG] IP address conflict detected");
                    instance().send_network_config_ack(false, "IP in use by active device");
                    continue;
                }
                
                // 3. Test gateway reachability (2-4s)
                if (!eth.testStaticIPReachability(config->ip, config->gateway, 
                                                   config->subnet, config->dns_primary)) {
                    LOG_ERROR("[NET_CFG] Gateway unreachable");
                    instance().send_network_config_ack(false, "Gateway unreachable");
                    continue;
                }
            }
            
            // All checks passed, save to NVS
            if (eth.saveNetworkConfig(config->use_static_ip, config->ip, config->gateway,
                                      config->subnet, config->dns_primary, config->dns_secondary)) {
                LOG_INFO("[NET_CFG] ✓ Configuration saved to NVS");
                instance().send_network_config_ack(true, "OK - reboot required");
            } else {
                LOG_ERROR("[NET_CFG] ✗ Failed to save configuration");
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
        LOG_ERROR("[MQTT_CFG] Invalid request message size: %d bytes", msg.len);
        return;
    }
    
    // Store receiver MAC for ACK response
    memcpy(receiver_mac_, msg.mac, 6);
    
    LOG_INFO("[MQTT_CFG] Received MQTT config request from receiver");
    
    // Send current configuration as ACK
    send_mqtt_config_ack(true, "Current configuration");
}

void EspnowMessageHandler::handle_mqtt_config_update(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(mqtt_config_update_t)) {
        LOG_ERROR("[MQTT_CFG] Invalid message size: %d bytes", msg.len);
        return;
    }
    
    const mqtt_config_update_t* config = reinterpret_cast<const mqtt_config_update_t*>(msg.data);
    
    // Store receiver MAC for ACK response
    memcpy(receiver_mac_, msg.mac, 6);
    
    LOG_INFO("[MQTT_CFG] Received MQTT config update:");
    LOG_INFO("[MQTT_CFG]   Enabled: %s", config->enabled ? "YES" : "NO");
    LOG_INFO("[MQTT_CFG]   Server: %d.%d.%d.%d:%d", 
             config->server[0], config->server[1], config->server[2], config->server[3],
             config->port);
    LOG_INFO("[MQTT_CFG]   Username: %s", strlen(config->username) > 0 ? config->username : "(none)");
    LOG_INFO("[MQTT_CFG]   Client ID: %s", config->client_id);
    
    // Validate configuration
    if (config->enabled) {
        // Check server IP is not 0.0.0.0 or 255.255.255.255
        if ((config->server[0] == 0 && config->server[1] == 0 && 
             config->server[2] == 0 && config->server[3] == 0) ||
            (config->server[0] == 255 && config->server[1] == 255 && 
             config->server[2] == 255 && config->server[3] == 255)) {
            LOG_ERROR("[MQTT_CFG] Invalid MQTT server address");
            send_mqtt_config_ack(false, "Invalid server IP");
            return;
        }
        
        // Validate port
        if (config->port < 1 || config->port > 65535) {
            LOG_ERROR("[MQTT_CFG] Invalid port: %d", config->port);
            send_mqtt_config_ack(false, "Invalid port number");
            return;
        }
        
        // Check client ID is not empty
        if (strlen(config->client_id) == 0) {
            LOG_ERROR("[MQTT_CFG] Client ID cannot be empty");
            send_mqtt_config_ack(false, "Client ID required");
            return;
        }
    }
    
    // Save configuration to NVS
    IPAddress server(config->server[0], config->server[1], config->server[2], config->server[3]);
    if (MqttConfigManager::saveConfig(config->enabled, server, config->port,
                                config->username, config->password, config->client_id)) {
        LOG_INFO("[MQTT_CFG] ✓ Configuration saved to NVS");
        
        // Apply configuration with hot-reload
        LOG_INFO("[MQTT_CFG] Applying configuration (hot-reload)");
        MqttConfigManager::applyConfig();
        
        // Wait a moment for connection attempt
        delay(1000);
        
        send_mqtt_config_ack(true, "Config saved and applied");
    } else {
        LOG_ERROR("[MQTT_CFG] ✗ Failed to save configuration");
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
        LOG_WARN("[MQTT_CFG] Receiver not registered as peer, adding now");
        if (!EspnowPeerManager::add_peer(receiver_mac_)) {
            LOG_ERROR("[MQTT_CFG] Failed to add receiver as peer");
            return;
        }
    }
    
    esp_err_t result = esp_now_send(receiver_mac_, (const uint8_t*)&ack, sizeof(ack));
    if (result == ESP_OK) {
        LOG_INFO("[MQTT_CFG] ✓ ACK sent to receiver (success=%d, connected=%d)", 
                 ack.success, ack.connected);
    } else {
        LOG_ERROR("[MQTT_CFG] ✗ Failed to send ACK: %s", esp_err_to_name(result));
    }
}

