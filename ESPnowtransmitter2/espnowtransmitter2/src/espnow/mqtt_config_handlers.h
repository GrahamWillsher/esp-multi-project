#pragma once

#include <espnow_transmitter.h>

namespace TxMqttConfigHandlers {

void handle_mqtt_config_request(const espnow_queue_msg_t& msg, uint8_t* receiver_mac);
void handle_mqtt_config_update(const espnow_queue_msg_t& msg, uint8_t* receiver_mac);
void send_mqtt_config_ack(const uint8_t* receiver_mac, bool success, const char* message);

} // namespace TxMqttConfigHandlers
