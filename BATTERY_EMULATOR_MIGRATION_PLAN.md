# Battery Emulator 9.2.4 to ESP32Projects Migration Plan

## Executive Summary
Migrate the single-device Battery-Emulator-9.2.4 project to a two-device architecture:
- **Transmitter (ESP32-POE-ISO)**: Real-time battery control system with Ethernet/MQTT
- **Receiver (LilyGo T-Display-S3)**: Web interface and display with ESP-NOW communication

---

## Current State Analysis

### Battery-Emulator-9.2.4 (Source)
**Location**: `C:\Users\GrahamWillsher\Downloads\Battery-Emulator-9.2.4\Battery-Emulator-9.2.4`
**Status**: ✓ Added to VS Code workspace

**Hardware Platforms** (platformio.ini):
- esp32dev (basic devkit)
- lilygo_330
- stark_330  
- lilygo_2CAN_330 (ESP32-S3 with 2x CAN)
- lilygo_t_connect_pro (ESP32-S3 with WiFi)

**Architecture**:
```
Software/
  Software.cpp (main entry point)
  src/
    battery/          # Battery BMS interfaces
    charger/          # Charger controllers
    communication/    # CAN, RS485, contactors, precharge
      ├── can/
      ├── contactorcontrol/
      ├── equipmentstopbutton/
      ├── nvm/
      ├── precharge_control/
      ├── rs485/
      └── Transmitter.h
    datalayer/        # Central data storage
    devboard/         # Board-specific code
      ├── display/
      ├── mqtt/
      ├── sdcard/
      ├── utils/
      ├── webserver/  # WiFi AP + web UI
      └── wifi/
    inverter/         # Inverter controllers
```

**Key Features**:
- Single WiFi AP for settings web interface
- Real-time battery monitoring and control
- CAN bus communication with BMS, charger, inverter
- MQTT telemetry (optional)
- SD card logging
- Precharge control and contactor management
- LED status indicators
- ElegantOTA for firmware updates

**Web Interface** (data/ folder):
- settings_body.html (main settings page - 662 lines)
- settings_scripts.html (JavaScript logic)
- settings_style.html (CSS styling)

**Current Task Structure** (Software.cpp):
- `main_loop_task` - Core 10ms control loop (battery, CAN, contactors)
- `connectivity_loop_task` - WiFi, webserver, OTA, display
- `logging_loop_task` - SD card logging
- `mqtt_loop_task` - MQTT publishing

---

### ESP32Projects (Destination)
**Location**: `C:\Users\GrahamWillsher\ESP32Projects`

**Current Projects**:
1. **espnowreciever_2** (Receiver - LilyGo T-Display-S3)
   - Web server with pages: monitor, OTA, debug, settings, systeminfo, reboot
   - ESP-NOW message router with 15 registered routes
   - TFT display showing SOC, power, LED status
   - Test mode and normal operation states
   - Configuration sync via ESP-NOW
   - TransmitterManager for tracking transmitter state

2. **ESPnowtransmitter2** (Transmitter - ESP32-POE-ISO)
   - Ethernet connectivity (static/DHCP)
   - ESP-NOW transmitter with periodic announcements
   - MQTT telemetry publishing
   - HTTP OTA server
   - Discovery and channel locking
   - Message handlers for control commands
   - Data sender task for battery metrics

3. **esp32common** (Shared Libraries)
   - espnow_transmitter/
   - espnow_common_utils/
   - firmware_metadata/
   - config_sync/
   - logging_utilities/

**Current ESP-NOW Messages** (espnow_common.h):
```
msg_probe, msg_ack, msg_data, msg_request_data, msg_abort_data,
msg_packet, msg_reboot, msg_ota_start, msg_flash_led, msg_debug_control,
msg_config_request_full, msg_config_snapshot, msg_config_update_delta,
msg_config_ack, msg_version_announce, msg_metadata_request, msg_metadata_response
```

**Current Receiver Web Pages**:
- `/` - Monitor page (SOC, power, test mode toggle)
- `/ota` - OTA firmware upload
- `/debug` - Debug level control
- `/settings` - System settings
- `/systeminfo` - Device info
- `/reboot` - Reboot control

---

## Migration Strategy

### Core Principles
1. **Preserve Real-Time Performance**: Transmitter control loop must remain < 10ms
2. **Minimize Transmitter Overhead**: Move all non-critical UI to receiver
3. **Bidirectional Settings**: Add save buttons and ESP-NOW config update messages
4. **Single Hardware Config**: Simplify to ESP32-POE-ISO only (transmitter)
5. **Progressive Migration**: Bite-sized, testable steps
6. **No Breaking Changes**: Keep existing functionality working during migration

---

## Phase-by-Phase Migration Plan

### **Phase 0: Pre-Migration Setup** (1-2 days)
**Goal**: Prepare workspace and establish baseline

#### Steps:
1. **Battery Emulator source location** ✓ COMPLETE
   - Source: `C:\Users\GrahamWillsher\Downloads\Battery-Emulator-9.2.4\Battery-Emulator-9.2.4`
   - Already added to VS Code workspace
   - Ready for analysis and code porting

2. **Create migration branch**
   ```bash
   cd C:\Users\GrahamWillsher\ESP32Projects
   git checkout -b feature/battery-emulator-migration
   ```

3. **Document current Battery Emulator functionality**
   - List all web pages and settings
   - Map all CAN messages
   - Document task priorities and timing
   - Identify data flow patterns

4. **Create mapping documents**
   - `WEBSERVER_PAGES_MAPPING.md` - Which pages go where
   - `DATA_LAYER_MAPPING.md` - How datalayer maps to ESP-NOW messages
   - `SETTINGS_MAPPING.md` - Settings structure migration

**Deliverable**: Clean workspace with documented baseline

---

