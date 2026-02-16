# Stage 1: Data Layer Packets + Transport Choice (Expanded)

## Architecture Reminder: Dual-Board Separation

### Original (Battery Emulator 9.2.4)
- **Single board** (multiple hardware variants: LilyGo, Stark, etc.)
- Runs battery control + WiFi AP + web UI + display all together
- Control loop and UI are tightly coupled
- All data in shared memory

### New (Dual-Board Design)
- **Transmitter (Olimex ESP32-POE2 + Waveshare RS485/CAN HAT B)**: Pure battery/inverter control
  - CAN I/O with BMS, charger, inverter via MCP2515 SPI interface (see [HARDWARE_HAL.md](../Migration/HARDWARE_HAL.md))
  - 10ms control loop (contactors, precharge, safety)
  - Runs independently of receiver
  - **Sends ESP-NOW summaries to receiver ONLY when needed** (on-change, on-request, or low-rate) via WiFi peer-to-peer
  - **Publishes MQTT telemetry to broker via Ethernet** (independent transport channel for persistence)
  - If receiver is offline → transmitter still operates normally (both ESP-NOW and MQTT are independent)
  
- **Receiver (LilyGo T-Display-S3)**: Pure UI and monitoring
  - Receives **ESP-NOW summaries** from transmitter via WiFi peer-to-peer (real-time, low-latency)
  - **Subscribes to MQTT broker via WiFi** (for persistent event logs and cell data)
  - Displays on web page and TFT screen
  - Can modify settings (sent back to transmitter via ESP-NOW)
  - If offline → no impact on transmitter operation (transmitter continues MQTT publishing independently via Ethernet)

### Key Implication
- **Real-time monitoring is NOT a core requirement**
- Transmitter's job: manage battery/inverter safely
- Receiver's job: show status and allow config changes
- Data rates should be **low** (1–5s summaries, on-request for details)

---

## Cell Data Analysis: Can ESP-NOW Handle It?

### Size Constraints
```
Single battery with 96 cells:
  - Cell voltages: 96 cells × 2 bytes = 192 bytes
  - Cell temps: 16 temps × 1 byte = 16 bytes
  - Balance state: 96 bits = 12 bytes
  Total: ~220 bytes

Dual battery systems:
  - 2 × 220 = 440 bytes (EXCEEDS single packet)
```

### ESP-NOW Transport
- **Max packet**: 250 bytes (theoretical max)
- **Typical payload**: ~200–230 bytes after overhead
- **Single battery cell array**: ✓ FITS (192 bytes)
- **Dual battery cell arrays**: ✗ Does NOT fit in single packet
- **Latency**: <100ms typical
- **Rate**: Can handle 1–10 packets/sec easily
- **Reliability**: Unacknowledged by default; can add retries
- **Best use**: Low-rate summaries, settings, immediate events

### MQTT Transport
- **Max packet**: Effectively unlimited (multi-frame TCP)
- **Cell data**: ✓ Ideal for large payloads
- **Storage**: ✓ Broker persists messages indefinitely
- **Latency**: 100ms–10s (depends on broker + network)
- **Rate**: Broker can handle hundreds of messages/sec
- **Best use**: Historical logs, cell arrays, events with storage

---

## Recommended Transport Split

| Data Type | Size | Rate | ESP-NOW | MQTT | Recommendation |
|-----------|------|------|---------|------|-----------------|
| System Status (heartbeat) | ~50 bytes | 1–5s | ✓ | ✓ | ESP-NOW for UI; optional MQTT for history |
| Battery Summary (SOC, V/I/P) | ~60 bytes | 1–5s | ✓ | ✓ | ESP-NOW for UI; optional MQTT for plots |
| Inverter Summary (AC power) | ~40 bytes | 1–5s | ✓ | ✓ | ESP-NOW for UI; optional MQTT |
| Charger Summary (DC power) | ~40 bytes | 5–10s | ✓ | ✓ | ESP-NOW for UI; optional MQTT |
| Cell Data (single battery) | 192 bytes | on-request | ✓ | ✓ | **ESP-NOW if <1 Hz; MQTT for storage** |
| Cell Data (dual battery) | 384 bytes | on-request | ✗ | ✓ | **MQTT only (too large for single packet)** |
| Events/Faults | ~30 bytes | on-event | ✓ | ✓ | **ESP-NOW for instant UI; MQTT for log** |
| Settings (one category) | ~80 bytes | on-request | ✓ | — | **ESP-NOW only (immediate feedback)** |

