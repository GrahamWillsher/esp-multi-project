# Battery Emulator Settings to NVS Mapping

**Created**: February 6, 2026  
**Phase**: 0 (Pre-Migration Setup)  
**Purpose**: Map Battery Emulator NVS settings to transmitter storage structure

---

## Source: Battery Emulator Settings

From `Battery-Emulator-9.2.4/Software/src/communication/nvm/comm_nvm.cpp`

Battery Emulator uses ESP32 `Preferences` library with namespace `"batterySettings"`.

---

## 1. Settings Categories

### 1.1 Battery Capacity & Limits

| Setting Key | Type | Description | Default | Range | NVS |
|------------|------|-------------|---------|-------|-----|
| BATTERY_WH_MAX | uint32_t | Total capacity | 30000 Wh | 1000-200000 | Yes |
| MAXCHARGEAMP | uint32_t | Max charge current | 300 dA (30.0 A) | 10-5000 | Yes |
| MAXDISCHARGEAMP | uint32_t | Max discharge current | 300 dA (30.0 A) | 10-5000 | Yes |
| TARGETCHVOLT | uint32_t | Max charge voltage | 4500 dV (450.0 V) | 1000-10000 | Yes |
| TARGETDISCHVOLT | uint32_t | Min discharge voltage | 3000 dV (300.0 V) | 1000-10000 | Yes |

### 1.2 SOC Scaling Settings

| Setting Key | Type | Description | Default | Range | NVS |
|------------|------|-------------|---------|-------|-----|
| MAXPERCENTAGE | uint32_t | SOC scaling max | 8000 (80.0%) | 0-10000 | Yes |
| MINPERCENTAGE | int32_t | SOC scaling min | 2000 (20.0%) | -100-5000 | Yes |
| USE_SCALED_SOC | bool | Enable SOC scaling | false | true/false | Yes |
| USEVOLTLIMITS | bool | Use custom voltage limits | false | true/false | Yes |

### 1.3 Battery Type Selection

| Setting Key | Type | Description | Default | Enum Values | NVS |
|------------|------|-------------|---------|-------------|-----|
| BATTTYPE | uint32_t | Battery brand/type | None | 0-50+ (BatteryType enum) | Yes |
| BATTCHEM | uint32_t | Battery chemistry | NCA | NCA/NMC/LFP/LTO | Yes |
| BATTPVMAX | uint32_t | User max pack voltage | 0 (auto) | 0-10000 dV | Yes |
| BATTPVMIN | uint32_t | User min pack voltage | 0 (auto) | 0-10000 dV | Yes |
| BATTCVMAX | uint32_t | User max cell voltage | 0 (auto) | 0-5000 mV | Yes |
| BATTCVMIN | uint32_t | User min cell voltage | 0 (auto) | 0-5000 mV | Yes |

### 1.4 Inverter Settings

| Setting Key | Type | Description | Default | NVS |
|------------|------|-------------|---------|-----|
| INVTYPE | uint32_t | Inverter protocol | None | Yes |
| INVCELLS | uint32_t | Number of cells | 0 (auto) | Yes |
| INVMODULES | uint32_t | Number of modules | 0 (auto) | Yes |
| INVCELLSPER | uint32_t | Cells per module | 0 (auto) | Yes |
| INVVLEVEL | uint32_t | Voltage level | 0 (auto) | Yes |
| INVAHCAPACITY | uint32_t | Capacity in Ah | 0 (auto) | Yes |
| INVBTYPE | uint32_t | Battery type for inverter | 0 (auto) | Yes |
| INVICNT | bool | Ignore contactors | false | Yes |

### 1.5 Charger Settings

| Setting Key | Type | Description | Default | NVS |
|------------|------|-------------|---------|-----|
| CHGTYPE | uint32_t | Charger type | None | Yes |
| CHGPOWER | uint32_t | Override charge power | 1000 W | Yes |
| DCHGPOWER | uint32_t | Override discharge power | 1000 W | Yes |

### 1.6 WiFi/Network Settings (NOT MIGRATED)

These settings will NOT be migrated - transmitter uses Ethernet only:

| Setting Key | Description | Migration Status |
|------------|-------------|-----------------|
| SSID | WiFi SSID | ❌ Not migrated (Ethernet only) |
| PASSWORD | WiFi password | ❌ Not migrated |
| WIFIAPENABLED | WiFi AP enabled | ❌ Not needed (no AP mode) |
| WIFICHANNEL | WiFi channel | ❌ Not needed |
| APNAME | AP SSID | ❌ Not needed |
| APPASSWORD | AP password | ❌ Not needed |
| STATICIP | Static IP enabled | ✓ Migrate (Ethernet) |
| LOCALIP1-4 | Static IP address | ✓ Migrate (Ethernet) |
| GATEWAY1-4 | Gateway IP | ✓ Migrate (Ethernet) |
| SUBNET1-4 | Subnet mask | ✓ Migrate (Ethernet) |
| HOSTNAME | Hostname | ✓ Migrate |

