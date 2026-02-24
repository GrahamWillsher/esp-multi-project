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
    
    <!-- Data Source Status -->
    <div class='info-box' style='margin: 20px 0; background: rgba(0,188,212,0.1); border-left: 5px solid #00BCD4;'>
        <h3 style='margin: 0 0 10px 0; color: #00BCD4;'>📊 Data Mode</h3>
        <div style='display: flex; align-items: center; gap: 15px;'>
            <div>
                <div style='color: #888; font-size: 13px;'>Current Mode</div>
                <div id='txDataMode' style='font-size: 18px; font-weight: bold; color: #4CAF50; margin-top: 5px;'>Loading...</div>
            </div>
            <div style='color: #888; font-size: 12px;'>
                <strong>Note:</strong> Test mode toggle is controlled on the transmitter.<br>
                To switch between test (dummy) and live (battery) data, see transmitter settings.
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
        async function updateDataMode() {
            try {
                const res = await fetch('/api/get_data_source');
                const data = await res.json();
                const modeEl = document.getElementById('txDataMode');
                if (data.mode === 'simulated') {
                    modeEl.textContent = 'Test Mode (Simulated Data)';
                    modeEl.style.color = '#FFD700';
                } else {
                    modeEl.textContent = 'Live Mode (Real Data)';
                    modeEl.style.color = '#4CAF50';
                }
            } catch (e) {
                document.getElementById('txDataMode').textContent = 'Unknown';
            }
        }
        updateDataMode();
        setInterval(updateDataMode, 2000);  // Update every 2s
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
