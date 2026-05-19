#pragma once

/**
 * @brief Generate the HTML content for the Hardware Config page.
 *
 * Includes: breadcrumb, page heading, navigation buttons, Status LED Pattern
 * settings card, Live LED Runtime Status card, and save button.
 * Calls generate_nav_buttons internally. Does NOT include a <script> block.
 *
 * @return Flash-resident HTML body content.
 */
const char* get_hardware_config_page_content();
