#include "systeminfo_page_content.h"

const char* get_systeminfo_page_content() {
    return R"rawliteral(
    <h1>📱 Receiver <span style='font-size: 0.5em; font-weight: 600;'>(<span id='receiverDeviceName'>Loading...</span>)</span></h1>

    <div class='settings-card'>
        <h3>WiFi Settings</h3>
        <div class='settings-row'>
            <label>Hostname:</label>
            <input type='text' id='hostname' value='Loading...' class='editable-field' />
        </div>
        <div class='settings-row'>
            <label>WiFi SSID:</label>
            <input type='text' id='ssid' value='Loading...' class='editable-field' />
        </div>
        <div class='settings-row'>
            <label>WiFi Password:</label>
            <input type='password' id='password' value='' class='editable-field' placeholder='Leave empty to keep current' />
        </div>
        <div class='settings-row'>
            <label>WiFi Channel:</label>
            <input type='text' id='wifiChannel' value='Loading...' disabled class='readonly-field' />
        </div>
    </div>

    <div class='settings-card'>
        <h3>
            IP Configuration
            <span id='networkModeBadge' class='network-mode-badge badge-dhcp'>DHCP</span>
        </h3>
        <div class='settings-row'>
            <label>Use Static IP:</label>
            <input type='checkbox' id='useStaticIP' />
        </div>
        <div class='settings-row' id='localIpRow'>
            <label>IP Address:</label>
            <div class='ip-row'>
                <input class='octet' id='ip0' type='text' maxlength='3' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='ip1' type='text' maxlength='3' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='ip2' type='text' maxlength='3' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='ip3' type='text' maxlength='3' disabled />
            </div>
        </div>
        <div class='settings-row' id='gatewayRow'>
            <label>Gateway:</label>
            <div class='ip-row'>
                <input class='octet' id='gw0' type='text' maxlength='3' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='gw1' type='text' maxlength='3' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='gw2' type='text' maxlength='3' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='gw3' type='text' maxlength='3' disabled />
            </div>
        </div>
        <div class='settings-row' id='subnetRow'>
            <label>Subnet Mask:</label>
            <div class='ip-row'>
                <input class='octet' id='sub0' type='text' maxlength='3' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='sub1' type='text' maxlength='3' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='sub2' type='text' maxlength='3' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='sub3' type='text' maxlength='3' disabled />
            </div>
        </div>
        <div class='settings-row' id='dns1Row'>
            <label>Primary DNS:</label>
            <div class='ip-row'>
                <input class='octet' id='dns1_0' type='text' maxlength='3' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='dns1_1' type='text' maxlength='3' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='dns1_2' type='text' maxlength='3' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='dns1_3' type='text' maxlength='3' disabled />
            </div>
        </div>
        <div class='settings-row' id='dns2Row'>
            <label>Secondary DNS:</label>
            <div class='ip-row'>
                <input class='octet' id='dns2_0' type='text' maxlength='3' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='dns2_1' type='text' maxlength='3' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='dns2_2' type='text' maxlength='3' disabled />
                <span class='dot'>.</span>
                <input class='octet' id='dns2_3' type='text' maxlength='3' disabled />
            </div>
        </div>
    </div>

    <div class='settings-card'>
        <h3>Display Configuration</h3>
        <div class='settings-row'>
            <label>Power Bar Renderer:</label>
            <select id='powerBarRendererMode' class='editable-field'>
                <option value='0'>Original</option>
                <option value='4'>Original (Rounded Ends)</option>
                <option value='1'>Soft</option>
                <option value='2'>Linear</option>
                <option value='3'>Hybrid</option>
            </select>
        </div>
    </div>

    <div class='settings-card'>
        <h3>MQTT Settings <span id='mqttStatusDot' class='status-dot' style='display: inline-block;' title='MQTT Status'></span></h3>
        <div class='settings-row'>
            <label>Enabled:</label>
            <input type='checkbox' id='mqttEnabled' />
        </div>
        <div class='settings-row'>
            <label>Server:</label>
            <div class='ip-row'>
                <input class='octet' id='mqtt0' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet' id='mqtt1' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet' id='mqtt2' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet' id='mqtt3' type='text' maxlength='3' />
            </div>
        </div>
        <div class='settings-row'>
            <label>Port:</label>
            <input type='text' id='mqttPort' value='1883' class='editable-field' />
        </div>
        <div class='settings-row'>
            <label>Username:</label>
            <input type='text' id='mqttUsername' value='' class='editable-field' placeholder='(optional)' />
        </div>
        <div class='settings-row'>
            <label>Password:</label>
            <input type='password' id='mqttPassword' value='' class='editable-field' placeholder='(optional)' />
        </div>
    </div>

    <div style='text-align: center; margin-top: 30px; margin-bottom: 30px;'>
        <button id='saveNetworkBtn' onclick='saveReceiverConfig()' style='padding: 12px 40px; font-size: 16px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;'>
            Save Receiver Configuration
        </button>
    </div>
)rawliteral";
}
