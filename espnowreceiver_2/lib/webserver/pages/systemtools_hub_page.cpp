#include "systemtools_hub_page.h"
#include "systemtools_hub_page_content.h"
#include "../common/page_generator.h"

static esp_err_t systemtools_hub_handler(httpd_req_t* req) {
    return send_rendered_page(req,
                              "System Tools",
                              get_systemtools_hub_page_content(),
                              PageRenderOptions("", ""));
}

esp_err_t register_systemtools_hub_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/systemtools",
        .method    = HTTP_GET,
        .handler   = systemtools_hub_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
