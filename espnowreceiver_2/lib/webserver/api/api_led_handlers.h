#ifndef API_LED_HANDLERS_H
#define API_LED_HANDLERS_H

#include <esp_http_server.h>

esp_err_t api_get_led_runtime_status_handler(httpd_req_t *req);
esp_err_t api_resync_led_state_handler(httpd_req_t *req);

#endif // API_LED_HANDLERS_H
