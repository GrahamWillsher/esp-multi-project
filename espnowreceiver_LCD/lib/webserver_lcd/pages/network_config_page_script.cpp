#include "network_config_page_script.h"

String get_network_config_page_script() {
    return R"rawliteral(
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
                SaveOperation.setButtonState(saveButton, {
                    text: 'Nothing to Save',
                    backgroundColor: '#6c757d',
                    disabled: true,
                    cursor: 'not-allowed'
                });
            } else {
                SaveOperation.setButtonState(saveButton, {
                    text: `Save ${changedCount} Changed Setting${changedCount > 1 ? 's' : ''}`,
                    backgroundColor: '#4CAF50',
                    disabled: false,
                    cursor: 'pointer'
                });
            }
        }
        
        // Load current configuration
        function loadConfig() {
            fetch('/api/get_receiver_network')
                .then(response => response.json())
                .then(data => {
                    // Populate WiFi credentials
                    document.getElementById('wifiMac').value = data.wifi_mac || 'N/A';
                    document.getElementById('hostname').value = data.hostname || '';
                    document.getElementById('ssid').value = data.ssid || '';
                    document.getElementById('password').value = data.password || '';  // Show saved password
                    document.getElementById('wifiChannel').value = data.channel || 'N/A';
                    
                    // Populate device details
                    document.getElementById('chipModel').value = data.chip_model || 'N/A';
                    document.getElementById('chipRevision').value = data.chip_revision || 'N/A';

                    // Populate metadata-driven receiver device name
                    fetch('/api/firmware_info')
                        .then(response => response.json())
                        .then(meta => {
                            const deviceEl = document.getElementById('deviceName');
                            if (!deviceEl) return;
                            if (meta.valid) {
                                const env = (meta.env || '').replace(/[-_]+/g, ' ').trim();
                                const prettyEnv = env.replace(/\b\w/g, c => c.toUpperCase());
                                if (prettyEnv && meta.device) {
                                    deviceEl.value = `${prettyEnv} (${meta.device})`;
                                } else if (prettyEnv) {
                                    deviceEl.value = prettyEnv;
                                } else if (meta.device) {
                                    deviceEl.value = meta.device;
                                } else {
                                    deviceEl.value = 'Unknown';
                                }
                            } else {
                                deviceEl.value = 'Metadata unavailable';
                            }
                        })
                        .catch(() => {
                            const deviceEl = document.getElementById('deviceName');
                            if (deviceEl) deviceEl.value = 'Failed to load';
                        });
                    
                    document.getElementById('useStaticIP').checked = data.use_static_ip || false;
                    
                    // Update badge using shared helper
                    ReceiverNetworkFormController.updateNetworkModeBadge(data.use_static_ip, 'networkModeBadge');
                    
                    // Populate static IP fields using shared helper
                    if (data.use_static_ip && data.static_ip) {
                        ReceiverNetworkFormController.setOctets('ip', data.static_ip);
                        ReceiverNetworkFormController.setOctets('gw', data.gateway || '0.0.0.0');
                        ReceiverNetworkFormController.setOctets('sub', data.subnet || '255.255.255.0');
                        if (data.dns_primary && data.dns_primary !== '') {
                            ReceiverNetworkFormController.setOctets('dns1_', data.dns_primary);
                        }
                        if (data.dns_secondary && data.dns_secondary !== '') {
                            ReceiverNetworkFormController.setOctets('dns2_', data.dns_secondary);
                        }
                    }
                    
                    // Toggle visibility using shared helper
                    ReceiverNetworkFormController.toggleStaticIpFields(data.use_static_ip, 
                        ['localIpRow', 'gatewayRow', 'subnetRow', 'dns1Row', 'dns2Row']);
                    
                    
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
                                    const useStatic = element.checked;
                                    ReceiverNetworkFormController.updateNetworkModeBadge(useStatic, 'networkModeBadge');
                                    ReceiverNetworkFormController.toggleStaticIpFields(useStatic,
                                        ['localIpRow', 'gatewayRow', 'subnetRow', 'dns1Row', 'dns2Row']);
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
        
        // Save network configuration
        async function saveNetworkConfig() {
            const btn = document.getElementById('saveNetworkBtn');
            const restoreButtonState = () => {
                updateSaveButtonText(countNetworkChanges());
            };

            SaveOperation.setButtonState(btn, {
                text: 'Saving...',
                backgroundColor: '#FF9800',
                disabled: true,
                cursor: 'not-allowed'
            });
            
            try {
                // Validate SSID
                const ssid = document.getElementById('ssid').value.trim();
                if (!ssid) {
                    alert('SSID is required');
                    restoreButtonState();
                    return;
                }
                
                // Validate password
                const password = document.getElementById('password').value;
                if (password.length > 0 && password.length < 8) {
                    alert('Password must be at least 8 characters for WPA2 security');
                    restoreButtonState();
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
                    const ipStr = ReceiverNetworkFormController.collectOctets('ip');
                    const gwStr = ReceiverNetworkFormController.collectOctets('gw');
                    const subStr = ReceiverNetworkFormController.collectOctets('sub');
                    
                    config.static_ip = ipStr.split('.').map(x => parseInt(x));
                    config.gateway = gwStr.split('.').map(x => parseInt(x));
                    config.subnet = subStr.split('.').map(x => parseInt(x));
                    
                    // Validate IP octets
                    const allOctets = [...config.static_ip, ...config.gateway, ...config.subnet];
                    if (allOctets.some(octet => isNaN(octet) || octet < 0 || octet > 255)) {
                        alert('All IP address fields must be numbers between 0 and 255');
                        restoreButtonState();
                        return;
                    }
                    
                    // Optional DNS servers
                    const dns1_0 = document.getElementById('dns1_0').value;
                    if (dns1_0) {
                        const dns1Str = ReceiverNetworkFormController.collectOctets('dns1_');
                        config.dns_primary = dns1Str.split('.').map(x => parseInt(x));
                    }
                    
                    const dns2_0 = document.getElementById('dns2_0').value;
                    if (dns2_0) {
                        const dns2Str = ReceiverNetworkFormController.collectOctets('dns2_');
                        config.dns_secondary = dns2Str.split('.').map(x => parseInt(x));
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
                    SaveOperation.setButtonState(btn, {
                        text: '✓ Saved! Device will reboot...',
                        backgroundColor: '#28a745',
                        disabled: true,
                        cursor: 'not-allowed'
                    });
                    
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
                    alert('Failed to save configuration: ' + (data.error || 'Unknown error'));
                    SaveOperation.showError(btn, '✗ Save Failed', restoreButtonState, 2000);
                }
            } catch (err) {
                console.error('Save failed:', err);
                alert('Failed to save configuration. Please try again.');
                SaveOperation.showError(btn, '✗ Save Failed', restoreButtonState, 2000);
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
}