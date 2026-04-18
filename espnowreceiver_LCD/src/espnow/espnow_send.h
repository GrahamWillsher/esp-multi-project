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
 * @brief Send batched component apply request (types/interfaces) with correlation ID
 */
bool send_component_apply_request(uint32_t request_id,
                                  uint8_t apply_mask,
                                  uint8_t battery_type,
                                  uint8_t inverter_type,
                                  uint8_t battery_interface,
                                  uint8_t inverter_interface);

/**
 * @brief Send event logs subscription control to transmitter via ESP-NOW
 */
bool send_event_logs_control(bool subscribe);

/**
 * @brief Send event logs clear command to transmitter via ESP-NOW
 */
bool send_event_logs_clear_request();

/**
 * @brief Request current LED state snapshot from transmitter via ESP-NOW
 */
bool send_led_state_request();

/**
 * @brief Request battery type catalog from transmitter
 */
bool send_battery_types_request();

/**
 * @brief Request inverter type catalog from transmitter
 */
bool send_inverter_types_request();

/**
 * @brief Request inverter interface catalog from transmitter
 */
bool send_inverter_interfaces_request();

/**
 * @brief Request current battery/inverter catalog versions from transmitter
 */
bool send_type_catalog_versions_request();

/**
 * @brief Send test data mode control to transmitter via ESP-NOW
 * @param mode Test data mode (0=OFF, 1=SOC_POWER_ONLY, 2=FULL_BATTERY_DATA)
 */
bool send_test_data_mode_control(uint8_t mode);

/**
 * @brief Get the last test data mode sent to transmitter (cached locally)
 */
uint8_t get_last_test_data_mode();
