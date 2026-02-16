# MQTT Topics Reference (Battery Emulator v9.2.4)

## Overview
Battery Emulator publishes telemetry data via MQTT. The receiver can subscribe to these topics for persistent data storage and monitoring.

**Connection Method**:
- **Transmitter (ESP32-POE-ISO)**: Connected via **Ethernet** to MQTT broker (wired, reliable)
- **Receiver (LilyGo T-Display-S3)**: Connected via **WiFi** to MQTT broker (wireless, for monitoring)
- **ESP-NOW messages**: Use **WiFi** protocol between transmitter and receiver (peer-to-peer, low-latency, independent of MQTT broker)

**Base Topic Prefix**: `BE` (configurable in Battery Emulator settings)  
**MQTT Quality of Service (QoS)**: 0 (At most once - fire and forget)  
**MQTT Retained Messages**: No (except availability topic)  

---

## Topic Structure

### 1. Primary Info Topic (Battery Status + System Status)
**Topic**: `BE/info`  
**Rate**: 5 seconds (when transmitter is running)  
**Retained**: No  

**JSON Fields**:
```json
{
  // System Status
  "bms_status": "OK|WARNING|FAULT",
  "pause_status": "not paused|paused",
  "event_level": "info|warning|error|critical",
  "emulator_status": "run|pause|stop",
  
  // Battery 1 (Single or first of dual system)
  "SOC": 80.25,                    // State of Charge (percent)
  "SOC_real": 80.1,                // Real SOC (percent)
  "state_of_health": 95.5,         // State of Health (percent)
  "temperature_min": 23.2,         // Min cell temperature (°C)
  "temperature_max": 24.8,         // Max cell temperature (°C)
  "cpu_temp": 42.5,                // CPU temperature (°C)
  "stat_batt_power": 5000,         // Battery power (W)
  "battery_current": 50.2,         // Battery current (A)
  "cell_max_voltage": 4.2,         // Max cell voltage (V)
  "cell_min_voltage": 4.18,        // Min cell voltage (V)
  "cell_voltage_delta": 20,        // Max - Min (mV)
  "battery_voltage": 403.2,        // Pack voltage (V)
  "total_capacity": 100000,        // Total capacity (Wh)
  "remaining_capacity_real": 80250,// Real remaining (Wh)
  "remaining_capacity": 80250,     // Reported remaining (Wh)
  "max_discharge_power": 50000,    // Max discharge (W)
  "max_charge_power": 25000,       // Max charge (W)
  "charged_energy": 45000,         // Total charged (Wh)
  "discharged_energy": 15000,      // Total discharged (Wh)
  "balancing_active_cells": 3,     // Cells being balanced
  "balancing_status": "Active",    // Balancing state
  
  // Battery 2 (only if dual-battery system enabled)
  "SOC_2": 81.0,                   // Battery 2 SOC
  "SOC_real_2": 80.9,
  "state_of_health_2": 95.2,
  "temperature_min_2": 23.1,
  "temperature_max_2": 24.9,
  "battery_current_2": 51.0,
  "cell_max_voltage_2": 4.21,
  "cell_min_voltage_2": 4.17,
  "cell_voltage_delta_2": 40,
  "battery_voltage_2": 404.1,
  "total_capacity_2": 100000,
  "remaining_capacity_real_2": 81000,
  "remaining_capacity_2": 81000,
  "max_discharge_power_2": 51000,
  "max_charge_power_2": 25500,
  "charged_energy_2": 45500,
  "discharged_energy_2": 14500,
  "balancing_active_cells_2": 2,
  "balancing_status_2": "Ready"
}
```

**Use Case**: Real-time battery monitoring, dashboards, triggering alerts  
**Receiver Action**: Parse JSON, update cache, push SSE to web UI

---

