#pragma once

#include <esp_http_server.h>

/**
 * @brief Register the Event Logs page (/events)
 */
esp_err_t register_event_logs_page(httpd_handle_t server);
