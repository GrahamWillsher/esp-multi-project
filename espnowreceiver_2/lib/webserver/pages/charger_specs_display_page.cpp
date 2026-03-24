#include "charger_specs_display_page.h"
#include "generic_specs_page.h"
#include "../common/spec_page_layout.h"
#include "../utils/transmitter_manager.h"
#include "../page_definitions.h"
#include "../logging.h"
#include <ArduinoJson.h>

/**
 * @brief Charger Specs Display Page
 * 
 * Displays static charger configuration received from transmitter via MQTT
 */
esp_err_t charger_specs_page_handler(httpd_req_t *req) {
    // Get charger specs from TransmitterManager
    String specs_json = TransmitterManager::getChargerSpecsJson();
    
    // Parse JSON to extract values for display
    DynamicJsonDocument doc(512);
    String charger_type = "Unknown";
    String charger_manufacturer = "Unknown";
    uint16_t max_charge_power_w = 0;
    uint16_t max_charge_current_da = 0;
    uint16_t min_charge_voltage_dv = 0;
    uint16_t max_charge_voltage_dv = 0;
    uint8_t supports_modbus = 0;
    uint8_t supports_can = 0;
    
    if (specs_json.length() > 0) {
        DeserializationError error = deserializeJson(doc, specs_json);
        if (!error) {
            charger_type = doc["charger_type"].as<String>();
            if (charger_type.length() == 0) charger_type = "Not configured";
            
            charger_manufacturer = doc["charger_manufacturer"].as<String>();
            if (charger_manufacturer.length() == 0) charger_manufacturer = "Generic";
            
            max_charge_power_w = doc["max_charge_power_w"] | 5000;
            max_charge_current_da = doc["max_charge_current_da"] | 500;  // 50A
            min_charge_voltage_dv = doc["min_charge_voltage_dv"] | 4000;
            max_charge_voltage_dv = doc["max_charge_voltage_dv"] | 5200;
            supports_modbus = doc["supports_modbus"] | 0;
            supports_can = doc["supports_can"] | 0;
        }
    }
    
    String html_header = build_spec_page_html_header("Charger Specifications",
                                                      "🔌 Charger Specifications",
                                                      "Charger Configuration (Real-time from MQTT)",
                                                      "BE/battery_specs",
                                                      "#fa709a",
                                                      "#fee140",
                                                      "#fa709a");

    const char* html_specs_section = R"(
        <div class="specs-grid">
            <div class="spec-card">
                <div class="spec-label">Type</div>
                <div class="spec-value" style="font-size: 1.4em;">%s</div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Manufacturer</div>
                <div class="spec-value" style="font-size: 1.4em;">%s</div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Max Charge Power</div>
                <div class="spec-value">%u<span class="spec-unit">W</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Max Charge Current</div>
                <div class="spec-value">%.1f<span class="spec-unit">A</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Charge Voltage Range</div>
                <div class="spec-value">%.1f - %.1f<span class="spec-unit">V</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Communication</div>
                <div style="margin-top: 10px;">
                    <div class="feature-badge %s">%s Modbus</div>
                    <div class="feature-badge %s">%s CAN</div>
                </div>
            </div>
        </div>
)";

    static const SpecPageNavLink kNavLinks[] = {
        {"/", "← Back to Dashboard"},
        {"/inverter_settings.html", "← Inverter Specs"},
        {"/system_settings.html", "System Specs →"},
    };

    String html_footer = build_spec_page_html_footer(
        ::build_spec_page_nav_links(kNavLinks, sizeof(kNavLinks) / sizeof(kNavLinks[0])));

    GenericSpecsPage::RenderConfig render_config = {
        .log_tag = "CHARGER_PAGE",
        .specs_section_size = 2048,
        .total_slack_bytes = 256,
        .allocate_specs_section_in_psram = false,
    };
    return GenericSpecsPage::send_formatted_page(req, html_header, html_specs_section, html_footer, render_config,
        charger_type.c_str(),
        charger_manufacturer.c_str(),
        max_charge_power_w,
        max_charge_current_da / 10.0f,
        min_charge_voltage_dv / 10.0f,
        max_charge_voltage_dv / 10.0f,
        supports_modbus ? "enabled" : "disabled",
        supports_modbus ? "✓" : "✗",
        supports_can ? "enabled" : "disabled",
        supports_can ? "✓" : "✗");
}

// Registration function for webserver initialization
esp_err_t register_charger_specs_page(httpd_handle_t server) {
    return GenericSpecsPage::register_page(server, "/charger_settings.html", charger_specs_page_handler);
}
