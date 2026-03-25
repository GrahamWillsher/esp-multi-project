#include "battery_settings_page.h"
#include "battery_settings_page_content.h"
#include "battery_settings_page_script.h"
#include "../common/page_generator.h"
#include <Arduino.h>

/**
 * @brief Handler for the /battery_settings page
 * 
 * Phase 2: Bidirectional settings page with Save buttons
 * Users can view and modify battery settings that are sent to the transmitter
 */
static esp_err_t battery_settings_handler(httpd_req_t *req) {
    String content = get_battery_settings_page_content();
    const char* script = get_battery_settings_page_script();

    return send_rendered_page(req, "ESP-NOW Receiver - Battery Settings", content, PageRenderOptions("", script));
}

/**
 * @brief Register the battery settings page handler
 */
esp_err_t register_battery_settings_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/transmitter/battery",
        .method    = HTTP_GET,
        .handler   = battery_settings_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
