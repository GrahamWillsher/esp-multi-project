#include "api_handlers.h"

#include "api_response_utils.h"
#include "api_control_handlers.h"
#include "api_debug_handlers.h"
#include "api_led_handlers.h"
#include "api_modular_handlers.h"
#include "api_network_handlers.h"
#include "api_settings_handlers.h"
#include "api_sse_handlers.h"
#include "api_telemetry_handlers.h"
#include "api_type_selection_handlers.h"

static esp_err_t notfound_handler(httpd_req_t *req) {
    return ApiResponseUtils::send_error_with_status(req, "404 Not Found", "Endpoint not found");
}

int register_all_api_handlers(httpd_handle_t server) {
    int count = 0;

    httpd_uri_t handlers[] = {
        {.uri = "/api/data", .method = HTTP_GET, .handler = api_data_handler, .user_ctx = NULL},
        {.uri = "/api/get_receiver_info", .method = HTTP_GET, .handler = api_get_receiver_info_handler, .user_ctx = NULL},
        {.uri = "/api/dashboard_data", .method = HTTP_GET, .handler = api_dashboard_data_handler, .user_ctx = NULL},
        {.uri = "/api/monitor", .method = HTTP_GET, .handler = api_monitor_handler, .user_ctx = NULL},
        {.uri = "/api/get_data_source", .method = HTTP_GET, .handler = api_get_data_source_handler, .user_ctx = NULL},
        {.uri = "/api/set_data_source", .method = HTTP_POST, .handler = api_set_data_source_handler, .user_ctx = NULL},
        {.uri = "/api/cell_data", .method = HTTP_GET, .handler = api_cell_data_handler, .user_ctx = NULL},
        {.uri = "/api/transmitter_ip", .method = HTTP_GET, .handler = api_transmitter_ip_handler, .user_ctx = NULL},
        {.uri = "/api/transmitter_health", .method = HTTP_GET, .handler = api_transmitter_health_handler, .user_ctx = NULL},
        {.uri = "/api/version", .method = HTTP_GET, .handler = api_version_handler, .user_ctx = NULL},
        {.uri = "/api/firmware_info", .method = HTTP_GET, .handler = api_firmware_info_handler, .user_ctx = NULL},
        {.uri = "/api/transmitter_metadata", .method = HTTP_GET, .handler = api_transmitter_metadata_handler, .user_ctx = NULL},
        {.uri = "/api/monitor_sse", .method = HTTP_GET, .handler = api_monitor_sse_handler, .user_ctx = NULL},
        {.uri = "/api/cell_stream", .method = HTTP_GET, .handler = api_cell_data_sse_handler, .user_ctx = NULL},
        {.uri = "/api/reboot", .method = HTTP_GET, .handler = api_reboot_handler, .user_ctx = NULL},
        {.uri = "/api/transmitter_ota_status", .method = HTTP_GET, .handler = api_transmitter_ota_status_handler, .user_ctx = NULL},
        {.uri = "/api/debugLevel", .method = HTTP_GET, .handler = api_get_debug_level_handler, .user_ctx = NULL},
        {.uri = "/api/setDebugLevel", .method = HTTP_GET, .handler = api_set_debug_level_handler, .user_ctx = NULL},
        {.uri = "/api/get_battery_settings", .method = HTTP_GET, .handler = api_get_battery_settings_handler, .user_ctx = NULL},
        {.uri = "/api/save_setting", .method = HTTP_POST, .handler = api_save_setting_handler, .user_ctx = NULL},
        {.uri = "/api/get_receiver_network", .method = HTTP_GET, .handler = api_get_receiver_network_handler, .user_ctx = NULL},
        {.uri = "/api/save_receiver_network", .method = HTTP_POST, .handler = api_save_receiver_network_handler, .user_ctx = NULL},
        {.uri = "/api/get_network_config", .method = HTTP_GET, .handler = api_get_network_config_handler, .user_ctx = NULL},
        {.uri = "/api/save_network_config", .method = HTTP_POST, .handler = api_save_network_config_handler, .user_ctx = NULL},
        {.uri = "/api/get_mqtt_config", .method = HTTP_GET, .handler = api_get_mqtt_config_handler, .user_ctx = NULL},
        {.uri = "/api/save_mqtt_config", .method = HTTP_POST, .handler = api_save_mqtt_config_handler, .user_ctx = NULL},
        {.uri = "/api/static_specs", .method = HTTP_GET, .handler = api_static_specs_handler, .user_ctx = NULL},
        {.uri = "/api/battery_specs", .method = HTTP_GET, .handler = api_battery_specs_handler, .user_ctx = NULL},
        {.uri = "/api/inverter_specs", .method = HTTP_GET, .handler = api_inverter_specs_handler, .user_ctx = NULL},
        {.uri = "/api/get_event_logs", .method = HTTP_GET, .handler = api_get_event_logs_handler, .user_ctx = NULL},
        {.uri = "/api/event_logs/subscribe", .method = HTTP_POST, .handler = api_event_logs_subscribe_handler, .user_ctx = NULL},
        {.uri = "/api/event_logs/unsubscribe", .method = HTTP_POST, .handler = api_event_logs_unsubscribe_handler, .user_ctx = NULL},
        {.uri = "/api/get_test_data_mode", .method = HTTP_GET, .handler = api_get_test_data_mode_handler, .user_ctx = NULL},
        {.uri = "/api/set_test_data_mode", .method = HTTP_POST, .handler = api_set_test_data_mode_handler, .user_ctx = NULL},
        {.uri = "/api/system_metrics", .method = HTTP_GET, .handler = api_system_metrics_handler, .user_ctx = NULL},
        {.uri = "/api/get_led_runtime_status", .method = HTTP_GET, .handler = api_get_led_runtime_status_handler, .user_ctx = NULL},
        {.uri = "/api/resync_led_state", .method = HTTP_POST, .handler = api_resync_led_state_handler, .user_ctx = NULL},
        {.uri = "/api/ota_upload", .method = HTTP_POST, .handler = api_ota_upload_handler, .user_ctx = NULL}
    };

    for (int index = 0; index < static_cast<int>(sizeof(handlers) / sizeof(httpd_uri_t)); ++index) {
        if (httpd_register_uri_handler(server, &handlers[index]) == ESP_OK) {
            count++;
        }
    }

    count += register_type_selection_api_handlers(server);

    httpd_uri_t notfound = {.uri = "/*", .method = HTTP_GET, .handler = notfound_handler, .user_ctx = NULL};
    if (httpd_register_uri_handler(server, &notfound) == ESP_OK) {
        count++;
    }

    return count;
}
