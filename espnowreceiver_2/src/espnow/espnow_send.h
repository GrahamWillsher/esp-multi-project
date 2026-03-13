#pragma once

#include <Arduino.h>

/**
 * @brief Get the last debug level sent to transmitter
 * @return Current debug level (0-7)
 */
uint8_t get_last_debug_level();

/**
 * @brief Send debug level control message to transmitter via ESP-NOW
 * @param level Debug level (0-7: EMERG, ALERT, CRIT, ERROR, WARNING, NOTICE, INFO, DEBUG)
 * @return true if sent successfully, false otherwise
 */
bool send_debug_level_control(uint8_t level);
/**
 * @brief Send component type selection (battery and inverter types) to transmitter via ESP-NOW
 * @param battery_type Battery profile type (0-31, 29=PYLON_BATTERY default)
 * @param inverter_type Inverter type (0-21, 0=NONE default)
 * @return true if sent successfully, false otherwise
 */
bool send_component_type_selection(uint8_t battery_type, uint8_t inverter_type);

/**
 * @brief Send component interface selection (battery and inverter interfaces) to transmitter via ESP-NOW
 * @param battery_interface Battery comm interface (0-5)
 * @param inverter_interface Inverter comm interface (0-5)
 * @return true if sent successfully, false otherwise
 */
bool send_component_interface_selection(uint8_t battery_interface, uint8_t inverter_interface);

/**
 * @brief Send event logs subscription control to transmitter via ESP-NOW
 * @param subscribe true to subscribe, false to unsubscribe
 * @return true if sent successfully, false otherwise
 */
bool send_event_logs_control(bool subscribe);

/**
 * @brief Send test data mode control to transmitter via ESP-NOW
 * @param mode Test data mode (0=OFF, 1=SOC_POWER_ONLY, 2=FULL_BATTERY_DATA)
 * @return true if sent successfully, false otherwise
 */
bool send_test_data_mode_control(uint8_t mode);

/**
 * @brief Get the last test data mode sent to transmitter (cached locally)
 * @return uint8_t Last mode sent (0-2)
 */
uint8_t get_last_test_data_mode();