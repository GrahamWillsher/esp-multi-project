#ifndef OTA_PAGE_H
#define OTA_PAGE_H

#include <esp_http_server.h>

/**
 * @brief Register the OTA firmware upload page handler
 * @param server HTTP server instance
 * @return ESP_OK on success
 */
esp_err_t register_ota_page(httpd_handle_t server);

#endif // OTA_PAGE_H
