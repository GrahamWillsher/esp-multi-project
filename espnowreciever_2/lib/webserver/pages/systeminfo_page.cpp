#include "systeminfo_page.h"
#include "../common/nav_buttons.h"
#include "../common/page_generator.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ESP.h>

// System info page handler - System information page
esp_err_t systeminfo_handler(httpd_req_t *req) {
    String content = R"rawliteral(
    <h1>ESP-NOW Receiver</h1>
    <h2>Receiver Configuration (LilyGo T-Display-S3)</h2>
    )rawliteral";
    
    // Add navigation buttons from central registry
    content += "    " + generate_nav_buttons("/receiver/config");
    
    content += R"rawliteral(
    
    <div class='settings-card'>
        <h3>Device Details</h3>
        <div class='settings-row'>
            <label>Device:</label>
            <input type='text' value='ESP32 T-Display-S3' disabled class='readonly-field' />
        </div>
        <div class='settings-row'>
            <label>Chip Model:</label>
            <input type='text' id='chipModel' value='Loading...' disabled class='readonly-field' />
        </div>
        <div class='settings-row'>
            <label>Chip Revision:</label>
            <input type='text' id='chipRevision' value='Loading...' disabled class='readonly-field' />
        </div>
        <div class='settings-row'>
            <label>WiFi MAC Address:</label>
            <input type='text' id='wifiMac' value='Loading...' disabled class='readonly-field' style='font-family: monospace;' />
        </div>
    </div>
    
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

    <div style='text-align: center; margin-top: 30px; margin-bottom: 30px;'>
        <button id='saveNetworkBtn' onclick='saveReceiverConfig()' style='padding: 12px 40px; font-size: 16px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;'>
            Save Receiver Configuration
        </button>
    </div>
)rawliteral";

    String script = R"rawliteral(
        const RECEIVER_CONFIG_FIELDS = [
            'hostname', 'ssid', 'password', 'useStaticIP',
            'ip0', 'ip1', 'ip2', 'ip3',
            'gw0', 'gw1', 'gw2', 'gw3',
            'sub0', 'sub1', 'sub2', 'sub3',
            'dns1_0', 'dns1_1', 'dns1_2', 'dns1_3',
            'dns2_0', 'dns2_1', 'dns2_2', 'dns2_3'
        ];

        let initialReceiverConfig = {};

        function setOctets(prefix, value) {
            const parts = (value || '').split('.');
            if (parts.length !== 4) {
                return;
            }
            document.getElementById(prefix + '0').value = parts[0];
            document.getElementById(prefix + '1').value = parts[1];
            document.getElementById(prefix + '2').value = parts[2];
            document.getElementById(prefix + '3').value = parts[3];
        }

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

        function updateNetworkBadge(useStatic) {
            const badge = document.getElementById('networkModeBadge');
            badge.textContent = useStatic ? 'Static IP' : 'DHCP';
            badge.className = useStatic ? 'network-mode-badge badge-static' : 'network-mode-badge badge-dhcp';

            const toggleRows = ['localIpRow', 'gatewayRow', 'subnetRow', 'dns1Row', 'dns2Row'];
            toggleRows.forEach(rowId => {
                const row = document.getElementById(rowId);
                if (row) {
                    row.style.display = useStatic ? 'grid' : 'none';
                    row.querySelectorAll('input').forEach(input => {
                        input.disabled = !useStatic;
                    });
                }
            });
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
                updateNetworkBadge(useStatic);

                setOctets('ip', data.static_ip);
                setOctets('gw', data.gateway);
                setOctets('sub', data.subnet);
                setOctets('dns1_', data.dns_primary);
                setOctets('dns2_', data.dns_secondary);

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
                                updateNetworkBadge(element.checked);
                            }
                        });
                    }
                });

                updateSaveButtonText(0);
            })
            .catch(err => {
                console.error('Failed to load receiver network info:', err);
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
                    use_static_ip: document.getElementById('useStaticIP').checked
                };

                if (payload.use_static_ip) {
                    payload.ip = `${document.getElementById('ip0').value}.${document.getElementById('ip1').value}.${document.getElementById('ip2').value}.${document.getElementById('ip3').value}`;
                    payload.gateway = `${document.getElementById('gw0').value}.${document.getElementById('gw1').value}.${document.getElementById('gw2').value}.${document.getElementById('gw3').value}`;
                    payload.subnet = `${document.getElementById('sub0').value}.${document.getElementById('sub1').value}.${document.getElementById('sub2').value}.${document.getElementById('sub3').value}`;
                    payload.dns_primary = `${document.getElementById('dns1_0').value}.${document.getElementById('dns1_1').value}.${document.getElementById('dns1_2').value}.${document.getElementById('dns1_3').value}`;
                    payload.dns_secondary = `${document.getElementById('dns2_0').value}.${document.getElementById('dns2_1').value}.${document.getElementById('dns2_2').value}.${document.getElementById('dns2_3').value}`;
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

    String html = generatePage("ESP-NOW Receiver Config", content, "", script);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

/**
 * @brief Register the systeminfo page handler with the HTTP server
 */
esp_err_t register_systeminfo_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/receiver/config",
        .method    = HTTP_GET,
        .handler   = systeminfo_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
