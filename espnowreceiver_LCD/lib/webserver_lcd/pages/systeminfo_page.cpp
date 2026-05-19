#include "systeminfo_page.h"
#include "systeminfo_page_content.h"
#include "systeminfo_page_script.h"
#include "../common/page_generator.h"
#include <Arduino.h>

// Content generator callback for streaming render of system info page
// Emits content incrementally without building it all in one String
static esp_err_t systeminfo_content_generator(httpd_req_t* req) {
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    const char* content = get_systeminfo_page_content();
    if (!content || content[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    // Route through guarded/adaptive streaming path.
    const esp_err_t rc = send_page_content_chunk(req, "systeminfo_content", content, strlen(content));
    if (rc != ESP_OK) {
        return rc;
    }

    return ESP_OK;
}

/**
 * @brief Handler for the receiver system info page
 * 
 * Shows receiver configuration and system information.
 * 
 * Section 3: Fully fixed solution - uses streaming render to avoid 
 * building large page body in a String.
 */
esp_err_t systeminfo_handler(httpd_req_t *req) {
    const char* script = get_systeminfo_page_script();

    // Use streaming render with callback
    // No temporary String is built for page body or script.
    return send_rendered_page_streaming(req, 
                                       "ESP-NOW Receiver Config", 
                                       systeminfo_content_generator,
                                       PageRenderOptions("", script));
}

/**
 * @brief Register the systeminfo page handler with the HTTP server
 */
esp_err_t register_systeminfo_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/receiver/config",
        .method    = HTTP_GET,
        .handler   = systeminfo_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
