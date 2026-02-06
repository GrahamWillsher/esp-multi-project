#include "espnow_tasks.h"
#include "battery_handlers.h"  // Phase 1: Battery Emulator data handlers
#include "../common.h"
#include "../display/display_core.h"
#include "../display/display_led.h"
#include "../config/config_receiver.h"
#include "../../lib/webserver/utils/transmitter_manager.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <espnow_common.h>
#include <espnow_peer_manager.h>
#include <espnow_discovery.h>
#include <espnow_message_router.h>
#include <espnow_standard_handlers.h>
#include <espnow_packet_utils.h>
#include <firmware_version.h>

extern void notify_sse_data_updated();

// Forward declarations for handler functions
void handle_data_message(const espnow_queue_msg_t* msg);
void handle_flash_led_message(const espnow_queue_msg_t* msg);
void handle_debug_ack_message(const espnow_queue_msg_t* msg);
void handle_packet_settings(const espnow_queue_msg_t* msg);
void handle_packet_events(const espnow_queue_msg_t* msg);
void handle_packet_logs(const espnow_queue_msg_t* msg);
void handle_packet_cell_info(const espnow_queue_msg_t* msg);
void handle_packet_unknown(const espnow_queue_msg_t* msg, uint8_t subtype);

// ═══════════════════════════════════════════════════════════════════════
// MESSAGE HANDLER CONFIGURATIONS
// ═══════════════════════════════════════════════════════════════════════

// Configuration for standard PROBE handler
EspnowStandardHandlers::ProbeHandlerConfig probe_config;

// Configuration for standard ACK handler  
EspnowStandardHandlers::AckHandlerConfig ack_config;

// ═══════════════════════════════════════════════════════════════════════
// MESSAGE ROUTER SETUP
// ═══════════════════════════════════════════════════════════════════════

