#pragma once

#include <Arduino.h>

/**
 * @brief Generate the CSS styles for the Battery Monitor page (SSE version).
 * @return String containing page-specific CSS (no <style> tags).
 */
String get_monitor2_page_styles();

/**
 * @brief Generate the JavaScript for the Battery Monitor page (SSE version).
 *
 * Manages an EventSource connection to /api/monitor_sse with exponential
 * backoff reconnection and a 30-second health-check interval.
 *
 * @return String containing the JavaScript (no <script> tags).
 */
String get_monitor2_page_script();