---

## Recommended Packet Set: ESP-NOW (Lightweight)

### 1. System Status (Heartbeat)
```
Purpose: Connection health, state summary, fault flags
Rate: 1–5s or on-change
Size: ~50 bytes

Fields:
  - uptime_sec (uint32_t) - seconds since boot
  - system_state (uint8_t) - IDLE/CHARGING/DISCHARGING/FAULT
  - fault_flags (uint32_t) - bitmask of active faults
  - contactor_main_plus (bool) - main contactor state
  - contactor_main_minus (bool)
  - contactor_charger (bool)
  - precharge_active (bool)
  - led_color (uint8_t) - current LED (for sync)
  - checksum (uint16_t)
```

### 2. Battery Status (Summary)
```
Purpose: UI display (SOC, V/I/P, temps)
Rate: 1–5s or on-change
Size: ~60 bytes
NOTE: Summaries only—NO cell-by-cell data in this packet

Fields:
  - soc_percent_100 (uint16_t) - SOC in 0.01% units (0-10000)
  - pack_voltage_mv (uint16_t) - pack voltage in mV
  - pack_current_ma (int16_t) - pack current in mA (signed)
  - pack_power_w (int16_t) - calculated power in W (signed)
  - temp_min_dc (int8_t) - min temp in 0.1°C units
  - temp_max_dc (int8_t) - max temp in 0.1°C units
  - max_charge_power_w (uint16_t)
  - max_discharge_power_w (uint16_t)
  - bms_status (uint8_t) - OK/WARNING/FAULT
  - checksum (uint16_t)
```

### 3. Inverter Status (Summary)
```
Purpose: UI display (AC power, mode)
Rate: 1–5s or on-change
Size: ~40 bytes

Fields:
  - inverter_state (uint8_t) - OFF/ON/FAULT/CHARGING
  - ac_voltage_v (uint16_t) - AC RMS voltage in 0.1V units
  - ac_current_a (uint16_t) - AC RMS current in 0.1A units
  - ac_power_w (int16_t) - real power in W (signed)
  - ac_freq_hz (uint16_t) - frequency in 0.1Hz units
  - fault_flags (uint16_t)
  - checksum (uint16_t)
```

### 4. Charger Status (Summary)
```
Purpose: UI display (charging power/status)
Rate: 5–10s or on-change
Size: ~40 bytes

Fields:
  - charger_state (uint8_t) - IDLE/CHARGING/FAULT
  - dc_voltage_v (uint16_t) - HV DC voltage in 0.1V units
  - dc_current_a (uint16_t) - charging current in 0.1A units
  - dc_power_w (uint16_t) - charging power in W
  - charger_temp_dc (int8_t) - charger temp in 0.1°C
  - fault_flags (uint8_t)
  - checksum (uint16_t)
```

### 5. Settings Snapshot (On-Request)
```
Purpose: Receiver refresh after reconnect
Rate: on-request or when version increments
Size: ~80–150 bytes (one category at a time)

Fields:
  - category (uint8_t) - BATTERY/INVERTER/CHARGER/SYSTEM
  - version (uint32_t) - settings version number
  - payload[] (variable) - serialized settings for that category
  - checksum (uint16_t)
```

### 6. Events (Immediate Alert)
```
Purpose: UI alerting + MQTT log
Rate: on-event only
Size: ~30 bytes

Fields:
  - event_code (uint16_t) - fault/warning/state-change code
  - severity (uint8_t) - INFO/WARNING/ERROR/CRITICAL
  - timestamp_sec (uint32_t) - seconds since boot
  - param1 (uint16_t) - context-dependent (voltage, current, etc.)
  - checksum (uint16_t)
```

