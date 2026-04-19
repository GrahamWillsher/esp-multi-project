#include "espnow/espnow_runtime.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp32common/espnow/common.h>
#include <esp32common/config/timing_config.h>
#include <esp32common/espnow/connection_manager.h>
#include <esp32common/espnow/message_router.h>
#include <esp32common/espnow/packet_utils.h>
#include <esp32common/espnow/standard_handlers.h>
#include <channel_manager.h>
#include <espnow_discovery.h>
#include <espnow_peer_manager.h>

#include "common_lcd.h"
#include "espnow/battery_data_store.h"
#include "espnow/battery_handlers.h"
#include "espnow/type_catalog_cache.h"
#include "espnow/rx_connection_handler.h"
#include "espnow/rx_heartbeat_manager.h"
#include "espnow/rx_state_machine.h"
#include "espnow/espnow_settings_sync.h"
#include "helpers.h"
#include "logging_config.h"
#include "runtime/display_update_queue.h"
#include "task_config.h"
#include "ui/runtime/ui_runtime.h"
#include "../../lib/webserver_lcd/utils/transmitter_manager.h"

// Forward declarations — webserver SSE and transmitter MAC registration.
// Defined in lib/webserver_lcd/webserver.cpp and utils/transmitter_manager.cpp.
void notify_sse_data_updated();
void register_transmitter_mac(const uint8_t* mac);

