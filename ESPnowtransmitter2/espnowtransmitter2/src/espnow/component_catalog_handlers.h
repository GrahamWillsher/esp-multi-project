#pragma once

#include <espnow_transmitter.h>

namespace TxComponentCatalogHandlers {

void handle_component_config(const espnow_queue_msg_t& msg);
void handle_component_interface(const espnow_queue_msg_t& msg);
void handle_request_battery_types(const espnow_queue_msg_t& msg, uint8_t* receiver_mac);
void handle_request_inverter_types(const espnow_queue_msg_t& msg, uint8_t* receiver_mac);
void handle_request_inverter_interfaces(const espnow_queue_msg_t& msg, uint8_t* receiver_mac);
void handle_request_type_catalog_versions(const espnow_queue_msg_t& msg, uint8_t* receiver_mac);

} // namespace TxComponentCatalogHandlers
