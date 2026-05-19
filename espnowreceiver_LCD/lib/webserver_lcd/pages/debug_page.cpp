#include "debug_page.h"
#include "debug_page_content.h"
#include "debug_page_script.h"
#include "../common/page_generator.h"

// Debug page handler
static esp_err_t debug_page_handler(httpd_req_t *req) {
    auto content_generator = [](httpd_req_t* request) -> esp_err_t {
        const char* content = get_debug_page_content();
        return send_page_content_chunk(request, "debug_content", content, strlen(content));
    };

    return send_rendered_page_streaming(req,
                                        "Debug Logging Control",
                                        content_generator,
                                        PageRenderOptions(get_debug_page_styles(), get_debug_page_script()));
}

// Register debug page
esp_err_t register_debug_page(httpd_handle_t server) {
    httpd_uri_t debug_uri = {
        .uri = "/debug",
        .method = HTTP_GET,
        .handler = debug_page_handler,
        .user_ctx = NULL
    };
    return httpd_register_uri_handler(server, &debug_uri);
}