### 7. Cell Data (On-Request or Low-Rate)
```
Purpose: Detailed cell voltages, temperatures, balancing info
Rate: on-request only; do NOT include in periodic broadcasts
Size: 192–384 bytes (single or dual battery)
Transport: ESP-NOW if <1 Hz; MQTT preferred for storage

**Single Battery**:
  - cell_voltages[96] (uint16_t × 96) - 192 bytes
  - cell_temps[16] (int8_t × 16) - 16 bytes
  - balance_state[12] (uint8_t × 12) - 12 bytes
  - checksum (uint16_t) - 2 bytes
  Total: ~222 bytes (fits ESP-NOW limit of 250)

**Dual Battery**:
  - battery1_cells[96] (uint16_t × 96) - 192 bytes
  - battery2_cells[96] (uint16_t × 96) - 192 bytes
  - Total: 384 bytes (EXCEEDS ESP-NOW limit—use MQTT)

**Recommendation**: 
- Single battery: Can use ESP-NOW request/response if needed
- Dual battery: Use MQTT topic instead; cell updates are not real-time-critical
- Always store in MQTT for analysis/plotting
```

---

## Recommended Packet Set: MQTT (Persistent Storage)

**MQTT Topics from Battery Emulator v9.2.4**

Base topic name (configurable): `BE` (Battery Emulator)

### 1. Primary Info Stream (Battery Status + Global Status)
```
Topic: BE/info

JSON Payload:
{
  "bms_status": "OK",
  "pause_status": "not paused",
  "event_level": "info",
  "emulator_status": "run",
  
  // Battery 1 data
  "SOC": 80.25,
  "SOC_real": 80.1,
  "state_of_health": 95.5,
  "temperature_min": 23.2,
  "temperature_max": 24.8,
  "cpu_temp": 42.5,
  "stat_batt_power": 5000,
  "battery_current": 50.2,
  "cell_max_voltage": 4.2,
  "cell_min_voltage": 4.18,
  "cell_voltage_delta": 20,
  "battery_voltage": 403.2,
  "total_capacity": 100000,
  "remaining_capacity_real": 80250,
  "remaining_capacity": 80250,
  "max_discharge_power": 50000,
  "max_charge_power": 25000,
  "charged_energy": 45000,
  "discharged_energy": 15000,
  "balancing_active_cells": 3,
  "balancing_status": "Active",
  
  // Battery 2 data (if dual-battery system)
  "SOC_2": 81.0,
  "SOC_real_2": 80.9,
  // ... same fields with _2 suffix
}

Rate: 5s (from Battery Emulator)
Retained: No
Availability topic: BE/status (publishes "online"/"offline")
```

### 2. Cell Data (Detailed Voltages)
```
Topic: BE/spec_data (Battery 1)
Topic: BE/spec_data_2 (Battery 2, if dual)

JSON Payload:
{
  "cell_voltages": [4.123, 4.121, 4.119, 4.120, ..., 4.122],  // 96 values in volts
  // Only cell_voltages array sent; no separate temps/balance
}

Rate: On-demand or if mqtt_transmit_all_cellvoltages enabled (rare)
Size per payload: ~800 bytes for 96 cells (JSON formatted)
Note: Receiver must enable by setting mqtt_transmit_all_cellvoltages = true
```

### 3. Home Assistant Auto-Discovery (Optional)
```
Topics: homeassistant/sensor/BE/{object_id}/config
         homeassistant/button/BE/{command}/config

Purpose: Home Assistant auto-discovery protocol (if using HA integration)
Payload: JSON with device metadata and entity definitions
Retained: Yes

Available buttons:
  - BMSRESET: Reset BMS
  - PAUSE: Pause charge/discharge
  - RESUME: Resume charge/discharge
  - RESTART: Restart Battery Emulator
  - STOP: Open Contactors

Receiver typically does NOT need this (it's for Home Assistant integrations).
```

