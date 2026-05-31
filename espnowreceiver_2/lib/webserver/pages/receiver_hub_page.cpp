#include "receiver_hub_page.h"
#include "receiver_hub_page_content.h"
#include "../common/page_generator.h"

#include <Arduino.h>
#include <WiFi.h>
#include <firmware_metadata.h>
#include <firmware_version.h>

static esp_err_t receiver_hub_handler(httpd_req_t* req) {
    auto format_display_name = [](const String& raw) -> String {
        String stripped = raw;
        stripped.trim();
        while (stripped.length() > 1 && stripped[0] == '"' && stripped[stripped.length() - 1] == '"') {
            stripped = stripped.substring(1, stripped.length() - 1);
            stripped.trim();
        }
        stripped.replace("-", " ");
        stripped.replace("_", " ");
        for (int i = 0; i < stripped.length(); ++i) {
            if (i == 0 || stripped[i - 1] == ' ') {
                stripped.setCharAt(i, static_cast<char>(toupper(stripped[i])));
            }
        }
        return stripped;
    };

    String device_subtitle = "Unknown Device";
    String version_text = FW_VERSION_STRING;
    String build_date = __DATE__;
    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata)) {
        device_subtitle = format_display_name(String(FirmwareMetadata::metadata.env_name));

        char rx_version_str[16];
        snprintf(rx_version_str, sizeof(rx_version_str), "v%d.%d.%d",
                 FirmwareMetadata::metadata.version_major,
                 FirmwareMetadata::metadata.version_minor,
                 FirmwareMetadata::metadata.version_patch);
        version_text = String(rx_version_str);

        if (strlen(FirmwareMetadata::metadata.build_date) > 0) {
            build_date = String(FirmwareMetadata::metadata.build_date);
            build_date.replace("-", " ");
        }
    }

    String status_text = "Online";
    String status_color = "#4CAF50";
    wifi_mode_t mode = WiFi.getMode();
    if ((mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) && WiFi.status() != WL_CONNECTED) {
        status_text = "AP Mode";
        status_color = "#FF9800";
    }

    String ip_text = WiFi.localIP().toString();
    if (ip_text == "0.0.0.0") {
        ip_text = WiFi.softAPIP().toString();
    }
    if (ip_text == "0.0.0.0") {
        ip_text = "Not available";
    }

    String content = get_receiver_hub_page_content(device_subtitle,
                                                   status_color,
                                                   status_text,
                                                   ip_text,
                                                   version_text,
                                                   build_date);
    return send_rendered_page(req, "Receiver Hub", content, PageRenderOptions("", ""));
}

esp_err_t register_receiver_hub_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/receiver",
        .method    = HTTP_GET,
        .handler   = receiver_hub_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