### 2. Cell Voltage Details Topic (Battery 1)
**Topic**: `BE/spec_data`  
**Rate**: On-demand or slow periodic (30-60s) if enabled in transmitter  
**Retained**: No  
**Condition**: Must enable `mqtt_transmit_all_cellvoltages = true` in Battery Emulator config  

**JSON Fields**:
```json
{
  "cell_voltages": [
    4.123, 4.121, 4.119, 4.120, 4.118, 4.125, ... 4.122
  ]
}
```

**Notes**:
- Array contains voltage for EACH cell in volts (3 decimal places)
- Count matches battery configuration (typically 96 cells)
- Size: ~800 bytes when formatted as JSON
- Low refresh rate due to payload size (not real-time)

**Use Case**: Cell analysis, balancing verification, long-term trending  
**Receiver Action**: Cache cell array, render cell monitor grid page

---

### 3. Cell Voltage Details Topic (Battery 2)
**Topic**: `BE/spec_data_2`  
**Rate**: Same as Battery 1  
**Retained**: No  
**Condition**: Only present if dual-battery system enabled  

**JSON Fields**: Same as `BE/spec_data` (separate array for battery 2)

---

### 4. Availability / LWT (Last Will & Testament)
**Topic**: `BE/status`  
**Payload**: `"online"` (when transmitter running) or `"offline"` (on disconnect)  
**Retained**: Yes  
**Rate**: On connect/disconnect only  

**Use Case**: Detect transmitter connection state  
**Receiver Action**: Monitor for "online" → show "MQTT Connected", "offline" → show "MQTT Disconnected"

---

### 5. Home Assistant Auto-Discovery (Optional)
**Topics**: 
- `homeassistant/sensor/BE/{object_id}/config`
- `homeassistant/button/BE/{subtype}/config`

**Purpose**: Enable Home Assistant to auto-discover Battery Emulator sensors  
**Retained**: Yes  
**Receiver**: Typically ignore (for Home Assistant integration only, not needed for dedicated receiver)

---

## Receiver MQTT Implementation Strategy

### Configuration Flow
1. User enters MQTT broker IP/port in receiver web UI settings
2. Receiver saves to NVS: `broker_ip`, `broker_port`, `username` (if needed), `password` (if needed)
3. Receiver connects to MQTT broker on startup or settings change
4. Receiver subscribes to: `BE/info`, `BE/spec_data`, `BE/spec_data_2`, `BE/status`

### Data Update Flow
```
[Transmitter (Ethernet)] 
   ↓ (publishes BE/info, BE/spec_data via MQTT Broker every 5s)
[MQTT Broker]
   ↓ (MQTT message via WiFi)
[Receiver MQTT Client (WiFi)]
   ↓ (mqtt_client.cpp handles message)
[MQTT Handlers]
   ↓ (parse JSON, validate, update cache)
[TransmitterManager Cache]
   ↓ (SSE push to web UI)
[Web Browser - Monitor Page]
   ↓ (real-time display)

Note: ESP-NOW messages use separate WiFi peer-to-peer channel (independent of MQTT broker)
```

### Cache Structure in Receiver
```cpp
struct TransmitterData {
  // ESP-NOW data (low-latency, immediate)
  SystemStatus esp_now_system;
  BatteryStatus esp_now_battery;
  InverterStatus esp_now_inverter;
  ChargerStatus esp_now_charger;
  
  // MQTT data (persistent, rich)
  BatteryStatus mqtt_battery;           // From BE/info
  std::vector<float> mqtt_cell_voltages;  // From BE/spec_data
  std::vector<float> mqtt_cell_voltages_2; // From BE/spec_data_2
  bool mqtt_connected;                  // From BE/status
  
  // Timestamps
  uint32_t esp_now_last_update_ms;
  uint32_t mqtt_last_update_ms;
};
```

### Display Priority
1. **Monitor Page**: Show ESP-NOW data (real-time, low-latency)
2. **MQTT Section**: Show MQTT data (richer info, persistent)
3. **Cell Monitor Page**: Show MQTT cell voltages (when available)
4. **Settings Page**: Display MQTT connection status