### 4. Status Topic (Availability)
```
Topic: BE/status

Payload: "online" (when transmitter is connected) or "offline" (LWT)
Retained: Yes
Use: Receiver can check connection health by monitoring this topic
```

---

## MQTT Configuration for Receiver

### Required MQTT Settings (espnowreciever_2)
The receiver needs MQTT configuration in NVS (non-volatile storage):

```cpp
// Location: lib/webserver/utils/mqtt_config.h (CREATE NEW FILE)

typedef struct {
  bool enabled;              // Enable MQTT
  char broker_ip[64];        // MQTT broker IP or hostname
  uint16_t broker_port;      // MQTT broker port (default 1883)
  char username[32];         // MQTT username (if required)
  char password[64];         // MQTT password (if required)
  char topic_prefix[32];     // Topic prefix (default "BE")
  uint16_t publish_interval_ms;  // How often to re-subscribe (10000 ms)
} mqtt_config_t;
```

### NVS Keys for Receiver MQTT Config
```cpp
#define NVS_MQTT_ENABLED      "mqtt_en"       // bool
#define NVS_MQTT_BROKER       "mqtt_broker"   // string (IP:PORT)
#define NVS_MQTT_USER         "mqtt_user"     // string
#define NVS_MQTT_PASS         "mqtt_pass"     // string
#define NVS_MQTT_TOPIC        "mqtt_topic"    // string (default "BE")
```

### Configuration Page in Web UI
**Location**: Add section to `espnowreciever_2/lib/webserver/pages/settings_page.cpp`

```html
<fieldset>
  <legend>MQTT Configuration</legend>
  <div>
    <label>
      <input type="checkbox" id="mqttEn" />
      Enable MQTT
    </label>
  </div>
  <div>
    <label>Broker IP:Port</label>
    <input type="text" id="mqttBroker" placeholder="192.168.1.100:1883" />
  </div>
  <div>
    <label>Username (optional)</label>
    <input type="text" id="mqttUser" />
  </div>
  <div>
    <label>Password (optional)</label>
    <input type="password" id="mqttPass" />
  </div>
  <div>
    <label>Topic Prefix</label>
    <input type="text" id="mqttTopic" value="BE" />
  </div>
  <button onclick="saveMqttConfig()">Save MQTT Settings</button>
</fieldset>
```

---

## MQTT Implementation for Receiver

### Architecture: MQTT Client in Receiver

**New Files to Create**:
- `espnowreciever_2/lib/mqtt_client/mqtt_client.h` - MQTT client wrapper
- `espnowreciever_2/lib/mqtt_client/mqtt_client.cpp` - Implementation
- `espnowreciever_2/lib/mqtt_client/mqtt_handlers.h` - Message handlers
- `espnowreciever_2/lib/mqtt_client/mqtt_handlers.cpp` - Handler implementations

### 1. MQTT Client Initialization

```cpp
// mqtt_client.h
#include <esp_mqtt_client.h>

class MQTTClient {
 public:
  MQTTClient();
  ~MQTTClient();
  
  void init(const char* broker_uri, const char* username, const char* password);
  void connect();
  void disconnect();
  bool is_connected() const { return mqtt_connected; }
  void handle_message(const char* topic, const char* payload);
  
  // Subscribe to topics
  void subscribe_to_info();
  void subscribe_to_cell_data();
  void subscribe_to_status();
  
 private:
  esp_mqtt_client_handle_t client;
  bool mqtt_connected;
  char topic_prefix[32];
};
```

### 2. MQTT Message Handlers

```cpp
// mqtt_handlers.h
#include "mqtt_client.h"
#include "../transmitter_manager/transmitter_manager.h"

class MQTTHandlers {
 public:
  // Handle BE/info topic
  static void handle_info_message(const char* payload);
  
  // Handle BE/spec_data (cell voltages)
  static void handle_cell_data_message(const char* payload);
  
  // Handle BE/status (availability)
  static void handle_status_message(const char* payload);
  
 private:
  // Helper to parse JSON and update cache
  static void update_battery_cache_from_json(const char* payload);
  static void update_cell_data_from_json(const char* payload);
};
```

