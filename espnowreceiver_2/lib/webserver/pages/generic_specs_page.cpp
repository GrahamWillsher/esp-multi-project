#include "generic_specs_page.h"

#include "../logging.h"

#include <stdarg.h>

namespace GenericSpecsPage {

esp_err_t send_formatted_page(httpd_req_t* req,
                              const String& html_header,
                              const char* html_specs_fmt,
                              const String& html_footer,
                              const RenderConfig& config,
                              ...) {
    size_t total_size = html_header.length() + config.specs_section_size + html_footer.length() + config.total_slack_bytes;
    char* response = static_cast<char*>(ps_malloc(total_size));
    if (!response) {
        LOG_ERROR("[%s] Failed to allocate %d bytes in PSRAM", config.log_tag, total_size);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    char stack_specs_buffer[2048];
    char* specs_section = stack_specs_buffer;
    bool free_specs_section = false;

    if (config.specs_section_size > sizeof(stack_specs_buffer) || config.allocate_specs_section_in_psram) {
        specs_section = static_cast<char*>(ps_malloc(config.specs_section_size));
        if (!specs_section) {
            free(response);
            LOG_ERROR("[%s] Failed to allocate specs buffer in PSRAM", config.log_tag);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
            return ESP_FAIL;
        }
        free_specs_section = true;
    }

    va_list args;
    va_start(args, config);
    vsnprintf(specs_section, config.specs_section_size, html_specs_fmt, args);
    va_end(args);

    size_t offset = 0;
    offset += snprintf(response + offset, total_size - offset, "%s", html_header.c_str());
    offset += snprintf(response + offset, total_size - offset, "%s", specs_section);
    offset += snprintf(response + offset, total_size - offset, "%s", html_footer.c_str());

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, response, strlen(response));

    if (free_specs_section) {
        free(specs_section);
    }
    free(response);

    LOG_INFO("[%s] Specs page served (%d bytes)", config.log_tag, offset);
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
