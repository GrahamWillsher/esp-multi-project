# Static Data Storage Implementation Plan
**Author**: AI Assistant  
**Date**: February 11, 2026  
**Status**: Proposed for Review  
**Related Document**: `Saving additional static data.md`

---

## Executive Summary

This document outlines the implementation plan for storing additional static configuration data in both the **Receiver** (WiFi settings) and **Transmitter** (extended configuration settings). The approach follows the established pattern already implemented for MQTT and Network configuration on the transmitter.

### Scope
1. **Receiver**: Add NVS storage for WiFi configuration (hostname, SSID, password, IP mode, static IP settings)
2. **Transmitter**: Add NVS storage for remaining configuration sections (Battery, Power, Inverter, CAN, Contactor settings)
3. Both devices will use **cache-first, then NVS** pattern for consistency

---

## Part 1: Receiver WiFi Configuration Storage

### 1.1 Problem Statement

Currently, the receiver's WiFi settings are hardcoded in `config_receiver.h`:
- Hostname
- WiFi SSID
- WiFi Password  
- Static IP configuration (IP, Gateway, Subnet, DNS)

**Goal**: Make these editable via web UI and persistent across reboots, with **no hardcoded defaults**.

**Approach**: Use Access Point (AP) mode for initial setup:
1. Fresh device → boots in AP mode (SSID: "ESP32-Receiver-Setup")
2. User connects to AP and enters credentials via web interface
3. Credentials saved to NVS and device reboots
4. Device connects to configured WiFi network
5. Subsequent boots use NVS credentials
6. If connection fails → automatic fallback to AP mode

**Key Benefits**:
- ✅ No hardcoded WiFi credentials in source code
- ✅ Secure initial setup process
- ✅ Standard IoT device setup pattern
- ✅ Automatic recovery if credentials become invalid
- ✅ Optional captive portal for seamless UX

### 1.2 Architecture Overview

Following the transmitter's MQTT/Network config pattern:

```
┌─────────────────┐
│   Web UI Page   │  (New: /receiver/network)
│  WiFi Settings  │
└────────┬────────┘
         │ POST /api/save_receiver_network
         v
┌─────────────────┐
│ ReceiverConfig  │  ← In-memory cache
│    Manager      │
└────────┬────────┘
         │ Persist
         v
┌─────────────────┐
│  NVS Storage    │
│ "rx_net_cfg"    │
└─────────────────┘
```

### 1.3 Implementation Components

#### 1.3.1 New Class: `ReceiverConfigManager`

**Location**: `espnowreciever_2/lib/webserver/utils/receiver_config_manager.h/cpp`

**Responsibilities**:
- Load WiFi configuration from NVS on boot
- Provide cached access to current configuration
- Save configuration changes to NVS
- Apply configuration changes (requires reboot for WiFi changes)

**Static Members**:
```cpp
class ReceiverConfigManager {
private:
    // WiFi Configuration
    static char hostname_[32];
    static char ssid_[32];
    static char password_[64];  // Plain text storage acceptable per requirements
    
    // IP Configuration
    static bool use_static_ip_;
    static uint8_t static_ip_[4];
    static uint8_t gateway_[4];
    static uint8_t subnet_[4];
    static uint8_t dns_primary_[4];
    static uint8_t dns_secondary_[4];
    
    static uint32_t config_version_;
    
    // NVS Keys
    static constexpr const char* NVS_NAMESPACE = "rx_net_cfg";
    static constexpr const char* NVS_KEY_HOSTNAME = "hostname";
    static constexpr const char* NVS_KEY_SSID = "ssid";
    static constexpr const char* NVS_KEY_PASSWORD = "password";
    static constexpr const char* NVS_KEY_USE_STATIC = "use_static";
    static constexpr const char* NVS_KEY_IP = "ip";
    static constexpr const char* NVS_KEY_GATEWAY = "gateway";
    static constexpr const char* NVS_KEY_SUBNET = "subnet";
    static constexpr const char* NVS_KEY_DNS1 = "dns1";
    static constexpr const char* NVS_KEY_DNS2 = "dns2";
    static constexpr const char* NVS_KEY_VERSION = "version";

public:
    // Load from NVS on boot
    static bool loadConfig();
    
    // Save to NVS
    static bool saveConfig(const char* hostname, const char* ssid, const char* password,
                          bool use_static, const uint8_t* ip, const uint8_t* gateway,
                          const uint8_t* subnet, const uint8_t* dns1, const uint8_t* dns2);
    
    // Getters for cached values
    static const char* getHostname() { return hostname_; }
    static const char* getSSID() { return ssid_; }
    static const char* getPassword() { return password_; }
    static bool useStaticIP() { return use_static_ip_; }
    static const uint8_t* getStaticIP() { return static_ip_; }
    static const uint8_t* getGateway() { return gateway_; }
    static const uint8_t* getSubnet() { return subnet_; }
    static const uint8_t* getDNSPrimary() { return dns_primary_; }
    static const uint8_t* getDNSSecondary() { return dns_secondary_; }
    static uint32_t getVersion() { return config_version_; }
    
    // Apply configuration (requires WiFi reconnect/reboot)
    static void applyConfig();
};
```

