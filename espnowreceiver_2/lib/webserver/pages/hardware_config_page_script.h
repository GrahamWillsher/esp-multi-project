#pragma once

#include <Arduino.h>

/**
 * @brief Generate the JavaScript for the Hardware Config page.
 *
 * Includes: LED mode change tracking, live LED status polling
 * (/api/get_led_runtime_status), hardware settings loader
 * (/api/get_battery_settings), save handler (/api/save_setting),
 * LED resync request (/api/resync_led_state), and 2-second poll interval.
 *
 * @return String containing the JavaScript (no <script> tags).
 */
String get_hardware_config_page_script();