namespace ESPNowRuntime {
namespace {

constexpr uint32_t kWorkerPollMs = 100;
constexpr uint32_t kRxStateStaleTimeoutMs = 15000;
constexpr uint32_t kRxStateGraceWindowMs = 2000;
constexpr int kQueueSize = 32;

bool g_radio_initialized = false;
bool g_state_initialized = false;
bool g_callbacks_registered = false;
bool g_discovery_started = false;
bool g_logged_probe = false;
bool g_logged_data = false;
bool g_logged_heartbeat = false;
bool g_logged_known_types[256] = {};
uint32_t g_rx_message_seq = 0;
uint32_t g_last_probe_ack_ms = 0;
uint32_t g_last_probe_ack_seq = 0;
uint8_t g_last_probe_ack_mac[6] = {};

EspnowStandardHandlers::ProbeHandlerConfig g_probe_config{};
EspnowStandardHandlers::AckHandlerConfig g_ack_config{};

void store_peer_mac(const uint8_t* mac) {
    if (!mac) {
        return;
    }
    memcpy(ESPNow::peer_mac, mac, 6);
}

void mark_link_alive(const uint8_t* mac) {
    store_peer_mac(mac);
}

void enqueue_snapshot(float soc_percent, int32_t power_w) {
    DisplayUpdateQueue::snapshot_t s{};
    s.soc_percent = soc_percent;
    s.power_w = power_w;
    (void)DisplayUpdateQueue::enqueue(s);
}

void log_type_once(uint8_t type, const char* label) {
    if (!g_logged_known_types[type]) {
        LOG_INFO("ESPNOW", "%s handling active", label);
        g_logged_known_types[type] = true;
    }
}

void mark_protocol_activity(const uint8_t* mac) {
    mark_link_alive(mac);
    register_transmitter_mac(mac);
    ReceiverConnectionHandler::instance().on_data_received(mac);
    RxStateMachine::instance().on_activity();
}

template <typename T>
bool validate_struct_size(const espnow_queue_msg_t* msg, const char* label) {
    if (!msg || msg->len < static_cast<int>(sizeof(T))) {
        LOG_WARN("ESPNOW", "%s too short: %d bytes", label, msg ? msg->len : -1);
        return false;
    }
    return true;
}

template <typename T>
void handle_known_struct_message(const espnow_queue_msg_t* msg, const char* label) {
    if (!validate_struct_size<T>(msg, label)) {
        return;
    }

    const auto* payload = reinterpret_cast<const T*>(msg->data);
    mark_protocol_activity(msg->mac);
    log_type_once(payload->type, label);
}

void handle_flash_led_message(const espnow_queue_msg_t* msg) {
    if (ESPNow::receiver_ota_led_override_active) {
        return;
    }

    if (!validate_struct_size<flash_led_t>(msg, "FLASH_LED")) {
        return;
    }

    const auto* flash_msg = reinterpret_cast<const flash_led_t*>(msg->data);
    if (flash_msg->color > 3U) {
        LOG_WARN("ESPNOW", "Invalid LED color code: %u", static_cast<unsigned>(flash_msg->color));
        return;
    }

    if (flash_msg->effect > 2U) {
        LOG_WARN("ESPNOW", "Invalid LED effect code: %u", static_cast<unsigned>(flash_msg->effect));
        return;
    }

    mark_protocol_activity(msg->mac);
    ESPNow::current_led_color = flash_msg->color;
    ESPNow::current_led_effect = flash_msg->effect;
    UI::Runtime::set_led_state(flash_msg->color, flash_msg->effect);
    ReceiverConnectionHandler::instance().on_led_state_received();
    log_type_once(flash_msg->type, "FLASH_LED");
}

void handle_temperature_report_message(const espnow_queue_msg_t* msg) {
    if (!validate_struct_size<temperature_report_t>(msg, "TEMPERATURE_REPORT")) {
        return;
    }

    const auto* report = reinterpret_cast<const temperature_report_t*>(msg->data);
    mark_protocol_activity(msg->mac);
    TransmitterManager::storeTemperatureReport(*report);
    notify_sse_data_updated();
    log_type_once(report->type, "TEMPERATURE_REPORT");

    if (report->valid) {
        LOG_DEBUG("ESPNOW", "TEMPERATURE_REPORT seq=%lu value=%.2fC",
                  static_cast<unsigned long>(report->seq),
                  static_cast<double>(report->temperature_centi_c) / 100.0);
    } else {
        LOG_WARN("ESPNOW", "TEMPERATURE_REPORT seq=%lu invalid",
                 static_cast<unsigned long>(report->seq));
    }
}

void handle_metadata_response_message(const espnow_queue_msg_t* msg) {
    if (!validate_struct_size<metadata_response_t>(msg, "METADATA_RESPONSE")) {
        return;
    }

    const auto* response = reinterpret_cast<const metadata_response_t*>(msg->data);
    mark_protocol_activity(msg->mac);

    TransmitterManager::storeMetadata(
        response->valid,
        response->env_name,
        response->device_type,
        response->version_major,
        response->version_minor,
        response->version_patch,
        response->build_date);

    notify_sse_data_updated();
    log_type_once(response->type, "METADATA_RESPONSE");
}

void handle_network_config_ack_message(const espnow_queue_msg_t* msg) {
    if (!validate_struct_size<network_config_ack_t>(msg, "NETWORK_CONFIG_ACK")) {
        return;
    }

    const auto* ack = reinterpret_cast<const network_config_ack_t*>(msg->data);
    mark_protocol_activity(msg->mac);

    TransmitterManager::storeNetworkConfig(
        ack->current_ip, ack->current_gateway, ack->current_subnet,
        ack->static_ip, ack->static_gateway, ack->static_subnet,
        ack->static_dns_primary, ack->static_dns_secondary,
        ack->use_static_ip != 0,
        ack->config_version);

    notify_sse_data_updated();
    log_type_once(ack->type, "NETWORK_CONFIG_ACK");
}

void handle_mqtt_config_ack_message(const espnow_queue_msg_t* msg) {
    if (!validate_struct_size<mqtt_config_ack_t>(msg, "MQTT_CONFIG_ACK")) {
        return;
    }

    const auto* ack = reinterpret_cast<const mqtt_config_ack_t*>(msg->data);
    if (!EspnowPacketUtils::verify_message_crc32(ack)) {
        LOG_WARN("ESPNOW", "MQTT_CONFIG_ACK CRC32 mismatch (stored=0x%08lX)",
                 static_cast<unsigned long>(ack->checksum));
        return;
    }

    mark_protocol_activity(msg->mac);

    TransmitterManager::storeMqttConfig(
        ack->enabled != 0,
        ack->server,
        ack->port,
        ack->username,
        ack->password,
        ack->client_id,
        ack->connected != 0,
        ack->config_version);

    notify_sse_data_updated();
    log_type_once(ack->type, "MQTT_CONFIG_ACK");
}

void handle_version_beacon_message(const espnow_queue_msg_t* msg) {
    if (!validate_struct_size<version_beacon_t>(msg, "VERSION_BEACON")) {
        return;
    }

    const auto* beacon = reinterpret_cast<const version_beacon_t*>(msg->data);
    mark_protocol_activity(msg->mac);

    TransmitterManager::updateRuntimeStatus(beacon->mqtt_connected, beacon->ethernet_connected);
    TransmitterManager::storeMetadata(
        true,
        beacon->env_name,
        "TRANSMITTER",
        beacon->version_major,
        beacon->version_minor,
        beacon->version_patch,
        beacon->build_date);

    notify_sse_data_updated();
    log_type_once(beacon->type, "VERSION_BEACON");
}

void handle_event_log_summary_message(const espnow_queue_msg_t* msg) {
    if (!validate_struct_size<event_log_summary_t>(msg, "EVENT_LOG_SUMMARY")) {
        return;
    }

    const auto* summary = reinterpret_cast<const event_log_summary_t*>(msg->data);
    mark_protocol_activity(msg->mac);
    TransmitterManager::storeEventLogSummary(*summary);
    notify_sse_data_updated();
    log_type_once(summary->type, "EVENT_LOG_SUMMARY");
}

void handle_event_logs_clear_ack_message(const espnow_queue_msg_t* msg) {
    if (!validate_struct_size<event_logs_clear_ack_t>(msg, "EVENT_LOGS_CLEAR_ACK")) {
        return;
    }

    const auto* ack = reinterpret_cast<const event_logs_clear_ack_t*>(msg->data);
    mark_protocol_activity(msg->mac);
    TransmitterManager::storeEventLogClearAck(*ack);
    notify_sse_data_updated();
    log_type_once(ack->type, "EVENT_LOGS_CLEAR_ACK");
}

void handle_time_transitions_snapshot_message(const espnow_queue_msg_t* msg) {
    if (!validate_struct_size<time_transitions_snapshot_t>(msg, "TIME_TRANSITIONS_SNAPSHOT")) {
        return;
    }

    const auto* snapshot = reinterpret_cast<const time_transitions_snapshot_t*>(msg->data);
    if (!EspnowPacketUtils::verify_message_crc32(snapshot)) {
        LOG_WARN("ESPNOW", "TIME_TRANSITIONS_SNAPSHOT CRC mismatch");
        return;
    }

    mark_protocol_activity(msg->mac);
    log_type_once(snapshot->type, "TIME_TRANSITIONS_SNAPSHOT");
}

void handle_type_catalog_fragment_message(const espnow_queue_msg_t* msg, const char* label) {
    if (!msg || msg->len < static_cast<int>(offsetof(type_catalog_fragment_t, entries))) {
        LOG_WARN("ESPNOW", "%s too short: %d bytes", label, msg ? msg->len : -1);
        return;
    }

    const auto* fragment = reinterpret_cast<const type_catalog_fragment_t*>(msg->data);

    if (fragment->entry_count > TYPE_CATALOG_MAX_ENTRIES_PER_FRAGMENT) {
        LOG_WARN("ESPNOW", "%s invalid entry_count=%u", label, static_cast<unsigned>(fragment->entry_count));
        return;
    }

    const size_t expected_len = offsetof(type_catalog_fragment_t, entries) +
                                (static_cast<size_t>(fragment->entry_count) * sizeof(type_catalog_entry_t));
    if (msg->len < static_cast<int>(expected_len)) {
        LOG_WARN("ESPNOW", "%s too short: %d bytes (expected >= %u)",
                 label,
                 msg->len,
                 static_cast<unsigned>(expected_len));
        return;
    }

    switch (fragment->type) {
        case msg_battery_types_fragment:
            TypeCatalogCache::handle_battery_fragment(fragment, static_cast<size_t>(msg->len));
            break;
        case msg_inverter_types_fragment:
            TypeCatalogCache::handle_inverter_fragment(fragment, static_cast<size_t>(msg->len));
            break;
        case msg_inverter_interfaces_fragment:
            TypeCatalogCache::handle_inverter_interface_fragment(fragment, static_cast<size_t>(msg->len));
            break;
        default:
            LOG_WARN("ESPNOW", "%s unexpected type=%u", label, static_cast<unsigned>(fragment->type));
            return;
    }

    mark_protocol_activity(msg->mac);
    log_type_once(fragment->type, label);
}

void handle_packet_subtype_message(const espnow_queue_msg_t* msg, const char* label) {
    if (!msg) {
        return;
    }

    EspnowPacketUtils::PacketInfo info;
    if (!EspnowPacketUtils::get_packet_info(msg, info)) {
        LOG_WARN("ESPNOW", "%s invalid packet structure", label);
        return;
    }

    const uint32_t calc_crc = EspnowPacketUtils::crc32_packet(info.payload, info.payload_len);
    if (calc_crc != info.checksum) {
        LOG_WARN("ESPNOW", "%s packet CRC32 mismatch (calc=0x%08lX recv=0x%08lX)",
                 label,
                 static_cast<unsigned long>(calc_crc),
                 static_cast<unsigned long>(info.checksum));
        return;
    }

    mark_protocol_activity(msg->mac);
    log_type_once(msg_packet, label);
}

void handle_data_message(const espnow_queue_msg_t* msg) {
    if (!msg || msg->len < static_cast<int>(sizeof(espnow_payload_t))) {
        return;
    }

    const auto* payload = reinterpret_cast<const espnow_payload_t*>(msg->data);
    if (!EspnowPacketUtils::verify_message_crc32(payload)) {
        LOG_WARN("ESPNOW", "Invalid msg_data CRC32 (stored=0x%08lX)",
                 static_cast<unsigned long>(payload->checksum));
        return;
    }

    const float soc = static_cast<float>(payload->soc > 100 ? 100 : payload->soc);
    BatteryData::update_basic_telemetry(payload->soc, static_cast<int32_t>(payload->power));
    enqueue_snapshot(soc, static_cast<int32_t>(payload->power));
    notify_sse_data_updated();
    register_transmitter_mac(msg->mac);
    mark_link_alive(msg->mac);
    ReceiverConnectionHandler::instance().on_power_data_received();
    ReceiverConnectionHandler::instance().on_data_received(msg->mac);
    RxStateMachine::instance().on_activity();

    if (!g_logged_data) {
        LOG_INFO("ESPNOW", "Legacy msg_data stream active");
        g_logged_data = true;
    }
}

void handle_battery_status_message(const espnow_queue_msg_t* msg) {
    if (!msg || msg->len < static_cast<int>(sizeof(battery_status_msg_t))) {
        return;
    }

    const auto* payload = reinterpret_cast<const battery_status_msg_t*>(msg->data);
    if (!EspnowPacketUtils::verify_message_crc32(payload)) {
        LOG_WARN("ESPNOW", "Invalid battery status CRC32 (stored=0x%08lX)",
                 static_cast<unsigned long>(payload->checksum));
        return;
    }

    const float soc = static_cast<float>(payload->soc_percent_100) / 100.0f;
    BatteryData::update_battery_status(*payload);
    enqueue_snapshot(soc, payload->power_W);
    notify_sse_data_updated();
    register_transmitter_mac(msg->mac);
    mark_protocol_activity(msg->mac);
    ReceiverConnectionHandler::instance().on_power_data_received();

    if (!g_logged_data) {
        LOG_INFO("ESPNOW", "Battery status stream active");
        g_logged_data = true;
    }
}

void handle_heartbeat_message(const espnow_queue_msg_t* msg) {
    if (!msg || msg->len < static_cast<int>(sizeof(heartbeat_t))) {
        return;
    }

    const auto* hb = reinterpret_cast<const heartbeat_t*>(msg->data);
    RxHeartbeatManager::instance().on_heartbeat(hb, msg->mac);
    mark_link_alive(msg->mac);

    if (!g_logged_heartbeat) {
        LOG_INFO("ESPNOW", "Heartbeat stream active");
        g_logged_heartbeat = true;
    }
}

void handle_heartbeat_ack_message(const espnow_queue_msg_t* msg) {
    if (!validate_struct_size<heartbeat_ack_t>(msg, "HEARTBEAT_ACK")) {
        return;
    }

    const auto* ack = reinterpret_cast<const heartbeat_ack_t*>(msg->data);
    if (!EspnowPacketUtils::verify_message_crc32(ack)) {
        LOG_WARN("ESPNOW", "HEARTBEAT_ACK CRC mismatch");
        return;
    }

    mark_protocol_activity(msg->mac);
    log_type_once(ack->type, "HEARTBEAT_ACK");
}

void on_data_recv(const uint8_t* mac, const uint8_t* data, int len) {
    if (!mac || !data || len < 1 || len > static_cast<int>(sizeof(espnow_queue_msg_t::data))) {
        return;
    }
    if (ESPNow::message_queue == nullptr) {
        return;
    }

    espnow_queue_msg_t msg{};
    memcpy(msg.mac, mac, 6);
    memcpy(msg.data, data, static_cast<size_t>(len));
    msg.len = len;
    msg.timestamp = millis();

    ESPNow::rx_callback_count++;

    if (xQueueSend(ESPNow::message_queue, &msg, 0) != pdPASS) {
        ESPNow::rx_queue_drop_count++;
        static uint32_t last_drop_log = 0;
        const uint32_t now = millis();
        if (now - last_drop_log > 2000) {
            last_drop_log = now;
            LOG_WARN("ESPNOW", "RX queue full, dropping packet");
        }
    } else {
        const UBaseType_t q_depth = uxQueueMessagesWaiting(ESPNow::message_queue);
        if (q_depth > ESPNow::rx_queue_high_watermark) {
            ESPNow::rx_queue_high_watermark = static_cast<uint32_t>(q_depth);
        }
    }
}

void on_data_sent(const uint8_t* mac, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS && mac) {
        LOG_WARN("ESPNOW", "Send failed to %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
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

    router.register_route(msg_probe,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            const auto state = EspNowConnectionManager::instance().get_state();
            bool send_probe_ack = true;

            if (msg->len >= static_cast<int>(sizeof(probe_t))) {
                const auto* probe = reinterpret_cast<const probe_t*>(msg->data);
                const uint32_t now = millis();

                // Always ACK reconnect probes (even while CONNECTED) so a transmitter
                // that has dropped to discovery can recover quickly.
                // Keep duplicate suppression to avoid ACK storms under retry pressure.
                const bool same_peer =
                    (memcmp(g_last_probe_ack_mac, msg->mac, sizeof(g_last_probe_ack_mac)) == 0);
                const bool same_seq = same_peer && (probe->seq == g_last_probe_ack_seq);
                const uint32_t min_interval_ms =
                    (state == EspNowConnectionState::CONNECTED) ? 120U : 40U;

                if (same_seq && ((now - g_last_probe_ack_ms) < min_interval_ms)) {
                    send_probe_ack = false;
                }

                if (send_probe_ack) {
                    g_last_probe_ack_ms = now;
                    g_last_probe_ack_seq = probe->seq;
                    memcpy(g_last_probe_ack_mac, msg->mac, sizeof(g_last_probe_ack_mac));
                }
            }

            g_probe_config.send_ack_response = send_probe_ack;
            EspnowStandardHandlers::handle_probe(msg, &g_probe_config);
        },
        0xFF,
        nullptr);

    router.register_route(msg_ack,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            EspnowStandardHandlers::handle_ack(msg, &g_ack_config);
        },
        0xFF,
        nullptr);

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

