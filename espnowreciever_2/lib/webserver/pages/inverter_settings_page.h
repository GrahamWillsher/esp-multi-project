#ifndef INVERTER_SETTINGS_PAGE_H
#define INVERTER_SETTINGS_PAGE_H

#include <esp_http_server.h>

/**
 * @brief Register the inverter settings page handler with the HTTP server
 * @param server HTTP server instance
 * @return ESP_OK on success
 */
esp_err_t register_inverter_settings_page(httpd_handle_t server);

#endif // INVERTER_SETTINGS_PAGE_H
