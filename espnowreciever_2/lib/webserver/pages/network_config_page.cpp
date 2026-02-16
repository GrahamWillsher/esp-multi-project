#include "network_config_page.h"
#include "../common/nav_buttons.h"
#include "../common/page_generator.h"
#include <Arduino.h>
#include <WiFi.h>

// Network configuration page handler - WiFi and IP settings
esp_err_t network_config_handler(httpd_req_t *req) {
    // Check if we're in AP mode - if so, show minimal setup page
    bool isAPMode = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
    
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
                   style='background-color: #f0f0f0;' value='LilyGo T-Display-S3'>
            
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

    String style = R"rawliteral(
        .form-control {
            max-width: 250px;
            padding: 0.5rem;
            border: 1px solid #ddd;
            border-radius: 4px;
            font-size: 1rem;
            box-sizing: border-box;
        }
        
        .form-control:focus {
            outline: none;
            border-color: #007bff;
            box-shadow: 0 0 0 3px rgba(0, 123, 255, 0.1);
        }
        
        .form-help {
            display: block;
            margin-top: 0.25rem;
            font-size: 0.875rem;
            color: #666;
        }
        
        .required {
            color: #dc3545;
        }
        
        /* IP row and octet styles removed - using common_styles.h definitions */
        
        .button-group {
            display: flex;
            gap: 1rem;
            margin-top: 2rem;
            justify-content: center;
        }
        
        .btn {
            padding: 0.75rem 1.5rem;
            font-size: 1rem;
            font-weight: 600;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            transition: background-color 0.2s;
            min-width: 150px;
        }
        
        .btn-primary {
            background-color: #4CAF50 !important;
            color: white !important;
        }
        
        .btn-primary:hover {
            background-color: #45a049 !important;
        }
        
        .btn-secondary {
            background-color: #6c757d !important;
            color: white !important;
        }
        
        .btn-secondary:hover {
            background-color: #545b62 !important;
        }
        
        .alert {
            padding: 1rem;
            border-radius: 4px;
            margin-bottom: 1.5rem;
        }
        
        .alert-warning {
            background-color: #fff3cd;
            border: 1px solid #ffc107;
            color: #856404;
        }
        
        .alert-info {
            background-color: #d1ecf1;
            border: 1px solid #17a2b8;
            color: #0c5460;
        }
        
        /* Editable field styling - right-aligned text */
        .editable-field {
            text-align: right;
        }
        
        /* MAC address monospace font */
        .editable-mac {
            font-family: 'Courier New', Courier, monospace;
        }
        
        /* Enhanced checkbox row highlight */
        .checkbox-row {
            display: flex;
            align-items: center;
            gap: 0.5rem;
            padding: 0.75rem;
            border-radius: 4px;
            background-color: #f8f9fa;
            border: 2px solid #e9ecef;
            transition: all 0.2s ease;
        }
        
        .checkbox-row:hover {
            background-color: #e9ecef;
            border-color: #007bff;
        }
        
        .checkbox-row input[type="checkbox"] {
            width: 20px;
            height: 20px;
            cursor: pointer;
        }
        
        .checkbox-row label {
            margin: 0;
            cursor: pointer;
            font-weight: 500;
        }
)rawliteral";

    String script = R"rawliteral(
        let isAPMode = false;
        let initialNetworkConfig = {};
        
        // All fields to track for changes
        const NETWORK_CONFIG_FIELDS = [
            'hostname', 'ssid', 'password', 'useStaticIP',
            'ip0', 'ip1', 'ip2', 'ip3',
            'gw0', 'gw1', 'gw2', 'gw3',
            'sub0', 'sub1', 'sub2', 'sub3',
            'dns1_0', 'dns1_1', 'dns1_2', 'dns1_3',
            'dns2_0', 'dns2_1', 'dns2_2', 'dns2_3'
        ];
        
        // Count changes in network configuration
        function countNetworkChanges() {
            let changes = 0;
            NETWORK_CONFIG_FIELDS.forEach(fieldId => {
                const element = document.getElementById(fieldId);
                if (element) {
                    const currentValue = element.type === 'checkbox' ? element.checked : element.value;
                    const initialValue = initialNetworkConfig[fieldId];
                    if (initialValue !== currentValue) {
                        changes++;
                    }
                }
            });
            return changes;
        }
        
        // Update save button text based on changes
        function updateSaveButtonText(changedCount) {
            const saveButton = document.getElementById('saveNetworkBtn');
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
        
        // Load current configuration
        function loadConfig() {
            fetch('/api/get_receiver_network')
                .then(response => response.json())
                .then(data => {
                    // Check if in AP mode
                    isAPMode = data.is_ap_mode || false;
                    const apWarning = document.getElementById('apModeWarning');
                    if (apWarning) {
                        apWarning.style.display = isAPMode ? 'block' : 'none';
                    }
                    
                    // Populate WiFi credentials
                    document.getElementById('wifiMac').value = data.wifi_mac || 'N/A';
                    document.getElementById('hostname').value = data.hostname || '';
                    document.getElementById('ssid').value = data.ssid || '';
                    document.getElementById('password').value = data.password || '';  // Show saved password
                    document.getElementById('wifiChannel').value = data.channel || 'N/A';
                    
                    // Populate device details
                    document.getElementById('chipModel').value = data.chip_model || 'N/A';
                    document.getElementById('chipRevision').value = data.chip_revision || 'N/A';
                    
                    document.getElementById('useStaticIP').checked = data.use_static_ip || false;
                    
                    // Update badge and toggle visibility
                    const badge = document.getElementById('networkModeBadge');
                    if (badge) {
                        badge.textContent = data.use_static_ip ? 'Static IP' : 'DHCP';
                        badge.className = data.use_static_ip ? 'network-mode-badge badge-static' : 'network-mode-badge badge-dhcp';
                    }
                    
                    // Toggle static IP section visibility
                    toggleStaticIPSection();
                    
                    // Populate static IP fields if present
                    if (data.use_static_ip && data.static_ip) {
                        const ip = data.static_ip.split('.');
                        const gw = (data.gateway || '0.0.0.0').split('.');
                        const sn = (data.subnet || '255.255.255.0').split('.');
                        const dns1 = (data.dns_primary || '').split('.');
                        const dns2 = (data.dns_secondary || '').split('.');
                        
                        // IP address
                        if (ip.length === 4) {
                            document.getElementById('ip0').value = ip[0];
                            document.getElementById('ip1').value = ip[1];
                            document.getElementById('ip2').value = ip[2];
                            document.getElementById('ip3').value = ip[3];
                        }
                        
                        // Gateway
                        if (gw.length === 4) {
                            document.getElementById('gw0').value = gw[0];
                            document.getElementById('gw1').value = gw[1];
                            document.getElementById('gw2').value = gw[2];
                            document.getElementById('gw3').value = gw[3];
                        }
                        
                        // Subnet
                        if (sn.length === 4) {
                            document.getElementById('sub0').value = sn[0];
                            document.getElementById('sub1').value = sn[1];
                            document.getElementById('sub2').value = sn[2];
                            document.getElementById('sub3').value = sn[3];
                        }
                        
                        // DNS (optional)
                        if (dns1.length === 4 && dns1[0] !== '') {
                            document.getElementById('dns1_0').value = dns1[0];
                            document.getElementById('dns1_1').value = dns1[1];
                            document.getElementById('dns1_2').value = dns1[2];
                            document.getElementById('dns1_3').value = dns1[3];
                        }
                        
                        if (dns2.length === 4 && dns2[0] !== '') {
                            document.getElementById('dns2_0').value = dns2[0];
                            document.getElementById('dns2_1').value = dns2[1];
                            document.getElementById('dns2_2').value = dns2[2];
                            document.getElementById('dns2_3').value = dns2[3];
                        }
                    }
                    
                    // Store initial values for change tracking
                    NETWORK_CONFIG_FIELDS.forEach(fieldId => {
                        const element = document.getElementById(fieldId);
                        if (element) {
                            initialNetworkConfig[fieldId] = element.type === 'checkbox' ? element.checked : element.value;
                        }
                    });
                    
                    // Add input event listeners to track changes (after initial values are captured)
                    NETWORK_CONFIG_FIELDS.forEach(fieldId => {
                        const element = document.getElementById(fieldId);
                        if (element) {
                            const eventType = element.type === 'checkbox' ? 'change' : 'input';
                            element.addEventListener(eventType, () => {
                                const changes = countNetworkChanges();
                                updateSaveButtonText(changes);
                                
                                // Update static IP section visibility when checkbox changes
                                if (fieldId === 'useStaticIP') {
                                    toggleStaticIPSection();
                                }
                            });
                        }
                    });
                    
                    // Update button state (initially no changes)
                    updateSaveButtonText(0);
                })
                .catch(err => {
                    console.error('Failed to load network config:', err);
                    alert('Failed to load configuration. Please try again.');
                });
        }
        
        // Toggle static IP section visibility
        function toggleStaticIPSection() {
            const useStatic = document.getElementById('useStaticIP').checked;
            
            // Update mode badge
            const badge = document.getElementById('networkModeBadge');
            if (badge) {
                badge.textContent = useStatic ? 'Static IP' : 'DHCP';
                badge.className = useStatic ? 'network-mode-badge badge-static' : 'network-mode-badge badge-dhcp';
            }
            
            // Show/hide IP configuration rows
            const toggleRows = ['localIpRow', 'gatewayRow', 'subnetRow'];
            toggleRows.forEach(rowId => {
                const row = document.getElementById(rowId);
                if (row) {
                    row.style.display = useStatic ? 'grid' : 'none';
                }
            });
            
            // Show/hide DNS rows (always hidden in DHCP mode)
            const dnsRows = ['dns1Row', 'dns2Row'];
            dnsRows.forEach(rowId => {
                const row = document.getElementById(rowId);
                if (row) {
                    row.style.display = useStatic ? 'grid' : 'none';
                }
            });
        }
        
        // Save network configuration
        async function saveNetworkConfig() {
            const btn = document.getElementById('saveNetworkBtn');
            const originalText = btn.textContent;
            
            btn.disabled = true;
            btn.textContent = 'Saving...';
            btn.style.backgroundColor = '#FF9800';
            
            try {
                // Validate SSID
                const ssid = document.getElementById('ssid').value.trim();
                if (!ssid) {
                    alert('SSID is required');
                    btn.disabled = false;
                    btn.textContent = originalText;
                    btn.style.backgroundColor = '#4CAF50';
                    return;
                }
                
                // Validate password
                const password = document.getElementById('password').value;
                if (password.length > 0 && password.length < 8) {
                    alert('Password must be at least 8 characters for WPA2 security');
                    btn.disabled = false;
                    btn.textContent = originalText;
                    btn.style.backgroundColor = '#4CAF50';
                    return;
                }
                
                // Build configuration object
                const config = {
                    hostname: document.getElementById('hostname').value.trim() || 'esp32-receiver',
                    ssid: ssid,
                    password: password,
                    use_static_ip: document.getElementById('useStaticIP').checked
                };
                
                // Add static IP fields if enabled
                if (config.use_static_ip) {
                    config.static_ip = [
                        parseInt(document.getElementById('ip0').value),
                        parseInt(document.getElementById('ip1').value),
                        parseInt(document.getElementById('ip2').value),
                        parseInt(document.getElementById('ip3').value)
                    ];
                    config.gateway = [
                        parseInt(document.getElementById('gw0').value),
                        parseInt(document.getElementById('gw1').value),
                        parseInt(document.getElementById('gw2').value),
                        parseInt(document.getElementById('gw3').value)
                    ];
                    config.subnet = [
                        parseInt(document.getElementById('sub0').value),
                        parseInt(document.getElementById('sub1').value),
                        parseInt(document.getElementById('sub2').value),
                        parseInt(document.getElementById('sub3').value)
                    ];
                    
                    // Validate IP octets
                    const allOctets = [...config.static_ip, ...config.gateway, ...config.subnet];
                    if (allOctets.some(octet => isNaN(octet) || octet < 0 || octet > 255)) {
                        alert('All IP address fields must be numbers between 0 and 255');
                        btn.disabled = false;
                        btn.textContent = originalText;
                        btn.style.backgroundColor = '#4CAF50';
                        return;
                    }
                    
                    // Optional DNS servers
                    const dns1_0 = document.getElementById('dns1_0').value;
                    if (dns1_0) {
                        config.dns_primary = [
                            parseInt(document.getElementById('dns1_0').value),
                            parseInt(document.getElementById('dns1_1').value),
                            parseInt(document.getElementById('dns1_2').value),
                            parseInt(document.getElementById('dns1_3').value)
                        ];
                    }
                    
                    const dns2_0 = document.getElementById('dns2_0').value;
                    if (dns2_0) {
                        config.dns_secondary = [
                            parseInt(document.getElementById('dns2_0').value),
                            parseInt(document.getElementById('dns2_1').value),
                            parseInt(document.getElementById('dns2_2').value),
                            parseInt(document.getElementById('dns2_3').value)
                        ];
                    }
                }
                
                // Save configuration
                const response = await fetch('/api/save_receiver_network', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json'
                    },
                    body: JSON.stringify(config)
                });
                
                const data = await response.json();
                
                if (data.success) {
                    btn.textContent = '✓ Saved! Device will reboot...';
                    btn.style.backgroundColor = '#28a745';
                    
                    // Show reboot notice and start countdown
                    document.getElementById('rebootNotice').style.display = 'block';
                    
                    let countdown = 3;
                    const countdownEl = document.getElementById('countdown');
                    const interval = setInterval(() => {
                        countdown--;
                        countdownEl.textContent = countdown;
                        if (countdown <= 0) {
                            clearInterval(interval);
                            // Device will reboot, redirect to potential new IP
                            window.location.href = '/';
                        }
                    }, 1000);
                } else {
                    btn.textContent = '✗ Save Failed';
                    btn.style.backgroundColor = '#dc3545';
                    alert('Failed to save configuration: ' + (data.error || 'Unknown error'));
                    
                    setTimeout(() => {
                        btn.disabled = false;
                        btn.textContent = originalText;
                        btn.style.backgroundColor = '#4CAF50';
                    }, 2000);
                }
            } catch (err) {
                console.error('Save failed:', err);
                btn.textContent = '✗ Save Failed';
                btn.style.backgroundColor = '#dc3545';
                alert('Failed to save configuration. Please try again.');
                
                setTimeout(() => {
                    btn.disabled = false;
                    btn.textContent = originalText;
                    btn.style.backgroundColor = '#4CAF50';
                }, 2000);
            }
        }
        
        // Auto-advance octet inputs on valid entry
        document.querySelectorAll('.octet').forEach((input, index, inputs) => {
            input.addEventListener('input', function() {
                if (this.value.length === 3 || (this.value.length > 0 && parseInt(this.value) > 25)) {
                    const nextInput = inputs[index + 1];
                    if (nextInput) nextInput.focus();
                }
            });
        });
        
        // Load configuration on page load
        loadConfig();
    )rawliteral";

    String pageTitle = isAPMode ? "ESP32 Setup" : "Network Configuration";
    String html = generatePage(pageTitle.c_str(), content, style, script);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

esp_err_t register_network_config_page(httpd_handle_t server) {
    httpd_uri_t uri_handler = {
        .uri       = "/receiver/network",
        .method    = HTTP_GET,
        .handler   = network_config_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri_handler);
}
