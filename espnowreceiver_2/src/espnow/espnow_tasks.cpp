#include "espnow_tasks.h"
#include "rx_connection_handler.h"
#include "rx_heartbeat_manager.h"
#include "rx_state_machine.h"
#include "espnow_send.h"
#include "component_apply_tracker.h"
#include "type_catalog_cache.h"
#include "battery_data_store.h"
#include <esp32common/espnow/connection_manager.h>
#include "battery_handlers.h"  // Phase 1: Battery Emulator data handlers
#include "battery_settings_cache.h"  // Phase 2: Settings version tracking
#include "../common.h"
#include "../display/display_update_queue.h"
#include "../display/display_led.h"
#include "../../lib/webserver/utils/transmitter_manager.h"
#include <cstring>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp32common/espnow/common.h>
#include <espnow_peer_manager.h>
#include <espnow_discovery.h>
#include <esp32common/espnow/message_router.h>
#include <esp32common/espnow/standard_handlers.h>
#include <esp32common/espnow/packet_utils.h>
#include <esp32common/config/timing_config.h>
#include <firmware_version.h>

extern void notify_sse_data_updated();

static constexpr const char* kLogTag = "ESPNOW";
static uint32_t g_rx_message_seq = 0;
static bool g_received_data_initialized = false;
static uint8_t g_last_received_soc = 0;
static int32_t g_last_received_power = 0;

void store_transmitter_mac(const uint8_t* mac) {
    if (!mac) {
        return;
    }
    memcpy(ESPNow::transmitter_mac, mac, 6);
}

static void update_received_data_cache(
    uint8_t soc,
    int32_t power,
    uint32_t voltage_mv,
    bool* out_first_data,
    bool* out_soc_changed,
    bool* out_power_changed) {
    const bool first_data = !g_received_data_initialized;
    const bool soc_changed = first_data || (g_last_received_soc != soc);
    const bool power_changed = first_data || (g_last_received_power != power);

    g_last_received_soc = soc;
    g_last_received_power = power;
    BatteryData::update_basic_telemetry(soc, power, voltage_mv);
    g_received_data_initialized = true;

    if (out_first_data) {
        *out_first_data = first_data;
    }
    if (out_soc_changed) {
        *out_soc_changed = soc_changed;
    }
    if (out_power_changed) {
        *out_power_changed = power_changed;
    }
}

static uint32_t estimate_voltage_mv(uint8_t soc) {
    uint32_t min_mv = 30000;
    uint32_t max_mv = 42000;

    if (TransmitterManager::hasBatterySettings()) {
        auto settings = TransmitterManager::getBatterySettings();
        if (settings.min_voltage_mv > 0 && settings.max_voltage_mv > settings.min_voltage_mv) {
            min_mv = settings.min_voltage_mv;
            max_mv = settings.max_voltage_mv;
        }
    }

    uint32_t clamped_soc = (soc > 100) ? 100 : soc;
    return min_mv + ((max_mv - min_mv) * clamped_soc) / 100;
}

static void update_display_if_changed(uint8_t soc, int32_t power, bool soc_changed, bool power_changed) {
    if (!DisplayUpdateQueue::enqueue(soc, power, soc_changed, power_changed)) {
        LOG_WARN(kLogTag, "Display snapshot queue full - latest update may have been dropped");
    }
}

void apply_telemetry_sample(
    const uint8_t* mac,
    uint8_t soc,
    int32_t power,
    const char* source,
    bool include_first_data_log) {
    bool first_data = false;
    bool soc_changed = false;
    bool power_changed = false;

    update_received_data_cache(
        soc,
        power,
        estimate_voltage_mv(soc),
        &first_data,
        &soc_changed,
        &power_changed
    );

    store_transmitter_mac(mac);
    notify_sse_data_updated();

    if (include_first_data_log) {
        LOG_DEBUG(kLogTag, "ESP-NOW RX: SOC=%d%%, Power=%dW (first=%s)",
                  soc, power, first_data ? "YES" : "no");
    } else {
        LOG_TRACE(kLogTag, "%s: SOC=%d%%, Power=%dW", source, soc, power);
    }

    update_display_if_changed(soc, power, soc_changed, power_changed);

    if (include_first_data_log && (soc_changed || power_changed)) {
        LOG_TRACE(kLogTag, "Display updated: SOC=%d%% Power=%dW", soc, power);
    }
}

