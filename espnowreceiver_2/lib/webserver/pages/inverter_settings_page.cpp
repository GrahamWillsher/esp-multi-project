#include "inverter_settings_page.h"
#include "../common/page_generator.h"
#include "../common/nav_buttons.h"
#include "../utils/transmitter_manager.h"
#include <Arduino.h>

/**
 * @brief Handler for the /transmitter/inverter page
 * 
 * Inverter type selection page with dropdown to select inverter protocol
 * Users can view and modify inverter type that is sent to the transmitter
 */
static esp_err_t inverter_settings_handler(httpd_req_t *req) {
    String content = R"rawliteral(
    <h1>Inverter Settings</h1>
    )rawliteral";
    
    // Add navigation buttons
    content += "    " + generate_nav_buttons("/transmitter/inverter");
    
    content += R"rawliteral(
    
    <div class='settings-card'>
        <h3>Inverter Protocol Selection</h3>
        <p style='color: #666; font-size: 14px;'>Select the inverter protocol that matches your inverter. The transmitter will communicate using the selected protocol.</p>
        <p style='color: #ff6b35; font-size: 14px; font-weight: bold;'>⚠️ Changing the inverter type or interface will reboot the transmitter to apply changes.</p>
        
        <div class='settings-row'>
            <label for='inverterType'>Inverter Protocol:</label>
            <select id='inverterType' onchange='updateInverterType()'>
                <option value=''>Loading...</option>
            </select>
        </div>

        <div class='settings-row'>
            <label for='inverterInterface'>Inverter Interface:</label>
            <select id='inverterInterface' onchange='updateInverterInterface()'>
                <option value=''>Loading...</option>
            </select>
        </div>
    </div>
    
    <div style='text-align: center; margin-top: 30px;'>
        <button id='saveButton' onclick='saveInverterSettings()' disabled style='padding: 12px 40px; font-size: 16px; background-color: #6c757d; color: white; border: none; border-radius: 4px; cursor: pointer;'>
            Nothing to Save
        </button>
    </div>
)rawliteral";

    String script = R"rawliteral(
        // Store initial value to detect changes
        let initialInverterType = '';
        let initialInverterInterface = '';
        let inverterTypeRetries = 0;
        const MAX_INVERTER_TYPE_RETRIES = 15;
        let inverterInterfaceRetries = 0;
        const MAX_INVERTER_INTERFACE_RETRIES = 15;
        
        window.onload = function() {
            // Load inverter types and current selection
            loadInverterTypes();
            loadInverterInterfaces();
        };
        
        function loadInverterTypes() {
            console.log('Loading inverter types...');
            
            fetch('/api/get_inverter_types')
                .then(response => response.json())
                .then(data => {
                    const typeSelect = document.getElementById('inverterType');

                    if (data.loading || !Array.isArray(data.types) || data.types.length === 0) {
                        typeSelect.innerHTML = "<option value=''>Loading...</option>";

                        if (inverterTypeRetries < MAX_INVERTER_TYPE_RETRIES) {
                            inverterTypeRetries++;
                            setTimeout(loadInverterTypes, 1000);
                        } else {
                            typeSelect.innerHTML = "<option value=''>No data (check transmitter link)</option>";
                        }
                        return;
                    }

                    inverterTypeRetries = 0;
                    typeSelect.innerHTML = '';
                    
                    // Populate dropdown with all inverter types
                    data.types.forEach(type => {
                        const option = document.createElement('option');
                        option.value = type.id;
                        option.textContent = type.name;
                        typeSelect.appendChild(option);
                    });
                    
                    // Load current selection
                    loadCurrentInverterType();
                    console.log('Inverter types loaded');
                })
                .catch(error => {
                    console.error('Error loading inverter types:', error);
                    alert('Failed to load inverter types. Please refresh the page.');
                });
        }
        
        function loadCurrentInverterType() {
            fetch('/api/get_selected_types')
                .then(response => response.json())
                .then(data => {
                    const typeSelect = document.getElementById('inverterType');
                    const currentType = String(data.inverter_type);
                    typeSelect.value = currentType;
                    initialInverterType = currentType;
                    
                    updateButtonState();
                    
                    console.log('Current inverter type loaded:', data.inverter_type);
                })
                .catch(error => {
                    console.error('Error loading current type:', error);
                    alert('Failed to load current inverter type. Please refresh the page.');
                });
        }

        function loadInverterInterfaces() {
            console.log('Loading inverter interfaces...');

            fetch('/api/get_inverter_interfaces')
                .then(response => response.json())
                .then(data => {
                    const interfaceSelect = document.getElementById('inverterInterface');

                    if (data.loading || !Array.isArray(data.types) || data.types.length === 0) {
                        interfaceSelect.innerHTML = "<option value=''>Loading...</option>";

                        if (inverterInterfaceRetries < MAX_INVERTER_INTERFACE_RETRIES) {
                            inverterInterfaceRetries++;
                            setTimeout(loadInverterInterfaces, 1000);
                        } else {
                            interfaceSelect.innerHTML = "<option value=''>No data (check transmitter link)</option>";
                        }
                        return;
                    }

                    inverterInterfaceRetries = 0;
                    interfaceSelect.innerHTML = '';

                    data.types.forEach(type => {
                        const option = document.createElement('option');
                        option.value = type.id;
                        option.textContent = type.name;
                        interfaceSelect.appendChild(option);
                    });

                    loadCurrentInverterInterface();
                    console.log('Inverter interfaces loaded');
                })
                .catch(error => {
                    console.error('Error loading inverter interfaces:', error);
                    alert('Failed to load inverter interfaces. Please refresh the page.');
                });
        }

        function loadCurrentInverterInterface() {
            fetch('/api/get_selected_interfaces')
                .then(response => response.json())
                .then(data => {
                    const interfaceSelect = document.getElementById('inverterInterface');
                    const currentInterface = String(data.inverter_interface);
                    interfaceSelect.value = currentInterface;
                    initialInverterInterface = currentInterface;

                    updateButtonState();

                    console.log('Current inverter interface loaded:', data.inverter_interface);
                })
                .catch(error => {
                    console.error('Error loading current interface:', error);
                    alert('Failed to load current inverter interface. Please refresh the page.');
                });
        }
        
        function updateInverterType() {
            const typeSelect = document.getElementById('inverterType');
            const selectedOption = typeSelect.options[typeSelect.selectedIndex];
            console.log(`Selected: ID=${typeSelect.value}, Name=${selectedOption.text}`);
            
            // Update save button state
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
            
            if (changedCount > 0) {
                saveButton.textContent = `Save ${changedCount} Change${changedCount > 1 ? 's' : ''}`;
                saveButton.style.backgroundColor = '#4CAF50';
                saveButton.disabled = false;
                saveButton.style.cursor = 'pointer';
            } else {
                saveButton.textContent = 'Nothing to Save';
                saveButton.style.backgroundColor = '#6c757d';
                saveButton.disabled = true;
                saveButton.style.cursor = 'not-allowed';
            }
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
            
            const selectedName = typeSelect.options[typeSelect.selectedIndex].text;
            const interfaceName = interfaceSelect.options[interfaceSelect.selectedIndex].text;
            const typeId = parseInt(typeSelect.value);
            const interfaceId = parseInt(interfaceSelect.value);
            
            // Show confirmation (inverter type change requires transmitter reboot)
            const confirmed = confirm(
                '⚠️ TRANSMITTER REBOOT REQUIRED\\n\\n' +
                (typeChanged ? ('Changing the inverter type to "' + selectedName + '" will reboot the transmitter to apply changes.\n\n') : '') +
                (interfaceChanged ? ('Changing the inverter interface to "' + interfaceName + '" will reboot the transmitter to apply changes.\n\n') : '') +
                'This will temporarily interrupt data transmission (approximately 30 seconds).\\n\\n' +
                'Do you want to continue?'
            );
            
            if (!confirmed) {
                console.log('Inverter change cancelled by user');
                return;
            }
            
            // Update button state
            saveButton.textContent = 'Saving...';
            saveButton.style.backgroundColor = '#ff9800';
            saveButton.disabled = true;
            
            const pendingSaves = [];
            if (typeChanged) {
                pendingSaves.push({ kind: 'type', value: typeId, name: selectedName });
            }
            if (interfaceChanged) {
                pendingSaves.push({ kind: 'interface', value: interfaceId, name: interfaceName });
            }

            function saveNext(index) {
                if (index >= pendingSaves.length) {
                    if (typeChanged) {
                        initialInverterType = typeSelect.value;
                    }
                    if (interfaceChanged) {
                        initialInverterInterface = interfaceSelect.value;
                    }

                    saveButton.textContent = '✓ Saved!';
                    saveButton.style.backgroundColor = '#28a745';

                    alert(
                        '✓ Inverter settings saved successfully!\n\n' +
                        'The transmitter will reboot to apply the new inverter settings.\n\n' +
                        'Please wait approximately 30 seconds for the transmitter to restart.'
                    );

                    setTimeout(() => {
                        updateButtonState();
                    }, 3000);
                    return;
                }

                const setting = pendingSaves[index];
                const url = setting.kind === 'type' ? '/api/set_inverter_type' : '/api/set_inverter_interface';
                const payload = setting.kind === 'type'
                    ? {type: setting.value}
                    : {interface: setting.value};

                console.log('Saving inverter ' + setting.kind + ': ID=' + setting.value + ' (' + setting.name + ')');

                fetch(url, {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify(payload)
                })
                .then(response => response.json())
                .then(data => {
                    if (data.success) {
                        console.log('Inverter ' + setting.kind + ' saved successfully');
                        setTimeout(() => saveNext(index + 1), 100);
                    } else {
                        console.error('Failed to save inverter ' + setting.kind + ':', data.error);
                        alert('ERROR: Failed to save inverter ' + setting.kind + '\n\n' + data.error);

                        saveButton.textContent = '✗ Save Failed';
                        saveButton.style.backgroundColor = '#dc3545';

                        setTimeout(() => {
                            updateButtonState();
                        }, 3000);
                    }
                })
                .catch(error => {
                    console.error('Error saving inverter ' + setting.kind + ':', error);
                    alert('ERROR: Network error while saving\n\n' + error.message + '\n\nPlease check transmitter connection and try again.');

                    saveButton.textContent = '✗ Network Error';
                    saveButton.style.backgroundColor = '#dc3545';

                    setTimeout(() => {
                        updateButtonState();
                    }, 3000);
                });
            }

            saveNext(0);
        }
    )rawliteral";

    String html = generatePage("ESP-NOW Receiver - Inverter Settings", content, "", script);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

/**
 * @brief Register the inverter settings page handler
 */
esp_err_t register_inverter_settings_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/transmitter/inverter",
        .method    = HTTP_GET,
        .handler   = inverter_settings_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
