#ifndef API_MIDDLEWARE_H
#define API_MIDDLEWARE_H

#include <esp_http_server.h>

namespace ApiMiddleware {

struct PressureGateStats {
    uint32_t read_throttled_total;
    uint32_t read_throttled_constrained;
    uint32_t read_throttled_critical;
    uint32_t mutation_blocked_critical;
};

enum class RoutePolicy {
    ReadOnly,
    MutatingNoBody,
    MutatingJson,
};

struct RouteContext {
    const char* uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t*);
    RoutePolicy policy;
};

esp_err_t dispatch(httpd_req_t* req);
bool register_route(httpd_handle_t server, const RouteContext& context);
PressureGateStats get_pressure_gate_stats();

}  // namespace ApiMiddleware

#endif  // API_MIDDLEWARE_H
