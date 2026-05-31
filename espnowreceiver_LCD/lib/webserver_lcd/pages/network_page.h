#pragma once
#include <esp_http_server.h>

namespace NetworkPage {

// GET  /config  — network configuration page (AP setup or STA config)
esp_err_t handle_get(httpd_req_t* req);
esp_err_t register_handler(httpd_handle_t server);

}  // namespace NetworkPage
