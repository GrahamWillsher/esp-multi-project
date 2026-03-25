#include "system_specs_display_page.h"
#include "generic_specs_page.h"
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
    
    static const WebserverCommonSpecLayout::SpecPageParams kPageParams = {
        .page_title     = "System Specifications",
        .heading        = "🖥️ System Specifications",
        .subtitle       = "System Configuration (Real-time from MQTT)",
        .source_topic   = "BE/battery_specs",
        .gradient_start = "#667eea",
        .gradient_end   = "#764ba2",
        .accent_color   = "#667eea",
    };

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

    static const SpecPageNavLink kNavLinks[] = {
        {"/", "← Back to Dashboard"},
        {"/charger_settings.html", "← Charger Specs"},
        {"/battery_settings.html", "Battery Specs →"},
    };

    return WebserverCommonSpecLayout::send_spec_page_response(
        req,
        kPageParams,
        kNavLinks,
        sizeof(kNavLinks) / sizeof(kNavLinks[0]),
        nullptr,
        html_specs_section,
        2048,
        false,
        "SYSTEM_PAGE",
        hardware_model.c_str(),
        can_interface.c_str(),
        firmware_version.c_str(),
        build_date.c_str(),
        can_speed_kbps,
        supports_diagnostics ? "enabled" : "disabled",
        supports_diagnostics ? "✓" : "✗");
}

// Registration function for webserver initialization
esp_err_t register_system_specs_page(httpd_handle_t server) {
    return GenericSpecsPage::register_page(server, "/system_settings.html", system_specs_page_handler);
}
