#pragma once

#include <Arduino.h>

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