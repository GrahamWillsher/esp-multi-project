#include "dashboard_page.h"
#include "../common/page_generator.h"
#include "../utils/transmitter_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include <firmware_version.h>

/**
 * @brief Handler for the dashboard landing page
 * 
 * Shows two device cards (Transmitter + Receiver) with status indicators
 * and system tools (Debug, OTA) at the bottom.
 */
static esp_err_t dashboard_handler(httpd_req_t *req) {
    // Get transmitter status
    bool tx_connected = TransmitterManager::isMACKnown();
    String tx_status = "Disconnected";
    String tx_status_color = "#ff6b35"; // Red/orange
    String tx_ip = "Unknown";
    String tx_ip_mode = "";  // (D) or (S)
    String tx_version = "Unknown";
    bool request_metadata = false;
    
    if (tx_connected) {
        tx_status = "Connected";
        tx_status_color = "#4CAF50"; // Green
        tx_ip = TransmitterManager::getIPString();
        if (tx_ip == "0.0.0.0") {
            tx_ip = "Not available";
        } else {
            // Show IP mode immediately
            tx_ip_mode = TransmitterManager::isStaticIP() ? " (S)" : " (D)";
        }
        
        if (TransmitterManager::hasMetadata()) {
            uint8_t major, minor, patch;
            TransmitterManager::getMetadataVersion(major, minor, patch);
            char version_str[16];
            snprintf(version_str, sizeof(version_str), "%d.%d.%d", major, minor, patch);
            tx_version = String(version_str);
        } else {
            // No metadata in cache - request it
            request_metadata = true;
        }
    }
    
    // Receiver status (always online)
    String rx_version = String(FW_VERSION_MAJOR) + "." + 
                        String(FW_VERSION_MINOR) + "." + 
                        String(FW_VERSION_PATCH);
    String rx_ip = WiFi.localIP().toString();
    String rx_ip_mode = " (S)";  // Receiver always uses static IP from Config
    
    String content = R"rawliteral(
    <h1>ESP-NOW System Dashboard</h1>
    
    <div style='display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin: 30px 0;'>
        
        <!-- Transmitter Device Card -->
        <a href='/transmitter' style='text-decoration: none;'>
            <div class='info-box' style='cursor: pointer; transition: transform 0.2s, box-shadow 0.2s; border-left: 5px solid #2196F3;'>
                <div onmouseover='this.parentElement.style.transform="translateY(-5px)"; this.parentElement.style.boxShadow="0 8px 20px rgba(0,0,0,0.3)";' 
                     onmouseout='this.parentElement.style.transform="translateY(0)"; this.parentElement.style.boxShadow="0 4px 6px rgba(0,0,0,0.2)";'>
                    <h2 style='margin: 0 0 15px 0; color: #2196F3;'>📡 Transmitter</h2>
                    <p style='color: #888; font-size: 14px; margin: 5px 0;'>ESP32-POE-ISO</p>
                    
                    <div style='margin: 20px 0; padding: 15px; background: rgba(0,0,0,0.3); border-radius: 8px;'>
                        <div style='display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px;'>
                            <div style='display: flex; align-items: center;'>
                                <span style='width: 12px; height: 12px; border-radius: 50%; background: )rawliteral";
    content += tx_status_color;
    content += R"rawliteral(; margin-right: 10px;'></span>
                                <span style='font-weight: bold; color: #FFD700;'>Status:</span>
                            </div>
                            <span id='txStatus' style='color: )rawliteral";
    content += tx_status_color;
    content += R"rawliteral(; font-weight: bold;'>)rawliteral";
    content += tx_status;
    content += R"rawliteral(</span>
                        </div>
                        <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
                            <span style='color: #FFD700; font-weight: bold;'>IP:</span>
                            <span>
                                <span id='txIP' style='font-family: monospace; color: #fff;'>)rawliteral";
    content += tx_ip;
    content += R"rawliteral(</span>
                                <span id='txIPMode' style='color: #888; font-size: 11px; margin-left: 5px;'>)rawliteral";
    content += tx_ip_mode;
    content += R"rawliteral(</span>
                            </span>
                        </div>
                        <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
                            <span style='color: #FFD700; font-weight: bold;'>Firmware:</span>
                            <span id='txVersion' style='color: #fff;'>)rawliteral";
    content += tx_version;
    content += R"rawliteral(</span>
                        </div>
                        <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
                            <span style='color: #FFD700; font-weight: bold;'>MAC:</span>
                            <span id='txMAC' style='font-family: monospace; font-size: 11px; color: #fff;'>)rawliteral";
    content += TransmitterManager::getMACString();
    content += R"rawliteral(</span>
                        </div>
                    </div>
                    
                    <div style='text-align: center; margin-top: 20px; padding: 12px; background: #2196F3; border-radius: 5px; color: white; font-weight: bold;'>
                        Click to Manage →
                    </div>
                </div>
            </div>
        </a>
        
        <!-- Receiver Device Card -->
        <a href='/receiver/config' style='text-decoration: none;'>
            <div class='info-box' style='cursor: pointer; transition: transform 0.2s, box-shadow 0.2s; border-left: 5px solid #4CAF50;'>
                <div onmouseover='this.parentElement.style.transform="translateY(-5px)"; this.parentElement.style.boxShadow="0 8px 20px rgba(0,0,0,0.3)";' 
                     onmouseout='this.parentElement.style.transform="translateY(0)"; this.parentElement.style.boxShadow="0 4px 6px rgba(0,0,0,0.2)";'>
                    <h2 style='margin: 0 0 15px 0; color: #4CAF50;'>📱 Receiver</h2>
                    <p style='color: #888; font-size: 14px; margin: 5px 0;'>T-Display-S3</p>
                    
                    <div style='margin: 20px 0; padding: 15px; background: rgba(0,0,0,0.3); border-radius: 8px;'>
                        <div style='display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px;'>
                            <div style='display: flex; align-items: center;'>
                                <span style='width: 12px; height: 12px; border-radius: 50%; background: #4CAF50; margin-right: 10px;'></span>
                                <span style='font-weight: bold; color: #FFD700;'>Status:</span>
                            </div>
                            <span style='color: #4CAF50; font-weight: bold;'>Online</span>
                        </div>
                        <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
                            <span style='color: #FFD700; font-weight: bold;'>IP:</span>
                            <span>
                                <span style='font-family: monospace; color: #fff;'>)rawliteral";
    content += rx_ip;
    content += R"rawliteral(</span>
                                <span id='rxIPMode' style='color: #888; font-size: 11px; margin-left: 5px;'>)rawliteral";
    content += rx_ip_mode;
    content += R"rawliteral(</span>
                            </span>
                        </div>
                        <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
                            <span style='color: #FFD700; font-weight: bold;'>Firmware:</span>
                            <span style='color: #fff;'>)rawliteral";
    content += rx_version;
    content += R"rawliteral(</span>
                        </div>
                        <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
                            <span style='color: #FFD700; font-weight: bold;'>MAC:</span>
                            <span style='font-family: monospace; font-size: 11px; color: #fff;'>)rawliteral";
    content += WiFi.macAddress();
    content += R"rawliteral(</span>
                        </div>
                    </div>
                    
                    <div style='text-align: center; margin-top: 20px; padding: 12px; background: #4CAF50; border-radius: 5px; color: white; font-weight: bold;'>
                        Click to Manage →
                    </div>
                </div>
            </div>
        </a>
        
    </div>
    
    <!-- ESP-NOW Link Visualization -->
    <div style='text-align: center; margin: 20px 0; padding: 15px; background: rgba(0,0,0,0.3); border-radius: 8px;'>
        <span style='color: #FFD700; font-size: 14px; font-weight: bold;'>ESP-NOW Communication: </span>
        <span id='espnowLink' style='font-weight: bold; color: )rawliteral";
    content += tx_status_color;
    content += R"rawliteral(;'>)rawliteral";
    content += (tx_connected ? "📡 Active" : "⚠️ Waiting for connection");
    content += R"rawliteral(</span>
    </div>
    
    <!-- System Tools Section -->
    <div class='info-box' style='margin-top: 30px;'>
        <h3 style='margin: 0 0 20px 0; color: #FF9800;'>🛠️ System Tools</h3>
        <div style='display: grid; grid-template-columns: 1fr 1fr; gap: 15px;'>
            <a href='/debug' style='text-decoration: none;'>
                <div style='padding: 15px; background: rgba(255,152,0,0.1); border: 2px solid #FF9800; border-radius: 8px; text-align: center; cursor: pointer; transition: background 0.2s;'
                     onmouseover='this.style.background="rgba(255,152,0,0.2)"'
                     onmouseout='this.style.background="rgba(255,152,0,0.1)"'>
                    <span style='font-size: 24px;'>🐛</span>
                    <div style='margin-top: 10px; color: #FF9800; font-weight: bold;'>Debug Logging</div>
                    <div style='font-size: 12px; color: #888; margin-top: 5px;'>Control logging levels</div>
                </div>
            </a>
            <a href='/ota' style='text-decoration: none;'>
                <div style='padding: 15px; background: rgba(255,152,0,0.1); border: 2px solid #FF9800; border-radius: 8px; text-align: center; cursor: pointer; transition: background 0.2s;'
                     onmouseover='this.style.background="rgba(255,152,0,0.2)"'
                     onmouseout='this.style.background="rgba(255,152,0,0.1)"'>
                    <span style='font-size: 24px;'>📤</span>
                    <div style='margin-top: 10px; color: #FF9800; font-weight: bold;'>OTA Update</div>
                    <div style='font-size: 12px; color: #888; margin-top: 5px;'>Update firmware</div>
                </div>
            </a>
        </div>
    </div>
    
    <script>
        // Request metadata if not available
        )rawliteral";
    if (request_metadata) {
        content += R"rawliteral(
        // Request firmware metadata from transmitter on page load
        fetch('/api/request_metadata').catch(e => console.error('Failed to request metadata:', e));
        )rawliteral";
    }
    content += R"rawliteral(
        
        // Update transmitter data every 10 seconds
        setInterval(async function() {
            try {
                const response = await fetch('/api/dashboard_data');
                const data = await response.json();
                
                // Update transmitter status (dynamic - can change)
                if (data.transmitter) {
                    const tx = data.transmitter;
                    const statusEl = document.getElementById('txStatus');
                    const linkEl = document.getElementById('espnowLink');
                    const txIPEl = document.getElementById('txIP');
                    const txIPModeEl = document.getElementById('txIPMode');
                    const txVersionEl = document.getElementById('txVersion');
                    const txMACEl = document.getElementById('txMAC');
                    
                    if (tx.connected) {
                        statusEl.textContent = 'Connected';
                        statusEl.style.color = '#4CAF50';
                        
                        // Update IP and mode (can change if transmitter reconfigures)
                        if (tx.ip && tx.ip !== 'Unknown' && tx.ip !== '0.0.0.0') {
                            txIPEl.textContent = tx.ip;
                            txIPModeEl.textContent = tx.is_static ? ' (S)' : ' (D)';
                        } else if (tx.ip === '0.0.0.0') {
                            txIPEl.textContent = 'Not available';
                            txIPModeEl.textContent = '';
                        }
                        if (tx.firmware && tx.firmware !== 'Unknown') {
                            txVersionEl.textContent = tx.firmware;
                        }
                        if (tx.mac && tx.mac !== 'Unknown') {
                            txMACEl.textContent = tx.mac;
                        }
                    } else {
                        statusEl.textContent = 'Disconnected';
                        statusEl.style.color = '#ff6b35';
                        linkEl.textContent = '⚠️ Waiting for connection';
                        linkEl.style.color = '#ff6b35';
                    }
                }
            } catch (e) {
                console.error('Failed to update dashboard:', e);
            }
        }, 10000);
    </script>
    )rawliteral";
    
    String page = generatePage("Dashboard", content, "/");
    return httpd_resp_send(req, page.c_str(), page.length());
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
