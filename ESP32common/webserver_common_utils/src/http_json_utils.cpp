#include <webserver_common_utils/http_json_utils.h>

#include <cstdio>
#include <cstring>

namespace HttpJsonUtils {

esp_err_t send_json(httpd_req_t* req, const char* json) {
    if (!req || !json) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, strlen(json));
}

esp_err_t send_json_error(httpd_req_t* req, const char* message) {
    char json[256];
    snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"%s\"}", message ? message : "Unknown error");
    return send_json(req, json);
}

bool read_request_body(
    httpd_req_t* req,
    char* buffer,
    size_t buffer_size,
    int* out_length,
    const char** out_error_message) {
    if (out_error_message) {
        *out_error_message = nullptr;
    }

    if (!req || !buffer || buffer_size < 2) {
        if (out_error_message) {
            *out_error_message = "Internal buffer error";
        }
        return false;
    }

    const int total_len = req->content_len;
    if (total_len <= 0 || total_len > static_cast<int>(buffer_size - 1)) {
        if (out_error_message) {
            *out_error_message = "Invalid request size";
        }
        return false;
    }

    int received = 0;
    while (received < total_len) {
        const int chunk_capacity = static_cast<int>(buffer_size - 1) - received;
        const int remaining = total_len - received;
        const int to_read = (remaining < chunk_capacity) ? remaining : chunk_capacity;

        const int ret = httpd_req_recv(req, buffer + received, to_read);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }

        if (ret <= 0) {
            if (out_error_message) {
                *out_error_message = "Failed to read request body";
            }
            return false;
        }

        received += ret;
    }

    buffer[received] = '\0';
    if (out_length) {
        *out_length = received;
    }

    return true;
}

} // namespace HttpJsonUtils
