#include "dashboard_page.h"
#include "dashboard_page_content.h"
#include "dashboard_page_script.h"
#include "../common/page_generator.h"
#include "../utils/transmitter_manager.h"
#include "../logging.h"
#include <WiFi.h>
#include <firmware_metadata.h>
#include <firmware_version.h>

// Content generator: collects 4 RX-side values into small stack buffers,
// then streams the full page body from flash via emit_dashboard_page_content.
// TX values are baked as "---" in the static HTML; JS updates them from
// /api/dashboard_data without any per-request heap allocation here.
static esp_err_t dashboard_content_generator(httpd_req_t* req) {
    // ── RX device name ────────────────────────────────────────────────────
    char rx_device_name[64] = "Unknown Device";
    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata) &&
        FirmwareMetadata::metadata.env_name[0] != '\0') {
        strlcpy(rx_device_name, FirmwareMetadata::metadata.env_name, sizeof(rx_device_name));
    }

    // ── RX IP ─────────────────────────────────────────────────────────────
    char rx_ip[48] = "";
    {
        String s = WiFi.localIP().toString();
        if (s.isEmpty()) {
            s = "0.0.0.0";
        }
        strlcpy(rx_ip, s.c_str(), sizeof(rx_ip));
    }

    // ── RX firmware version ───────────────────────────────────────────────
    char rx_version[24] = "Unknown";
    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata)) {
        snprintf(rx_version, sizeof(rx_version), "%u.%u.%u",
                 static_cast<unsigned>(FirmwareMetadata::metadata.version_major),
                 static_cast<unsigned>(FirmwareMetadata::metadata.version_minor),
                 static_cast<unsigned>(FirmwareMetadata::metadata.version_patch));
    } else {
        strlcpy(rx_version, FW_VERSION_STRING, sizeof(rx_version));
    }

    // ── RX MAC ────────────────────────────────────────────────────────────
    char rx_mac[24] = "Unknown";
    {
        String s = WiFi.macAddress();
        if (!s.isEmpty()) {
            strlcpy(rx_mac, s.c_str(), sizeof(rx_mac));
        }
    }

    return emit_dashboard_page_content(req, rx_device_name, rx_ip, rx_version, rx_mac);
}

esp_err_t dashboard_handler(httpd_req_t* req) {
    ESP_LOGV(TAG, "dashboard_handler: Rendering dashboard page");

    const char* title = "Dashboard";
    PageRenderOptions options("", get_dashboard_page_script());

    esp_err_t ret = send_rendered_page_streaming(req, title, dashboard_content_generator, options);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "dashboard_handler: Failed to render page, err=%d", ret);
    }

    return ret;
}

esp_err_t register_dashboard_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = dashboard_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