### **Phase 1: Data Layer Integration** (3-4 days)
**Goal**: Map Battery Emulator datalayer to ESP-NOW messaging

#### Current Datalayer Structure:
```cpp
// From datalayer.h (Battery Emulator)
struct {
  BatteryStatus status;
  BatterySettings settings;
  BatteryInfo info;
} battery;

struct {
  SystemStatus status;
  SystemSettings settings;
  SystemInfo info;
} system;

struct {
  ChargerStatus status;
  ChargerSettings settings;
} charger;

struct {
  InverterStatus status;
  InverterSettings settings;
} inverter;
```

#### Steps:

1. **Create new ESP-NOW message types** in `espnow_common.h`
   ```cpp
   // Battery data messages
   msg_battery_status,      // SOC, voltage, current, temp
   msg_battery_settings,    // User configurable settings
   msg_battery_info,        // Static info (capacity, chemistry)
   
   // Charger data messages
   msg_charger_status,      // HV/LV voltage, current, power
   msg_charger_settings,    // Charge limits, modes
   
   // Inverter data messages
   msg_inverter_status,     // AC voltage, current, frequency
   msg_inverter_settings,   // Power limits, modes
   
   // System data messages
   msg_system_status,       // Contactors, errors, LED state
   msg_system_settings,     // Global settings
   msg_system_info,         // Device info, uptime
   ```

2. **Create packed message structures**
   ```cpp
   typedef struct __attribute__((packed)) {
     uint8_t type;                    // msg_battery_status
     uint16_t soc_percent_100;        // SOC in 0.01%
     uint32_t voltage_mV;             // Voltage in mV
     int32_t current_mA;              // Current in mA (signed)
     int16_t temperature_dC;          // Temperature in 0.1°C
     int16_t power_W;                 // Power in W (signed)
     uint16_t max_charge_power_W;
     uint16_t max_discharge_power_W;
     uint8_t bms_status;              // OK/WARNING/FAULT
     uint16_t checksum;
   } battery_status_msg_t;
   ```

3. **Create transmitter sender functions**
   ```cpp
   // In ESPnowtransmitter2/src/battery/battery_sender.cpp
   void send_battery_status() {
     battery_status_msg_t msg;
     msg.type = msg_battery_status;
     msg.soc_percent_100 = datalayer.battery.status.soc_percent * 100;
     msg.voltage_mV = datalayer.battery.status.voltage_dV * 100;
     // ... populate all fields
     msg.checksum = calculate_checksum(&msg, sizeof(msg) - 2);
     esp_now_send(receiver_mac, (uint8_t*)&msg, sizeof(msg));
   }
   ```

4. **Create receiver handler functions**
   ```cpp
   // In espnowreciever_2/src/espnow/battery_handlers.cpp
   void handle_battery_status(const espnow_queue_msg_t* msg) {
     const battery_status_msg_t* data = (battery_status_msg_t*)msg->data;
     
     // Validate checksum
     if (!validate_checksum(data, sizeof(*data))) return;
     
     // Store in global state
     g_battery_soc = data->soc_percent_100 / 100.0;
     g_battery_voltage = data->voltage_mV / 1000.0;
     g_battery_current = data->current_mA / 1000.0;
     // ... store all fields
     
     // Notify web clients via SSE
     notify_sse_battery_updated();
   }
   ```

5. **Register routes in message router**
   ```cpp
   router.register_route(msg_battery_status, handle_battery_status, 0xFF, nullptr);
   router.register_route(msg_charger_status, handle_charger_status, 0xFF, nullptr);
   router.register_route(msg_inverter_status, handle_inverter_status, 0xFF, nullptr);
   router.register_route(msg_system_status, handle_system_status, 0xFF, nullptr);
   ```

6. **Create dummy data generator for testing** (1 day)
```cpp
// ESPnowtransmitter2/src/testing/dummy_data_generator.cpp
// TEMPORARY: Used for Phase 1-3 web development before real hardware integration

void send_dummy_battery_data() {
  battery_status_msg_t msg;
  msg.type = msg_battery_status;
  
  // Generate realistic dummy data
  static uint16_t soc = 8000; // 80.00%
  soc = (soc + random(-100, 100)) % 10000;
  msg.soc_percent_100 = soc;
  
  msg.voltage_mV = 48000 + random(-1000, 1000);  // 48V ±1V
  msg.current_mA = random(-50000, 50000);         // ±50A
  msg.temperature_dC = 250 + random(-20, 20);     // 25°C ±2°C
  msg.power_W = (msg.voltage_mV * msg.current_mA) / 1000000;
  msg.max_charge_power_W = 3000;
  msg.max_discharge_power_W = 5000;
  msg.bms_status = (soc < 2000) ? FAULT : (soc < 3000) ? WARNING : OK;
  
  msg.checksum = calculate_checksum(&msg, sizeof(msg) - 2);
  esp_now_send(receiver_mac, (uint8_t*)&msg, sizeof(msg));
}

void dummy_data_task(void* parameter) {
  // LOW PRIORITY - Only for testing Phase 1-3
  while (true) {
    send_dummy_battery_data();
    vTaskDelay(pdMS_TO_TICKS(200));
    
    send_dummy_charger_data();
    vTaskDelay(pdMS_TO_TICKS(200));
    
    send_dummy_inverter_data();
    vTaskDelay(pdMS_TO_TICKS(200));
    
    send_dummy_system_data();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// NOTE: This task will be REMOVED in Phase 4 when real data is available
```

**Testing Phase 1**:
- [ ] Transmitter sends dummy battery status every 200ms
- [ ] Receiver logs received battery status
- [ ] Checksum validation works
- [ ] Dummy data is realistic (voltage/current/SOC ranges make sense)
- [ ] Web page displays dummy battery data
- [ ] SSE updates push to browser in real-time
- [ ] **Unit test**: Message structure packing/unpacking
- [ ] **Unit test**: Checksum calculation

