#ifndef API_MIDDLEWARE_H
#define API_MIDDLEWARE_H

#include <esp_http_server.h>

namespace ApiMiddleware {

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

}  // namespace ApiMiddleware

#endif  // API_MIDDLEWARE_H
