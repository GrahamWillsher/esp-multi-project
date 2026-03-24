#pragma once

#include <Arduino.h>

/**
 * @brief Generate the HTML content for the Inverter Settings page.
 *
 * Includes the page heading, navigation buttons, inverter protocol
 * selector dropdown, interface selector dropdown, and save button.
 * Calls generate_nav_buttons internally.
 *
 * @return String containing the HTML body content (no <script> block).
 */
String get_inverter_settings_page_content();
