#include "hardware_config_page_script.h"

String get_hardware_config_page_script() {
    return R"rawliteral(
        let initialLedMode = '0';

        function updateSaveButton() {
            const button = document.getElementById('saveHardwareBtn');
            const statusEl = document.getElementById('saveStatus');
            const currentValue = document.getElementById('ledMode').value;
            const changedCount = (currentValue !== initialLedMode) ? 1 : 0;

            FormChangeTracker.updateSaveButton(button, changedCount, {
                nothingText: 'Nothing to Save',
                changedSingularTemplate: 'Save 1 Change',
                changedPluralTemplate: 'Save {count} Changes'
            });

            if (changedCount > 0) {
                if (statusEl.textContent === '&#10003; Hardware config saved') {
                    statusEl.textContent = '';
                    statusEl.style.color = '#888';
                }
            }
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
            const button = document.getElementById('saveHardwareBtn');
            const statusEl = document.getElementById('saveStatus');
            const ledMode = parseInt(document.getElementById('ledMode').value, 10);

            if (String(ledMode) === initialLedMode) {
                updateSaveButton();
                return;
            }

            button.textContent = 'Saving...';
            button.style.backgroundColor = '#ff9800';
            button.disabled = true;
            button.style.cursor = 'wait';
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
                    statusEl.textContent = '&#10003; Hardware config saved';
                    statusEl.style.color = '#4CAF50';
                    setTimeout(loadLiveLedStatus, 400);
                } else {
                    button.textContent = '&#10007; Save Failed';
                    button.style.backgroundColor = '#dc3545';
                    statusEl.textContent = '&#10007; ' + (data.message || 'Failed to save hardware config');
                    statusEl.style.color = '#f44336';
                    setTimeout(updateSaveButton, 3000);
                }
            } catch (error) {
                console.error('Failed to save hardware settings:', error);
                button.textContent = '&#10007; Network Error';
                button.style.backgroundColor = '#dc3545';
                statusEl.textContent = '&#10007; Failed to save hardware config';
                statusEl.style.color = '#f44336';
                setTimeout(updateSaveButton, 3000);
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
                    statusEl.textContent = '&#10003; LED resync requested';
                    statusEl.style.color = '#4CAF50';
                    setTimeout(loadLiveLedStatus, 500);
                } else {
                    statusEl.textContent = '&#10007; ' + (data.message || 'Resync failed');
                    statusEl.style.color = '#f44336';
                }
            } catch (error) {
                console.error('Failed to request LED resync:', error);
                statusEl.textContent = '&#10007; Resync request failed';
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
    )rawliteral";
}
