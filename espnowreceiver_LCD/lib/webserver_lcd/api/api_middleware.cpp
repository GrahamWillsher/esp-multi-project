#include "api_middleware.h"

#include "api_response_utils.h"
// NOTE: X-Radio-Pressure response header is intentionally NOT implemented here.
// That header was designed for ESP-NOW coexistence (see
// RECEIVER_HTTP_ESPNOW_COEXISTENCE_FULL_INVESTIGATION_2026_05_05.md) and depended
// on RadioPressureState / RxRadioArbiterFsm / EspnowTxScheduler — all of which are
// removed in the MQTT-only architecture.  The heap-admission gate below (503 when
// free heap < 60 KB) is the only pressure signal that remains relevant.
// See MQTT_ONLY_TRANSPORT_FEASIBILITY_2026_05_14.md Section 10.C.4 for the
// formal deprecation rationale.
#include "../logging.h"
#include "../webserver.h"

#include <esp_heap_caps.h>

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cstring>

namespace ApiMiddleware {
namespace {

constexpr size_t kMaxWrappedRoutes = 96;
constexpr int kMaxJsonBodyLen = 1024;
constexpr size_t kMaxRateLimitEntries = 96;
constexpr uint32_t kMinFreeHeapHeavyEndpointBytes = 60U * 1024U;
constexpr uint32_t kMinLargest8BitBlockBytes = 16U * 1024U;
constexpr uint32_t kHeavyEndpointRetryMs = 2000;

RouteContext g_routes[kMaxWrappedRoutes];
size_t g_route_count = 0;

PressureGateStats g_pressure_gate_stats = {0, 0, 0, 0};

struct RateLimitPolicy {
    const char* uri;
    uint16_t refill_per_sec;
    uint16_t burst;
};

struct RateLimitEntry {
    bool in_use;
    int sockfd;
    const char* uri;
    uint32_t tokens_milli;
    uint32_t last_refill_ms;
};

RateLimitEntry g_rate_limits[kMaxRateLimitEntries] = {};

constexpr RateLimitPolicy kRateLimitPolicies[] = {
    {"/api/monitor", 5, 10},
    {"/api/dashboard_data", 5, 10},
    {"/api/cell_data", 1, 3},
    {"/api/cell_data_page", 1, 3},
    {"/api/get_event_logs", 1, 3},
    {"/api/event_logs_page", 1, 3},
};

constexpr uint32_t kSlowRequestWarnMs = 250;

const char* method_to_string(int method) {
    switch (method) {
        case HTTP_GET:    return "GET";
        case HTTP_POST:   return "POST";
        case HTTP_PUT:    return "PUT";
        case HTTP_PATCH:  return "PATCH";
        case HTTP_DELETE: return "DELETE";
        case HTTP_HEAD:   return "HEAD";
        default:          return "UNKNOWN";
    }
}

bool is_mutation_authorized(httpd_req_t* /*req*/) {
    // Current deployment model is private/local DIY. Keep hook centralized so
    // stricter auth (token/session/cert) can be enforced without touching each endpoint.
    return true;
}

bool has_json_content_type(httpd_req_t* req) {
    char content_type[64] = {0};
    const esp_err_t rc = httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type));
    if (rc != ESP_OK) {
        return false;
    }

    return strstr(content_type, "application/json") != nullptr;
}

bool is_heavy_read_endpoint(const char* uri) {
    if (!uri) {
        return false;
    }

    return strcmp(uri, "/api/cell_data") == 0 ||
           strcmp(uri, "/api/cell_data_page") == 0 ||
           strcmp(uri, "/api/get_event_logs") == 0 ||
           strcmp(uri, "/api/event_logs_page") == 0;
}

bool get_rate_limit_policy(const char* uri, RateLimitPolicy& out) {
    if (!uri) {
        return false;
    }

    for (const auto& policy : kRateLimitPolicies) {
        if (strcmp(policy.uri, uri) == 0) {
            out = policy;
            return true;
        }
    }
    return false;
}

