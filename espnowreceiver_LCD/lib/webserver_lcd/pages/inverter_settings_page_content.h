#pragma once

/**
 * @brief Generate the HTML content for the Inverter Settings page.
 *
 * Includes the page heading, navigation buttons, inverter protocol
 * selector dropdown, interface selector dropdown, and save button.
 * Calls generate_nav_buttons internally.
 *
 * @return Flash-resident HTML body content (no <script> block).
 */
const char* get_inverter_settings_page_content();
