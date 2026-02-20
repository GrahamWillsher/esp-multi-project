#ifndef CELLMONITOR_PAGE_H
#define CELLMONITOR_PAGE_H

#include <esp_http_server.h>

/**
 * @brief Register the cell monitor page handler
 *
 * @param server HTTP server handle
 * @return ESP_OK on success
 */
esp_err_t register_cellmonitor_page(httpd_handle_t server);

#endif // CELLMONITOR_PAGE_H