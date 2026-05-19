#pragma once

#include <Arduino.h>

/**
 * @brief Generate the CSS styles for the Battery Monitor page.
 * @return Pointer to static page-specific CSS (no <style> tags).
 */
const char* get_monitor2_page_styles();

/**
 * @brief Generate the JavaScript for the Battery Monitor page.
 *
 * Uses snapshot polling from /api/monitor with adaptive backoff and
 * stale-data visibility.
 *
 * @return Pointer to static JavaScript (no <script> tags).
 */
const char* get_monitor2_page_script();
