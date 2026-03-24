#include "system_specs_display_page.h"
#include "../common/spec_page_layout.h"
#include "../utils/transmitter_manager.h"
#include "../page_definitions.h"
#include "../logging.h"
#include <ArduinoJson.h>

/**
 * @brief System Specs Display Page
 * 
 * Displays static system configuration received from transmitter via MQTT
 */
esp_err_t system_specs_page_handler(httpd_req_t *req) {
    // Get system specs from TransmitterManager
    String specs_json = TransmitterManager::getSystemSpecsJson();
    
    // Parse JSON to extract values for display
    DynamicJsonDocument doc(512);
    String hardware_model = "Unknown";
    String can_interface = "Unknown";
    String firmware_version = "Unknown";
    String build_date = "Unknown";
    uint8_t can_speed_kbps = 250;
    uint8_t supports_diagnostics = 0;
    
    if (specs_json.length() > 0) {
        DeserializationError error = deserializeJson(doc, specs_json);
        if (!error) {
            hardware_model = doc["hardware_model"].as<String>();
            if (hardware_model.length() == 0) hardware_model = "Unknown";
            
            can_interface = doc["can_interface"].as<String>();
            if (can_interface.length() == 0) can_interface = "MCP2515";
            
            can_speed_kbps = doc["can_speed_kbps"] | 250;
            supports_diagnostics = doc["supports_diagnostics"] | 1;
        }
    }

    // Firmware/build display should come from transmitter embedded metadata only
    if (TransmitterManager::hasMetadata()) {
        uint8_t major, minor, patch;
        TransmitterManager::getMetadataVersion(major, minor, patch);
        char version_buf[16];
        snprintf(version_buf, sizeof(version_buf), "%d.%d.%d", major, minor, patch);
        firmware_version = String(version_buf);

        const char* md_build = TransmitterManager::getMetadataBuildDate();
        if (md_build && strlen(md_build) > 0) {
            build_date = String(md_build);
        } else {
            build_date = "Unknown";
        }
    } else {
        firmware_version = "Metadata unavailable";
        build_date = "Metadata unavailable";
    }
    
    String html_header = build_spec_page_html_header("System Specifications",
                                                      "🖥️ System Specifications",
                                                      "System Configuration (Real-time from MQTT)",
                                                      "BE/battery_specs",
                                                      "#667eea",
                                                      "#764ba2",
                                                      "#667eea");

    const char* html_specs_section = R"(
        <div class="specs-grid">
            <div class="spec-card">
                <div class="spec-label">Hardware Model</div>
                <div class="spec-value" style="font-size: 1.4em;">%s</div>
            </div>
            <div class="spec-card">
                <div class="spec-label">CAN Interface</div>
                <div class="spec-value" style="font-size: 1.4em;">%s</div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Firmware Version</div>
                <div class="spec-value" style="font-size: 1.4em;">%s</div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Build Date</div>
                <div class="spec-value" style="font-size: 1.2em;">%s</div>
            </div>
            <div class="spec-card">
                <div class="spec-label">CAN Bus Speed</div>
                <div class="spec-value">%u<span class="spec-unit">kbps</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Diagnostics</div>
                <div style="margin-top: 10px;">
                    <div class="feature-badge %s">%s Supported</div>
                </div>
            </div>
        </div>
)";

    String html_footer = build_spec_page_html_footer(R"(
            <a href="/" class="btn btn-secondary">← Back to Dashboard</a>
            <a href="/charger_settings.html" class="btn btn-secondary">← Charger Specs</a>
            <a href="/battery_settings.html" class="btn btn-secondary">Battery Specs →</a>
)" );

    // Allocate response buffer (PSRAM for large allocations)
    size_t html_header_len = html_header.length();
    size_t html_footer_len = html_footer.length();
    size_t specs_section_max = 2048;
    size_t total_size = html_header_len + specs_section_max + html_footer_len + 256;
    
    char* response = (char*)ps_malloc(total_size);
    if (!response) {
        LOG_ERROR("SYSTEM_PAGE", "Failed to allocate %d bytes in PSRAM", total_size);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    // Build response safely
    char specs_section[2048];
    snprintf(specs_section, sizeof(specs_section), html_specs_section,
             hardware_model.c_str(),
             can_interface.c_str(),
             firmware_version.c_str(),
             build_date.c_str(),
             can_speed_kbps,
             supports_diagnostics ? "enabled" : "disabled",
             supports_diagnostics ? "✓" : "✗");
    
    // Safe concatenation
    size_t offset = 0;
    offset += snprintf(response + offset, total_size - offset, "%s", html_header.c_str());
    offset += snprintf(response + offset, total_size - offset, "%s", specs_section);
    offset += snprintf(response + offset, total_size - offset, "%s", html_footer.c_str());
    
    // Send response
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, response, strlen(response));
    
    free(response);
    LOG_INFO("SYSTEM_PAGE", "System specs page served (%d bytes)", offset);
    
    return ESP_OK;
}

// Registration function for webserver initialization
esp_err_t register_system_specs_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/system_settings.html",
        .method    = HTTP_GET,
        .handler   = system_specs_page_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
