#include "inverter_specs_display_page.h"
#include "../utils/transmitter_manager.h"
#include "../../receiver_config/receiver_config_manager.h"
#include "../page_definitions.h"
#include "../logging.h"
#include <ArduinoJson.h>

/**
 * @brief Lookup inverter protocol name by type ID
 * Maps inverter type ID to protocol display name
 * Used as fallback when MQTT specs unavailable
 */
static const char* get_inverter_protocol_name(uint8_t inverter_type) {
    static const struct {
        uint8_t id;
        const char* name;
    } inverter_types[] = {
        {0, "None"},
        {1, "Afore battery over CAN"},
        {2, "BYD Battery-Box Premium HVS over CAN Bus"},
        {3, "BYD 11kWh HVM battery over Modbus RTU"},
        {4, "Ferroamp Pylon battery over CAN bus"},
        {5, "FoxESS compatible HV2600/ECS4100 battery"},
        {6, "Growatt High Voltage protocol via CAN"},
        {7, "Growatt Low Voltage (48V) protocol via CAN"},
        {8, "Growatt WIT compatible battery via CAN"},
        {9, "BYD battery via Kostal RS485"},
        {10, "Pylontech HV battery over CAN bus"},
        {11, "Pylontech LV battery over CAN bus"},
        {12, "Schneider V2 SE BMS CAN"},
        {13, "SMA compatible BYD H"},
        {14, "SMA compatible BYD Battery-Box HVS"},
        {15, "SMA Low Voltage (48V) protocol via CAN"},
        {16, "SMA Tripower CAN"},
        {17, "Sofar BMS (Extended) via CAN, Battery ID"},
        {18, "SolaX Triple Power LFP over CAN bus"},
        {19, "Solxpow compatible battery"},
        {20, "Sol-Ark LV protocol over CAN bus"},
        {21, "Sungrow SBRXXX emulation over CAN bus"}
    };
    
    for (size_t i = 0; i < sizeof(inverter_types) / sizeof(inverter_types[0]); i++) {
        if (inverter_types[i].id == inverter_type) {
            return inverter_types[i].name;
        }
    }
    
    return "Unknown";
}

/**
 * @brief Inverter Specs Display Page
 * 
 * Displays static inverter configuration received from transmitter via MQTT
 * Source: BE/spec_data_2 MQTT topic
 */
