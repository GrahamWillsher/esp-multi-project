# MQTT Configuration Implementation Plan

## ✅ APPROVED FOR IMPLEMENTATION

### User Decisions
1. **Password**: Save in plaintext, display as `"********"` in UI
2. **Save Button**: Unified "Save Transmitter Config" for all sections
3. **Topics**: Keep hardcoded (deferred for future enhancement)
4. **MQTT Reload**: Hot-reload without device reboot
5. **Port**: User-selectable, defaults to 1883

---

## Executive Summary
This document outlines the complete implementation for enabling MQTT configuration management in the ESP-NOW transmitter/receiver system. The implementation will allow the receiver's web interface to configure MQTT settings on the transmitter, including enabling/disabling MQTT and setting the broker IP address in 4-quad format (xxx.xxx.xxx.xxx).

**Key Features:**
- ✅ Runtime MQTT configuration (no recompilation needed)
- ✅ 4-quad IP address input (xxx.xxx.xxx.xxx)
- ✅ Enable/disable checkbox (fully functional)
- ✅ Hot-reload MQTT client (no reboot required)
- ✅ Unified save button (all transmitter config sections)
- ✅ Password masking in UI
- ✅ User-selectable port (default: 1883)
- ✅ NVS persistence on transmitter
- ✅ ESP-NOW protocol for config synchronization

---

## Current State Analysis

### Transmitter (Current)
- **Location**: `network_config.h`
- **MQTT Configuration**:
  - Server: `const char* server{"192.168.1.221"}` (string format)
  - Port: `uint16_t port{1883}`
  - Username: `const char* username{"Aintree34"}`
  - Password: `const char* password{"Shanghai17"}`
  - Client ID: `const char* client_id{"espnow_transmitter"}`
  - Enabled: `constexpr bool MQTT_ENABLED = true` (compile-time constant)
- **Storage**: Currently hardcoded in header file, no NVS storage
- **Problem**: Not configurable at runtime, requires recompilation

### Receiver (Current)
- **UI**: Settings page shows MQTT fields but all are `disabled`
- **Fields**:
  - MQTT Enabled checkbox (checked, disabled)
  - MQTT Server (text input, disabled, shows "192.168.1.221")
  - MQTT Port (text input, disabled, shows "1883")
  - MQTT User (text input, disabled)
  - MQTT Password (password input, disabled)
  - MQTT Client ID (text input, disabled)
- **Tracking**: `TRANSMITTER_CONFIG_FIELDS` array does NOT include MQTT fields
- **Problem**: UI exists but completely non-functional

### ESP-NOW Protocol (Current)
- **Existing Messages**:
  - `msg_network_config_request` / `msg_network_config_update` / `msg_network_config_ack` (for IP config)
  - Settings category enum includes `SETTINGS_MQTT = 4`
  - Generic `settings_update_msg_t` structure exists
- **Gap**: No dedicated MQTT configuration messages

---

## Proposed Implementation

### Phase 1: Data Structures & Protocol

#### 1.1 ESP-NOW Message Structures (`espnow_common.h`)

```cpp
// MQTT configuration request message - Receiver → Transmitter
typedef struct __attribute__((packed)) {
    uint8_t type;                // msg_mqtt_config_request
} mqtt_config_request_t;  // Total: 1 byte

// MQTT configuration update message - Receiver → Transmitter
typedef struct __attribute__((packed)) {
    uint8_t type;                // msg_mqtt_config_update
    uint8_t enabled;             // 0 = disabled, 1 = enabled
    uint8_t server[4];           // MQTT broker IP (4 octets: xxx.xxx.xxx.xxx)
    uint16_t port;               // MQTT broker port
    char username[32];           // Username (empty string if none)
    char password[32];           // Password (empty string if none)
    char client_id[32];          // Client ID
    uint32_t config_version;     // Version for tracking
    uint16_t checksum;           // Integrity check
} mqtt_config_update_t;  // Total: 106 bytes

// MQTT configuration ACK - Transmitter → Receiver
typedef struct __attribute__((packed)) {
    uint8_t type;                // msg_mqtt_config_ack
    uint8_t success;             // 0 = failed, 1 = success
    uint8_t enabled;             // Current MQTT enabled state
    uint8_t server[4];           // Current MQTT broker IP
    uint16_t port;               // Current MQTT port
    char username[32];           // Current username
    char password[32];           // Current password (or masked)
    char client_id[32];          // Current client ID
    uint32_t config_version;     // Configuration version
    char message[64];            // Status message
    uint16_t checksum;           // Integrity check
} mqtt_config_ack_t;  // Total: 178 bytes
```

