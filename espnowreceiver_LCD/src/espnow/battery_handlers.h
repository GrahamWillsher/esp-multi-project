#pragma once

#include <esp32common/espnow/common.h>

bool handle_battery_status(const espnow_queue_msg_t* msg);
bool handle_battery_info(const espnow_queue_msg_t* msg);
bool handle_charger_status(const espnow_queue_msg_t* msg);
bool handle_inverter_status(const espnow_queue_msg_t* msg);
bool handle_system_status(const espnow_queue_msg_t* msg);
bool handle_component_config(const espnow_queue_msg_t* msg);

bool validate_checksum(const void* data, size_t len);
