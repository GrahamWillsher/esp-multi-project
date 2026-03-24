#include "transmitter_hub_page.h"
#include "transmitter_hub_page_content.h"
#include "transmitter_hub_page_script.h"
#include "../common/page_generator.h"
#include "../utils/transmitter_manager.h"
#include <Arduino.h>

/**
 * @brief Handler for the transmitter hub page
 * 
 * Central navigation for all transmitter-related functions.
 */
static esp_err_t transmitter_hub_handler(httpd_req_t *req) {
    // Get transmitter status
    bool connected = TransmitterManager::isMACKnown();
    String status_color = connected ? "#4CAF50" : "#ff6b35";
    String status_text = connected ? "Connected" : "Disconnected";
    String ip_text = TransmitterManager::getIPString();
    if (ip_text == "0.0.0.0") ip_text = "Not available";

    String version_text = "Unknown";
    String build_date = "";
    String device_subtitle = "Metadata pending";
    if (TransmitterManager::hasMetadata()) {
        uint8_t major, minor, patch;
        TransmitterManager::getMetadataVersion(major, minor, patch);
        char version_str[16];
        snprintf(version_str, sizeof(version_str), "v%d.%d.%d", major, minor, patch);
        version_text = String(version_str);
        build_date = String(TransmitterManager::getMetadataBuildDate());

        const char* env = TransmitterManager::getMetadataEnv();
        const char* dev = TransmitterManager::getMetadataDevice();
        if (env && strlen(env) > 0) {
            String pretty = String(env);
            pretty.replace("-", " ");
            pretty.replace("_", " ");
            for (int i = 0; i < pretty.length(); ++i) {
                if (i == 0 || pretty[i - 1] == ' ') {
                    pretty.setCharAt(i, toupper(pretty[i]));
                }
            }
            device_subtitle = pretty;
        } else if (dev && strlen(dev) > 0) {
            device_subtitle = String(dev);
        }
    }

    String content = get_transmitter_hub_page_content(
        device_subtitle,
        status_color,
        status_text,
        ip_text,
        version_text,
        build_date.isEmpty() ? "Unknown" : build_date
    );
    String script = get_transmitter_hub_page_script();

    return send_rendered_page(req, "Transmitter Hub", content, PageRenderOptions("", script));
}

esp_err_t register_transmitter_hub_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/transmitter",
        .method    = HTTP_GET,
        .handler   = transmitter_hub_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
