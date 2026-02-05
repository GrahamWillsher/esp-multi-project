#include "settings_page.h"
#include "../common/page_generator.h"
#include "../common/nav_buttons.h"
#include "../utils/transmitter_manager.h"
#include "../processors/settings_processor.h"
#include "../../../src/config/config_receiver.h"
#include <Arduino.h>

/**
 * @brief Handler for the root "/" settings page
 * 
 * This is the most complex page - displays all configuration settings from the transmitter.
 * Features template processing, dynamic visibility controls, and ESP-NOW IP data fetching.
 */
static esp_err_t root_handler(httpd_req_t *req) {
    String content = R"rawliteral(
    <h1>ESP-NOW System Settings</h1>
    <h2>Transmitter Configuration</h2>
    <div class='note'>
        📡 These settings are from the transmitter device (ESP32-POE-ISO)
    </div>
    )rawliteral";
    
    // Add navigation buttons from central registry
    content += "    " + generate_nav_buttons("/");
    
    // Show connection status with MAC address if connected
    content += "\n    <div class='note'>\n        📡 ";
    if (TransmitterManager::isMACKnown()) {
        content += "Connected to device: " + TransmitterManager::getMACString();
    } else {
        content += "Waiting for connection from remote device...";
    }
    content += "\n    </div>\n";
    
    content += R"rawliteral(
    
    <div class='settings-card'>
        <h3>Ethernet Static IP Configuration</h3>
        <div class='settings-row'>
            <label>Static IP Enabled:</label>
            <input type='checkbox' id='staticIpEnabled' disabled />
        </div>
        <div class='settings-row' id='localIpRow'>
            <label>Local IP:</label>
            <div class='ip-row'>
                <input class='octet' type='text' value='192' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='168' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='1' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='100' disabled />
            </div>
        </div>
        <div class='settings-row' id='gatewayRow'>
            <label>Gateway:</label>
            <div class='ip-row'>
                <input class='octet' type='text' value='192' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='168' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='1' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='1' disabled />
            </div>
        </div>
        <div class='settings-row' id='subnetRow'>
            <label>Subnet:</label>
            <div class='ip-row'>
                <input class='octet' type='text' value='255' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='255' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='255' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='0' disabled />
            </div>
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>MQTT Configuration</h3>
        <div class='settings-row'>
            <label>MQTT Enabled:</label>
            <input type='checkbox' id='mqttEnabled' checked disabled />
        </div>
        <div class='settings-row' id='mqttServerRow'>
            <label>MQTT Server:</label>
            <input type='text' value='192.168.1.221' disabled style='background-color: white; color: #333;' />
        </div>
        <div class='settings-row' id='mqttPortRow'>
            <label>MQTT Port:</label>
            <input type='text' value='1883' disabled style='background-color: white; color: #333;' />
        </div>
        <div class='settings-row' id='mqttUserRow'>
            <label>MQTT User:</label>
            <input type='text' value='Aintree34' disabled style='background-color: white; color: #333;' />
        </div>
        <div class='settings-row' id='mqttPasswordRow'>
            <label>MQTT Password:</label>
            <input type='password' value='Shanghai17' disabled style='background-color: white; color: #333;' />
        </div>
        <div class='settings-row' id='mqttClientIdRow'>
            <label>MQTT Client ID:</label>
            <input type='text' value='espnow_transmitter' disabled style='background-color: white; color: #333;' />
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>Battery Configuration</h3>
        <div class='settings-row'>
            <label>Battery Type:</label>
            <input type='text' value='ESP-NOW Remote' disabled />
        </div>
        <div class='settings-row'>
            <label>Double Battery:</label>
            <input type='checkbox' %DBLBTR% disabled />
        </div>
        <div class='settings-row'>
            <label>Battery Max Voltage:</label>
            <input type='text' value='%BATTPVMAX% V' disabled />
        </div>
        <div class='settings-row'>
            <label>Battery Min Voltage:</label>
            <input type='text' value='%BATTPVMIN% V' disabled />
        </div>
        <div class='settings-row'>
            <label>Cell Max Voltage:</label>
            <input type='text' value='%BATTCVMAX% mV' disabled />
        </div>
        <div class='settings-row'>
            <label>Cell Min Voltage:</label>
            <input type='text' value='%BATTCVMIN% mV' disabled />
        </div>
        <div class='settings-row'>
            <label>Use Estimated SOC:</label>
            <input type='checkbox' %SOCESTIMATED% disabled />
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>Power Settings</h3>
        <div class='settings-row'>
            <label>Charge Power:</label>
            <input type='text' value='%CHGPOWER% W' disabled />
        </div>
        <div class='settings-row'>
            <label>Discharge Power:</label>
            <input type='text' value='%DCHGPOWER% W' disabled />
        </div>
        <div class='settings-row'>
            <label>Max Pre-charge Time:</label>
            <input type='text' value='%MAXPRETIME% ms' disabled />
        </div>
        <div class='settings-row'>
            <label>Pre-charge Duration:</label>
            <input type='text' value='%PRECHGMS% ms' disabled />
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>Inverter Configuration</h3>
        <div class='settings-row'>
            <label>Inverter Cells:</label>
            <input type='text' value='%INVCELLS%' disabled />
        </div>
        <div class='settings-row'>
            <label>Inverter Modules:</label>
            <input type='text' value='%INVMODULES%' disabled />
        </div>
        <div class='settings-row'>
            <label>Cells Per Module:</label>
            <input type='text' value='%INVCELLSPER%' disabled />
        </div>
        <div class='settings-row'>
            <label>Voltage Level:</label>
            <input type='text' value='%INVVLEVEL% V' disabled />
        </div>
        <div class='settings-row'>
            <label>Capacity:</label>
            <input type='text' value='%INVCAPACITY% Ah' disabled />
        </div>
        <div class='settings-row'>
            <label>Battery Type:</label>
            <input type='text' value='%INVBTYPE%' disabled />
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>CAN Configuration</h3>
        <div class='settings-row'>
            <label>CAN Frequency:</label>
            <input type='text' value='%CANFREQ% kHz' disabled />
        </div>
        <div class='settings-row'>
            <label>CAN FD Frequency:</label>
            <input type='text' value='%CANFDFREQ% MHz' disabled />
        </div>
        <div class='settings-row'>
            <label>Sofar Inverter ID:</label>
            <input type='text' value='%SOFAR_ID%' disabled />
        </div>
        <div class='settings-row'>
            <label>Pylon Send Interval:</label>
            <input type='text' value='%PYLONSEND% ms' disabled />
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>Contactor Control</h3>
        <div class='settings-row'>
            <label>Contactor Control:</label>
            <input type='checkbox' %CNTCTRL% disabled />
        </div>
        <div class='settings-row'>
            <label>NC Contactor:</label>
            <input type='checkbox' %NCCONTACTOR% disabled />
        </div>
        <div class='settings-row'>
            <label>PWM Frequency:</label>
            <input type='text' value='%PWMFREQ% Hz' disabled />
        </div>
    </div>
    
    <p style='color: #666; font-size: 14px; margin-top: 30px;' id='config-status'>
        ⚙️ Transmitter settings are read-only. Changes must be made on the transmitter device code.
    </p>
    
    <p style='color: #999; font-size: 12px; margin-top: 10px;' id='config-version'>
        <!-- Version info will be populated via JavaScript -->
    </p>
)rawliteral";

    // Check if config is available and add status to page
    auto& configMgr = ReceiverConfigManager::instance();
    if (!configMgr.isConfigAvailable()) {
        // Insert warning at the beginning of content
        String warning = R"rawliteral(
    <div class='note' style='background-color: #fff3cd; border-color: #ffc107; color: #856404;'>
        ⚠️ Transmitter configuration not yet received. Displayed values are defaults. 
        Please wait for synchronization...
    </div>
)rawliteral";
        int insert_pos = content.indexOf("<div class='settings-card'>");
        if (insert_pos != -1) {
            content = content.substring(0, insert_pos) + warning + "\n    " + content.substring(insert_pos);
        }
    }

    // Process placeholders
    int start_pos = 0;
    while ((start_pos = content.indexOf("%", start_pos)) != -1) {
        int end_pos = content.indexOf("%", start_pos + 1);
        if (end_pos == -1) break;
        
        String placeholder = content.substring(start_pos + 1, end_pos);
        String value = settings_processor(placeholder);
        
        content = content.substring(0, start_pos) + value + content.substring(end_pos + 1);
        start_pos += value.length();
    }

    // Add JavaScript to handle conditional visibility and IP data loading
    String script = R"rawliteral(
        window.onload = function() {
            console.log('Settings page loaded');
            
            // Transmitter now sends IP data automatically when Ethernet connects
            // No need to request it - just fetch what's already been received
            // Wait a moment for page to fully load, then fetch IP data
            setTimeout(fetchTransmitterIP, 100);
            
            // Function to fetch and populate transmitter IP data
            function fetchTransmitterIP() {
                fetch('/api/transmitter_ip')
                    .then(response => response.json())
                    .then(data => {
                        console.log('Received IP data:', data);
                        if (data.success) {
                            // Parse and populate IP fields
                            const ipParts = data.ip.split('.');
                            const gatewayParts = data.gateway.split('.');
                            const subnetParts = data.subnet.split('.');
                            
                            // Find all IP input fields and populate them
                            const localIpInputs = document.querySelectorAll('#localIpRow .octet');
                            const gatewayInputs = document.querySelectorAll('#gatewayRow .octet');
                            const subnetInputs = document.querySelectorAll('#subnetRow .octet');
                            
                            if (localIpInputs.length === 4) {
                                for (let i = 0; i < 4; i++) {
                                    localIpInputs[i].value = ipParts[i] || '0';
                                }
                            }
                            
                            if (gatewayInputs.length === 4) {
                                for (let i = 0; i < 4; i++) {
                                    gatewayInputs[i].value = gatewayParts[i] || '0';
                                }
                            }
                            
                            if (subnetInputs.length === 4) {
                                for (let i = 0; i < 4; i++) {
                                    subnetInputs[i].value = subnetParts[i] || '0';
                                }
                            }
                            
                            console.log('IP fields populated successfully');
                        } else {
                            console.log('IP data not yet received, will retry');
                            // Retry after 1 second
                            setTimeout(fetchTransmitterIP, 1000);
                        }
                    })
                    .catch(err => {
                        console.error('Failed to fetch IP data:', err);
                    });
            }
            
            // Function to update AP Name visibility based on WiFi AP Enabled checkbox
            function updateApNameVisibility() {
                const apEnabledCheckbox = document.getElementById('wifiApEnabled');
                const apNameRow = document.getElementById('apNameRow');
                
                console.log('updateApNameVisibility called');
                console.log('apEnabledCheckbox:', apEnabledCheckbox);
                console.log('apEnabledCheckbox.checked:', apEnabledCheckbox ? apEnabledCheckbox.checked : 'null');
                console.log('apNameRow:', apNameRow);
                
                if (apEnabledCheckbox && apNameRow) {
                    if (apEnabledCheckbox.checked) {
                        apNameRow.style.display = '';
                        console.log('AP Name row shown');
                    } else {
                        apNameRow.style.display = 'none';
                        console.log('AP Name row hidden');
                    }
                }
            }
            
            // Function to update Static IP configuration visibility
            function updateStaticIpVisibility() {
                const staticIpCheckbox = document.getElementById('staticIpEnabled');
                const localIpRow = document.getElementById('localIpRow');
                const gatewayRow = document.getElementById('gatewayRow');
                const subnetRow = document.getElementById('subnetRow');
                
                console.log('updateStaticIpVisibility called');
                console.log('staticIpCheckbox:', staticIpCheckbox);
                console.log('staticIpCheckbox.checked:', staticIpCheckbox ? staticIpCheckbox.checked : 'null');
                
                if (staticIpCheckbox) {
                    const display = staticIpCheckbox.checked ? '' : 'none';
                    if (localIpRow) {
                        localIpRow.style.display = display;
                        console.log('Local IP row display:', display);
                    }
                    if (gatewayRow) {
                        gatewayRow.style.display = display;
                        console.log('Gateway row display:', display);
                    }
                    if (subnetRow) {
                        subnetRow.style.display = display;
                        console.log('Subnet row display:', display);
                    }
                }
            }
            
            // Function to update MQTT configuration visibility
            function updateMqttVisibility() {
                const mqttCheckbox = document.getElementById('mqttEnabled');
                const mqttServerRow = document.getElementById('mqttServerRow');
                const mqttPortRow = document.getElementById('mqttPortRow');
                const mqttUserRow = document.getElementById('mqttUserRow');
                const mqttPasswordRow = document.getElementById('mqttPasswordRow');
                const mqttTopicRow = document.getElementById('mqttTopicRow');
                const mqttTimeoutRow = document.getElementById('mqttTimeoutRow');
                const mqttObjIdPrefixRow = document.getElementById('mqttObjIdPrefixRow');
                const mqttDeviceNameRow = document.getElementById('mqttDeviceNameRow');
                const haDeviceIdRow = document.getElementById('haDeviceIdRow');
                
                console.log('updateMqttVisibility called');
                console.log('mqttCheckbox:', mqttCheckbox);
                console.log('mqttCheckbox.checked:', mqttCheckbox ? mqttCheckbox.checked : 'null');
                
                if (mqttCheckbox) {
                    const display = mqttCheckbox.checked ? '' : 'none';
                    if (mqttServerRow) mqttServerRow.style.display = display;
                    if (mqttPortRow) mqttPortRow.style.display = display;
                    if (mqttUserRow) mqttUserRow.style.display = display;
                    if (mqttPasswordRow) mqttPasswordRow.style.display = display;
                    if (mqttTopicRow) mqttTopicRow.style.display = display;
                    if (mqttTimeoutRow) mqttTimeoutRow.style.display = display;
                    if (mqttObjIdPrefixRow) mqttObjIdPrefixRow.style.display = display;
                    if (mqttDeviceNameRow) mqttDeviceNameRow.style.display = display;
                    if (haDeviceIdRow) haDeviceIdRow.style.display = display;
                    console.log('MQTT configuration rows display:', display);
                }
            }
            
            // Initialize visibility on page load
            updateApNameVisibility();
            updateStaticIpVisibility();
            updateMqttVisibility();
            
            // Display config version if available
            fetch('/api/config_version')
                .then(response => response.json())
                .then(data => {
                    const versionElement = document.getElementById('config-version');
                    if (versionElement && data.available) {
                        versionElement.textContent = `Configuration Version: ${data.global_version} (Last Updated: ${new Date(data.timestamp * 1000).toLocaleString()})`;
                    }
                })
                .catch(err => console.log('Config version not available'));
            
            // Update visibility when checkboxes change
            const apEnabledCheckbox = document.getElementById('wifiApEnabled');
            if (apEnabledCheckbox) {
                apEnabledCheckbox.addEventListener('change', updateApNameVisibility);
            }
            
            const staticIpCheckbox = document.getElementById('staticIpEnabled');
            if (staticIpCheckbox) {
                staticIpCheckbox.addEventListener('change', updateStaticIpVisibility);
            }
            
            const mqttCheckbox = document.getElementById('mqttEnabled');
            if (mqttCheckbox) {
                mqttCheckbox.addEventListener('change', updateMqttVisibility);
            }
        };
    )rawliteral";

    String html = generatePage("ESP-NOW Receiver - Settings", content, "", script);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

/**
 * @brief Register the settings page handler with the HTTP server
 */
esp_err_t register_settings_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = root_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