#### 1.3.2 Integration with WiFi Setup

**File**: `espnowreciever_2/src/config/wifi_setup.cpp`

**Changes**:
```cpp
void setupWiFi() {
    // Load configuration from ReceiverConfigManager
    if (!ReceiverConfigManager::loadConfig()) {
        LOG_WARN("[WIFI] No saved config found - starting in AP mode for initial setup");
        startAPMode();
        return;  // Stay in AP mode until credentials configured
    }
    
    // Validate essential credentials exist
    if (strlen(ReceiverConfigManager::getSSID()) == 0) {
        LOG_ERROR("[WIFI] SSID is empty - starting in AP mode");
        startAPMode();
        return;
    }
    
    // Normal WiFi connection mode
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(ReceiverConfigManager::getHostname());
    
    if (ReceiverConfigManager::useStaticIP()) {
        IPAddress ip(ReceiverConfigManager::getStaticIP());
        IPAddress gateway(ReceiverConfigManager::getGateway());
        IPAddress subnet(ReceiverConfigManager::getSubnet());
        IPAddress dns1(ReceiverConfigManager::getDNSPrimary());
        IPAddress dns2(ReceiverConfigManager::getDNSSecondary());
        
        WiFi.config(ip, gateway, subnet, dns1, dns2);
        LOG_INFO("[WIFI] Configured static IP: %s", ip.toString().c_str());
    } else {
        LOG_INFO("[WIFI] Using DHCP");
    }
    
    WiFi.begin(ReceiverConfigManager::getSSID(), ReceiverConfigManager::getPassword());
    LOG_INFO("[WIFI] Connecting to SSID: %s", ReceiverConfigManager::getSSID());
    
    // Wait for connection with timeout
    int timeout = 30;  // 30 seconds
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
        delay(1000);
        timeout--;
        LOG_DEBUG("[WIFI] Connecting... %d", timeout);
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        LOG_ERROR("[WIFI] Failed to connect - falling back to AP mode");
        startAPMode();
    } else {
        LOG_INFO("[WIFI] Connected! IP: %s", WiFi.localIP().toString().c_str());
    }
}

void startAPMode() {
    LOG_INFO("[WIFI] Starting Access Point mode");
    
    // AP credentials
    const char* ap_ssid = "ESP32-Receiver-Setup";
    const char* ap_password = "setup1234";  // Minimum 8 characters for WPA2
    
    // Set AP IP configuration
    IPAddress ap_ip(192, 168, 4, 1);
    IPAddress ap_gateway(192, 168, 4, 1);
    IPAddress ap_subnet(255, 255, 255, 0);
    
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(ap_ip, ap_gateway, ap_subnet);
    WiFi.softAP(ap_ssid, ap_password);
    
    LOG_INFO("[WIFI] AP Started: %s", ap_ssid);
    LOG_INFO("[WIFI] AP Password: %s", ap_password);
    LOG_INFO("[WIFI] AP IP: %s", WiFi.softAPIP().toString().c_str());
    LOG_INFO("[WIFI] Connect to this AP and navigate to http://192.168.4.1");
    
    // Web server will show setup page automatically
    // See section 1.3.3 for setup page implementation
}
```

#### 1.3.3 Web UI Page

**New Page**: `/receiver/network` (also serves as AP setup page)

**Location**: `espnowreciever_2/lib/webserver/pages/receiver_network_page.cpp`

**Features**:
- **AP Setup Mode**: Simplified setup page when in AP mode
  - Basic WiFi credential input (SSID, password)
  - Optional static IP configuration
  - Save and Reboot button
  - Clear instructions for first-time setup
  
- **Normal Mode**: Full network configuration page
  - Display current WiFi settings
  - Edit hostname, SSID, password
  - Toggle static/DHCP IP mode
  - Configure static IP settings
  - Save button with validation
  - Warning: "Changes require device reboot to take effect"

