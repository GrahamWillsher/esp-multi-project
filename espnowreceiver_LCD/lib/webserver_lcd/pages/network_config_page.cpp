#include "network_config_page.h"
#include "network_config_page_content.h"
#include "network_config_page_style.h"
#include "network_config_page_script.h"
#include "../common/page_generator.h"

#include <Arduino.h>
#include <WiFi.h>

// Network configuration page handler - WiFi and IP settings
esp_err_t network_config_handler(httpd_req_t *req) {
    const bool isAPMode = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);

    const String content = get_network_config_page_content(isAPMode);
    const String style = get_network_config_page_style();
    const String script = get_network_config_page_script();

    const String pageTitle = isAPMode ? "ESP32 Setup" : "Network Configuration";

    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    return send_rendered_page(req, pageTitle.c_str(), content, PageRenderOptions(style, script));
}

esp_err_t register_network_config_page(httpd_handle_t server) {
    httpd_uri_t uri_handler = {
        .uri = "/receiver/network",
        .method = HTTP_GET,
        .handler = network_config_handler,
        .user_ctx = NULL
    };
    return httpd_register_uri_handler(server, &uri_handler);
}
