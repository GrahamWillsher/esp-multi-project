#include "battery_specs_display_page.h"
#include "../utils/transmitter_manager.h"
#include "../page_definitions.h"
#include "../logging.h"
#include <ArduinoJson.h>

/**
 * @brief Battery Specs Display Page
 * 
 * Displays static battery configuration received from transmitter via MQTT
 * Source: BE/battery_specs MQTT topic
 */
esp_err_t battery_specs_page_handler(httpd_req_t *req) {
    // Get battery specs from TransmitterManager
    String specs_json = TransmitterManager::getBatterySpecsJson();
    
    // Parse JSON to extract values for display
    DynamicJsonDocument doc(512);
    String battery_type = "Unknown";
    uint32_t nominal_capacity_wh = 0;
    uint16_t max_design_voltage_dv = 0;
    uint8_t number_of_cells = 0;
    uint32_t min_design_voltage_dv = 0;
    float max_charge_current_a = 0;
    float max_discharge_current_a = 0;
    uint8_t battery_chemistry = 0;
    uint8_t cell_count_series = 0;
    uint8_t cell_count_parallel = 0;
    
    if (specs_json.length() > 0) {
        DeserializationError error = deserializeJson(doc, specs_json);
        if (!error) {
            // Extract fields safely with defaults
            battery_type = doc["battery_type"].as<String>();
            if (battery_type.length() == 0) battery_type = "Unknown";
            
            nominal_capacity_wh = doc["nominal_capacity_wh"] | 30000;
            // Transmitter sends these as floats already converted (V, not dV)
            float max_voltage_v = doc["max_design_voltage"] | 500.0f;
            max_design_voltage_dv = (uint16_t)(max_voltage_v * 10);
            number_of_cells = doc["number_of_cells"] | 96;
            float min_voltage_v = doc["min_design_voltage"] | 270.0f;
            min_design_voltage_dv = (uint32_t)(min_voltage_v * 10);
            // Charge/discharge currents are not in battery specs, use defaults
            max_charge_current_a = 120.0f;
            max_discharge_current_a = 120.0f;
            battery_chemistry = doc["battery_chemistry"] | 0;
            cell_count_series = doc["cell_count_series"] | 32;
            cell_count_parallel = doc["cell_count_parallel"] | 3;
        }
    }
    
    // HTML page
    const char* html_header = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Battery Specifications</title>
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
        .spec-card.alert-none { border-left-color: #667eea; }
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
        .status-grid {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 15px;
            padding: 20px;
            background: white;
            border-radius: 12px;
            box-shadow: 0 5px 20px rgba(0, 0, 0, 0.1);
            margin-bottom: 20px;
        }
        .status-item {
            padding: 15px;
            background: #f8f9fa;
            border-radius: 8px;
            border-left: 4px solid #667eea;
        }
        .status-label { color: black; font-size: 0.9em; }
        .status-value { color: #333; font-size: 1.4em; font-weight: 700; }
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
            background: #5568d3;
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
            .status-grid { grid-template-columns: 1fr; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🔋 Battery Specifications</h1>
            <p>Battery Emulator Configuration (Real-time from MQTT)</p>
        </div>
        
        <div class="source-info">
            📡 Source: Battery Emulator via MQTT Topic: <strong>BE/battery_specs</strong>
        </div>
)";

    const char* html_specs_section = R"(
        <div class="specs-grid">
            <div class="spec-card">
                <div class="spec-label">Battery Type</div>
                <div class="spec-value" id="batteryTypeValue">%s</div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Nominal Capacity</div>
                <div class="spec-value" id="nominalCapacityValue">%lu<span class="spec-unit">Wh</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Max Design Voltage</div>
                <div class="spec-value" id="maxDesignVoltageValue">%.1f<span class="spec-unit">V</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Min Design Voltage</div>
                <div class="spec-value" id="minDesignVoltageValue">%.1f<span class="spec-unit">V</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Number of Cells</div>
                <div class="spec-value" id="numberOfCellsValue">%d</div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Max Charge Current</div>
                <div class="spec-value" id="maxChargeCurrentValue">%.1f<span class="spec-unit">A</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Max Discharge Current</div>
                <div class="spec-value" id="maxDischargeCurrentValue">%.1f<span class="spec-unit">A</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Chemistry</div>
                <div class="spec-value" id="chemistryValue">%d</div>
            </div>
        </div>
)";

    const char* html_footer = R"(
        <div class="nav-buttons">
            <a href="/" class="btn btn-secondary">← Back to Dashboard</a>
            <a href="/charger_settings.html" class="btn btn-secondary">Charger Specs →</a>
            <a href="/inverter_settings.html" class="btn btn-secondary">Inverter Specs →</a>
        </div>
    </div>
    <script>
        function loadSelectedBatteryType() {
            fetch('/api/get_selected_types')
                .then(response => response.json())
                .then(selected => {
                    const typeId = selected.battery_type;
                    return fetch('/api/get_battery_types')
                        .then(response => response.json())
                        .then(types => {
                            const match = types.types.find(t => t.id === typeId);
                            const label = match ? `${match.name}` : 'Unknown';
                            const el = document.getElementById('selectedBatteryType');
                            if (el) {
                                el.textContent = label;
                            }

                            const typeEl = document.getElementById('batteryTypeValue');
                            if (typeEl) {
                                const current = (typeEl.textContent || '').trim();
                                if (current === '' || current === 'Unknown' || current === 'TEST_DUMMY') {
                                    typeEl.textContent = label;
                                }
                            }
                        });
                })
                .catch(error => {
                    const el = document.getElementById('selectedBatteryType');
                    if (el) {
                        el.textContent = 'Unavailable';
                    }
                    console.error('Failed to load selected battery type:', error);
                });
        }

        let batterySettingsRetries = 0;
        const MAX_BATTERY_SETTINGS_RETRIES = 5;

        function loadBatterySettingsFallback() {
            fetch('/api/get_battery_settings')
                .then(response => response.json())
                .then(data => {
                    if (!data.success) {
                        if (batterySettingsRetries < MAX_BATTERY_SETTINGS_RETRIES) {
                            batterySettingsRetries++;
                            setTimeout(loadBatterySettingsFallback, 1000);
                        }
                        return;
                    }

                    const chemistryNames = ['NCA', 'NMC', 'LFP', 'LTO'];

                    const nominal = document.getElementById('nominalCapacityValue');
                    if (nominal) nominal.innerHTML = `${data.capacity_wh}<span class="spec-unit">Wh</span>`;

                    const maxV = document.getElementById('maxDesignVoltageValue');
                    if (maxV) maxV.innerHTML = `${(data.max_voltage_mv / 1000).toFixed(1)}<span class="spec-unit">V</span>`;

                    const minV = document.getElementById('minDesignVoltageValue');
                    if (minV) minV.innerHTML = `${(data.min_voltage_mv / 1000).toFixed(1)}<span class="spec-unit">V</span>`;

                    const cells = document.getElementById('numberOfCellsValue');
                    if (cells) cells.textContent = data.cell_count;

                    const maxC = document.getElementById('maxChargeCurrentValue');
                    if (maxC) maxC.innerHTML = `${data.max_charge_current_a.toFixed(1)}<span class="spec-unit">A</span>`;

                    const maxD = document.getElementById('maxDischargeCurrentValue');
                    if (maxD) maxD.innerHTML = `${data.max_discharge_current_a.toFixed(1)}<span class="spec-unit">A</span>`;

                    const chem = document.getElementById('chemistryValue');
                    if (chem) chem.textContent = chemistryNames[data.chemistry] || String(data.chemistry);
                })
                .catch(error => console.error('Failed to load battery settings:', error));
        }

        window.addEventListener('load', () => {
            loadSelectedBatteryType();
            // Do not load fallback - battery specs page should only display values from MQTT BE/battery_specs
        });
    </script>
</body>
</html>
)";

    // Allocate response buffer (PSRAM for large allocations to avoid heap fragmentation)
    size_t html_header_len = strlen(html_header);
    size_t html_footer_len = strlen(html_footer);
    size_t specs_section_max = 2048;
    size_t total_size = html_header_len + specs_section_max + html_footer_len + 256; // +256 for safety
    
    char* response = (char*)ps_malloc(total_size);
    if (!response) {
        LOG_ERROR("BATTERY_PAGE", "Failed to allocate %d bytes in PSRAM", total_size);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    // Build response safely using snprintf to avoid buffer overflow
    char specs_section[2048];
    snprintf(specs_section, sizeof(specs_section), html_specs_section,
             battery_type.c_str(),
             nominal_capacity_wh,
             max_design_voltage_dv / 10.0f,
             min_design_voltage_dv / 10.0f,
             number_of_cells,
             max_charge_current_a / 10.0f,
             max_discharge_current_a / 10.0f,
             battery_chemistry);
    
    // Safe concatenation using snprintf
    size_t offset = 0;
    offset += snprintf(response + offset, total_size - offset, "%s", html_header);
    offset += snprintf(response + offset, total_size - offset, "%s", specs_section);
    offset += snprintf(response + offset, total_size - offset, "%s", html_footer);
    
    // Send response
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, response, strlen(response));
    
    free(response);
    LOG_INFO("BATTERY_PAGE", "Battery specs page served (%d bytes)", offset);
    
    return ESP_OK;
}

// Registration function for webserver initialization
esp_err_t register_battery_specs_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/battery_settings.html",
        .method    = HTTP_GET,
        .handler   = battery_specs_page_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
