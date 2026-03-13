#ifndef API_HANDLERS_H
#define API_HANDLERS_H

#include <esp_http_server.h>

/**
 * @brief Register all API endpoint handlers with the HTTP server
 * @param server HTTP server instance
 * @return Number of handlers successfully registered
 */
int register_all_api_handlers(httpd_handle_t server);

#endif // API_HANDLERS_H
