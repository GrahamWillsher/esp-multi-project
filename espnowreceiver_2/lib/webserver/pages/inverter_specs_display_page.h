#ifndef INVERTER_SPECS_PAGE_H
#define INVERTER_SPECS_PAGE_H

#include <esp_http_server.h>

/**
 * @brief Inverter Specs Display Page
 * Shows inverter static configuration from battery emulator
 * Subscribes to BE/spec_data_2 MQTT topic
 */
esp_err_t inverter_specs_page_handler(httpd_req_t *req);

/**
 * @brief Register Inverter Specs Page with webserver
 */
esp_err_t register_inverter_specs_page(httpd_handle_t server);

#endif // INVERTER_SPECS_PAGE_H