**Deliverable**: ESP-NOW message protocol working with dummy data generator

---

### **Phase 2: Settings Bidirectional Flow** (4-5 days)
**Goal**: Enable settings changes from receiver webserver to transmitter

#### Current Problem:
- Receiver web pages can display settings
- No mechanism to send changes back to transmitter
- Transmitter needs to persist settings to NVS

#### Solution Architecture:
```
Receiver Web Page → Save Button → ESP-NOW msg_settings_update → Transmitter Handler → NVS Storage
```

#### Steps:

1. **Create settings update message types**
   ```cpp
   typedef struct __attribute__((packed)) {
     uint8_t type;                    // msg_battery_settings_update
     uint8_t setting_id;              // Which setting to update
     uint32_t value;                  // New value
     uint16_t checksum;
   } settings_update_msg_t;
   
   typedef struct __attribute__((packed)) {
     uint8_t type;                    // msg_settings_update_ack
     uint8_t setting_id;              // Echo back
     bool success;                    // Saved successfully?
     char error_msg[32];              // Error description if failed
   } settings_update_ack_msg_t;
   ```

2. **Define setting IDs enum**
   ```cpp
   enum SettingID {
     SETTING_MAX_CHARGE_CURRENT,
     SETTING_MAX_DISCHARGE_CURRENT,
     SETTING_MAX_CHARGE_VOLTAGE,
     SETTING_MIN_DISCHARGE_VOLTAGE,
     SETTING_SOC_HIGH_LIMIT,
     SETTING_SOC_LOW_LIMIT,
     SETTING_BATTERY_CAPACITY,
     SETTING_MQTT_ENABLED,
     SETTING_MQTT_SERVER,
     // ... etc
   };
   ```

3. **Add "Save" buttons to receiver settings pages**
   ```javascript
   // In espnowreciever_2/lib/webserver/pages/settings_page.cpp
   function saveBatterySetting(settingName, value) {
     fetch('/api/save_setting', {
       method: 'POST',
       headers: {'Content-Type': 'application/json'},
       body: JSON.stringify({
         category: 'battery',
         setting: settingName,
         value: value
       })
     })
     .then(response => response.json())
     .then(data => {
       if (data.success) {
         alert('Setting saved successfully!');
       } else {
         alert('Failed to save: ' + data.error);
       }
     });
   }
   ```

4. **Create receiver API handler for settings**
   ```cpp
   // In espnowreciever_2/lib/webserver/api/api_handlers.cpp
   static esp_err_t api_save_setting_handler(httpd_req_t *req) {
     // Parse JSON body
     char buf[256];
     httpd_req_recv(req, buf, sizeof(buf));
     
     // Parse category, setting, value
     // Map to SettingID enum
     // Create settings_update_msg_t
     // Send via ESP-NOW to transmitter
     // Wait for ACK (with timeout)
     // Return success/failure JSON
   }
   ```

5. **Create transmitter settings handler**
   ```cpp
   // In ESPnowtransmitter2/src/settings/settings_handler.cpp
   void handle_settings_update(const espnow_queue_msg_t* msg) {
     const settings_update_msg_t* update = (settings_update_msg_t*)msg->data;
     
     // Validate checksum
     // Apply setting based on setting_id
     // Save to NVS
     bool success = save_setting_to_nvs(update->setting_id, update->value);
     
     // Send ACK back to receiver
     settings_update_ack_msg_t ack;
     ack.type = msg_settings_update_ack;
     ack.setting_id = update->setting_id;
     ack.success = success;
     if (!success) {
       strcpy(ack.error_msg, "NVS write failed");
     }
     esp_now_send(msg->mac, (uint8_t*)&ack, sizeof(ack));
   }
   ```

6. **Add NVS helper functions**
   ```cpp
   // In ESPnowtransmitter2/src/storage/nvs_settings.cpp
   bool save_setting_to_nvs(uint8_t setting_id, uint32_t value);
   bool load_settings_from_nvs();
   void restore_default_settings();
   ```

**Testing Phase 2**:
- [ ] Change setting on receiver web page
- [ ] Click Save button
- [ ] Verify ESP-NOW message sent
- [ ] Verify transmitter receives and stores to NVS (dummy storage for now)
- [ ] Verify ACK received by receiver
- [ ] Reload page and verify setting persists
- [ ] Reboot transmitter and verify setting persists
- [ ] **Unit test**: Settings serialization/deserialization
- [ ] **Unit test**: NVS storage layer (mock)
- [ ] **Unit test**: ACK timeout handling

**Deliverable**: Bidirectional settings management working with dummy NVS backend

---

### **Phase 3: Web Page Migration** (5-7 days)
**Goal**: Move Battery Emulator web pages to receiver

#### Battery Emulator Web Pages (from settings_body.html analysis):
1. **Main Status Page** - Live battery metrics, charts
2. **Battery Settings** - Capacity, limits, SOC range
3. **Charger Settings** - Charge power, voltage limits
4. **Inverter Settings** - Discharge power, AC settings
5. **System Settings** - MQTT, WiFi, logging
6. **Hardware Config** - GPIO options (LILYGO 2CAN)
7. **Events/Logs** - Error history, event log
8. **Cell Info** - Individual cell voltages

#### Receiver Current Pages:
1. `/` - Monitor (SOC, power)
2. `/ota` - Firmware updates
3. `/debug` - Debug level
4. `/settings` - Basic settings
5. `/systeminfo` - Device info
6. `/reboot` - Reboot control

#### Migration Mapping:

| Battery Emulator Page | Receiver Destination | Priority | Effort |
|----------------------|---------------------|----------|--------|
| Main Status | `/` (enhance existing) | HIGH | 2 days |
| Battery Settings | `/battery_settings` (new) | HIGH | 2 days |
| Charger Settings | `/charger_settings` (new) | MEDIUM | 1 day |
| Inverter Settings | `/inverter_settings` (new) | MEDIUM | 1 day |
| System Settings | `/settings` (enhance) | HIGH | 1 day |
| Events/Logs | `/events` (new) | LOW | 2 days |
| Cell Info | `/cells` (new) | MEDIUM | 2 days |

