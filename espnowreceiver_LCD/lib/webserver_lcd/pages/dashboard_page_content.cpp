#include "dashboard_page_content.h"
#include "../common/page_generator.h"
#include <esp_http_server.h>
#include <cstring>

// HTML split at 4 truly-dynamic RX insertion points.
// TX values baked in as "---" (JS updates via /api/dashboard_data).
// All static segments live in flash; zero runtime heap allocation.

#define _SEND_LIT(req, lit) \
    do { if (send_page_content_chunk((req), "dashboard_content", (lit), sizeof(lit)-1) != ESP_OK) return ESP_FAIL; } while(0)
#define _SEND_STR(req, s) \
    do { const char* _sv = (s); if (_sv && *_sv) { if (send_page_content_chunk((req), "dashboard_dynamic", _sv, strlen(_sv)) != ESP_OK) return ESP_FAIL; } } while(0)

esp_err_t emit_dashboard_page_content(
    httpd_req_t* req,
    const char* rx_device_name,
    const char* rx_ip,
    const char* rx_version,
    const char* rx_mac
) {
    // ── Segment 1: start to rx_device_name ────────────────────────────────
    _SEND_LIT(req, R"rawliteral(
    <h1>Battery Emulator System Dashboard</h1>
    
    <div style='display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin: 30px 0;'>
        
        <!-- Transmitter Device Card -->
        <a href='/transmitter' style='text-decoration: none;' title='Click to manage'>
            <div class='info-box' style='cursor: pointer; transition: transform 0.2s, box-shadow 0.2s; border-left: 5px solid #2196F3;'>
                <div onmouseover='this.parentElement.style.transform="translateY(-5px)"; this.parentElement.style.boxShadow="0 8px 20px rgba(0,0,0,0.3)";' 
                     onmouseout='this.parentElement.style.transform="translateY(0)"; this.parentElement.style.boxShadow="0 4px 6px rgba(0,0,0,0.2)";'>
                    <h2 style='margin: 0 0 15px 0; color: #2196F3;'>&#128225; Transmitter</h2>
                    <div style='display: flex; justify-content: space-between; align-items: center; gap: 12px; margin: 5px 0;'>
                        <p id='txDeviceName' style='color: #888; font-size: 14px; margin: 0;'>---</p>
                        <span id='txTemperature' style='color: #888; font-size: 14px; white-space: nowrap;'>&#127777;&#65039; --.-&deg;C</span>
                    </div>
                    
                    <div style='margin: 20px 0; padding: 15px; background: rgba(0,0,0,0.3); border-radius: 8px;'>
                        <div style='display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px;'>
                            <div style='display: flex; align-items: center;'>
                                <span id='txStatusDot' style='width: 12px; height: 12px; border-radius: 50%; background: #888; margin-right: 10px;'></span>
                                <span style='font-weight: bold; color: #FFD700;'>Status:</span>
                            </div>
                            <span id='txStatus' style='color: #888; font-weight: bold;'>---</span>
                        </div>
                        <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
                            <span style='color: #FFD700; font-weight: bold;'>IP:</span>
                            <span>
                                <span id='txIP' style='font-family: monospace; color: #fff;'>---</span>
                                <span id='txIPMode' style='color: #888; font-size: 11px; margin-left: 5px;'></span>
                            </span>
                        </div>
                        <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
                            <span style='color: #FFD700; font-weight: bold;'>Firmware:</span>
                            <span id='txVersion' style='color: #fff;'>---</span>
                        </div>
                        <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
                            <span style='color: #FFD700; font-weight: bold;'>MAC:</span>
                            <span id='txMAC' style='font-family: monospace; font-size: 11px; color: #fff;'>---</span>
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
                    <h2 style='margin: 0 0 15px 0; color: #4CAF50;'>&#128241; Receiver</h2>
                    <div style='display: flex; justify-content: space-between; align-items: center; gap: 12px; margin: 5px 0;'>
                        <p id='rxDeviceName' style='color: #888; font-size: 14px; margin: 0;'>)rawliteral");
    // ── Dynamic 1: rx_device_name ──────────────────────────────────────────
    _SEND_STR(req, rx_device_name);
    _SEND_LIT(req, R"rawliteral(</p>
                        <span id='rxTemperature' style='color: #888; font-size: 14px; white-space: nowrap;'>&#127777;&#65039; --.-&deg;C</span>
                    </div>
                    
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
                                <span style='font-family: monospace; color: #fff;'>)rawliteral");
    // ── Dynamic 2: rx_ip ──────────────────────────────────────────────────
    _SEND_STR(req, rx_ip);
    _SEND_LIT(req, R"rawliteral(</span>
                                <span id='rxIPMode' style='color: #888; font-size: 11px; margin-left: 5px;'></span>
                            </span>
                        </div>
                        <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
                            <span style='color: #FFD700; font-weight: bold;'>Firmware:</span>
                            <span style='color: #fff;'>)rawliteral");
    // ── Dynamic 3: rx_version ─────────────────────────────────────────────
    _SEND_STR(req, rx_version);
    _SEND_LIT(req, R"rawliteral(</span>
                        </div>
                        <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
                            <span style='color: #FFD700; font-weight: bold;'>MAC:</span>
                            <span style='font-family: monospace; font-size: 11px; color: #fff;'>)rawliteral");
    // ── Dynamic 4: rx_mac ─────────────────────────────────────────────────
    _SEND_STR(req, rx_mac);
    _SEND_LIT(req, R"rawliteral(</span>
                        </div>
                    </div>
                </div>
            </div>
        </a>
        
    </div>


    <!-- Battery Emulator Specifications -->
    <div class='info-box' style='margin: 20px 0;'>
        <h3 style='margin: 0 0 20px 0; color: #4CAF50;'>&#128267; Battery Emulator Specifications</h3>
        <p style='color: #888; font-size: 14px; margin: 0 0 15px 0;'>View static configuration data received via MQTT from transmitter</p>
        <div style='display: grid; grid-template-columns: repeat(5, minmax(0, 1fr)); gap: 10px;'>
            <a href='/battery_settings.html' style='text-decoration: none;'>
                <div style='padding: 10px; background: rgba(76,175,80,0.1); border: 2px solid #4CAF50; border-radius: 8px; text-align: center; cursor: pointer; transition: all 0.2s;'
                     onmouseover='this.style.background="rgba(76,175,80,0.2)"; this.style.transform="translateY(-3px)";'
                     onmouseout='this.style.background="rgba(76,175,80,0.1)"; this.style.transform="translateY(0)";'>
                    <span style='font-size: 26px;'>&#128267;</span>
                    <div style='margin-top: 6px; color: #4CAF50; font-weight: bold; font-size: 14px;'>Battery</div>
                    <div style='font-size: 10px; color: #888; margin-top: 4px;'>Cell chemistry, limits</div>
                </div>
            </a>
            <a href='/inverter_settings.html' style='text-decoration: none;'>
                <div style='padding: 10px; background: rgba(33,150,243,0.1); border: 2px solid #2196F3; border-radius: 8px; text-align: center; cursor: pointer; transition: all 0.2s;'
                     onmouseover='this.style.background="rgba(33,150,243,0.2)"; this.style.transform="translateY(-3px)";'
                     onmouseout='this.style.background="rgba(33,150,243,0.1)"; this.style.transform="translateY(0)";'>
                    <span style='font-size: 26px;'>&#9889;</span>
                    <div style='margin-top: 6px; color: #2196F3; font-weight: bold; font-size: 14px;'>Inverter</div>
                    <div style='font-size: 10px; color: #888; margin-top: 4px;'>Power limits, AC specs</div>
                </div>
            </a>
            <a href='/charger_settings.html' style='text-decoration: none;'>
                <div style='padding: 10px; background: rgba(255,193,7,0.1); border: 2px solid #FFC107; border-radius: 8px; text-align: center; cursor: pointer; transition: all 0.2s;'
                     onmouseover='this.style.background="rgba(255,193,7,0.2)"; this.style.transform="translateY(-3px)";'
                     onmouseout='this.style.background="rgba(255,193,7,0.1)"; this.style.transform="translateY(0)";'>
                    <span style='font-size: 26px;'>&#128268;</span>
                    <div style='margin-top: 6px; color: #FFC107; font-weight: bold; font-size: 14px;'>Charger</div>
                    <div style='font-size: 10px; color: #888; margin-top: 4px;'>Charge rates, limits</div>
                </div>
            </a>
            <a href='/system_settings.html' style='text-decoration: none;'>
                <div style='padding: 10px; background: rgba(156,39,176,0.1); border: 2px solid #9C27B0; border-radius: 8px; text-align: center; cursor: pointer; transition: all 0.2s;'
                     onmouseover='this.style.background="rgba(156,39,176,0.2)"; this.style.transform="translateY(-3px)";'
                     onmouseout='this.style.background="rgba(156,39,176,0.1)"; this.style.transform="translateY(0)";'>
                    <span style='font-size: 26px;'>&#9881;&#65039;</span>
                    <div style='margin-top: 6px; color: #9C27B0; font-weight: bold; font-size: 14px;'>System</div>
                    <div style='font-size: 10px; color: #888; margin-top: 4px;'>Capabilities, safety</div>
                </div>
            </a>
            <a href='/cellmonitor' style='text-decoration: none;'>
                <div style='padding: 10px; background: rgba(0,188,212,0.1); border: 2px solid #00BCD4; border-radius: 8px; text-align: center; cursor: pointer; transition: all 0.2s;'
                     onmouseover='this.style.background="rgba(0,188,212,0.2)"; this.style.transform="translateY(-3px)";'
                     onmouseout='this.style.background="rgba(0,188,212,0.1)"; this.style.transform="translateY(0)";'>
                    <span style='font-size: 26px;'>&#129514;</span>
                    <div style='margin-top: 6px; color: #00BCD4; font-weight: bold; font-size: 14px;'>Cell Monitor</div>
                    <div style='font-size: 10px; color: #888; margin-top: 4px;'>Cell voltages</div>
                </div>
            </a>
        </div>
    </div>
    
    <!-- Transmitter Time & Uptime Display -->
    <div class='info-box' style='margin: 20px 0;'>
        <div style='display: flex; justify-content: space-between; align-items: center; gap: 12px; flex-wrap: wrap; margin: 0 0 15px 0;'>
            <h3 style='margin: 0; color: #2196F3;'>&#9200; Transmitter Time &amp; Uptime</h3>
            <span id='txGeoStatus' style='font-size: 12px; color: #888;'>Waiting...</span>
        </div>
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
        <h3 style='margin: 0 0 20px 0; color: #FF9800;'>&#128295; System Tools</h3>
        <div style='display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 15px;'>
            <a href='/debug' style='text-decoration: none;'>
                <div style='padding: 15px; background: rgba(255,152,0,0.1); border: 2px solid #FF9800; border-radius: 8px; text-align: center; cursor: pointer; transition: background 0.2s;'
                     onmouseover='this.style.background="rgba(255,152,0,0.2)"'
                     onmouseout='this.style.background="rgba(255,152,0,0.1)"'>
                    <span style='font-size: 24px;'>&#128027;</span>
                    <div style='margin-top: 10px; color: #FF9800; font-weight: bold;'>Debug Logging</div>
                    <div style='font-size: 12px; color: #888; margin-top: 5px;'>Control logging levels</div>
                </div>
            </a>
            <a href='/ota' style='text-decoration: none;'>
                <div style='padding: 15px; background: rgba(255,152,0,0.1); border: 2px solid #FF9800; border-radius: 8px; text-align: center; cursor: pointer; transition: background 0.2s;'
                     onmouseover='this.style.background="rgba(255,152,0,0.2)"'
                     onmouseout='this.style.background="rgba(255,152,0,0.1)"'>
                    <span style='font-size: 24px;'>&#128228;</span>
                    <div style='margin-top: 10px; color: #FF9800; font-weight: bold;'>OTA Update</div>
                    <div style='font-size: 12px; color: #888; margin-top: 5px;'>Update firmware</div>
                </div>
            </a>
            <a id='eventLogLink' href='/events' onclick='window.location="/events"; return false;' style='text-decoration: none; display: block; cursor: pointer;'>
                <div id='eventLogCard' style='padding: 15px; background: rgba(255,152,0,0.1); border: 2px solid #FF9800; border-radius: 8px; text-align: center; cursor: pointer; transition: all 0.2s;'
                     onmouseover='this.style.background="rgba(255,152,0,0.2)"'
                     onmouseout='this.style.background="rgba(255,152,0,0.1)"'>
                    <span style='font-size: 24px;'>&#128203;</span>
                    <div style='margin-top: 10px; color: #FF9800; font-weight: bold;'>Event Logs</div>
                    <div id='eventLogStatus' style='font-size: 12px; color: #888; margin-top: 5px;'>View system events</div>
                </div>
            </a>
        </div>
    </div>
)rawliteral");

    return ESP_OK;
}

#undef _SEND_LIT
#undef _SEND_STR
