# Transmitter Configuration Change Tracking System

## Overview
The Settings Page implements a **unified change tracking system** for all transmitter configuration sections. This allows a single "Save" button to track changes across multiple configuration areas (Network, MQTT, Battery, Power, etc.) and display the number of changed settings.

## Architecture

### Key Components

1. **`initialTransmitterConfig` object**: Stores initial values of all tracked fields when page loads
2. **`TRANSMITTER_CONFIG_FIELDS` array**: Lists all field IDs to track across all sections
3. **`storeInitialTransmitterConfig()`**: Captures initial values after page load
4. **`attachTransmitterChangeListeners()`**: Attaches change listeners to all tracked fields
5. **`countTransmitterChanges()`**: Counts fields that differ from initial values
6. **`updateSaveButtonText()`**: Updates button text to show number of changed fields

### Current Implementation (Network Configuration)

**File:** `lib/webserver/pages/settings_page.cpp`

```javascript
// Store initial transmitter config values to detect changes across ALL sections
let initialTransmitterConfig = {};

// Transmitter config field IDs - includes ALL configurable sections
// Add new sections here as they become configurable
const TRANSMITTER_CONFIG_FIELDS = [
    // Network Configuration
    'staticIpEnabled',
    'ip0', 'ip1', 'ip2', 'ip3',
    'gw0', 'gw1', 'gw2', 'gw3',
    'sub0', 'sub1', 'sub2', 'sub3',
    'dns1_0', 'dns1_1', 'dns1_2', 'dns1_3',
    'dns2_0', 'dns2_1', 'dns2_2', 'dns2_3'
    // MQTT Configuration fields will be added here when implemented
    // Battery Configuration fields will be added here when implemented
    // Power Settings fields will be added here when implemented
];
```

## How It Works

### 1. Page Load Sequence
```javascript
// In loadNetworkConfig():
fetch('/api/get_network_config')
    .then(response => response.json())
    .then(data => {
        // Populate fields from API
        document.getElementById('staticIpEnabled').checked = data.static_ip;
        // ... populate all IP fields ...
        
        // Store initial values and attach listeners
        storeInitialTransmitterConfig();  // Capture initial state
        attachTransmitterChangeListeners();  // Listen for changes
        updateSaveButtonText(0);  // Show "Nothing to Save"
    });
```

### 2. Change Detection
```javascript
// When user changes any field:
element.addEventListener('input', () => {
    const changedCount = countTransmitterChanges();  // Count changes
    updateSaveButtonText(changedCount);  // Update button
});
```

### 3. Save Confirmation
```javascript
// After successful save:
storeInitialTransmitterConfig();  // Update initial values
updateSaveButtonText(0);  // Reset button to "Nothing to Save"
```

### 4. Button States
- **0 changes**: "Nothing to Save" (gray, disabled)
- **1+ changes**: "Save N Changed Setting(s)" (green, enabled)
- **After save**: "✓ Saved! Reboot transmitter to apply." (green, 3 seconds)

## Adding New Configuration Sections

### Example: Adding MQTT Configuration

#### Step 1: Add HTML Fields
```cpp
R"rawliteral(
<div class='settings-card'>
    <h3>MQTT Configuration</h3>
    <div class='settings-row'>
        <label for='mqttEnabled'>Enable MQTT</label>
        <input type='checkbox' id='mqttEnabled'>
    </div>
    <div class='settings-row'>
        <label for='mqttServer'>MQTT Server</label>
        <input type='text' id='mqttServer' placeholder='192.168.1.100'>
    </div>
    <div class='settings-row'>
        <label for='mqttPort'>MQTT Port</label>
        <input type='number' id='mqttPort' value='1883'>
    </div>
    <div class='settings-row'>
        <label for='mqttUsername'>Username</label>
        <input type='text' id='mqttUsername'>
    </div>
    <div class='settings-row'>
        <label for='mqttPassword'>Password</label>
        <input type='password' id='mqttPassword'>
    </div>
</div>
)rawliteral"
```

#### Step 2: Add Field IDs to Tracking Array
```javascript
const TRANSMITTER_CONFIG_FIELDS = [
    // Network Configuration
    'staticIpEnabled',
    'ip0', 'ip1', 'ip2', 'ip3',
    // ... existing network fields ...
    
    // MQTT Configuration
    'mqttEnabled',
    'mqttServer',
    'mqttPort',
    'mqttUsername',
    'mqttPassword',
    
    // Battery Configuration fields will be added here when implemented
    // Power Settings fields will be added here when implemented
];
```

