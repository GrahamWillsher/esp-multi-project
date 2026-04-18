#include "monitor2_page.h"
#include "monitor2_page_content.h"
#include "monitor2_page_script.h"
#include "../common/page_generator.h"

esp_err_t monitor2_handler(httpd_req_t *req) {
    return send_rendered_page(req, "ESP-NOW Receiver - Battery Monitor (SSE)",
                              get_monitor2_page_content(),
                              PageRenderOptions(get_monitor2_page_styles(), get_monitor2_page_script()));
}

esp_err_t register_monitor2_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri = "/transmitter/monitor2",
        .method = HTTP_GET,
        .handler = monitor2_handler,
        .user_ctx = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
