#ifndef API_NETWORK_HANDLERS_H
#define API_NETWORK_HANDLERS_H

#include <esp_http_server.h>

esp_err_t api_get_receiver_network_handler(httpd_req_t *req);
esp_err_t api_save_receiver_network_handler(httpd_req_t *req);
esp_err_t api_get_network_config_handler(httpd_req_t *req);
esp_err_t api_save_network_config_handler(httpd_req_t *req);
esp_err_t api_get_mqtt_config_handler(httpd_req_t *req);
esp_err_t api_save_mqtt_config_handler(httpd_req_t *req);

#endif // API_NETWORK_HANDLERS_H
