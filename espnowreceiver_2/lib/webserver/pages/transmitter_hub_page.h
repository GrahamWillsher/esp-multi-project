#ifndef TRANSMITTER_HUB_PAGE_H
#define TRANSMITTER_HUB_PAGE_H

#include <esp_http_server.h>

/**
 * @brief Register the transmitter hub page handler
 * 
 * Central navigation hub for all transmitter-related functions.
 * Shows status summary and navigation to sub-pages.
 * 
 * @param server HTTP server handle
 * @return ESP_OK on success
 */
esp_err_t register_transmitter_hub_page(httpd_handle_t server);

#endif // TRANSMITTER_HUB_PAGE_H