esp_err_t inverter_specs_page_handler(httpd_req_t *req) {
    // Get inverter specs from TransmitterManager
    String specs_json = TransmitterManager::getInverterSpecsJson();
    
    // Parse JSON to extract values for display
    DynamicJsonDocument doc(512);
    String inverter_protocol = "Unknown";
    uint16_t min_input_voltage_dv = 0;
    uint16_t max_input_voltage_dv = 0;
    uint16_t nominal_output_voltage_dv = 0;
    uint16_t max_output_power_w = 0;
    uint8_t supports_modbus = 0;
    uint8_t supports_can = 0;
    uint16_t efficiency_percent = 0;
    uint8_t input_phases = 0;
    uint8_t output_phases = 0;
    
    if (specs_json.length() > 0) {
        DeserializationError error = deserializeJson(doc, specs_json);
        if (!error) {
            inverter_protocol = doc["inverter_protocol"].as<String>();
            
            min_input_voltage_dv = doc["min_input_voltage_dv"] | 1800;
            max_input_voltage_dv = doc["max_input_voltage_dv"] | 5500;
            nominal_output_voltage_dv = doc["nominal_output_voltage_dv"] | 2300;
            max_output_power_w = doc["max_output_power_w"] | 10000;
            supports_modbus = doc["supports_modbus"] | 0;
            supports_can = doc["supports_can"] | 0;
            efficiency_percent = doc["efficiency_percent"] | 950;  // 95.0%
            input_phases = doc["input_phases"] | 3;
            output_phases = doc["output_phases"] | 3;
        }
    }
    
    // Fallback: if protocol is still empty, use selected inverter type to get protocol name
    if (inverter_protocol.length() == 0 || inverter_protocol == "Unknown") {
        uint8_t selected_type = ReceiverNetworkConfig::getInverterType();
        inverter_protocol = get_inverter_protocol_name(selected_type);
    }
    
    // HTML page
    const char* html_header = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Inverter Specifications</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #f093fb 0%, #f5576c 100%);
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
            border-left: 5px solid #f5576c;
            transition: transform 0.3s ease, box-shadow 0.3s ease;
        }
        .spec-card:hover {
            transform: translateY(-5px);
            box-shadow: 0 10px 30px rgba(0, 0, 0, 0.15);
        }
        .spec-label {
            font-size: 0.9em;
            color: #888;
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
            background: #f5576c;
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
            background: rgba(245, 87, 108, 0.1);
            border: 1px solid #f5576c;
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
            background: #f5576c;
            color: white;
        }
        .btn-primary:hover {
            background: #d63d50;
            box-shadow: 0 5px 15px rgba(245, 87, 108, 0.4);
        }
        .btn-secondary {
            background: white;
            color: #f5576c;
            border: 2px solid #f5576c;
        }
        .btn-secondary:hover {
            background: #f5576c;
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
            <h1>⚡ Inverter Specifications</h1>
            <p>Inverter Configuration (Real-time from MQTT)</p>
        </div>
        
        <div class="source-info">
            📡 Source: Battery Emulator via MQTT Topic: <strong>BE/spec_data_2</strong>
        </div>
)";

    const char* html_specs_section = R"(
        <div class="specs-grid">
            <div class="spec-card">
                <div class="spec-label">Protocol</div>
                <div class="spec-value" style="font-size: 1.4em;">%s</div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Input Voltage Range</div>
                <div class="spec-value">%.1f - %.1f<span class="spec-unit">V</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Output Voltage</div>
                <div class="spec-value">%.1f<span class="spec-unit">V</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Max Output Power</div>
                <div class="spec-value">%u<span class="spec-unit">W</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Efficiency</div>
                <div class="spec-value">%.1f<span class="spec-unit">%%</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Input Phases</div>
                <div class="spec-value">%u</div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Output Phases</div>
                <div class="spec-value">%u</div>
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
        <div class="spec-card" style="margin-top: 20px;">
            <h3 style="margin-bottom: 15px; color: #333;">⚙️ Inverter Type Selection</h3>
            <p style="color: #666; font-size: 14px; margin-bottom: 15px;">Select the inverter protocol to use. The transmitter will switch to the selected protocol.</p>
            
            <div style="display: flex; gap: 10px; margin-bottom: 10px;">
                <select id='inverterType' style='flex: 1; padding: 10px; border: 1px solid #ddd; border-radius: 4px; font-size: 14px;'>
                    <option value=''>Loading inverter types...</option>
                </select>
                <button onclick='saveInverterType()' style='padding: 12px 40px; font-size: 16px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;'>Save</button>
            </div>
            <div id='inverterTypeStatus' style='font-size: 14px; color: #666; height: 20px;'></div>
        </div>
        
        <div class="nav-buttons">
            <a href="/" class="btn btn-secondary">← Back to Dashboard</a>
            <a href="/battery_settings.html" class="btn btn-secondary">← Battery Specs</a>
            <a href="/charger_settings.html" class="btn btn-secondary">Charger Specs →</a>
        </div>
    </div>
    
    <script>
        window.onload = function() {
            loadInverterTypes();
        };
        
        function loadInverterTypes() {
            console.log('Loading inverter types...');
            
            fetch('/api/get_inverter_types')
                .then(response => response.json())
                .then(data => {
                    const typeSelect = document.getElementById('inverterType');
                    typeSelect.innerHTML = '';
                    
                    data.types.forEach(type => {
                        const option = document.createElement('option');
                        option.value = type.id;
                        option.textContent = type.name;
                        typeSelect.appendChild(option);
                    });
                    
                    // Load current selection
                    loadCurrentInverterType();
                    console.log('Inverter types loaded');
                })
                .catch(error => console.error('Error loading inverter types:', error));
        }
        
        function loadCurrentInverterType() {
            fetch('/api/get_selected_types')
                .then(response => response.json())
                .then(data => {
                    const typeSelect = document.getElementById('inverterType');
                    typeSelect.value = data.inverter_type;
                    console.log('Current inverter type loaded:', data.inverter_type);
                })
                .catch(error => console.error('Error loading current type:', error));
        }
        
        function saveInverterType() {
            const typeSelect = document.getElementById('inverterType');
            const typeId = parseInt(typeSelect.value);
            const typeName = typeSelect.options[typeSelect.selectedIndex].text;
            const statusDiv = document.getElementById('inverterTypeStatus');
            
            console.log(`Saving inverter type: ${typeId} (${typeName})`);
            statusDiv.textContent = 'Saving...';
            statusDiv.style.color = '#ff9800';
            
            fetch('/api/set_inverter_type', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({type: typeId})
            })
                .then(response => response.json())
                .then(data => {
                    if (data.success) {
                        statusDiv.textContent = `✓ Saved ${typeName}. Transmitter is switching protocol...`;
                        statusDiv.style.color = '#4CAF50';
                        console.log('Inverter type saved successfully');
                    } else {
                        statusDiv.textContent = `✗ Error: ${data.error}`;
                        statusDiv.style.color = '#dc3545';
                        console.error('Failed to save inverter type:', data.error);
                    }
                })
                .catch(error => {
                    statusDiv.textContent = `✗ Error: ${error.message}`;
                    statusDiv.style.color = '#dc3545';
                    console.error('Error saving inverter type:', error);
                });
        }
    </script>
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
        LOG_ERROR("INVERTER_PAGE", "Failed to allocate %d bytes in PSRAM", total_size);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    // Build response safely
    char specs_section[2048];
    snprintf(specs_section, sizeof(specs_section), html_specs_section,
             inverter_protocol.c_str(),
             min_input_voltage_dv / 10.0f,
             max_input_voltage_dv / 10.0f,
             nominal_output_voltage_dv / 10.0f,
             max_output_power_w,
             efficiency_percent / 10.0f,
             input_phases,
             output_phases,
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
    LOG_INFO("INVERTER_PAGE", "Inverter specs page served (%d bytes)", offset);
    
    return ESP_OK;
}

// Registration function for webserver initialization
esp_err_t register_inverter_specs_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/inverter_settings.html",
        .method    = HTTP_GET,
        .handler   = inverter_specs_page_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
