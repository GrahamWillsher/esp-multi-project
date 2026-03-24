#pragma once

#include <Arduino.h>

/**
 * @brief Generate the CSS styles for the Debug Logging Control page.
 * @return String containing page-specific CSS (no <style> tags).
 */
String get_debug_page_styles();

/**
 * @brief Generate the JavaScript for the Debug Logging Control page.
 *
 * Includes: loadCurrentDebugLevel() polling /api/debugLevel and
 * setDebugLevel() posting to /api/setDebugLevel?level=N.
 *
 * @return String containing the JavaScript (no <script> tags).
 */
String get_debug_page_script();
