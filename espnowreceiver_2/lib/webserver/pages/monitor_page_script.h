#pragma once

#include <Arduino.h>

/**
 * @brief Generate the CSS styles for the Battery Monitor page (polling version).
 * @return String containing page-specific CSS (no <style> tags).
 */
String get_monitor_page_styles();

/**
 * @brief Generate the JavaScript for the Battery Monitor page (polling version).
 *
 * Polls /api/monitor every 1 second to update SOC, Power, and Voltage displays.
 *
 * @return String containing the JavaScript (no <script> tags).
 */
String get_monitor_page_script();