**HTML Structure (AP Mode)**:
```html
<div class='settings-card'>
    <h1>Welcome to ESP32 Receiver Setup</h1>
    <p>This is a first-time setup. Please configure your WiFi network.</p>
    
    <div style='background: #ff9800; padding: 15px; border-radius: 5px; margin: 20px 0;'>
        <strong>⚠️ Initial Setup Required</strong><br>
        No WiFi configuration found. Please enter your network credentials below.
        The device will reboot after saving and connect to your network.
    </div>
    
    <h3>WiFi Network Credentials</h3>
    <div class='settings-row'>
        <label>Network SSID:</label>
        <input type='text' id='ssid' placeholder='Your WiFi Network Name' required />
    </div>
    <div class='settings-row'>
        <label>WiFi Password:</label>
        <input type='password' id='password' placeholder='WiFi Password' required />
    </div>
    
    <div class='settings-row'>
        <label>Device Hostname:</label>
        <input type='text' id='hostname' value='ESP32-Receiver' />
    </div>
    
    <h3>IP Configuration (Optional)</h3>
    <div class='settings-row'>
        <label>Use Static IP:</label>
        <input type='checkbox' id='useStatic' onchange='toggleIPFields()' />
        <small style='color: #888;'>(Leave unchecked for DHCP - recommended for first setup)</small>
    </div>
    
    <div id='staticIPFields' style='display: none;'>
        <!-- IP, Gateway, Subnet, DNS fields -->
    </div>
    
    <button onclick='saveSetupConfig()' class='button' style='background: #4CAF50; font-size: 18px; padding: 15px 40px;'>
        💾 Save Configuration and Reboot
    </button>
    
    <div class='note' style='margin-top: 20px; color: #888;'>
        After saving, the device will reboot and connect to your WiFi network.<br>
        You will need to reconnect to your main network and access the device at its new IP address.
    </div>
</div>

<script>
async function saveSetupConfig() {
    const ssid = document.getElementById('ssid').value;
    const password = document.getElementById('password').value;
    const hostname = document.getElementById('hostname').value;
    const useStatic = document.getElementById('useStatic').checked;
    
    // Validation
    if (!ssid || ssid.length === 0) {
        alert('Please enter WiFi SSID');
        return;
    }
    
    if (!password || password.length < 8) {
        alert('WiFi password must be at least 8 characters');
        return;
    }
    
    // Build config object
    const config = {
        hostname: hostname,
        ssid: ssid,
        password: password,
        use_static: useStatic,
        ip: useStatic ? getIPFromFields('ip') : '0.0.0.0',
        gateway: useStatic ? getIPFromFields('gw') : '0.0.0.0',
        subnet: useStatic ? getIPFromFields('sn') : '255.255.255.0',
        dns1: useStatic ? getIPFromFields('dns1') : '0.0.0.0',
        dns2: useStatic ? getIPFromFields('dns2') : '0.0.0.0'
    };
    
    try {
        const response = await fetch('/api/save_receiver_network', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        });
        
        const result = await response.json();
        
        if (result.success) {
            alert('Configuration saved! Device will reboot in 3 seconds.\\n\\n' +
                  'Please reconnect to your main WiFi network and access the device at its new IP address.');
            
            // Show countdown
            let countdown = 3;
            const countdownInterval = setInterval(() => {
                countdown--;
                if (countdown <= 0) {
                    clearInterval(countdownInterval);
                    window.location.href = '/';  // Will fail as device reboots, but good practice
                }
            }, 1000);
        } else {
            alert('Failed to save configuration: ' + result.message);
        }
    } catch (error) {
        alert('Error saving configuration: ' + error.message);
    }
}
</script>
```

#### 1.3.4 API Endpoints

**File**: `espnowreciever_2/lib/webserver/api/api_handlers.cpp`

**New Endpoints**:

1. **GET `/api/get_receiver_network`**
   - Returns current cached WiFi configuration as JSON
   - Fields: hostname, ssid, password, use_static, ip, gateway, subnet, dns1, dns2
   - Also returns `is_ap_mode` flag to indicate if currently in AP mode

2. **POST `/api/save_receiver_network`**
   - Validates input (SSID not empty, valid IP addresses if static)
   - Calls `ReceiverConfigManager::saveConfig()`
   - Schedules reboot after 3 seconds
   - Returns success/error response

**Example Implementation**:
```cpp
static esp_err_t api_save_receiver_network_handler(httpd_req_t *req) {
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return send_json_error(req, "Failed to read request");
    }
    buf[ret] = '\0';
    
    // Parse JSON
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, buf)) {
        return send_json_error(req, "Invalid JSON");
    }
    
    // Extract and validate fields
    const char* hostname = doc["hostname"] | "ESP32-Receiver";
    const char* ssid = doc["ssid"] | "";
    const char* password = doc["password"] | "";
    bool use_static = doc["use_static"] | false;
    
    // Validate SSID (required)
    if (strlen(ssid) == 0) {
        return send_json_error(req, "SSID cannot be empty");
    }
    
    // Validate password (WPA2 minimum 8 characters)
    if (strlen(password) < 8) {
        return send_json_error(req, "Password must be at least 8 characters");
    }
    
    // Parse IP addresses if static mode
    IPAddress ip, gateway, subnet, dns1, dns2;
    if (use_static) {
        if (!ip.fromString(doc["ip"]) || !gateway.fromString(doc["gateway"]) ||
            !subnet.fromString(doc["subnet"])) {
            return send_json_error(req, "Invalid IP address format");
        }
        dns1.fromString(doc["dns1"] | "8.8.8.8");
        dns2.fromString(doc["dns2"] | "8.8.4.4");
    }
    
    // Convert to arrays
    uint8_t ip_arr[4] = {ip[0], ip[1], ip[2], ip[3]};
    uint8_t gw_arr[4] = {gateway[0], gateway[1], gateway[2], gateway[3]};
    uint8_t sn_arr[4] = {subnet[0], subnet[1], subnet[2], subnet[3]};
    uint8_t dns1_arr[4] = {dns1[0], dns1[1], dns1[2], dns1[3]};
    uint8_t dns2_arr[4] = {dns2[0], dns2[1], dns2[2], dns2[3]};
    
    // Save to NVS
    if (!ReceiverConfigManager::saveConfig(hostname, ssid, password, use_static,
                                           ip_arr, gw_arr, sn_arr, dns1_arr, dns2_arr)) {
        return send_json_error(req, "Failed to save configuration to NVS");
    }
    
    LOG_INFO("[API] WiFi configuration saved - scheduling reboot in 3 seconds");
    
    // Schedule reboot
    static bool reboot_scheduled = false;
    if (!reboot_scheduled) {
        reboot_scheduled = true;
        xTaskCreate([](void*) {
            vTaskDelay(pdMS_TO_TICKS(3000));
            LOG_INFO("[API] Rebooting to apply WiFi configuration...");
            ESP.restart();
        }, "reboot", 2048, NULL, 1, NULL);
    }
    
    return send_json_success(req, "Configuration saved - rebooting");
}
```

