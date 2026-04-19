#pragma once

#include <Arduino.h>
#include <atomic>
#include <esp_now.h>
#include <esp32common/espnow/common.h>
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
extern std::atomic<bool> g_discovery_started;
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

template <typename T>
inline bool validate_struct_size(const espnow_queue_msg_t* msg, const char* label) {
    if (!msg || msg->len < static_cast<int>(sizeof(T))) {
        LOG_WARN("ESPNOW", "%s too short: %d bytes", label, msg ? msg->len : -1);
        return false;
    }
    return true;
}

template <typename T>
inline void handle_known_struct_message(const espnow_queue_msg_t* msg, const char* label) {
    if (!validate_struct_size<T>(msg, label)) {
        return;
    }

    const auto* payload = reinterpret_cast<const T*>(msg->data);
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
void handle_heartbeat_message(const espnow_queue_msg_t* msg);
void handle_heartbeat_ack_message(const espnow_queue_msg_t* msg);
void on_data_recv(const uint8_t* mac, const uint8_t* data, int len);
void on_data_sent(const uint8_t* mac, esp_now_send_status_t status);
void setup_message_routes();

}  // namespace ESPNowRuntime::Detail