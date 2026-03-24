#include "inverter_settings_page.h"
#include "inverter_settings_page_content.h"
#include "inverter_settings_page_script.h"
#include "../common/page_generator.h"
#include <Arduino.h>

/**
 * @brief Handler for the /transmitter/inverter page
 *
 * Inverter type and interface selection page. Users can view and modify the
 * inverter protocol sent to the transmitter.
 */
static esp_err_t inverter_settings_handler(httpd_req_t *req) {
    const String content = get_inverter_settings_page_content();
    const String script  = get_inverter_settings_page_script();
    return send_rendered_page(req, "ESP-NOW Receiver - Inverter Settings", content, PageRenderOptions("", script));
}

/**
 * @brief Register the inverter settings page handler
 */
esp_err_t register_inverter_settings_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/transmitter/inverter",
        .method    = HTTP_GET,
        .handler   = inverter_settings_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}