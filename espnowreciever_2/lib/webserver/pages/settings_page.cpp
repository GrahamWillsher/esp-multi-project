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
    <h2>Transmitter Configuration (ESP32-POE-ISO)</h2>
    )rawliteral";
    
    // Add navigation buttons from central registry
    content += "    " + generate_nav_buttons("/");
    
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
    
    <p style='color: #999; font-size: 12px; margin-top: 10px;' id='config-version'>
        <!-- Version info will be populated via JavaScript -->
    </p>
    
    <div style='text-align: center; margin-top: 30px; margin-bottom: 30px;'>
        <button id='saveNetworkBtn' onclick='saveTransmitterConfig()' style='padding: 12px 40px; font-size: 16px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;'>
            Save Transmitter Configuration
        </button>
    </div>
)rawliteral";

    // Configuration is loaded via template processor - no warning needed

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
        console.log('Settings page script loading...');
        
        // ===== GLOBAL SCOPE VARIABLES AND FUNCTIONS =====
        // These must be global so they can be accessed from event handlers
        
        // Store initial transmitter config values to detect changes across ALL sections
        let initialTransmitterConfig = {};
        
        // Store both DHCP and Static IP configurations
        let dhcpConfig = { ip: null, gateway: null, subnet: null };
        let staticConfig = { ip: null, gateway: null, subnet: null, dns1: null, dns2: null };
        
        console.log('Global variables initialized');
        
        // Transmitter config field IDs - includes ALL configurable sections
        // Add new sections here as they become configurable
        const TRANSMITTER_CONFIG_FIELDS = [
            // Network Configuration
            'staticIpEnabled',
            'ip0', 'ip1', 'ip2', 'ip3',
            'gw0', 'gw1', 'gw2', 'gw3',
            'sub0', 'sub1', 'sub2', 'sub3',
            'dns1_0', 'dns1_1', 'dns1_2', 'dns1_3',
            'dns2_0', 'dns2_1', 'dns2_2', 'dns2_3',
            // MQTT Configuration
            'mqttEnabled',
            'mqtt_server_0', 'mqtt_server_1', 'mqtt_server_2', 'mqtt_server_3',
            'mqttPort',
            'mqttUsername',
            'mqttPassword',
            'mqttClientId'
            // Battery Configuration fields will be added here when implemented
            // Power Settings fields will be added here when implemented
        ];
        
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
        
        // Function to update MQTT configuration visibility
        function updateMqttVisibility() {
            const mqttCheckbox = document.getElementById('mqttEnabled');
            const mqttServerRow = document.getElementById('mqttServerRow');
            const mqttPortRow = document.getElementById('mqttPortRow');
            const mqttUserRow = document.getElementById('mqttUserRow');
            const mqttPasswordRow = document.getElementById('mqttPasswordRow');
            const mqttClientIdRow = document.getElementById('mqttClientIdRow');
            
            console.log('updateMqttVisibility called');
            console.log('mqttCheckbox:', mqttCheckbox);
            console.log('mqttCheckbox.checked:', mqttCheckbox ? mqttCheckbox.checked : 'null');
            
            if (mqttCheckbox) {
                const display = mqttCheckbox.checked ? '' : 'none';
                if (mqttServerRow) mqttServerRow.style.display = display;
                if (mqttPortRow) mqttPortRow.style.display = display;
                if (mqttUserRow) mqttUserRow.style.display = display;
                if (mqttPasswordRow) mqttPasswordRow.style.display = display;
                if (mqttClientIdRow) mqttClientIdRow.style.display = display;
                console.log('MQTT configuration rows display:', display);
            }
        }
        
        // Store initial values for change detection across all transmitter config sections
        function storeInitialTransmitterConfig() {
            TRANSMITTER_CONFIG_FIELDS.forEach(fieldId => {
                const element = document.getElementById(fieldId);
                if (element) {
                    initialTransmitterConfig[fieldId] = element.type === 'checkbox' ? element.checked : element.value;
                }
            });
            console.log('Initial transmitter config stored:', initialTransmitterConfig);
        }
        
        // Attach change listeners to all transmitter config fields
        function attachTransmitterChangeListeners() {
            TRANSMITTER_CONFIG_FIELDS.forEach(fieldId => {
                const element = document.getElementById(fieldId);
                if (element) {
                    // Listen to both input and change events
                    const eventTypes = element.type === 'checkbox' ? ['change'] : ['input', 'change'];
                    eventTypes.forEach(eventType => {
                        element.addEventListener(eventType, () => {
                            console.log('Change detected in field:', fieldId, 'event:', eventType);
                            const changedCount = countTransmitterChanges();
                            console.log('Changed count:', changedCount);
                            updateSaveButtonText(changedCount);
                            
                            // Toggle field visibility when checkbox changes
                            if (fieldId === 'staticIpEnabled') {
                                toggleNetworkFields();
                            }
                        });
                    });
                } else {
                    console.warn('Field not found:', fieldId);
                }
            });
            console.log('Change listeners attached to', TRANSMITTER_CONFIG_FIELDS.length, 'fields');
        }
        
        // Count how many fields have changed from initial values
        function countTransmitterChanges() {
            let changes = 0;
            TRANSMITTER_CONFIG_FIELDS.forEach(fieldId => {
                const element = document.getElementById(fieldId);
                if (element) {
                    const currentValue = element.type === 'checkbox' ? element.checked : element.value;
                    const initialValue = initialTransmitterConfig[fieldId];
                    if (initialValue !== currentValue) {
                        console.log('Change in', fieldId, ':', initialValue, '->', currentValue);
                        changes++;
                    }
                }
            });
            return changes;
        }
        
        // Update save button text based on number of changed fields
        function updateSaveButtonText(changedCount) {
            const saveButton = document.getElementById('saveNetworkBtn');
            console.log('Updating button text, changedCount:', changedCount);
            if (changedCount === 0) {
                saveButton.textContent = 'Nothing to Save';
                saveButton.style.backgroundColor = '#6c757d';
                saveButton.disabled = true;
            } else {
                saveButton.textContent = `Save ${changedCount} Changed Setting${changedCount > 1 ? 's' : ''}`;
                saveButton.style.backgroundColor = '#4CAF50';
                saveButton.disabled = false;
            }
        }
        
        // Load network configuration from transmitter
        async function loadNetworkConfig() {
            try {
                const response = await fetch('/api/get_network_config');
                const data = await response.json();
                
                // If cache is empty, request data from transmitter
                if (!data.success) {
                    console.log('Network config cache empty - requesting from transmitter');
                    
                    // Send request to transmitter
                    const requestResponse = await fetch('/api/request_network_config', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' }
                    });
                    const requestResult = await requestResponse.json();
                    
                    if (requestResult.success) {
                        console.log('Request sent to transmitter - waiting for response...');
                        
                        // Wait 2 seconds for transmitter to respond
                        await new Promise(resolve => setTimeout(resolve, 2000));
                        
                        // Retry loading config
                        const retryResponse = await fetch('/api/get_network_config');
                        const retryData = await retryResponse.json();
                        
                        if (retryData.success && retryData.current && retryData.static_config) {
                            console.log('Network config received from transmitter');
                            populateNetworkConfig(retryData);
                        } else {
                            console.error('Still no network config after request - transmitter may be offline');
                            alert('Cannot load network configuration. Please ensure transmitter is powered on and connected.');
                        }
                    } else {
                        console.error('Failed to send request to transmitter:', requestResult.message);
                        alert('Cannot communicate with transmitter: ' + requestResult.message);
                    }
                    return;
                }
                
                // Cache has data - populate normally
                populateNetworkConfig(data);
            } catch (error) {
                console.error('Failed to load network config:', error);
            }
        }
        
        // Populate network configuration fields with data
        function populateNetworkConfig(data) {
            if (!data.success || !data.current || !data.static_config) {
                console.error('Invalid network config data');
                return;
            }
            
            // Parse current network configuration (active IP - DHCP or Static)
            const currentIp = data.current.ip.split('.');
            const currentGateway = data.current.gateway.split('.');
            const currentSubnet = data.current.subnet.split('.');
            
            // Parse static configuration from NVS
            const staticIp = data.static_config.ip.split('.');
            const staticGateway = data.static_config.gateway.split('.');
            const staticSubnet = data.static_config.subnet.split('.');
            const staticDns1 = data.static_config.dns_primary.split('.');
            const staticDns2 = data.static_config.dns_secondary.split('.');
            
            // Store static configuration (always from static_config)
            staticConfig.ip = staticIp;
            staticConfig.gateway = staticGateway;
            staticConfig.subnet = staticSubnet;
            staticConfig.dns1 = staticDns1;
            staticConfig.dns2 = staticDns2;
            
            // Store DHCP configuration
            // Only save DHCP values if device is currently in DHCP mode
            // If in Static mode, we don't have DHCP values, so leave as null
            if (!data.use_static_ip) {
                // Currently in DHCP mode, so current values ARE the DHCP values
                dhcpConfig.ip = currentIp;
                dhcpConfig.gateway = currentGateway;
                dhcpConfig.subnet = currentSubnet;
                console.log('Saved DHCP config from current values');
            } else {
                // Currently in Static mode, DHCP values not available
                dhcpConfig.ip = null;
                dhcpConfig.gateway = null;
                dhcpConfig.subnet = null;
                console.log('DHCP config not available (device in Static mode)');
            }
            
            // Set checkbox and initial badge state
            document.getElementById('staticIpEnabled').checked = data.use_static_ip;
            const badge = document.getElementById('networkModeBadge');
            if (badge) {
                badge.textContent = data.use_static_ip ? 'Static IP' : 'DHCP';
                badge.className = data.use_static_ip ? 'network-mode-badge badge-static' : 'network-mode-badge badge-dhcp';
            }
            
            // Set IP fields with appropriate values
            const currentConfig = data.use_static_ip ? staticConfig : dhcpConfig;
            for (let i = 0; i < 4; i++) {
                document.getElementById('ip' + i).value = currentConfig.ip[i];
                document.getElementById('gw' + i).value = currentConfig.gateway[i];
                document.getElementById('sub' + i).value = currentConfig.subnet[i];
                if (data.use_static_ip) {
                    document.getElementById('dns1_' + i).value = staticDns1[i];
                    document.getElementById('dns2_' + i).value = staticDns2[i];
                }
            }
            
            // Update field visibility
            toggleNetworkFields();
            
            // Store initial values and attach listeners
            storeInitialTransmitterConfig();
            attachTransmitterChangeListeners();
            updateSaveButtonText(0);
            
            console.log('Network config loaded and populated');
            console.log('DHCP config:', dhcpConfig);
            console.log('Static config:', staticConfig);
        }
        
        // Toggle network input fields based on static IP checkbox
        function toggleNetworkFields() {
            console.log('toggleNetworkFields called');
            const checkbox = document.getElementById('staticIpEnabled');
            if (!checkbox) {
                console.error('staticIpEnabled checkbox not found!');
                return;
            }
            const enabled = checkbox.checked;
            console.log('Static IP enabled:', enabled);
            
            // Update mode badge
            const badge = document.getElementById('networkModeBadge');
            if (badge) {
                badge.textContent = enabled ? 'Static IP' : 'DHCP';
                badge.className = enabled ? 'network-mode-badge badge-static' : 'network-mode-badge badge-dhcp';
                console.log('Badge updated:', badge.textContent);
            } else {
                console.warn('Badge not found');
            }
            
            // Swap IP values between DHCP and Static when toggling
            const targetConfig = enabled ? staticConfig : dhcpConfig;
            console.log('Target config:', targetConfig);
            if (targetConfig && targetConfig.ip) {
                console.log('Swapping IP values to:', targetConfig.ip);
                for (let i = 0; i < 4; i++) {
                    document.getElementById('ip' + i).value = targetConfig.ip[i];
                    document.getElementById('gw' + i).value = targetConfig.gateway[i];
                    document.getElementById('sub' + i).value = targetConfig.subnet[i];
                }
            } else if (!enabled && !dhcpConfig.ip) {
                // Toggling to DHCP but no DHCP values available (device was in Static mode)
                console.log('DHCP config not available - clearing fields');
                for (let i = 0; i < 4; i++) {
                    document.getElementById('ip' + i).value = '';
                    document.getElementById('gw' + i).value = '';
                    document.getElementById('sub' + i).value = '';
                }
                // Show a helpful message in the first octet
                document.getElementById('ip0').placeholder = 'Not configured';
                document.getElementById('gw0').placeholder = 'Not configured';
                document.getElementById('sub0').placeholder = 'Not configured';
            } else {
                console.log('Config not loaded yet, keeping current values');
            }
            
            // Enable/disable IP, Gateway, Subnet fields
            const ipFields = ['ip0', 'ip1', 'ip2', 'ip3', 'gw0', 'gw1', 'gw2', 'gw3', 'sub0', 'sub1', 'sub2', 'sub3'];
            console.log('Toggling fields, enabled:', enabled);
            ipFields.forEach(fieldId => {
                const field = document.getElementById(fieldId);
                if (field) {
                    field.disabled = !enabled;
                    // Update visual styling based on state
                    if (enabled) {
                        field.classList.remove('readonly-field');
                        field.classList.add('editable-field');
                    } else {
                        field.classList.remove('editable-field');
                        field.classList.add('readonly-field');
                    }
                }
            });
            
            // Show/hide DNS rows and warning
            const toggleRows = ['dns1Row', 'dns2Row', 'networkWarningRow'];
            toggleRows.forEach(rowId => {
                const row = document.getElementById(rowId);
                if (row) {
                    row.style.display = enabled ? 'grid' : 'none';
                    console.log('Row', rowId, 'display:', row.style.display);
                }
            });
            
            // Update DNS fields' class styling when showing
            if (enabled) {
                const dnsFields = ['dns1_0', 'dns1_1', 'dns1_2', 'dns1_3', 'dns2_0', 'dns2_1', 'dns2_2', 'dns2_3'];
                dnsFields.forEach(fieldId => {
                    const field = document.getElementById(fieldId);
                    if (field) {
                        field.classList.remove('readonly-field');
                        field.classList.add('editable-field');
                    }
                });
            }
            
            console.log('toggleNetworkFields complete');
        }
        
        // ===== MQTT Configuration Functions =====
        
        // Load MQTT configuration from receiver cache
        async function loadMqttConfig() {
            try {
                const response = await fetch('/api/get_mqtt_config');
                const data = await response.json();
                
                // If cache is empty, request data from transmitter
                if (!data.success) {
                    console.log('MQTT config cache empty - requesting from transmitter');
                    
                    // Send request to transmitter
                    const requestResponse = await fetch('/api/request_mqtt_config', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' }
                    });
                    const requestResult = await requestResponse.json();
                    
                    if (requestResult.success) {
                        console.log('Request sent to transmitter - waiting for response...');
                        
                        // Wait 2 seconds for transmitter to respond
                        await new Promise(resolve => setTimeout(resolve, 2000));
                        
                        // Retry loading config
                        const retryResponse = await fetch('/api/get_mqtt_config');
                        const retryData = await retryResponse.json();
                        
                        if (retryData.success) {
                            console.log('MQTT config received from transmitter');
                            populateMqttConfig(retryData);
                        } else {
                            console.error('Still no MQTT config after request - transmitter may be offline');
                            alert('Cannot load MQTT configuration. Please ensure transmitter is powered on and connected.');
                        }
                    } else {
                        console.error('Failed to send request to transmitter:', requestResult.message);
                        alert('Cannot communicate with transmitter: ' + requestResult.message);
                    }
                    return;
                }
                
                // Cache has data - populate normally
                populateMqttConfig(data);
            } catch (error) {
                console.error('Failed to load MQTT config:', error);
            }
        }
        
        // Populate MQTT configuration fields with data
        function populateMqttConfig(data) {
            if (!data.success) {
                console.error('Invalid MQTT config data');
                return;
            }
            
            // Populate enabled checkbox
            const mqttEnabledCheckbox = document.getElementById('mqttEnabled');
            if (mqttEnabledCheckbox) {
                mqttEnabledCheckbox.checked = data.enabled;
            }
            
            // Parse and populate server IP
            const serverIp = data.server.split('.');
            for (let i = 0; i < 4; i++) {
                const field = document.getElementById('mqtt_server_' + i);
                if (field) {
                    field.value = serverIp[i] || '0';
                }
            }
            
            // Populate port
            const portField = document.getElementById('mqttPort');
            if (portField) {
                portField.value = data.port;
            }
            
            // Populate username
            const usernameField = document.getElementById('mqttUsername');
            if (usernameField) {
                usernameField.value = data.username;
            }
            
            // Populate password (displayed as ********)
            const passwordField = document.getElementById('mqttPassword');
            if (passwordField) {
                passwordField.value = data.password;
            }
            
            // Populate client ID
            const clientIdField = document.getElementById('mqttClientId');
            if (clientIdField) {
                clientIdField.value = data.client_id;
            }
            
            // Update visibility of MQTT fields
            updateMqttVisibility();
            
            // Update MQTT status dot indicator
            const mqttStatusDot = document.getElementById('mqttStatusDot');
            if (mqttStatusDot) {
                if (data.enabled && data.connected) {
                    // MQTT enabled and broker connected - show green
                    mqttStatusDot.style.display = 'inline-block';
                    mqttStatusDot.className = 'status-dot connected';
                    mqttStatusDot.title = 'MQTT Connected';
                } else if (data.enabled && !data.connected) {
                    // MQTT enabled but not connected - show orange/connecting
                    mqttStatusDot.style.display = 'inline-block';
                    mqttStatusDot.className = 'status-dot connecting';
                    mqttStatusDot.title = 'MQTT Disconnected';
                } else {
                    // MQTT disabled - hide dot
                    mqttStatusDot.style.display = 'none';
                }
            }
            
            console.log('MQTT config loaded and populated');
        }
        
        // Save MQTT configuration to transmitter
        async function saveMqttConfig() {
            try {
                const enabled = document.getElementById('mqttEnabled').checked;
                
                const config = {
                    enabled: enabled,
                    server: `${document.getElementById('mqtt_server_0').value}.${document.getElementById('mqtt_server_1').value}.${document.getElementById('mqtt_server_2').value}.${document.getElementById('mqtt_server_3').value}`,
                    port: parseInt(document.getElementById('mqttPort').value),
                    username: document.getElementById('mqttUsername').value,
                    password: document.getElementById('mqttPassword').value,
                    client_id: document.getElementById('mqttClientId').value
                };
                
                const response = await fetch('/api/save_mqtt_config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(config)
                });
                
                const data = await response.json();
                
                return data.success;
            } catch (error) {
                console.error('Failed to save MQTT config:', error);
                return false;
            }
        }
        
        // ===== Network Configuration Functions =====
        
        // Unified save function for all transmitter configuration sections
        async function saveTransmitterConfig() {
            const btn = document.getElementById('saveNetworkBtn');
            const originalText = btn.textContent;
            
            btn.disabled = true;
            btn.textContent = 'Saving...';
            btn.style.backgroundColor = '#FF9800';
            
            try {
                // Detect which sections have changes
                const networkFields = ['staticIpEnabled', 'ip0', 'ip1', 'ip2', 'ip3', 'gw0', 'gw1', 'gw2', 'gw3', 'sub0', 'sub1', 'sub2', 'sub3', 'dns1_0', 'dns1_1', 'dns1_2', 'dns1_3', 'dns2_0', 'dns2_1', 'dns2_2', 'dns2_3'];
                const mqttFields = ['mqttEnabled', 'mqtt_server_0', 'mqtt_server_1', 'mqtt_server_2', 'mqtt_server_3', 'mqttPort', 'mqttUsername', 'mqttPassword', 'mqttClientId'];
                
                let hasNetworkChanges = false;
                let hasMqttChanges = false;
                
                // Check for network changes
                networkFields.forEach(fieldId => {
                    const element = document.getElementById(fieldId);
                    if (element) {
                        const currentValue = element.type === 'checkbox' ? element.checked : element.value;
                        const initialValue = initialTransmitterConfig[fieldId];
                        if (initialValue !== currentValue) {
                            hasNetworkChanges = true;
                        }
                    }
                });
                
                // Check for MQTT changes
                mqttFields.forEach(fieldId => {
                    const element = document.getElementById(fieldId);
                    if (element) {
                        const currentValue = element.type === 'checkbox' ? element.checked : element.value;
                        const initialValue = initialTransmitterConfig[fieldId];
                        if (initialValue !== currentValue) {
                            hasMqttChanges = true;
                        }
                    }
                });
                
                console.log('Changes detected - Network:', hasNetworkChanges, 'MQTT:', hasMqttChanges);
                
                let networkSuccess = true;
                let mqttSuccess = true;
                let errorMessage = '';
                
                // Save network config if changed
                if (hasNetworkChanges) {
                    console.log('Saving network configuration...');
                    const use_static_ip = document.getElementById('staticIpEnabled').checked;
                    
                    const networkConfig = {
                        use_static_ip: use_static_ip,
                        ip: use_static_ip ? 
                            `${document.getElementById('ip0').value}.${document.getElementById('ip1').value}.${document.getElementById('ip2').value}.${document.getElementById('ip3').value}` : 
                            '0.0.0.0',
                        gateway: use_static_ip ?
                            `${document.getElementById('gw0').value}.${document.getElementById('gw1').value}.${document.getElementById('gw2').value}.${document.getElementById('gw3').value}` :
                            '0.0.0.0',
                        subnet: use_static_ip ?
                            `${document.getElementById('sub0').value}.${document.getElementById('sub1').value}.${document.getElementById('sub2').value}.${document.getElementById('sub3').value}` :
                            '0.0.0.0',
                        dns_primary: use_static_ip ?
                            `${document.getElementById('dns1_0').value}.${document.getElementById('dns1_1').value}.${document.getElementById('dns1_2').value}.${document.getElementById('dns1_3').value}` :
                            '8.8.8.8',
                        dns_secondary: use_static_ip ?
                            `${document.getElementById('dns2_0').value}.${document.getElementById('dns2_1').value}.${document.getElementById('dns2_2').value}.${document.getElementById('dns2_3').value}` :
                            '8.8.4.4'
                    };
                    
                    const response = await fetch('/api/save_network_config', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify(networkConfig)
                    });
                    
                    const data = await response.json();
                    networkSuccess = data.success;
                    if (!networkSuccess) {
                        errorMessage += 'Network: ' + (data.message || 'Unknown error') + ' ';
                    }
                }
                
                // Save MQTT config if changed
                if (hasMqttChanges) {
                    console.log('Saving MQTT configuration...');
                    mqttSuccess = await saveMqttConfig();
                    if (!mqttSuccess) {
                        errorMessage += 'MQTT: Save failed ';
                    }
                }
                
                // Update button based on results
                if (networkSuccess && mqttSuccess) {
                    const savedSections = [];
                    if (hasNetworkChanges) savedSections.push('Network');
                    if (hasMqttChanges) savedSections.push('MQTT');
                    
                    btn.textContent = '✓ Saved ' + savedSections.join(' + ') + '! Reboot transmitter to apply.';
                    btn.style.backgroundColor = '#28a745';
                    
                    // Update initial values to reflect saved state
                    storeInitialTransmitterConfig();
                    
                    setTimeout(() => {
                        updateSaveButtonText(0);
                    }, 3000);
                } else {
                    btn.textContent = '✗ Failed: ' + errorMessage;
                    btn.style.backgroundColor = '#dc3545';
                    setTimeout(() => {
                        btn.textContent = originalText;
                        btn.style.backgroundColor = '#4CAF50';
                        btn.disabled = false;
                    }, 3000);
                }
            } catch (error) {
                btn.textContent = '✗ Save error';
                btn.style.backgroundColor = '#dc3545';
                console.error('Save failed:', error);
                setTimeout(() => {
                    btn.textContent = originalText;
                    btn.style.backgroundColor = '#4CAF50';
                    btn.disabled = false;
                }, 3000);
            }
        }
        
        // Save network configuration to transmitter (legacy - called by unified save)
        async function saveNetworkConfig() {
            const btn = document.getElementById('saveNetworkBtn');
            const originalText = btn.textContent;
            
            btn.disabled = true;
            btn.textContent = 'Saving...';
            btn.style.backgroundColor = '#FF9800';
            
            try {
                const use_static_ip = document.getElementById('staticIpEnabled').checked;
                
                const config = {
                    use_static_ip: use_static_ip,
                    ip: use_static_ip ? 
                        `${document.getElementById('ip0').value}.${document.getElementById('ip1').value}.${document.getElementById('ip2').value}.${document.getElementById('ip3').value}` : 
                        '0.0.0.0',
                    gateway: use_static_ip ?
                        `${document.getElementById('gw0').value}.${document.getElementById('gw1').value}.${document.getElementById('gw2').value}.${document.getElementById('gw3').value}` :
                        '0.0.0.0',
                    subnet: use_static_ip ?
                        `${document.getElementById('sub0').value}.${document.getElementById('sub1').value}.${document.getElementById('sub2').value}.${document.getElementById('sub3').value}` :
                        '0.0.0.0',
                    dns_primary: use_static_ip ?
                        `${document.getElementById('dns1_0').value}.${document.getElementById('dns1_1').value}.${document.getElementById('dns1_2').value}.${document.getElementById('dns1_3').value}` :
                        '8.8.8.8',
                    dns_secondary: use_static_ip ?
                        `${document.getElementById('dns2_0').value}.${document.getElementById('dns2_1').value}.${document.getElementById('dns2_2').value}.${document.getElementById('dns2_3').value}` :
                        '8.8.4.4'
                };
                
                const response = await fetch('/api/save_network_config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(config)
                });
                
                const data = await response.json();
                
                if (data.success) {
                    btn.textContent = '✓ Saved! Reboot transmitter to apply.';
                    btn.style.backgroundColor = '#28a745';
                    
                    // Update initial values to reflect saved state
                    storeInitialTransmitterConfig();
                    
                    setTimeout(() => {
                        updateSaveButtonText(0);
                    }, 3000);
                } else {
                    btn.textContent = '✗ Failed: ' + (data.message || 'Unknown error');
                    btn.style.backgroundColor = '#dc3545';
                    setTimeout(() => {
                        btn.textContent = originalText;
                        btn.style.backgroundColor = '#4CAF50';
                        btn.disabled = false;
                    }, 3000);
                }
            } catch (error) {
                btn.textContent = '✗ Network error';
                btn.style.backgroundColor = '#dc3545';
                console.error('Save failed:', error);
                setTimeout(() => {
                    btn.textContent = originalText;
                    btn.style.backgroundColor = '#4CAF50';
                    btn.disabled = false;
                }, 3000);
            }
        }
        
        // ===== PAGE INITIALIZATION =====
        // Initialize on page load
        window.addEventListener('DOMContentLoaded', function() {
            console.log('DOMContentLoaded event fired');
            
            // Attach event listener to static IP checkbox
            const staticIpCheckbox = document.getElementById('staticIpEnabled');
            if (staticIpCheckbox) {
                staticIpCheckbox.addEventListener('change', toggleNetworkFields);
                console.log('Static IP checkbox event listener attached');
            } else {
                console.error('Static IP checkbox not found!');
            }
            
            // Initialize visibility
            updateApNameVisibility();
            updateMqttVisibility();
            
            // Load configurations (this will store initial values and attach listeners)
            loadNetworkConfig();
            loadMqttConfig();
        });
        
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
        .uri       = "/transmitter/config",
        .method    = HTTP_GET,
        .handler   = root_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