RateLimitEntry* find_or_create_rate_limit_entry(const char* uri, int sockfd, uint32_t now_ms, uint16_t burst) {
    RateLimitEntry* free_entry = nullptr;
    for (auto& entry : g_rate_limits) {
        if (entry.in_use && entry.sockfd == sockfd && entry.uri && strcmp(entry.uri, uri) == 0) {
            return &entry;
        }
        if (!entry.in_use && free_entry == nullptr) {
            free_entry = &entry;
        }
    }

    if (free_entry) {
        free_entry->in_use = true;
        free_entry->sockfd = sockfd;
        free_entry->uri = uri;
        free_entry->tokens_milli = static_cast<uint32_t>(burst) * 1000U;
        free_entry->last_refill_ms = now_ms;
        return free_entry;
    }

    RateLimitEntry* oldest = &g_rate_limits[0];
    for (auto& entry : g_rate_limits) {
        if (entry.last_refill_ms < oldest->last_refill_ms) {
            oldest = &entry;
        }
    }

    oldest->in_use = true;
    oldest->sockfd = sockfd;
    oldest->uri = uri;
    oldest->tokens_milli = static_cast<uint32_t>(burst) * 1000U;
    oldest->last_refill_ms = now_ms;
    return oldest;
}

esp_err_t send_retry_json(httpd_req_t* req, const char* status, const char* message, uint32_t retry_ms) {
    char retry_after_buf[12];
    const uint32_t retry_after_s = (retry_ms + 999U) / 1000U;
    snprintf(retry_after_buf, sizeof(retry_after_buf), "%lu", static_cast<unsigned long>(retry_after_s));
    httpd_resp_set_hdr(req, "Retry-After", retry_after_buf);
    httpd_resp_set_status(req, status);

    StaticJsonDocument<192> doc;
    doc["success"] = false;
    doc["retry_ms"] = retry_ms;
    doc["message"] = message;
    return ApiResponseUtils::send_json_doc(req, doc);
}

esp_err_t enforce_heap_admission(httpd_req_t* req, const RouteContext* context) {
    if (!context || context->policy != RoutePolicy::ReadOnly || !is_heavy_read_endpoint(context->uri)) {
        return ESP_OK;
    }

    const uint32_t free_heap = ESP.getFreeHeap();
    const uint32_t largest_8bit = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (free_heap >= kMinFreeHeapHeavyEndpointBytes && largest_8bit >= kMinLargest8BitBlockBytes) {
        return ESP_OK;
    }

    LOG_WARN("API", "Heavy endpoint rejected by heap admission: uri=%s free=%lu largest8=%lu",
             context->uri ? context->uri : "<unknown>",
             static_cast<unsigned long>(free_heap),
             static_cast<unsigned long>(largest_8bit));

    return send_retry_json(req,
                           "503 Service Unavailable",
                           "Server under memory pressure; retry shortly",
                           kHeavyEndpointRetryMs);
}

esp_err_t enforce_endpoint_rate_limit(httpd_req_t* req, const RouteContext* context) {
    if (!req || !context || context->policy != RoutePolicy::ReadOnly || !context->uri) {
        return ESP_OK;
    }

    RateLimitPolicy policy{};
    if (!get_rate_limit_policy(context->uri, policy)) {
        return ESP_OK;
    }

    const int sockfd = httpd_req_to_sockfd(req);
    if (sockfd < 0) {
        return ESP_OK;
    }

    const uint32_t now_ms = millis();
    RateLimitEntry* entry = find_or_create_rate_limit_entry(context->uri, sockfd, now_ms, policy.burst);
    if (!entry) {
        return ESP_OK;
    }

    const uint32_t elapsed_ms = now_ms - entry->last_refill_ms;
    if (elapsed_ms > 0) {
        const uint32_t refill_milli = elapsed_ms * static_cast<uint32_t>(policy.refill_per_sec);
        const uint32_t max_tokens_milli = static_cast<uint32_t>(policy.burst) * 1000U;
        entry->tokens_milli = (entry->tokens_milli + refill_milli > max_tokens_milli)
                                  ? max_tokens_milli
                                  : (entry->tokens_milli + refill_milli);
        entry->last_refill_ms = now_ms;
    }

    if (entry->tokens_milli < 1000U) {
        const uint32_t deficit = 1000U - entry->tokens_milli;
        const uint32_t refill_rate = static_cast<uint32_t>(policy.refill_per_sec);
        const uint32_t retry_ms = (deficit + refill_rate - 1U) / refill_rate;
        return send_retry_json(req,
                               "429 Too Many Requests",
                               "Rate limit exceeded for endpoint",
                               (retry_ms == 0U) ? 1U : retry_ms);
    }

    entry->tokens_milli -= 1000U;
    return ESP_OK;
}

}  // namespace

