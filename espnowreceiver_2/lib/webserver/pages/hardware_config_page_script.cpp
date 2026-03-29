#include "hardware_config_page_script.h"

String get_hardware_config_page_script() {
    return R"rawliteral(
        const FIELD_MAP = {
            canFdAsClassic: { category: 7, field: 4, type: 'checkbox' }, // SETTINGS_CAN / CAN_USE_CANFD_AS_CLASSIC
            canFreq:     { category: 7, field: 0, type: 'number' }, // SETTINGS_CAN / CAN_FREQUENCY_KHZ
            canFdFreq:   { category: 7, field: 1, type: 'number' }, // SETTINGS_CAN / CAN_FD_FREQUENCY_MHZ
            eqStop:      { category: 6, field: 4, type: 'number' }, // SETTINGS_POWER / POWER_EQUIPMENT_STOP_TYPE
            cntCtrl:     { category: 8, field: 0, type: 'checkbox' }, // SETTINGS_CONTACTOR / CONTACTOR_CONTROL_ENABLED
            prechgMs:    { category: 6, field: 3, type: 'number' }, // SETTINGS_POWER / POWER_PRECHARGE_DURATION_MS
            ncContactor: { category: 8, field: 1, type: 'checkbox' }, // SETTINGS_CONTACTOR / CONTACTOR_NC_MODE
            pwmCntCtrl:  { category: 8, field: 3, type: 'checkbox' }, // SETTINGS_CONTACTOR / CONTACTOR_PWM_ENABLED
            pwmFreqHz:   { category: 8, field: 2, type: 'number' }, // SETTINGS_CONTACTOR / CONTACTOR_PWM_FREQUENCY_HZ
            pwmHoldDuty: { category: 8, field: 4, type: 'number' }, // SETTINGS_CONTACTOR / CONTACTOR_PWM_HOLD_DUTY
            perBmsReset: { category: 8, field: 5, type: 'checkbox' }, // SETTINGS_CONTACTOR / CONTACTOR_PERIODIC_BMS_RESET
            bmsFirstAlignTime: { category: 8, field: 7, type: 'time' }, // SETTINGS_CONTACTOR / CONTACTOR_BMS_FIRST_ALIGN_TARGET_MINUTES
            extPrecharge:{ category: 6, field: 5, type: 'checkbox' }, // SETTINGS_POWER / POWER_EXTERNAL_PRECHARGE_ENABLED
            noInvDisc:   { category: 6, field: 6, type: 'checkbox' }, // SETTINGS_POWER / POWER_NO_INVERTER_DISCONNECT_CONTACTOR
            maxPreTime:  { category: 6, field: 2, type: 'number' }, // SETTINGS_POWER / POWER_MAX_PRECHARGE_MS
            ledMode:     { category: 0, field: 15, type: 'number' } // SETTINGS_BATTERY / BATTERY_LED_MODE
        };

        const READ_ONLY_IDS = [];
        const CONDITIONAL_ROWS = {
            cntctrl:    ['prechgMs', 'ncContactor', 'pwmCntCtrl'],
            pwmcntctrl: ['pwmFreqHz', 'pwmHoldDuty'],
            extprecharge: ['maxPreTime', 'noInvDisc'],
            periodicbms: ['bmsFirstAlignTime']
        };

        let initialState = {};

        function getFieldValue(id) {
            const el = document.getElementById(id);
            if (!el) return null;
            if (el.type === 'checkbox') return !!el.checked;
            return el.value;
        }

        function setFieldValue(id, value) {
            const el = document.getElementById(id);
            if (!el) return;
            if (el.type === 'checkbox') {
                el.checked = !!value;
            } else {
                el.value = value;
            }
        }

        function minutesToTimeString(totalMinutes) {
            const minutes = Number.isInteger(totalMinutes) ? totalMinutes : 120;
            const bounded = Math.max(0, Math.min(1439, minutes));
            const hh = String(Math.floor(bounded / 60)).padStart(2, '0');
            const mm = String(bounded % 60).padStart(2, '0');
            return `${hh}:${mm}`;
        }

        function timeStringToMinutes(value) {
            if (typeof value !== 'string') return null;
            const match = value.match(/^(\d{2}):(\d{2})$/);
            if (!match) return null;
            const hh = parseInt(match[1], 10);
            const mm = parseInt(match[2], 10);
            if (Number.isNaN(hh) || Number.isNaN(mm) || hh < 0 || hh > 23 || mm < 0 || mm > 59) {
                return null;
            }
            return (hh * 60) + mm;
        }

        function countChanges() {
            let changed = 0;
            Object.keys(FIELD_MAP).forEach((id) => {
                if (String(initialState[id]) !== String(getFieldValue(id))) {
                    changed++;
                }
            });
            return changed;
        }

        function updateSaveButton() {
            const button = document.getElementById('saveHardwareBtn');
            const changedCount = countChanges();

            FormChangeTracker.updateSaveButton(button, changedCount, {
                nothingText: 'Nothing to Save',
                changedSingularTemplate: 'Save 1 Change',
                changedPluralTemplate: 'Save {count} Changes'
            });
        }

        function setRowVisibility(inputId, visible) {
            const input = document.getElementById(inputId);
            const label = input ? input.previousElementSibling : null;
            if (input) {
                input.style.display = visible ? '' : 'none';
                input.disabled = !visible || READ_ONLY_IDS.includes(inputId);
            }
            if (label && label.tagName === 'LABEL') {
                label.style.display = visible ? '' : 'none';
            }
        }

        function updateConditionalVisibility() {
            const cntEnabled = !!(document.getElementById('cntCtrl') && document.getElementById('cntCtrl').checked);
            CONDITIONAL_ROWS.cntctrl.forEach((id) => setRowVisibility(id, cntEnabled));

            const pwmEnabled = cntEnabled && !!(document.getElementById('pwmCntCtrl') && document.getElementById('pwmCntCtrl').checked);
            CONDITIONAL_ROWS.pwmcntctrl.forEach((id) => setRowVisibility(id, pwmEnabled));

            const extPrechargeEnabled = !!(document.getElementById('extPrecharge') && document.getElementById('extPrecharge').checked);
            CONDITIONAL_ROWS.extprecharge.forEach((id) => setRowVisibility(id, extPrechargeEnabled));

            const periodicEnabled = !!(document.getElementById('perBmsReset') && document.getElementById('perBmsReset').checked);
            CONDITIONAL_ROWS.periodicbms.forEach((id) => setRowVisibility(id, periodicEnabled));
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
            try {
                const response = await fetch('/api/get_battery_settings');
                const data = await response.json();

                if (!data.success) {
                    return;
                }

                setFieldValue('canFreq', Number.isInteger(data.can_frequency_khz) ? data.can_frequency_khz : 8);
                setFieldValue('canFdFreq', Number.isInteger(data.can_fd_frequency_mhz) ? data.can_fd_frequency_mhz : 40);
                setFieldValue('canFdAsClassic', !!data.use_canfd_as_classic);
                setFieldValue('eqStop', String(Number.isInteger(data.equipment_stop_type) ? data.equipment_stop_type : 0));
                setFieldValue('cntCtrl', !!data.contactor_control_enabled);
                setFieldValue('prechgMs', Number.isInteger(data.precharge_duration_ms) ? data.precharge_duration_ms : 100);
                setFieldValue('ncContactor', !!data.contactor_nc_mode);
                setFieldValue('pwmCntCtrl', !!data.contactor_pwm_enabled);
                setFieldValue('pwmFreqHz', Number.isInteger(data.contactor_pwm_frequency_hz) ? data.contactor_pwm_frequency_hz : 20000);
                setFieldValue('pwmHoldDuty', Number.isInteger(data.contactor_pwm_hold_duty) ? data.contactor_pwm_hold_duty : 250);
                setFieldValue('perBmsReset', !!data.periodic_bms_reset);
                setFieldValue('bmsFirstAlignTime', minutesToTimeString(Number.isInteger(data.bms_first_align_target_minutes) ? data.bms_first_align_target_minutes : 120));
                setFieldValue('extPrecharge', !!data.external_precharge_enabled);
                setFieldValue('noInvDisc', !!data.no_inverter_disconnect_contactor);
                setFieldValue('maxPreTime', Number.isInteger(data.max_precharge_ms) ? data.max_precharge_ms : 15000);

                const ledMode = Number.isInteger(data.led_mode) ? data.led_mode : 0;
                setFieldValue('ledMode', String(ledMode >= 0 && ledMode <= 2 ? ledMode : 0));

                Object.keys(FIELD_MAP).forEach((id) => {
                    initialState[id] = getFieldValue(id);
                });

                updateConditionalVisibility();
                updateSaveButton();
            } catch (error) {
                console.error('Failed to load hardware settings:', error);
            }
        }

        async function saveHardwareSettings() {
            const button = document.getElementById('saveHardwareBtn');

            const changedEntries = Object.keys(FIELD_MAP)
                .filter((id) => String(initialState[id]) !== String(getFieldValue(id)))
                .map((id) => ({ id, ...FIELD_MAP[id], value: getFieldValue(id) }));

            if (changedEntries.length === 0) {
                updateSaveButton();
                return;
            }

            SaveOperation.setButtonState(button, {
                text: 'Saving...',
                backgroundColor: '#ff9800',
                disabled: true,
                cursor: 'not-allowed'
            });

            try {
                for (const entry of changedEntries) {
                    let payloadValue;
                    if (entry.type === 'checkbox') {
                        payloadValue = entry.value ? 1 : 0;
                    } else if (entry.type === 'time') {
                        payloadValue = timeStringToMinutes(entry.value);
                        if (payloadValue === null) {
                            throw new Error('Invalid reset target time format. Expected HH:MM.');
                        }
                    } else {
                        payloadValue = parseInt(entry.value, 10);
                    }

                    const response = await fetch('/api/save_setting', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({
                            category: entry.category,
                            field: entry.field,
                            value: payloadValue
                        })
                    });

                    const data = await response.json();
                    if (!data.success) {
                        throw new Error(data.message || ('Failed to save field: ' + entry.id));
                    }
                }

                Object.keys(FIELD_MAP).forEach((id) => {
                    initialState[id] = getFieldValue(id);
                });

                SaveOperation.showSuccess(button, '✓ Saved!', () => {
                    updateSaveButton();
                }, 1200);
                setTimeout(loadLiveLedStatus, 400);
            } catch (error) {
                console.error('Failed to save hardware settings:', error);
                SaveOperation.showError(button, '✗ Save Failed', () => {
                    updateSaveButton();
                }, 2500);
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
            READ_ONLY_IDS.forEach((id) => {
                const el = document.getElementById(id);
                if (el) {
                    el.disabled = true;
                    el.style.opacity = '0.6';
                    el.style.cursor = 'not-allowed';
                }
            });

            Object.keys(FIELD_MAP).forEach((id) => {
                const el = document.getElementById(id);
                if (!el) return;
                const evt = (el.type === 'checkbox' || el.type === 'time') ? 'change' : 'input';
                el.addEventListener(evt, () => {
                    if (id === 'cntCtrl' || id === 'pwmCntCtrl' || id === 'perBmsReset') {
                        updateConditionalVisibility();
                    }
                    updateSaveButton();
                });
            });

            const extPrechargeEl = document.getElementById('extPrecharge');
            if (extPrechargeEl) {
                extPrechargeEl.addEventListener('change', updateConditionalVisibility);
            }

            loadHardwareSettings();
            loadLiveLedStatus();
            setInterval(loadLiveLedStatus, 2000);
        });
    )rawliteral";
}
