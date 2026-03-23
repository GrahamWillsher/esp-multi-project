#include "settings_page_content.h"
#include "../common/nav_buttons.h"

String get_settings_page_content() {
    String content = R"rawliteral(
    <h1>ESP-NOW System Settings</h1>
    <h2>Transmitter Configuration</h2>
    )rawliteral";

    // Add navigation buttons from central registry
    content += "    " + generate_nav_buttons("/transmitter/config");

    content += R"rawliteral(
    
    <div class='settings-card'>
        <h3>
            Ethernet IP Configuration
            <span id='networkModeBadge' class='network-mode-badge badge-dhcp'>DHCP</span>
        </h3>
        <div class='settings-row'>
            <label>Use Static IP:</label>
            <input type='checkbox' id='staticIpEnabled' />
        </div>
        <div class='settings-row' id='localIpRow'>
            <label>IP Address:</label>
            <div class='ip-row'>
                <input class='octet' id='ip0' type='text' maxlength='3' value='192' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='ip1' type='text' maxlength='3' value='168' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='ip2' type='text' maxlength='3' value='1' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='ip3' type='text' maxlength='3' value='100' disabled />
            </div>
        </div>
        <div class='settings-row' id='gatewayRow'>
            <label>Gateway:</label>
            <div class='ip-row'>
                <input class='octet' id='gw0' type='text' maxlength='3' value='192' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='gw1' type='text' maxlength='3' value='168' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='gw2' type='text' maxlength='3' value='1' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='gw3' type='text' maxlength='3' value='1' disabled />
            </div>
        </div>
        <div class='settings-row' id='subnetRow'>
            <label>Subnet Mask:</label>
            <div class='ip-row'>
                <input class='octet' id='sub0' type='text' maxlength='3' value='255' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='sub1' type='text' maxlength='3' value='255' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='sub2' type='text' maxlength='3' value='255' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='sub3' type='text' maxlength='3' value='0' disabled />
            </div>
        </div>
        <div class='settings-row' id='dns1Row' style='display: none;'>
            <label>DNS Primary:</label>
            <div class='ip-row'>
                <input class='octet' id='dns1_0' type='text' maxlength='3' value='8' />
                <span class='dot'>.</span>
                <input class='octet' id='dns1_1' type='text' maxlength='3' value='8' />
                <span class='dot'>.</span>
                <input class='octet' id='dns1_2' type='text' maxlength='3' value='8' />
                <span class='dot'>.</span>
                <input class='octet' id='dns1_3' type='text' maxlength='3' value='8' />
            </div>
        </div>
        <div class='settings-row' id='dns2Row' style='display: none;'>
            <label>DNS Secondary:</label>
            <div class='ip-row'>
                <input class='octet' id='dns2_0' type='text' maxlength='3' value='8' />
                <span class='dot'>.</span>
                <input class='octet' id='dns2_1' type='text' maxlength='3' value='8' />
                <span class='dot'>.</span>
                <input class='octet' id='dns2_2' type='text' maxlength='3' value='4' />
                <span class='dot'>.</span>
                <input class='octet' id='dns2_3' type='text' maxlength='3' value='4' />
            </div>
        </div>
        <div class='settings-row' id='networkWarningRow' style='display: none;'>
            <p style='color: #FF9800; font-size: 12px; margin: 10px 0; white-space: nowrap;'>⚠️ Warning: IP conflict detection cannot detect powered-off devices.</p>
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>
            MQTT Configuration
            <span id='mqttStatusDot' class='status-dot' style='display: none;' title='MQTT Status'></span>
        </h3>
        <div class='settings-row'>
            <label>MQTT Enabled:</label>
            <input type='checkbox' id='mqttEnabled' onchange='updateMqttVisibility()' />
        </div>
        <div class='settings-row' id='mqttServerRow'>
            <label>MQTT Server:</label>
            <div class='ip-row'>
                <input class='octet' id='mqtt_server_0' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet' id='mqtt_server_1' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet' id='mqtt_server_2' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet' id='mqtt_server_3' type='text' maxlength='3' />
            </div>
        </div>
        <div class='settings-row' id='mqttPortRow'>
            <label>MQTT Port:</label>
            <input type='number' id='mqttPort' min='1' max='65535' value='1883' class='editable-field' />
        </div>
        <div class='settings-row' id='mqttUserRow'>
            <label>MQTT User:</label>
            <input type='text' id='mqttUsername' class='editable-field' />
        </div>
        <div class='settings-row' id='mqttPasswordRow'>
            <label>MQTT Password:</label>
            <input type='password' id='mqttPassword' class='editable-field' />
        </div>
        <div class='settings-row' id='mqttClientIdRow'>
            <label>MQTT Client ID:</label>
            <input type='text' id='mqttClientId' class='editable-field' />
        </div>
    </div>
    
    <p style='color: #999; font-size: 12px; margin-top: 10px;' id='config-version'>
        <!-- Version info will be populated via JavaScript -->
    </p>
    
    <div style='text-align: center; margin-top: 30px; margin-bottom: 30px;'>
        <button id='saveNetworkBtn' onclick='saveTransmitterConfig()' style='padding: 12px 40px; font-size: 16px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;'>
            Save Transmitter Configuration
        </button>
    </div>
)rawliteral";

    return content;
}