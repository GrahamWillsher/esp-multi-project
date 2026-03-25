#pragma once

#include <Arduino.h>
#include <cstddef>
#include <esp_err.h>
#include <esp_http_server.h>

namespace WebserverCommonSpecLayout {

struct SpecPageNavLink {
    const char* href;
    const char* label;
};

/// Bundle of all static parameters needed to render a spec page header.
/// All pointer fields must remain valid for the duration of the send call.
struct SpecPageParams {
    const char* page_title;      ///< Browser <title> text
    const char* heading;         ///< <h1> heading (may include HTML entities / UTF-8 emoji)
    const char* subtitle;        ///< Subtitle <p> text
    const char* source_topic;    ///< MQTT source topic shown in the source-info banner
    const char* gradient_start;  ///< CSS linear-gradient start colour (e.g. "#667eea")
    const char* gradient_end;    ///< CSS linear-gradient end colour
    const char* accent_color;    ///< CSS accent colour used for borders, buttons, and badges
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

esp_err_t send_chunked_html_response(httpd_req_t* req,
                                     const String& html_header,
                                     const char* html_body,
                                     const String& html_footer,
                                     const char* log_tag = nullptr);

/// Send a complete spec page (header → specs body → nav footer) via HTTP chunked
/// transfer.  No String heap allocation is performed; all static page sections
/// are sent directly from flash.
///
/// @param inline_script  Optional JavaScript block appended before </body>.  Pass nullptr to omit.
/// @param specs_buf_size Scratch buffer size for the formatted specs section (bytes).
/// @param use_psram      Allocate the specs scratch buffer from PSRAM instead of the stack.
/// @param log_tag        Diagnostic log tag (may be nullptr).
/// @param ...            printf-style format arguments for specs_fmt.
esp_err_t send_spec_page_response(httpd_req_t*           req,
                                   const SpecPageParams&  params,
                                   const SpecPageNavLink* nav_links,
                                   size_t                 nav_link_count,
                                   const char*            inline_script,
                                   const char*            specs_fmt,
                                   size_t                 specs_buf_size,
                                   bool                   use_psram,
                                   const char*            log_tag,
                                   ...);

} // namespace WebserverCommonSpecLayout
