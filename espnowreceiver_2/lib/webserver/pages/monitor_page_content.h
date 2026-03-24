#pragma once

#include <Arduino.h>

/**
 * @brief Generate the HTML content for the Battery Monitor page (polling version).
 * @return String containing the page HTML body (no <html>/<body>/<head> tags).
 */
String get_monitor_page_content();
