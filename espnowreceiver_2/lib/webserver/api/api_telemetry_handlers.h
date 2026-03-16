#ifndef API_TELEMETRY_HANDLERS_H
#define API_TELEMETRY_HANDLERS_H

#include <esp_http_server.h>

esp_err_t api_data_handler(httpd_req_t *req);
esp_err_t api_get_receiver_info_handler(httpd_req_t *req);
esp_err_t api_monitor_handler(httpd_req_t *req);
esp_err_t api_get_data_source_handler(httpd_req_t *req);
esp_err_t api_cell_data_handler(httpd_req_t *req);
esp_err_t api_dashboard_data_handler(httpd_req_t *req);
esp_err_t api_transmitter_ip_handler(httpd_req_t *req);
esp_err_t api_version_handler(httpd_req_t *req);
esp_err_t api_firmware_info_handler(httpd_req_t *req);
esp_err_t api_transmitter_metadata_handler(httpd_req_t *req);
esp_err_t api_transmitter_health_handler(httpd_req_t *req);
esp_err_t api_static_specs_handler(httpd_req_t *req);
esp_err_t api_battery_specs_handler(httpd_req_t *req);
esp_err_t api_inverter_specs_handler(httpd_req_t *req);
esp_err_t api_get_event_logs_handler(httpd_req_t *req);
esp_err_t api_system_metrics_handler(httpd_req_t *req);

#endif // API_TELEMETRY_HANDLERS_H
