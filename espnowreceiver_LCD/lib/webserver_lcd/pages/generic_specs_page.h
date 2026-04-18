#pragma once

#include <esp_http_server.h>
#include <Arduino.h>

namespace GenericSpecsPage {

struct RenderConfig {
    const char* log_tag;
    size_t specs_section_size;
    size_t total_slack_bytes;
    bool allocate_specs_section_in_psram;
};

esp_err_t send_formatted_page(httpd_req_t* req,
                              const String& html_header,
                              const char* html_specs_fmt,
                              const String& html_footer,
                              const RenderConfig& config,
                              ...);

esp_err_t register_page(httpd_handle_t server,
                        const char* uri,
                        esp_err_t (*handler)(httpd_req_t*));

} // namespace GenericSpecsPage
