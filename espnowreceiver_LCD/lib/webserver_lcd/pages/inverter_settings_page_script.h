#pragma once

#include <Arduino.h>

/**
 * @brief Generate the JavaScript for the Inverter Settings page.
 *
 * Includes: inverter type dropdown population (with retry), interface dropdown
 * population (with retry), current selection loading, change tracking,
 * save button state management, and SaveOperation.runComponentApply call.
 *
 * @return String containing the JavaScript (no <script> tags).
 */
String get_inverter_settings_page_script();