esp_err_t dispatch(httpd_req_t* req) {
    const uint32_t started_ms = millis();
    webserver_on_request_start(started_ms);

    auto finalize = [&](esp_err_t rc) -> esp_err_t {
        const uint32_t now_ms = millis();
        const uint32_t elapsed_ms = now_ms - started_ms;
        webserver_on_request_end(now_ms, elapsed_ms, rc == ESP_OK);
        return rc;
    };

    if (!req || !req->user_ctx) {
        return finalize(ApiResponseUtils::send_error_with_status(req, "500 Internal Server Error", "Route context missing"));
    }

    const auto* context = reinterpret_cast<const RouteContext*>(req->user_ctx);
    if (!context->handler) {
        return finalize(ApiResponseUtils::send_error_with_status(req, "500 Internal Server Error", "Route handler missing"));
    }

    if (context->policy != RoutePolicy::ReadOnly) {
        if (!is_mutation_authorized(req)) {
            LOG_WARN("API", "Mutation rejected by auth middleware: %s", context->uri ? context->uri : "<unknown>");
            return finalize(ApiResponseUtils::send_error_with_status(req, "403 Forbidden", "Mutation not authorized"));
        }
    }

    if (context->policy == RoutePolicy::MutatingJson) {
        if (req->content_len <= 0) {
            return finalize(ApiResponseUtils::send_error_with_status(req, "400 Bad Request", "JSON body required"));
        }

        if (req->content_len > kMaxJsonBodyLen) {
            return finalize(ApiResponseUtils::send_error_with_status(req, "413 Payload Too Large", "JSON body too large"));
        }

        if (!has_json_content_type(req)) {
            return finalize(ApiResponseUtils::send_error_with_status(req, "415 Unsupported Media Type", "Content-Type must be application/json"));
        }
    }

    const esp_err_t heap_rc = enforce_heap_admission(req, context);
    if (heap_rc != ESP_OK) {
        return finalize(heap_rc);
    }

    const esp_err_t rate_limit_rc = enforce_endpoint_rate_limit(req, context);
    if (rate_limit_rc != ESP_OK) {
        return finalize(rate_limit_rc);
    }

    const esp_err_t handler_rc = context->handler(req);
    const uint32_t elapsed_ms = millis() - started_ms;
    if (handler_rc != ESP_OK || elapsed_ms >= kSlowRequestWarnMs) {
        LOG_WARN("API", "%s %s rc=%d dur=%lu ms",
                 method_to_string(req->method),
                 context->uri ? context->uri : "<unknown>",
                 static_cast<int>(handler_rc),
                 static_cast<unsigned long>(elapsed_ms));
    }

    return finalize(handler_rc);
}

bool register_route(httpd_handle_t server, const RouteContext& context) {
    if (g_route_count >= kMaxWrappedRoutes) {
        LOG_ERROR("API", "API middleware route capacity exceeded");
        return false;
    }

    g_routes[g_route_count] = context;
    auto* route = &g_routes[g_route_count++];

    httpd_uri_t uri = {
        .uri = route->uri,
        .method = route->method,
        .handler = dispatch,
        .user_ctx = route,
    };

    const esp_err_t rc = httpd_register_uri_handler(server, &uri);
    if (rc != ESP_OK) {
        LOG_WARN("API", "Failed to register route %s (%d)", route->uri ? route->uri : "<null>", static_cast<int>(rc));
        return false;
    }

    return true;
}

PressureGateStats get_pressure_gate_stats() {
    return g_pressure_gate_stats;
}

}  // namespace ApiMiddleware
