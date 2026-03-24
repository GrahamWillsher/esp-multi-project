#pragma once

#include <Arduino.h>

/**
 * @brief Generate the HTML content for the Event Logs page.
 * @return String containing the page HTML body (no <html>/<body>/<head> tags).
 */
String get_event_logs_page_content();
