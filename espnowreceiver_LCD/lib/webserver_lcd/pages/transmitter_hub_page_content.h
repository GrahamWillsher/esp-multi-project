#ifndef TRANSMITTER_HUB_PAGE_CONTENT_H
#define TRANSMITTER_HUB_PAGE_CONTENT_H

#include <esp_http_server.h>

// Emits the hub page body directly to the HTTP response in segments.
// Uses static flash-resident HTML; only the 6 tiny dynamic values are sent
// from stack-allocated char buffers.  Zero runtime heap allocation.
esp_err_t emit_transmitter_hub_page_content(
    httpd_req_t* req,
    const char* device_subtitle,
    const char* status_color,
    const char* status_text,
    const char* ip_text,
    const char* version_text,
    const char* build_date
);

#endif
