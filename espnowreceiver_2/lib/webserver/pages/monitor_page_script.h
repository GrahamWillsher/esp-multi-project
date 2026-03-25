#pragma once

#include <Arduino.h>

/**
 * @brief Generate the CSS styles for the Battery Monitor page (polling version).
 * @return Pointer to static page-specific CSS (no <style> tags).
 */
const char* get_monitor_page_styles();

/**
 * @brief Generate the JavaScript for the Battery Monitor page (polling version).
 *
 * Polls /api/monitor every 1 second to update SOC, Power, and Voltage displays.
 *
 * @return Pointer to static JavaScript (no <script> tags).
 */
const char* get_monitor_page_script();
