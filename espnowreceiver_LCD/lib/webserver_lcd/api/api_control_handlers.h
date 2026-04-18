#ifndef API_CONTROL_HANDLERS_H
#define API_CONTROL_HANDLERS_H

#include <esp_http_server.h>

esp_err_t api_reboot_handler(httpd_req_t *req);
esp_err_t api_transmitter_ota_status_handler(httpd_req_t *req);
esp_err_t api_ota_upload_handler(httpd_req_t *req);
esp_err_t api_ota_upload_receiver_handler(httpd_req_t *req);

#endif // API_CONTROL_HANDLERS_H