#include "espnow_tasks_internal.h"

// ═══════════════════════════════════════════════════════════════════════
// MESSAGE HANDLER CONFIGURATIONS
// ═══════════════════════════════════════════════════════════════════════

// Configuration for standard PROBE handler
EspnowStandardHandlers::ProbeHandlerConfig probe_config{};

// Configuration for standard ACK handler  
EspnowStandardHandlers::AckHandlerConfig ack_config{};

// ═══════════════════════════════════════════════════════════════════════
// MESSAGE ROUTER SETUP
// ═══════════════════════════════════════════════════════════════════════

static void request_config_section(const uint8_t* mac, config_section_t section, uint32_t requested_version, const char* label) {
    if (!mac) return;

    config_section_request_t request = {};
    request.type = msg_config_section_request;
    request.section = section;
    request.requested_version = requested_version;

    esp_err_t result = esp_now_send(mac, reinterpret_cast<const uint8_t*>(&request), sizeof(request));
    if (result == ESP_OK) {
        LOG_INFO(kLogTag, "[CONFIG_REQ] Sent %s request (v%u)", label, requested_version);
    } else {
        LOG_ERROR(kLogTag, "[CONFIG_REQ] Failed %s request: %s", label, esp_err_to_name(result));
    }
}

void setup_message_routes() {
    auto& router = EspnowMessageRouter::instance();
    TypeCatalogCache::init();
    probe_config = {};
    ack_config = {};
    
    // Setup PROBE handler configuration
    probe_config.send_ack_response = true;
    probe_config.connection_flag = nullptr;  // Using RxStateMachine for state management
    probe_config.peer_mac_storage = nullptr;  // We use register_transmitter_mac instead
    
    // Called every time a PROBE is received (transmitter announcing itself)
    // Only send initialization requests ONCE per connection to avoid flooding
    probe_config.on_probe_received = [](const uint8_t* mac, uint32_t seq) {
        ReceiverConnectionHandler::instance().on_probe_received(mac);
        
        LOG_DEBUG(kLogTag, "PROBE received (seq=%u)", seq);
        
        // Always store transmitter MAC
        store_transmitter_mac(mac);
        TransmitterManager::registerMAC(mac);
    };
    
    // Connection ownership is managed by the worker/connection handler path.
    probe_config.on_connection = nullptr;
    
    // Setup ACK handler configuration (receiver sends ACKs, doesn't usually receive them)
    ack_config.connection_flag = nullptr;  // Using RxStateMachine for state management
    ack_config.peer_mac_storage = nullptr;
    ack_config.expected_seq = nullptr;      // Receiver doesn't validate ACK sequences
    ack_config.lock_channel = nullptr;      // Receiver doesn't change channels
    ack_config.ack_received_flag = nullptr;
    ack_config.set_wifi_channel = false;    // Receiver stays on its configured channel
    ack_config.on_connection = nullptr;
    
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
            ReceiverConnectionHandler::instance().on_power_data_received();
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
    
    // Register heartbeat handler
    router.register_route(msg_heartbeat,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            if (msg->len >= (int)sizeof(heartbeat_t)) {
                const heartbeat_t* hb = reinterpret_cast<const heartbeat_t*>(msg->data);
                RxHeartbeatManager::instance().on_heartbeat(hb, msg->mac);
            }
        },
        0xFF, nullptr);
    
    // =========================================================================
    // PHASE 1: Register Battery Emulator data message handlers
    // =========================================================================
    
    router.register_route(msg_battery_status,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            // Signal to the retry timer that the data stream is active
            ReceiverConnectionHandler::instance().on_power_data_received();
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
    
    router.register_route(msg_component_config,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_component_config(msg);
        },
        0xFF, nullptr);

    // Dynamic type catalog responses from transmitter
    router.register_route(msg_battery_types_fragment,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            const auto* fragment = reinterpret_cast<const type_catalog_fragment_t*>(msg->data);
            TypeCatalogCache::handle_battery_fragment(fragment, static_cast<size_t>(msg->len));
        },
        0xFF, nullptr);

    router.register_route(msg_inverter_types_fragment,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            const auto* fragment = reinterpret_cast<const type_catalog_fragment_t*>(msg->data);
            TypeCatalogCache::handle_inverter_fragment(fragment, static_cast<size_t>(msg->len));
        },
        0xFF, nullptr);

    router.register_route(msg_inverter_interfaces_fragment,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            const auto* fragment = reinterpret_cast<const type_catalog_fragment_t*>(msg->data);
            TypeCatalogCache::handle_inverter_interface_fragment(fragment, static_cast<size_t>(msg->len));
        },
        0xFF, nullptr);

    router.register_route(msg_type_catalog_versions,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            if (msg->len < (int)sizeof(type_catalog_versions_t)) {
                LOG_WARN("TYPE_CATALOG", "Invalid catalog versions message size: %d", msg->len);
                return;
            }

            ReceiverConnectionHandler::instance().on_type_catalog_versions_received();

            const auto* versions = reinterpret_cast<const type_catalog_versions_t*>(msg->data);
            TypeCatalogCache::update_announced_versions(versions->battery_catalog_version,
                                                        versions->inverter_catalog_version);

            LOG_INFO("TYPE_CATALOG", "Received catalog versions: battery=%u (applied=%u), inverter=%u (applied=%u)",
                     (unsigned)versions->battery_catalog_version,
                     (unsigned)TypeCatalogCache::battery_applied_version(),
                     (unsigned)versions->inverter_catalog_version,
                     (unsigned)TypeCatalogCache::inverter_applied_version());

            if (TypeCatalogCache::battery_refresh_required()) {
                LOG_INFO("TYPE_CATALOG", "Battery catalog refresh required - requesting battery catalog");
                send_battery_types_request();
            }

            if (TypeCatalogCache::inverter_refresh_required()) {
                LOG_INFO("TYPE_CATALOG", "Inverter catalog refresh required - requesting inverter catalog");
                send_inverter_types_request();
            }
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

    router.register_route(msg_component_apply_ack,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_component_apply_ack_message(msg);
        },
        0xFF, nullptr);
    
    // =========================================================================
    
    // Register packet handlers with subtypes
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
    
    // V2: Legacy version message handlers removed (msg_version_announce, msg_version_request, msg_version_response)
    // Only metadata-based version exchange used in v2
    
    // Register metadata response handler
    router.register_route(msg_metadata_response,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            if (msg->len >= (int)sizeof(metadata_response_t)) {
                const metadata_response_t* response = reinterpret_cast<const metadata_response_t*>(msg->data);
                
                char indicator = response->valid ? '@' : '*';
                LOG_INFO(kLogTag, "=== RECEIVED METADATA_RESPONSE ===");
                LOG_INFO(kLogTag, "  Request ID: %u", response->request_id);
                LOG_INFO(kLogTag, "  Valid: %s %c", response->valid ? "true" : "false", indicator);
                LOG_INFO(kLogTag, "  Device: %s", response->device_type);
                LOG_INFO(kLogTag, "  Env: %s", response->env_name);
                LOG_INFO(kLogTag, "  Version: v%d.%d.%d", response->version_major, response->version_minor, response->version_patch);
                LOG_INFO(kLogTag, "  Built: %s", response->build_date);
                LOG_INFO(kLogTag, "==================================");
                
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
                LOG_WARN(kLogTag, "METADATA_RESPONSE too short: %d bytes", msg->len);
            }
        },
        0xFF, nullptr);
    
    // Register network config ACK handler
    router.register_route(msg_network_config_ack,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            if (msg->len >= (int)sizeof(network_config_ack_t)) {
                const network_config_ack_t* ack = reinterpret_cast<const network_config_ack_t*>(msg->data);
                
                LOG_INFO(kLogTag, "[NET_CFG] Received ACK: %s (success=%d)", ack->message, ack->success);
                LOG_INFO(kLogTag, "[NET_CFG]   Mode: %s, Version: %u",
                         ack->use_static_ip ? "Static" : "DHCP",
                         ack->config_version);
                LOG_INFO(kLogTag, "[NET_CFG]   Current: %d.%d.%d.%d / %d.%d.%d.%d / %d.%d.%d.%d",
                         ack->current_ip[0], ack->current_ip[1], ack->current_ip[2], ack->current_ip[3],
                         ack->current_gateway[0], ack->current_gateway[1], ack->current_gateway[2], ack->current_gateway[3],
                         ack->current_subnet[0], ack->current_subnet[1], ack->current_subnet[2], ack->current_subnet[3]);
                LOG_INFO(kLogTag, "[NET_CFG]   Static: %d.%d.%d.%d / %d.%d.%d.%d / %d.%d.%d.%d",
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
                
                LOG_INFO(kLogTag, "[MQTT_CFG] Received ACK: %s (success=%d)", ack->message, ack->success);
                LOG_INFO(kLogTag, "[MQTT_CFG]   Enabled: %s, Connected: %s",
                         ack->enabled ? "YES" : "NO",
                         ack->connected ? "YES" : "NO");
                LOG_INFO(kLogTag, "[MQTT_CFG]   Server: %d.%d.%d.%d:%d",
                         ack->server[0], ack->server[1], ack->server[2], ack->server[3],
                         ack->port);
                LOG_INFO(kLogTag, "[MQTT_CFG]   Client ID: %s, Version: %u",
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
    
    // Heartbeat handler already registered above with RxHeartbeatManager
    // Old heartbeat echo code removed - now using proper ACK-based protocol
    
    // =========================================================================
    // PHASE 4: Version-Based Cache Synchronization
    // =========================================================================
    
    // Version beacon handler (transmitter → receiver every 15s)
    router.register_route(msg_version_beacon,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            if (msg->len >= (int)sizeof(version_beacon_t)) {
                const version_beacon_t* beacon = reinterpret_cast<const version_beacon_t*>(msg->data);
                
                LOG_INFO(kLogTag, "[VERSION_BEACON] Received: MQTT:v%u, Net:v%u, Batt:v%u, Profile:v%u, Meta:v%u (MQTT:%s, ETH:%s)",
                         beacon->mqtt_config_version,
                         beacon->network_config_version,
                         beacon->battery_settings_version,
                         beacon->power_profile_version,
                         beacon->metadata_config_version,
                         beacon->mqtt_connected ? "CONN" : "DISC",
                         beacon->ethernet_connected ? "UP" : "DOWN");
                
                // Update runtime status (MQTT connected, Ethernet connected)
                LOG_INFO(kLogTag, "[VERSION_BEACON] Updating TX runtime status: mqtt_connected=%d → %d",
                         TransmitterManager::isMqttConnected(),
                         beacon->mqtt_connected);
                TransmitterManager::updateRuntimeStatus(
                    beacon->mqtt_connected,
                    beacon->ethernet_connected
                );
                
                // Check each config section version against our cache and request if stale/unknown.
                // Uses the shared request_config_section() helper to avoid repeating the
                // build/send/log pattern for each section.
                bool request_sent = false;

                struct SectionCheck {
                    bool        needs_update;
                    uint32_t    cached_version;
                    uint32_t    beacon_version;
                    config_section_t section;
                    const char* label;
                };

                const uint32_t cached_mqtt_version =
                    TransmitterManager::isMqttConfigKnown() ? TransmitterManager::getMqttConfigVersion() : 0;
                const uint32_t cached_net_version =
                    TransmitterManager::isIPKnown() ? TransmitterManager::getNetworkConfigVersion() : 0;
                const uint32_t cached_batt_version =
                    TransmitterManager::hasBatterySettings() ? BatterySettingsCache::instance().get_version() : 0;

                const SectionCheck checks[] = {
                    {
                        !TransmitterManager::isMqttConfigKnown() ||
                            cached_mqtt_version != beacon->mqtt_config_version,
                        cached_mqtt_version, beacon->mqtt_config_version,
                        config_section_mqtt, "MQTT"
                    },
                    {
                        !TransmitterManager::isIPKnown() ||
                            cached_net_version != beacon->network_config_version,
                        cached_net_version, beacon->network_config_version,
                        config_section_network, "Network"
                    },
                    {
                        !TransmitterManager::hasBatterySettings() ||
                            cached_batt_version != beacon->battery_settings_version,
                        cached_batt_version, beacon->battery_settings_version,
                        config_section_battery, "Battery"
                    },
                };

                for (const auto& chk : checks) {
                    if (chk.needs_update) {
                        LOG_INFO(kLogTag, "[VERSION_BEACON] %s config %s: cached v%u, beacon v%u - requesting update",
                                 chk.label,
                                 chk.cached_version == 0 ? "unknown" : "stale",
                                 chk.cached_version,
                                 chk.beacon_version);
                        request_config_section(msg->mac, chk.section, chk.beacon_version, chk.label);
                        request_sent = true;
                    }
                }

                // Extract metadata directly from beacon (no separate request/response needed)
                // Store in cache with MAC address from sender
                TransmitterManager::storeMetadata(
                    true,  // Always valid if beacon received
                    beacon->env_name,
                    "TRANSMITTER",  // Device type is known
                    beacon->version_major,
                    beacon->version_minor,
                    beacon->version_patch,
                    beacon->build_date  // Build date from transmitter firmware metadata
                );
                
                LOG_DEBUG(kLogTag, "[VERSION_BEACON] Metadata: %s v%d.%d.%d",
                         beacon->env_name,
                         beacon->version_major,
                         beacon->version_minor,
                         beacon->version_patch);
                
                // Power profile checks would go here when implemented
                
                if (!request_sent) {
                    LOG_DEBUG(kLogTag, "All cached configs up-to-date");
                }
            }
        },
        0xFF, nullptr);
    
    LOG_DEBUG(kLogTag, "Registered %d message routes", router.route_count());
}

// Note: task_periodic_announcement removed - EspnowDiscovery::start() is now called directly from setup()
// since it creates its own internal task. The old wrapper task was causing the callback lambda to be
// destroyed while the internal task was still running, leading to crashes.

void task_espnow_worker(void *parameter) {
    LOG_DEBUG(kLogTag, "ESP-NOW Worker task started");

    auto& state_machine = RxStateMachine::instance();
    auto& connection_handler = ReceiverConnectionHandler::instance();
    auto& connection_manager = EspNowConnectionManager::instance();

    if (!state_machine.init()) {
        LOG_ERROR(kLogTag, "Failed to initialize RX state machine");
    }
    
    // Initialize battery settings cache (Phase 2: Version tracking)
    BatterySettingsCache::instance().init();
    LOG_INFO(kLogTag, "Battery settings cache initialized (version=%u)", 
             BatterySettingsCache::instance().get_version());
    
    // NOTE: Message routes are now initialized in setup() BEFORE this task starts
    // This prevents race condition where PROBE messages arrive before handlers are registered
    
    auto& router = EspnowMessageRouter::instance();
    espnow_queue_msg_t queue_msg;
    
    // Connection watchdog state
    static ConnectionState transmitter_state = { false, 0 };
    uint32_t last_stats_log_ms = millis();
    uint32_t last_callbacks = 0;
    uint32_t last_drops = 0;
    
    // NOTE: Connection timeout is now handled by RxHeartbeatManager::tick() (90s timeout)
    // This ensures consistency and avoids duplicate timeout checks
    
    for (;;) {
        // Check for messages with short timeout to allow periodic connection check
        if (xQueueReceive(ESPNow::queue, &queue_msg, pdMS_TO_TICKS(TimingConfig::RX_ESPNOW_QUEUE_RECEIVE_TIMEOUT_MS)) == pdTRUE) {
            if (queue_msg.len < 1) continue;

            const uint8_t msg_type = queue_msg.data[0];
            connection_handler.on_link_activity(queue_msg.mac);
            state_machine.on_message_processing(msg_type, ++g_rx_message_seq);
            
            // Update connection watchdog on any received message
            transmitter_state.last_rx_time_ms = millis();

            // Centralized connection timeout ownership: any ESP-NOW traffic indicates liveness.
            connection_manager.on_heartbeat_received();

            // Keep dashboard connection status fresh on any ESP-NOW traffic
            if (TransmitterManager::isMACKnown()) {
                TransmitterManager::updateRuntimeStatus(
                    TransmitterManager::isMqttConnected(),
                    TransmitterManager::isEthernetConnected()
                );
            }

            // Ensure sender is registered as a peer using common utility
            // Only trigger peer_registered events when in CONNECTING state to avoid spam
            auto current_state = connection_manager.get_state();
            bool is_connecting = (current_state == EspNowConnectionState::CONNECTING);
            
            if (!EspnowPeerManager::is_peer_registered(queue_msg.mac)) {
                if (EspnowPeerManager::add_peer(queue_msg.mac, 0)) {
                    if (is_connecting) {
                        connection_handler.on_peer_registered(queue_msg.mac);
                    }
                }
            } else {
                // Peer already exists (e.g., transmitter reboot). Ensure state advances.
                if (is_connecting && !connection_manager.is_connected()) {
                    connection_handler.on_peer_registered(queue_msg.mac);
                }
            }

            if (!transmitter_state.is_connected && connection_manager.is_connected()) {
                transmitter_state.is_connected = true;
                LOG_INFO(kLogTag, "Transmitter connection established");
                // Note: Initialization requests (config sections, REQUEST_DATA) are sent automatically
                // by the state machine callback when transitioning to CONNECTED state.
                // See rx_connection_handler.cpp state callback for implementation.
            }

            // Post DATA_RECEIVED event for actual data messages only
            // Exclude periodic keep-alive/status messages (heartbeat, version beacon)
            // These are tracked separately and don't represent "new data" for connection purposes
            if (connection_manager.is_connected()) {
                bool is_keepalive_msg = (msg_type == msg_heartbeat || 
                                         msg_type == msg_heartbeat_ack ||
                                         msg_type == msg_version_beacon);
                
                if (!is_keepalive_msg) {
                    connection_handler.on_data_received(queue_msg.mac);
                }
            }
            
            // Route message using common router
            if (!router.route_message(queue_msg)) {
                state_machine.on_message_error();
                // Message not handled - check if it's an unknown packet subtype
                uint8_t type = queue_msg.data[0];
                if (type == msg_packet) {
                    EspnowPacketUtils::PacketInfo info;
                    if (EspnowPacketUtils::get_packet_info(&queue_msg, info)) {
                        handle_packet_unknown(&queue_msg, info.subtype);
                    }
                } else {
                    LOG_WARN(kLogTag, "Unknown message type: %u, len=%d", type, queue_msg.len);
                }
            } else {
                state_machine.on_message_valid();
            }
        }

        state_machine.check_stale(TimingConfig::RX_ESPNOW_STALE_TIMEOUT_MS,
                      TimingConfig::RX_ESPNOW_CONFIG_UPDATE_GRACE_MS);

        const uint32_t now_ms = millis();
        if ((now_ms - last_stats_log_ms) >= TimingConfig::RX_ESPNOW_QUEUE_STATS_LOG_INTERVAL_MS) {
            uint32_t callbacks = ESPNow::rx_callback_count;
            uint32_t drops = ESPNow::rx_queue_drop_count;
            UBaseType_t queued_now = uxQueueMessagesWaiting(ESPNow::queue);
            uint32_t high_water = ESPNow::rx_queue_high_watermark;

            LOG_INFO(kLogTag,
                     "Queue stats: cb(+%lu)=%lu drop(+%lu)=%lu q=%u/%d high=%lu",
                     static_cast<unsigned long>(callbacks - last_callbacks),
                     static_cast<unsigned long>(callbacks),
                     static_cast<unsigned long>(drops - last_drops),
                     static_cast<unsigned long>(drops),
                     static_cast<unsigned>(queued_now),
                     ESPNow::QUEUE_SIZE,
                     static_cast<unsigned long>(high_water));

            last_callbacks = callbacks;
            last_drops = drops;
            last_stats_log_ms = now_ms;
        }
        
        // NOTE: Connection timeout check REMOVED - now handled by RxHeartbeatManager::tick()
        // The heartbeat manager provides proper 90-second timeout with grace period
        // and coordinates with the ConnectionManager state machine.
    }
}

// Handler implementations live in espnow_message_handlers.cpp and espnow_settings_sync.cpp.
