#include "inverter_settings_page_content.h"

String get_inverter_settings_page_content() {
    String content = R"rawliteral(
    <h1>Inverter Settings</h1>
    <div style='margin-bottom: 20px;'>
        <a href='/' style='display: inline-block; padding: 10px 16px; background: #4CAF50; color: white; text-decoration: none; border-radius: 6px; font-weight: bold;'>
            ← Dashboard
        </a>
    </div>
    )rawliteral";

    content += R"rawliteral(

    <div class='settings-card'>
        <h3>Inverter Protocol Selection</h3>
        <p style='color: #666; font-size: 14px;'>Select the inverter protocol that matches your inverter. The transmitter will communicate using the selected protocol.</p>
        <p style='color: #ff6b35; font-size: 14px; font-weight: bold;'>&#9888;&#65039; Changing the inverter type or interface will reboot the transmitter to apply changes.</p>
        
        <div class='settings-row'>
            <label for='inverterType'>Inverter Protocol:</label>
            <select id='inverterType' onchange='updateInverterType()'>
                <option value=''>Loading...</option>
            </select>
        </div>

        <div class='settings-row'>
            <label for='inverterInterface'>Inverter Interface:</label>
            <select id='inverterInterface' onchange='updateInverterInterface()'>
                <option value=''>Loading...</option>
            </select>
        </div>
    </div>
    
    <div style='text-align: center; margin-top: 30px;'>
        <button id='saveButton' onclick='saveInverterSettings()' disabled style='padding: 12px 40px; font-size: 16px; background-color: #6c757d; color: white; border: none; border-radius: 4px; cursor: pointer;'>
            Nothing to Save
        </button>
    </div>
    )rawliteral";

    return content;
}
