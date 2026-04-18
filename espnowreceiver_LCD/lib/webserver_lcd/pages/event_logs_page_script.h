#pragma once

#include <Arduino.h>

/**
 * @brief Generate the CSS styles for the Event Logs page.
 * @return Pointer to static page-specific CSS (no <style> tags).
 */
const char* get_event_logs_page_styles();

/**
 * @brief Generate the JavaScript for the Event Logs page.
 *
 * Includes: loadEvents() polling /api/get_event_logs?limit=100,
 * subscribe/unsubscribe lifecycle on /api/event_logs/subscribe and
 * /api/event_logs/unsubscribe, and 5-second auto-refresh.
 *
 * @return Pointer to static JavaScript (no <script> tags).
 */
const char* get_event_logs_page_script();
