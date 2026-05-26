#include "monitor2_page.h"
#include "monitor2_page_content.h"
#include "monitor2_page_script.h"
#include "../common/page_generator.h"

// Static HTML content emitter for monitor2 page
static esp_err_t monitor2_content_generator(httpd_req_t* req) {
    const char* content = get_monitor2_page_content();
    return send_page_content_chunk(req, "monitor2_content", content, strlen(content));
}

esp_err_t monitor2_handler(httpd_req_t *req) {
    const char* title = "Battery Emulator Receiver - Battery Monitor (SSE)";
    PageRenderOptions options(get_monitor2_page_styles(), get_monitor2_page_script());
    
    return send_rendered_page_streaming(req, title, monitor2_content_generator, options);
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
