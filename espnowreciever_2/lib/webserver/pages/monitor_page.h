#ifndef MONITOR_PAGE_H
#define MONITOR_PAGE_H

#include <esp_http_server.h>

/**
 * @brief Register the battery monitor page handler
 * @param server HTTP server instance
 * @return ESP_OK on success
 */
esp_err_t register_monitor_page(httpd_handle_t server);

#endif // MONITOR_PAGE_H
