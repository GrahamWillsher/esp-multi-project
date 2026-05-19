#include "api_response_utils.h"

#include <webserver_common_utils/http_json_utils.h>

#include <ArduinoJson.h>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <new>

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
    // Measure first so we can pick stack vs heap path and avoid over-allocation.
    const size_t json_len = measureJson(doc);
    if (json_len == 0) {
        return HttpJsonUtils::send_json(req, "{}");
    }
    // Stack path: keeps allocator pressure off the heap for the common case.
    if (json_len <= 512) {
        char buf[513];
        serializeJson(doc, buf, sizeof(buf));
        return HttpJsonUtils::send_json(req, buf);
    }
    // Heap path for larger documents: exactly one allocation sized to the payload.
    std::unique_ptr<char[]> buf(new (std::nothrow) char[json_len + 1]);
    if (!buf) {
        return HttpJsonUtils::send_json_error(req, "Out of memory");
    }
    serializeJson(doc, buf.get(), json_len + 1);
    return HttpJsonUtils::send_json(req, buf.get());
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

} // namespace ApiResponseUtils
