#include "battery_settings_page_script.h"

String get_battery_settings_page_script() {
    return R"rawliteral(
        // Store initial values to detect changes
        let initialValues = {};
        let batterySettingsRetries = 0;
        const MAX_BATTERY_SETTINGS_RETRIES = 5;
        
        window.onload = function() {
            // Load current settings from transmitter
            loadBatterySettings();
            // Phase 3.1: Load battery types for selector
            loadBatteryTypes();
            // Battery interface selection
            loadBatteryInterfaces();
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
            const interfaceSelect = document.getElementById('batteryInterface');
            if (interfaceSelect) {
                initialValues['batteryInterface'] = interfaceSelect.value;
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
            const interfaceSelect = document.getElementById('batteryInterface');
            if (interfaceSelect) {
                interfaceSelect.addEventListener('change', () => {
                    updateButtonText(getChangedCount());
                });
            }
        }

        function getChangedCount() {
            const trackedFieldIds = FIELD_MAP.map(fm => fm.id).concat(['batteryType', 'batteryInterface']);
            return FormChangeTracker.countChanges(initialValues, trackedFieldIds);
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
            const interfaceSelect = document.getElementById('batteryInterface');
            const batteryInterfaceChanged = interfaceSelect && initialValues['batteryInterface'] !== interfaceSelect.value;
            console.log('Total changed settings: ' + changedSettings.length + ', batteryTypeChanged=' + batteryTypeChanged);

            if (changedSettings.length === 0 && !batteryTypeChanged && !batteryInterfaceChanged) {
                console.log('No changed settings to save');
                return;
            }

            const saveButton = document.getElementById('saveButton');
            const pendingSaves = [...changedSettings];
            const componentApplyPending = batteryTypeChanged || batteryInterfaceChanged;

            SaveOperation.runSequential({
                items: pendingSaves,
                saveButton: saveButton,
                onItemStart: (setting, index, total, button) => {
                    SaveOperation.setButtonState(button, {
                        text: 'Saving ' + (index + 1) + ' of ' + total + '...',
                        backgroundColor: '#ff9800',
                        disabled: true,
                        cursor: 'not-allowed'
                    });
                },
                executeItem: (setting) => {
                    const fieldId = BATTERY_FIELDS[setting.field];
                    const isFloat = FLOAT_FIELDS.includes(setting.field);
                    const value = isFloat ? parseFloat(setting.value) : parseInt(setting.value);

                    console.log('Sending to API: field=' + setting.field + ' (id=' + fieldId + '), value=' + value + ' (' + (isFloat ? 'float' : 'int') + ')');

                    const savePromise = fetch('/api/save_setting', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify({
                            category: 0,
                            field: fieldId,
                            value: value
                        })
                    }).then(response => response.json());

                    const timeoutPromise = new Promise((_, reject) =>
                        setTimeout(() => reject(new Error('Timeout after 5 seconds')), 5000)
                    );

                    return Promise.race([savePromise, timeoutPromise]);
                },
                onItemSuccess: (setting, data) => {
                    console.log('API response:', data);
                    console.log('Saved ' + setting.field + ': ' + setting.oldValue + ' → ' + setting.value);
                },
                onItemFailure: (setting, data) => {
                    console.error('Failed to save ' + setting.field + ': ' + data.message);
                    console.error('Full response:', data);
                    alert('ERROR saving ' + setting.field + ': ' + (data.message || 'Unknown error'));
                },
                onError: (setting, error) => {
                    console.error('FETCH ERROR for ' + setting.field + ':', error);
                    console.error('Error name:', error.name);
                    console.error('Error message:', error.message);
                    if (error.stack) console.error('Error stack:', error.stack);
                    alert('ERROR saving ' + setting.field + ': ' + error.message + '\n\nPlease check transmitter connection and try again.');
                    SaveOperation.showError(saveButton, '✗ Save Failed!', () => {
                        updateButtonText(getChangedCount());
                    }, 3000);
                },
                onComplete: ({ successCount, failureCount, totalCount }) => {
                    if (failureCount > 0) {
                        SaveOperation.showError(saveButton, '⚠ ' + failureCount + ' Failed', () => {
                            const remainingCount = totalCount - successCount;
                            updateButtonText(remainingCount);
                        }, 3000);
                        return;
                    }

                    changedSettings.forEach(s => {
                        initialValues[s.id] = s.value;
                    });

                    if (componentApplyPending) {
                        let applyMask = 0;
                        if (batteryTypeChanged) applyMask |= 0x01;
                        if (batteryInterfaceChanged) applyMask |= 0x04;

                        SaveOperation.runComponentApply({
                            payload: {
                                apply_mask: applyMask,
                                battery_type: parseInt(typeSelect.value),
                                battery_interface: parseInt(interfaceSelect.value)
                            },
                            saveButton: saveButton,
                            onReadyForReboot: () => {
                                if (batteryTypeChanged) {
                                    initialValues['batteryType'] = typeSelect.value;
                                }
                                if (batteryInterfaceChanged) {
                                    initialValues['batteryInterface'] = interfaceSelect.value;
                                }
                            },
                            restoreButton: () => {
                                updateButtonText(getChangedCount());
                            },
                            onRequestError: (error) => {
                                console.error('Component apply failed:', error);
                                SaveOperation.showError(saveButton, '✗ Save Failed', () => {
                                    updateButtonText(getChangedCount());
                                }, 3000);
                            }
                        });
                        return;
                    }

                    SaveOperation.showSuccess(saveButton, '✓ All Saved!', () => {
                        updateButtonText(getChangedCount());
                    }, 3000);
                }
            });
        }
        
        // Phase 3.1: Battery Type Selection
        function loadBatteryTypes() {
            console.log('Loading battery types...');

            return CatalogLoader.loadCatalogSelect({
                catalogEndpoint: '/api/get_battery_types',
                selectedEndpoint: '/api/get_selected_types',
                selectedKey: 'battery_type',
                selectId: 'batteryType',
                loadingText: 'Loading...',
                emptyText: 'No data (check transmitter link)',
                maxRetries: 15,
                logLabel: 'battery types',
                onSelectedLoaded: (selectedValue, selected) => {
                    initialValues['batteryType'] = selectedValue;
                    updateButtonText(getChangedCount());
                    console.log('Current battery type loaded:', selected.battery_type);
                },
                onError: (error) => {
                    console.error('Error loading battery types:', error);
                }
            });
        }

        function loadBatteryInterfaces() {
            console.log('Loading battery interfaces...');

            return CatalogLoader.loadCatalogSelect({
                catalogEndpoint: '/api/get_battery_interfaces',
                selectedEndpoint: '/api/get_selected_interfaces',
                selectedKey: 'battery_interface',
                selectId: 'batteryInterface',
                loadingText: 'Loading...',
                emptyText: 'No data available',
                maxRetries: 0,
                logLabel: 'battery interfaces',
                onSelectedLoaded: (selectedValue, selected) => {
                    initialValues['batteryInterface'] = selectedValue;
                    updateButtonText(getChangedCount());
                    console.log('Current battery interface loaded:', selected.battery_interface);
                },
                onError: (error) => {
                    console.error('Error loading battery interfaces:', error);
                }
            });
        }
        
        function updateBatteryType() {
            const typeSelect = document.getElementById('batteryType');
            const selectedOption = typeSelect.options[typeSelect.selectedIndex];
            console.log(`Selected: ID=${typeSelect.value}, Name=${selectedOption.text}`);
            updateButtonText(getChangedCount());
        }

        function updateBatteryInterface() {
            const interfaceSelect = document.getElementById('batteryInterface');
            const selectedOption = interfaceSelect.options[interfaceSelect.selectedIndex];
            console.log(`Selected: ID=${interfaceSelect.value}, Name=${selectedOption.text}`);
            updateButtonText(getChangedCount());
        }
        
        function updateButtonText(changedCount) {
            const saveButton = document.getElementById('saveButton');
            FormChangeTracker.updateSaveButton(saveButton, changedCount, {
                nothingText: 'Nothing to Save',
                changedSingularTemplate: 'Save 1 Changed Setting',
                changedPluralTemplate: 'Save {count} Changed Settings'
            });
        }
    )rawliteral";
}