#### Steps:

**1. Enhance Main Monitor Page** (2 days)
- Add battery voltage, current, temp display
- Add charger status (HV/LV voltage, current)
- Add inverter status (AC voltage, power)
- Add contactor states (main+, main-, precharge, charger)
- Add real-time charts (SOC history, power graph)
- Keep existing test mode functionality

**Files**:
- `espnowreciever_2/lib/webserver/pages/monitor_page.cpp`
- `espnowreciever_2/lib/webserver/api/api_handlers.cpp` (add /api/battery_full_status)

**2. Create Battery Settings Page** (2 days)
- Form fields: capacity, max charge current, max discharge current
- Form fields: high SOC limit, low SOC limit
- Form fields: max voltage, min voltage
- Save button (uses Phase 2 settings update mechanism)
- Load current values from transmitter on page load

**Files**:
- `espnowreciever_2/lib/webserver/pages/battery_settings_page.cpp` (new)
- Register in page_definitions.cpp

**3. Create Charger Settings Page** (1 day)
- Form fields: max charge power, target voltage
- Form fields: charger type selection
- Save/load mechanism

**Files**:
- `espnowreciever_2/lib/webserver/pages/charger_settings_page.cpp` (new)

**4. Create Inverter Settings Page** (1 day)
- Form fields: max discharge power, inverter type
- Form fields: AC voltage/frequency settings
- Save/load mechanism

**Files**:
- `espnowreciever_2/lib/webserver/pages/inverter_settings_page.cpp` (new)

**5. Enhance System Settings Page** (1 day)
- Add MQTT settings (server, port, username, password)
- Add SD card logging toggle
- Add CAN logging toggle
- Keep existing debug level control

**Files**:
- `espnowreciever_2/lib/webserver/pages/settings_page.cpp` (modify)

**6. Create Events/Logs Page** (2 days)
- Table of recent events with timestamps
- Severity indicators (info/warning/error)
- Clear all button
- Auto-refresh via SSE
- Graceful handling when no events available (show "No events")

**Files**:
- `espnowreciever_2/lib/webserver/pages/events_page.cpp` (new)
- Add ESP-NOW message for event history
- **Unit test**: Event filtering and sorting

**7. Create Cell Info Page** (2 days)
- Table of all cell voltages
- Highlight min/max cells
- Cell balancing status
- Temperature distribution
- Handle unavailable cell data (show placeholders)

**Files**:
- `espnowreciever_2/lib/webserver/pages/cells_page.cpp` (new)
- Add ESP-NOW message for cell data array
- **Unit test**: Cell voltage min/max detection

**8. Update Navigation** (0.5 days)
- Add new pages to nav_buttons.cpp
- Organize into categories (Monitor, Settings, Diagnostics)

**Testing Phase 3**:
- [ ] All pages render correctly
- [ ] Save buttons work on all settings pages (with dummy data)
- [ ] Values persist across reboots (dummy NVS)
- [ ] Real-time updates via SSE work with dummy data
- [ ] Navigation flows smoothly
- [ ] Mobile responsive design
- [ ] Graceful handling when transmitter data unavailable (show "Waiting for data...")
- [ ] Placeholder text for missing data items
- [ ] **Unit test**: Page rendering with null/undefined data
- [ ] **Unit test**: SSE reconnection logic
- [ ] **Unit test**: Settings validation (range checks)

**Deliverable**: Complete web interface on receiver matching Battery Emulator functionality (using dummy ESP-NOW data)

**IMPORTANT**: At end of Phase 3, receiver web interface is FULLY FUNCTIONAL with dummy data. This allows parallel transmitter development in Phase 4.

---

### **Phase 4: Transmitter Core Integration** (5-7 days)
**Goal**: Integrate Battery Emulator control logic into transmitter

#### Architecture Decision:
**CONFIRMED**: Port Battery Emulator code directly to transmitter (no bridge layer)
- Cleaner codebase
- Better performance
- Easier to maintain
- Single hardware platform (ESP32-POE-ISO only)

#### Pre-Phase 4 Note:
**Phase 3 (Web Pages) is now COMPLETE** before starting Phase 4. This means:
- Receiver web interface is fully functional
- All pages tested with dummy ESP-NOW data
- Settings save/load working
- SSE updates working
- Now ready to replace dummy data with real control loop data

#### Steps:

**1. Create transmitter project structure** (1 day)
```
ESPnowtransmitter2/espnowtransmitter2/src/
  battery_control/        # NEW: Battery Emulator core logic
    ├── battery/          # Copied from Battery Emulator
    ├── charger/          # Copied from Battery Emulator
    ├── inverter/         # Copied from Battery Emulator
    ├── communication/    # Modified for ESP-NOW
    │   ├── can/          # Kept (CAN bus communication)
    │   ├── contactorcontrol/  # Kept
    │   ├── precharge_control/ # Kept
    │   └── espnow_bridge/     # NEW: Replaces webserver communication
    ├── datalayer/        # Copied (central data structure)
    └── control_loop/     # NEW: Main control task
```

**2. Port datalayer structure** (1 day)
- Copy `datalayer.h` and `datalayer.cpp`
- Adapt for ESP32-POE-ISO hardware (no display, no SD card initially)
- Keep all battery/charger/inverter structures intact

**3. Create simulated battery interface** (1 day) **[NO REAL HARDWARE]**
- **SIMULATE** battery BMS data (no real LEAF/Tesla/BMW hardware)
- Create dummy CAN message generator (realistic voltage/current/SOC patterns)
- Adapt for hardware-specific CAN pins (prepared for future real hardware)
- **Unit tests**: CAN message parser with known test vectors

