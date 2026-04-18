#pragma once
#include <esp_http_server.h>

namespace ApiNetwork {

// GET  /api/v1/network  — returns current NVS config as JSON
esp_err_t handle_get(httpd_req_t* req);

// POST /api/v1/network  — saves JSON config to NVS, responds, then reboots after 500 ms
esp_err_t handle_post(httpd_req_t* req);

esp_err_t register_handlers(httpd_handle_t server);

}  // namespace ApiNetwork
