#include "charger_specs_display_page.h"
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
    
    // HTML page
    const char* html_header = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Charger Specifications</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #fa709a 0%, #fee140 100%);
            min-height: 100vh;
            padding: 20px;
        }
        .container { max-width: 900px; margin: 0 auto; }
        .header {
            background: rgba(255, 255, 255, 0.95);
            border-radius: 12px;
            padding: 30px;
            margin-bottom: 20px;
            box-shadow: 0 10px 40px rgba(0, 0, 0, 0.1);
        }
        .header h1 {
            color: #333;
            margin-bottom: 10px;
            font-size: 2.5em;
        }
        .header p {
            color: black;
            font-size: 1.1em;
            font-weight: 600;
        }
        .specs-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 20px;
            margin-bottom: 20px;
        }
        .spec-card {
            background: white;
            border-radius: 12px;
            padding: 25px;
            box-shadow: 0 5px 20px rgba(0, 0, 0, 0.1);
            border-left: 5px solid #fa709a;
            transition: transform 0.3s ease, box-shadow 0.3s ease;
        }
        .spec-card:hover {
            transform: translateY(-5px);
            box-shadow: 0 10px 30px rgba(0, 0, 0, 0.15);
        }
        .spec-label {
            font-size: 0.9em;
            color: black;
            text-transform: uppercase;
            letter-spacing: 1px;
            margin-bottom: 8px;
            font-weight: 600;
        }
        .spec-value {
            font-size: 1.8em;
            color: #333;
            font-weight: 700;
            margin-bottom: 5px;
        }
        .spec-unit {
            font-size: 0.9em;
            color: #999;
        }
        .feature-badge {
            display: inline-block;
            padding: 5px 12px;
            background: #fa709a;
            color: white;
            border-radius: 20px;
            font-size: 0.85em;
            margin-right: 5px;
            margin-top: 5px;
        }
        .feature-badge.enabled { background: #20c997; }
        .feature-badge.disabled { background: #ccc; }
        .source-info {
            padding: 15px 20px;
            background: rgba(250, 112, 154, 0.1);
            border: 1px solid #fa709a;
            border-radius: 8px;
            color: black;
            font-size: 0.95em;
            text-align: center;
            margin-bottom: 20px;
        }
        .nav-buttons {
            display: flex;
            gap: 10px;
            justify-content: center;
            margin-top: 20px;
            flex-wrap: wrap;
        }
        .btn {
            padding: 12px 24px;
            border: none;
            border-radius: 8px;
            font-size: 1em;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s ease;
            text-decoration: none;
            display: inline-block;
        }
        .btn-primary {
            background: #fa709a;
            color: white;
        }
        .btn-primary:hover {
            background: #d85a82;
            box-shadow: 0 5px 15px rgba(250, 112, 154, 0.4);
        }
        .btn-secondary {
            background: white;
            color: #fa709a;
            border: 2px solid #fa709a;
        }
        .btn-secondary:hover {
            background: #fa709a;
            color: white;
        }
        @media (max-width: 768px) {
            .header h1 { font-size: 1.8em; }
            .specs-grid { grid-template-columns: 1fr; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🔌 Charger Specifications</h1>
            <p>Charger Configuration (Real-time from MQTT)</p>
        </div>
        
        <div class="source-info">
            📡 Source: Battery Emulator via MQTT Topic: <strong>BE/battery_specs</strong>
        </div>
)";

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

    const char* html_footer = R"(
        <div class="nav-buttons">
            <a href="/" class="btn btn-secondary">← Back to Dashboard</a>
            <a href="/inverter_settings.html" class="btn btn-secondary">← Inverter Specs</a>
            <a href="/system_settings.html" class="btn btn-secondary">System Specs →</a>
        </div>
    </div>
</body>
</html>
)";

    // Allocate response buffer (PSRAM for large allocations)
    size_t html_header_len = strlen(html_header);
    size_t html_footer_len = strlen(html_footer);
    size_t specs_section_max = 2048;
    size_t total_size = html_header_len + specs_section_max + html_footer_len + 256;
    
    char* response = (char*)ps_malloc(total_size);
    if (!response) {
        LOG_ERROR("CHARGER_PAGE", "Failed to allocate %d bytes in PSRAM", total_size);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    // Build response safely
    char specs_section[2048];
    snprintf(specs_section, sizeof(specs_section), html_specs_section,
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
    
    // Safe concatenation
    size_t offset = 0;
    offset += snprintf(response + offset, total_size - offset, "%s", html_header);
    offset += snprintf(response + offset, total_size - offset, "%s", specs_section);
    offset += snprintf(response + offset, total_size - offset, "%s", html_footer);
    
    // Send response
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, response, strlen(response));
    
    free(response);
    LOG_INFO("CHARGER_PAGE", "Charger specs page served (%d bytes)", offset);
    
    return ESP_OK;
}

// Registration function for webserver initialization
esp_err_t register_charger_specs_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/charger_settings.html",
        .method    = HTTP_GET,
        .handler   = charger_specs_page_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
