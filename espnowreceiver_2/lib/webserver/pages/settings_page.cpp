#include "settings_page.h"
#include "settings_page_content.h"
#include "settings_page_script.h"
#include "../common/page_generator.h"
#include <Arduino.h>

/**
 * @brief Handler for the transmitter configuration page
 * 
 * This is the most complex page - displays all configuration settings from the transmitter.
 * Features API-driven population, dynamic visibility controls, and transmitter config editing.
 */
static esp_err_t root_handler(httpd_req_t *req) {
    String content = get_settings_page_content();

    String script = get_settings_page_script();

    String html = renderPage("ESP-NOW Receiver - Settings", content, PageRenderOptions("", script));
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

/**
 * @brief Register the settings page handler with the HTTP server
 */
esp_err_t register_settings_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/transmitter/config",
        .method    = HTTP_GET,
        .handler   = root_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
