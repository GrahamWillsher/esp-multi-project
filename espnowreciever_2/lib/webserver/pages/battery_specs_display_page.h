#ifndef BATTERY_SPECS_PAGE_H
#define BATTERY_SPECS_PAGE_H

#include <esp_http_server.h>

/**
 * @brief Battery Specs Display Page
 * Shows battery static configuration from battery emulator
 * Subscribes to BE/battery_specs MQTT topic
 */
esp_err_t battery_specs_page_handler(httpd_req_t *req);

/**
 * @brief Register Battery Specs Page with webserver
 */
esp_err_t register_battery_specs_page(httpd_handle_t server);

#endif // BATTERY_SPECS_PAGE_H
