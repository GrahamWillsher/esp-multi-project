# Configuration Synchronization - Quick Start Guide

## For Developers

### Adding a New Config Field

#### 1. Update config_structures.h
```cpp
// Add field to appropriate section
struct MqttConfig {
    char broker_address[64];
    uint16_t broker_port;
    char username[32];
    char password[32];
    char client_id[32];
    bool enabled;
    uint16_t keepalive;  // <-- NEW FIELD
} __attribute__((packed));
```

#### 2. Update config_manager.cpp
```cpp
// Add field ID enum
enum MqttFieldId {
    MQTT_FIELD_BROKER_ADDRESS = 0,
    MQTT_FIELD_BROKER_PORT = 1,
    // ... existing fields ...
    MQTT_FIELD_KEEPALIVE = 6,  // <-- NEW ID
};

// Add update handler
void ConfigManager::updateMqttField(uint8_t field_id, const void* value, size_t value_len) {
    switch (field_id) {
        // ... existing cases ...
        case MQTT_FIELD_KEEPALIVE:
            if (value_len == sizeof(uint16_t)) {
                config_.mqtt.keepalive = *(const uint16_t*)value;
            }
            break;
    }
}
```

#### 3. Update config_provider.cpp (Transmitter)
```cpp
void TransmitterConfigProvider::init() {
    // ... existing initializations ...
    
    // Add new field initialization
    extern uint16_t mqtt_keepalive;  // From your global config
    config_manager_.updateField(MQTT_CONFIG, MQTT_FIELD_KEEPALIVE, 
                                &mqtt_keepalive, sizeof(mqtt_keepalive));
}

// To send delta update when field changes
void onMqttKeepaliveChanged(uint16_t new_value) {
    TransmitterConfigProvider::instance().sendDeltaUpdate(
        MQTT_CONFIG, 
        MQTT_FIELD_KEEPALIVE, 
        &new_value, 
        sizeof(new_value)
    );
}
```

#### 4. Update settings_processor.cpp (Receiver)
```cpp
String settings_processor(const String& var) {
    auto& configMgr = ReceiverConfigManager::instance();
    bool configAvailable = configMgr.isConfigAvailable();
    
    // ... existing fields ...
    
    if (var == "MQTTKEEPALIVE") {
        if (configAvailable) {
            const MqttConfig& mqtt = configMgr.getMqttConfig();
            return String(mqtt.keepalive);
        }
        return "60";  // Default value
    }
}
```

#### 5. Update settings_page.cpp (HTML)
```cpp
content += R"rawliteral(
    <div class='settings-row' id='mqttKeepaliveRow'>
        <label>MQTT Keepalive:</label>
        <input type='text' value='%MQTTKEEPALIVE% sec' disabled />
    </div>
)rawliteral";
```

### Adding a New Config Section

#### 1. Add section enum
```cpp
enum ConfigSection {
    MQTT_CONFIG = 0,
    // ... existing sections ...
    DISPLAY_CONFIG = 8,  // <-- NEW SECTION
};
```

#### 2. Create structure
```cpp
struct DisplayConfig {
    uint8_t brightness;
    bool auto_dim;
    uint16_t timeout_ms;
    uint8_t orientation;
} __attribute__((packed));
```

#### 3. Add to FullConfigSnapshot
```cpp
struct FullConfigSnapshot {
    ConfigVersion version;
    MqttConfig mqtt;
    // ... existing configs ...
    DisplayConfig display;  // <-- NEW
    uint32_t checksum;
} __attribute__((packed));
```

#### 4. Update ConfigManager
```cpp
class ConfigManager {
public:
    const DisplayConfig& getDisplayConfig() const { return config_.display; }
    void updateDisplayField(uint8_t field_id, const void* value, size_t value_len);
    
private:
    void updateField(ConfigSection section, uint8_t field_id, 
                    const void* value, size_t value_len) {
        switch (section) {
            // ... existing cases ...
            case DISPLAY_CONFIG:
                updateDisplayField(field_id, value, value_len);
                config_.version.display_version++;
                break;
        }
        config_.version.global_version++;
    }
};
```

## For Users

### Viewing Configuration Status

1. **Open receiver web interface**: `http://receiver-ip/`
2. **Navigate to Settings page**
3. **Check status indicators**:
   - ✅ Green: Config synchronized successfully
   - ⚠️ Yellow: Config not yet received
   - ❌ Red: Synchronization error

### Configuration Version
Bottom of settings page shows:
```
Configuration Version: 42 (Last Updated: 2025-01-22 14:30:45)
```

### Troubleshooting

#### Config Not Showing
**Symptoms**: Yellow warning banner, default values displayed
**Causes**:
- Transmitter not connected
- Config sync in progress
- Network issues