### 3. Subscribe to MQTT Topics

In receiver main initialization:

```cpp
// src/main.cpp (addition to setup)
void setup() {
  // ... existing setup code ...
  
  if (mqtt_config.enabled) {
    // Format broker URI: mqtt://ip:port
    String broker_uri = String("mqtt://") + mqtt_config.broker_ip + 
                       ":" + String(mqtt_config.broker_port);
    
    mqtt_client.init(
      broker_uri.c_str(),
      mqtt_config.username,
      mqtt_config.password
    );
    mqtt_client.connect();
    
    // Subscribe to all topics with prefix (e.g., BE/+)
    mqtt_client.subscribe_to_info();       // BE/info
    mqtt_client.subscribe_to_cell_data();  // BE/spec_data, BE/spec_data_2
    mqtt_client.subscribe_to_status();     // BE/status
  }
}
```

### 4. Update TransmitterManager Cache from MQTT

The receiver already has `TransmitterManager` class. Extend it to handle MQTT data:

```cpp
// In transmitter_manager.h, add:
struct MQTTBatteryData {
  float soc_percent;
  float pack_voltage_v;
  float pack_current_a;
  int32_t pack_power_w;
  float temp_min_c;
  float temp_max_c;
  std::string bms_status;
  std::string balancing_status;
  
  // Cell data
  std::vector<float> cell_voltages_v;  // 96 cells
  uint16_t cell_max_voltage_mv;
  uint16_t cell_min_voltage_mv;
  uint16_t cell_voltage_delta_mv;
  uint16_t balancing_active_cells;
};

// Add to TransmitterManager class:
void update_from_mqtt(const MQTTBatteryData& data);
MQTTBatteryData get_mqtt_battery_data() const;
```

### 5. Display MQTT Data in Web UI

**Monitor Page Enhancement** (`espnowreciever_2/lib/webserver/pages/monitor_page.cpp`):

```cpp
// Add MQTT data display section
String render_mqtt_battery_section() {
  String html = "<section class='mqtt-battery'>";
  html += "<h3>Battery Status (MQTT)</h3>";
  html += "<div id='mqtt-status'>";
  html += "  <p>SOC: <span id='mqtt-soc'>--</span> %</p>";
  html += "  <p>Voltage: <span id='mqtt-voltage'>--</span> V</p>";
  html += "  <p>Current: <span id='mqtt-current'>--</span> A</p>";
  html += "  <p>Power: <span id='mqtt-power'>--</span> W</p>";
  html += "  <p>Temp Min: <span id='mqtt-temp-min'>--</span> °C</p>";
  html += "  <p>Temp Max: <span id='mqtt-temp-max'>--</span> °C</p>";
  html += "  <p>BMS Status: <span id='mqtt-bms-status'>--</span></p>";
  html += "</div>";
  html += "</section>";
  return html;
}
```

### 6. SSE Updates from MQTT Data

When MQTT data arrives, push to SSE subscribers:

```cpp
// In mqtt_handlers.cpp
void MQTTHandlers::handle_info_message(const char* payload) {
  // Parse JSON payload
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  
  if (!error) {
    MQTTBatteryData data;
    data.soc_percent = doc["SOC"] | 0.0;
    data.pack_voltage_v = doc["battery_voltage"] | 0.0;
    data.pack_current_a = doc["battery_current"] | 0.0;
    data.pack_power_w = doc["stat_batt_power"] | 0;
    data.temp_min_c = doc["temperature_min"] | 0.0;
    data.temp_max_c = doc["temperature_max"] | 0.0;
    data.bms_status = doc["bms_status"] | "Unknown";
    
    // Update cache
    TransmitterManager::instance().update_from_mqtt(data);
    
    // Push SSE update to all connected clients
    sse_notifier.notify_mqtt_update();
  }
}
```

### 7. Cell Monitor Page (Using MQTT Cell Data)

**New Page**: `espnowreciever_2/lib/webserver/pages/cells_page.cpp`

