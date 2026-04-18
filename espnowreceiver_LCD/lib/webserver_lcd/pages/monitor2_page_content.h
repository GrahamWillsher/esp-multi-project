#pragma once

#include <Arduino.h>

/**
 * @brief Generate the HTML content for the Battery Monitor page (SSE version).
 * @return String containing the page HTML body (no <html>/<body>/<head> tags).
 */
String get_monitor2_page_content();
