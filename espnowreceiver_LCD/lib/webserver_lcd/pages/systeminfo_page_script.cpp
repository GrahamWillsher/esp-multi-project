#include "systeminfo_page_script.h"

String get_systeminfo_page_script() {
    return R"rawliteral(
        const RECEIVER_CONFIG_FIELDS = [
            'hostname', 'ssid', 'password', 'useStaticIP',
            'ip0', 'ip1', 'ip2', 'ip3',
            'gw0', 'gw1', 'gw2', 'gw3',
            'sub0', 'sub1', 'sub2', 'sub3',
            'dns1_0', 'dns1_1', 'dns1_2', 'dns1_3',
            'dns2_0', 'dns2_1', 'dns2_2', 'dns2_3',
            'mqttEnabled', 'mqtt0', 'mqtt1', 'mqtt2', 'mqtt3', 'mqttPort', 'mqttUsername', 'mqttPassword'
        ];

        let initialReceiverConfig = {};

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

        function countReceiverChanges() {
            let changes = 0;
            RECEIVER_CONFIG_FIELDS.forEach(fieldId => {
                const element = document.getElementById(fieldId);
                if (element) {
                    const currentValue = element.type === 'checkbox' ? element.checked : element.value;
                    const initialValue = initialReceiverConfig[fieldId];
                    if (initialValue !== currentValue) {
                        changes++;
                    }
                }
            });
            return changes;
        }

        // Load receiver network info
        fetch('/api/get_receiver_network')
            .then(response => response.json())
            .then(data => {
                document.getElementById('wifiMac').value = data.wifi_mac || 'N/A';
                document.getElementById('hostname').value = data.hostname || '';
                document.getElementById('ssid').value = data.ssid || '';
                document.getElementById('password').value = data.password || '';
                document.getElementById('wifiChannel').value = data.channel || 'N/A';

                document.getElementById('chipModel').value = data.chip_model || 'N/A';
                document.getElementById('chipRevision').value = data.chip_revision || 'N/A';

                const useStatic = data.use_static_ip || false;
                document.getElementById('useStaticIP').checked = useStatic;
                
                // Use shared helpers for network form management
                ReceiverNetworkFormController.updateNetworkModeBadge(useStatic, 'networkModeBadge');
                ReceiverNetworkFormController.setOctets('ip', data.static_ip);
                ReceiverNetworkFormController.setOctets('gw', data.gateway);
                ReceiverNetworkFormController.setOctets('sub', data.subnet);
                ReceiverNetworkFormController.setOctets('dns1_', data.dns_primary);
                ReceiverNetworkFormController.setOctets('dns2_', data.dns_secondary);
                ReceiverNetworkFormController.toggleStaticIpFields(useStatic,
                    ['localIpRow', 'gatewayRow', 'subnetRow', 'dns1Row', 'dns2Row']);

                // Load MQTT configuration
                document.getElementById('mqttEnabled').checked = data.mqtt_enabled || false;
                ReceiverNetworkFormController.setOctets('mqtt', data.mqtt_server);
                document.getElementById('mqttPort').value = data.mqtt_port || '1883';
                document.getElementById('mqttUsername').value = data.mqtt_username || '';
                if (data.mqtt_password && data.mqtt_password !== '') {
                    document.getElementById('mqttPassword').value = '********';
                } else {
                    document.getElementById('mqttPassword').value = '';
                }

                RECEIVER_CONFIG_FIELDS.forEach(fieldId => {
                    const element = document.getElementById(fieldId);
                    if (element) {
                        initialReceiverConfig[fieldId] = element.type === 'checkbox' ? element.checked : element.value;
                    }
                });

                RECEIVER_CONFIG_FIELDS.forEach(fieldId => {
                    const element = document.getElementById(fieldId);
                    if (element) {
                        const eventType = element.type === 'checkbox' ? 'change' : 'input';
                        element.addEventListener(eventType, () => {
                            const changes = countReceiverChanges();
                            updateSaveButtonText(changes);

                            if (fieldId === 'useStaticIP') {
                                const useStatic = element.checked;
                                ReceiverNetworkFormController.updateNetworkModeBadge(useStatic, 'networkModeBadge');
                                ReceiverNetworkFormController.toggleStaticIpFields(useStatic,
                                    ['localIpRow', 'gatewayRow', 'subnetRow', 'dns1Row', 'dns2Row']);
                            }
                        });
                    }
                });

                updateSaveButtonText(0);
            })
            .catch(err => {
                console.error('Failed to load receiver network info:', err);
            });

        // Load receiver firmware metadata for authoritative device naming
        fetch('/api/firmware_info')
            .then(response => response.json())
            .then(data => {
                const deviceEl = document.getElementById('deviceName');
                if (!deviceEl) return;

                if (data.valid) {
                    const env = (data.env || '').replace(/[-_]+/g, ' ').trim();
                    const prettyEnv = env.replace(/\b\w/g, c => c.toUpperCase());
                    if (prettyEnv && data.device) {
                        deviceEl.value = `${prettyEnv} (${data.device})`;
                    } else if (prettyEnv) {
                        deviceEl.value = prettyEnv;
                    } else if (data.device) {
                        deviceEl.value = data.device;
                    } else {
                        deviceEl.value = 'Unknown';
                    }
                } else {
                    deviceEl.value = 'Metadata unavailable';
                }
            })
            .catch(() => {
                const deviceEl = document.getElementById('deviceName');
                if (deviceEl) {
                    deviceEl.value = 'Failed to load';
                }
            });

        async function saveReceiverConfig() {
            const btn = document.getElementById('saveNetworkBtn');
            const originalText = btn.textContent;

            btn.disabled = true;
            btn.textContent = 'Saving...';
            btn.style.backgroundColor = '#FF9800';

            try {
                const ssid = document.getElementById('ssid').value.trim();
                if (!ssid) {
                    alert('SSID is required');
                    btn.disabled = false;
                    btn.textContent = originalText;
                    btn.style.backgroundColor = '#4CAF50';
                    return;
                }

                const payload = {
                    hostname: document.getElementById('hostname').value.trim(),
                    ssid: ssid,
                    password: document.getElementById('password').value,
                    use_static_ip: document.getElementById('useStaticIP').checked,
                    mqtt_enabled: document.getElementById('mqttEnabled').checked,
                    mqtt_server: ReceiverNetworkFormController.collectOctets('mqtt'),
                    mqtt_port: parseInt(document.getElementById('mqttPort').value) || 1883,
                    mqtt_username: document.getElementById('mqttUsername').value.trim(),
                    mqtt_password: document.getElementById('mqttPassword').value
                };

                if (payload.use_static_ip) {
                    payload.ip = ReceiverNetworkFormController.collectOctets('ip');
                    payload.gateway = ReceiverNetworkFormController.collectOctets('gw');
                    payload.subnet = ReceiverNetworkFormController.collectOctets('sub');
                    payload.dns_primary = ReceiverNetworkFormController.collectOctets('dns1_');
                    payload.dns_secondary = ReceiverNetworkFormController.collectOctets('dns2_');
                }

                const response = await fetch('/api/save_receiver_network', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(payload)
                });

                const result = await response.json();
                if (result.success) {
                    alert('Receiver configuration saved. The device will reboot if needed.');
                    updateSaveButtonText(0);
                } else {
                    alert(result.message || 'Failed to save configuration');
                }
            } catch (err) {
                console.error('Failed to save receiver config:', err);
                alert('Failed to save configuration. Please try again.');
            }

            btn.disabled = false;
            btn.textContent = originalText;
            btn.style.backgroundColor = '#4CAF50';
        }
    )rawliteral";
}