**New Message Types** (add to `msg_type` enum):
- `msg_mqtt_config_request`
- `msg_mqtt_config_update`
- `msg_mqtt_config_ack`

#### 1.2 Transmitter MQTT Manager Class

**New File**: `lib/mqtt_manager/mqtt_manager.h` & `mqtt_manager.cpp`

```cpp
class MqttManager {
private:
    static bool enabled_;
    static IPAddress server_;
    static uint16_t port_;
    static char username_[32];
    static char password_[32];
    static char client_id_[32];
    static uint32_t config_version_;
    
    static constexpr const char* NVS_NAMESPACE = "mqtt_cfg";
    
public:
    // Load from NVS
    static bool loadConfig();
    
    // Save to NVS
    static bool saveConfig(bool enabled, const IPAddress& server, uint16_t port,
                          const char* username, const char* password, 
                          const char* client_id);
    
    // Getters
    static bool isEnabled() { return enabled_; }
    static IPAddress getServer() { return server_; }
    static uint16_t getPort() { return port_; }
    static const char* getUsername() { return username_; }
    static const char* getPassword() { return password_; }
    static const char* getClientId() { return client_id_; }
    static uint32_t getConfigVersion() { return config_version_; }
    
    // Apply configuration (restart MQTT client if needed)
    static void applyConfig();
};
```

**NVS Keys**:
- `mqtt_enabled` (uint8_t)
- `mqtt_server` (4 bytes, uint8_t[4])
- `mqtt_port` (uint16_t)
- `mqtt_user` (string, max 32)
- `mqtt_pass` (string, max 32)
- `mqtt_client` (string, max 32)
- `mqtt_version` (uint32_t)

---

### Phase 2: Transmitter Implementation

#### 2.1 Message Handler (`message_handler.cpp`)

```cpp
// Register route
router.register_route(msg_mqtt_config_request,
    [](const espnow_queue_msg_t* msg, void* ctx) {
        static_cast<EspnowMessageHandler*>(ctx)->handle_mqtt_config_request(*msg);
    },
    0xFF, this);

router.register_route(msg_mqtt_config_update,
    [](const espnow_queue_msg_t* msg, void* ctx) {
        static_cast<EspnowMessageHandler*>(ctx)->handle_mqtt_config_update(*msg);
    },
    0xFF, this);

// Handler functions
void EspnowMessageHandler::handle_mqtt_config_request(const espnow_queue_msg_t& msg) {
    memcpy(receiver_mac_, msg.mac, 6);
    LOG_INFO("[MQTT_CFG] Received config request");
    send_mqtt_config_ack(true, "Current configuration");
}

void EspnowMessageHandler::handle_mqtt_config_update(const espnow_queue_msg_t& msg) {
    // Parse message
    // Validate (IP not 0.0.0.0, port valid, etc.)
    // Save to MqttManager
    // Send ACK
}

void EspnowMessageHandler::send_mqtt_config_ack(bool success, const char* message) {
    // Build ack with current config from MqttManager
    // Send via esp_now_send()
}
```

#### 2.2 Initialization & Boot Sequence

**In `main.cpp` or initialization code**:
```cpp
void setup() {
    // ... existing setup ...
    
    // Load MQTT configuration from NVS
    if (!MqttManager::loadConfig()) {
        LOG_WARN("MQTT config not found, using defaults");
        // Use hardcoded defaults from network_config.h
        MqttManager::saveConfig(
            config::features::MQTT_ENABLED,
            IPAddress(192, 168, 1, 221),
            1883,
            config::get_mqtt_config().username,
            config::get_mqtt_config().password,
            config::get_mqtt_config().client_id
        );
    }
    
    // Apply MQTT configuration
    MqttManager::applyConfig();
}
```