**Example Response**:
```json
{
  "success": true,
  "hostname": "ESP32-Receiver",
  "ssid": "YourWiFi",
  "password": "YourPassword",
  "use_static": false,
  "ip": "0.0.0.0",
  "gateway": "0.0.0.0",
  "subnet": "255.255.255.0",
  "dns1": "0.0.0.0",
  "dns2": "0.0.0.0",
  "is_ap_mode": false
}
```

#### 1.3.5 Page Registration

**File**: `espnowreciever_2/lib/webserver/page_definitions.cpp`

Add to PAGE_DEFINITIONS:
```cpp
{ "/receiver/network",  "Network Settings",  subtype_none,  false },
```

Add navigation link in `/receiver/config` (systeminfo_page.cpp).

### 1.3.6 AP Mode Compatibility

**ESP32 AP Mode Support**: ✅ Fully supported in ESP32 Arduino core

**Features Used**:
- `WiFi.mode(WIFI_AP)` - Set Access Point mode
- `WiFi.softAP(ssid, password)` - Create AP with WPA2 security
- `WiFi.softAPConfig(ip, gateway, subnet)` - Configure AP IP
- `WiFi.softAPIP()` - Get AP IP address
- Built-in DHCP server (automatically serves 192.168.4.2-192.168.4.254)

**DNS Captive Portal** (Optional Enhancement):
```cpp
#include <DNSServer.h>

DNSServer dnsServer;
const byte DNS_PORT = 53;

void startAPMode() {
    // ... existing AP setup ...
    
    // Start DNS server to redirect all requests to AP IP (captive portal)
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    LOG_INFO("[DNS] Captive portal started - all DNS queries redirect to %s", 
             WiFi.softAPIP().toString().c_str());
}

void loop() {
    if (WiFi.getMode() == WIFI_AP) {
        dnsServer.processNextRequest();  // Handle DNS in loop
    }
    // ... rest of loop
}
```

**Benefits of DNS Captive Portal**:
- User types any URL → automatically redirected to setup page
- Better UX - no need to remember 192.168.4.1
- Standard "captive portal" behavior like hotels/airports
- Works on all devices (phones, tablets, laptops)

**Library Dependency**:
```ini
# platformio.ini
lib_deps =
    ...
    DNSServer  # Built-in ESP32 library, no version needed
```

**mDNS Alternative** (For Normal Mode):
```cpp
#include <ESPmDNS.h>

void setupWiFi() {
    // ... after WiFi connected ...
    
    if (WiFi.status() == WL_CONNECTED) {
        // Start mDNS responder
        if (MDNS.begin(ReceiverConfigManager::getHostname())) {
            MDNS.addService("http", "tcp", 80);
            LOG_INFO("[mDNS] Responder started: http://%s.local", 
                     ReceiverConfigManager::getHostname());
        }
    }
}
```

**User Access Methods**:
1. **AP Mode**: http://192.168.4.1 or any URL (if captive portal enabled)
2. **Normal Mode (DHCP)**: http://[hostname].local (if mDNS enabled)
3. **Normal Mode (Static IP)**: http://[configured-ip]

**Compatibility Matrix**:

| Feature | ESP32-S3 | ESP32 | T-Display-S3 | Required Library |
|---------|----------|-------|--------------|------------------|
| WiFi AP Mode | ✅ | ✅ | ✅ | WiFi (built-in) |
| WPA2 Security | ✅ | ✅ | ✅ | WiFi (built-in) |
| Static IP | ✅ | ✅ | ✅ | WiFi (built-in) |
| DNS Server | ✅ | ✅ | ✅ | DNSServer (built-in) |
| mDNS | ✅ | ✅ | ✅ | ESPmDNS (built-in) |
| NVS Storage | ✅ | ✅ | ✅ | Preferences (built-in) |
| Dual DNS | ✅ | ✅ | ✅ | WiFi.config() supports 2 DNS |

**All features are compatible** with T-Display-S3 (ESP32-S3) and standard ESP32 devices.

### 1.4 Implementation Sequence (Receiver)

1. Create `ReceiverConfigManager` class (header + implementation)
2. Add NVS load/save methods with array-based IP storage
3. Add AP mode support in `wifi_setup.cpp`
4. Create `/receiver/network` page with AP setup mode detection
5. Add API endpoints for get/save receiver network config
6. Implement auto-reboot after save
7. Add navigation links to new page
8. Test complete flow:
   - Fresh install → AP mode → enter credentials → reboot → normal operation
   - Normal mode → change credentials → reboot → verify changes
   - Failed connection → fallback to AP mode