```cpp
// Use MQTT cell data to render cell monitor
// Based on Battery Emulator cellmonitor_html.cpp design

String render_cell_monitor() {
  String html = "<div class='cell-monitor'>";
  html += "<h2>Cell Voltages</h2>";
  
  // Get cell data from MQTT cache
  MQTTBatteryData data = TransmitterManager::instance().get_mqtt_battery_data();
  
  if (data.cell_voltages_v.empty()) {
    html += "<p>No cell data available (enable mqtt_transmit_all_cellvoltages on transmitter)</p>";
  } else {
    // Display cell statistics
    html += "<div class='cell-stats'>";
    html += "<p>Max Voltage: <strong>" + String(data.cell_max_voltage_mv / 1000.0, 3) + " V</strong></p>";
    html += "<p>Min Voltage: <strong>" + String(data.cell_min_voltage_mv / 1000.0, 3) + " V</strong></p>";
    html += "<p>Delta: <strong>" + String(data.cell_voltage_delta_mv) + " mV</strong></p>";
    html += "<p>Balancing Cells: <strong>" + String(data.balancing_active_cells) + "</strong></p>";
    html += "</div>";
    
    // Render cell grid (based on Battery Emulator layout)
    html += "<div class='cell-grid'>";
    for (size_t i = 0; i < data.cell_voltages_v.size(); i++) {
      float voltage = data.cell_voltages_v[i];
      String cell_class = (voltage < data.cell_min_voltage_mv / 1000.0 + 0.05) ? "cell low-voltage" : "cell";
      html += "<div class='" + cell_class + "'>";
      html += "Cell " + String(i + 1) + "<br>";
      html += String(voltage, 3) + " V";
      html += "</div>";
    }
    html += "</div>";
  }
  
  html += "</div>";
  return html;
}
```

### 8. Legacy Code Removal

When implementing MQTT in receiver:

**REMOVE** dummy data generator if it exists:
```cpp
// DELETE if present: espnowreciever_2/src/dummy_data_generator.cpp/h
// DELETE: any hardcoded test data in transmitter_manager
```

**REMOVE** any old test MQTT code:
```cpp
// DELETE: any mock MQTT implementations
// DELETE: test topics or placeholder handlers
```

**KEEP** only:
- Real ESP-NOW message handlers (from transmitter)
- Real MQTT client integration (new)
- Unified data cache in TransmitterManager

---

## Stage 1 Implementation Checklist

### Phase A: Packet Definition
- [ ] **Define ESP-NOW status packets** in `ESP32common/espnow_transmitter/espnow_common.h`
  - System status struct (50 bytes)
  - Battery status struct (60 bytes)
  - Inverter status struct (40 bytes)
  - Charger status struct (40 bytes)
  - Settings snapshot struct (variable)
  - Event struct (30 bytes)
  - Cell data struct (single and dual) with size limit warnings
  - ALL structs use `__attribute__((packed))`

- [ ] **Verify packet sizes**
  - All ESP-NOW packets: <250 bytes ✓
  - Document assumptions (single vs dual battery) ✓
  - Add inline comments: "Fits ESP-NOW (X bytes used, Y bytes free)"

- [ ] **Define MQTT topics and payloads**
  - Create `docs/MQTT_TOPICS.md` with topic names and JSON schemas
  - `telemetry/events/` - event log stream
  - `telemetry/battery/cells/` - cell data
  - `telemetry/battery/summary/` - periodic stats

### Phase B: Transmitter Senders
- [ ] **Implement status senders** (low-rate, on-change)
  - `transmitter_send_system_status()` - 1–5s or on-change
  - `transmitter_send_battery_status()` - 1–5s or on-change
  - `transmitter_send_inverter_status()` - 1–5s or on-change
  - `transmitter_send_charger_status()` - 5–10s or on-change
  - All senders use datalayer fields
  - All senders calculate checksums

- [ ] **Implement event handling**
  - `transmitter_send_event_espnow()` - immediate ESP-NOW alert
  - `transmitter_publish_event_mqtt()` - durable log
  - Wire into existing fault/warning handlers

