#ifndef DASHBOARD_PAGE_H
#define DASHBOARD_PAGE_H

#include <esp_http_server.h>

/**
 * @brief Register the dashboard (landing page) handler
 * 
 * Landing page with device cards showing transmitter and receiver status.
 * Auto-refreshes every 5 seconds to update status indicators.
 * 
 * @param server HTTP server handle
 * @return ESP_OK on success
 */
esp_err_t register_dashboard_page(httpd_handle_t server);

#endif // DASHBOARD_PAGE_H
