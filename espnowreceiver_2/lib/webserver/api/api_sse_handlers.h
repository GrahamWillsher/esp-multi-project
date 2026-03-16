#ifndef API_SSE_HANDLERS_H
#define API_SSE_HANDLERS_H

#include <esp_http_server.h>

esp_err_t api_cell_data_sse_handler(httpd_req_t *req);
esp_err_t api_monitor_sse_handler(httpd_req_t *req);

#endif // API_SSE_HANDLERS_H
