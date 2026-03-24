#pragma once

#include <Arduino.h>
#include <cstddef>

namespace WebserverCommonSpecLayout {

struct SpecPageNavLink {
    const char* href;
    const char* label;
};

String build_spec_page_html_header(const String& page_title,
                                   const String& heading,
                                   const String& subtitle,
                                   const String& source_topic,
                                   const String& gradient_start,
                                   const String& gradient_end,
                                   const String& accent_color);

String build_spec_page_html_footer(const String& nav_links_html,
                                   const String& inline_script = String());

String build_spec_page_nav_links(const SpecPageNavLink* links,
                                 size_t link_count);

} // namespace WebserverCommonSpecLayout
