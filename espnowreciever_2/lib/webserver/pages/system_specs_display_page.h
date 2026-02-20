#ifndef SYSTEM_SPECS_PAGE_H
#define SYSTEM_SPECS_PAGE_H

#include <esp_http_server.h>

/**
 * @brief System Specs Display Page
 * Shows system static configuration from battery emulator
 */
esp_err_t system_specs_page_handler(httpd_req_t *req);

/**
 * @brief Register System Specs Page with webserver
 */
esp_err_t register_system_specs_page(httpd_handle_t server);

#endif // SYSTEM_SPECS_PAGE_H
