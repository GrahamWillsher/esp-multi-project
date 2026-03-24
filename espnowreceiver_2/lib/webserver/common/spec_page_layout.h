#pragma once

#include <Arduino.h>
#include <cstddef>
#include <webserver_common_utils/spec_page_layout.h>

using SpecPageNavLink = WebserverCommonSpecLayout::SpecPageNavLink;

/**
 * @brief Build the common HTML/CSS header block used by spec display pages.
 */
inline String build_spec_page_html_header(const String& page_title,
                                          const String& heading,
                                          const String& subtitle,
                                          const String& source_topic,
                                          const String& gradient_start,
                                          const String& gradient_end,
                                          const String& accent_color) {
    return WebserverCommonSpecLayout::build_spec_page_html_header(
        page_title,
        heading,
        subtitle,
        source_topic,
        gradient_start,
        gradient_end,
        accent_color);
}

/**
 * @brief Build the common footer block (nav + optional script + closing tags).
 */
inline String build_spec_page_html_footer(const String& nav_links_html,
                                          const String& inline_script = String()) {
    return WebserverCommonSpecLayout::build_spec_page_html_footer(nav_links_html, inline_script);
}

/**
 * @brief Generate spec-page navigation link HTML from a declarative config table.
 */
inline String build_spec_page_nav_links(const SpecPageNavLink* links,
                                        size_t link_count) {
    return WebserverCommonSpecLayout::build_spec_page_nav_links(links, link_count);
}
