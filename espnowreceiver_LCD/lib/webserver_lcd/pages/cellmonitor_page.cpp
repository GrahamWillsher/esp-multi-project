#include "cellmonitor_page.h"
#include "cellmonitor_page_content.h"
#include "cellmonitor_page_script.h"
#include "../common/page_generator.h"

static esp_err_t cellmonitor_handler(httpd_req_t *req) {
    auto content_generator = [](httpd_req_t* request) -> esp_err_t {
        const char* content = get_cellmonitor_page_content();
        return send_page_content_chunk(request, "cellmonitor_content", content, strlen(content));
    };
    const char* script  = get_cellmonitor_page_script();
    return send_rendered_page_streaming(req,
                                        "Cell Monitor",
                                        content_generator,
                                        PageRenderOptions("", script));
}

esp_err_t register_cellmonitor_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/cellmonitor",
        .method    = HTTP_GET,
        .handler   = cellmonitor_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}