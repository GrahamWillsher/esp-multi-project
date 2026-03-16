#ifndef API_SETTINGS_HANDLERS_H
#define API_SETTINGS_HANDLERS_H

#include <esp_http_server.h>

esp_err_t api_set_data_source_handler(httpd_req_t *req);
esp_err_t api_get_battery_settings_handler(httpd_req_t *req);
esp_err_t api_save_setting_handler(httpd_req_t *req);

#endif // API_SETTINGS_HANDLERS_H
