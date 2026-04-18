#ifndef NETWORK_CONFIG_PAGE_H
#define NETWORK_CONFIG_PAGE_H

#include <esp_http_server.h>

/**
 * @brief Register the receiver network configuration page handler
 * @param server HTTP server instance
 * @return ESP_OK on success
 */
esp_err_t register_network_config_page(httpd_handle_t server);

#endif