---

### Phase 3: Receiver Implementation

#### 3.1 TransmitterManager (`transmitter_manager.h`)

**Add MQTT data storage**:
```cpp
class TransmitterManager {
private:
    // ... existing fields ...
    
    // MQTT configuration (from transmitter)
    static bool mqtt_enabled;
    static uint8_t mqtt_server[4];
    static uint16_t mqtt_port;
    static char mqtt_username[32];
    static char mqtt_password[32];
    static char mqtt_client_id[32];
    static uint32_t mqtt_config_version;
    static bool mqtt_config_known;
    
public:
    // MQTT management
    static void storeMqttConfig(bool enabled, const uint8_t* server, uint16_t port,
                               const char* username, const char* password,
                               const char* client_id, uint32_t version);
    
    static bool isMqttEnabled();
    static const uint8_t* getMqttServer();
    static uint16_t getMqttPort();
    static const char* getMqttUsername();
    static const char* getMqttPassword();
    static const char* getMqttClientId();
    static bool isMqttConfigKnown();
    static String getMqttServerString();
};
```

#### 3.2 API Handlers (`api_handlers.cpp`)

**New Endpoints**:

```cpp
// GET /api/get_mqtt_config
static esp_err_t api_get_mqtt_config_handler(httpd_req_t *req) {
    char json[512];
    
    if (!TransmitterManager::isMqttConfigKnown()) {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"MQTT config not cached\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    const uint8_t* server = TransmitterManager::getMqttServer();
    snprintf(json, sizeof(json),
        "{\"success\":true,"
        "\"enabled\":%s,"
        "\"server\":\"%d.%d.%d.%d\","
        "\"port\":%d,"
        "\"username\":\"%s\","
        "\"password\":\"%s\","
        "\"client_id\":\"%s\"}",
        TransmitterManager::isMqttEnabled() ? "true" : "false",
        server[0], server[1], server[2], server[3],
        TransmitterManager::getMqttPort(),
        TransmitterManager::getMqttUsername(),
        "********",  // Mask password in UI
        TransmitterManager::getMqttClientId()
    );
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// POST /api/request_mqtt_config
static esp_err_t api_request_mqtt_config_handler(httpd_req_t *req) {
    // Send msg_mqtt_config_request via ESP-NOW
    // Similar to api_request_network_config_handler
}

// POST /api/save_mqtt_config
static esp_err_t api_save_mqtt_config_handler(httpd_req_t *req) {
    // Parse JSON body
    // Validate inputs
    // Build mqtt_config_update_t message
    // Send via ESP-NOW
    // Return success/failure
}
```

**Register endpoints**:
```cpp
{.uri = "/api/get_mqtt_config", .method = HTTP_GET, .handler = api_get_mqtt_config_handler, .user_ctx = NULL},
{.uri = "/api/request_mqtt_config", .method = HTTP_POST, .handler = api_request_mqtt_config_handler, .user_ctx = NULL},
{.uri = "/api/save_mqtt_config", .method = HTTP_POST, .handler = api_save_mqtt_config_handler, .user_ctx = NULL},
```

#### 3.3 ESP-NOW Message Receiver (`espnow_receiver.cpp` or equivalent)

**Add handler for `msg_mqtt_config_ack`**:
```cpp
case msg_mqtt_config_ack: {
    const mqtt_config_ack_t* ack = (const mqtt_config_ack_t*)data;
    
    if (ack->success) {
        TransmitterManager::storeMqttConfig(
            ack->enabled,
            ack->server,
            ack->port,
            ack->username,
            ack->password,
            ack->client_id,
            ack->config_version
        );
        LOG_INFO("MQTT config cached from transmitter");
    }
    break;
}
```

---

### Phase 4: Web UI Implementation

#### 4.1 HTML Structure (`settings_page.cpp`)

**Current** (disabled fields):
```html
<input type='checkbox' id='mqttEnabled' checked disabled />
<input type='text' value='192.168.1.221' disabled />
```