**4. Create simulated charger interface** (1 day) **[NO REAL HARDWARE]**
- **SIMULATE** charger controller data
- Create dummy CAN messages for HV/LV voltage, current, power
- Adapt for ESP-NOW status reporting
- **Unit tests**: Charger state transitions

**5. Create simulated inverter interface** (1 day) **[NO REAL HARDWARE]**
- **SIMULATE** inverter controller data
- Create dummy CAN messages for AC voltage, frequency, power
- Adapt for ESP-NOW status reporting
- **Unit tests**: Inverter control logic

**6. Create control loop task** (1 day) **[SIMULATED HARDWARE]**
```cpp
// ESPnowtransmitter2/src/battery_control/control_loop/control_task.cpp
void battery_control_loop(void* parameter) {
  // HIGH PRIORITY TASK - Core 0
  const TickType_t interval = pdMS_TO_TICKS(10); // 10ms
  TickType_t last_wake = xTaskGetTickCount();
  
  while (true) {
    uint32_t start_time = micros();
    
    // 1. Read CAN messages (battery, charger, inverter)
    // NO REAL HARDWARE: Generate simulated CAN data
    simulate_can_messages();
    
    // 2. Update datalayer from simulated CAN data
    update_battery_status();    // Simulated SOC decay/regen
    update_charger_status();    // Simulated charging logic
    update_inverter_status();   // Simulated power flow
    
    // 3. Run control logic
    update_contactors();        // Simulated (log state only)
    update_precharge();         // Simulated timing sequence
    check_safety_limits();      // Real logic, dummy values
    
    // 4. Send CAN messages (commands to charger/inverter)
    // NO REAL HARDWARE: Log intended commands
    log_simulated_can_commands();
    
    // 5. Send ESP-NOW status updates - DO NOT SEND FROM THIS TASK
    // Battery control loop should ONLY focus on real-time control
    // ESP-NOW updates are handled by separate lower-priority task
    // This prevents ESP-NOW latency from affecting control loop timing
    
    // Timing profiling
    uint32_t loop_time_us = micros() - start_time;
    if (loop_time_us > 10000) {
      LOG_WARN("Control loop exceeded 10ms: %u us", loop_time_us);
    }
    
    // Maintain precise 10ms timing
    vTaskDelayUntil(&last_wake, interval);
  }
}

// NOTE: When real hardware available, replace simulate_*() calls with real CAN I/O
// All control logic remains identical
```

**7. Adjust task priorities** (1 day)
```cRITICAL PRIORITY STRUCTURE:
// The main battery control loop is the HIGHEST priority task.
// ALL other tasks are subordinate and must not interfere.

// New priorities (FreeRTOS: higher number = higher priority):
// - Battery control loop: Priority 4 (HIGHEST - Core 0) - 10ms cycle MUST be maintained
// - CAN TX/RX: Priority 3 (Core 0) - Real-time communication with BMS/charger/inverter
// - ESP-NOW RX handler: Priority 2 (Core 1) - Message processing (not time-critical)
// - ESP-NOW data sender: Priority 1 (Core 1) - Send status updates when CPU available
// - Discovery/Announcements: Priority 1 (Core 1)
// - MQTT telemetry: Priority 0 (Core 1) - Background only, no time constraints
// - Ethernet/NTP: Priority 0 (Core 1) - Background tasks

// IMPORTANT: ESP-NOW status updates are NOT time-critical
// - Webserver display can tolerate delays (100ms-1000ms is acceptable)
// - LCD display updates are low priority
// - SSE updates happen when data arrives, no strict timing required
// - Data sender, MQTT: Priority 0 (background)
```

**8. Remove dummy data generator** (0.5 days)
- Delete dummy_data_generator.cpp (created in Phase 1)
- Remove dummy_data_task
- Verify receiver now receives data from real control loop

**9. Remove WiFi AP code** (0.5 days)
- Remove WiFi AP initialization
- Keep WiFi in STA mode for ESP-NOW only
- Remove webserver from transmitter
- Remove ElegantOTA (replaced by receiver OTA page)

**10. Create ESP-NOW data sender task** (1 day)
```cpp
// Separate LOW PRIORITY task for ESP-NOW updates
// This task reads from datalayer and sends to receiver
// NO strict timing requirements - updates happen when CPU available

void espnow_data_sender_task(void* parameter) {
  // LOW PRIORITY - Core 1
  const TickType_t interval = pdMS_TO_TICKS(200); // 200ms updates (5 Hz)
  TickType_t last_wake = xTaskGetTickCount();
  
  while (true) {
    // Send battery status (only if receiver connected)
    if (receiver_connected) {
      send_battery_status_espnow();
      
      // Stagger updates to avoid bursts
      vTaskDelay(pdMS_TO_TICKS(20));
      send_charger_status_espnow();
      
      vTaskDelay(pdMS_TO_TICKS(20));
      send_inverter_status_espnow();
      
      vTaskDelay(pdMS_TO_TICKS(20));
      send_system_status_espnow();
    }
    
    // Wait for next cycle (NOT strict timing, just nominal interval)
    vTaskDelayUntil(&last_wake, interval);
  }
}
```

**Testing Phase 4** (NO REAL HARDWARE - USING DUMMY VALUES):
- [ ] Control loop runs at STABLE 10ms (measured with timing logs)
- [ ] Control loop timing NEVER exceeds 10ms even with heavy ESP-NOW traffic
- [ ] **DUMMY**: CAN messages simulated (no real BMS/charger/inverter)
- [ ] **DUMMY**: Contactors simulated (log state changes only)
- [ ] **DUMMY**: Precharge sequence simulated
- [ ] Battery data flows to receiver (timing not critical - 200ms-1000ms acceptable)
- [ ] Settings updates from receiver work (can take several seconds, not an issue)
- [ ] Task priorities NEVER interfere with control loop
- [ ] Ethernet/MQTT continues working
- [ ] Receiver web interface now shows LIVE simulated data (not dummy generator)
- [ ] Remove dummy_data_generator task (replaced by real control loop data)
- [ ] **Unit test**: Control loop timing profiling
- [ ] **Unit test**: CAN message parser (with mock data)
- [ ] **Unit test**: Contactor state machine
- [ ] **Unit test**: Safety limit checks

