#pragma once

#include <Arduino.h>

/**
 * @brief Build the shared-layout HTML header for Inverter Specifications page.
 */
String get_inverter_specs_page_html_header();

/**
 * @brief printf-compatible format template for the Inverter Specifications data grid.
 *
 * Format arguments (in declaration order):
 *   1.  %s   - inverter protocol name
 *   2.  %.1f - min input voltage (V)
 *   3.  %.1f - max input voltage (V)
 *   4.  %.1f - nominal output voltage (V)
 *   5.  %u   - max output power (W)
 *   6.  %.1f - efficiency (%%)
 *   7.  %u   - input phases
 *   8.  %u   - output phases
 *   9.  %s   - Modbus badge CSS class ("enabled" | "disabled")
 *  10.  %s   - Modbus badge label ("✓" | "✗")
 *  11.  %s   - CAN badge CSS class ("enabled" | "disabled")
 *  12.  %s   - CAN badge label ("✓" | "✗")
 *
 * @return Pointer to static format string. Never null.
 */
const char* get_inverter_specs_section_fmt();
