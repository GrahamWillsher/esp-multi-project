#pragma once

#include <espnow_transmitter.h>

namespace TxControlHandlers {

void handle_reboot(const espnow_queue_msg_t& msg);
void handle_ota_start(const espnow_queue_msg_t& msg);
void handle_debug_control(const espnow_queue_msg_t& msg, uint8_t* receiver_mac);
void send_debug_ack(const uint8_t* receiver_mac, uint8_t applied, uint8_t previous, uint8_t status);
void save_debug_level(uint8_t level);
uint8_t load_debug_level();

} // namespace TxControlHandlers