**Deliverable**: Fully functional battery control system on transmitter with ESP-NOW communication (using simulated hardware, no real BMS/charger/inverter)

**NOTE**: Real hardware integration (when available) will be drop-in replacement - no architecture changes needed

---

### **Phase 5: LED Migration** (2 days)
**Goal**: Move LED control to simulated LED on receiver display

#### Current:
- Battery Emulator uses physical LED (RGB or single color)
- LED states: Red (fault), Orange (warning), Green (OK), blinking patterns

#### Target:
- Receiver has simulated LED on TFT display
- Already supports Red/Orange/Green colors
- **Receiver already handles flashing logic** - transmitter just sends color
- May need to add flash type parameter later (heartbeat, different patterns) if required

#### Steps:
 (SIMPLIFIED)
```cpp
// SIMPLE VERSION - Receiver handles all flashing logic
typedef struct __attribute__((packed)) {
  uint8_t type;           // msg_flash_led (already exists!)
  uint8_t color;          // LED_RED/LED_GREEN/LED_ORANGE (existing enum)
} flash_led_t;

// NOTE: msg_flash_led and flash_led_t already exist in espnow_common.h
// Receiver already implements flashing in display_led.cpp
// Just need to update transmitter to send correct color based on system state

// FUTURE ENHANCEMENT (if needed):
// Add optional flash pattern parameter:
// typedef struct __attribute__((packed)) {
//   uint8_t type;         // msg_flash_led
//   uint8_t color;        // LED_RED/LED_GREEN/LED_ORANGE
//   uint8_t pattern;      // FLASH_NORMAL/FLASH_HEARTBEAT/FLASH_RAPID (optional)
// } flash_led_t LED_PULSE = 3          // Breathing effect
};
```

**2. Update transmitter LED logic** (SIMPLIFIED)
```cpp
// In ESPnowtransmitter2/src/battery_control/led_control.cpp
void update_led_state() {
  uint8_t color;
  
  // Determine LED color based on system state
  if (datalayer.battery.status.bms_status == FAULT) {
    color = LED_RED;
  } else if (datalayer.battery.status.bms_status == WARNING) {
    color = LED_ORANGE;
  } else {
    color = LED_GREEN;
  }
  
  // Send to receiver (reuse existing msg_flash_led message type)
  flash_led_t msg;
  msg.type = msg_flash_led;
  msg.color = color;
  esp_now_send(receiver_mac, (uint8_t*)&msg, sizeof(msg));
}

// NOTES:
// - Receiver display_led.cpp already handles flashing automatically
// - No pattern parameter needed initially
// - If heartbeat or other patterns needed later, add pattern field and update receiver
```

**3. Receiver LED display** (NO CHANGES NEEDED)
```cpp
// In espnowreciever_2/src/espnow/espnow_tasks.cpp
// LED handler ALREADY EXISTS and works correctly:
void handle_flash_led_message(const espnow_queue_msg_t* msg) {
  const flash_led_t* led_msg = (const flash_led_t*)msg->data;
  
  // Update LED color (existing code)
  ESPNow::current_led_color = (LEDColor)led_msg->color;
  ESPNow::dirty_flags.led_changed = true;
  
  LOG_DEBUG("LED color set to: %d", led_msg->color);
}

// Receiver display_led.cpp already implements:
// - Automatic flashing/blinking
// - Gradient transitions
// - Color updates
// - All rendering logic

// NO CHANGES NEEDED - existing code handles everything!

// FUTURE: If heartbeat or special patterns needed, can add:
// - Check for optional pattern field in message
// - Implement new patterns in existing animation system
```

**4. Remove physical LED code from transmitter**
- No physical LED on ESP32-POE-ISO
- Remove LED GPIO initialization
- Keep LED state calculation logic (for ESP-NOW)

**Testing Phase 5**:
- [ ] Fault condition shows red LED on receiver (flashing handled by receiver)
- [ ] Warning shows orange LED
- [ ] Normal operation shows green LED
- [ ] LED transitions are smooth (existing receiver code)
- [ ] LED updates when state changes (no strict timing requirement)
- [ ] Check if existing flashing is adequate, or if heartbeat pattern needed

**Deliverable**: Simulated LED on receiver display matching transmitter state

**FUTURE ENHANCEMENT** (if needed):
- [ ] Add heartbeat pattern support
- [ ] Add rapid flash pattern
- [ ] Update message structure to include pattern field

---

### **Phase 6: Hardware Simplification** (1 day)
**Goal**: Lock transmitter to ESP32-POE-ISO hardware only

#### Steps:

**1. Update platformio.ini**
```ini
[env:esp32-poe-iso]
platform = espressif32@6.5.0
board = esp32-poe-iso
framework = arduino
build_flags = 
    -D HW_ESPNOW_BATTERY_CONTROLLER
    -D TRANSMITTER_DEVICE
    -D FW_VERSION_MAJOR=2
    -D FW_VERSION_MINOR=0
    -D FW_VERSION_PATCH=0
    # ... existing flags
```

**2. Remove multi-hardware ifdefs**
```cpp
// Replace:
#if defined(HW_LILYGO) || defined(HW_STARK) || ...

// With:
#ifdef HW_ESPNOW_BATTERY_CONTROLLER
```

**3. Remove unused hardware configs**
- Delete LILYGO GPIO options
- Delete STARK board definitions
- Keep only ESP32-POE-ISO specifics

