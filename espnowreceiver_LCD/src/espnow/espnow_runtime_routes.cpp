#include "espnow/espnow_runtime_detail.h"

#include <channel_manager.h>
#include <espnow_discovery.h>
#include <espnow_peer_manager.h>
#include <esp32common/espnow/connection_manager.h>
#include <esp32common/espnow/message_router.h>
#include <rx_route_registry.h>

#include "espnow/battery_handlers.h"
#include "espnow/espnow_settings_sync.h"
#include "espnow/rx_connection_handler.h"
#include "espnow/rx_state_machine.h"

namespace ESPNowRuntime::Detail {

namespace {
RxProbeAckThrottleState g_probe_ack_throttle_state{};
}

void setup_message_routes() {
    auto& router = EspnowMessageRouter::instance();
    router.clear_routes();

    g_probe_config = {};
    g_ack_config = {};

    g_probe_config.send_ack_response = true;
    g_probe_config.connection_flag = nullptr;
    g_probe_config.peer_mac_storage = nullptr;
    g_probe_config.on_connection = nullptr;
    g_probe_config.on_probe_received = [](const uint8_t* mac, uint32_t seq) {
        ReceiverConnectionHandler::instance().on_probe_received(mac);
        store_peer_mac(mac);
        register_transmitter_mac(mac);
        if (!g_logged_probe) {
            LOG_INFO("ESPNOW", "Probe received (seq=%lu)", static_cast<unsigned long>(seq));
            g_logged_probe = true;
        }
    };

    g_ack_config.connection_flag = nullptr;
    g_ack_config.peer_mac_storage = nullptr;
    g_ack_config.expected_seq = nullptr;
    g_ack_config.lock_channel = nullptr;
    g_ack_config.ack_received_flag = nullptr;
    g_ack_config.set_wifi_channel = false;
    g_ack_config.on_connection = nullptr;

    register_standard_probe_ack_routes(router, g_probe_config, g_ack_config, g_probe_ack_throttle_state);

    // Phase 2: bidirectional connection confirmation handshake
    register_standard_connect_confirm_route(router);

    router.register_route(msg_data,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_data_message(msg);
        },
        0xFF,
        nullptr);

