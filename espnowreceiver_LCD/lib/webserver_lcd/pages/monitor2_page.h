#ifndef MONITOR2_PAGE_H
#define MONITOR2_PAGE_H

#include <esp_http_server.h>

/**
 * @brief Battery Monitor page handler (SSE version)
 * 
 * Displays real-time battery data using Server-Sent Events.
 * Shows SOC and Power with live updates.
 * 
 * @param req HTTP request
 * @return ESP_OK on success
 */
esp_err_t monitor2_handler(httpd_req_t *req);

/**
 * @brief Register monitor2 page with HTTP server
 * 
 * @param server HTTP server handle
 * @return ESP_OK on success
 */
esp_err_t register_monitor2_page(httpd_handle_t server);

#endif
