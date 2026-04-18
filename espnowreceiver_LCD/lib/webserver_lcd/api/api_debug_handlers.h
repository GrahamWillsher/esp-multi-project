#ifndef API_DEBUG_HANDLERS_H
#define API_DEBUG_HANDLERS_H

#include <esp_http_server.h>

esp_err_t api_get_debug_level_handler(httpd_req_t *req);
esp_err_t api_set_debug_level_handler(httpd_req_t *req);

#endif // API_DEBUG_HANDLERS_H