- [ ] **Implement settings snapshot**
  - `transmitter_send_settings_snapshot()` - on-request, one category at a time
  - Triggered by receiver request message
  - Include version number for conflict detection

- [ ] **Implement cell data sender**
  - `transmitter_send_cell_data_espnow()` - on-request only
  - `transmitter_publish_cell_data_mqtt()` - periodic or on-request
  - Document: "Single battery fits ESP-NOW; dual battery use MQTT"

### Phase C: Receiver Handlers
- [ ] **Create ESP-NOW handlers**
  - `handle_system_status()` - extract, cache, mark dirty
  - `handle_battery_status()` - extract, cache, mark dirty
  - `handle_inverter_status()` - extract, cache, mark dirty
  - `handle_charger_status()` - extract, cache, mark dirty
  - `handle_event()` - display alert + store
  - `handle_settings_snapshot()` - merge into local cache
  - All handlers validate checksums

- [ ] **Update receiver cache** (`TransmitterManager`)
  - Add structs for system_status, battery_status, inverter_status, charger_status
  - Add timestamp tracking (when was data last received?)
  - Add "data available" flags (use for "Waiting for data..." UI)

- [ ] **Create SSE updates**
  - Wire cache updates to SSE notifications
  - Push to monitor page on new data
  - Include timestamp for receiver timeout detection

### Phase D: Web UI Integration + MQTT Configuration
- [ ] **Enhance monitor page** (`espnowreciever_2/lib/webserver/pages/monitor_page.cpp`)
  - Display system status (ESP-NOW): uptime, state, faults
  - Display battery summary (ESP-NOW): SOC, V/I/P, temps
  - Display inverter summary (ESP-NOW): AC power, mode
  - Display charger summary (ESP-NOW): DC power, state
  - Show "Waiting for data..." placeholders if transmitter not connected (ESP-NOW timeout)
  - Show last-update timestamp for each section

- [ ] **Create MQTT Settings Page** (`lib/webserver/pages/settings_page.cpp`)
  - Add MQTT configuration section
  - Input fields: broker IP, port, username, password, topic prefix (default "BE")
  - Connection status indicator
  - Save/load from NVS storage
  - Test connection button
  - Enable/disable toggle

- [ ] **Add MQTT Battery Section** (`lib/webserver/pages/monitor_page.cpp`)
  - Display MQTT battery status (from BE/info topic)
  - Show: SOC, voltage, current, power, min/max temps
  - Show: BMS status, balancing status, event level
  - Show: Last MQTT update timestamp
  - Display "MQTT disconnected" warning if no data for 60s

- [ ] **Create Cell Monitor Page** (`lib/webserver/pages/cells_page.cpp`)
  - Grid layout for 96 cells (based on Battery Emulator cellmonitor.cpp)
  - Display cell voltages from MQTT (BE/spec_data)
  - Show: max voltage, min voltage, delta with color coding
  - Highlight low-voltage cells in red
  - Show balancing status per cell (cyan for active)
  - Display: "Cell data disabled - enable mqtt_transmit_all_cellvoltages on transmitter"
  - Bar graph visualization of cell voltages

- [ ] **Implement MQTT Client** (NEW - Create mqtt_client/ lib)
  - [ ] `lib/mqtt_client/mqtt_client.h` - MQTT client wrapper
  - [ ] `lib/mqtt_client/mqtt_client.cpp` - Implementation using esp_mqtt_client
  - [ ] `lib/mqtt_client/mqtt_handlers.h` - Message handlers
  - [ ] `lib/mqtt_client/mqtt_handlers.cpp` - Parse JSON and update cache
  - Subscribe to: `BE/info`, `BE/spec_data`, `BE/spec_data_2`, `BE/status`
  - Handle Home Assistant discovery (ignore or support as needed)

- [ ] **Extend TransmitterManager** (`lib/webserver/utils/transmitter_manager.cpp`)
  - Add MQTTBatteryData struct with cell arrays
  - Add `update_from_mqtt()` method
  - Add `get_mqtt_battery_data()` method
  - Timestamp tracking for each MQTT update
  - Connection health flag

