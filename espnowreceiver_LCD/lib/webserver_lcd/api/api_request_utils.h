#ifndef API_REQUEST_UTILS_H
#define API_REQUEST_UTILS_H

#include "api_response_utils.h"
#include <webserver_common_utils/http_json_utils.h>

#include <ArduinoJson.h>
#include <esp_http_server.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ApiRequestUtils {

inline bool read_request_body_or_respond(
    httpd_req_t* req,
    char* buffer,
    size_t buffer_size,
    esp_err_t* out_response_error = nullptr) {
    const char* read_error = nullptr;
    if (HttpJsonUtils::read_request_body(req, buffer, buffer_size, nullptr, &read_error)) {
        return true;
    }

    if (out_response_error) {
        *out_response_error = ApiResponseUtils::send_error_message(
            req,
            read_error ? read_error : "Failed to read request body");
    }
    return false;
}

template <typename TDocument>
bool parse_json_or_respond(
    httpd_req_t* req,
    const char* json,
    TDocument& doc,
    esp_err_t* out_response_error = nullptr,
    const char** out_parse_error = nullptr) {
    DeserializationError error = deserializeJson(doc, json);
    if (!error) {
        if (out_parse_error) {
            *out_parse_error = nullptr;
        }
        return true;
    }

    if (out_parse_error) {
        *out_parse_error = error.c_str();
    }
    if (out_response_error) {
        *out_response_error = ApiResponseUtils::send_json_parse_error(req);
    }
    return false;
}

template <typename TDocument>
bool read_json_body_or_respond(
    httpd_req_t* req,
    char* buffer,
    size_t buffer_size,
    TDocument& doc,
    esp_err_t* out_response_error = nullptr,
    const char** out_parse_error = nullptr) {
    if (!read_request_body_or_respond(req, buffer, buffer_size, out_response_error)) {
        if (out_parse_error) {
            *out_parse_error = nullptr;
        }
        return false;
    }

    return parse_json_or_respond(req, buffer, doc, out_response_error, out_parse_error);
}

inline bool parse_ipv4(const char* ip_str, uint8_t out[4]) {
    if (!ip_str || !out || ip_str[0] == '\0') {
        return false;
    }

    int a = 0;
    int b = 0;
    int c = 0;
    int d = 0;
    if (std::sscanf(ip_str, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
        return false;
    }

    if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) {
        return false;
    }

    out[0] = static_cast<uint8_t>(a);
    out[1] = static_cast<uint8_t>(b);
    out[2] = static_cast<uint8_t>(c);
    out[3] = static_cast<uint8_t>(d);
    return true;
}

inline void copy_ipv4(uint8_t out[4], const uint8_t source[4]) {
    if (!out || !source) {
        return;
    }

    std::memcpy(out, source, 4);
}

inline bool parse_ipv4_or_default(const char* ip_str, uint8_t out[4], const uint8_t fallback[4]) {
    if (parse_ipv4(ip_str, out)) {
        return true;
    }

    copy_ipv4(out, fallback);
    return false;
}

} // namespace ApiRequestUtils

#endif // API_REQUEST_UTILS_H