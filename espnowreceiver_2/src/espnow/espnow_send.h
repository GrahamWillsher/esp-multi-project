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
 * @param request_id Receiver-generated request ID
 * @param apply_mask Bitmask of fields to apply (component_apply_mask_t)
 * @param battery_type Desired battery type
 * @param inverter_type Desired inverter type
 * @param battery_interface Desired battery interface
 * @param inverter_interface Desired inverter interface
 * @return true if sent successfully, false otherwise
 */
bool send_component_apply_request(uint32_t request_id,
								  uint8_t apply_mask,
								  uint8_t battery_type,
								  uint8_t inverter_type,
								  uint8_t battery_interface,
								  uint8_t inverter_interface);

/**
 * @brief Send event logs subscription control to transmitter via ESP-NOW
 * @param subscribe true to subscribe, false to unsubscribe
 * @return true if sent successfully, false otherwise
 */
bool send_event_logs_control(bool subscribe);

/**
 * @brief Request battery type catalog from transmitter
 * @return true if request sent successfully
 */
bool send_battery_types_request();

/**
 * @brief Request inverter type catalog from transmitter
 * @return true if request sent successfully
 */
bool send_inverter_types_request();

/**
 * @brief Request inverter interface catalog from transmitter
 * @return true if request sent successfully
 */
bool send_inverter_interfaces_request();

/**
 * @brief Request current battery/inverter catalog versions from transmitter
 * @return true if request sent successfully
 */
bool send_type_catalog_versions_request();

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