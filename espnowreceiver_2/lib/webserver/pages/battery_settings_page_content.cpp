#include "battery_settings_page_content.h"

String get_battery_settings_page_content() {
    String content = R"rawliteral(
    <h1>Battery Settings</h1>
    <div style='margin-bottom: 20px;'>
        <a href='/' style='display: inline-block; padding: 10px 16px; background: #4CAF50; color: white; text-decoration: none; border-radius: 6px; font-weight: bold;'>
            ← Dashboard
        </a>
    </div>
    )rawliteral";

    content += R"rawliteral(
    
    <div class='settings-card'>
        <h3>Battery Capacity & Limits</h3>
        
        <div class='settings-row'>
            <label for='batteryCapacity'>Battery Capacity (Wh):</label>
            <input type='number' id='batteryCapacity' min='1000' max='1000000' />
        </div>
        
        <div class='settings-row'>
            <label for='maxVoltage'>Max Voltage (mV):</label>
            <input type='number' id='maxVoltage' min='30000' max='100000' />
        </div>
        
        <div class='settings-row'>
            <label for='minVoltage'>Min Voltage (mV):</label>
            <input type='number' id='minVoltage' min='20000' max='80000' />
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>Current Limits</h3>
        
        <div class='settings-row'>
            <label for='maxChargeCurrent'>Max Charge Current (A):</label>
            <input type='number' id='maxChargeCurrent' min='0' max='500' step='1' />
        </div>
        
        <div class='settings-row'>
            <label for='maxDischargeCurrent'>Max Discharge Current (A):</label>
            <input type='number' id='maxDischargeCurrent' min='0' max='500' step='1' />
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>State of Charge (SOC) Limits</h3>
        
        <div class='settings-row'>
            <label for='socHighLimit'>SOC High Limit (%):</label>
            <input type='number' id='socHighLimit' min='50' max='100' step='1' />
        </div>
        
        <div class='settings-row'>
            <label for='socLowLimit'>SOC Low Limit (%):</label>
            <input type='number' id='socLowLimit' min='0' max='50' step='1' />
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>Cell Configuration</h3>
        
        <div class='settings-row'>
            <label for='cellCount'>Cell Count (detected from battery):</label>
            <input type='number' id='cellCount' readonly style='background-color: #f5f5f5; cursor: not-allowed;' />
        </div>
        
        <div class='settings-row'>
            <label for='chemistry'>Battery Chemistry:</label>
            <select id='chemistry'>
                <option value='0'>NCA (Nickel Cobalt Aluminum)</option>
                <option value='1'>NMC (Nickel Manganese Cobalt)</option>
                <option value='2'>LFP (Lithium Iron Phosphate)</option>
                <option value='3'>LTO (Lithium Titanate)</option>
            </select>
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>Battery Type Selection</h3>
        <p style='color: #666; font-size: 14px;'>Select the battery profile to use. The transmitter will switch to the selected profile.</p>
        <p style='color: #ff6b35; font-size: 14px; font-weight: bold;'>⚠️ Changing the battery type or interface will reboot the transmitter to apply changes.</p>
        
        <div class='settings-row'>
            <label for='batteryType'>Battery Type:</label>
            <select id='batteryType' onchange='updateBatteryType()'>
                <option value=''>Loading...</option>
            </select>
        </div>

        <div class='settings-row'>
            <label for='batteryInterface'>Battery Interface:</label>
            <select id='batteryInterface' onchange='updateBatteryInterface()'>
                <option value=''>Loading...</option>
            </select>
        </div>
    </div>
    
    <div style='text-align: center; margin-top: 30px;'>
        <button id='saveButton' onclick='saveAllSettings()' disabled style='padding: 12px 40px; font-size: 16px; background-color: #6c757d; color: white; border: none; border-radius: 4px; cursor: not-allowed;'>
            Nothing to Save
        </button>
    </div>
)rawliteral";

    return content;
}