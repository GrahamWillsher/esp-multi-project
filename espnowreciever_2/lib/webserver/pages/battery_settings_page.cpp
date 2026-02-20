#include "battery_settings_page.h"
#include "../common/page_generator.h"
#include "../common/nav_buttons.h"
#include "../utils/transmitter_manager.h"
#include <Arduino.h>

/**
 * @brief Handler for the /battery_settings page
 * 
 * Phase 2: Bidirectional settings page with Save buttons
 * Users can view and modify battery settings that are sent to the transmitter
 */
static esp_err_t battery_settings_handler(httpd_req_t *req) {
    String content = R"rawliteral(
    <h1>Battery Settings</h1>
    )rawliteral";
    
    // Add navigation buttons
    content += "    " + generate_nav_buttons("/battery_settings");
    
    content += R"rawliteral(
    
    <div class='settings-card'>
        <h3>Battery Capacity & Limits</h3>
        
        <div class='settings-row'>
            <label for='batteryCapacity'>Battery Capacity (Wh):</label>
            <input type='number' id='batteryCapacity' min='1000' max='1000000' />
        </div>
        
        <div class='settings-row'>
            <label for='maxVoltage'>Max Voltage (mV):</label>
            <input type='number' id='maxVoltage' min='30000' max='100000' />
        </div>
        
        <div class='settings-row'>
            <label for='minVoltage'>Min Voltage (mV):</label>
            <input type='number' id='minVoltage' min='20000' max='80000' />
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>Current Limits</h3>
        
        <div class='settings-row'>
            <label for='maxChargeCurrent'>Max Charge Current (A):</label>
            <input type='number' id='maxChargeCurrent' min='0' max='500' step='1' />
        </div>
        
        <div class='settings-row'>
            <label for='maxDischargeCurrent'>Max Discharge Current (A):</label>
            <input type='number' id='maxDischargeCurrent' min='0' max='500' step='1' />
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>State of Charge (SOC) Limits</h3>
        
        <div class='settings-row'>
            <label for='socHighLimit'>SOC High Limit (%):</label>
            <input type='number' id='socHighLimit' min='50' max='100' step='1' />
        </div>
        
        <div class='settings-row'>
            <label for='socLowLimit'>SOC Low Limit (%):</label>
            <input type='number' id='socLowLimit' min='0' max='50' step='1' />
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>Cell Configuration</h3>
        
        <div class='settings-row'>
            <label for='cellCount'>Cell Count (detected from battery):</label>
            <input type='number' id='cellCount' readonly style='background-color: #f5f5f5; cursor: not-allowed;' />
        </div>
        
        <div class='settings-row'>
            <label for='chemistry'>Battery Chemistry:</label>
            <select id='chemistry'>
                <option value='0'>NCA (Nickel Cobalt Aluminum)</option>
                <option value='1'>NMC (Nickel Manganese Cobalt)</option>
                <option value='2'>LFP (Lithium Iron Phosphate)</option>
                <option value='3'>LTO (Lithium Titanate)</option>
            </select>
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>Battery Type Selection</h3>
        <p style='color: #666; font-size: 14px;'>Select the battery profile to use. The transmitter will switch to the selected profile.</p>
        <p style='color: #ff6b35; font-size: 14px; font-weight: bold;'>⚠️ Changing the battery or inverter type will reboot the transmitter to apply changes.</p>
        
        <div class='settings-row'>
            <label for='batteryType'>Battery Type:</label>
            <select id='batteryType' onchange='updateBatteryType()'>
                <option value=''>Loading...</option>
            </select>
        </div>
    </div>
    
    <div style='text-align: center; margin-top: 30px;'>
        <button id='saveButton' onclick='saveAllSettings()' style='padding: 12px 40px; font-size: 16px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;'>
            Save All Settings
        </button>
    </div>
)rawliteral";

    String script = R"rawliteral(
        // Store initial values to detect changes
        let initialValues = {};
        let batterySettingsRetries = 0;
        const MAX_BATTERY_SETTINGS_RETRIES = 5;
        
        window.onload = function() {
            // Load current settings from transmitter
            loadBatterySettings();
            // Phase 3.1: Load battery types for selector
            loadBatteryTypes();
        };
        
        // Map field names to BatterySettingsField enum values
        const BATTERY_FIELDS = {
            'CAPACITY_WH': 0,
            'MAX_VOLTAGE_MV': 1,
            'MIN_VOLTAGE_MV': 2,
            'MAX_CHARGE_CURRENT_A': 3,
            'MAX_DISCHARGE_CURRENT_A': 4,
            'SOC_HIGH_LIMIT': 5,
            'SOC_LOW_LIMIT': 6,
            'CELL_COUNT': 7,
            'CHEMISTRY': 8
        };
        
        // Fields that use float values (current settings)
        const FLOAT_FIELDS = ['MAX_CHARGE_CURRENT_A', 'MAX_DISCHARGE_CURRENT_A'];
        
        // Map input IDs to field names
        const FIELD_MAP = [
            { id: 'batteryCapacity', field: 'CAPACITY_WH' },
            { id: 'maxVoltage', field: 'MAX_VOLTAGE_MV' },
            { id: 'minVoltage', field: 'MIN_VOLTAGE_MV' },
            { id: 'maxChargeCurrent', field: 'MAX_CHARGE_CURRENT_A' },
            { id: 'maxDischargeCurrent', field: 'MAX_DISCHARGE_CURRENT_A' },
            { id: 'socHighLimit', field: 'SOC_HIGH_LIMIT' },
            { id: 'socLowLimit', field: 'SOC_LOW_LIMIT' },
            { id: 'chemistry', field: 'CHEMISTRY' }
        ];
        
        function storeInitialValues() {
            FIELD_MAP.forEach(mapping => {
                const element = document.getElementById(mapping.id);
                if (element) {
                    initialValues[mapping.id] = element.value;
                }
            });
            const typeSelect = document.getElementById('batteryType');
            if (typeSelect) {
                initialValues['batteryType'] = typeSelect.value;
            }
            console.log('Initial values stored:', initialValues);
        }
        
        function attachChangeListeners() {
            // Attach input event listeners to all fields
            FIELD_MAP.forEach(m => {
                const el = document.getElementById(m.id);
                if (el) {
                    el.addEventListener('input', () => {
                        updateButtonText(getChangedCount());
                    });
                }
            });
            const typeSelect = document.getElementById('batteryType');
            if (typeSelect) {
                typeSelect.addEventListener('change', () => {
                    updateButtonText(getChangedCount());
                });
            }
        }

        function getChangedCount() {
            let changedCount = FIELD_MAP.filter(fm => {
                const fEl = document.getElementById(fm.id);
                return fEl && initialValues[fm.id] !== fEl.value;
            }).length;

            const typeSelect = document.getElementById('batteryType');
            if (typeSelect && initialValues['batteryType'] !== typeSelect.value) {
                changedCount++;
            }

            return changedCount;
        }
        
        function loadBatterySettings() {
            console.log('Loading battery settings from transmitter...');
            
            fetch('/api/get_battery_settings')
                .then(response => response.json())
                .then(data => {
                    console.log('Received battery settings:', data);
                    
                    if (!data.success) {
                        if (batterySettingsRetries < MAX_BATTERY_SETTINGS_RETRIES) {
                            batterySettingsRetries++;
                            console.log('Battery settings not ready, retrying (' + batterySettingsRetries + ')...');
                            setTimeout(loadBatterySettings, 1000);
                        } else {
                            console.error('Battery settings not available after retries');
                        }
                        return;
                    }

                    if (data.success) {
                        // Update form fields with loaded values
                        document.getElementById('batteryCapacity').value = data.capacity_wh;
                        document.getElementById('maxVoltage').value = data.max_voltage_mv;
                        document.getElementById('minVoltage').value = data.min_voltage_mv;
                        document.getElementById('maxChargeCurrent').value = data.max_charge_current_a;
                        document.getElementById('maxDischargeCurrent').value = data.max_discharge_current_a;
                        document.getElementById('socHighLimit').value = data.soc_high_limit;
                        document.getElementById('socLowLimit').value = data.soc_low_limit;
                        document.getElementById('chemistry').value = data.chemistry;
                        
                        // Load cell count from battery specs (read-only, detected from battery)
                        loadCellCountFromSpecs();
                        
                        // Store these as initial values
                        storeInitialValues();
                        attachChangeListeners();
                        updateButtonText(getChangedCount());
                        
                        console.log('Battery settings loaded and populated');
                    } else {
                        console.error('Failed to load settings:', data.error);
                    }
                })
                .catch(error => {
                    console.error('Error loading battery settings:', error);
                    // Keep default values from HTML on error
                });
        }
        
        function loadCellCountFromSpecs() {
            // Fetch cell count from battery specs (detected from Battery Emulator)
            fetch('/api/battery_specs')
                .then(response => response.json())
                .then(data => {
                    const cellCountEl = document.getElementById('cellCount');
                    if (data && data.number_of_cells && data.number_of_cells > 0) {
                        cellCountEl.value = data.number_of_cells;
                        console.log('Cell count loaded from battery specs: ' + data.number_of_cells);
                    } else {
                        cellCountEl.value = '';
                        cellCountEl.placeholder = 'Not detected';
                        console.warn('Battery specs available but no cell count:', data);
                    }
                })
                .catch(error => {
                    console.warn('Could not load cell count from specs:', error);
                    const cellCountEl = document.getElementById('cellCount');
                    cellCountEl.value = '';
                    cellCountEl.placeholder = 'Not available';
                });
        }
        
        function saveAllSettings() {
            console.log('saveAllSettings() called');
            
            // Find only changed settings
            const changedSettings = [];
            FIELD_MAP.forEach(mapping => {
                const element = document.getElementById(mapping.id);
                if (element && initialValues[mapping.id] !== element.value) {
                    console.log('Changed: ' + mapping.field + ' from ' + initialValues[mapping.id] + ' to ' + element.value);
                    
                    changedSettings.push({
                        id: mapping.id,
                        field: mapping.field,
                        value: element.value,
                        oldValue: initialValues[mapping.id]
                    });
                }
            });
            const typeSelect = document.getElementById('batteryType');
            const batteryTypeChanged = typeSelect && initialValues['batteryType'] !== typeSelect.value;
            console.log('Total changed settings: ' + changedSettings.length + ', batteryTypeChanged=' + batteryTypeChanged);
            
            if (changedSettings.length === 0 && !batteryTypeChanged) {
                console.log('No changed settings to save');
                return;
            }
            
            // Show confirmation if battery type changed (requires transmitter reboot)
            if (batteryTypeChanged) {
                const selectedName = typeSelect.options[typeSelect.selectedIndex].text;
                const confirmed = confirm(
                    '⚠️ TRANSMITTER REBOOT REQUIRED\\n\\n' +
                    'Changing the battery type to "' + selectedName + '" will reboot the transmitter to apply changes.\\n\\n' +
                    'This will temporarily interrupt data transmission (approximately 30 seconds).\\n\\n' +
                    'Do you want to continue?'
                );
                
                if (!confirmed) {
                    console.log('Battery type change cancelled by user');
                    return;
                }
            }
            
            const saveButton = document.getElementById('saveButton');
            console.log('Changed settings:', changedSettings);
            
            let savedCount = 0;
            let failedCount = 0;

            const pendingSaves = [...changedSettings];
            if (batteryTypeChanged) {
                pendingSaves.push({
                    kind: 'batteryType',
                    id: 'batteryType',
                    value: typeSelect.value,
                    name: typeSelect.options[typeSelect.selectedIndex].text
                });
            }
            
            // Save each changed setting sequentially with a small delay
            function saveNext(index) {
                if (index >= pendingSaves.length) {
                    // All done - update initial values for successfully saved settings
                    if (failedCount === 0) {
                        changedSettings.forEach(s => {
                            initialValues[s.id] = s.value;
                        });
                        if (batteryTypeChanged) {
                            initialValues['batteryType'] = typeSelect.value;
                        }
                        saveButton.textContent = '✓ All Saved!';
                        saveButton.style.backgroundColor = '#28a745';
                        setTimeout(() => {
                            updateButtonText(getChangedCount());
                        }, 3000);
                    } else {
                        saveButton.textContent = '⚠ ' + failedCount + ' Failed';
                        saveButton.style.backgroundColor = '#dc3545';
                        setTimeout(() => {
                            const remainingCount = pendingSaves.length - savedCount;
                            updateButtonText(remainingCount);
                        }, 3000);
                    }
                    return;
                }
                
                // Update button text with current progress
                saveButton.textContent = 'Saving ' + (index + 1) + ' of ' + pendingSaves.length + '...';
                
                const setting = pendingSaves[index];
                if (setting.kind === 'batteryType') {
                    const typeId = parseInt(setting.value);
                    console.log('Sending to API: battery type=' + typeId + ' (' + setting.name + ')');

                    fetch('/api/set_battery_type', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify({type: typeId})
                    })
                        .then(response => response.json())
                        .then(data => {
                            if (data.success) {
                                savedCount++;
                                console.log('Battery type saved successfully');
                            } else {
                                failedCount++;
                                console.error('Failed to save battery type:', data.error);
                                alert('ERROR saving battery type: ' + data.error);
                            }
                            setTimeout(() => saveNext(index + 1), 100);
                        })
                        .catch(error => {
                            failedCount++;
                            console.error('Error saving battery type:', error);
                            alert('ERROR saving battery type: ' + error.message + '\n\nPlease check transmitter connection and try again.');
                            saveButton.textContent = '✗ Save Failed!';
                            saveButton.style.backgroundColor = '#dc3545';
                            setTimeout(() => {
                                const remainingCount = pendingSaves.length - index;
                                updateButtonText(remainingCount);
                            }, 3000);
                        });
                    return;
                }

                const fieldId = BATTERY_FIELDS[setting.field];
                
                // Determine if this field uses float or integer
                const isFloat = FLOAT_FIELDS.includes(setting.field);
                const value = isFloat ? parseFloat(setting.value) : parseInt(setting.value);
                
                console.log('Sending to API: field=' + setting.field + ' (id=' + fieldId + '), value=' + value + ' (' + (isFloat ? 'float' : 'int') + ')');
                
                // Send to API with 5-second timeout
                const savePromise = fetch('/api/save_setting', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({
                        category: 0,  // BATTERY category
                        field: fieldId,
                        value: value
                    })
                });
                
                const timeoutPromise = new Promise((_, reject) => 
                    setTimeout(() => reject(new Error('Timeout after 5 seconds')), 5000)
                );
                
                Promise.race([savePromise, timeoutPromise])
                .then(response => response.json())
                .then(data => {
                    console.log('API response:', data);
                    console.log('Success:', data.success);
                    console.log('Message:', data.message);
                    if (data.success) {
                        savedCount++;
                        console.log('Saved ' + setting.field + ': ' + setting.oldValue + ' → ' + setting.value);
                    } else {
                        failedCount++;
                        console.error('Failed to save ' + setting.field + ': ' + data.message);
                        console.error('Full response:', data);
                        alert('ERROR saving ' + setting.field + ': ' + data.message);
                    }
                    // Save next setting after short delay
                    setTimeout(() => saveNext(index + 1), 100);
                })
                .catch(error => {
                    failedCount++;
                    console.error('FETCH ERROR for ' + setting.field + ':', error);
                    console.error('Error name:', error.name);
                    console.error('Error message:', error.message);
                    if (error.stack) console.error('Error stack:', error.stack);
                    alert('ERROR saving ' + setting.field + ': ' + error.message + '\\n\\nPlease check transmitter connection and try again.');
                    // Stop on first failure to avoid cascading errors
                    saveButton.textContent = '✗ Save Failed!';
                    saveButton.style.backgroundColor = '#dc3545';
                    setTimeout(() => {
                        const remainingCount = pendingSaves.length - index;
                        updateButtonText(remainingCount);
                    }, 3000);
                });
            }
            
            // Start saving from first changed setting
            saveNext(0);
        }
        
        // Phase 3.1: Battery Type Selection
        function loadBatteryTypes() {
            console.log('Loading battery types...');
            
            fetch('/api/get_battery_types')
                .then(response => response.json())
                .then(data => {
                    const typeSelect = document.getElementById('batteryType');
                    typeSelect.innerHTML = '';
                    
                    data.types.forEach(type => {
                        const option = document.createElement('option');
                        option.value = type.id;
                        option.textContent = type.name;
                        typeSelect.appendChild(option);
                    });
                    
                    // Load current selection
                    loadCurrentBatteryType();
                    console.log('Battery types loaded');
                })
                .catch(error => console.error('Error loading battery types:', error));
        }
        
        function loadCurrentBatteryType() {
            fetch('/api/get_selected_types')
                .then(response => response.json())
                .then(data => {
                    const typeSelect = document.getElementById('batteryType');
                    typeSelect.value = data.battery_type;
                    initialValues['batteryType'] = typeSelect.value;
                    updateButtonText(getChangedCount());
                    console.log('Current battery type loaded:', data.battery_type);
                })
                .catch(error => console.error('Error loading current type:', error));
        }
        
        function updateBatteryType() {
            const typeSelect = document.getElementById('batteryType');
            const selectedOption = typeSelect.options[typeSelect.selectedIndex];
            console.log(`Selected: ID=${typeSelect.value}, Name=${selectedOption.text}`);
            updateButtonText(getChangedCount());
        }
        
        function updateButtonText(changedCount) {
            const saveButton = document.getElementById('saveButton');
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
    )rawliteral";

    String html = generatePage("ESP-NOW Receiver - Battery Settings", content, "", script);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

/**
 * @brief Register the battery settings page handler
 */
esp_err_t register_battery_settings_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/transmitter/battery",
        .method    = HTTP_GET,
        .handler   = battery_settings_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
