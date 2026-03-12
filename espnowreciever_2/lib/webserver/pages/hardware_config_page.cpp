#include "hardware_config_page.h"
#include "../common/page_generator.h"
#include "../common/nav_buttons.h"
#include <Arduino.h>

/**
 * @brief Handler for the transmitter hardware config page
 *
 * Currently hosts hardware-adjacent transmitter controls that are synchronized
 * from the receiver cache and saved back via ESP-NOW settings updates.
 */
static esp_err_t hardware_config_handler(httpd_req_t *req) {
    String content = R"rawliteral(
    <!-- Breadcrumb -->
    <div style='margin-bottom: 20px; padding: 10px; background: rgba(0,0,0,0.2); border-radius: 5px; font-size: 14px;'>
        <a href='/' style='color: #888; text-decoration: none;'>Dashboard</a>
        <span style='color: #888; margin: 0 8px;'>&gt;</span>
        <a href='/transmitter' style='color: #888; text-decoration: none;'>Transmitter</a>
        <span style='color: #888; margin: 0 8px;'>&gt;</span>
        <span style='color: #2196F3;'>Hardware Config</span>
    </div>

    <h1>Hardware Config</h1>
    )rawliteral";

    content += "    " + generate_nav_buttons("/transmitter/hardware");

    content += R"rawliteral(

    <div class='settings-card'>
        <h3>Status LED Pattern</h3>
        <p style='color: #888; font-size: 13px; margin-top: 0;'>Choose the receiver-side simulated LED effect policy synchronized from the transmitter.</p>

        <div class='settings-row'>
            <label for='ledMode'>Status LED Pattern:</label>
            <select id='ledMode' class='editable-field'>
                <option value='0'>Classic</option>
                <option value='1'>Energy Flow</option>
                <option value='2'>Heartbeat</option>
            </select>
        </div>
    </div>

    <div class='settings-card'>
        <h3>Live LED Runtime Status</h3>
        <div class='settings-row'>
            <label>Current Color:</label>
            <span id='liveLedColor' style='font-weight: bold;'>--</span>
        </div>
        <div class='settings-row'>
            <label>Current Effect:</label>
            <span id='liveLedEffect' style='font-weight: bold;'>--</span>
        </div>
        <div class='settings-row'>
            <label>Expected Effect:</label>
            <span id='expectedLedEffect' style='font-weight: bold;'>--</span>
        </div>
        <div class='settings-row'>
            <label>Sync Status:</label>
            <span id='ledSyncStatus' style='font-weight: bold;'>--</span>
        </div>
        <div class='settings-row'>
            <label>Manual Resync:</label>
            <button id='resyncLedBtn' onclick='resyncLedState()' style='padding: 8px 16px; font-size: 13px; background-color: #2196F3; color: white; border: none; border-radius: 4px; cursor: pointer;'>
                Resync LED Now
            </button>
        </div>
        <div id='resyncStatus' style='color: #888; font-size: 12px; margin-top: 8px; min-height: 18px;'></div>
    </div>

    <div style='text-align: center; margin-top: 30px;'>
        <button id='saveHardwareBtn' onclick='saveHardwareSettings()' style='padding: 12px 40px; font-size: 16px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;'>
            Save Hardware Config
        </button>
        <div id='saveStatus' style='color: #888; font-size: 12px; margin-top: 12px; min-height: 18px;'></div>
    </div>

    <script>
        let initialLedMode = '0';

        function updateSaveButton() {
            const button = document.getElementById('saveHardwareBtn');
            const currentValue = document.getElementById('ledMode').value;
            const changed = currentValue !== initialLedMode;
            button.textContent = changed ? 'Save Hardware Config (1 change)' : 'Save Hardware Config';
        }

        async function loadLiveLedStatus() {
            try {
                const response = await fetch('/api/get_led_runtime_status');
                const data = await response.json();

                if (!data.success) {
                    return;
                }

                document.getElementById('liveLedColor').textContent = data.current_color_name || '--';
                document.getElementById('liveLedEffect').textContent = data.current_effect_name || '--';
                document.getElementById('expectedLedEffect').textContent = data.expected_effect_name || '--';

                const syncEl = document.getElementById('ledSyncStatus');
                if (data.effect_synced) {
                    syncEl.textContent = 'Synced';
                    syncEl.style.color = '#4CAF50';
                } else {
                    syncEl.textContent = data.has_led_policy ? 'Sync Pending' : 'No LED policy cached';
                    syncEl.style.color = '#FF9800';
                }
            } catch (error) {
                console.error('Failed to load live LED status:', error);
            }
        }

        async function loadHardwareSettings() {
            const statusEl = document.getElementById('saveStatus');

            try {
                const response = await fetch('/api/get_battery_settings');
                const data = await response.json();

                if (!data.success) {
                    statusEl.textContent = data.requested ? 'Waiting for transmitter battery settings...' : 'Battery settings not available yet';
                    statusEl.style.color = '#FF9800';
                    return;
                }

                const ledMode = Number.isInteger(data.led_mode) ? data.led_mode : 0;
                document.getElementById('ledMode').value = String(ledMode >= 0 && ledMode <= 2 ? ledMode : 0);
                initialLedMode = document.getElementById('ledMode').value;
                updateSaveButton();
                statusEl.textContent = '';
            } catch (error) {
                console.error('Failed to load hardware settings:', error);
                statusEl.textContent = 'Failed to load hardware settings';
                statusEl.style.color = '#f44336';
            }
        }

        async function saveHardwareSettings() {
            const statusEl = document.getElementById('saveStatus');
            const ledMode = parseInt(document.getElementById('ledMode').value, 10);

            statusEl.textContent = 'Saving...';
            statusEl.style.color = '#888';

            try {
                const response = await fetch('/api/save_setting', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        category: 0,
                        field: 15,
                        value: ledMode
                    })
                });

                const data = await response.json();
                if (data.success) {
                    initialLedMode = String(ledMode);
                    updateSaveButton();
                    statusEl.textContent = '✓ Hardware config saved';
                    statusEl.style.color = '#4CAF50';
                    setTimeout(loadLiveLedStatus, 400);
                } else {
                    statusEl.textContent = '✗ ' + (data.message || 'Failed to save hardware config');
                    statusEl.style.color = '#f44336';
                }
            } catch (error) {
                console.error('Failed to save hardware settings:', error);
                statusEl.textContent = '✗ Failed to save hardware config';
                statusEl.style.color = '#f44336';
            }
        }

        async function resyncLedState() {
            const statusEl = document.getElementById('resyncStatus');
            const btn = document.getElementById('resyncLedBtn');

            btn.disabled = true;
            statusEl.textContent = 'Requesting LED resync...';
            statusEl.style.color = '#888';

            try {
                const response = await fetch('/api/resync_led_state', { method: 'POST' });
                const data = await response.json();

                if (data.success) {
                    statusEl.textContent = '✓ LED resync requested';
                    statusEl.style.color = '#4CAF50';
                    setTimeout(loadLiveLedStatus, 500);
                } else {
                    statusEl.textContent = '✗ ' + (data.message || 'Resync failed');
                    statusEl.style.color = '#f44336';
                }
            } catch (error) {
                console.error('Failed to request LED resync:', error);
                statusEl.textContent = '✗ Resync request failed';
                statusEl.style.color = '#f44336';
            } finally {
                btn.disabled = false;
            }
        }

        document.addEventListener('DOMContentLoaded', function() {
            document.getElementById('ledMode').addEventListener('change', updateSaveButton);
            loadHardwareSettings();
            loadLiveLedStatus();
            setInterval(loadLiveLedStatus, 2000);
        });
    </script>
    )rawliteral";

    String page = generatePage("Hardware Config", content, "/transmitter/hardware");
    return httpd_resp_send(req, page.c_str(), page.length());
}

esp_err_t register_hardware_config_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/transmitter/hardware",
        .method    = HTTP_GET,
        .handler   = hardware_config_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}