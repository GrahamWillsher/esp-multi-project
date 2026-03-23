#include "network_config_page_content.h"
#include "../common/nav_buttons.h"

String get_network_config_page_content(bool isAPMode) {
    String content;

    if (isAPMode) {
        // Minimal AP mode setup page - NO navigation, NO extras
        content = R"rawliteral(
    <div style='max-width: 600px; margin: 50px auto; padding: 20px;'>
        <h1 style='text-align: center; color: #4CAF50; margin-bottom: 10px;'>📶 ESP32 Receiver Setup</h1>
        <p style='text-align: center; color: #888; margin-bottom: 30px;'>Configure WiFi and Network Settings</p>
        
        <div class='alert alert-info' style='margin-bottom: 20px;'>
            <strong>ℹ️ Setup Mode</strong><br>
            Enter your WiFi credentials below to connect the receiver to your network.
            The device will reboot after saving.
        </div>
)rawliteral";
    } else {
        // Normal mode with full navigation
        content = R"rawliteral(
    <h1>Receiver Network Configuration</h1>
    <h2>WiFi & IP Settings</h2>
    )rawliteral";

        // Add navigation buttons from central registry
        content += "    " + generate_nav_buttons("/receiver/network");

        content += "\n    ";
    }

    content += R"rawliteral(
    
    <!-- WiFi Credentials Section -->
    <div class='info-box'>
        <h3>WiFi Credentials</h3>
        <div style='display: grid; grid-template-columns: 1fr 1.5fr; gap: 10px; align-items: center;'>
            
            <label for='wifiMac'>WiFi MAC Address:</label>
            <input type='text' id='wifiMac' readonly class='form-control editable-mac' 
                   style='background-color: #f0f0f0; font-family: monospace;'>
            
            <label for='hostname'>Hostname:</label>
            <input type='text' id='hostname' name='hostname' maxlength='31' 
                   placeholder='esp32-receiver' class='form-control editable-field'>
            
            <label for='ssid'>WiFi SSID: <span class='required'>*</span></label>
            <input type='text' id='ssid' name='ssid' maxlength='31' required 
                   class='form-control editable-field'>
            
            <label for='password'>WiFi Password:</label>
            <input type='password' id='password' name='password' maxlength='63' 
                   class='form-control editable-field' placeholder='Leave empty to keep current'>
            
            <label for='wifiChannel'>WiFi Channel:</label>
            <input type='text' id='wifiChannel' name='wifiChannel' readonly 
                   class='form-control' style='background-color: #f0f0f0;'>
            
        </div>
    </div>
    
    <!-- Device Details Section -->
    <div class='info-box'>
        <h3>Device Details</h3>
        <div style='display: grid; grid-template-columns: 1fr 1.5fr; gap: 10px; align-items: center;'>
            
            <label>Device:</label>
            <input type='text' id='deviceName' readonly class='form-control' 
                     style='background-color: #f0f0f0;' value='Loading...'>
            
            <label>Chip Model:</label>
            <input type='text' id='chipModel' readonly class='form-control' 
                   style='background-color: #f0f0f0;'>
            
            <label>Chip Revision:</label>
            <input type='text' id='chipRevision' readonly class='form-control' 
                   style='background-color: #f0f0f0;'>
            
        </div>
    </div>
    
    <!-- IP Configuration Section -->
    <div class='info-box'>
        <h3>
            IP Configuration
            <span id='networkModeBadge' class='network-mode-badge badge-dhcp'>DHCP</span>
        </h3>
        <div class='checkbox-row'>
            <input type='checkbox' id='useStaticIP' name='useStaticIP'>
            <label for='useStaticIP'>Use Static IP</label>
        </div>
        <div class='settings-row' id='localIpRow'>
            <label>IP Address: <span class='required'>*</span></label>
            <div class='ip-row'>
                <input class='octet' type='text' id='ip0' maxlength='3' required>
                <span class='dot'>.</span>
                <input class='octet' type='text' id='ip1' maxlength='3' required>
                <span class='dot'>.</span>
                <input class='octet' type='text' id='ip2' maxlength='3' required>
                <span class='dot'>.</span>
                <input class='octet' type='text' id='ip3' maxlength='3' required>
            </div>
        </div>
        <div class='settings-row' id='gatewayRow'>
            <label>Gateway: <span class='required'>*</span></label>
            <div class='ip-row'>
                <input class='octet' type='text' id='gw0' maxlength='3' required>
                <span class='dot'>.</span>
                <input class='octet' type='text' id='gw1' maxlength='3' required>
                <span class='dot'>.</span>
                <input class='octet' type='text' id='gw2' maxlength='3' required>
                <span class='dot'>.</span>
                <input class='octet' type='text' id='gw3' maxlength='3' required>
            </div>
        </div>
        <div class='settings-row' id='subnetRow'>
            <label>Subnet Mask: <span class='required'>*</span></label>
            <div class='ip-row'>
                <input class='octet' type='text' id='sub0' maxlength='3' required>
                <span class='dot'>.</span>
                <input class='octet' type='text' id='sub1' maxlength='3' required>
                <span class='dot'>.</span>
                <input class='octet' type='text' id='sub2' maxlength='3' required>
                <span class='dot'>.</span>
                <input class='octet' type='text' id='sub3' maxlength='3' required>
            </div>
        </div>
        <div class='settings-row' id='dns1Row' style='display: none;'>
            <label>Primary DNS:</label>
            <div class='ip-row'>
                <input class='octet' type='text' id='dns1_0' maxlength='3'>
                <span class='dot'>.</span>
                <input class='octet' type='text' id='dns1_1' maxlength='3'>
                <span class='dot'>.</span>
                <input class='octet' type='text' id='dns1_2' maxlength='3'>
                <span class='dot'>.</span>
                <input class='octet' type='text' id='dns1_3' maxlength='3'>
            </div>
        </div>
        <div class='settings-row' id='dns2Row' style='display: none;'>
            <label>Secondary DNS:</label>
            <div class='ip-row'>
                <input class='octet' type='text' id='dns2_0' maxlength='3'>
                <span class='dot'>.</span>
                <input class='octet' type='text' id='dns2_1' maxlength='3'>
                <span class='dot'>.</span>
                <input class='octet' type='text' id='dns2_2' maxlength='3'>
                <span class='dot'>.</span>
                <input class='octet' type='text' id='dns2_3' maxlength='3'>
            </div>
        </div>
    </div>
    
    <!-- Action Buttons -->
    <div style='text-align: center; margin-top: 30px;'>
        <button id='saveNetworkBtn' onclick='saveNetworkConfig()' 
                style='padding: 15px 50px; font-size: 18px; font-weight: bold; 
                       background-color: #4CAF50; color: white; border: none; 
                       border-radius: 8px; cursor: pointer; box-shadow: 0 4px 6px rgba(0,0,0,0.2);
                       transition: all 0.3s;'
                onmouseover='this.style.backgroundColor="#45a049"; this.style.transform="translateY(-2px)";'
                onmouseout='this.style.backgroundColor="#4CAF50"; this.style.transform="translateY(0)";'>
            Save Network Configuration
        </button>
    </div>
    
    <div style='text-align: center; margin-top: 15px;'>
        <button type='button' onclick='loadConfig()' 
                style='padding: 10px 30px; font-size: 14px; background-color: #6c757d; 
                       color: white; border: none; border-radius: 6px; cursor: pointer;'>
            Reload Settings
        </button>
    </div>
    
    <!-- Reboot Countdown (shown after save) -->
    <div id='rebootNotice' class='alert alert-info' style='display: none;'>
        <strong>Configuration Saved!</strong><br>
        Device will reboot in <span id='countdown'>3</span> seconds...
    </div>
)rawliteral";

    return content;
}