void setup_message_routes() {
    auto& router = EspnowMessageRouter::instance();
    
    // Setup PROBE handler configuration
    probe_config.send_ack_response = true;
    probe_config.connection_flag = &ESPNow::transmitter_connected;
    probe_config.peer_mac_storage = nullptr;  // We use register_transmitter_mac instead
    
    // Called every time a PROBE is received (transmitter announcing itself)
    // This happens on transmitter startup, so request fresh config data
    probe_config.on_probe_received = [](const uint8_t* mac, uint32_t seq) {
        LOG_DEBUG("PROBE received (seq=%u) - requesting config update", seq);
        
        // Store transmitter MAC
        memcpy(ESPNow::transmitter_mac, mac, 6);
        TransmitterManager::registerMAC(mac);
        
        // Request full configuration snapshot from transmitter
        // (transmitter may have rebooted with new config)
        ReceiverConfigManager::instance().requestFullSnapshot(mac);
        
        // Send REQUEST_DATA to ensure power profile stream is active
        request_data_t req_msg = { msg_request_data, subtype_power_profile };
        esp_err_t result = esp_now_send(mac, (const uint8_t*)&req_msg, sizeof(req_msg));
        if (result == ESP_OK) {
            LOG_DEBUG("Requested power profile data stream");
        }
        
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
        
        result = esp_now_send(mac, (const uint8_t*)&announce, sizeof(announce));
        if (result == ESP_OK) {
            LOG_INFO("Sent version info to transmitter: v%d.%d.%d", 
                     FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
        }
    };
    
    // Called only when connection state changes (first connection)
    probe_config.on_connection = [](const uint8_t* mac, bool connected) {
        LOG_INFO("Transmitter connected via PROBE");
        
        // Request full config snapshot from transmitter
        if (connected) {
            ReceiverConfigManager::instance().requestFullSnapshot(mac);
        }
    };
    
    // Setup ACK handler configuration (receiver sends ACKs, doesn't usually receive them)
    ack_config.connection_flag = &ESPNow::transmitter_connected;
    ack_config.peer_mac_storage = nullptr;
    ack_config.expected_seq = nullptr;      // Receiver doesn't validate ACK sequences
    ack_config.lock_channel = nullptr;      // Receiver doesn't change channels
    ack_config.ack_received_flag = nullptr;
    ack_config.set_wifi_channel = false;    // Receiver stays on its configured channel
    ack_config.on_connection = [](const uint8_t* mac, bool connected) {
        // Store transmitter MAC for sending control messages
        if (connected) {
            memcpy(ESPNow::transmitter_mac, mac, 6);
            TransmitterManager::registerMAC(mac);
            LOG_INFO("Transmitter MAC registered: %02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            
            // Request full configuration snapshot from transmitter
            ReceiverConfigManager::instance().requestFullSnapshot(mac);
            LOG_INFO("Requested full configuration snapshot from transmitter");
            
            // Request firmware metadata from transmitter
            metadata_request_t meta_req;
            meta_req.type = msg_metadata_request;
            meta_req.request_id = esp_random();
            esp_err_t meta_result = esp_now_send(mac, (const uint8_t*)&meta_req, sizeof(meta_req));
            if (meta_result == ESP_OK) {
                LOG_INFO("Requested firmware metadata from transmitter");
            } else {
                LOG_WARN("Failed to send METADATA_REQUEST: %s", esp_err_to_name(meta_result));
            }
            
            // Send REQUEST_DATA to start receiving battery data
            request_data_t req_msg = { msg_request_data, subtype_power_profile };
            esp_err_t result = esp_now_send(mac, (const uint8_t*)&req_msg, sizeof(req_msg));
            if (result == ESP_OK) {
                LOG_INFO("Sent REQUEST_DATA for power profile data stream");
            } else {
                LOG_WARN("Failed to send REQUEST_DATA: %s", esp_err_to_name(result));
            }
        }
        LOG_INFO("Transmitter connected via ACK");
    };
    
    // Register standard message handlers
    router.register_route(msg_probe, 
        [](const espnow_queue_msg_t* msg, void* ctx) {
            EspnowStandardHandlers::handle_probe(msg, &probe_config);
        }, 
        0xFF, nullptr);
    
    router.register_route(msg_ack,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            EspnowStandardHandlers::handle_ack(msg, &ack_config);
        },
        0xFF, nullptr);
    
    // Register custom DATA handler
    router.register_route(msg_data,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_data_message(msg);
        },
        0xFF, nullptr);
    
    // Register flash LED handler
    router.register_route(msg_flash_led,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_flash_led_message(msg);
        },
        0xFF, nullptr);
    
    // Register debug acknowledgment handler
    router.register_route(msg_debug_ack,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_debug_ack_message(msg);
        },
        0xFF, nullptr);
    
    // =========================================================================
    // PHASE 1: Register Battery Emulator data message handlers
    // =========================================================================
    
    router.register_route(msg_battery_status,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_battery_status(msg);
        },
        0xFF, nullptr);
    
    router.register_route(msg_battery_info,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_battery_info(msg);
        },
        0xFF, nullptr);
    
    router.register_route(msg_charger_status,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_charger_status(msg);
        },
        0xFF, nullptr);
    
    router.register_route(msg_inverter_status,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_inverter_status(msg);
        },
        0xFF, nullptr);
    
    router.register_route(msg_system_status,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_system_status(msg);
        },
        0xFF, nullptr);
    
    // =========================================================================
    
    // Register packet handlers with subtypes
    router.register_route(msg_packet,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_packet_settings(msg);
        },
        subtype_settings, nullptr);
    
    router.register_route(msg_packet,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_packet_events(msg);
        },
        subtype_events, nullptr);
    
    router.register_route(msg_packet,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_packet_logs(msg);
        },
        subtype_logs, nullptr);
    
    router.register_route(msg_packet,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_packet_cell_info(msg);
        },
        subtype_cell_info, nullptr);
    
    // Register configuration message handlers
    router.register_route(msg_config_snapshot,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            ReceiverConfigManager::instance().onSnapshotReceived(msg->mac, msg->data, msg->len);
        },
        0xFF, nullptr);
    
    router.register_route(msg_config_update_delta,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            ReceiverConfigManager::instance().onDeltaUpdateReceived(msg->mac, msg->data, msg->len);
        },
        0xFF, nullptr);
    
    // Register version exchange message handlers
    router.register_route(msg_version_announce,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            LOG_INFO("=== RECEIVED VERSION_ANNOUNCE ===");
            LOG_INFO("  Message length: %d bytes (expected: %d)", msg->len, sizeof(version_announce_t));
            
            if (msg->len >= (int)sizeof(version_announce_t)) {
                const version_announce_t* announce = reinterpret_cast<const version_announce_t*>(msg->data);
                
                LOG_INFO("  Message Type: %d", announce->type);
                LOG_INFO("  Firmware Version: %u", announce->firmware_version);
                LOG_INFO("  Protocol Version: %u", announce->protocol_version);
                LOG_INFO("  Min Compatible: %u", announce->min_compatible_version);
                LOG_INFO("  Device Type: '%s'", announce->device_type);
                LOG_INFO("  Build Date: '%s'", announce->build_date);
                LOG_INFO("  Build Time: '%s'", announce->build_time);
                LOG_INFO("  Uptime: %u seconds", announce->uptime_seconds);
                
                TransmitterManager::storeFirmwareVersion(announce->firmware_version, 
                                                        announce->build_date, 
                                                        announce->build_time);
                
                uint8_t tx_major = (announce->firmware_version / 10000);
                uint8_t tx_minor = (announce->firmware_version / 100) % 100;
                uint8_t tx_patch = announce->firmware_version % 100;
                
                LOG_INFO("  Parsed: v%d.%d.%d (stored in TransmitterManager)", tx_major, tx_minor, tx_patch);
                
                if (!isVersionCompatible(announce->firmware_version)) {
                    LOG_WARN("⚠ Version incompatible: receiver v%d.%d.%d, transmitter v%d.%d.%d",
                             FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH,
                             tx_major, tx_minor, tx_patch);
                }
                LOG_INFO("=================================");
            } else {
                LOG_WARN("✗ VERSION_ANNOUNCE too short: %d bytes (expected %d)", 
                         msg->len, sizeof(version_announce_t));
            }
        },
        0xFF, nullptr);
    
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
                    LOG_DEBUG("Sent VERSION_RESPONSE to transmitter");
                } else {
                    LOG_ERROR("Failed to send VERSION_RESPONSE: %s", esp_err_to_name(result));
                }
            }
        },
        0xFF, nullptr);
    
    router.register_route(msg_version_response,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            if (msg->len >= (int)sizeof(version_response_t)) {
                const version_response_t* response = reinterpret_cast<const version_response_t*>(msg->data);
                TransmitterManager::storeFirmwareVersion(response->firmware_version);
                LOG_DEBUG("Received VERSION_RESPONSE: %s v%d.%d.%d",
                         response->device_type,
                         response->firmware_version / 10000,
                         (response->firmware_version / 100) % 100,
                         response->firmware_version % 100);
            }
        },
        0xFF, nullptr);
    
    // Register metadata response handler
    router.register_route(msg_metadata_response,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            if (msg->len >= (int)sizeof(metadata_response_t)) {
                const metadata_response_t* response = reinterpret_cast<const metadata_response_t*>(msg->data);
                
                char indicator = response->valid ? '@' : '*';
                LOG_INFO("=== RECEIVED METADATA_RESPONSE ===");
                LOG_INFO("  Request ID: %u", response->request_id);
                LOG_INFO("  Valid: %s %c", response->valid ? "true" : "false", indicator);
                LOG_INFO("  Device: %s", response->device_type);
                LOG_INFO("  Env: %s", response->env_name);
                LOG_INFO("  Version: v%d.%d.%d", response->version_major, response->version_minor, response->version_patch);
                LOG_INFO("  Built: %s", response->build_date);
                LOG_INFO("==================================");
                
                // Store in TransmitterManager
                TransmitterManager::storeMetadata(
                    response->valid,
                    response->env_name,
                    response->device_type,
                    response->version_major,
                    response->version_minor,
                    response->version_patch,
                    response->build_date
                );
            } else {
                LOG_WARN("METADATA_RESPONSE too short: %d bytes", msg->len);
            }
        },
        0xFF, nullptr);
    
    LOG_DEBUG("Registered %d message routes", router.route_count());
}

