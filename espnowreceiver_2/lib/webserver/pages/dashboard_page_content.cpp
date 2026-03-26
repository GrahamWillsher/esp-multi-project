#include "dashboard_page_content.h"

String get_dashboard_page_content(const String& tx_status,
                                  const String& tx_status_color,
                                  const String& tx_ip,
                                  const String& tx_ip_mode,
                                  const String& tx_version,
                                  const String& tx_device_name,
                                  const String& tx_mac,
                                  const String& rx_ip,
                                  const String& rx_ip_mode,
                                  const String& rx_version,
                                  const String& rx_device_name,
                                  const String& rx_mac) {
    String content = R"rawliteral(
    <h1>Battery Emulator System Dashboard</h1>
    
    <div style='display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin: 30px 0;'>
        
        <!-- Transmitter Device Card -->
        <a href='/transmitter' style='text-decoration: none;' title='Click to manage'>
            <div class='info-box' style='cursor: pointer; transition: transform 0.2s, box-shadow 0.2s; border-left: 5px solid #2196F3;'>
                <div onmouseover='this.parentElement.style.transform="translateY(-5px)"; this.parentElement.style.boxShadow="0 8px 20px rgba(0,0,0,0.3)";' 
                     onmouseout='this.parentElement.style.transform="translateY(0)"; this.parentElement.style.boxShadow="0 4px 6px rgba(0,0,0,0.2)";'>
                    <h2 style='margin: 0 0 15px 0; color: #2196F3;'>📡 Transmitter</h2>
                    <p id='txDeviceName' style='color: #888; font-size: 14px; margin: 5px 0;'>)rawliteral";
    content += tx_device_name;
    content += R"rawliteral(</p>
                    
                    <div style='margin: 20px 0; padding: 15px; background: rgba(0,0,0,0.3); border-radius: 8px;'>
                        <div style='display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px;'>
                            <div style='display: flex; align-items: center;'>
                                <span id='txStatusDot' style='width: 12px; height: 12px; border-radius: 50%; background: )rawliteral";
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
    content += tx_mac;
    content += R"rawliteral(</span>
                        </div>
                    </div>
                </div>
            </div>
        </a>
        
        <!-- Receiver Device Card -->
        <a href='/receiver/config' style='text-decoration: none;' title='Click to manage'>
            <div class='info-box' style='cursor: pointer; transition: transform 0.2s, box-shadow 0.2s; border-left: 5px solid #4CAF50;'>
                <div onmouseover='this.parentElement.style.transform="translateY(-5px)"; this.parentElement.style.boxShadow="0 8px 20px rgba(0,0,0,0.3)";' 
                     onmouseout='this.parentElement.style.transform="translateY(0)"; this.parentElement.style.boxShadow="0 4px 6px rgba(0,0,0,0.2)";'>
                    <h2 style='margin: 0 0 15px 0; color: #4CAF50;'>📱 Receiver</h2>
                    <p id='rxDeviceName' style='color: #888; font-size: 14px; margin: 5px 0;'>)rawliteral";
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
    content += rx_mac;
    content += R"rawliteral(</span>
                        </div>
                    </div>
                </div>
            </div>
        </a>
        
    </div>
    


    <!-- Battery Emulator Specifications -->
    <div class='info-box' style='margin: 20px 0;'>
        <h3 style='margin: 0 0 20px 0; color: #4CAF50;'>🔋 Battery Emulator Specifications</h3>
        <p style='color: #888; font-size: 14px; margin: 0 0 15px 0;'>View static configuration data received via MQTT from transmitter</p>
        <div style='display: grid; grid-template-columns: repeat(5, minmax(0, 1fr)); gap: 10px;'>
            <a href='/battery_settings.html' style='text-decoration: none;'>
                <div style='padding: 10px; background: rgba(76,175,80,0.1); border: 2px solid #4CAF50; border-radius: 8px; text-align: center; cursor: pointer; transition: all 0.2s;'
                     onmouseover='this.style.background="rgba(76,175,80,0.2)"; this.style.transform="translateY(-3px)";'
                     onmouseout='this.style.background="rgba(76,175,80,0.1)"; this.style.transform="translateY(0)";'>
                    <span style='font-size: 26px;'>🔋</span>
                    <div style='margin-top: 6px; color: #4CAF50; font-weight: bold; font-size: 14px;'>Battery</div>
                    <div style='font-size: 10px; color: #888; margin-top: 4px;'>Cell chemistry, limits</div>
                </div>
            </a>
            <a href='/inverter_settings.html' style='text-decoration: none;'>
                <div style='padding: 10px; background: rgba(33,150,243,0.1); border: 2px solid #2196F3; border-radius: 8px; text-align: center; cursor: pointer; transition: all 0.2s;'
                     onmouseover='this.style.background="rgba(33,150,243,0.2)"; this.style.transform="translateY(-3px)";'
                     onmouseout='this.style.background="rgba(33,150,243,0.1)"; this.style.transform="translateY(0)";'>
                    <span style='font-size: 26px;'>⚡</span>
                    <div style='margin-top: 6px; color: #2196F3; font-weight: bold; font-size: 14px;'>Inverter</div>
                    <div style='font-size: 10px; color: #888; margin-top: 4px;'>Power limits, AC specs</div>
                </div>
            </a>
            <a href='/charger_settings.html' style='text-decoration: none;'>
                <div style='padding: 10px; background: rgba(255,193,7,0.1); border: 2px solid #FFC107; border-radius: 8px; text-align: center; cursor: pointer; transition: all 0.2s;'
                     onmouseover='this.style.background="rgba(255,193,7,0.2)"; this.style.transform="translateY(-3px)";'
                     onmouseout='this.style.background="rgba(255,193,7,0.1)"; this.style.transform="translateY(0)";'>
                    <span style='font-size: 26px;'>🔌</span>
                    <div style='margin-top: 6px; color: #FFC107; font-weight: bold; font-size: 14px;'>Charger</div>
                    <div style='font-size: 10px; color: #888; margin-top: 4px;'>Charge rates, limits</div>
                </div>
            </a>
            <a href='/system_settings.html' style='text-decoration: none;'>
                <div style='padding: 10px; background: rgba(156,39,176,0.1); border: 2px solid #9C27B0; border-radius: 8px; text-align: center; cursor: pointer; transition: all 0.2s;'
                     onmouseover='this.style.background="rgba(156,39,176,0.2)"; this.style.transform="translateY(-3px)";'
                     onmouseout='this.style.background="rgba(156,39,176,0.1)"; this.style.transform="translateY(0)";'>
                    <span style='font-size: 26px;'>⚙️</span>
                    <div style='margin-top: 6px; color: #9C27B0; font-weight: bold; font-size: 14px;'>System</div>
                    <div style='font-size: 10px; color: #888; margin-top: 4px;'>Capabilities, safety</div>
                </div>
            </a>
            <a href='/cellmonitor' style='text-decoration: none;'>
                <div style='padding: 10px; background: rgba(0,188,212,0.1); border: 2px solid #00BCD4; border-radius: 8px; text-align: center; cursor: pointer; transition: all 0.2s;'
                     onmouseover='this.style.background="rgba(0,188,212,0.2)"; this.style.transform="translateY(-3px)";'
                     onmouseout='this.style.background="rgba(0,188,212,0.1)"; this.style.transform="translateY(0)";'>
                    <span style='font-size: 26px;'>🧪</span>
                    <div style='margin-top: 6px; color: #00BCD4; font-weight: bold; font-size: 14px;'>Cell Monitor</div>
                    <div style='font-size: 10px; color: #888; margin-top: 4px;'>Cell voltages</div>
                </div>
            </a>
        </div>
    </div>
    
    <!-- Transmitter Time & Uptime Display -->
    <div class='info-box' style='margin: 20px 0;'>
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
                    <span style='color: #FFD700; font-weight: bold;'>Updated:</span>
                    <span id='txLastUpdate' style='color: #fff; font-size: 12px;'>Waiting...</span>
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
    )rawliteral";

    return content;
}