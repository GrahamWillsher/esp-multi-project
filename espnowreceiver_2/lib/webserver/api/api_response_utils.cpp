#include "api_response_utils.h"

#include <webserver_common_utils/http_json_utils.h>

#include <ArduinoJson.h>
#include <cstdarg>
#include <cstdio>

namespace ApiResponseUtils {

esp_err_t send_success(httpd_req_t* req) {
    return HttpJsonUtils::send_json(req, "{\"success\":true}");
}

esp_err_t send_jsonf(httpd_req_t* req, const char* format, ...) {
    char json[512];
    va_list args;
    va_start(args, format);
    vsnprintf(json, sizeof(json), format, args);
    va_end(args);
    return HttpJsonUtils::send_json(req, json);
}

esp_err_t send_json_no_cache(httpd_req_t* req, const char* json) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    return HttpJsonUtils::send_json(req, json);
}

esp_err_t send_success_message(httpd_req_t* req, const char* message) {
    return send_jsonf(req, "{\"success\":true,\"message\":\"%s\"}", message);
}

esp_err_t send_error_message(httpd_req_t* req, const char* message) {
    return HttpJsonUtils::send_json_error(req, message);
}

esp_err_t send_json_parse_error(httpd_req_t* req) {
    return send_error_message(req, "JSON parse error");
}

esp_err_t send_transmitter_mac_unknown(httpd_req_t* req) {
    return send_error_message(req, "Transmitter MAC unknown");
}

esp_err_t send_error_with_status(httpd_req_t* req, const char* status, const char* message) {
    httpd_resp_set_status(req, status);
    return send_error_message(req, message);
}

esp_err_t send_json_doc(httpd_req_t* req, JsonDocument& doc) {
    String buf;
    serializeJson(doc, buf);
    return HttpJsonUtils::send_json(req, buf.c_str());
}

esp_err_t send_success_doc(httpd_req_t* req, JsonDocument& doc) {
    doc["success"] = true;
    return send_json_doc(req, doc);
}

void format_ipv4(char* buf, const uint8_t ip[4]) {
    snprintf(buf, 16, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

void escape_double_quotes(const char* src, char* dst, size_t max_len) {
    if (!src || !dst || max_len == 0) {
        if (dst && max_len > 0) {
            dst[0] = '\0';
        }
        return;
    }

    size_t copy_index = 0;
    while (src[copy_index] != '\0' && copy_index < max_len - 1) {
        dst[copy_index] = (src[copy_index] == '"') ? '\'' : src[copy_index];
        copy_index++;
    }
    dst[copy_index] = '\0';
}

esp_err_t send_espnow_send_result(httpd_req_t* req,
                                  esp_err_t espnow_result,
                                  const char* success_msg) {
    if (espnow_result == ESP_OK) {
        return send_success_message(req, success_msg);
    }
    return send_jsonf(req,
                      "{\"success\":false,\"message\":\"ESP-NOW send failed: %s\"}",
                      esp_err_to_name(espnow_result));
}

} // namespace ApiResponseUtils