// Note: task_periodic_announcement removed - EspnowDiscovery::start() is now called directly from setup()
// since it creates its own internal task. The old wrapper task was causing the callback lambda to be
// destroyed while the internal task was still running, leading to crashes.

void task_espnow_worker(void *parameter) {
    LOG_DEBUG("ESP-NOW Worker task started");
    
    // Setup message routes on first run
    static bool routes_initialized = false;
    if (!routes_initialized) {
        setup_message_routes();
        routes_initialized = true;
    }
    
    auto& router = EspnowMessageRouter::instance();
    espnow_queue_msg_t queue_msg;
    
    for (;;) {
        if (xQueueReceive(ESPNow::queue, &queue_msg, portMAX_DELAY) == pdTRUE) {
            if (queue_msg.len < 1) continue;
            
            // Ensure sender is registered as a peer using common utility
            if (!EspnowPeerManager::is_peer_registered(queue_msg.mac)) {
                EspnowPeerManager::add_peer(queue_msg.mac, 0);
            }
            
            // Route message using common router
            if (!router.route_message(queue_msg)) {
                // Message not handled - check if it's an unknown packet subtype
                uint8_t type = queue_msg.data[0];
                if (type == msg_packet) {
                    EspnowPacketUtils::PacketInfo info;
                    if (EspnowPacketUtils::get_packet_info(&queue_msg, info)) {
                        handle_packet_unknown(&queue_msg, info.subtype);
                    }
                } else {
                    LOG_WARN("[ESP-NOW] Unknown message type: %u, len=%d", type, queue_msg.len);
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
// MESSAGE HANDLER IMPLEMENTATIONS
// ═══════════════════════════════════════════════════════════════════════

// Note: handle_probe_message and handle_ack_message removed - now using
// EspnowStandardHandlers::handle_probe and EspnowStandardHandlers::handle_ack
// from common library with custom configuration callbacks.

void handle_flash_led_message(const espnow_queue_msg_t* msg) {
    if (msg->len >= (int)sizeof(flash_led_t)) {
        const flash_led_t* flash_msg = reinterpret_cast<const flash_led_t*>(msg->data);
        
        // Validate color code (wire format matches enum: 0=RED, 1=GREEN, 2=ORANGE)
        if (flash_msg->color > LED_ORANGE) {
            LOG_WARN("Invalid LED color code: %d", flash_msg->color);
            return;
        }
        
        LEDColor color = static_cast<LEDColor>(flash_msg->color);
        
        static const char* color_names[] = {"RED", "GREEN", "ORANGE"};
        LOG_DEBUG("Flash LED request: color=%d (%s)", flash_msg->color, color_names[flash_msg->color]);
        
        // Store the current LED color for status indicator task to use
        ESPNow::current_led_color = color;
    }
}

void handle_debug_ack_message(const espnow_queue_msg_t* msg) {
    if (msg->len >= (int)sizeof(debug_ack_t)) {
        const debug_ack_t* ack = reinterpret_cast<const debug_ack_t*>(msg->data);
        
        static const char* level_names[] = {"EMERG", "ALERT", "CRIT", "ERROR", "WARNING", "NOTICE", "INFO", "DEBUG"};
        static const char* status_names[] = {"Success", "Invalid level", "Error"};
        
        LOG_INFO("[ESP-NOW] Debug ACK received: applied=%s (%d), previous=%s (%d), status=%s",
                 ack->applied <= 7 ? level_names[ack->applied] : "UNKNOWN",
                 ack->applied,
                 ack->previous <= 7 ? level_names[ack->previous] : "UNKNOWN",
                 ack->previous,
                 ack->status <= 2 ? status_names[ack->status] : "UNKNOWN");
        
        if (ack->status != 0) {
            LOG_WARN("[ESP-NOW] Transmitter reported error changing debug level");
        }
    } else {
        LOG_WARN("[ESP-NOW] Debug ACK packet too short: %d bytes (expected %d)", 
                 msg->len, sizeof(debug_ack_t));
    }
}

void handle_data_message(const espnow_queue_msg_t* msg) {
    if (msg->len >= (int)sizeof(espnow_payload_t)) {
        const espnow_payload_t* payload = reinterpret_cast<const espnow_payload_t*>(msg->data);
        
        uint16_t calc_checksum = payload->soc + (uint16_t)payload->power;
        
        if (calc_checksum == payload->checksum) {
            // Update global variables with dirty flags
            if (ESPNow::received_soc != payload->soc) {
                ESPNow::received_soc = payload->soc;
                ESPNow::dirty_flags.soc_changed = true;
            }
            if (ESPNow::received_power != payload->power) {
                ESPNow::received_power = payload->power;
                ESPNow::dirty_flags.power_changed = true;
            }
            ESPNow::data_received = true;
            
            // Store transmitter MAC for sending control messages
            memcpy(ESPNow::transmitter_mac, msg->mac, 6);
            notify_sse_data_updated();
            
            // State machine transition
            extern SystemState current_state;
            if (current_state == SystemState::TEST_MODE) {
                transition_to_state(SystemState::NORMAL_OPERATION);
            }
            
            LOG_DEBUG("[ESP-NOW] Valid: SOC=%d%%, Power=%dW (MAC: %02X:%02X:%02X:%02X:%02X:%02X)", 
                         payload->soc, payload->power,
                         msg->mac[0], msg->mac[1], msg->mac[2],
                         msg->mac[3], msg->mac[4], msg->mac[5]);
            
            // Batch display updates in single mutex acquisition to reduce contention
            if (ESPNow::dirty_flags.soc_changed || ESPNow::dirty_flags.power_changed) {
                if (xSemaphoreTake(RTOS::tft_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (ESPNow::dirty_flags.soc_changed) {
                        display_soc((float)payload->soc);
                        ESPNow::dirty_flags.soc_changed = false;
                    }
                    if (ESPNow::dirty_flags.power_changed) {
                        display_power((int32_t)payload->power);
                        ESPNow::dirty_flags.power_changed = false;
                    }
                    xSemaphoreGive(RTOS::tft_mutex);
                }
            }
        } else {
            // CRC mismatch
            LOG_WARN("CRC failed: expected 0x%04X, got 0x%04X", 
                         calc_checksum, payload->checksum);
        }
    }
}

void handle_packet_settings(const espnow_queue_msg_t* msg) {
    EspnowPacketUtils::PacketInfo info;
    if (!EspnowPacketUtils::get_packet_info(msg, info)) {
        LOG_WARN("Invalid packet structure");
        return;
    }
    
    EspnowPacketUtils::print_packet_info(info, "SETTINGS");
    
    if (info.payload_len >= 12) {
        extern void store_transmitter_ip_data(const uint8_t* ip, const uint8_t* gateway, const uint8_t* subnet);
        
        const uint8_t* ip = &info.payload[0];
        const uint8_t* gateway = &info.payload[4];
        const uint8_t* subnet = &info.payload[8];
        
        store_transmitter_ip_data(ip, gateway, subnet);
        
        LOG_DEBUG("Received IP: %d.%d.%d.%d, GW: %d.%d.%d.%d, Subnet: %d.%d.%d.%d",
                     ip[0], ip[1], ip[2], ip[3],
                     gateway[0], gateway[1], gateway[2], gateway[3],
                     subnet[0], subnet[1], subnet[2], subnet[3]);
    }
}

void handle_packet_events(const espnow_queue_msg_t* msg) {
    EspnowPacketUtils::PacketInfo info;
    if (!EspnowPacketUtils::get_packet_info(msg, info)) {
        LOG_WARN("Invalid packet structure");
        return;
    }
    
    EspnowPacketUtils::print_packet_info(info, "EVENTS");
    
    if (info.payload_len >= 5) {
        uint8_t soc = info.payload[0];
        int32_t power;
        memcpy(&power, &info.payload[1], sizeof(int32_t));
        
        if (ESPNow::received_soc != soc) {
            ESPNow::received_soc = soc;
            ESPNow::dirty_flags.soc_changed = true;
        }
        if (ESPNow::received_power != power) {
            ESPNow::received_power = power;
            ESPNow::dirty_flags.power_changed = true;
        }
        ESPNow::data_received = true;
        
        // Store transmitter MAC for sending control messages
        memcpy(ESPNow::transmitter_mac, msg->mac, 6);
        notify_sse_data_updated();
        
        LOG_TRACE("EVENTS: SOC=%d%%, Power=%dW", soc, power);
        
        // Batch display updates in single mutex acquisition to reduce contention
        if (ESPNow::dirty_flags.soc_changed || ESPNow::dirty_flags.power_changed) {
            if (xSemaphoreTake(RTOS::tft_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (ESPNow::dirty_flags.soc_changed) {
                    display_soc((float)soc);
                    ESPNow::dirty_flags.soc_changed = false;
                }
                if (ESPNow::dirty_flags.power_changed) {
                    display_power(power);
                    ESPNow::dirty_flags.power_changed = false;
                }
                xSemaphoreGive(RTOS::tft_mutex);
            }
        }
    }
}

void handle_packet_logs(const espnow_queue_msg_t* msg) {
    EspnowPacketUtils::PacketInfo info;
    if (EspnowPacketUtils::get_packet_info(msg, info)) {
        EspnowPacketUtils::print_packet_info(info, "LOGS");
    }
}

void handle_packet_cell_info(const espnow_queue_msg_t* msg) {
    EspnowPacketUtils::PacketInfo info;
    if (EspnowPacketUtils::get_packet_info(msg, info)) {
        EspnowPacketUtils::print_packet_info(info, "CELL_INFO");
    }
}

void handle_packet_unknown(const espnow_queue_msg_t* msg, uint8_t subtype) {
    EspnowPacketUtils::PacketInfo info;
    if (EspnowPacketUtils::get_packet_info(msg, info)) {
        LOG_WARN("[ESP-NOW] PACKET/UNKNOWN: subtype=%u, seq=%u", 
                     subtype, info.seq);
    }
}
