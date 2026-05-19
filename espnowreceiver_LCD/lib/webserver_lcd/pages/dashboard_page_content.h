#ifndef DASHBOARD_PAGE_CONTENT_H
#define DASHBOARD_PAGE_CONTENT_H

#include <esp_http_server.h>

// Emits the dashboard page body directly to the HTTP response.
// TX fields are all set to "---" (JS fills them via /api/dashboard_data).
// The four RX fields JS does NOT update are supplied as const char* from
// small stack buffers in the caller; zero runtime heap allocation.
esp_err_t emit_dashboard_page_content(
    httpd_req_t* req,
    const char* rx_device_name,
    const char* rx_ip,
    const char* rx_version,
    const char* rx_mac
);

#endif // DASHBOARD_PAGE_CONTENT_H