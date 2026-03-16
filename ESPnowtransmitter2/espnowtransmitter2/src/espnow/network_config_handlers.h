#pragma once

#include <espnow_transmitter.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace TxNetworkConfigHandlers {

void handle_network_config_request(const espnow_queue_msg_t& msg, uint8_t* receiver_mac);
void handle_network_config_update(const espnow_queue_msg_t& msg, uint8_t* receiver_mac, QueueHandle_t network_config_queue);
void send_network_config_ack(const uint8_t* receiver_mac, bool success, const char* message);
void process_network_config_update(const espnow_queue_msg_t& msg);

} // namespace TxNetworkConfigHandlers
