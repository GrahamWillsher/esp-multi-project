#include "systeminfo_page.h"
#include "systeminfo_page_content.h"
#include "systeminfo_page_script.h"
#include "../common/page_generator.h"
#include <Arduino.h>

// System info page handler - System information page
esp_err_t systeminfo_handler(httpd_req_t *req) {
    String content = get_systeminfo_page_content();
    String script = get_systeminfo_page_script();

    return send_rendered_page(req, "ESP-NOW Receiver Config", content, PageRenderOptions("", script));
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
