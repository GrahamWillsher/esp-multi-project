#pragma once

#include <esp_http_server.h>

// Register debug logging page handler
esp_err_t register_debug_page(httpd_handle_t server);
