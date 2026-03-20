#include "api_modular_handlers.h"

#include "api_request_utils.h"
#include "api_response_utils.h"
#include <webserver_common_utils/http_json_utils.h>
#include "../../src/espnow/espnow_send.h"
#include "../../src/mqtt/mqtt_client.h"

#include <ArduinoJson.h>
#include <cstring>

esp_err_t api_get_test_data_mode_handler(httpd_req_t *req) {
    char json_response[256];

    const uint8_t mode = get_last_test_data_mode();
    const char* mode_names[] = {"OFF", "SOC_POWER_ONLY", "FULL_BATTERY_DATA"};

    snprintf(json_response, sizeof(json_response),
             "{\"success\":true,\"mode\":%d,\"mode_name\":\"%s\",\"message\":\"Current test data mode\"}",
             mode, mode_names[mode]);

    return HttpJsonUtils::send_json(req, json_response);
}

esp_err_t api_set_test_data_mode_handler(httpd_req_t *req) {
    char buffer[100];
    StaticJsonDocument<256> doc;
    esp_err_t response_error = ESP_OK;
    if (!ApiRequestUtils::read_json_body_or_respond(req, buffer, sizeof(buffer), doc, &response_error)) {
        return response_error;
    }

    uint8_t mode = 0;

    if (doc["mode"].is<const char*>()) {
        const char* mode_str = doc["mode"];
        if (strcmp(mode_str, "OFF") == 0) mode = 0;
        else if (strcmp(mode_str, "SOC_POWER_ONLY") == 0) mode = 1;
        else if (strcmp(mode_str, "FULL_BATTERY_DATA") == 0) mode = 2;
        else {
            return ApiResponseUtils::send_error_message(req, "Invalid mode string");
        }
    } else if (doc["mode"].is<int>()) {
        mode = doc["mode"].as<int>();
        if (mode > 2) {
            return ApiResponseUtils::send_error_message(req, "Invalid mode number (0-2)");
        }
    }

    if (send_test_data_mode_control(mode)) {
        const char* mode_names[] = {"OFF", "SOC_POWER_ONLY", "FULL_BATTERY_DATA"};
        char response[256];
        snprintf(response, sizeof(response),
                 "{\"success\":true,\"mode\":\"%s\",\"message\":\"Test data mode changed\"}",
                 mode_names[mode]);
        return HttpJsonUtils::send_json(req, response);
    }

    return ApiResponseUtils::send_error_message(req, "Failed to send command to transmitter");
}

esp_err_t api_event_logs_subscribe_handler(httpd_req_t *req) {
    MqttClient::incrementEventLogSubscribers();
    return ApiResponseUtils::send_success(req);
}

esp_err_t api_event_logs_unsubscribe_handler(httpd_req_t *req) {
    MqttClient::decrementEventLogSubscribers();
    return ApiResponseUtils::send_success(req);
}
