#include "api_middleware.h"

#include "api_response_utils.h"
#include "../logging.h"

#include <cstring>

namespace ApiMiddleware {
namespace {

constexpr size_t kMaxWrappedRoutes = 96;
constexpr int kMaxJsonBodyLen = 1024;

RouteContext g_routes[kMaxWrappedRoutes];
size_t g_route_count = 0;

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

}  // namespace

esp_err_t dispatch(httpd_req_t* req) {
    if (!req || !req->user_ctx) {
        return ApiResponseUtils::send_error_with_status(req, "500 Internal Server Error", "Route context missing");
    }

    const auto* context = reinterpret_cast<const RouteContext*>(req->user_ctx);
    if (!context->handler) {
        return ApiResponseUtils::send_error_with_status(req, "500 Internal Server Error", "Route handler missing");
    }

    if (context->policy != RoutePolicy::ReadOnly) {
        if (!is_mutation_authorized(req)) {
            LOG_WARN("API", "Mutation rejected by auth middleware: %s", context->uri ? context->uri : "<unknown>");
            return ApiResponseUtils::send_error_with_status(req, "403 Forbidden", "Mutation not authorized");
        }
    }

    if (context->policy == RoutePolicy::MutatingJson) {
        if (req->content_len <= 0) {
            return ApiResponseUtils::send_error_with_status(req, "400 Bad Request", "JSON body required");
        }

        if (req->content_len > kMaxJsonBodyLen) {
            return ApiResponseUtils::send_error_with_status(req, "413 Payload Too Large", "JSON body too large");
        }

        if (!has_json_content_type(req)) {
            return ApiResponseUtils::send_error_with_status(req, "415 Unsupported Media Type", "Content-Type must be application/json");
        }
    }

    return context->handler(req);
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

}  // namespace ApiMiddleware