    router.register_route(msg_heartbeat,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_heartbeat_message(msg);
        },
        0xFF,
        nullptr);

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

    router.register_route(msg_battery_types_fragment,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_type_catalog_fragment_message(msg, "BATTERY_TYPES_FRAGMENT");
        },
        0xFF,
        nullptr);

    router.register_route(msg_inverter_types_fragment,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_type_catalog_fragment_message(msg, "INVERTER_TYPES_FRAGMENT");
        },
        0xFF,
        nullptr);

    router.register_route(msg_inverter_interfaces_fragment,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_type_catalog_fragment_message(msg, "INVERTER_INTERFACES_FRAGMENT");
        },
        0xFF,
        nullptr);

    router.register_route(msg_type_catalog_versions,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_known_struct_message<type_catalog_versions_t>(msg, "TYPE_CATALOG_VERSIONS");
        },
        0xFF,
        nullptr);

    router.register_route(msg_packet,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_packet_subtype_message(msg, "PACKET_EVENTS");
        },
        subtype_events,
        nullptr);

    router.register_route(msg_packet,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_packet_subtype_message(msg, "PACKET_LOGS");
        },
        subtype_logs,
        nullptr);

    router.register_route(msg_packet,
        [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            handle_packet_subtype_message(msg, "PACKET_CELL_INFO");
        },
        subtype_cell_info,
        nullptr);

    LOG_INFO("ESPNOW", "Registered %u ESP-NOW routes", static_cast<unsigned>(router.route_count()));
}

}  // namespace

