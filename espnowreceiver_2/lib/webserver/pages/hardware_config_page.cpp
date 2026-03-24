#include "hardware_config_page.h"
#include "hardware_config_page_content.h"
#include "hardware_config_page_script.h"
#include "../common/page_generator.h"
#include <Arduino.h>

/**
 * @brief Handler for the /transmitter/hardware page
 *
 * Hardware-adjacent transmitter controls (Status LED pattern) synchronized
 * from the receiver cache and saved back via ESP-NOW settings updates.
 */
static esp_err_t hardware_config_handler(httpd_req_t *req) {
    const String content = get_hardware_config_page_content();
    const String script  = get_hardware_config_page_script();
    return send_rendered_page(req, "Hardware Config", content, PageRenderOptions("", script));
}

esp_err_t register_hardware_config_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/transmitter/hardware",
        .method    = HTTP_GET,
        .handler   = hardware_config_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}