- [ ] **Add SSE Push for MQTT** 
  - Push battery updates to monitor page when MQTT data arrives
  - Push cell data updates to cell monitor page
  - Include timestamp and source (ESP-NOW vs MQTT)

- [ ] **REMOVE ALL LEGACY CODE** (Critical)
  - [ ] Delete any dummy data generators (`dummy_data_generator.*`)
  - [ ] Delete any hardcoded test/mock MQTT implementations
  - [ ] Remove test topics and placeholder handlers
  - [ ] Remove any old MQTT client code from previous attempts
  - [ ] Remove any test data seeding in transmitter_manager
  - [ ] Verify all data comes from live sources (ESP-NOW or MQTT only)

### Phase E: Testing
- [ ] **Create dummy transmitter senders** (for early testing)
  - Simulate realistic system/battery/inverter/charger status
  - Send at 1–5s intervals
  - Inject occasional faults/events for testing

- [ ] **Verify packet sizes**
  - Compile and check sizeof() for all structs
  - Confirm <250 bytes ✓
  - Log actual sizes in debug output

- [ ] **Verify data rates**
  - Monitor ESP-NOW traffic (log packet count/sec)
  - Confirm summaries <1 Hz ✓
  - Confirm cell data <0.2 Hz when requested ✓

- [ ] **Smoke tests**
  - Transmitter sends dummy status → receiver displays
  - Receiver shows timestamps (no stale data)
  - MQTT event log working
  - MQTT cell data query working (if implemented)

### Phase F: Documentation
- [ ] **Create `PACKET_DEFINITIONS.md`**
  - All ESP-NOW packet structs with field descriptions
  - All MQTT topic/payload schemas
  - Size analysis and bandwidth estimates

- [ ] **Create `DATA_TRANSFER_DESIGN.md`**
  - Transport choice rationale (ESP-NOW vs MQTT)
  - Rate justifications
  - Cell data handling for single/dual batteries

---

## Summary: Bandwidth Estimates

### ESP-NOW (Transmitter → Receiver)
```
System status:    50 bytes × 0.5 Hz =   25 bytes/sec
Battery status:   60 bytes × 0.5 Hz =   30 bytes/sec
Inverter status:  40 bytes × 0.5 Hz =   20 bytes/sec
Charger status:   40 bytes × 0.2 Hz =    8 bytes/sec
Events:           30 bytes × 0.01 Hz =  0.3 bytes/sec
Settings:         80 bytes × 0 Hz =     0 bytes/sec (on-request only)
Cell data:        192 bytes × 0 Hz =    0 bytes/sec (on-request only)
                                  ─────────────────
                        Total:    ~83 bytes/sec

Equivalent to: ~664 bits/sec (negligible load on ESP-NOW)
Comfortable room for retries and overhead.
```

### MQTT (Transmitter → Broker)
```
Events:           100 bytes × 0.01 Hz = 1 byte/sec (occasional)
Cell data:        400 bytes × 0.01 Hz = 4 bytes/sec (slow periodic or on-request)
Summary:          200 bytes × 0.03 Hz = 6 bytes/sec (optional trending)
                                  ────────────────
                        Total:    ~11 bytes/sec

Equivalent to: ~88 bits/sec (trivial load on Ethernet)
MQTT broker easily handles this.
```

---

## Key Design Decisions

1. **No real-time display requirement** → Low-rate summaries (1–5s) are sufficient
2. **Receiver can be offline** → Transmitter operates independently
3. **Cell data too large for high-rate ESP-NOW** → On-request only; store in MQTT
4. **Dual batteries can't fit in single packet** → Force MQTT for multi-battery systems
5. **Events need both immediate AND persistent** → ESP-NOW for alert + MQTT for log
6. **Settings interactive feedback** → ESP-NOW only (immediate response)

---

**Status**: Ready for Phase 1 implementation  
**Next**: Build the packet structs and senders