bool init_radio() {
    if (g_radio_initialized) {
        return true;
    }

    const esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ps_err != ESP_OK) {
        LOG_WARN("ESPNOW", "esp_wifi_set_ps(WIFI_PS_NONE) failed: %d", static_cast<int>(ps_err));
    }

    const esp_err_t init_rc = esp_now_init();
    if (init_rc != ESP_OK) {
        LOG_ERROR("ESPNOW", "esp_now_init failed: %d", static_cast<int>(init_rc));
        return false;
    }

    g_radio_initialized = true;
    LOG_INFO("ESPNOW", "ESP-NOW radio initialized on WiFi channel %d", static_cast<int>(WiFi.channel()));
    return true;
}

bool prepare_runtime() {
    if (ESPNow::message_queue == nullptr) {
        ESPNow::message_queue = xQueueCreate(kQueueSize, sizeof(espnow_queue_msg_t));
        if (ESPNow::message_queue == nullptr) {
            LOG_ERROR("ESPNOW", "Failed to create message queue");
            return false;
        }
    }

    setup_message_routes();
    return true;
}

bool init_state() {
    if (g_state_initialized) {
        return true;
    }

    if (!g_radio_initialized) {
        LOG_ERROR("ESPNOW", "init_state called before init_radio");
        return false;
    }

    if (!ChannelManager::instance().init()) {
        LOG_WARN("ESPNOW", "Channel manager init failed");
    }

    if (!EspNowConnectionManager::instance().init()) {
        LOG_ERROR("ESPNOW", "Connection manager init failed");
        return false;
    }
    EspNowConnectionManager::instance().set_auto_reconnect(true);
    EspNowConnectionManager::instance().set_connecting_timeout_ms(TimingConfig::ESPNOW_CONNECTING_TIMEOUT_MS);

    if (!RxStateMachine::instance().init()) {
        LOG_ERROR("ESPNOW", "RX state machine init failed");
        return false;
    }

    ReceiverConnectionHandler::instance().init();
    RxHeartbeatManager::instance().init();

    if (!g_callbacks_registered) {
        if (esp_now_register_recv_cb(on_data_recv) != ESP_OK) {
            LOG_ERROR("ESPNOW", "esp_now_register_recv_cb failed");
            return false;
        }
        if (esp_now_register_send_cb(on_data_sent) != ESP_OK) {
            LOG_ERROR("ESPNOW", "esp_now_register_send_cb failed");
            return false;
        }
        g_callbacks_registered = true;
    }

    // Discovery is started in RuntimeTaskStartup::start_runtime_tasks() to
    // mirror receiver_2 startup ordering.  Keep a guarded fallback here.
    if (!EspnowDiscovery::instance().is_running()) {
        EspnowDiscovery::instance().start(
            []() {
                return EspNowConnectionManager::instance().is_connected();
            },
            TimingConfig::ANNOUNCEMENT_INTERVAL_MS,
            TaskConfig::ANNOUNCEMENT_PRIORITY,
            TaskConfig::ANNOUNCEMENT_TASK_STACK);
    }

    g_discovery_started = EspnowDiscovery::instance().is_running();
    if (!g_discovery_started) {
        LOG_ERROR("ESPNOW", "Discovery task is not running after init_state");
        return false;
    }

    LOG_INFO("ESPNOW", "Discovery state: running=%s suspended=%s",
             EspnowDiscovery::instance().is_running() ? "yes" : "no",
             EspnowDiscovery::instance().is_suspended() ? "yes" : "no");

    g_state_initialized = true;
    LOG_INFO("ESPNOW", "ESP-NOW state initialized (staged LCD receiver lifecycle)");
    return true;
}

