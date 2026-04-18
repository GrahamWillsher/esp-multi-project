#pragma once

#include <Arduino.h>

/**
 * @brief Generate the HTML content for the Hardware Config page.
 *
 * Includes: breadcrumb, page heading, navigation buttons, Status LED Pattern
 * settings card, Live LED Runtime Status card, and save button.
 * Calls generate_nav_buttons internally. Does NOT include a <script> block.
 *
 * @return String containing the HTML body content.
 */
String get_hardware_config_page_content();
