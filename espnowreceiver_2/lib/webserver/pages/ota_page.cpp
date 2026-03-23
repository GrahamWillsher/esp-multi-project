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
    String script = get_ota_page_script();

    String html = renderPage("ESP-NOW Receiver - OTA Update", content, PageRenderOptions("", script));
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

/**
 * @brief Register the OTA page handler with the HTTP server
 */
esp_err_t register_ota_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/ota",
        .method    = HTTP_GET,
        .handler   = ota_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