### 1.7 MQTT Settings

| Setting Key | Type | Description | Default | NVS |
|------------|------|-------------|---------|-----|
| MQTTENABLED | bool | MQTT enabled | false | Yes |
| MQTTSERVER | String | MQTT server address | "" | Yes |
| MQTTPORT | uint32_t | MQTT port | 1883 | Yes |
| MQTTUSER | String | MQTT username | "" | Yes |
| MQTTPASSWORD | String | MQTT password | "" | Yes |
| MQTTTIMEOUT | uint32_t | MQTT timeout | 2000 ms | Yes |
| HADISC | bool | Home Assistant discovery | false | Yes |
| MQTTCELLV | bool | Transmit cell voltages | false | Yes |

### 1.8 Logging Settings

| Setting Key | Type | Description | Default | NVS |
|------------|------|-------------|---------|-----|
| CANLOGUSB | bool | CAN logging to USB | false | Yes |
| USBENABLED | bool | USB logging | false | Yes |
| WEBENABLED | bool | Web logging | false | Yes |
| CANLOGSD | bool | CAN logging to SD | false | Yes |
| SDLOGENABLED | bool | SD logging | false | Yes |
| PERFPROFILE | bool | Performance profiling | false | Yes |

### 1.9 Hardware/Contactor Settings

| Setting Key | Type | Description | Default | NVS |
|------------|------|-------------|---------|-----|
| CNTCTRL | bool | Contactor control enabled | false | Yes |
| NCCONTACTOR | bool | Inverted contactor logic | false | Yes |
| PRECHGMS | uint32_t | Precharge time | 100 ms | Yes |
| EXTPRECHARGE | bool | External precharge | false | Yes |
| NOINVDISC | bool | Inverter NO contactor | false | Yes |
| MAXPRETIME | uint32_t | Max precharge time | 15000 ms | Yes |
| PWMCNTCTRL | bool | PWM contactor control | false | Yes |
| PWMFREQ | uint32_t | PWM frequency | 20000 Hz | Yes |
| PWMHOLD | uint32_t | PWM hold duty | 250 (25.0%) | Yes |

### 1.10 Special Functions

| Setting Key | Type | Description | Default | NVS |
|------------|------|-------------|---------|-----|
| EQUIPMENT_STOP | bool | E-stop active | false | Yes |
| EQSTOP | uint32_t | E-stop behavior | NOT_CONNECTED | Yes |
| PERBMSRESET | bool | Periodic BMS reset | false | Yes |
| REMBMSRESET | bool | Remote BMS reset | false | Yes |
| BMSRESETDUR | uint32_t | BMS reset duration | 30000 ms | Yes |
| LEDMODE | uint32_t | LED mode | CLASSIC | Yes |

---

## 2. Settings Migration Plan

### 2.1 Phase 1: Message Definition

**ESP-NOW Message**: `msg_settings_request`

```cpp
typedef struct __attribute__((packed)) {
  uint8_t type;                          // msg_settings_request
  uint8_t category;                      // Which category: BATTERY/INVERTER/CHARGER/MQTT/etc
  uint16_t checksum;
} settings_request_msg_t;  // 4 bytes
```

**ESP-NOW Message**: `msg_settings_response`

```cpp
typedef struct __attribute__((packed)) {
  uint8_t type;                          // msg_settings_response
  uint8_t category;                      // BATTERY/INVERTER/CHARGER/MQTT/etc
  uint8_t settings_data[240];            // Variable size based on category
  uint16_t checksum;
} settings_response_msg_t;  // Max 244 bytes
```

**ESP-NOW Message**: `msg_settings_update`

```cpp
typedef struct __attribute__((packed)) {
  uint8_t type;                          // msg_settings_update
  uint8_t category;                      // BATTERY/INVERTER/CHARGER/MQTT/etc
  uint8_t setting_id;                    // Which specific setting
  uint32_t value_uint32;                 // Value (union with other types)
  char value_string[64];                 // For strings (MQTT server, etc)
  uint16_t checksum;
} settings_update_msg_t;  // 73 bytes
```

**ESP-NOW Message**: `msg_settings_update_ack`

