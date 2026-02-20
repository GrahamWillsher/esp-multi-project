#include "system_specs_display_page.h"
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
    String firmware_version = "1.0.0";
    String build_date = "Unknown";
    uint8_t can_speed_kbps = 250;
    uint8_t supports_diagnostics = 0;
    
    if (specs_json.length() > 0) {
        DeserializationError error = deserializeJson(doc, specs_json);
        if (!error) {
            hardware_model = doc["hardware_model"].as<String>();
            if (hardware_model.length() == 0) hardware_model = "ESP32-POE-ISO";
            
            can_interface = doc["can_interface"].as<String>();
            if (can_interface.length() == 0) can_interface = "MCP2515";
            
            firmware_version = doc["firmware_version"].as<String>();
            if (firmware_version.length() == 0) firmware_version = "1.0.0";
            
            build_date = doc["build_date"].as<String>();
            if (build_date.length() == 0) build_date = "Not available";
            
            can_speed_kbps = doc["can_speed_kbps"] | 250;
            supports_diagnostics = doc["supports_diagnostics"] | 1;
        }
    }
    
    // HTML page
    const char* html_header = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>System Specifications</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
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
            border-left: 5px solid #667eea;
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
            word-break: break-word;
        }
        .spec-unit {
            font-size: 0.9em;
            color: #999;
        }
        .feature-badge {
            display: inline-block;
            padding: 5px 12px;
            background: #667eea;
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
            background: rgba(102, 126, 234, 0.1);
            border: 1px solid #667eea;
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
            background: #667eea;
            color: white;
        }
        .btn-primary:hover {
            background: #505aa8;
            box-shadow: 0 5px 15px rgba(102, 126, 234, 0.4);
        }
        .btn-secondary {
            background: white;
            color: #667eea;
            border: 2px solid #667eea;
        }
        .btn-secondary:hover {
            background: #667eea;
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
            <h1>🖥️ System Specifications</h1>
            <p>System Configuration (Real-time from MQTT)</p>
        </div>
        
        <div class="source-info">
            📡 Source: Battery Emulator via MQTT Topic: <strong>BE/battery_specs</strong>
        </div>
)";

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

    const char* html_footer = R"(
        <div class="nav-buttons">
            <a href="/" class="btn btn-secondary">← Back to Dashboard</a>
            <a href="/charger_settings.html" class="btn btn-secondary">← Charger Specs</a>
            <a href="/battery_settings.html" class="btn btn-secondary">Battery Specs →</a>
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
    offset += snprintf(response + offset, total_size - offset, "%s", html_header);
    offset += snprintf(response + offset, total_size - offset, "%s", specs_section);
    offset += snprintf(response + offset, total_size - offset, "%s", html_footer);
    
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