**4. Update documentation**
- README.md: Single hardware platform
- Pin assignments specific to ESP32-POE-ISO
- CAN bus wiring diagram

**Testing Phase 6**:
- [ ] Firmware builds without errors
- [ ] No unused hardware code
- [ ] Simplified configuration

**Deliverable**: Clean, single-hardware transmitter codebase

---

### **Phase 7: Virgin Board Provisioning** (DEFERRED)
**Status**: SKIPPED for initial migration - to be implemented later

#### Rationale:
- Not required for initial migration
- Discovery mechanism already exists (transmitter broadcasts, receiver responds)
- Can provision manually via serial console if needed
- Defer to future version once core functionality is stable

#### Future Implementation Notes:
When implemented, provisioning should:
- Use existing ESP-NOW discovery mechanism
- Store receiver MAC in NVS after first connection
- Support factory reset to clear pairing
- NOT use WiFi AP (keep transmitter ESP-NOW only)

**SKIPPED - Moving to Phase 8 (Testing)**

---

### **Phase 8: Testing & Validation** (3-5 days)
**Goal**: Comprehensive end-to-end testing

#### Test Categories:

**1. Functional Tests**
- [ ] Battery data displayed on receiver web page
- [ ] Settings changes saved and persist
- [ ] LED indicators work correctly
- [ ] OTA updates work for both devices
- [ ] MQTT telemetry published
- [ ] Ethernet connectivity stable
- [ ] CAN communication with BMS/charger/inverter
- [ ] Contactor control works
- [ ] Precharge sequence operates correctly

**2. Performance Tests**
- [ ] Control loop maintains 10ms cycle time CONSISTENTLY (critical!)
- [ ] Control loop NEVER exceeds 10ms even under heavy load
- [ ] ESP-NOW latency not critical (200ms-1000ms acceptable for display updates)
- [ ] Web page load time < 3 seconds (acceptable)
- [ ] SSE updates depend on ESP-NOW timing - NOT time-critical
- [ ] No memory leaks over 24 hours
- [ ] Task stack usage within limits
- [ ] Control loop has ZERO interference from ESP-NOW/web tasks

**3. Reliability Tests**
- [ ] Automatic recovery from lost ESP-NOW connection
- [ ] Receiver reboot doesn't affect transmitter control
- [ ] Transmitter reboot recovers gracefully
- [ ] Network outage doesn't affect core control
- [ ] Settings corruption detection and recovery

**4. Safety Tests**
- [ ] Overcurrent protection works
- [ ] Overvoltage protection works
- [ ] Undervoltage protection works
- [ ] Contactor fault detection
- [ ] Emergency shutdown via receiver web page

**5. Integration Tests**
- [ ] Complete charge cycle
- [ ] Complete discharge cycle
- [ ] Mode transitions (idle → charge → discharge)
- [ ] Multi-hour continuous operation

**Testing Tools** (NO REAL HARDWARE):
- Serial logging for timing validation (replaces oscilloscope)
- Software timing profiler (ESP32 cycle counters)
- Simulated CAN data generator (replaces real BMS/charger/inverter)
- Unit test framework (CppUTest or Unity)
- Code coverage tools (gcov/lcov)

**FUTURE**: When real hardware available:
- Logic analyzer for CAN bus
- Oscilloscope for timing validation
- Power supply for battery simulation
- Load bank for discharge testing

**Deliverable**: Fully tested system with simulated hardware (production-ready architecture, pending real hardware validation)

---

### **Phase 9: Documentation** (2-3 days)
**Goal**: Complete user and developer documentation

#### Documents to Create:

**1. User Guide**
- Hardware setup and wiring
- Initial provisioning process
- Web interface walkthrough
- Settings reference
- Troubleshooting guide

**2. Developer Documentation**
- Architecture overview
- ESP-NOW message protocol specification
- Data flow diagrams
- Task priority and timing analysis
- Code organization guide

**3. Migration Guide**
- Differences from original Battery Emulator
- Breaking changes
- Configuration migration steps
- Rollback procedure

**4. API Reference**
- All ESP-NOW message types
- REST API endpoints
- Settings structure
- Event codes

**5. Hardware Documentation**
- Bill of materials
- Wiring diagrams
- CAN bus termination
- Power supply requirements

**Files to Create**:
- `docs/USER_GUIDE.md`
- `docs/DEVELOPER_GUIDE.md`
- `docs/MIGRATION_FROM_BATTERY_EMULATOR.md`
- `docs/API_REFERENCE.md`
- `docs/HARDWARE_SETUP.md`
- `docs/TROUBLESHOOTING.md`

**Deliverable**: Complete documentation suite

---

## Summary Timeline

| Phase | Duration | Dependencies | Deliverable |
|-------|----------|--------------|-------------|
| 0: Pre-Migration | 1-2 days | None | Workspace setup, documentation |
| 1: Data Layer | 3-4 days | Phase 0 | ESP-NOW message definitions + dummy sender |
| 2: Settings Flow | 4-5 days | Phase 1 | Bidirectional settings sync (dummy data) |
| 3: Web Pages | 5-7 days | Phase 2 | **Complete web interface (dummy data)** |
| 4: Core Integration | 5-7 days | Phase 3 | **Battery control on transmitter (live data)** |
| 5: LED Migration | 1 day | Phase 4 | Simulated LED display (simplified) |
| 6: Hardware Simplify | 1 day | Phase 4 | Single hardware config |
| 7: Provisioning | SKIPPED | - | (Deferred to future version) |
| 8: Testing | 3-5 days | Phase 1-6 | Validated system (dummy data) |
| 9: Documentation | 2-3 days | Phase 8 | Complete docs |

**Total Duration**: 25-38 days (~5-8 weeks)**
*Reduced from original 6-9 weeks due to provisioning deferral and LED simplification*

