#include "settings_page.h"
#include "../common/page_generator.h"
#include <Arduino.h>

static esp_err_t root_handler(httpd_req_t *req) {
    String content = R"rawliteral(
    <h1>Transmitter Configuration</h1>
    )rawliteral";

    content += R"rawliteral(
    <div style='margin-bottom: 20px;'>
        <a href='/' style='display: inline-block; padding: 10px 16px; background: #4CAF50; color: white; text-decoration: none; border-radius: 6px; font-weight: bold;'>
            ← Dashboard
        </a>
    </div>
    )rawliteral";

    content += R"rawliteral(
    <div class='settings-card'>
        <h3>Transmitter Details</h3>
        <div class='settings-row'><label>Status:</label><input type='text' id='txStatus' value='Loading...' disabled class='readonly-field' /></div>
        <div class='settings-row'><label>Environment:</label><input type='text' id='txEnvironment' value='Loading...' disabled class='readonly-field' /></div>
        <div class='settings-row'><label>Device:</label><input type='text' id='txDevice' value='Loading...' disabled class='readonly-field' /></div>
        <div class='settings-row'><label>Firmware Version:</label><input type='text' id='txVersion' value='Loading...' disabled class='readonly-field' /></div>
        <div class='settings-row'><label>Build Date:</label><input type='text' id='txBuildDate' value='Loading...' disabled class='readonly-field' /></div>
    </div>

    <div class='settings-card'>
        <h3>IP Configuration <span id='networkModeBadge' class='network-mode-badge badge-dhcp'>Loading...</span></h3>
        <div class='settings-row'>
            <label>Use Static IP:</label>
            <input type='checkbox' id='txUseStaticIP' />
        </div>

        <div class='settings-row' id='staticIpRow'>
            <label>Static IP:</label>
            <div class='ip-row'>
                <input class='octet editable-field' id='staticIp0' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='staticIp1' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='staticIp2' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='staticIp3' type='text' maxlength='3' />
            </div>
        </div>
        <div class='settings-row' id='staticGwRow'>
            <label>Static Gateway:</label>
            <div class='ip-row'>
                <input class='octet editable-field' id='staticGw0' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='staticGw1' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='staticGw2' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='staticGw3' type='text' maxlength='3' />
            </div>
        </div>
        <div class='settings-row' id='staticSubRow'>
            <label>Static Subnet:</label>
            <div class='ip-row'>
                <input class='octet editable-field' id='staticSub0' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='staticSub1' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='staticSub2' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='staticSub3' type='text' maxlength='3' />
            </div>
        </div>
        <div class='settings-row' id='dns1Row'>
            <label>Primary DNS:</label>
            <div class='ip-row'>
                <input class='octet editable-field' id='dns1_0' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='dns1_1' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='dns1_2' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='dns1_3' type='text' maxlength='3' />
            </div>
        </div>
        <div class='settings-row' id='dns2Row'>
            <label>Secondary DNS:</label>
            <div class='ip-row'>
                <input class='octet editable-field' id='dns2_0' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='dns2_1' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='dns2_2' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='dns2_3' type='text' maxlength='3' />
            </div>
        </div>
    </div>

    <div class='settings-card'>
        <h3>MQTT Settings <span id='mqttStatusDot' class='status-dot' style='display: inline-block;' title='MQTT Status'></span></h3>
        <div class='settings-row'><label>Enabled:</label><input type='checkbox' id='mqttEnabled' /></div>
        <div class='settings-row' id='mqttServerRow'>
            <label>Server:</label>
            <div class='ip-row'>
                <input class='octet editable-field' id='mqtt0' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='mqtt1' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='mqtt2' type='text' maxlength='3' />
                <span class='dot'>.</span>
                <input class='octet editable-field' id='mqtt3' type='text' maxlength='3' />
            </div>
        </div>
        <div class='settings-row' id='mqttPortRow'><label>Port:</label><input type='number' id='mqttPort' min='1' max='65535' value='1883' class='editable-field' /></div>
        <div class='settings-row' id='mqttUsernameRow'><label>Username:</label><input type='text' id='mqttUsername' value='' class='editable-field' /></div>
        <div class='settings-row' id='mqttPasswordRow'><label>Password:</label><input type='password' id='mqttPassword' value='' class='editable-field' /></div>
        <div class='settings-row' id='mqttClientIdRow'><label>Client ID:</label><input type='text' id='mqttClientId' value='' class='editable-field' /></div>
    </div>

    <div style='text-align: center; margin-top: 30px; margin-bottom: 30px;'>
        <button id='saveNetworkBtn' onclick='saveTransmitterConfig()' style='padding: 12px 40px; font-size: 16px; background-color: #6c757d; color: white; border: none; border-radius: 4px; cursor: pointer;' disabled>Nothing to Save</button>
    </div>
    )rawliteral";

    String script = R"rawliteral(
        const TX_CONFIG_FIELDS = [
            'txUseStaticIP',
            'staticIp0', 'staticIp1', 'staticIp2', 'staticIp3',
            'staticGw0', 'staticGw1', 'staticGw2', 'staticGw3',
            'staticSub0', 'staticSub1', 'staticSub2', 'staticSub3',
            'dns1_0', 'dns1_1', 'dns1_2', 'dns1_3',
            'dns2_0', 'dns2_1', 'dns2_2', 'dns2_3',
            'mqttEnabled', 'mqtt0', 'mqtt1', 'mqtt2', 'mqtt3',
            'mqttPort', 'mqttUsername', 'mqttPassword', 'mqttClientId'
        ];

        let initialTxConfig = {};

        function setField(id, value) {
            const el = document.getElementById(id);
            if (!el) return;
            el.value = (value === undefined || value === null || value === '') ? 'N/A' : value;
        }

        function setOctets(prefix, value) {
            const parts = String(value || '').split('.');
            for (let i = 0; i < 4; i++) {
                const el = document.getElementById(prefix + i);
                if (!el) continue;
                el.value = parts.length === 4 ? parts[i] : '--';
            }
        }

        function setOctetsError(prefix, text) {
            const first = document.getElementById(prefix + '0');
            if (first) first.value = text;
            for (let i = 1; i < 4; i++) {
                const el = document.getElementById(prefix + i);
                if (el) el.value = '--';
            }
        }

        function collectOctets(prefix) {
            return `${document.getElementById(prefix + '0').value}.${document.getElementById(prefix + '1').value}.${document.getElementById(prefix + '2').value}.${document.getElementById(prefix + '3').value}`;
        }

        function updateSaveButtonText(changedCount) {
            const saveButton = document.getElementById('saveNetworkBtn');
            if (!saveButton) return;

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

        function storeInitialConfig() {
            TX_CONFIG_FIELDS.forEach(fieldId => {
                const el = document.getElementById(fieldId);
                if (el) {
                    initialTxConfig[fieldId] = el.type === 'checkbox' ? el.checked : el.value;
                }
            });
        }

        function countConfigChanges() {
            let changes = 0;
            TX_CONFIG_FIELDS.forEach(fieldId => {
                const el = document.getElementById(fieldId);
                if (!el) return;
                const currentValue = el.type === 'checkbox' ? el.checked : el.value;
                if (initialTxConfig[fieldId] !== currentValue) {
                    changes++;
                }
            });
            return changes;
        }

        function updateNetworkModeUI(useStatic) {
            const badge = document.getElementById('networkModeBadge');
            if (badge) {
                badge.textContent = useStatic ? 'Static IP' : 'DHCP';
                badge.className = useStatic ? 'network-mode-badge badge-static' : 'network-mode-badge badge-dhcp';
            }

            const toggleRows = ['staticIpRow', 'staticGwRow', 'staticSubRow', 'dns1Row', 'dns2Row'];
            toggleRows.forEach(rowId => {
                const row = document.getElementById(rowId);
                if (!row) return;
                row.style.display = useStatic ? 'grid' : 'none';
                row.querySelectorAll('input').forEach(input => {
                    input.disabled = !useStatic;
                });
            });
        }

        function updateMqttVisibility() {
            const mqttEnabledEl = document.getElementById('mqttEnabled');
            const enabled = !!(mqttEnabledEl && mqttEnabledEl.checked);
            const rows = ['mqttServerRow', 'mqttPortRow', 'mqttUsernameRow', 'mqttPasswordRow', 'mqttClientIdRow'];
            rows.forEach(id => {
                const row = document.getElementById(id);
                if (!row) return;
                row.style.display = enabled ? 'grid' : 'none';
                row.querySelectorAll('input').forEach(input => {
                    input.disabled = !enabled;
                });
            });
        }

        function attachChangeListeners() {
            TX_CONFIG_FIELDS.forEach(fieldId => {
                const el = document.getElementById(fieldId);
                if (!el) return;
                const eventType = el.type === 'checkbox' ? 'change' : 'input';
                el.addEventListener(eventType, () => {
                    if (fieldId === 'txUseStaticIP') {
                        updateNetworkModeUI(el.checked);
                    }
                    if (fieldId === 'mqttEnabled') {
                        updateMqttVisibility();
                    }
                    updateSaveButtonText(countConfigChanges());
                });
            });
        }

        function formatEnv(env) {
            if (!env) return 'N/A';
            const spaced = String(env).replace(/[-_]+/g, ' ').trim();
            return spaced.replace(/\b\w/g, c => c.toUpperCase());
        }

        function updateMqttIndicator(connected) {
            const dot = document.getElementById('mqttStatusDot');
            if (!dot) return;
            dot.className = 'status-dot ' + (connected ? 'connected' : 'disconnected');
            dot.title = connected ? 'MQTT Connected' : 'MQTT Disconnected';
        }

        async function loadTransmitterMetadata() {
            try {
                const res = await fetch('/api/transmitter_metadata');
                const data = await res.json();

                if (data.status === 'received') {
                    setField('txStatus', data.valid ? 'Connected' : 'Metadata received (invalid)');
                    setField('txEnvironment', formatEnv(data.env));
                    setField('txDevice', data.device || 'N/A');
                    setField('txVersion', data.version || 'N/A');
                    setField('txBuildDate', data.build_date || 'N/A');
                    return true;
                }

                setField('txStatus', 'Waiting for transmitter metadata');
                setField('txEnvironment', 'N/A');
                setField('txDevice', 'N/A');
                setField('txVersion', 'N/A');
                setField('txBuildDate', 'N/A');
            } catch (e) {
                console.error('Failed to load transmitter metadata:', e);
            }

            try {
                const fallback = await fetch('/api/version');
                const v = await fallback.json();
                setField('txStatus', 'Connected (version fallback)');
                setField('txEnvironment', 'N/A');
                setField('txDevice', 'Transmitter');
                setField('txVersion', v.transmitter_version || 'Unknown');
                setField('txBuildDate', v.transmitter_build_date || 'Unknown');
                return true;
            } catch (e2) {
                console.error('Failed to load transmitter version fallback:', e2);
                setField('txStatus', 'Disconnected / unavailable');
                return false;
            }
        }

        async function loadNetworkConfig() {
            try {
                const res = await fetch('/api/get_network_config');
                const data = await res.json();
                const staticFlagEl = document.getElementById('txUseStaticIP');

                if (!data.success) {
                    if (staticFlagEl) {
                        staticFlagEl.checked = false;
                        updateNetworkModeUI(false);
                    }
                    setOctetsError('staticIp', 'N/A');
                    setOctetsError('staticGw', 'N/A');
                    setOctetsError('staticSub', 'N/A');
                    setOctetsError('dns1_', 'N/A');
                    setOctetsError('dns2_', 'N/A');
                    return false;
                }

                if (staticFlagEl) {
                    staticFlagEl.checked = !!data.use_static_ip;
                    updateNetworkModeUI(!!data.use_static_ip);
                }
                setOctets('staticIp', data.static_config && data.static_config.ip);
                setOctets('staticGw', data.static_config && data.static_config.gateway);
                setOctets('staticSub', data.static_config && data.static_config.subnet);
                setOctets('dns1_', data.static_config && data.static_config.dns_primary);
                setOctets('dns2_', data.static_config && data.static_config.dns_secondary);
                return true;
            } catch (e) {
                console.error('Failed to load transmitter network config:', e);
                return false;
            }
        }

        async function loadMqttConfig() {
            try {
                const res = await fetch('/api/get_mqtt_config');
                const data = await res.json();
                const mqttEnabledEl = document.getElementById('mqttEnabled');

                if (!data.success) {
                    if (mqttEnabledEl) mqttEnabledEl.checked = false;
                    setOctetsError('mqtt', 'N/A');
                    setField('mqttPort', '1883');
                    setField('mqttUsername', '');
                    setField('mqttPassword', '');
                    setField('mqttClientId', '');
                    updateMqttVisibility();
                    updateMqttIndicator(false);
                    return false;
                }

                if (mqttEnabledEl) mqttEnabledEl.checked = !!data.enabled;
                setOctets('mqtt', data.server);
                setField('mqttPort', data.port);
                setField('mqttUsername', data.username);
                setField('mqttPassword', data.password);
                setField('mqttClientId', data.client_id);
                updateMqttVisibility();
                updateMqttIndicator(!!data.connected);
                return true;
            } catch (e) {
                console.error('Failed to load transmitter MQTT config:', e);
                setOctetsError('mqtt', 'Err');
                setField('mqttPort', 'Error');
                setField('mqttUsername', 'Error');
                setField('mqttPassword', 'Error');
                setField('mqttClientId', 'Error');
                updateMqttVisibility();
                updateMqttIndicator(false);
                return false;
            }
        }

        async function loadAll() {
            const [metaOk, netOk, mqttOk] = await Promise.all([
                loadTransmitterMetadata(),
                loadNetworkConfig(),
                loadMqttConfig()
            ]);
            return (metaOk || netOk || mqttOk);
        }

        async function saveTransmitterConfig() {
            const btn = document.getElementById('saveNetworkBtn');
            const originalText = btn ? btn.textContent : 'Save';
            if (btn) {
                btn.disabled = true;
                btn.textContent = 'Saving...';
                btn.style.backgroundColor = '#FF9800';
            }

            try {
                const useStatic = document.getElementById('txUseStaticIP').checked;
                const networkPayload = {
                    use_static_ip: useStatic,
                    ip: useStatic ? collectOctets('staticIp') : '0.0.0.0',
                    gateway: useStatic ? collectOctets('staticGw') : '0.0.0.0',
                    subnet: useStatic ? collectOctets('staticSub') : '0.0.0.0',
                    dns_primary: useStatic ? collectOctets('dns1_') : '8.8.8.8',
                    dns_secondary: useStatic ? collectOctets('dns2_') : '8.8.4.4'
                };

                const mqttPayload = {
                    enabled: document.getElementById('mqttEnabled').checked,
                    server: collectOctets('mqtt'),
                    port: parseInt(document.getElementById('mqttPort').value || '1883', 10),
                    username: document.getElementById('mqttUsername').value || '',
                    password: document.getElementById('mqttPassword').value || '',
                    client_id: document.getElementById('mqttClientId').value || ''
                };

                const [networkRes, mqttRes] = await Promise.all([
                    fetch('/api/save_network_config', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify(networkPayload)
                    }),
                    fetch('/api/save_mqtt_config', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify(mqttPayload)
                    })
                ]);

                const networkData = await networkRes.json();
                const mqttData = await mqttRes.json();
                if (networkData.success && mqttData.success) {
                    await loadAll();
                    storeInitialConfig();
                    updateSaveButtonText(0);
                    if (btn) {
                        btn.textContent = '✓ Saved!';
                        btn.style.backgroundColor = '#28a745';
                    }
                    setTimeout(() => updateSaveButtonText(0), 2000);
                } else {
                    if (btn) {
                        btn.textContent = '✗ Save Failed';
                        btn.style.backgroundColor = '#dc3545';
                    }
                    setTimeout(() => {
                        if (btn) {
                            btn.textContent = originalText;
                        }
                        updateSaveButtonText(countConfigChanges());
                    }, 2500);
                }
            } catch (e) {
                console.error('Failed to save transmitter config:', e);
                if (btn) {
                    btn.textContent = '✗ Network Error';
                    btn.style.backgroundColor = '#dc3545';
                }
                setTimeout(() => {
                    if (btn) {
                        btn.textContent = originalText;
                    }
                    updateSaveButtonText(countConfigChanges());
                }, 2500);
            }
        }

        window.addEventListener('DOMContentLoaded', () => {
            updateNetworkModeUI(false);
            updateMqttVisibility();
            attachChangeListeners();

            loadAll().then(() => {
                storeInitialConfig();
                updateSaveButtonText(0);
            });
        });
    )rawliteral";

    return send_rendered_page(req,
                              "ESP-NOW Receiver - Transmitter Config",
                              content,
                              PageRenderOptions("", script),
                              "text/html");
}

esp_err_t register_settings_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/transmitter/config",
        .method    = HTTP_GET,
        .handler   = root_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
