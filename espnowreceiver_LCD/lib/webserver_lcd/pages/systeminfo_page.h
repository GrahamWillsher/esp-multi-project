#ifndef SYSTEMINFO_PAGE_H
#define SYSTEMINFO_PAGE_H

#include <esp_http_server.h>

/**
 * @brief Register the system information page handler
 * @param server HTTP server instance
 * @return ESP_OK on success
 */
esp_err_t register_systeminfo_page(httpd_handle_t server);

#endif