**Solutions**:
1. Wait 5-10 seconds for connection
2. Check ESP-NOW connection status
3. Reboot receiver
4. Check transmitter logs

#### Config Out of Sync
**Symptoms**: Old values displayed, version number doesn't match
**Causes**:
- Missed delta update
- ACK lost in transmission

**Solutions**:
1. Receiver will auto-request resync after 5 failed retries
2. Manual resync: Reboot receiver
3. Check network reliability

#### Partial Config
**Symptoms**: Some fields empty, others populated
**Causes**:
- Fragment loss during snapshot
- Checksum validation failure

**Solutions**:
1. Check serial logs for fragment errors
2. Receiver will retry automatically
3. Wait for resync

## API Endpoints

### GET /api/config_version
Returns current configuration version and status.

**Response (Config Available)**:
```json
{
    "available": true,
    "global_version": 42,
    "timestamp": 1705934445
}
```

**Response (Config Unavailable)**:
```json
{
    "available": false
}
```

### Using in Custom Pages
```javascript
fetch('/api/config_version')
    .then(response => response.json())
    .then(data => {
        if (data.available) {
            console.log('Config version:', data.global_version);
            const date = new Date(data.timestamp * 1000);
            console.log('Last updated:', date.toLocaleString());
        } else {
            console.log('Config not available yet');
        }
    });
```

## Serial Monitor Messages

### Transmitter
```
[CONFIG] Configuration provider initialized
[APP_RX_TASK] Received CONFIG_REQUEST_FULL from 12:34:56:78:9A:BC
[CONFIG] Sending full snapshot (seq=1234, fragments=2)
[CONFIG] Fragment 0/2 sent
[CONFIG] Fragment 1/2 sent
[APP_RX_TASK] Received CONFIG_ACK (seq=1234)
[CONFIG] Snapshot acknowledged, canceling retries
```

### Receiver
```
Transmitter connected via PROBE
Requested full configuration snapshot from transmitter
[CONFIG] Received snapshot fragment 0/2 (seq=1234)
[CONFIG] Received snapshot fragment 1/2 (seq=1234)
[CONFIG] All fragments received, validating...
[CONFIG] Checksum valid, applying configuration
[CONFIG] Sending ACK (seq=1234)
[CONFIG] Configuration synchronized (version=42)
```

## Performance Metrics

### Typical Operation
- Initial sync: ~500ms
- Delta update: ~50ms
- Retry attempt: 150ms-1s
- Memory usage: ~2KB
- CPU usage: <1%

### Network Traffic
- Full snapshot: ~700 bytes (2 fragments + ACKs)
- Delta update: ~40 bytes (update + ACK)
- Retry overhead: ~20 bytes per retry
- Background: 0 bytes (event-driven)

## Best Practices

### For Transmitter Code
1. Initialize ConfigProvider in setup()
2. Call process() in loop()
3. Send delta updates immediately when config changes
4. Don't send updates during critical operations
5. Log all config changes

### For Receiver Code
1. Check isConfigAvailable() before using config
2. Provide default values as fallback
3. Display status to users
4. Handle config updates gracefully
5. Don't assume config is always available

### For Web Pages
1. Show loading indicator during sync
2. Display version number for debugging
3. Provide clear error messages
4. Allow manual refresh/resync
5. Validate displayed data

## Common Patterns

### Conditional Features
```cpp
if (ReceiverConfigManager::instance().isConfigAvailable()) {
    const MqttConfig& mqtt = ReceiverConfigManager::instance().getMqttConfig();
    if (mqtt.enabled) {
        // Use MQTT config
    }
} else {
    // Use defaults or show warning
}
```

### Config-Driven UI
```cpp
const BatteryConfig& batt = ReceiverConfigManager::instance().getBatteryConfig();
if (batt.double_battery) {
    displayTwoBatteries();
} else {
    displaySingleBattery();
}
```

### Version Checking
```cpp
static uint32_t last_version = 0;
uint32_t current_version = ReceiverConfigManager::instance().getGlobalVersion();

if (current_version != last_version) {
    // Config changed, refresh UI
    updateDisplay();
    last_version = current_version;
}
```

## Security Notes

1. **No encryption**: Config data sent in plaintext over ESP-NOW
2. **No authentication**: Any device can request config
3. **No authorization**: All config fields equally accessible
4. **Mitigation**: ESP-NOW uses MAC address filtering, operates on local network

For sensitive deployments:
- Implement field-level encryption
- Add device authentication tokens
- Use secure channels for critical fields
- Audit config access logs

## Version History
- v1.0 (2025-01): Initial implementation
  - Full snapshot + delta updates
  - ACK-based reliability
  - Fragment support
  - 8 config sections
  - Web interface integration
