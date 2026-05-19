#ifndef NETWORK_CONFIG_PAGE_CONTENT_H
#define NETWORK_CONFIG_PAGE_CONTENT_H

#include <esp_err.h>
#include <esp_http_server.h>

esp_err_t emit_network_config_page_content(httpd_req_t* req, bool isAPMode);

#endif // NETWORK_CONFIG_PAGE_CONTENT_H