**CRITICAL: Phase 3 (Web Pages) comes BEFORE Phase 4 (Core Integration)**
- Allows receiver web UI development in parallel
- Enables testing with dummy ESP-NOW messages
- Provides working interface before real hardware integration
- Dummy data generator on transmitter for Phase 1-3 testing

---

## Risk Mitigation

### High Risk Items

**1. Control Loop Timing** (CRITICAL)
- **Risk**: ESP-NOW overhead breaks 10ms cycle
- **Mitigation**: 
  - **ESP-NOW is LOWER priority than control loop** (confirmed architecture)
  - Control loop on Core 0, Priority 4 (highest)
  - ESP-NOW sender on Core 1, Priority 1 (low)
  - NEVER send ESP-NOW from control loop task
  - Control loop only does: CAN I/O, datalayer update, contactor logic
  - Separate task handles all ESP-NOW transmission
  - Profile timing extensively with oscilloscope
- **Fallback**: Reduce ESP-NOW update frequency to 500ms or 1000ms (acceptable for display)
- **Acceptance**: Display updates can be delayed - NOT time-critical

**2. Settings Corruption**
- **Risk**: NVS corruption during power loss
- **Mitigation**:
  - Use NVS commit transactions
  - Validate settings on load
  - Maintain default settings in code
- **Fallback**: Factory reset button

**3. ESP-NOW Reliability**
- **Risk**: Lost messages in noisy environment
- **Mitigation**:
  - Implement message retry with timeout
  - Use checksums on all messages
  - Monitor connection quality
- **Fallback**: Auto-reconnect with exponential backoff

### Medium Risk Items

**1. Web Page Complexity**
- **Risk**: Settings pages too complex to migrate cleanly
- **Mitigation**:
  - Start simple, iterate
  - Prioritize critical settings first
  - Defer advanced features to later phases
- **Fallback**: Keep some settings transmitter-only via serial console

**2. CAN Bus Conflicts**
- **Risk**: CAN timing affected by ESP-NOW
- **Mitigation**:
  - CAN on high priority task
  - Profile CAN bus load
  - Separate cores for CAN and ESP-NOW
- **Fallback**: Reduce ESP-NOW update rate

**3. Memory Constraints**
- **Risk**: Combined codebase too large for flash
- **Mitigation**:
  - Remove unused battery/charger/inverter drivers
  - Use partitions efficiently
  - Profile flash usage
- **Fallback**: External flash or code optimization

---

## Code Review Suggestions (Deferred)

The following improvements should be implemented AFTER migration is complete:

### Performance Optimizations
1. **Zero-copy ESP-NOW**: Use DMA buffers directly
2. **Message batching**: Combine multiple updates into single packet
3. **Differential updates**: Only send changed values
4. **Compression**: LZ4 compression for large data transfers

### Code Quality
1. **Unit tests**: Add CppUTest framework
2. **Static analysis**: Enable clang-tidy checks
3. **Memory profiling**: Valgrind equivalent for ESP32
4. **Code coverage**: Track test coverage

### Architecture
1. **State machines**: Formalize control logic as state machine
2. **Observer pattern**: Decouple data updates from UI
3. **Dependency injection**: Improve testability
4. **Interface abstraction**: Abstract hardware interfaces

### Maintainability
1. **Doxygen**: Add comprehensive code documentation
2. **Design patterns**: Document architectural patterns used
3. **Module boundaries**: Enforce strict layer separation
4. **Error handling**: Standardize error codes and handling

---

## Success Criteria

**Phase 1-4 Success** (Core Functionality):
- [ ] Real-time battery control working on transmitter
- [ ] Web interface displaying all critical data
- [ ] Settings changes save successfully
- [ ] 10ms control loop timing maintained
- [ ] Zero data loss in ESP-NOW communication

**Phase 5-7 Success** (Integration):
- [ ] LED simulation working correctly
- [ ] Single hardware configuration clean
- [ ] Virgin board auto-provisioning works

**Phase 8-9 Success** (Production Ready):
- [ ] All functional tests pass
- [ ] Performance within specifications
- [ ] Safety systems validated
- [ ] Documentation complete

**Overall Migration Success**:
- [ ] Feature parity with Battery Emulator 9.2.4
- [ ] Improved performance (lower transmitter load)
- [ ] Better user experience (responsive web UI)
- [ ] Maintainable two-device architecture
- [ ] Production-ready reliability

---

## Key Design Decisions (CONFIRMED)

1. **Architecture**: Port Battery Emulator code directly (no bridge layer) ✓
2. **Hardware**: Lock to ESP32-POE-ISO only (single platform) ✓
3. **Task Priority**: Control loop is HIGHEST priority, ESP-NOW/display are LOWEST ✓
4. **Timing**: Display updates NOT time-critical (200ms-1000ms acceptable) ✓
5. **LED Control**: Send color only, receiver handles flashing ✓
6. **Provisioning**: Deferred to future version ✓
7. **Phase Ordering**: Web pages (Phase 3) BEFORE core integration (Phase 4) ✓
8. **Testing Strategy**: Incremental unit tests during migration (each phase) ✓
9. **Hardware Testing**: No BMS/charger/inverter - use dummy values ✓
10. **Rollback Plan**: None - one-way commitment to new architecture ✓
11. **Version Numbering**: v2.0.0 (v1.1.1 baseline + Battery Emulator migration) ✓
12. **Backwards Compatibility**: Not needed (fresh start with v2.0.0) ✓

---

**Document Version**: 1.0  
**Created**: February 5, 2026  
**Last Updated**: February 5, 2026  
**Author**: AI Assistant  
**Status**: Draft for Review

**Next Steps**:
1. Review and approve migration plan
2. Set up Phase 0 workspace
3. Begin Phase 1 implementation
4. Schedule weekly progress reviews
