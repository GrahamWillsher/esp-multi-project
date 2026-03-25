#pragma once

#include <Arduino.h>

/**
 * @brief Generate the CSS styles for the Debug Logging Control page.
 * @return Pointer to static page-specific CSS (no <style> tags).
 */
const char* get_debug_page_styles();

/**
 * @brief Generate the JavaScript for the Debug Logging Control page.
 *
 * Includes: loadCurrentDebugLevel() polling /api/debugLevel and
 * setDebugLevel() posting to /api/setDebugLevel?level=N.
 *
 * @return Pointer to static JavaScript (no <script> tags).
 */
const char* get_debug_page_script();
