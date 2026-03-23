#pragma once

// Internal shared declarations for the espnow_tasks compilation unit family.
// Included by espnow_tasks.cpp, espnow_message_handlers.cpp, and espnow_settings_sync.cpp.
// Not intended for use outside this module.

#include <esp32common/espnow/common.h>

// Shared helpers — defined in espnow_tasks.cpp
void apply_telemetry_sample(const uint8_t* mac, uint8_t soc, int32_t power,
                             const char* source, bool include_first_data_log);
void store_transmitter_mac(const uint8_t* mac);

// Message handler declarations — defined in espnow_message_handlers.cpp
void handle_data_message(const espnow_queue_msg_t* msg);
void handle_flash_led_message(const espnow_queue_msg_t* msg);
void handle_debug_ack_message(const espnow_queue_msg_t* msg);
void handle_packet_events(const espnow_queue_msg_t* msg);
void handle_packet_logs(const espnow_queue_msg_t* msg);
void handle_packet_cell_info(const espnow_queue_msg_t* msg);
void handle_packet_unknown(const espnow_queue_msg_t* msg, uint8_t subtype);

// Settings sync handler declarations — defined in espnow_settings_sync.cpp
void handle_settings_update_ack(const espnow_queue_msg_t* msg);
void handle_settings_changed(const espnow_queue_msg_t* msg);
void handle_component_apply_ack_message(const espnow_queue_msg_t* msg);
