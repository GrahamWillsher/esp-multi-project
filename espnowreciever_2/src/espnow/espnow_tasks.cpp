#include "espnow_tasks.h"
#include "battery_handlers.h"  // Phase 1: Battery Emulator data handlers
#include "battery_settings_cache.h"  // Phase 2: Settings version tracking
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

// Phase 2: Settings message handlers
void handle_settings_update_ack(const espnow_queue_msg_t* msg);
void handle_settings_changed(const espnow_queue_msg_t* msg);
static void request_category_refresh(const uint8_t* mac, uint8_t category, const char* reason);

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

// Static initialization flag (shared between setup_message_routes and reset function)
static bool* g_initialization_sent_ptr = nullptr;

// Helper function to reset initialization flag when connection is lost
void reset_initialization_flag() {
    if (g_initialization_sent_ptr) {
        *g_initialization_sent_ptr = false;
        LOG_DEBUG("[INIT] Initialization flag reset for reconnection");
    }
}

void setup_message_routes() {
    auto& router = EspnowMessageRouter::instance();
    
    // Track if initialization messages have been sent to avoid redundant requests
    static bool initialization_sent = false;
    g_initialization_sent_ptr = &initialization_sent;  // Store pointer for reset function
    
    // Setup PROBE handler configuration
    probe_config.send_ack_response = true;
    probe_config.connection_flag = &ESPNow::transmitter_connected;
    probe_config.peer_mac_storage = nullptr;  // We use register_transmitter_mac instead
    
    // Called every time a PROBE is received (transmitter announcing itself)
    // Only send initialization requests ONCE per connection to avoid flooding
    probe_config.on_probe_received = [&initialization_sent](const uint8_t* mac, uint32_t seq) {
        LOG_DEBUG("PROBE received (seq=%u)", seq);
        
        // Always store transmitter MAC
        memcpy(ESPNow::transmitter_mac, mac, 6);
        TransmitterManager::registerMAC(mac);
        
        // Only send initialization requests once per connection
        if (!initialization_sent) {
            LOG_INFO("[INIT] First PROBE - sending initialization requests");
            
            // Request full configuration snapshot from transmitter
            ReceiverConfigManager::instance().requestFullSnapshot(mac);
            
            // Request static data (IP address, settings, etc.)
            request_data_t static_req = { msg_request_data, subtype_settings };
            esp_err_t static_result = esp_now_send(mac, (const uint8_t*)&static_req, sizeof(static_req));
            if (static_result == ESP_OK) {
                LOG_INFO("Requested static data (settings) from transmitter");
            } else {
                LOG_WARN("Failed to request static data: %s", esp_err_to_name(static_result));
            }
            
            // Send REQUEST_DATA to ensure power profile stream is active
            request_data_t req_msg = { msg_request_data, subtype_power_profile };
            esp_err_t result = esp_now_send(mac, (const uint8_t*)&req_msg, sizeof(req_msg));
            if (result == ESP_OK) {
                LOG_INFO("Requested power profile data stream");
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
                LOG_INFO("Sent version info to transmitter: %d.%d.%d", 
                         FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
            }
            
            // Mark initialization as complete
            initialization_sent = true;
            LOG_INFO("[INIT] Initialization requests sent - subsequent PROBEs will be ignored");
        }
    };
    
    // Called only when connection state changes (first connection)
    probe_config.on_connection = [](const uint8_t* mac, bool connected) {
        LOG_INFO("Transmitter connected via PROBE");
        // Note: Config requests are already sent in on_probe_received callback
        // No need to duplicate them here
    };
    
    // Setup ACK handler configuration (receiver sends ACKs, doesn't usually receive them)
    ack_config.connection_flag = &ESPNow::transmitter_connected;
    ack_config.peer_mac_storage = nullptr;
    ack_config.expected_seq = nullptr;      // Receiver doesn't validate ACK sequences
    ack_config.lock_channel = nullptr;      // Receiver doesn't change channels
    ack_config.ack_received_flag = nullptr;
    ack_config.set_wifi_channel = false;    // Receiver stays on its configured channel
    ack_config.on_connection = [&initialization_sent](const uint8_t* mac, bool connected) {
        // Store transmitter MAC for sending control messages
        if (connected) {
            memcpy(ESPNow::transmitter_mac, mac, 6);
            TransmitterManager::registerMAC(mac);
            LOG_INFO("Transmitter MAC registered: %02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            
            // Only request metadata once per connection
            if (!initialization_sent) {
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
    // PHASE 2: Settings Bidirectional Flow - Register handlers
    // =========================================================================
    
    router.register_route(msg_settings_update_ack,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_settings_update_ack(msg);
        },
        0xFF, nullptr);
    
    router.register_route(msg_settings_changed,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_settings_changed(msg);
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
    
    // V2: Legacy version message handlers removed (msg_version_announce, msg_version_request, msg_version_response)
    // Only metadata-based version exchange used in v2
    
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
    
    // Register network config ACK handler
    router.register_route(msg_network_config_ack,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            if (msg->len >= (int)sizeof(network_config_ack_t)) {
                const network_config_ack_t* ack = reinterpret_cast<const network_config_ack_t*>(msg->data);
                
                LOG_INFO("[NET_CFG] Received ACK: %s (success=%d)", ack->message, ack->success);
                LOG_INFO("[NET_CFG]   Mode: %s, Version: %u",
                         ack->use_static_ip ? "Static" : "DHCP",
                         ack->config_version);
                LOG_INFO("[NET_CFG]   Current: %d.%d.%d.%d / %d.%d.%d.%d / %d.%d.%d.%d",
                         ack->current_ip[0], ack->current_ip[1], ack->current_ip[2], ack->current_ip[3],
                         ack->current_gateway[0], ack->current_gateway[1], ack->current_gateway[2], ack->current_gateway[3],
                         ack->current_subnet[0], ack->current_subnet[1], ack->current_subnet[2], ack->current_subnet[3]);
                LOG_INFO("[NET_CFG]   Static: %d.%d.%d.%d / %d.%d.%d.%d / %d.%d.%d.%d",
                         ack->static_ip[0], ack->static_ip[1], ack->static_ip[2], ack->static_ip[3],
                         ack->static_gateway[0], ack->static_gateway[1], ack->static_gateway[2], ack->static_gateway[3],
                         ack->static_subnet[0], ack->static_subnet[1], ack->static_subnet[2], ack->static_subnet[3]);
                
                // Store complete network configuration (current + static)
                TransmitterManager::storeNetworkConfig(
                    ack->current_ip, ack->current_gateway, ack->current_subnet,
                    ack->static_ip, ack->static_gateway, ack->static_subnet,
                    ack->static_dns_primary, ack->static_dns_secondary,
                    ack->use_static_ip != 0, ack->config_version);
            }
        },
        0xFF, nullptr);
    
    // Register MQTT config ACK handler
    router.register_route(msg_mqtt_config_ack,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            if (msg->len >= (int)sizeof(mqtt_config_ack_t)) {
                const mqtt_config_ack_t* ack = reinterpret_cast<const mqtt_config_ack_t*>(msg->data);
                
                LOG_INFO("[MQTT_CFG] Received ACK: %s (success=%d)", ack->message, ack->success);
                LOG_INFO("[MQTT_CFG]   Enabled: %s, Connected: %s",
                         ack->enabled ? "YES" : "NO",
                         ack->connected ? "YES" : "NO");
                LOG_INFO("[MQTT_CFG]   Server: %d.%d.%d.%d:%d",
                         ack->server[0], ack->server[1], ack->server[2], ack->server[3],
                         ack->port);
                LOG_INFO("[MQTT_CFG]   Client ID: %s, Version: %u",
                         ack->client_id, ack->config_version);
                
                // Store MQTT configuration in TransmitterManager cache
                TransmitterManager::storeMqttConfig(
                    ack->enabled != 0,
                    ack->server,
                    ack->port,
                    ack->username,
                    ack->password,
                    ack->client_id,
                    ack->connected != 0,
                    ack->config_version);
            }
        },
        0xFF, nullptr);
    
    // =========================================================================
    // SECTION 11: Transmitter-Active Architecture
    // =========================================================================
    
    // Heartbeat handler (10s interval keep-alive)
    router.register_route(msg_heartbeat,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            // Heartbeat received from transmitter - respond to confirm we're alive
            LOG_DEBUG("[HEARTBEAT] Received from transmitter, sending response");
            
            // Echo the heartbeat back to the transmitter
            heartbeat_t response;
            response.type = msg_heartbeat;
            response.timestamp = millis();
            response.seq = 0;  // Response sequence (not tracked yet)
            
            esp_err_t result = esp_now_send(msg->mac, (const uint8_t*)&response, sizeof(response));
            if (result != ESP_OK) {
                LOG_ERROR("[HEARTBEAT] Failed to send response: %s", esp_err_to_name(result));
            }
        },
        0xFF, nullptr);
    
    // =========================================================================
    // PHASE 4: Version-Based Cache Synchronization
    // =========================================================================
    
    // Version beacon handler (transmitter → receiver every 15s)
    router.register_route(msg_version_beacon,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            if (msg->len >= (int)sizeof(version_beacon_t)) {
                const version_beacon_t* beacon = reinterpret_cast<const version_beacon_t*>(msg->data);
                
                LOG_DEBUG("[VERSION_BEACON] Received: MQTT:v%u, Net:v%u, Batt:v%u, Profile:v%u (MQTT:%s, ETH:%s)",
                         beacon->mqtt_config_version,
                         beacon->network_config_version,
                         beacon->battery_settings_version,
                         beacon->power_profile_version,
                         beacon->mqtt_connected ? "CONN" : "DISC",
                         beacon->ethernet_connected ? "UP" : "DOWN");
                
                // Update runtime status (MQTT connected, Ethernet connected)
                TransmitterManager::updateRuntimeStatus(
                    beacon->mqtt_connected,
                    beacon->ethernet_connected
                );
                
                // Check each config section version against our cache
                bool request_sent = false;
                
                // Check MQTT config version
                // Request if: 1) Never received, OR 2) Version mismatch
                bool need_mqtt_update = !TransmitterManager::isMqttConfigKnown();
                if (!need_mqtt_update) {
                    uint32_t cached_version = TransmitterManager::getMqttConfigVersion();
                    need_mqtt_update = (cached_version != beacon->mqtt_config_version);
                }
                
                if (need_mqtt_update) {
                    LOG_INFO("[VERSION_BEACON] MQTT config %s: cached v%u, beacon v%u - requesting update",
                             TransmitterManager::isMqttConfigKnown() ? "stale" : "unknown",
                             TransmitterManager::isMqttConfigKnown() ? TransmitterManager::getMqttConfigVersion() : 0,
                             beacon->mqtt_config_version);
                    
                    // Send config request for MQTT section
                    config_section_request_t request;
                    request.type = msg_config_section_request;
                    request.section = config_section_mqtt;
                    request.requested_version = beacon->mqtt_config_version;
                    
                    esp_err_t result = esp_now_send(msg->mac, (const uint8_t*)&request, sizeof(request));
                    if (result == ESP_OK) {
                        LOG_INFO("[VERSION_BEACON] Sent MQTT config request (v%u)", beacon->mqtt_config_version);
                    } else {
                        LOG_ERROR("[VERSION_BEACON] Failed to send MQTT config request: %s", esp_err_to_name(result));
                    }
                    request_sent = true;
                }
                
                // Check network config version
                // Request if: 1) Never received, OR 2) Version mismatch
                bool need_network_update = !TransmitterManager::isIPKnown();
                if (!need_network_update) {
                    uint32_t cached_version = TransmitterManager::getNetworkConfigVersion();
                    need_network_update = (cached_version != beacon->network_config_version);
                }
                
                if (need_network_update) {
                    LOG_INFO("[VERSION_BEACON] Network config %s: cached v%u, beacon v%u - requesting update",
                             TransmitterManager::isIPKnown() ? "stale" : "unknown",
                             TransmitterManager::isIPKnown() ? TransmitterManager::getNetworkConfigVersion() : 0,
                             beacon->network_config_version);
                    
                    // Send config request for Network section
                    config_section_request_t request;
                    request.type = msg_config_section_request;
                    request.section = config_section_network;
                    request.requested_version = beacon->network_config_version;
                    
                    esp_err_t result = esp_now_send(msg->mac, (const uint8_t*)&request, sizeof(request));
                    if (result == ESP_OK) {
                        LOG_INFO("[VERSION_BEACON] Sent Network config request (v%u)", beacon->network_config_version);
                    } else {
                        LOG_ERROR("[VERSION_BEACON] Failed to send Network config request: %s", esp_err_to_name(result));
                    }
                    request_sent = true;
                }
                
                // Battery and power profile checks would go here when implemented
                
                if (!request_sent) {
                    LOG_DEBUG("[VERSION_BEACON] All cached configs up-to-date");
                }
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
    
    // Initialize battery settings cache (Phase 2: Version tracking)
    BatterySettingsCache::instance().init();
    LOG_INFO("Battery settings cache initialized (version=%u)", 
             BatterySettingsCache::instance().get_version());
    
    // NOTE: Message routes are now initialized in setup() BEFORE this task starts
    // This prevents race condition where PROBE messages arrive before handlers are registered
    
    auto& router = EspnowMessageRouter::instance();
    espnow_queue_msg_t queue_msg;
    
    // Connection watchdog state
    static ConnectionState transmitter_state = { false, 0 };
    constexpr uint32_t CONNECTION_TIMEOUT_MS = 30000;  // 30 seconds
    
    for (;;) {
        // Check for messages with short timeout to allow periodic connection check
        if (xQueueReceive(ESPNow::queue, &queue_msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (queue_msg.len < 1) continue;
            
            // Update connection watchdog on any received message
            transmitter_state.last_rx_time_ms = millis();
            if (!transmitter_state.is_connected) {
                transmitter_state.is_connected = true;
                LOG_INFO("[WATCHDOG] Transmitter connection established");
            }
            
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
        
        // Check for connection timeout (runs every second when queue empty)
        if (transmitter_state.is_connected) {
            if (millis() - transmitter_state.last_rx_time_ms > CONNECTION_TIMEOUT_MS) {
                transmitter_state.is_connected = false;
                ESPNow::transmitter_connected = false;
                
                // Reset initialization flag to allow re-initialization on reconnection
                // This is accessed via the static variable in setup_message_routes()
                extern void reset_initialization_flag();  // Forward declaration
                reset_initialization_flag();
                
                LOG_WARN("[WATCHDOG] Transmitter connection lost (timeout: %u ms)", CONNECTION_TIMEOUT_MS);
                
                // Restart discovery task to reconnect
                if (EspnowDiscovery::instance().is_running()) {
                    LOG_INFO("[WATCHDOG] Resuming discovery announcements to reconnect");
                    EspnowDiscovery::instance().resume();
                } else {
                    LOG_INFO("[WATCHDOG] Restarting discovery task to reconnect");
                    EspnowDiscovery::instance().start(
                        []() -> bool {
                            return ESPNow::transmitter_connected;
                        },
                        5000,  // 5 second interval
                        1,     // Low priority
                        4096   // Stack size
                    );
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
    
    // Parse IP data (bytes 0-11)
    if (info.payload_len >= 12) {
        extern void store_transmitter_ip_data(const uint8_t* ip, const uint8_t* gateway, const uint8_t* subnet);
        
        const uint8_t* ip = &info.payload[0];
        const uint8_t* gateway = &info.payload[4];
        const uint8_t* subnet = &info.payload[8];
        
        store_transmitter_ip_data(ip, gateway, subnet);
        
        LOG_DEBUG("IP: %d.%d.%d.%d, GW: %d.%d.%d.%d, Subnet: %d.%d.%d.%d",
                     ip[0], ip[1], ip[2], ip[3],
                     gateway[0], gateway[1], gateway[2], gateway[3],
                     subnet[0], subnet[1], subnet[2], subnet[3]);
    }
    
    // Note: Battery settings now come via separate msg_battery_info message
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

// ============================================================================
// PHASE 2: Settings Synchronization Handlers
// ============================================================================

void handle_settings_update_ack(const espnow_queue_msg_t* msg) {
    if (msg->len < sizeof(settings_update_ack_msg_t)) {
        LOG_WARN("Settings update ACK message too short: %d bytes", msg->len);
        return;
    }
    
    const settings_update_ack_msg_t* ack = reinterpret_cast<const settings_update_ack_msg_t*>(msg->data);
    
    const char* category_str = (ack->category == SETTINGS_BATTERY) ? "BATTERY" : 
                               (ack->category == SETTINGS_CHARGER) ? "CHARGER" : 
                               (ack->category == SETTINGS_INVERTER) ? "INVERTER" : "UNKNOWN";
    
    if (ack->success) {
        LOG_INFO("✓ Settings update ACK: category=%s, field=%d, version=%u", 
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
        LOG_ERROR("✗ Settings update FAILED: category=%s, field=%d, error=%s", 
                  category_str, ack->field_id, ack->error_msg);
        
        // On failure, request current settings to ensure we're in sync with transmitter
        request_category_refresh(msg->mac, ack->category, "to verify state after failure");
    }
}

// Helper function to request refresh of a specific settings category
// This ensures we only refresh what changed, not all settings
static void request_category_refresh(const uint8_t* mac, uint8_t category, const char* reason) {
    esp_err_t result = ESP_OK;
    
    switch (category) {
        case SETTINGS_BATTERY:
            // Request battery settings only (transmitter sends battery_settings_full_msg_t)
            LOG_INFO("Requesting battery settings refresh %s", reason);
            {
                request_data_t req = { msg_request_data, subtype_battery_config };
                result = esp_now_send(mac, (const uint8_t*)&req, sizeof(req));
            }
            break;
            
        case SETTINGS_CHARGER:
            // TODO Phase 3: Request charger settings only
            // request_data_t req = { msg_request_data, subtype_charger_config };
            LOG_WARN("Charger settings refresh not yet implemented");
            break;
            
        case SETTINGS_INVERTER:
            // TODO Phase 3: Request inverter settings only
            // request_data_t req = { msg_request_data, subtype_inverter };
            LOG_WARN("Inverter settings refresh not yet implemented");
            break;
            
        case SETTINGS_SYSTEM:
            // TODO Phase 3: Request system settings only
            // request_data_t req = { msg_request_data, subtype_system };
            LOG_WARN("System settings refresh not yet implemented");
            break;
            
        default:
            LOG_ERROR("Unknown settings category: %d", category);
            return;
    }
    
    if (result != ESP_OK) {
        LOG_WARN("Failed to request category %d refresh: %s", category, esp_err_to_name(result));
    }
}

void handle_settings_changed(const espnow_queue_msg_t* msg) {
    if (msg->len < sizeof(settings_changed_msg_t)) {
        LOG_WARN("Settings changed message too short: %d bytes", msg->len);
        return;
    }
    
    const settings_changed_msg_t* change = reinterpret_cast<const settings_changed_msg_t*>(msg->data);
    
    // Verify checksum using common utility
    if (!EspnowPacketUtils::verify_message_checksum(change)) {
        uint16_t calc_checksum = EspnowPacketUtils::calculate_message_checksum(change);
        LOG_WARN("Settings changed checksum mismatch: calc=%u, recv=%u", calc_checksum, change->checksum);
        return;
    }
    
    const char* category_str = (change->category == SETTINGS_BATTERY) ? "BATTERY" : 
                               (change->category == SETTINGS_CHARGER) ? "CHARGER" : 
                               (change->category == SETTINGS_INVERTER) ? "INVERTER" : "UNKNOWN";
    
    LOG_INFO("⚡ Settings changed notification: category=%s, new_version=%u", category_str, change->new_version);
    
    // Update our local version number to match transmitter
    // Note: Individual field updates are already handled via msg_battery_settings_update ACK
    // We don't need to request full settings - just update the version
    BatterySettingsCache::instance().mark_updated(change->new_version);
    LOG_DEBUG("Version updated to %u (no full settings refresh needed)", change->new_version);
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