void task_worker(void* /*parameter*/) {
    auto& router = EspnowMessageRouter::instance();
    auto& connection_handler = ReceiverConnectionHandler::instance();
    auto& connection_manager = EspNowConnectionManager::instance();

    espnow_queue_msg_t msg{};
    for (;;) {
        if (!g_state_initialized) {
            smart_delay(kWorkerPollMs);
            continue;
        }

        if (ESPNow::message_queue != nullptr &&
            xQueueReceive(ESPNow::message_queue, &msg, pdMS_TO_TICKS(kWorkerPollMs)) == pdPASS) {
            if (msg.len > 0) {
                const uint8_t msg_type = msg.data[0];

                connection_handler.on_link_activity(msg.mac);
                RxStateMachine::instance().on_message_processing(msg_type, ++g_rx_message_seq);

                if (!EspnowPeerManager::is_peer_registered(msg.mac)) {
                    if (EspnowPeerManager::add_peer(msg.mac, 0)) {
                        connection_handler.on_peer_registered(msg.mac);
                    }
                } else if (!connection_manager.is_connected()) {
                    connection_handler.on_peer_registered(msg.mac);
                }

                if (!router.route_message(msg)) {
                    RxStateMachine::instance().on_message_error();
                    LOG_WARN("ESPNOW", "Unhandled message type=%u len=%d",
                             static_cast<unsigned>(msg_type), msg.len);
                } else {
                    RxStateMachine::instance().on_message_valid();
                }
            }
        }

        connection_manager.process_events();
        connection_handler.tick();
        RxHeartbeatManager::instance().tick();
        BatteryData::refresh_staleness();
        RxStateMachine::instance().check_stale(kRxStateStaleTimeoutMs, kRxStateGraceWindowMs);
    }
}

bool is_connected() {
    return g_state_initialized && EspNowConnectionManager::instance().is_connected();
}

}  // namespace ESPNowRuntime