#### Step 3: Load MQTT Values from API
```javascript
async function loadMqttConfig() {
    try {
        const response = await fetch('/api/get_mqtt_config');
        const data = await response.json();
        
        // Populate MQTT fields
        document.getElementById('mqttEnabled').checked = data.enabled || false;
        document.getElementById('mqttServer').value = data.server || '';
        document.getElementById('mqttPort').value = data.port || 1883;
        document.getElementById('mqttUsername').value = data.username || '';
        document.getElementById('mqttPassword').value = data.password || '';
        
        // Re-store initial values and re-attach listeners
        storeInitialTransmitterConfig();
        attachTransmitterChangeListeners();
        updateSaveButtonText(0);
        
    } catch (error) {
        console.error('Failed to load MQTT config:', error);
    }
}

// Call during page initialization
window.onload = function() {
    loadNetworkConfig();
    loadMqttConfig();  // Add this
};
```

#### Step 4: Update Save Function
The save function should handle all sections:

```javascript
async function saveTransmitterConfig() {
    const saveButton = document.getElementById('saveNetworkBtn');
    const originalText = saveButton.textContent;
    
    saveButton.disabled = true;
    saveButton.textContent = 'Saving...';
    
    // Gather all configuration data
    const configData = {
        network: {
            static_ip: document.getElementById('staticIpEnabled').checked,
            local_ip: getIpFromFields('ip'),
            gateway: getIpFromFields('gw'),
            subnet: getIpFromFields('sub'),
            dns_primary: getIpFromFields('dns1'),
            dns_secondary: getIpFromFields('dns2')
        },
        mqtt: {
            enabled: document.getElementById('mqttEnabled').checked,
            server: document.getElementById('mqttServer').value,
            port: parseInt(document.getElementById('mqttPort').value),
            username: document.getElementById('mqttUsername').value,
            password: document.getElementById('mqttPassword').value
        }
        // Add more sections as needed
    };
    
    try {
        const response = await fetch('/api/save_transmitter_config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(configData)
        });
        
        const data = await response.json();
        
        if (data.success) {
            saveButton.textContent = '✓ Saved! Reboot transmitter to apply.';
            saveButton.style.backgroundColor = '#28a745';
            
            storeInitialTransmitterConfig();
            setTimeout(() => {
                updateSaveButtonText(0);
            }, 3000);
        }
    } catch (error) {
        console.error('Save failed:', error);
    }
}
```

## API Endpoints Required

### Receiver Side
- **GET `/api/get_network_config`** - Returns current network settings
- **GET `/api/get_mqtt_config`** - Returns current MQTT settings (when implemented)
- **POST `/api/save_transmitter_config`** - Saves all changed settings to transmitter

### Transmitter Side
- Handles ESP-NOW messages for config updates
- Stores configuration in NVS
- Sends acknowledgment back to receiver

## Best Practices

1. **Always add field IDs to `TRANSMITTER_CONFIG_FIELDS`** when adding new inputs
2. **Use consistent naming patterns**: `sectionFieldName` (e.g., `mqttServer`, `batteryCapacity`)
3. **Load all configs on page load** and call `storeInitialTransmitterConfig()` after each load
4. **Use a single save button** at the bottom of the page for all sections
5. **Update initial values after successful save** to reset change tracking
6. **Add inline comments** in `TRANSMITTER_CONFIG_FIELDS` showing where to add future sections

## Debugging

Enable console logging to track change detection:
```javascript
console.log('Change detected in field:', fieldId);
console.log('Changed count:', changedCount);
console.log('Initial transmitter config stored:', initialTransmitterConfig);
```

All debug logging is already built into the functions for troubleshooting.

## Related Files

- **`lib/webserver/pages/settings_page.cpp`** - Main settings page with change tracking
- **`lib/webserver/api/api_handlers.cpp`** - API endpoint handlers
- **`lib/webserver/pages/battery_settings_page.cpp`** - Reference implementation (battery-specific pattern)

## Status

- ✅ Network Configuration - Fully implemented with unified tracking
- 🔄 MQTT Configuration - Placeholder comments added, ready for implementation
- 🔄 Battery Configuration - Placeholder comments added, ready for implementation
- 🔄 Power Settings - Placeholder comments added, ready for implementation

## Migration Notes

Previously, the system used network-specific names:
- `initialNetworkConfig` → **NOW:** `initialTransmitterConfig`
- `NETWORK_FIELDS` → **NOW:** `TRANSMITTER_CONFIG_FIELDS`
- `storeInitialNetworkConfig()` → **NOW:** `storeInitialTransmitterConfig()`
- `attachNetworkChangeListeners()` → **NOW:** `attachTransmitterChangeListeners()`
- `countNetworkChanges()` → **NOW:** `countTransmitterChanges()`
- `updateNetworkButtonText()` → **NOW:** `updateSaveButtonText()`

This refactoring ensures the system is **future-proof** and can handle all transmitter configuration sections with a single unified tracking system.
