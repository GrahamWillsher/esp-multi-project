#include "transmitter_hub_page.h"
#include "transmitter_hub_page_content.h"
#include "transmitter_hub_page_script.h"
#include "../common/page_generator.h"
#include "../utils/transmitter_manager.h"
#include <Arduino.h>
#include <cstring>

// ──────────────────────────────────────────────────────────────────────────────
// Namespace holds fixed-size char arrays populated once per request and read
// back by the content generator callback.  The httpd task is single-threaded so
// there is no concurrent access concern.
// ──────────────────────────────────────────────────────────────────────────────
namespace {
struct HubRequestData {
    char device_subtitle[64];
    char status_color[16];
    char status_text[24];
    char ip_text[48];
    char version_text[16];
    char build_date[32];
} g_hub;

esp_err_t transmitter_hub_content_generator(httpd_req_t* req) {
    return emit_transmitter_hub_page_content(
        req,
        g_hub.device_subtitle,
        g_hub.status_color,
        g_hub.status_text,
        g_hub.ip_text,
        g_hub.version_text,
        g_hub.build_date
    );
}
}  // namespace

/**
 * @brief Handler for the transmitter hub page
 * 
 * Central navigation for all transmitter-related functions.
 */
static esp_err_t transmitter_hub_handler(httpd_req_t *req) {
    // Populate g_hub fields from tiny stack computation — no String heap alloc.
    const bool connected = TransmitterManager::isMACKnown();
    strlcpy(g_hub.status_color, connected ? "#2196F3" : "#ff6b35", sizeof(g_hub.status_color));
    strlcpy(g_hub.status_text,  connected ? "Connected" : "Disconnected", sizeof(g_hub.status_text));

    {
        String ip = TransmitterManager::getIPString();
        if (ip == "0.0.0.0") ip = "Not available";
        strlcpy(g_hub.ip_text, ip.c_str(), sizeof(g_hub.ip_text));
    }

    strlcpy(g_hub.version_text, "Unknown", sizeof(g_hub.version_text));
    g_hub.build_date[0] = '\0';
    strlcpy(g_hub.device_subtitle, "Metadata pending", sizeof(g_hub.device_subtitle));

    if (TransmitterManager::hasMetadata()) {
        uint8_t major, minor, patch;
        TransmitterManager::getMetadataVersion(major, minor, patch);
        snprintf(g_hub.version_text, sizeof(g_hub.version_text), "v%d.%d.%d", major, minor, patch);

        const char* bd = TransmitterManager::getMetadataBuildDate();
        if (bd) strlcpy(g_hub.build_date, bd, sizeof(g_hub.build_date));

        const char* env = TransmitterManager::getMetadataEnv();
        const char* dev = TransmitterManager::getMetadataDevice();
        if (env && strlen(env) > 0) {
            // Title-case the env string (replace - and _ with spaces, capitalise words)
            char pretty[64];
            strlcpy(pretty, env, sizeof(pretty));
            for (size_t i = 0; pretty[i]; ++i) {
                if (pretty[i] == '-' || pretty[i] == '_') pretty[i] = ' ';
                if (i == 0 || pretty[i - 1] == ' ') pretty[i] = (char)toupper((uint8_t)pretty[i]);
            }
            strlcpy(g_hub.device_subtitle, pretty, sizeof(g_hub.device_subtitle));
        } else if (dev && strlen(dev) > 0) {
            strlcpy(g_hub.device_subtitle, dev, sizeof(g_hub.device_subtitle));
        }
    }

    if (g_hub.build_date[0] == '\0') strlcpy(g_hub.build_date, "Unknown", sizeof(g_hub.build_date));

    const esp_err_t rc = send_rendered_page_streaming(
        req,
        "Transmitter Hub",
        transmitter_hub_content_generator,
        PageRenderOptions(nullptr, get_transmitter_hub_page_script()));

    // Clear fields after use (zero out before next request for safety)
    memset(&g_hub, 0, sizeof(g_hub));
    return rc;
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
