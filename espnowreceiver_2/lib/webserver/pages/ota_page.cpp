#include "ota_page.h"
#include "ota_page_content.h"
#include "ota_page_script.h"
#include "../common/page_generator.h"
#include <Arduino.h>

/**
 * @brief Handler for the OTA firmware upload page
 * 
 * This page allows users to upload firmware files to the transmitter via HTTP.
 * Features file selection, upload progress tracking, and automatic redirect on success.
 */
static esp_err_t ota_handler(httpd_req_t *req) {
    String content = get_ota_page_content();
    const char* script = get_ota_page_script();

    return send_rendered_page(req,
                              "OTA Firmware Update",
                              content,
                              PageRenderOptions("", script, false));
}

/**
 * @brief Register the OTA page handler with the HTTP server
 */
esp_err_t register_ota_page(httpd_handle_t server) {
    // Primary route (required)
    httpd_uri_t uri_primary = {
        .uri       = "/systemtools/ota",
        .method    = HTTP_GET,
        .handler   = ota_handler,
        .user_ctx  = NULL
    };

    const esp_err_t primary_result = httpd_register_uri_handler(server, &uri_primary);
    if (primary_result != ESP_OK) {
        return primary_result;
    }

    return ESP_OK;
}
