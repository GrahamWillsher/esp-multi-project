#ifndef API_MODULAR_HANDLERS_H
#define API_MODULAR_HANDLERS_H

#include <esp_http_server.h>

esp_err_t api_get_test_data_mode_handler(httpd_req_t *req);
esp_err_t api_set_test_data_mode_handler(httpd_req_t *req);

esp_err_t api_event_logs_subscribe_handler(httpd_req_t *req);
esp_err_t api_event_logs_unsubscribe_handler(httpd_req_t *req);

#endif // API_MODULAR_HANDLERS_H
