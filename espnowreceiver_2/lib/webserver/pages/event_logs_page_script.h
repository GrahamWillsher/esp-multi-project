#pragma once

#include <Arduino.h>

/**
 * @brief Generate the CSS styles for the Event Logs page.
 * @return String containing page-specific CSS (no <style> tags).
 */
String get_event_logs_page_styles();

/**
 * @brief Generate the JavaScript for the Event Logs page.
 *
 * Includes: loadEvents() polling /api/get_event_logs?limit=100,
 * subscribe/unsubscribe lifecycle on /api/event_logs/subscribe and
 * /api/event_logs/unsubscribe, and 5-second auto-refresh.
 *
 * @return String containing the JavaScript (no <script> tags).
 */
String get_event_logs_page_script();
