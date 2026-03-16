#include "api_modular_handlers.h"

#include "../utils/http_json_utils.h"
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
    const char* read_error = nullptr;

    if (!HttpJsonUtils::read_request_body(req, buffer, sizeof(buffer), nullptr, &read_error)) {
        return HttpJsonUtils::send_json_error(req, read_error);
    }

    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, buffer);

    if (error) {
        char response[256];
        snprintf(response, sizeof(response), "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return HttpJsonUtils::send_json(req, response);
    }

    uint8_t mode = 0;

    if (doc["mode"].is<const char*>()) {
        const char* mode_str = doc["mode"];
        if (strcmp(mode_str, "OFF") == 0) mode = 0;
        else if (strcmp(mode_str, "SOC_POWER_ONLY") == 0) mode = 1;
        else if (strcmp(mode_str, "FULL_BATTERY_DATA") == 0) mode = 2;
        else {
            char response[256];
            snprintf(response, sizeof(response), "{\"success\":false,\"error\":\"Invalid mode string\"}");
            return HttpJsonUtils::send_json(req, response);
        }
    } else if (doc["mode"].is<int>()) {
        mode = doc["mode"].as<int>();
        if (mode > 2) {
            char response[256];
            snprintf(response, sizeof(response), "{\"success\":false,\"error\":\"Invalid mode number (0-2)\"}");
            return HttpJsonUtils::send_json(req, response);
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

    char response[256];
    snprintf(response, sizeof(response),
             "{\"success\":false,\"error\":\"Failed to send command to transmitter\"}");
    return HttpJsonUtils::send_json(req, response);
}

esp_err_t api_event_logs_subscribe_handler(httpd_req_t *req) {
    MqttClient::incrementEventLogSubscribers();

    const char* response = "{\"success\":true}";
    return HttpJsonUtils::send_json(req, response);
}

esp_err_t api_event_logs_unsubscribe_handler(httpd_req_t *req) {
    MqttClient::decrementEventLogSubscribers();

    const char* response = "{\"success\":true}";
    return HttpJsonUtils::send_json(req, response);
}
