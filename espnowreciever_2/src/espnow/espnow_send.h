#pragma once

#include <Arduino.h>

/**
 * @brief Send debug level control message to transmitter via ESP-NOW
 * @param level Debug level (0-7: EMERG, ALERT, CRIT, ERROR, WARNING, NOTICE, INFO, DEBUG)
 * @return true if sent successfully, false otherwise
 */
bool send_debug_level_control(uint8_t level);
