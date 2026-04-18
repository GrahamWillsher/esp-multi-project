#pragma once
// api_utils.h — inline JSON request/response helpers for webserver_lcd.
// Thin wrappers around HttpJsonUtils primitives from esp32common.

#include <webserver_common_utils/http_json_utils.h>
#include <ArduinoJson.h>
#include <esp_http_server.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ApiUtils {

// ── Response helpers ───────────────────────────────────────────────────────

inline esp_err_t send_json(httpd_req_t* req, const char* json) {
    return HttpJsonUtils::send_json(req, json);
}

inline esp_err_t send_ok(httpd_req_t* req) {
    return HttpJsonUtils::send_json(req, "{\"success\":true}");
}

inline esp_err_t send_err(httpd_req_t* req, const char* message) {
    return HttpJsonUtils::send_json_error(req, message);
}

inline esp_err_t send_json_doc(httpd_req_t* req, JsonDocument& doc) {
    char buf[768];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) {
        return HttpJsonUtils::send_json_error(req, "response too large");
    }
    return HttpJsonUtils::send_json(req, buf);
}

// ── Request helpers ────────────────────────────────────────────────────────

// Reads up to buffer_size-1 bytes of request body into buffer, null-terminates.
// Returns false and sends an error response on failure.
inline bool read_body(httpd_req_t* req, char* buffer, size_t buffer_size, esp_err_t* out_err) {
    const char* err_msg = nullptr;
    if (HttpJsonUtils::read_request_body(req, buffer, buffer_size, nullptr, &err_msg)) {
        return true;
    }
    if (out_err) {
        *out_err = HttpJsonUtils::send_json_error(
            req, err_msg ? err_msg : "Failed to read request body");
    }
    return false;
}

// Reads body and deserialises JSON into doc.
// Returns false and sends an error response on failure.
template <typename TDoc>
inline bool read_json(httpd_req_t* req, char* buf, size_t buf_size, TDoc& doc, esp_err_t* out_err) {
    if (!read_body(req, buf, buf_size, out_err)) {
        return false;
    }
    const DeserializationError de_err = deserializeJson(doc, buf);
    if (de_err) {
        if (out_err) {
            *out_err = HttpJsonUtils::send_json_error(req, "JSON parse error");
        }
        return false;
    }
    return true;
}

// ── IP formatting ──────────────────────────────────────────────────────────

// Formats a 4-byte IPv4 address into buf (must be >= 16 bytes).
inline void format_ipv4(char* buf, const uint8_t ip[4]) {
    snprintf(buf, 16, "%u.%u.%u.%u",
             static_cast<unsigned>(ip[0]),
             static_cast<unsigned>(ip[1]),
             static_cast<unsigned>(ip[2]),
             static_cast<unsigned>(ip[3]));
}

// Parses a dotted-decimal string "a.b.c.d" into a 4-byte array.
// Returns true on success.
inline bool parse_ipv4(const char* str, uint8_t out[4]) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (sscanf(str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return false;
    }
    if (a > 255 || b > 255 || c > 255 || d > 255) {
        return false;
    }
    out[0] = static_cast<uint8_t>(a);
    out[1] = static_cast<uint8_t>(b);
    out[2] = static_cast<uint8_t>(c);
    out[3] = static_cast<uint8_t>(d);
    return true;
}

}  // namespace ApiUtils
