#include "monitor_page.h"
#include "monitor_page_content.h"
#include "monitor_page_script.h"
#include "../common/page_generator.h"

/**
 * @brief Handler for the battery monitor page (non-SSE version)
 *
 * This page displays battery SOC and Power with 1-second auto-refresh via polling.
 * Unlike monitor2_page which uses SSE, this uses interval-based fetch requests.
 */
static esp_err_t monitor_handler(httpd_req_t *req) {
    return send_rendered_page(req, "ESP-NOW Receiver - Battery Monitor",
                              get_monitor_page_content(),
                              PageRenderOptions(get_monitor_page_styles(), get_monitor_page_script()));
}

/**
 * @brief Register the monitor page handler with the HTTP server
 */
esp_err_t register_monitor_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/transmitter/monitor",
        .method    = HTTP_GET,
        .handler   = monitor_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
