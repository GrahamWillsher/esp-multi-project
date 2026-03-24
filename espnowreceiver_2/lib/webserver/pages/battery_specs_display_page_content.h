#pragma once

#include <Arduino.h>

/**
 * @brief Build the shared-layout HTML header for Battery Specifications page.
 */
String get_battery_specs_page_html_header();

/**
 * @brief printf-compatible format template for the Battery Specifications data grid.
 *
 * Format arguments (in declaration order):
 *   1.  %s   - battery type name
 *   2.  %lu  - nominal capacity (Wh)
 *   3.  %.1f - max design voltage (V)
 *   4.  %.1f - min design voltage (V)
 *   5.  %d   - number of cells
 *   6.  %.1f - max charge current (A)
 *   7.  %.1f - max discharge current (A)
 *   8.  %d   - battery chemistry index
 *
 * @return Pointer to static format string. Never null.
 */
const char* get_battery_specs_section_fmt();
