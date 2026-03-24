#pragma once

#include <Arduino.h>

/**
 * @brief Generate the HTML content for the Debug Logging Control page.
 * @return String containing the page HTML body (no <html>/<body>/<head> tags).
 */
String get_debug_page_content();