---

## Configuration Files Reference

### Battery Emulator MQTT Config
**File**: `Software/src/devboard/mqtt/mqtt.cpp`

**Key Variables**:
```cpp
bool mqtt_enabled = false;              // Enable MQTT publishing
bool ha_autodiscovery_enabled = false;  // Enable HA discovery
bool mqtt_transmit_all_cellvoltages = false;  // Publish cell data (large payload!)
const char* mqtt_topic_name = "BE";     // Topic prefix
const char* mqtt_server_default = "";   // MQTT broker (set via web UI)
const int mqtt_port_default = 0;        // MQTT port (set via web UI)
```

**User can configure via Battery Emulator web UI**:
- MQTT Broker IP/Port
- Username/Password
- Topic prefix (default: "BE")
- Enable/disable specific data streams

---

## Receiver MQTT Configuration Storage

### NVS Keys (Non-Volatile Storage)
```
Key Name          | Type     | Default        | Max Length
------------------|----------|----------------|----------
mqtt_enabled      | u8 (bool)| 0 (disabled)   | 1 byte
mqtt_broker       | string   | ""             | 64 bytes (IP:port)
mqtt_user         | string   | ""             | 32 bytes
mqtt_pass         | string   | ""             | 64 bytes
mqtt_topic        | string   | "BE"           | 32 bytes
mqtt_port         | u16      | 1883           | 2 bytes
```

### Web UI Settings Form
```html
<form id="mqtt-config">
  <input type="checkbox" id="mqtt_en" name="mqtt_enabled" />
  <input type="text" id="mqtt_broker" placeholder="192.168.1.100:1883" />
  <input type="text" id="mqtt_user" placeholder="username" />
  <input type="password" id="mqtt_pass" placeholder="password" />
  <input type="text" id="mqtt_topic" value="BE" />
  <button type="submit">Save Settings</button>
</form>
```

---

## Troubleshooting

### Receiver not receiving MQTT data?
1. Check `BE/status` topic → should show "online" if transmitter connected
2. Verify MQTT broker is running and accessible
3. Verify firewall allows port 1883
4. Check receiver MQTT config matches broker address
5. Verify Battery Emulator has MQTT enabled and configured
6. Check username/password if broker requires authentication

### Cell data not appearing?
1. Verify `mqtt_transmit_all_cellvoltages = true` on transmitter
2. Check `BE/spec_data` topic is being published (check broker logs)
3. Cell data is low-rate (30-60s interval) - be patient
4. Large payload - may cause MQTT latency spikes

### MQTT connection drops?
1. Check WiFi stability on transmitter
2. Verify MQTT broker keepalive timeout (typical: 60s)
3. Check network congestion (receiver may be on poor WiFi)
4. Try higher timeout value in receiver config

---

## Data Retention & Persistence

**Battery Emulator (Transmitter)**:
- Publishes BE/info every 5s (no retention)
- Publishes BE/spec_data on-demand (no retention)
- Only BE/status is retained (availability)

**Receiver**:
- Caches latest MQTT data in RAM
- Can optionally log to SPIFFS or SD card (not automated)
- Use MQTT broker "message retention" to persist data server-side
- Enable broker persistence in mosquitto.conf:
  ```
  persistence true
  persistence_location /var/lib/mosquitto/
  ```

---

## Battery Emulator MQTT Code Reference

**Main MQTT file**: `Software/src/devboard/mqtt/mqtt.cpp`

**Key Functions**:
- `publish_values()` - Publishes all data at interval
- `publish_common_info()` - Publishes BE/info
- `publish_cell_voltages()` - Publishes BE/spec_data
- `mqtt_publish(topic, msg, retained)` - Low-level publish

**Configuration UI**: `Software/src/devboard/webserver/settings_html.cpp`
- MQTT broker settings page

