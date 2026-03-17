#include "api_debug_handlers.h"

#include "../../src/espnow/espnow_send.h"
#include "api_response_utils.h"

#include <Arduino.h>
#include <cstdio>

esp_err_t api_get_debug_level_handler(httpd_req_t *req) {
    uint8_t level = get_last_debug_level();
    const char* level_names[] = {"EMERG", "ALERT", "CRIT", "ERROR", "WARNING", "NOTICE", "INFO", "DEBUG"};

    return ApiResponseUtils::send_jsonf(req,
                                        "{\"success\":true,\"level\":%d,\"level_name\":\"%s\"}",
                                        level,
                                        level_names[level]);
}

esp_err_t api_set_debug_level_handler(httpd_req_t *req) {
    char buf[64];
    int ret = httpd_req_get_url_query_str(req, buf, sizeof(buf));

    if (ret == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(buf, "level", param, sizeof(param)) == ESP_OK) {
            uint8_t level = atoi(param);

            if (level > 7) {
                return ApiResponseUtils::send_error_message(req, "Invalid debug level (must be 0-7)");
            }

            if (send_debug_level_control(level)) {
                const char* level_names[] = {"EMERG", "ALERT", "CRIT", "ERROR", "WARNING", "NOTICE", "INFO", "DEBUG"};
                return ApiResponseUtils::send_jsonf(req,
                                                    "{\"success\":true,\"message\":\"Debug level set to %d (%s)\",\"level\":%d}",
                                                    level,
                                                    level_names[level],
                                                    level);
            } else {
                return ApiResponseUtils::send_error_message(req, "Failed to send debug control (transmitter not connected?)");
            }
        }
    }

    return ApiResponseUtils::send_error_with_status(req, "400 Bad Request", "Missing level parameter");
}
