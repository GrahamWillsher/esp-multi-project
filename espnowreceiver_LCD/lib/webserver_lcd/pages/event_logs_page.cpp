#include "event_logs_page.h"
#include "event_logs_page_content.h"
#include "event_logs_page_script.h"
#include "../common/page_generator.h"

// Event logs page handler - uses receiver API to fetch logs
static esp_err_t event_logs_page_handler(httpd_req_t *req) {
    auto content_generator = [](httpd_req_t* request) -> esp_err_t {
        const char* content = get_event_logs_page_content();
        return send_page_content_chunk(request, "event_logs_content", content, strlen(content));
    };

    return send_rendered_page_streaming(req,
                                        "Event Logs",
                                        content_generator,
                                        PageRenderOptions(get_event_logs_page_styles(), get_event_logs_page_script()));
}

// Register event logs page
esp_err_t register_event_logs_page(httpd_handle_t server) {
    httpd_uri_t events_uri = {
        .uri = "/systemtools/events",
        .method = HTTP_GET,
        .handler = event_logs_page_handler,
        .user_ctx = NULL
    };
    return httpd_register_uri_handler(server, &events_uri);
}
