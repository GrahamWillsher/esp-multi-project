#include "inverter_settings_page_script.h"

String get_inverter_settings_page_script() {
    return R"rawliteral(
        // Store initial value to detect changes
        let initialInverterType = '';
        let initialInverterInterface = '';
        
        window.onload = function() {
            loadInverterTypes();
            loadInverterInterfaces();
        };
        
        function loadInverterTypes() {
            console.log('Loading inverter types...');

            return CatalogLoader.loadCatalogSelect({
                catalogEndpoint: '/api/get_inverter_types',
                selectedEndpoint: '/api/get_selected_types',
                selectedKey: 'inverter_type',
                selectId: 'inverterType',
                loadingText: 'Loading...',
                emptyText: 'No data (check transmitter link)',
                maxRetries: 15,
                logLabel: 'inverter types',
                onSelectedLoaded: (selectedValue, selected) => {
                    initialInverterType = selectedValue;
                    updateButtonState();
                    console.log('Current inverter type loaded:', selected.inverter_type);
                },
                onError: (error) => {
                    console.error('Error loading inverter types:', error);
                    alert('Failed to load inverter types. Please refresh the page.');
                }
            });
        }

        function loadInverterInterfaces() {
            console.log('Loading inverter interfaces...');

            return CatalogLoader.loadCatalogSelect({
                catalogEndpoint: '/api/get_inverter_interfaces',
                selectedEndpoint: '/api/get_selected_interfaces',
                selectedKey: 'inverter_interface',
                selectId: 'inverterInterface',
                loadingText: 'Loading...',
                emptyText: 'No data (check transmitter link)',
                maxRetries: 15,
                logLabel: 'inverter interfaces',
                onSelectedLoaded: (selectedValue, selected) => {
                    initialInverterInterface = selectedValue;
                    updateButtonState();
                    console.log('Current inverter interface loaded:', selected.inverter_interface);
                },
                onError: (error) => {
                    console.error('Error loading inverter interfaces:', error);
                    alert('Failed to load inverter interfaces. Please refresh the page.');
                }
            });
        }
        
        function updateInverterType() {
            const typeSelect = document.getElementById('inverterType');
            const selectedOption = typeSelect.options[typeSelect.selectedIndex];
            console.log(`Selected: ID=${typeSelect.value}, Name=${selectedOption.text}`);
            updateButtonState();
        }

        function updateInverterInterface() {
            const interfaceSelect = document.getElementById('inverterInterface');
            const selectedOption = interfaceSelect.options[interfaceSelect.selectedIndex];
            console.log(`Selected: ID=${interfaceSelect.value}, Name=${selectedOption.text}`);
            updateButtonState();
        }
        
        function updateButtonState() {
            const typeSelect = document.getElementById('inverterType');
            const interfaceSelect = document.getElementById('inverterInterface');
            const saveButton = document.getElementById('saveButton');
            
            const typeChanged = typeSelect.value !== initialInverterType;
            const interfaceChanged = interfaceSelect.value !== initialInverterInterface;
            const changedCount = (typeChanged ? 1 : 0) + (interfaceChanged ? 1 : 0);

            FormChangeTracker.updateSaveButton(saveButton, changedCount, {
                nothingText: 'Nothing to Save',
                changedSingularTemplate: 'Save 1 Change',
                changedPluralTemplate: 'Save {count} Changes'
            });
        }
        
        function saveInverterSettings() {
            const typeSelect = document.getElementById('inverterType');
            const interfaceSelect = document.getElementById('inverterInterface');
            const saveButton = document.getElementById('saveButton');
            
            const typeChanged = typeSelect.value !== initialInverterType;
            const interfaceChanged = interfaceSelect.value !== initialInverterInterface;

            if (!typeChanged && !interfaceChanged) {
                console.log('No inverter changes to save');
                return;
            }
            
            const typeId = parseInt(typeSelect.value);
            const interfaceId = parseInt(interfaceSelect.value);

            SaveOperation.setButtonState(saveButton, {
                text: 'Saving...',
                backgroundColor: '#ff9800',
                disabled: true,
                cursor: 'not-allowed'
            });
            
            let applyMask = 0;
            if (typeChanged) applyMask |= 0x02;
            if (interfaceChanged) applyMask |= 0x08;

            SaveOperation.runComponentApply({
                payload: {
                    apply_mask: applyMask,
                    inverter_type: typeId,
                    inverter_interface: interfaceId
                },
                saveButton: saveButton,
                onReadyForReboot: () => {
                    if (typeChanged) {
                        initialInverterType = typeSelect.value;
                    }
                    if (interfaceChanged) {
                        initialInverterInterface = interfaceSelect.value;
                    }
                },
                restoreButton: () => {
                    updateButtonState();
                },
                onRequestError: (error) => {
                    console.error('Error saving inverter settings:', error);
                    alert('ERROR: ' + error.message + '\n\nPlease check transmitter connection and try again.');
                    SaveOperation.showError(saveButton, '&#10007; Save Failed', () => {
                        updateButtonState();
                    }, 3000);
                }
            });
        }
    )rawliteral";
}