---

## Part 2: Transmitter Extended Configuration Storage

### 2.1 Problem Statement

The transmitter currently stores:
- ✅ MQTT configuration (via `MqttConfigManager`)
- ✅ Network configuration (via EthernetConfig + NVS)

**Missing NVS Storage**:
- Battery Configuration (capacity, voltage limits, cell count, chemistry)
- Power Settings (charge/discharge power, precharge settings)
- Inverter Configuration (cells, modules, voltage, capacity, type)
- CAN Configuration (frequency, FD frequency, Sofar ID, Pylon interval)
- Contactor Control (enable, NC mode, PWM frequency)

**Goal**: Make all settings editable via receiver's web UI and persistent across transmitter reboots.

### 2.2 Architecture Overview

Extend the existing `EnhancedCache` pattern:

```
┌──────────────────────┐
│  Receiver Web UI     │
│ /transmitter/config  │
└──────────┬───────────┘
           │ ESP-NOW Message
           v
┌──────────────────────┐
│  Transmitter         │
│  Message Handler     │
└──────────┬───────────┘
           │ Save to NVS
           v
┌──────────────────────┐
│  Enhanced Cache      │
│  (Memory + NVS)      │
└──────────────────────┘
```

### 2.3 New Configuration Structures

**File**: `esp32common/espnow_common.h`

Add new structures for each configuration section:

```cpp
// Battery Configuration
struct battery_config_t {
    msg_type type;                    // msg_battery_config_update
    uint32_t capacity_wh;             // Battery capacity in Wh
    uint32_t max_voltage_mv;          // Maximum voltage in mV
    uint32_t min_voltage_mv;          // Minimum voltage in mV
    float max_charge_current_a;       // Maximum charge current in A
    float max_discharge_current_a;    // Maximum discharge current in A
    uint8_t soc_high_limit;           // SOC high limit %
    uint8_t soc_low_limit;            // SOC low limit %
    uint8_t cell_count;               // Number of cells
    uint8_t chemistry;                // Battery chemistry type
    uint16_t checksum;                // Validation checksum
} __attribute__((packed));

// Power Settings Configuration
struct power_settings_config_t {
    msg_type type;                    // msg_power_settings_update
    uint32_t charge_power_w;          // Charge power in W
    uint32_t discharge_power_w;       // Discharge power in W
    uint32_t max_precharge_time_ms;   // Max precharge time
    uint32_t precharge_duration_ms;   // Precharge duration
    uint16_t checksum;
} __attribute__((packed));

// Inverter Configuration
struct inverter_config_t {
    msg_type type;                    // msg_inverter_config_update
    uint16_t cells;                   // Number of cells
    uint16_t modules;                 // Number of modules
    uint16_t cells_per_module;        // Cells per module
    uint32_t voltage_level_mv;        // Voltage level in mV
    uint32_t capacity_ah;             // Capacity in Ah
    uint8_t battery_type;             // Battery type code
    uint16_t checksum;
} __attribute__((packed));

// CAN Configuration
struct can_config_t {
    msg_type type;                    // msg_can_config_update
    uint32_t frequency_khz;           // CAN frequency in kHz
    uint32_t fd_frequency_mhz;        // CAN FD frequency in MHz
    uint16_t sofar_inverter_id;       // Sofar inverter ID
    uint32_t pylon_send_interval_ms;  // Pylon send interval
    uint16_t checksum;
} __attribute__((packed));

// Contactor Control Configuration
struct contactor_config_t {
    msg_type type;                    // msg_contactor_config_update
    bool control_enabled;             // Contactor control enabled
    bool nc_contactor;                // Normally closed contactor
    uint32_t pwm_frequency_hz;        // PWM frequency in Hz
    uint16_t checksum;
} __attribute__((packed));
```

### 2.4 Message Types

Add to `enum msg_type` in `espnow_common.h`:

```cpp
// Configuration update messages
msg_battery_config_request = 0x60,
msg_battery_config_update = 0x61,
msg_battery_config_ack = 0x62,

msg_power_settings_request = 0x63,
msg_power_settings_update = 0x64,
msg_power_settings_ack = 0x65,

msg_inverter_config_request = 0x66,
msg_inverter_config_update = 0x67,
msg_inverter_config_ack = 0x68,

msg_can_config_request = 0x69,
msg_can_config_update = 0x6A,
msg_can_config_ack = 0x6B,

msg_contactor_config_request = 0x6C,
msg_contactor_config_update = 0x6D,
msg_contactor_config_ack = 0x6E,
```

### 2.5 Enhanced Cache Extension

**File**: `ESPnowtransmitter2/src/espnow/enhanced_cache.h`

Add new cache data types:

```cpp
enum class CacheDataType : uint8_t {
    STATE_NETWORK = 0,
    STATE_MQTT = 1,
    STATE_BATTERY = 2,
    STATE_POWER_SETTINGS = 3,      // NEW
    STATE_INVERTER = 4,            // NEW
    STATE_CAN = 5,                 // NEW
    STATE_CONTACTOR = 6,           // NEW
    MAX_DATA_TYPES
};
```

Add storage members:

```cpp
class EnhancedCache {
private:
    // Existing...
    static NetworkState network_state_;
    static MqttState mqtt_state_;
    static BatteryState battery_state_;
    
    // NEW
    static PowerSettingsState power_settings_state_;
    static InverterState inverter_state_;
    static CANState can_state_;
    static ContactorState contactor_state_;
    
public:
    // NEW: Getters and setters for each configuration type
    static const PowerSettingsState& getPowerSettings();
    static void setPowerSettings(const PowerSettingsState& state);
    
    static const InverterState& getInverter();
    static void setInverter(const InverterState& state);
    
    static const CANState& getCAN();
    static void setCAN(const CANState& state);
    
    static const ContactorState& getContactor();
    static void setContactor(const ContactorState& state);
};
```

### 2.6 NVS Storage Implementation

**File**: `ESPnowtransmitter2/src/espnow/enhanced_cache.cpp`

Extend `save_state_to_nvs()` and `restore_state_from_nvs()`:

```cpp
bool EnhancedCache::save_state_to_nvs(CacheDataType data_type) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        return false;
    }
    
    bool success = false;
    switch (data_type) {
        case CacheDataType::STATE_POWER_SETTINGS:
            success = prefs.putBytes("pwr_settings", &power_settings_state_, 
                                    sizeof(PowerSettingsState));
            break;
            
        case CacheDataType::STATE_INVERTER:
            success = prefs.putBytes("inverter", &inverter_state_, 
                                    sizeof(InverterState));
            break;
            
        case CacheDataType::STATE_CAN:
            success = prefs.putBytes("can", &can_state_, sizeof(CANState));
            break;
            
        case CacheDataType::STATE_CONTACTOR:
            success = prefs.putBytes("contactor", &contactor_state_, 
                                    sizeof(ContactorState));
            break;
            
        // ... existing cases
    }
    
    prefs.end();
    return success;
}
```

### 2.7 Message Handlers (Transmitter)

**File**: `ESPnowtransmitter2/src/espnow/message_handler.cpp`

Following the MQTT/Network config pattern, add handlers for each config type:

```cpp
void EspnowMessageHandler::handle_battery_config_update(const espnow_queue_msg_t& msg) {
    // 1. Validate message
    // 2. Verify checksum
    // 3. Update EnhancedCache
    // 4. Save to NVS
    // 5. Apply configuration (if applicable)
    // 6. Send ACK back to receiver
}

// Similar handlers for:
// - handle_power_settings_update()
// - handle_inverter_config_update()
// - handle_can_config_update()
// - handle_contactor_config_update()
```

### 2.8 Receiver-Side Changes

#### 2.8.1 TransmitterManager Cache

**File**: `espnowreciever_2/lib/webserver/utils/transmitter_manager.h`

Add cache storage for new config types:

```cpp
class TransmitterManager {
private:
    // Existing...
    static BatterySettings battery_settings;
    
    // NEW
    struct PowerSettings {
        uint32_t charge_power_w;
        uint32_t discharge_power_w;
        uint32_t max_precharge_time_ms;
        uint32_t precharge_duration_ms;
    };
    static PowerSettings power_settings_;
    static bool power_settings_known_;
    
    // ... similar structs for Inverter, CAN, Contactor
    
public:
    // Store methods
    static void storePowerSettings(const PowerSettings& settings);
    static void storeInverterConfig(const InverterConfig& config);
    static void storeCANConfig(const CANConfig& config);
    static void storeContactorConfig(const ContactorConfig& config);
    
    // Get methods
    static PowerSettings getPowerSettings();
    static bool hasPowerSettings();
    // ... similar for other configs
};
```

#### 2.8.2 API Endpoints

**File**: `espnowreciever_2/lib/webserver/api/api_handlers.cpp`

Add API endpoints following MQTT pattern:

```cpp
// GET endpoints - return cached config
static esp_err_t api_get_battery_config_handler(httpd_req_t *req);
static esp_err_t api_get_power_settings_handler(httpd_req_t *req);
static esp_err_t api_get_inverter_config_handler(httpd_req_t *req);
static esp_err_t api_get_can_config_handler(httpd_req_t *req);
static esp_err_t api_get_contactor_config_handler(httpd_req_t *req);

// POST endpoints - request config from transmitter
static esp_err_t api_request_battery_config_handler(httpd_req_t *req);
static esp_err_t api_request_power_settings_handler(httpd_req_t *req);
// ... etc

// POST endpoints - save config to transmitter
static esp_err_t api_save_battery_config_handler(httpd_req_t *req);
static esp_err_t api_save_power_settings_handler(httpd_req_t *req);
// ... etc
```

#### 2.8.3 Web UI Updates

**File**: `espnowreciever_2/lib/webserver/pages/settings_page.cpp`

**Changes Required**:

1. **Make fields editable** - Remove `disabled` attribute from all config inputs
2. **Add JavaScript loaders** - Create load functions for each config section
3. **Add save functions** - Create save functions mirroring MQTT pattern
4. **Add change detection** - Track changes across all editable fields
5. **Add save button** - Single "Save All Changes" button at bottom

**Example for Battery Config**:

