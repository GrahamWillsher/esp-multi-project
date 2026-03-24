#pragma once

#include <Arduino.h>

/**
 * @brief Generate the HTML content for the Cell Monitor page.
 *
 * @return String containing the HTML body content (no <script> block).
 */
String get_cellmonitor_page_content();
