#include "transmitter_hub_page_content.h"

String get_transmitter_hub_page_content(
    const String& device_subtitle,
    const String& status_color,
    const String& status_text,
    const String& ip_text,
    const String& version_text,
    const String& build_date
) {
    String content = R"rawliteral(
    <div style='margin-bottom: 20px;'>
        <a href='/' style='display: inline-block; padding: 10px 16px; background: #4CAF50; color: white; text-decoration: none; border-radius: 6px; font-weight: bold;'>
            ← Dashboard
        </a>
    </div>

    <h1 style='color: #2196F3;'>📡 Transmitter Management</h1>
    <p style='color: #888; margin-top: -10px;'>)rawliteral";
    content += device_subtitle;
    content += R"rawliteral(</p>

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
                <div id='txFirmwareVersion' style='font-size: 16px; font-weight: bold; margin-top: 5px;'>)rawliteral";
    content += version_text;
    content += R"rawliteral(</div>
            </div>
            <div>
                <div style='color: #888; font-size: 13px;'>Build Date</div>
                <div id='txFirmwareBuildDate' style='font-size: 13px; margin-top: 5px; color: #888;'>)rawliteral";
    content += build_date;
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

        <!-- Hardware Config -->
        <a href='/transmitter/hardware' style='text-decoration: none;'>
            <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #2196F3;'
                 onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#42A5F5"'
                 onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#2196F3"'>
                <div style='font-size: 36px; margin: 10px 0;'>💡</div>
                <div style='font-weight: bold; color: #2196F3; font-size: 16px;'>Hardware Config</div>
                <div style='font-size: 12px; color: #888; margin-top: 8px;'>Status LED Pattern</div>
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

    )rawliteral";

    return content;
}
