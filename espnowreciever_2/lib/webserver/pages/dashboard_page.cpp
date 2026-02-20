#include "dashboard_page.h"
#include "../common/page_generator.h"
#include "../utils/transmitter_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include <firmware_version.h>
#include <firmware_metadata.h>

/**
 * @brief Helper function to capitalize first letter of each word
 */
static String capitalizeWords(const char* str) {
    if (!str || strlen(str) == 0) return String("");
    
    // Convert to String first to avoid char-by-char issues
    String input = String(str);
    String result;
    result.reserve(input.length() + 10);  // Pre-allocate to avoid reallocation
    
    bool capitalize_next = true;
    
    for (unsigned int i = 0; i < input.length(); i++) {
        char c = input.charAt(i);
        if (c == '-' || c == '_' || c == ' ') {
            result += ' ';
            capitalize_next = true;
        } else if (capitalize_next) {
            result += (char)toupper(c);
            capitalize_next = false;
        } else {
            result += (char)tolower(c);
        }
    }
    
    return result;
}

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
    String tx_device_name = "ESP32 Transmitter";  // Default friendly name
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
            
            // Get device name from env if available
            const char* env = TransmitterManager::getMetadataEnv();
            if (env && strlen(env) > 0) {
                tx_device_name = capitalizeWords(env);
            }
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
    
    // Get receiver device name from metadata
    String rx_device_name = "Unknown Device";
    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata)) {
        rx_device_name = capitalizeWords(FirmwareMetadata::metadata.env_name);
    }
    
    String content = R"rawliteral(
    <h1>ESP-NOW System Dashboard</h1>
    
    <div style='display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin: 30px 0;'>
        
        <!-- Transmitter Device Card -->
        <a href='/transmitter' style='text-decoration: none;'>
            <div class='info-box' style='cursor: pointer; transition: transform 0.2s, box-shadow 0.2s; border-left: 5px solid #2196F3;'>
                <div onmouseover='this.parentElement.style.transform="translateY(-5px)"; this.parentElement.style.boxShadow="0 8px 20px rgba(0,0,0,0.3)";' 
                     onmouseout='this.parentElement.style.transform="translateY(0)"; this.parentElement.style.boxShadow="0 4px 6px rgba(0,0,0,0.2)";'>
                    <h2 style='margin: 0 0 15px 0; color: #2196F3;'>📡 Transmitter</h2>
                    <p style='color: #888; font-size: 14px; margin: 5px 0;'>)rawliteral";
    content += tx_device_name;
    content += R"rawliteral(</p>
                    
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
                    <p style='color: #888; font-size: 14px; margin: 5px 0;'>)rawliteral";
    content += rx_device_name;
    content += R"rawliteral(</p>
                    
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

    <!-- Data Source Toggle -->
    <div class='info-box' style='margin: 20px 0; display: flex; align-items: center; justify-content: space-between;'>
        <div>
            <h3 style='margin: 0 0 6px 0; color: #FFD700;'>📊 Data Source</h3>
            <div style='color: #888; font-size: 13px;'>Use simulated data when no battery is connected</div>
        </div>
        <div style='display: flex; align-items: center; gap: 10px;'>
            <span id='dataSourceLabel' style='font-weight: bold; color: #FFD700;'>Loading...</span>
            <label style='position: relative; display: inline-block; width: 50px; height: 24px;'>
                <input id='dataSourceToggle' type='checkbox' style='opacity: 0; width: 0; height: 0;'>
                <span style='position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #666; transition: .2s; border-radius: 24px;'
                      id='dataSourceSlider'></span>
                <span style='position: absolute; content: ""; height: 18px; width: 18px; left: 3px; bottom: 3px; background-color: white; transition: .2s; border-radius: 50%;'
                      id='dataSourceKnob'></span>
            </label>
        </div>
    </div>
    
    <!-- Battery Emulator Specifications -->
    <div class='info-box' style='margin: 20px 0;'>
        <h3 style='margin: 0 0 20px 0; color: #4CAF50;'>🔋 Battery Emulator Specifications</h3>
        <p style='color: #888; font-size: 14px; margin: 0 0 15px 0;'>View static configuration data received via MQTT from transmitter</p>
        <div style='display: grid; grid-template-columns: 1fr 1fr 1fr 1fr; gap: 15px;'>
            <a href='/battery_settings.html' style='text-decoration: none;'>
                <div style='padding: 15px; background: rgba(76,175,80,0.1); border: 2px solid #4CAF50; border-radius: 8px; text-align: center; cursor: pointer; transition: all 0.2s;'
                     onmouseover='this.style.background="rgba(76,175,80,0.2)"; this.style.transform="translateY(-3px)";'
                     onmouseout='this.style.background="rgba(76,175,80,0.1)"; this.style.transform="translateY(0)";'>
                    <span style='font-size: 32px;'>🔋</span>
                    <div style='margin-top: 10px; color: #4CAF50; font-weight: bold;'>Battery</div>
                    <div style='font-size: 11px; color: #888; margin-top: 5px;'>Cell chemistry, limits</div>
                </div>
            </a>
            <a href='/inverter_settings.html' style='text-decoration: none;'>
                <div style='padding: 15px; background: rgba(33,150,243,0.1); border: 2px solid #2196F3; border-radius: 8px; text-align: center; cursor: pointer; transition: all 0.2s;'
                     onmouseover='this.style.background="rgba(33,150,243,0.2)"; this.style.transform="translateY(-3px)";'
                     onmouseout='this.style.background="rgba(33,150,243,0.1)"; this.style.transform="translateY(0)";'>
                    <span style='font-size: 32px;'>⚡</span>
                    <div style='margin-top: 10px; color: #2196F3; font-weight: bold;'>Inverter</div>
                    <div style='font-size: 11px; color: #888; margin-top: 5px;'>Power limits, AC specs</div>
                </div>
            </a>
            <a href='/charger_settings.html' style='text-decoration: none;'>
                <div style='padding: 15px; background: rgba(255,193,7,0.1); border: 2px solid #FFC107; border-radius: 8px; text-align: center; cursor: pointer; transition: all 0.2s;'
                     onmouseover='this.style.background="rgba(255,193,7,0.2)"; this.style.transform="translateY(-3px)";'
                     onmouseout='this.style.background="rgba(255,193,7,0.1)"; this.style.transform="translateY(0)";'>
                    <span style='font-size: 32px;'>🔌</span>
                    <div style='margin-top: 10px; color: #FFC107; font-weight: bold;'>Charger</div>
                    <div style='font-size: 11px; color: #888; margin-top: 5px;'>Charge rates, limits</div>
                </div>
            </a>
            <a href='/system_settings.html' style='text-decoration: none;'>
                <div style='padding: 15px; background: rgba(156,39,176,0.1); border: 2px solid #9C27B0; border-radius: 8px; text-align: center; cursor: pointer; transition: all 0.2s;'
                     onmouseover='this.style.background="rgba(156,39,176,0.2)"; this.style.transform="translateY(-3px)";'
                     onmouseout='this.style.background="rgba(156,39,176,0.1)"; this.style.transform="translateY(0)";'>
                    <span style='font-size: 32px;'>⚙️</span>
                    <div style='margin-top: 10px; color: #9C27B0; font-weight: bold;'>System</div>
                    <div style='font-size: 11px; color: #888; margin-top: 5px;'>Capabilities, safety</div>
                </div>
            </a>
            <a href='/cellmonitor' style='text-decoration: none;'>
                <div style='padding: 15px; background: rgba(0,188,212,0.1); border: 2px solid #00BCD4; border-radius: 8px; text-align: center; cursor: pointer; transition: all 0.2s;'
                     onmouseover='this.style.background="rgba(0,188,212,0.2)"; this.style.transform="translateY(-3px)";'
                     onmouseout='this.style.background="rgba(0,188,212,0.1)"; this.style.transform="translateY(0)";'>
                    <span style='font-size: 32px;'>🧪</span>
                    <div style='margin-top: 10px; color: #00BCD4; font-weight: bold;'>Cell Monitor</div>
                    <div style='font-size: 11px; color: #888; margin-top: 5px;'>Cell voltages</div>
                </div>
            </a>
        </div>
    </div>
    
    <!-- Transmitter Time & Uptime Display -->
    <div style='margin: 20px 0; padding: 15px; background: rgba(0,0,0,0.3); border-radius: 8px;'>
        <h3 style='margin: 0 0 15px 0; color: #2196F3;'>⏰ Transmitter Time & Uptime</h3>
        <div style='display: grid; grid-template-columns: 1fr 1fr; gap: 15px;'>
            <div>
                <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
                    <span style='color: #FFD700; font-weight: bold;'>Time:</span>
                    <span id='txTime' style='font-family: monospace; color: #fff; font-size: 12px;'>-- -- ----</span>
                </div>
                <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
                    <span style='color: #FFD700; font-weight: bold;'>Uptime:</span>
                    <span id='txUptime' style='font-family: monospace; color: #fff; font-size: 12px;'>-- -- ----</span>
                </div>
            </div>
            <div>
                <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
                    <span style='color: #FFD700; font-weight: bold;'>Source:</span>
                    <span id='txTimeSource' style='font-size: 12px;'>Unsynced</span>
                </div>
                <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
                    <span style='color: #999; font-size: 11px;'>Updated:</span>
                    <span id='txLastUpdate' style='color: #999; font-size: 11px;'>Waiting...</span>
                </div>
            </div>
        </div>
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
        // Time formatting functions
        function formatTimeWithTimezone(unixTime, timeZone = 'GMT') {
            if (!unixTime || unixTime === 0) return '-- -- ----';
            try {
                const date = new Date(unixTime * 1000);
                const formatter = new Intl.DateTimeFormat('en-GB', {
                    year: 'numeric',
                    month: '2-digit',
                    day: '2-digit',
                    hour: '2-digit',
                    minute: '2-digit',
                    second: '2-digit',
                    timeZone: 'UTC'
                });
                const parts = formatter.formatToParts(date);
                const values = {};
                parts.forEach(part => {
                    if (part.type !== 'literal') {
                        values[part.type] = part.value;
                    }
                });
                return `${values.day}-${values.month}-${values.year} ${values.hour}:${values.minute}:${values.second} ${timeZone}`;
            } catch (e) {
                return '-- -- ----';
            }
        }
        
        function formatUptime(ms) {
            if (!ms || ms === 0) return '-- -- ----';
            const totalSeconds = Math.floor(ms / 1000);
            const days = Math.floor(totalSeconds / 86400);
            const hours = Math.floor((totalSeconds % 86400) / 3600);
            const minutes = Math.floor((totalSeconds % 3600) / 60);
            const seconds = totalSeconds % 60;
            
            if (days > 0) {
                return `${days}d ${String(hours).padStart(2, '0')}:${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
            } else {
                return `${String(hours).padStart(2, '0')}:${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
            }
        }
        
        function getTimeSourceLabel(source) {
            switch(source) {
                case 0: return 'Unsynced';
                case 1: return 'NTP';
                case 2: return 'Manual';
                case 3: return 'GPS';
                default: return 'Unknown';
            }
        }
        
        function getTimeSourceColor(source) {
            switch(source) {
                case 0: return '#ff6b35';  // Red - unsynced
                case 1: return '#4CAF50';  // Green - NTP
                case 2: return '#FF9800';  // Orange - Manual
                case 3: return '#2196F3';  // Blue - GPS
                default: return '#999';
            }
        }

        function updateDataSourceUI(isSimulated) {
            const label = document.getElementById('dataSourceLabel');
            const toggle = document.getElementById('dataSourceToggle');
            const slider = document.getElementById('dataSourceSlider');
            const knob = document.getElementById('dataSourceKnob');

            label.textContent = isSimulated ? 'Simulated' : 'Live';
            label.style.color = isSimulated ? '#FFD700' : '#4CAF50';
            toggle.checked = isSimulated;
            slider.style.backgroundColor = isSimulated ? '#FFD700' : '#4CAF50';
            knob.style.transform = isSimulated ? 'translateX(26px)' : 'translateX(0)';
        }

        async function loadDataSource() {
            try {
                const res = await fetch('/api/get_data_source');
                const data = await res.json();
                updateDataSourceUI(data.mode === 'simulated');
            } catch (e) {
                console.debug('Failed to load data source:', e);
            }
        }

        async function setDataSource(isSimulated) {
            try {
                const res = await fetch('/api/set_data_source', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ mode: isSimulated ? 'simulated' : 'live' })
                });
                const data = await res.json();
                if (!data.success) {
                    throw new Error(data.error || 'Failed to set data source');
                }
                updateDataSourceUI(isSimulated);
            } catch (e) {
                console.error('Failed to set data source:', e);
                // Revert toggle state
                const toggle = document.getElementById('dataSourceToggle');
                updateDataSourceUI(!toggle.checked);
            }
        }
        
        let lastUpdateTime = 0;
        
        function updateTimerDisplay() {
            if (lastUpdateTime === 0) return;
            const now = Date.now();
            const elapsed = Math.floor((now - lastUpdateTime) / 1000);
            
            if (elapsed < 60) {
                document.getElementById('txLastUpdate').textContent = elapsed + 's ago';
            } else if (elapsed < 3600) {
                const mins = Math.floor(elapsed / 60);
                document.getElementById('txLastUpdate').textContent = mins + 'm ago';
            } else if (elapsed < 86400) {
                const hours = Math.floor(elapsed / 3600);
                document.getElementById('txLastUpdate').textContent = hours + 'h ago';
            } else {
                const days = Math.floor(elapsed / 86400);
                document.getElementById('txLastUpdate').textContent = days + 'd ago';
            }
            
            // Update color based on staleness
            const updateEl = document.getElementById('txLastUpdate');
            if (elapsed <= 10) {
                updateEl.style.color = '#4CAF50';  // Green - fresh
            } else if (elapsed <= 30) {
                updateEl.style.color = '#FF9800';  // Orange - stale
            } else {
                updateEl.style.color = '#ff6b35';  // Red - very stale
            }
        }
        
        // Update timer every 1 second
        setInterval(updateTimerDisplay, 1000);

        // Data source toggle setup
        document.getElementById('dataSourceToggle').addEventListener('change', function(e) {
            setDataSource(e.target.checked);
        });
        loadDataSource();
        
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
                
                // Fetch transmitter time data
                try {
                    const timeResponse = await fetch('/api/transmitter_health');
                    const timeData = await timeResponse.json();
                    
                    if (timeData && timeData.uptime_ms !== undefined) {
                        // Update time display
                        document.getElementById('txTime').textContent = formatTimeWithTimezone(timeData.unix_time, 'GMT');
                        document.getElementById('txUptime').textContent = formatUptime(timeData.uptime_ms);
                        
                        // Update time source
                        const sourceEl = document.getElementById('txTimeSource');
                        sourceEl.textContent = getTimeSourceLabel(timeData.time_source);
                        sourceEl.style.color = getTimeSourceColor(timeData.time_source);
                        
                        // Update last update time
                        lastUpdateTime = Date.now();
                        updateTimerDisplay();
                    }
                } catch (e) {
                    console.debug('Time data not yet available:', e);
                }
            } catch (e) {
                console.error('Failed to update dashboard:', e);
            }
        }, 10000);
        
        // Initial fetch on page load
        setTimeout(async function() {
            try {
                const timeResponse = await fetch('/api/transmitter_health');
                const timeData = await timeResponse.json();
                
                if (timeData && timeData.uptime_ms !== undefined) {
                    document.getElementById('txTime').textContent = formatTimeWithTimezone(timeData.unix_time, 'GMT');
                    document.getElementById('txUptime').textContent = formatUptime(timeData.uptime_ms);
                    const sourceEl = document.getElementById('txTimeSource');
                    sourceEl.textContent = getTimeSourceLabel(timeData.time_source);
                    sourceEl.style.color = getTimeSourceColor(timeData.time_source);
                    lastUpdateTime = Date.now();
                    updateTimerDisplay();
                }
            } catch (e) {
                console.debug('Initial time data fetch failed:', e);
            }
        }, 500);
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