**New** (4-quad IP input + enabled fields):
```html
<div class='settings-row'>
    <label>MQTT Enabled:</label>
    <input type='checkbox' id='mqttEnabled' />
</div>
<div class='settings-row' id='mqttServerRow'>
    <label>MQTT Server:</label>
    <div class='ip-input-group'>
        <input type='number' id='mqtt_server_0' min='0' max='255' class='ip-quad' />
        <span>.</span>
        <input type='number' id='mqtt_server_1' min='0' max='255' class='ip-quad' />
        <span>.</span>
        <input type='number' id='mqtt_server_2' min='0' max='255' class='ip-quad' />
        <span>.</span>
        <input type='number' id='mqtt_server_3' min='0' max='255' class='ip-quad' />
    </div>
</div>
<div class='settings-row' id='mqttPortRow'>
    <label>MQTT Port:</label>
    <input type='number' id='mqttPort' min='1' max='65535' />
</div>
<div class='settings-row' id='mqttUserRow'>
    <label>MQTT User:</label>
    <input type='text' id='mqttUsername' maxlength='31' />
</div>
<div class='settings-row' id='mqttPasswordRow'>
    <label>MQTT Password:</label>
    <input type='password' id='mqttPassword' maxlength='31' />
</div>
<div class='settings-row' id='mqttClientIdRow'>
    <label>MQTT Client ID:</label>
    <input type='text' id='mqttClientId' maxlength='31' />
</div>
```

#### 4.2 JavaScript Functions (`settings_page.cpp`)

**Update TRANSMITTER_CONFIG_FIELDS**:
```javascript
const TRANSMITTER_CONFIG_FIELDS = [
    // Network Configuration
    'staticIpEnabled',
    'ip0', 'ip1', 'ip2', 'ip3',
    // ... DNS fields ...
    
    // MQTT Configuration
    'mqttEnabled',
    'mqtt_server_0', 'mqtt_server_1', 'mqtt_server_2', 'mqtt_server_3',
    'mqttPort',
    'mqttUsername',
    'mqttPassword',
    'mqttClientId'
];
```

**Load MQTT Config Function**:
```javascript
async function loadMqttConfig() {
    try {
        const response = await fetch('/api/get_mqtt_config');
        const data = await response.json();
        
        if (!data.success) {
            console.log('MQTT config cache empty - requesting from transmitter');
            await fetch('/api/request_mqtt_config', { method: 'POST' });
            await new Promise(resolve => setTimeout(resolve, 2000));
            
            const retryResponse = await fetch('/api/get_mqtt_config');
            const retryData = await retryResponse.json();
            
            if (retryData.success) {
                populateMqttConfig(retryData);
            }
            return;
        }
        
        populateMqttConfig(data);
    } catch (error) {
        console.error('Failed to load MQTT config:', error);
    }
}

function populateMqttConfig(data) {
    document.getElementById('mqttEnabled').checked = data.enabled;
    
    const serverParts = data.server.split('.');
    for (let i = 0; i < 4; i++) {
        document.getElementById('mqtt_server_' + i).value = serverParts[i];
    }
    
    document.getElementById('mqttPort').value = data.port;
    document.getElementById('mqttUsername').value = data.username;
    // Don't populate password - keep masked
    document.getElementById('mqttClientId').value = data.client_id;
    
    updateMqttVisibility();
}
```

**Update Toggle Function**:
```javascript
function updateMqttVisibility() {
    const enabled = document.getElementById('mqttEnabled').checked;
    const rows = ['mqttServerRow', 'mqttPortRow', 'mqttUserRow', 
                  'mqttPasswordRow', 'mqttClientIdRow'];
    
    rows.forEach(rowId => {
        const row = document.getElementById(rowId);
        if (row) {
            row.style.display = enabled ? 'flex' : 'none';
        }
    });
}
```

