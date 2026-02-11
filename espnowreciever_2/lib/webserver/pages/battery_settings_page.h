#ifndef BATTERY_SETTINGS_PAGE_H
#define BATTERY_SETTINGS_PAGE_H

#include <esp_http_server.h>

/**
 * @brief Register the battery settings page handler with the HTTP server
 * @param server HTTP server instance
 * @return ESP_OK on success
 */
esp_err_t register_battery_settings_page(httpd_handle_t server);

#endif // BATTERY_SETTINGS_PAGE_H
