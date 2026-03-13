#pragma once

#include <esp_http_server.h>

/**
 * @brief Register battery/inverter type and interface selection API handlers
 * @param server HTTP server instance
 * @return Number of handlers successfully registered
 */
int register_type_selection_api_handlers(httpd_handle_t server);
