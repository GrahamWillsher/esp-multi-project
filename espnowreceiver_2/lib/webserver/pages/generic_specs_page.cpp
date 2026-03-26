#include "generic_specs_page.h"

#include "../logging.h"

#include <webserver_common_utils/spec_page_layout.h>

#include <stdarg.h>

namespace {

constexpr size_t kDefaultSpecsScratchBufferBytes = 2048;

} // namespace

namespace GenericSpecsPage {

esp_err_t send_formatted_page(httpd_req_t* req,
                              const String& html_header,
                              const char* html_specs_fmt,
                              const String& html_footer,
                              const RenderConfig& config,
                              ...) {
    char stack_specs_buffer[kDefaultSpecsScratchBufferBytes];
    char* specs_section = stack_specs_buffer;
    bool free_specs_section = false;

    if (config.specs_section_size > sizeof(stack_specs_buffer) || config.allocate_specs_section_in_psram) {
        specs_section = static_cast<char*>(ps_malloc(config.specs_section_size));
        if (!specs_section) {
            LOG_ERROR(config.log_tag, "Failed to allocate specs buffer in PSRAM");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
            return ESP_FAIL;
        }
        free_specs_section = true;
    }

    va_list args;
    va_start(args, config);
    vsnprintf(specs_section, config.specs_section_size, html_specs_fmt, args);
    va_end(args);

    const size_t sent_bytes = html_header.length() + strlen(specs_section) + html_footer.length();
    const esp_err_t send_result = WebserverCommonSpecLayout::send_chunked_html_response(
        req,
        html_header,
        specs_section,
        html_footer,
        config.log_tag);

    if (free_specs_section) {
        free(specs_section);
    }

    if (send_result != ESP_OK) {
        LOG_ERROR(config.log_tag, "Failed to send chunked HTML response");
        return ESP_FAIL;
    }

    LOG_INFO(config.log_tag, "Specs page served (%d bytes)", static_cast<int>(sent_bytes));
    return ESP_OK;
}

esp_err_t register_page(httpd_handle_t server,
                        const char* uri,
                        esp_err_t (*handler)(httpd_req_t*)) {
    httpd_uri_t page_uri = {
        .uri = uri,
        .method = HTTP_GET,
        .handler = handler,
        .user_ctx = NULL,
    };
    return httpd_register_uri_handler(server, &page_uri);
}

} // namespace GenericSpecsPage