**Save Function** (add to existing save):
```javascript
async function saveMqttConfig() {
    const config = {
        enabled: document.getElementById('mqttEnabled').checked,
        server: `${document.getElementById('mqtt_server_0').value}.` +
                `${document.getElementById('mqtt_server_1').value}.` +
                `${document.getElementById('mqtt_server_2').value}.` +
                `${document.getElementById('mqtt_server_3').value}`,
        port: parseInt(document.getElementById('mqttPort').value),
        username: document.getElementById('mqttUsername').value,
        password: document.getElementById('mqttPassword').value,
        client_id: document.getElementById('mqttClientId').value
    };
    
    const response = await fetch('/api/save_mqtt_config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(config)
    });
    
    const result = await response.json();
    // Handle success/error
}
```

**Initialize on Page Load**:
```javascript
window.addEventListener('DOMContentLoaded', function() {
    // ... existing initialization ...
    
    // Load MQTT config
    loadMqttConfig();
    
    // Attach MQTT enabled checkbox listener
    const mqttCheckbox = document.getElementById('mqttEnabled');
    if (mqttCheckbox) {
        mqttCheckbox.addEventListener('change', updateMqttVisibility);
    }
});
```

---

## Suggestions for Improvement

### 1. **Password Security** ✅ APPROVED
- **Implementation**: Store password in plaintext in NVS, display as `"********"` in UI
- **Approach**: 
  - API returns `"********"` mask instead of actual password
  - Only update password if user enters a new value (detect if field changed from "********")
  - Password stored in text format on transmitter
  - Future consideration: ESP32 flash encryption for enhanced security

### 2. **MQTT Connection Status** 🎯 ENHANCED
- **Implementation with Hot-Reload**: 
  - After config save and hot-reload, transmitter reports connection status
  - Add `mqtt_connected` boolean to mqtt_config_ack message
  - Display connection status badge in UI (🟢 Connected / 🔴 Disconnected)
  - Immediate feedback after applying new configuration
  - Future: Add periodic status updates via existing status messages

### 3. **Port Validation** ✅ APPROVED
- **Decision**: Port is user-selectable with default value of 1883
- **Implementation**: 
  - Number input field with default value `1883`
  - Client-side validation (1-65535)
  - Server-side validation before saving
  ```javascript
  if (port < 1 || port > 65535) {
      alert('Port must be between 1 and 65535');
      return;
  }
  ```

### 4. **IP Address Validation**
- **Suggestion**: Validate IP is not 0.0.0.0 or 255.255.255.255
  ```javascript
  if (server === '0.0.0.0' || server === '255.255.255.255') {
      alert('Invalid MQTT server address');
      return;
  }
  ```

### 5. **Unified Save Button** ✅ APPROVED
- **Decision**: Single unified "Save Transmitter Config" button for all sections
- **Implementation**: 
  - One save button that detects changes across Network, MQTT, Battery, and future sections
  - Change counter aggregates all modified fields
  - Sends multiple ESP-NOW messages if needed (network + MQTT)
  - Simplifies UI and makes future section additions easier

### 6. **Topics Configuration** ⏸️ DEFERRED
- **Decision**: Keep MQTT topics hardcoded for now
- **Reason**: Simplifies initial implementation, can be added later if needed
- **Current topics remain**:
  - Data: `espnow/transmitter/data`
  - Status: `espnow/transmitter/status`
  - OTA: `espnow/transmitter/ota`

### 7. **Hot-Reload MQTT Client** ✅ APPROVED
- **Decision**: Implement hot-reload of MQTT client without reboot
- **Implementation**: 
  - After saving MQTT config, transmitter disconnects existing MQTT client
  - Reinitializes MQTT client with new settings
  - Reconnects to broker with updated configuration
  - No device reboot required
  - Provides immediate feedback on configuration changes

### 8. **Connection Test** ⏸️ NOT NEEDED
- **Decision**: Not implementing pre-save test
- **Reason**: Hot-reload provides immediate feedback after save
  - User can see connection status immediately after applying config
  - Failed connections won't break anything (config still saved)
  - Simpler implementation without test-before-save logic

### 9. **Default Values**
- **Suggestion**: Add "Restore Defaults" button that resets to hardcoded values from `network_config.h`

### 10. **Visual Consistency**
- **Suggestion**: Apply same Material Design styling as network config:
  - Readonly fields when MQTT disabled
  - Color-coded indicators
  - Smooth transitions

---

## Implementation Order

