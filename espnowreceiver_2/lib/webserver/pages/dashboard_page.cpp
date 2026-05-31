#include "dashboard_page.h"
#include "dashboard_page_content.h"
#include "dashboard_page_script.h"
#include "../common/page_generator.h"
#include "../utils/transmitter_manager.h"
#include "../../receiver_config/receiver_config_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include <firmware_metadata.h>

/**
 * @brief Handler for the dashboard landing page
 * 
 * Shows two device cards (Transmitter + Receiver) with status indicators
 * and system tools (Debug, OTA) at the bottom.
 */
static esp_err_t dashboard_handler(httpd_req_t *req) {
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

    // Get transmitter Ethernet status for dashboard indicator
    bool tx_connected = TransmitterManager::isEthernetConnected();
    String tx_status = "Disconnected";
    String tx_status_color = "#ff6b35"; // Red
    String tx_ip = TransmitterManager::getIPString();
    String tx_ip_mode = "";  // (D) or (S)
    String tx_version = "Unknown";
    String tx_device_name = "Unknown Device";  // Default matches receiver fallback

    if (tx_ip == "0.0.0.0") {
        tx_ip = "Not available";
    } else if (tx_ip != "Unknown") {
        tx_ip_mode = TransmitterManager::isStaticIP() ? " (S)" : " (D)";
    }

    if (tx_connected) {
        tx_status = "Connected";
        tx_status_color = "#4CAF50"; // Green when connected
    }

    if (TransmitterManager::hasMetadata()) {
        uint8_t major, minor, patch;
        TransmitterManager::getMetadataVersion(major, minor, patch);
        char version_str[16];
        snprintf(version_str, sizeof(version_str), "v%d.%d.%d", major, minor, patch);
        tx_version = String(version_str);

        const char* env = TransmitterManager::getMetadataEnv();
        if (env && strlen(env) > 0) {
            tx_device_name = format_display_name(String(env));
        }
    }
    
    // Receiver status (always online)
    String rx_version = "Unknown";
    String rx_ip = WiFi.localIP().toString();
    String rx_ip_mode = ReceiverNetworkConfig::useStaticIP() ? " (S)" : " (D)";
    
    // Get receiver device name from metadata
    String rx_device_name = "Unknown Device";
    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata)) {
        rx_device_name = format_display_name(String(FirmwareMetadata::metadata.env_name));
        char rx_version_str[16];
        snprintf(rx_version_str, sizeof(rx_version_str), "v%d.%d.%d",
                 FirmwareMetadata::metadata.version_major,
                 FirmwareMetadata::metadata.version_minor,
                 FirmwareMetadata::metadata.version_patch);
        rx_version = String(rx_version_str);
    }
    String content = get_dashboard_page_content(tx_status,
                                                tx_status_color,
                                                tx_ip,
                                                tx_ip_mode,
                                                tx_version,
                                                tx_device_name,
                                                TransmitterManager::getMACString(),
                                                rx_ip,
                                                rx_ip_mode,
                                                rx_version,
                                                rx_device_name,
                                                WiFi.macAddress());
    const char* script = get_dashboard_page_script();

    return send_rendered_page(req, "Dashboard", content, PageRenderOptions("", script));
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