```cpp
typedef struct __attribute__((packed)) {
  uint8_t type;                          // msg_settings_update_ack
  uint8_t category;
  uint8_t setting_id;
  uint8_t success;                       // 1=saved, 0=failed
  char error_msg[32];                    // Error description
  uint16_t checksum;
} settings_update_ack_msg_t;  // 39 bytes
```

---

### 2.2 Phase 2: NVS Structure on Transmitter

**Namespace**: `"batt_emu_v2"` (new namespace for v2.0.0)

**Storage Organization**:

```cpp
// ESPnowtransmitter2/src/storage/settings_storage.h

enum SettingCategory {
  CATEGORY_BATTERY = 0,
  CATEGORY_INVERTER = 1,
  CATEGORY_CHARGER = 2,
  CATEGORY_MQTT = 3,
  CATEGORY_NETWORK = 4,
  CATEGORY_LOGGING = 5,
  CATEGORY_HARDWARE = 6,
  CATEGORY_SYSTEM = 7
};

enum BatterySettingID {
  BATT_CAPACITY_WH = 0,
  BATT_MAX_CHARGE_CURRENT_DA = 1,
  BATT_MAX_DISCHARGE_CURRENT_DA = 2,
  BATT_MAX_CHARGE_VOLTAGE_DV = 3,
  BATT_MIN_DISCHARGE_VOLTAGE_DV = 4,
  BATT_SOC_MAX_PERCENT = 5,
  BATT_SOC_MIN_PERCENT = 6,
  BATT_SOC_SCALING_ACTIVE = 7,
  BATT_VOLTAGE_LIMITS_ACTIVE = 8,
  BATT_CHEMISTRY = 9,
  BATT_TYPE = 10
};

enum MQTTSettingID {
  MQTT_ENABLED = 0,
  MQTT_SERVER = 1,          // String
  MQTT_PORT = 2,
  MQTT_USER = 3,            // String
  MQTT_PASSWORD = 4,        // String
  MQTT_TIMEOUT_MS = 5,
  MQTT_HA_DISCOVERY = 6,
  MQTT_CELL_VOLTAGES = 7
};

// ... etc for other categories
```

**NVS Helper Functions**:

```cpp
// ESPnowtransmitter2/src/storage/settings_storage.cpp

class SettingsStorage {
public:
  bool load_all_settings();
  bool save_setting(uint8_t category, uint8_t setting_id, uint32_t value);
  bool save_setting_string(uint8_t category, uint8_t setting_id, const char* value);
  uint32_t get_setting(uint8_t category, uint8_t setting_id);
  const char* get_setting_string(uint8_t category, uint8_t setting_id);
  bool restore_defaults();
  
private:
  Preferences prefs;
  const char* get_namespace_for_category(uint8_t category);
};
```

---

### 2.3 Phase 3: Receiver Settings Pages

Each settings page sends ESP-NOW messages to transmitter:

**Example: Battery Settings Page**

```javascript
// espnowreciever_2/lib/webserver/pages/battery_settings_page.cpp

function saveBatterySetting(settingName, value) {
  fetch('/api/save_battery_setting', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({
      setting_id: settingName,
      value: value
    })
  })
  .then(response => response.json())
  .then(data => {
    if (data.success) {
      showNotification('Setting saved!', 'success');
    } else {
      showNotification('Failed: ' + data.error, 'error');
    }
  });
}
```

**Receiver API Handler**:

```cpp
// espnowreciever_2/lib/webserver/api/api_handlers.cpp

static esp_err_t api_save_battery_setting_handler(httpd_req_t *req) {
  // Parse JSON body
  char buf[256];
  httpd_req_recv(req, buf, sizeof(buf));
  
  // Extract setting_id and value
  uint8_t setting_id = ...;
  uint32_t value = ...;
  
  // Create ESP-NOW message
  settings_update_msg_t msg;
  msg.type = msg_settings_update;
  msg.category = CATEGORY_BATTERY;
  msg.setting_id = setting_id;
  msg.value_uint32 = value;
  msg.checksum = calculate_checksum(&msg, sizeof(msg) - 2);
  
  // Send to transmitter
  esp_now_send(transmitter_mac, (uint8_t*)&msg, sizeof(msg));
  
  // Wait for ACK (with timeout)
  bool ack_received = wait_for_settings_ack(1000);  // 1 second timeout
  
  // Return JSON response
  if (ack_received) {
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
  } else {
    httpd_resp_send(req, "{\"success\":false,\"error\":\"Timeout\"}", HTTPD_RESP_USE_STRLEN);
  }
  
  return ESP_OK;
}
```