```javascript
async function loadBatteryConfig() {
    try {
        const response = await fetch('/api/get_battery_config');
        const data = await response.json();
        
        if (!data.success) {
            // Request from transmitter if cache empty
            await fetch('/api/request_battery_config', { method: 'POST' });
            await new Promise(resolve => setTimeout(resolve, 2000));
            const retryResponse = await fetch('/api/get_battery_config');
            const retryData = await retryResponse.json();
            if (retryData.success) {
                populateBatteryConfig(retryData);
            }
            return;
        }
        
        populateBatteryConfig(data);
    } catch (error) {
        console.error('Failed to load battery config:', error);
    }
}

function populateBatteryConfig(data) {
    document.getElementById('batteryCapacity').value = data.capacity_wh;
    document.getElementById('batteryMaxVoltage').value = data.max_voltage_mv;
    document.getElementById('batteryMinVoltage').value = data.min_voltage_mv;
    // ... etc
    
    storeInitialTransmitterConfig();
}

async function saveBatteryConfig() {
    const config = {
        capacity_wh: parseInt(document.getElementById('batteryCapacity').value),
        max_voltage_mv: parseInt(document.getElementById('batteryMaxVoltage').value),
        min_voltage_mv: parseInt(document.getElementById('batteryMinVoltage').value),
        // ... etc
    };
    
    const response = await fetch('/api/save_battery_config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(config)
    });
    
    const result = await response.json();
    if (result.success) {
        alert('Battery configuration saved successfully');
        loadBatteryConfig(); // Reload to refresh
    } else {
        alert('Failed to save: ' + result.message);
    }
}
```

### 2.9 Implementation Sequence (Transmitter)

**Phase 1: Message Infrastructure**
1. Add new message types and structures to `espnow_common.h`
2. Add new `CacheDataType` enums to `enhanced_cache.h`

**Phase 2: Cache & NVS (per config type)**
3. Add state structures to EnhancedCache
4. Implement NVS save/restore for each type
5. Add getters/setters to EnhancedCache

**Phase 3: Message Handlers (per config type)**
6. Implement request handlers (send cached data to receiver)
7. Implement update handlers (receive, validate, save, apply, ACK)
8. Register handlers in message router

**Phase 4: Receiver Integration (per config type)**
9. Add cache storage to TransmitterManager
10. Add API endpoints (get, request, save)
11. Update web UI to load/display/edit/save

**Phase 5: Testing**
12. Test each config section independently
13. Verify NVS persistence across reboots
14. Test error handling and validation

---

## 3. Implementation Priorities

### 3.1 Recommended Order

**Phase 1: Receiver WiFi Configuration** (Standalone, simpler)
- Lower risk, independent of transmitter
- Gives experience with NVS pattern
- Immediate user value

**Phase 2: Transmitter Battery Configuration** (Most critical)
- Most frequently modified setting
- Template for other config types
- High user value

**Phase 3: Remaining Transmitter Configs** (In parallel)
- Power Settings
- Inverter Configuration
- CAN Configuration
- Contactor Control

### 3.2 Estimated Effort

| Component | LOC Estimate | Complexity | Time Estimate |
|-----------|-------------|------------|---------------|
| ReceiverConfigManager | 250 | Medium | 4 hours |
| Receiver Network Page | 300 | Low | 3 hours |
| Receiver API Endpoints | 150 | Low | 2 hours |
| **Receiver Total** | **700** | - | **9 hours** |
| | | | |
| Enhanced Cache Extension | 200 | Medium | 3 hours |
| Battery Config Messages | 300 | Medium | 4 hours |
| Power Settings Messages | 250 | Low | 3 hours |
| Inverter Config Messages | 250 | Low | 3 hours |
| CAN Config Messages | 250 | Low | 3 hours |
| Contactor Config Messages | 200 | Low | 2 hours |
| Web UI Enhancements | 500 | Medium | 6 hours |
| **Transmitter Total** | **1950** | - | **24 hours** |
| | | | |
| **Grand Total** | **2650** | - | **33 hours** |

---

## 4. Risk Analysis & Mitigations

### 4.1 Risks

1. **WiFi Config Error Bricks Receiver**
   - ~~Mitigation: Add fallback AP mode if WiFi fails to connect after 30 seconds~~ ✅ **IMPLEMENTED**
   - Mitigation: Keep default config in code as backup ❌ **REMOVED** (no hardcoded defaults)
   - Mitigation: **AP mode is primary recovery method** - device always boots to AP if no valid config

2. **Invalid Config Causes Boot Loop**
   - Mitigation: Validate all inputs before saving
   - Mitigation: Add checksum verification on load
   - Mitigation: Factory reset option (button hold on boot)

3. **NVS Corruption**
   - Mitigation: Use versioning in all config structures
   - Mitigation: Implement `nvs_flash_erase()` recovery option
   - Mitigation: Log all NVS operations for debugging

4. **Config Type Explosion**
   - Mitigation: Use consistent naming patterns
   - Mitigation: Document each config type thoroughly
   - Mitigation: Create code generation templates

### 4.2 Validation Strategy

