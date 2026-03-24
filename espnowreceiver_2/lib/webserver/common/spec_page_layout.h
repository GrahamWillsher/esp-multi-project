#pragma once

#include <Arduino.h>

/**
 * @brief Build the common HTML/CSS header block used by spec display pages.
 */
String build_spec_page_html_header(const String& page_title,
                                   const String& heading,
                                   const String& subtitle,
                                   const String& source_topic,
                                   const String& gradient_start,
                                   const String& gradient_end,
                                   const String& accent_color);

/**
 * @brief Build the common footer block (nav + optional script + closing tags).
 */
String build_spec_page_html_footer(const String& nav_links_html,
                                   const String& inline_script = String());
