#ifndef SETTINGS_PAGE_H
#define SETTINGS_PAGE_H

#include <esp_http_server.h>

/**
 * @brief Register the settings page handler (root "/" page)
 * @param server HTTP server instance
 * @return ESP_OK on success
 */
esp_err_t register_settings_page(httpd_handle_t server);

#endif // SETTINGS_PAGE_H
