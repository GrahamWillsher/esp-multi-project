#include "transmitter_hub_page.h"
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
    if (TransmitterManager::hasMetadata()) {
        uint8_t major, minor, patch;
        TransmitterManager::getMetadataVersion(major, minor, patch);
        char version_str[16];
        snprintf(version_str, sizeof(version_str), "v%d.%d.%d", major, minor, patch);
        version_text = String(version_str);
        build_date = String(TransmitterManager::getMetadataBuildDate());
    }
    
    String content = R"rawliteral(
    <!-- Breadcrumb -->
    <div style='margin-bottom: 20px; padding: 10px; background: rgba(0,0,0,0.2); border-radius: 5px; font-size: 14px;'>
        <a href='/' style='color: #888; text-decoration: none;'>Dashboard</a>
        <span style='color: #888; margin: 0 8px;'>></span>
        <span style='color: #2196F3;'>Transmitter</span>
    </div>
    
    <h1 style='color: #2196F3;'>📡 Transmitter Management</h1>
    <p style='color: #888; margin-top: -10px;'>ESP32-POE-ISO</p>
    
    <!-- Status Summary -->
    <div class='info-box' style='margin: 20px 0; border-left: 5px solid )rawliteral";
    content += status_color;
    content += R"rawliteral(;'>
        <h3 style='margin: 0 0 15px 0;'>📊 Status Summary</h3>
        <div style='display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px;'>
            <div>
                <div style='color: #888; font-size: 13px;'>Connection</div>
                <div style='font-size: 18px; font-weight: bold; color: )rawliteral";
    content += status_color;
    content += R"rawliteral(; margin-top: 5px;'>)rawliteral";
    content += status_text;
    content += R"rawliteral(</div>
            </div>
            <div>
                <div style='color: #888; font-size: 13px;'>IP Address</div>
                <div style='font-size: 16px; font-weight: bold; margin-top: 5px; font-family: monospace;'>)rawliteral";
    content += ip_text;
    content += R"rawliteral(</div>
            </div>
            <div>
                <div style='color: #888; font-size: 13px;'>Firmware</div>
                <div style='font-size: 16px; font-weight: bold; margin-top: 5px;'>)rawliteral";
    content += version_text;
    content += R"rawliteral(</div>
            </div>
            <div>
                <div style='color: #888; font-size: 13px;'>Build Date</div>
                <div style='font-size: 13px; margin-top: 5px; color: #888;'>)rawliteral";
    content += build_date.isEmpty() ? "Unknown" : build_date;
    content += R"rawliteral(</div>
            </div>
        </div>
    </div>
    
    <!-- Test Data Mode Control -->
    <div class='info-box' style='margin: 20px 0; background: rgba(76,175,80,0.1); border-left: 5px solid #4CAF50;'>
        <h3 style='margin: 0 0 15px 0; color: #4CAF50;'>🧪 Test Data Mode Control</h3>
        <div style='display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px;'>
            <div>
                <div style='color: #888; font-size: 13px; margin-bottom: 8px;'>Current Mode</div>
                <div id='txTestDataMode' style='font-size: 16px; font-weight: bold; color: #2196F3; margin-bottom: 15px; min-height: 25px;'>Loading...</div>
                <div style='color: #888; font-size: 12px;'>
                    <strong>Available Modes:</strong><br>
                    • <strong>OFF</strong> - Real CAN data only<br>
                    • <strong>SOC_POWER_ONLY</strong> - Test SOC & power<br>
                    • <strong>FULL_BATTERY_DATA</strong> - Test all battery data
                </div>
            </div>
            <div>
                <div style='color: #888; font-size: 13px; margin-bottom: 8px;'>Set Mode</div>
                <div style='display: flex; gap: 8px; flex-wrap: wrap;'>
                    <button onclick='setTestDataMode(0)' style='flex: 1; min-width: 80px; padding: 8px; background: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer; font-weight: bold;' id='btnModeOff'>OFF</button>
                    <button onclick='setTestDataMode(1)' style='flex: 1; min-width: 80px; padding: 8px; background: #FF9800; color: white; border: none; border-radius: 4px; cursor: pointer; font-weight: bold;' id='btnModeSoc'>SOC_POWER</button>
                    <button onclick='setTestDataMode(2)' style='flex: 1; min-width: 80px; padding: 8px; background: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer; font-weight: bold;' id='btnModeFull'>FULL</button>
                </div>
                <div id='modeStatus' style='color: #888; font-size: 12px; margin-top: 10px; min-height: 30px;'></div>
            </div>
        </div>
    </div>
    
    <!-- Navigation Cards -->
    <h3 style='margin: 30px 0 15px 0;'>⚙️ Functions</h3>
    <div style='display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px;'>
        
        <!-- Configuration -->
        <a href='/transmitter/config' style='text-decoration: none;'>
            <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #2196F3;'
                 onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#42A5F5"'
                 onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#2196F3"'>
                <div style='font-size: 36px; margin: 10px 0;'>⚙️</div>
                <div style='font-weight: bold; color: #2196F3; font-size: 16px;'>Configuration</div>
                <div style='font-size: 12px; color: #888; margin-top: 8px;'>Network, MQTT, Settings</div>
            </div>
        </a>
        
        <!-- Battery Settings -->
        <a href='/transmitter/battery' style='text-decoration: none;'>
            <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #2196F3;'
                 onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#42A5F5"'
                 onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#2196F3"'>
                <div style='font-size: 36px; margin: 10px 0;'>🔋</div>
                <div style='font-weight: bold; color: #2196F3; font-size: 16px;'>Battery Settings</div>
                <div style='font-size: 12px; color: #888; margin-top: 8px;'>Capacity, Limits, Chemistry</div>
            </div>
        </a>
        
        <!-- Inverter Settings -->
        <a href='/transmitter/inverter' style='text-decoration: none;'>
            <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #2196F3;'
                 onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#42A5F5"'
                 onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#2196F3"'>
                <div style='font-size: 36px; margin: 10px 0;'>⚡</div>
                <div style='font-weight: bold; color: #2196F3; font-size: 16px;'>Inverter Settings</div>
                <div style='font-size: 12px; color: #888; margin-top: 8px;'>Protocol Selection</div>
            </div>
        </a>
        
        <!-- Monitor (Polling) -->
        <a href='/transmitter/monitor' style='text-decoration: none;'>
            <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #2196F3;'
                 onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#42A5F5"'
                 onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#2196F3"'>
                <div style='font-size: 36px; margin: 10px 0;'>📊</div>
                <div style='font-weight: bold; color: #2196F3; font-size: 16px;'>Monitor (Polling)</div>
                <div style='font-size: 12px; color: #888; margin-top: 8px;'>1-second refresh</div>
            </div>
        </a>
        
        <!-- Monitor (Real-time) -->
        <a href='/transmitter/monitor2' style='text-decoration: none;'>
            <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #2196F3;'
                 onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#42A5F5"'
                 onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#2196F3"'>
                <div style='font-size: 36px; margin: 10px 0;'>📈</div>
                <div style='font-weight: bold; color: #2196F3; font-size: 16px;'>Monitor (Real-time)</div>
                <div style='font-size: 12px; color: #888; margin-top: 8px;'>SSE live data</div>
            </div>
        </a>
        
        <!-- Reboot -->
        <a href='/transmitter/reboot' style='text-decoration: none;'>
            <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #ff6b35;'
                 onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#ff8c5a"'
                 onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#ff6b35"'>
                <div style='font-size: 36px; margin: 10px 0;'>🔄</div>
                <div style='font-weight: bold; color: #ff6b35; font-size: 16px;'>Reboot Device</div>
                <div style='font-size: 12px; color: #888; margin-top: 8px;'>Restart transmitter</div>
            </div>
        </a>
        
    </div>
    
    <!-- Back to Dashboard -->
    <div style='margin-top: 30px; text-align: center;'>
        <a href='/' style='display: inline-block; padding: 12px 30px; background: rgba(255,255,255,0.1); border-radius: 5px; text-decoration: none; color: #888; transition: background 0.2s;'
           onmouseover='this.style.background="rgba(255,255,255,0.2)"'
           onmouseout='this.style.background="rgba(255,255,255,0.1)"'>
            ← Back to Dashboard
        </a>
    </div>

    <script>
        // Update test data mode display
        async function updateTestDataMode() {
            try {
                const res = await fetch('/api/get_test_data_mode');
                const data = await res.json();
                const modeEl = document.getElementById('txTestDataMode');
                let modeText = 'Unknown';
                let modeColor = '#888';
                
                if (data.mode === 0 || data.mode === 'OFF') {
                    modeText = 'OFF (Real CAN Data Only)';
                    modeColor = '#f44336';
                } else if (data.mode === 1 || data.mode === 'SOC_POWER_ONLY') {
                    modeText = 'SOC_POWER_ONLY (Test SOC & Power)';
                    modeColor = '#FF9800';
                } else if (data.mode === 2 || data.mode === 'FULL_BATTERY_DATA') {
                    modeText = 'FULL_BATTERY_DATA (All Test Data)';
                    modeColor = '#4CAF50';
                }
                
                modeEl.textContent = modeText;
                modeEl.style.color = modeColor;
            } catch (e) {
                console.error('Error fetching test data mode:', e);
                document.getElementById('txTestDataMode').textContent = 'Error fetching status';
            }
        }
        
        // Set test data mode
        async function setTestDataMode(mode) {
            try {
                const statusEl = document.getElementById('modeStatus');
                statusEl.textContent = 'Sending...';
                statusEl.style.color = '#888';
                
                const res = await fetch('/api/set_test_data_mode', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json'
                    },
                    body: JSON.stringify({ mode: mode })
                });
                
                if (res.ok) {
                    statusEl.textContent = '✓ Mode changed successfully';
                    statusEl.style.color = '#4CAF50';
                    // Refresh the display after a short delay
                    setTimeout(updateTestDataMode, 500);
                } else {
                    statusEl.textContent = '✗ Failed to set mode. Transmitter may be disconnected.';
                    statusEl.style.color = '#f44336';
                }
            } catch (e) {
                console.error('Error setting test data mode:', e);
                document.getElementById('modeStatus').textContent = '✗ Error: ' + e.message;
                document.getElementById('modeStatus').style.color = '#f44336';
            }
        }
        
        // Initial update and set interval
        updateTestDataMode();
        setInterval(updateTestDataMode, 3000);  // Update every 3s
    </script>
    )rawliteral";
    
    String page = generatePage("Transmitter Hub", content, "/transmitter");
    return httpd_resp_send(req, page.c_str(), page.length());
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
