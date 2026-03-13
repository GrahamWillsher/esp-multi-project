#ifndef REBOOT_PAGE_H
#define REBOOT_PAGE_H

#include <esp_http_server.h>

/**
 * @brief Reboot page handler
 * 
 * Displays reboot confirmation page with button to send
 * reboot command to transmitter.
 * 
 * @param req HTTP request
 * @return ESP_OK on success
 */
esp_err_t reboot_handler(httpd_req_t *req);

/**
 * @brief Register reboot page with HTTP server
 * 
 * @param server HTTP server handle
 * @return ESP_OK on success
 */
esp_err_t register_reboot_page(httpd_handle_t server);

#endif