**Input Validation Rules**:
- WiFi SSID: Not empty, max 32 chars
- WiFi Password: Max 63 chars
- IP Addresses: Valid quad format (0-255)
- Port Numbers: 1-65535
- Numeric Values: Within sensor/hardware limits
- Checksums: Match calculated value

**Testing Checklist**:
- [ ] Save config → reboot → verify persistence
- [ ] Invalid input rejected with error message
- [ ] NVS corruption recovery works
- [ ] Fallback to defaults on corruption
- [ ] Web UI shows correct values after save
- [ ] ESP-NOW messages have correct format
- [ ] ACK messages received and processed
- [ ] Multiple rapid saves don't corrupt data

---

## 5. Breaking Changes & Migration

### 5.1 Receiver WiFi

**Breaking Change**: Hardcoded WiFi config replaced with NVS

**Migration Path**:
1. On first boot with new firmware, no NVS config exists
2. Code detects missing config and starts in **AP (Access Point) mode**
3. User connects to AP and enters WiFi credentials via captive portal
4. Credentials saved to NVS and device reboots
5. On second boot, connects to configured WiFi network normally
6. **User action required**: One-time WiFi setup via AP mode

### 5.2 Transmitter Config

**Breaking Change**: Config previously read from header files, now from NVS

**Migration Path**:
1. On first boot, EnhancedCache detects missing NVS sections
2. Populates NVS with current values from header files
3. Future boots read from NVS
4. No user action required for migration

---

## 6. Documentation Requirements

### 6.1 User Documentation

**File**: `espnowreciever_2/docs/USER_GUIDE.md`

Add sections:
- "Configuring WiFi Settings"
- "Configuring Transmitter via Web UI"
- "Factory Reset Procedure"
- "Troubleshooting Network Issues"

### 6.2 Developer Documentation

**File**: `espnowreciever_2/docs/DEVELOPER_GUIDE.md`

Add sections:
- "ReceiverConfigManager API"
- "Adding New Configuration Types"
- "NVS Structure Reference"
- "Config Message Protocol"

---

## 7. Testing Plan

### 7.1 Unit Tests

**Receiver**:
- ReceiverConfigManager::saveConfig() with valid data
- ReceiverConfigManager::loadConfig() from empty NVS
- ReceiverConfigManager::loadConfig() with corrupted data
- IP address validation
- SSID validation

**Transmitter**:
- EnhancedCache save/restore for each config type
- Message validation (checksum, bounds)
- NVS corruption recovery

### 7.2 Integration Tests

**Receiver**:
- Save WiFi config → reboot → verify connection
- Change SSID → reboot → verify new network
- Static IP → DHCP transition
- **Fresh install → AP mode → enter credentials → verify connection**
- **Invalid credentials → reboot → fallback to AP mode**
- **AP mode captive portal → any URL redirects to setup page**
- mDNS access via hostname.local (optional)

**Transmitter**:
- Web UI save → ESP-NOW message → NVS persistence
- Request config → populate cache → display in UI
- Invalid config rejected with error
- Multiple config sections saved independently

### 7.3 End-to-End Tests

1. Fresh install → configure WiFi → verify persistence
2. Configure all transmitter sections → reboot → verify all persist
3. Change one section → verify only that section saved
4. Corrupted NVS → verify fallback to defaults
5. Factory reset → verify return to defaults

---

## 8. Recommendations

### 8.1 Suggested Improvements

1. **Backup/Restore Feature**
   - Export all configs to JSON file
   - Import JSON to restore configs
   - Location: New page `/system/backup`

2. **Config Validation UI**
   - Real-time validation feedback
   - Disable save button if invalid
   - Show which fields have errors

3. **Change History**
   - Log config changes with timestamp
   - Show last modified date per section
   - Useful for debugging

4. **Factory Reset Button**
   - Physical button or web UI option
   - Clears all NVS, restores defaults
   - Requires confirmation

### 8.2 Future Enhancements

1. **Config Profiles**
   - Save multiple configuration sets
   - Quick switch between profiles
   - Example: "Home Network" vs "Mobile Hotspot"

2. **Config Templates**
   - Predefined settings for common scenarios
   - Example battery chemistries pre-configured
   - One-click apply

3. **Remote Config Push**
   - Configure multiple receivers from one UI
   - Broadcast config updates
   - Useful for fleet management

---

## 9. Conclusion

This implementation plan provides a comprehensive approach to adding persistent configuration storage to both the Receiver (WiFi settings) and Transmitter (extended settings). The design follows established patterns already proven in the MQTT/Network configuration implementation, ensuring consistency and maintainability.

### Next Steps

1. **Review this document** with stakeholders
2. **Prioritize** implementation phases
3. **Create detailed task breakdown** for Phase 1 (Receiver WiFi)
4. **Begin implementation** with Receiver as proof of concept
5. **Iterate and extend** to Transmitter based on lessons learned

### Success Criteria

- ✅ All configuration changes persist across reboots
- ✅ Web UI provides intuitive editing experience
- ✅ Invalid configs are rejected with clear error messages
- ✅ Factory reset capability available
- ✅ No breaking changes for existing users (auto-migration)
- ✅ Comprehensive documentation for users and developers

---

**Document Version**: 1.0  
**Review Status**: Awaiting Review  
**Next Review Date**: TBD
