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
    String tx_device_name = "Unknown Device";  // Default matches receiver fallback
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
        <div style='display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 15px;'>
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
            <a id='eventLogLink' href='/events' style='text-decoration: none;'>
                <div id='eventLogCard' style='padding: 15px; background: rgba(255,152,0,0.1); border: 2px solid #FF9800; border-radius: 8px; text-align: center; cursor: pointer; transition: all 0.2s;'
                     onmouseover='if(!this.classList.contains("disabled")) this.style.background="rgba(255,152,0,0.2)"'
                     onmouseout='if(!this.classList.contains("disabled")) this.style.background="rgba(255,152,0,0.1)"'>
                    <span style='font-size: 24px;'>📋</span>
                    <div style='margin-top: 10px; color: #FF9800; font-weight: bold;'>Event Logs</div>
                    <div id='eventLogStatus' style='font-size: 12px; color: #888; margin-top: 5px;'>View system events</div>
                </div>
            </a>
        </div>
    </div>
    
    <script>
        // Track last update time for "X seconds ago" display
        let lastUpdateTime = Date.now();
        let lastSeenUptimeMs = 0;  // Track previous uptime value to detect actual updates
        
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
        
        function formatLastUpdate(ms) {
            if (!Number.isFinite(ms) || ms < 0) {
                return 'Now';
            }
            const totalSeconds = Math.floor(ms / 1000);
            const days = Math.floor(totalSeconds / 86400);
            const hours = Math.floor((totalSeconds % 86400) / 3600);
            const minutes = Math.floor((totalSeconds % 3600) / 60);
            const seconds = totalSeconds % 60;
            
            if (days > 0) {
                return `${days}d, ${String(hours).padStart(2, '0')}H:${String(minutes).padStart(2, '0')}M:${String(seconds).padStart(2, '0')}S ago`;
            } else if (hours > 0) {
                return `${String(hours).padStart(2, '0')}H:${String(minutes).padStart(2, '0')}M:${String(seconds).padStart(2, '0')}S ago`;
            } else if (minutes > 0) {
                return `${minutes}M:${String(seconds).padStart(2, '0')}S ago`;
            } else {
                return `${seconds}s ago`;
            }
        }
        
        function updateTimerDisplay() {
            const msSinceUpdate = Date.now() - lastUpdateTime;
            const secondsSinceUpdate = Math.floor(msSinceUpdate / 1000);
            const lastUpdateStr = formatLastUpdate(msSinceUpdate);
            
            const lastUpdateEl = document.getElementById('txLastUpdate');
            lastUpdateEl.textContent = lastUpdateStr;
            
            // Change color based on staleness
            if (secondsSinceUpdate < 2) {
                lastUpdateEl.style.color = '#4CAF50';  // Green - fresh
            } else if (secondsSinceUpdate < 5) {
                lastUpdateEl.style.color = '#FFD700';  // Yellow - slightly stale
            } else if (secondsSinceUpdate < 10) {
                lastUpdateEl.style.color = '#FF9800';  // Orange - getting stale
            } else {
                lastUpdateEl.style.color = '#ff6b35';  // Red - very stale
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

        
        // Update transmitter data every 2 seconds (match transmission rate)
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
                        
                        // Only update "last update" time if uptime_ms has actually changed (new data from transmitter)
                        if (timeData.uptime_ms !== lastSeenUptimeMs) {
                            lastSeenUptimeMs = timeData.uptime_ms;
                            lastUpdateTime = Date.now();
                            updateTimerDisplay();
                        }
                    }
                } catch (e) {
                    console.debug('Time data not yet available:', e);
                }
            } catch (e) {
                console.error('Failed to update dashboard:', e);
            }
        }, 2000);
        
        // Load event logs from transmitter
        async function loadEventLogs() {
            const statusEl = document.getElementById('eventLogStatus');
            const cardEl = document.getElementById('eventLogCard');
            const linkEl = document.getElementById('eventLogLink');
            statusEl.textContent = 'Loading...';
            statusEl.style.color = '#FFD700';
            
            try {
                const response = await fetch('/api/get_event_logs?limit=100');
                const data = await response.json();
                
                if (data.success && data.event_count !== undefined && data.event_count > 0) {
                    // Count event types if events array exists
                    let errorCount = 0;
                    let warningCount = 0;
                    let infoCount = 0;
                    
                    if (data.events && Array.isArray(data.events)) {
                        data.events.forEach(event => {
                            if (event.level === 3) {  // ERROR
                                errorCount++;
                            } else if (event.level === 4) {  // WARNING
                                warningCount++;
                            } else if (event.level === 6) {  // INFO
                                infoCount++;
                            }
                        });
                    }
                    
                    // Update status display - enable card
                    let statusText = data.event_count + ' events';
                    if (errorCount > 0) {
                        statusText += ` | ${errorCount} errors`;
                    }
                    if (warningCount > 0) {
                        statusText += ` | ${warningCount} warnings`;
                    }
                    
                    statusEl.textContent = statusText;
                    statusEl.style.color = '#4CAF50';
                    cardEl.classList.remove('disabled');
                    linkEl.style.pointerEvents = 'auto';
                    cardEl.style.opacity = '1';
                    
                    // Log event summary
                    console.log('Event Summary:', {
                        total: data.event_count,
                        errors: errorCount,
                        warnings: warningCount,
                        info: infoCount
                    });
                } else {
                    // No data available - disable card and show appropriate message
                    cardEl.classList.add('disabled');
                    linkEl.style.pointerEvents = 'none';
                    cardEl.style.opacity = '0.5';
                    cardEl.style.cursor = 'not-allowed';
                    
                    if (data.success && data.event_count === 0) {
                        statusEl.textContent = 'No events to display';
                        statusEl.style.color = '#888';
                    } else if (data.success === false && data.error && data.error.includes('not connected')) {
                        statusEl.textContent = 'Transmitter offline';
                        statusEl.style.color = '#FFD700';
                    } else {
                        statusEl.textContent = 'Not available';
                        statusEl.style.color = '#888';
                    }
                }
            } catch (e) {
                // Connection error - disable card
                cardEl.classList.add('disabled');
                linkEl.style.pointerEvents = 'none';
                cardEl.style.opacity = '0.5';
                cardEl.style.cursor = 'not-allowed';
                statusEl.textContent = 'Connection error';
                statusEl.style.color = '#ff6b35';
                console.error('Event logs fetch failed:', e);
            }
        }
        
        // Load event logs on page load
        window.addEventListener('load', function() {
            loadEventLogs();
        });
        

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
                    lastSeenUptimeMs = timeData.uptime_ms;
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
