#include "memoryhealth_page.h"
#include "memoryhealth_page_content.h"
#include "memoryhealth_page_script.h"
#include "../common/page_generator.h"

static esp_err_t memoryhealth_handler(httpd_req_t *req) {
    return send_rendered_page(req,
                              "Memory Health",
                              get_memoryhealth_page_content(),
                              PageRenderOptions("", get_memoryhealth_page_script(), false));
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
