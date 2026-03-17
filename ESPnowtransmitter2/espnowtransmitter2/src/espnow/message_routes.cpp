#include "message_handler.h"

#include "component_catalog_handlers.h"

#include "discovery_task.h"
#include "version_beacon_manager.h"
#include "tx_send_guard.h"
#include "../network/mqtt_manager.h"
#include "../settings/settings_manager.h"
#include "../config/logging_config.h"

#include <esp32common/espnow/message_router.h>
#include <esp32common/espnow/standard_handlers.h>
#include <espnow_transmitter.h>
#include <firmware_version.h>
#include <cstring>

void EspnowMessageHandler::setup_message_routes() {
    auto& router = EspnowMessageRouter::instance();
    probe_config_ = {};
    ack_config_ = {};

    // Setup PROBE handler configuration
    probe_config_.send_ack_response = true;
    probe_config_.connection_flag = nullptr;
    probe_config_.peer_mac_storage = receiver_mac_;
    probe_config_.on_connection = [](const uint8_t* mac, bool connected) {
        LOG_INFO("MSG_HANDLER", "Receiver connected via PROBE");
    };

    // Setup ACK handler configuration
    ack_config_.peer_mac_storage = receiver_mac_;
    ack_config_.connection_flag = nullptr;
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

    // Batched component apply request handler (receiver → transmitter)
    router.register_route(msg_component_apply_request,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            TxComponentCatalogHandlers::handle_component_apply_request(*msg);
        },
        0xFF, this);

    router.register_route(msg_request_battery_types,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_request_battery_types(*msg);
        },
        0xFF, this);

    router.register_route(msg_request_inverter_types,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_request_inverter_types(*msg);
        },
        0xFF, this);

    router.register_route(msg_request_inverter_interfaces,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_request_inverter_interfaces(*msg);
        },
        0xFF, this);

    router.register_route(msg_request_type_catalog_versions,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_request_type_catalog_versions(*msg);
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

                esp_err_t result = TxSendGuard::send_to_receiver_guarded(
                    msg->mac,
                    (const uint8_t*)&response,
                    sizeof(response),
                    "version_response"
                );
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
