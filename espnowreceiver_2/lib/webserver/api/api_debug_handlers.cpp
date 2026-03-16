#include "api_debug_handlers.h"

#include "../../src/espnow/espnow_send.h"

#include <Arduino.h>
#include <cstdio>

esp_err_t api_get_debug_level_handler(httpd_req_t *req) {
    uint8_t level = get_last_debug_level();
    const char* level_names[] = {"EMERG", "ALERT", "CRIT", "ERROR", "WARNING", "NOTICE", "INFO", "DEBUG"};

    char json_response[256];
    snprintf(json_response, sizeof(json_response),
             "{\"success\":true,\"level\":%d,\"level_name\":\"%s\"}",
             level, level_names[level]);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_response);
    return ESP_OK;
}

esp_err_t api_set_debug_level_handler(httpd_req_t *req) {
    char buf[64];
    int ret = httpd_req_get_url_query_str(req, buf, sizeof(buf));

    if (ret == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(buf, "level", param, sizeof(param)) == ESP_OK) {
            uint8_t level = atoi(param);

            if (level > 7) {
                char error_json[128];
                snprintf(error_json, sizeof(error_json),
                         "{\"success\":false,\"message\":\"Invalid debug level (must be 0-7)\"}");
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, error_json);
                return ESP_OK;
            }

            if (send_debug_level_control(level)) {
                char success_json[256];
                const char* level_names[] = {"EMERG", "ALERT", "CRIT", "ERROR", "WARNING", "NOTICE", "INFO", "DEBUG"};
                snprintf(success_json, sizeof(success_json),
                         "{\"success\":true,\"message\":\"Debug level set to %d (%s)\",\"level\":%d}",
                         level, level_names[level], level);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, success_json);
            } else {
                char error_json[256];
                snprintf(error_json, sizeof(error_json),
                         "{\"success\":false,\"message\":\"Failed to send debug control (transmitter not connected?)\"}");
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, error_json);
            }
            return ESP_OK;
        }
    }

    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Missing level parameter\"}");
    return ESP_OK;
}
