#include "espnow/espnow_runtime_detail.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp32common/espnow/packet_utils.h>

#include "espnow/battery_data_store.h"
#include "espnow/battery_handlers.h"
#include "espnow/espnow_settings_sync.h"
#include "espnow/rx_connection_handler.h"
#include "espnow/rx_heartbeat_manager.h"
#include "espnow/rx_state_machine.h"
#include "espnow/type_catalog_cache.h"
#include "ui/runtime/ui_runtime.h"
#include "../../lib/webserver_lcd/utils/transmitter_manager.h"

namespace ESPNowRuntime::Detail {

std::atomic<bool> g_radio_initialized{false};
std::atomic<bool> g_state_initialized{false};
std::atomic<bool> g_callbacks_registered{false};
std::atomic<bool> g_discovery_started{false};
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
    DisplayUpdateQueue::snapshot_t snapshot{};
    snapshot.soc_percent = soc_percent;
    snapshot.power_w = power_w;
    (void)DisplayUpdateQueue::enqueue(snapshot);
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

void handle_flash_led_message(const espnow_queue_msg_t* msg) {
    if (ESPNow::receiver_ota_led_override_active.load(std::memory_order_relaxed)) {
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
    ESPNow::current_led_color.store(flash_msg->color, std::memory_order_relaxed);
    ESPNow::current_led_effect.store(flash_msg->effect, std::memory_order_relaxed);
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

    ESPNow::rx_callback_count.fetch_add(1, std::memory_order_relaxed);

    if (xQueueSend(ESPNow::message_queue, &msg, 0) != pdPASS) {
        ESPNow::rx_queue_drop_count.fetch_add(1, std::memory_order_relaxed);
        static uint32_t last_drop_log = 0;
        const uint32_t now = millis();
        if (now - last_drop_log > 2000) {
            last_drop_log = now;
            LOG_WARN("ESPNOW", "RX queue full, dropping packet");
        }
    } else {
        const UBaseType_t q_depth = uxQueueMessagesWaiting(ESPNow::message_queue);
        uint32_t observed = ESPNow::rx_queue_high_watermark.load(std::memory_order_relaxed);
        while (q_depth > observed &&
               !ESPNow::rx_queue_high_watermark.compare_exchange_weak(
                   observed,
                   static_cast<uint32_t>(q_depth),
                   std::memory_order_relaxed,
                   std::memory_order_relaxed)) {
        }
    }
}

void on_data_sent(const uint8_t* mac, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS && mac) {
        LOG_WARN("ESPNOW", "Send failed to %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

}  // namespace ESPNowRuntime::Detail