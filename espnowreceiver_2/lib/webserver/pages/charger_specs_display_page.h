#ifndef CHARGER_SPECS_PAGE_H
#define CHARGER_SPECS_PAGE_H

#include <esp_http_server.h>

/**
 * @brief Charger Specs Display Page
 * Shows charger static configuration from battery emulator
 */
esp_err_t charger_specs_page_handler(httpd_req_t *req);

/**
 * @brief Register Charger Specs Page with webserver
 */
esp_err_t register_charger_specs_page(httpd_handle_t server);

#endif // CHARGER_SPECS_PAGE_H
