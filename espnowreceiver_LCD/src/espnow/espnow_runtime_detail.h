#pragma once

#include <Arduino.h>
#include <atomic>
#include <esp_now.h>
#include <esp32common/espnow/common.h>
#include <esp32common/espnow/packet_utils.h>
#include <esp32common/espnow/standard_handlers.h>

#include "espnow/battery_data_store.h"
#include "common_lcd.h"
#include "logging_config.h"
#include "runtime/display_update_queue.h"

void notify_sse_data_updated();
void register_transmitter_mac(const uint8_t* mac);

namespace ESPNowRuntime::Detail {

extern std::atomic<bool> g_radio_initialized;
extern std::atomic<bool> g_state_initialized;
extern std::atomic<bool> g_callbacks_registered;
extern bool g_logged_probe;
extern bool g_logged_data;
extern bool g_logged_heartbeat;
extern bool g_logged_known_types[256];
extern uint32_t g_rx_message_seq;
extern uint32_t g_last_probe_ack_ms;
extern uint32_t g_last_probe_ack_seq;
extern uint8_t g_last_probe_ack_mac[6];

extern EspnowStandardHandlers::ProbeHandlerConfig g_probe_config;
extern EspnowStandardHandlers::AckHandlerConfig g_ack_config;

void store_peer_mac(const uint8_t* mac);
void mark_link_alive(const uint8_t* mac);
void enqueue_snapshot(float soc_percent, int32_t power_w);
void log_type_once(uint8_t type, const char* label);
void mark_protocol_activity(const uint8_t* mac);

struct IngressParseResult {
    const espnow_queue_msg_t* msg = nullptr;
    uint8_t type = 0;
    uint8_t subtype = 0xFF;
    bool has_packet_info = false;
    EspnowPacketUtils::PacketInfo packet_info{};
};

bool parse_ingress_message(const espnow_queue_msg_t& msg, IngressParseResult& out);
bool validate_ingress_message(const IngressParseResult& parsed);
bool dispatch_ingress_message(const IngressParseResult& parsed);
bool process_ingress_message(const espnow_queue_msg_t& msg);

template <typename T>
inline bool validate_struct_size(const espnow_queue_msg_t* msg, const char* label) {
    if (!msg || msg->len < static_cast<int>(sizeof(T))) {
        LOG_WARN("ESPNOW", "%s too short: %d bytes", label, msg ? msg->len : -1);
        return false;
    }
    return true;
}

template <typename T>
inline const T* decode_struct_message(const espnow_queue_msg_t* msg,
                                      const char* label,
                                      uint8_t expected_type = 0xFF) {
    if (!validate_struct_size<T>(msg, label)) {
        return nullptr;
    }

    const auto* payload = reinterpret_cast<const T*>(msg->data);
    if (expected_type != 0xFF && payload->type != expected_type) {
        LOG_WARN("ESPNOW", "%s type mismatch: got=%u expected=%u",
                 label,
                 static_cast<unsigned>(payload->type),
                 static_cast<unsigned>(expected_type));
        return nullptr;
    }

    return payload;
}

template <typename T>
inline const T* decode_crc32_message(const espnow_queue_msg_t* msg,
                                     const char* label,
                                     uint8_t expected_type = 0xFF) {
    const auto* payload = decode_struct_message<T>(msg, label, expected_type);
    if (!payload) {
        return nullptr;
    }

    if (!EspnowPacketUtils::verify_message_crc32(payload)) {
        LOG_WARN("ESPNOW", "%s CRC32 mismatch (stored=0x%08lX)",
                 label,
                 static_cast<unsigned long>(payload->checksum));
        return nullptr;
    }

    return payload;
}

template <typename T>
inline void handle_known_struct_message(const espnow_queue_msg_t* msg, const char* label) {
    const auto* payload = decode_struct_message<T>(msg, label);
    if (!payload) {
        return;
    }

    mark_protocol_activity(msg->mac);
    log_type_once(payload->type, label);
}

void handle_flash_led_message(const espnow_queue_msg_t* msg);
void handle_temperature_report_message(const espnow_queue_msg_t* msg);
void handle_metadata_response_message(const espnow_queue_msg_t* msg);
void handle_network_config_ack_message(const espnow_queue_msg_t* msg);
void handle_mqtt_config_ack_message(const espnow_queue_msg_t* msg);
void handle_version_beacon_message(const espnow_queue_msg_t* msg);
void handle_event_log_summary_message(const espnow_queue_msg_t* msg);
void handle_event_logs_clear_ack_message(const espnow_queue_msg_t* msg);
void handle_time_transitions_snapshot_message(const espnow_queue_msg_t* msg);
void handle_type_catalog_fragment_message(const espnow_queue_msg_t* msg, const char* label);
void handle_packet_subtype_message(const espnow_queue_msg_t* msg, const char* label);
void handle_data_message(const espnow_queue_msg_t* msg);
void handle_battery_status_message(const espnow_queue_msg_t* msg);
void handle_heartbeat_ack_message(const espnow_queue_msg_t* msg);
void on_data_recv(const uint8_t* mac, const uint8_t* data, int len);
void on_data_sent(const uint8_t* mac, esp_now_send_status_t status);
void setup_message_routes();

}  // namespace ESPNowRuntime::Detail