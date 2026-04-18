#ifndef HARDWARE_CONFIG_PAGE_H
#define HARDWARE_CONFIG_PAGE_H

#include <esp_http_server.h>

/**
 * @brief Register the transmitter hardware config page handler
 * @param server HTTP server instance
 * @return ESP_OK on success
 */
esp_err_t register_hardware_config_page(httpd_handle_t server);

#endif // HARDWARE_CONFIG_PAGE_H