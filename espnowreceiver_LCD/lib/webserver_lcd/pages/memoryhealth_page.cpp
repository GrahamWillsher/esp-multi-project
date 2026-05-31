#include "memoryhealth_page.h"
#include "memoryhealth_page_content.h"
#include "memoryhealth_page_script.h"
#include "../common/page_generator.h"
#include <cstring>

static esp_err_t memoryhealth_content_generator(httpd_req_t* req) {
    const char* content = get_memoryhealth_page_content();
    return send_page_content_chunk(req, "memoryhealth_content", content, strlen(content));
}

static esp_err_t memoryhealth_handler(httpd_req_t *req) {
    const char* script = get_memoryhealth_page_script();

    return send_rendered_page_streaming(req,
                                        "Memory Health",
                                        memoryhealth_content_generator,
                                        PageRenderOptions("", script, false));
}

esp_err_t register_memoryhealth_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri = "/receiver/memoryhealth",
        .method = HTTP_GET,
        .handler = memoryhealth_handler,
        .user_ctx = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
