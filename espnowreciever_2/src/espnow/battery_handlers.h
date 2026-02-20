/**
 * Battery Emulator Data Handlers - Phase 1
 * 
 * Handles battery/charger/inverter/system status messages from transmitter
 * and forwards data to web UI via SSE and displays on TFT
 */

#ifndef BATTERY_HANDLERS_H
#define BATTERY_HANDLERS_H

#include <espnow_common.h>
#include "../common.h"

/**
 * @brief Handle battery status message
 * @param msg ESP-NOW message from queue
 */
void handle_battery_status(const espnow_queue_msg_t* msg);

/**
 * @brief Handle battery info message (static data)
 * @param msg ESP-NOW message from queue
 */
void handle_battery_info(const espnow_queue_msg_t* msg);

/**
 * @brief Handle charger status message
 * @param msg ESP-NOW message from queue
 */
void handle_charger_status(const espnow_queue_msg_t* msg);

/**
 * @brief Handle inverter status message
 * @param msg ESP-NOW message from queue
 */
void handle_inverter_status(const espnow_queue_msg_t* msg);

/**
 * @brief Handle system status message
 * @param msg ESP-NOW message from queue
 */
void handle_system_status(const espnow_queue_msg_t* msg);

/**
 * @brief Handle component configuration message
 * @param msg ESP-NOW message from queue
 */
void handle_component_config(const espnow_queue_msg_t* msg);

/**
 * @brief Validate message checksum
 * @param data Pointer to message data
 * @param len Length of message (including checksum field)
 * @return true if checksum is valid, false otherwise
 */
bool validate_checksum(const void* data, size_t len);

#endif // BATTERY_HANDLERS_H