**Transmitter Settings Handler**:

```cpp
// ESPnowtransmitter2/src/settings/settings_handler.cpp

void handle_settings_update(const espnow_queue_msg_t* msg) {
  const settings_update_msg_t* update = (settings_update_msg_t*)msg->data;
  
  // Validate checksum
  if (!validate_checksum(update, sizeof(*update))) {
    send_settings_nack(msg->mac, update->category, update->setting_id, "Checksum failed");
    return;
  }
  
  // Apply setting based on category and setting_id
  bool success = false;
  const char* error_msg = "";
  
  switch (update->category) {
    case CATEGORY_BATTERY:
      success = apply_battery_setting(update->setting_id, update->value_uint32);
      break;
    case CATEGORY_MQTT:
      if (update->setting_id == MQTT_SERVER || update->setting_id == MQTT_USER || update->setting_id == MQTT_PASSWORD) {
        success = apply_mqtt_string_setting(update->setting_id, update->value_string);
      } else {
        success = apply_mqtt_setting(update->setting_id, update->value_uint32);
      }
      break;
    // ... other categories
  }
  
  // Save to NVS
  if (success) {
    success = settings_storage.save_setting(update->category, update->setting_id, update->value_uint32);
    if (!success) {
      error_msg = "NVS write failed";
    }
  }
  
  // Send ACK
  send_settings_ack(msg->mac, update->category, update->setting_id, success, error_msg);
}

bool apply_battery_setting(uint8_t setting_id, uint32_t value) {
  switch (setting_id) {
    case BATT_CAPACITY_WH:
      if (value < 1000 || value > 200000) return false;  // Range check
      datalayer.battery.info.total_capacity_Wh = value;
      return true;
      
    case BATT_MAX_CHARGE_CURRENT_DA:
      if (value < 10 || value > 5000) return false;
      datalayer.battery.settings.max_user_set_charge_dA = value;
      return true;
      
    // ... other settings
      
    default:
      return false;
  }
}
```

---

## 3. Settings Validation

### 3.1 Range Validation

All settings must be validated before saving:

| Setting | Min | Max | Default |
|---------|-----|-----|---------|
| Capacity (Wh) | 1000 | 200000 | 30000 |
| Charge current (dA) | 10 | 5000 | 300 |
| Discharge current (dA) | 10 | 5000 | 300 |
| Charge voltage (dV) | 1000 | 10000 | 4500 |
| Discharge voltage (dV) | 1000 | 10000 | 3000 |
| SOC max (%) | 0 | 10000 | 8000 |
| SOC min (%) | -100 | 5000 | 2000 |
| MQTT port | 1 | 65535 | 1883 |

### 3.2 Dependency Validation

Some settings depend on others:

- **Voltage limits**: Max charge voltage > Min discharge voltage
- **SOC scaling**: Max percentage > Min percentage
- **Current limits**: Must not exceed battery chemistry limits

---

## 4. Factory Reset

**ESP-NOW Message**: `msg_factory_reset`

```cpp
typedef struct __attribute__((packed)) {
  uint8_t type;                          // msg_factory_reset
  uint32_t magic_number;                 // 0xDEADBEEF (safety check)
  uint16_t checksum;
} factory_reset_msg_t;  // 7 bytes
```

**Transmitter Handler**:

```cpp
void handle_factory_reset(const espnow_queue_msg_t* msg) {
  const factory_reset_msg_t* reset = (factory_reset_msg_t*)msg->data;
  
  // Validate magic number
  if (reset->magic_number != 0xDEADBEEF) {
    LOG_ERROR("Invalid factory reset magic number");
    return;
  }
  
  // Erase all NVS partitions
  nvs_flash_erase();
  nvs_flash_init();
  
  // Reboot
  esp_restart();
}
```

---

## 5. Testing Checklist

- [ ] Load all settings from NVS on boot
- [ ] Settings displayed correctly on web pages
- [ ] Range validation prevents invalid values
- [ ] Save button triggers ESP-NOW message
- [ ] Transmitter receives and validates settings
- [ ] Settings saved to NVS successfully
- [ ] ACK message sent back to receiver
- [ ] Receiver displays success/failure message
- [ ] Settings persist across reboots
- [ ] Factory reset erases all settings
- [ ] Default settings loaded after factory reset
- [ ] String settings (MQTT server, username) work correctly
- [ ] Settings update while control loop running (no interference)

---

**Status**: ✓ COMPLETE  
**Next**: Create TASK_PRIORITIES_AND_TIMING.md