### Priority 1: Core Functionality
1. ESP-NOW message structures
2. Transmitter MqttManager class
3. Transmitter message handlers
4. Receiver API endpoints
5. Basic UI with 4-quad IP input

### Priority 2: Polish
6. Load/save JavaScript functions
7. Visibility toggling
8. Change tracking integration
9. Validation
10. Hot-reload implementation

### Priority 3: Enhancements (Future)
11. Connection status display with live feedback
12. Restore defaults button
13. Visual polish and Material Design consistency

**Note**: Test connection feature NOT needed (hot-reload provides immediate feedback)

---

## Testing Checklist

- [ ] MQTT enabled checkbox toggles visibility
- [ ] 4-quad IP input accepts valid IPs (0-255 per quad)
- [ ] Port accepts valid range (1-65535)
- [ ] Empty cache triggers auto-request on page load
- [ ] Save sends ESP-NOW message to transmitter
- [ ] Transmitter saves config to NVS
- [ ] Transmitter ACK updates receiver cache
- [ ] Reload page shows saved values
- [ ] Disable MQTT → fields hidden, MQTT client stops
- [ ] Enable MQTT → fields shown, MQTT client starts
- [ ] Invalid IP rejected (0.0.0.0, 255.255.255.255)
- [ ] Password masked in API response
- [ ] Change tracking counts MQTT fields correctly
- [ ] Save button updates text appropriately
- [ ] Hot-reload applies new MQTT config without reboot
- [ ] MQTT client reconnects with new settings
- [ ] Connection status reported after hot-reload
- [ ] Unified save button handles multiple config sections

---

## Files to Modify

### Transmitter
1. `espnow_common.h` - Add message structures and types
2. `mqtt_manager.h` (NEW) - MQTT config manager
3. `mqtt_manager.cpp` (NEW) - Implementation
4. `message_handler.h` - Add handler declarations
5. `message_handler.cpp` - Add handlers and routing
6. `main.cpp` - Initialize MQTT manager

### Receiver
7. `transmitter_manager.h` - Add MQTT data fields
8. `transmitter_manager.cpp` - Add MQTT storage methods
9. `api_handlers.cpp` - Add 3 new endpoints
10. `settings_page.cpp` - Update HTML and JavaScript
11. `espnow_receiver.cpp` (or equivalent) - Handle mqtt_config_ack

### Estimated Lines of Code
- **New code**: ~600 lines
- **Modified code**: ~200 lines
- **Total effort**: 4-6 hours

---

## User Decisions ✅

1. **Password handling**: ✅ Save in plaintext, display as `"********"` in UI
2. **Save button**: ✅ Unified "Save Transmitter Config" button for all sections
3. **Topics**: ⏸️ Keep hardcoded (can add later if needed)
4. **MQTT reload**: ✅ Hot-reload MQTT client without device reboot
5. **Port**: ✅ User-selectable, default to 1883

---

## Implementation Approach

### Unified Save Button Architecture
```javascript
async function saveTransmitterConfig() {
    // Detect changes in all sections
    const networkChanged = hasNetworkChanges();
    const mqttChanged = hasMqttChanges();
    const batteryChanged = hasBatteryChanges(); // Future
    
    // Send messages for changed sections only
    if (networkChanged) await saveNetworkConfig();
    if (mqttChanged) await saveMqttConfig();
    if (batteryChanged) await saveBatteryConfig(); // Future
    
    // Update button state based on all results
}
```

### Hot-Reload Implementation
```cpp
void MqttManager::applyConfig() {
    // Disconnect existing client
    if (mqttClient.connected()) {
        mqttClient.disconnect();
        LOG_INFO("[MQTT] Disconnected for config reload");
    }
    
    // Apply new configuration
    mqttClient.setServer(server_, port_);
    if (strlen(username_) > 0) {
        mqttClient.setCredentials(username_, password_);
    }
    mqttClient.setClientId(client_id_);
    
    // Reconnect with new settings
    if (enabled_) {
        mqttClient.connect();
        LOG_INFO("[MQTT] Reconnecting with new config");
    }
}
```

---

**✅ APPROVED - Ready to proceed with implementation.**