    router.register_route(msg_flash_led,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_flash_led_message(msg);
        },
        0xFF,
        nullptr);

    router.register_route(msg_debug_ack,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_known_struct_message<debug_ack_t>(msg, "DEBUG_ACK");
        },
        0xFF,
        nullptr);

    router.register_route(msg_battery_status,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_battery_status_message(msg);
        },
        0xFF,
        nullptr);

    router.register_route(msg_battery_info,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            if (handle_battery_info(msg)) {
                ReceiverConnectionHandler::instance().on_data_received(msg->mac);
                RxStateMachine::instance().on_activity();
                log_type_once(msg_battery_info, "BATTERY_INFO");
            }
        },
        0xFF,
        nullptr);

    router.register_route(msg_charger_status,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            if (handle_charger_status(msg)) {
                ReceiverConnectionHandler::instance().on_data_received(msg->mac);
                RxStateMachine::instance().on_activity();
                log_type_once(msg_charger_status, "CHARGER_STATUS");
            }
        },
        0xFF,
        nullptr);

    router.register_route(msg_inverter_status,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            if (handle_inverter_status(msg)) {
                ReceiverConnectionHandler::instance().on_data_received(msg->mac);
                RxStateMachine::instance().on_activity();
                log_type_once(msg_inverter_status, "INVERTER_STATUS");
            }
        },
        0xFF,
        nullptr);

    router.register_route(msg_system_status,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            if (handle_system_status(msg)) {
                ReceiverConnectionHandler::instance().on_data_received(msg->mac);
                RxStateMachine::instance().on_activity();
                log_type_once(msg_system_status, "SYSTEM_STATUS");
            }
        },
        0xFF,
        nullptr);

    router.register_route(msg_component_config,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            if (handle_component_config(msg)) {
                ReceiverConnectionHandler::instance().on_data_received(msg->mac);
                RxStateMachine::instance().on_activity();
                log_type_once(msg_component_config, "COMPONENT_CONFIG");
            }
        },
        0xFF,
        nullptr);

    router.register_route(msg_settings_update_ack,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            if (EspnowSettingsSync::handle_settings_update_ack(msg)) {
                ReceiverConnectionHandler::instance().on_data_received(msg->mac);
                RxStateMachine::instance().on_activity();
                log_type_once(msg_settings_update_ack, "SETTINGS_UPDATE_ACK");
            }
        },
        0xFF,
        nullptr);

    router.register_route(msg_settings_changed,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            if (EspnowSettingsSync::handle_settings_changed(msg)) {
                ReceiverConnectionHandler::instance().on_data_received(msg->mac);
                RxStateMachine::instance().on_activity();
                log_type_once(msg_settings_changed, "SETTINGS_CHANGED");
            }
        },
        0xFF,
        nullptr);

    router.register_route(msg_network_config_ack,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_network_config_ack_message(msg);
        },
        0xFF,
        nullptr);

    router.register_route(msg_mqtt_config_ack,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_mqtt_config_ack_message(msg);
        },
        0xFF,
        nullptr);

    router.register_route(msg_version_beacon,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_version_beacon_message(msg);
        },
        0xFF,
        nullptr);

    router.register_route(msg_metadata_response,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_metadata_response_message(msg);
        },
        0xFF,
        nullptr);

    router.register_route(msg_time_transitions_snapshot,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_time_transitions_snapshot_message(msg);
        },
        0xFF,
        nullptr);

    register_standard_heartbeat_route(router, [](const uint8_t* mac) {
        mark_link_alive(mac);
        if (!g_logged_heartbeat) {
            LOG_INFO("ESPNOW", "Heartbeat stream active");
            g_logged_heartbeat = true;
        }
    });

    router.register_route(msg_heartbeat_ack,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_heartbeat_ack_message(msg);
        },
        0xFF,
        nullptr);

    router.register_route(msg_config_changed,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_known_struct_message<config_changed_t>(msg, "CONFIG_CHANGED");
        },
        0xFF,
        nullptr);

    router.register_route(msg_component_apply_ack,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            if (EspnowSettingsSync::handle_component_apply_ack(msg)) {
                ReceiverConnectionHandler::instance().on_data_received(msg->mac);
                RxStateMachine::instance().on_activity();
                log_type_once(msg_component_apply_ack, "COMPONENT_APPLY_ACK");
            }
        },
        0xFF,
        nullptr);

    router.register_route(msg_event_log_summary,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_event_log_summary_message(msg);
        },
        0xFF,
        nullptr);

    router.register_route(msg_event_logs_clear_ack,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_event_logs_clear_ack_message(msg);
        },
        0xFF,
        nullptr);

    router.register_route(msg_temperature_report,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_temperature_report_message(msg);
        },
        0xFF,
        nullptr);

    register_standard_type_catalog_fragment_routes(
        router,
        [](const espnow_queue_msg_t* msg, uint8_t /*fragment_type*/, const char* label) {
            handle_type_catalog_fragment_message(msg, label);
        });

    router.register_route(msg_type_catalog_versions,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_known_struct_message<type_catalog_versions_t>(msg, "TYPE_CATALOG_VERSIONS");
        },
        0xFF,
        nullptr);

    register_standard_packet_subtype_routes(
        router,
        [](const espnow_queue_msg_t* msg, uint8_t /*packet_subtype*/, const char* label) {
            handle_packet_subtype_message(msg, label);
        });

    LOG_INFO("ESPNOW", "Registered %u ESP-NOW routes", static_cast<unsigned>(router.route_count()));
}

}  // namespace ESPNowRuntime::Detail