# Receiver Alignment Report: Bringing `espnowreceiver_2` and `espnowreceiver_LCD` into Parity

**Date:** 2026-05-19  
**Scope:** Aligning MQTT data ingestion behavior between `espnowreceiver_2` and `espnowreceiver_LCD` projects  
**Related Audit:** [ESP-NOW → MQTT Data Migration Audit](ESPNOW_TO_MQTT_DATA_AUDIT_2026_05_19.md)

---

## Executive Summary

Investigation revealed that **`espnowreceiver_2` and `espnowreceiver_LCD` have divergent MQTT message handler implementations**, particularly for battery telemetry and runtime state ingestion. While both projects share the same MQTT topic schema and BatteryData storage model, their `mqtt_client.cpp` implementations differ significantly:

- **`espnowreceiver_LCD`**: Attempts broader telemetry extraction but incomplete (temperature only + some fields parsed but not stored)
- **`espnowreceiver_2`**: More minimal (temperature-only extraction; no BatteryData snapshot updates from MQTT)

This divergence creates two problems:
1. **Inconsistent telemetry surface** when MQTT is the sole data source (ESP-NOW offline)
2. **Missed data ingestion** — fields present in MQTT payloads are silently discarded in receiver_2

This report prescribes specific code changes required to achieve full parity.

---

## Addendum (2026-05-19): Transmitter MQTT output vs receiver intake

This table compares what the transmitter currently sends/subscribes via MQTT against what each receiver currently consumes.

| Topic / flow | Transmitter side | `espnowreceiver_2` | `espnowreceiver_LCD` | Difference highlighted |
|---|---|---|---|---|
| `batt-emu/mqtt-v1/tx/state/heartbeat` | Published (`publish_heartbeat`) | No subscription in `subscribeToTopics()` | No subscription in `subscribeToTopics()` | **TX publishes, both RX variants ignore.** |
| `batt-emu/mqtt-v1/tx/state/battery_live` | Periodic publish path exists but is gated by `MQTT_FEATURE_LIVE_BATTERY_TELEMETRY` (currently `0`) | Subscribed | Subscribed | **Both receivers expect stream; TX periodic stream currently gated off.** |
| `batt-emu/mqtt-v1/tx/state/runtime/led` + `.../runtime/system` | Published on connect bundle; periodic path also under live-telemetry gate | Subscribed | Subscribed | Aligned topic coverage, but periodic cadence still controlled by TX feature gate. |
| `batt-emu/mqtt-v1/tx/state/runtime/charger` + `.../runtime/inverter` | Published on connect bundle; periodic path also under live-telemetry gate | Subscribed | Handler exists but no base subscription in `subscribeToTopics()` | **LCD mismatch: handler exists, topic not subscribed.** |
| `batt-emu/mqtt-v1/tx/state/cell_data/chunk` | Published when `MQTT_FEATURE_CELL_DATA_TELEMETRY=1` (currently enabled) | Subscribed only when not paused | Subscribed only when not paused | Aligned demand-gated behavior. |
| `batt-emu/mqtt-v1/tx/state/event_logs/chunk` | Published only when event-log streaming enabled and subscriber count > 0 | Callback handler exists; base subscription list omits this topic | Dynamically subscribes/unsubscribes with viewer count | **receiver_2 coverage gap vs LCD dynamic subscription model.** |
| `batt-emu/mqtt-v1/tx/ack/*` | TX emits concrete ACK topics (`control`, `battery`, `network`, `mqtt`, `event_logs_clear`) | Subscribes wildcard `tx/ack/#` | Subscribes specific ACK topics | Functionally aligned, different subscription strategy. |
| `batt-emu/mqtt-v1/rx/cmd/*` command intake | TX subscribes to selected update/control/refresh/stream topics | Sends supported commands through API gates | Sends supported commands through API gates | Mostly aligned for active command set. |
| Legacy broker status topic (`config::topics.status`) | TX publishes retained `online/offline` | Not consumed | Not consumed | Non-blocking drift (unused by receivers). |

Reference files checked:
- `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_task.cpp`
- `esp32common/include/esp32common/mqtt/mqtt_feature_flags.h`
- `espnowreceiver_2/src/mqtt/mqtt_client.cpp`
- `espnowreceiver_LCD/src/mqtt/mqtt_client.cpp`
- `espnowreceiver_LCD/include/mqtt/mqtt_topics_receiver.h`

---

## Part 1: Current State Analysis

### 1.1 MQTT Client Handler Comparison

#### `espnowreceiver_LCD` → `src/mqtt/mqtt_client.cpp`

**`handleBatteryLive()` (lines 628–655):**
```cpp
// Current implementation
const uint32_t seq = doc["seq"] | static_cast<uint32_t>(millis());
const uint32_t uptime_ms = doc["uptime_ms"] | static_cast<uint32_t>(millis());

// Temperature extraction
const int16_t temperature_centi_c = doc["temperature_centi_c"].isNull() ? 
  static_cast<int16_t>(doc["temperature_c"] * 100) : 
  doc["temperature_centi_c"];

// STORES: Temperature only
TransmitterManager::storeTemperatureReport(true, seq, temperature_centi_c, uptime_ms);

// SILENTLY DISCARDS: 
// - soc_percent (doc["soc"])
// - power_w (doc["power"] or doc["power_w"])
// - voltage_mv (doc["voltage_mv"])
// - current_ma (doc["current_ma"])
// - bms_status (doc["bms_status"])
// - max_charge_power_w, max_discharge_power_w
```

**Status:** Battery telemetry fields (SOC, power, voltage, current) are **parsed from MQTT payload but NOT stored into BatteryData snapshot** or legacy globals.

#### `espnowreceiver_2` → `src/mqtt/mqtt_client.cpp`

**`handleBatteryLive()` (lines 628–655):**
```cpp
// Implementation is even more minimal
// Only stores temperature if present
// Does NOT parse: soc, power, voltage, current, bms_status, limits
```

**Status:** Even more restricted than LCD variant — pure temperature-only path, no attempt to extract other battery fields.

---

### 1.2 BatteryData Storage Layer

Both receivers share the same `BatteryData` snapshot model (`espnowreceiver_2/src/espnow/battery_data_store.h`):

**Available update methods (not called from MQTT handlers):**
```cpp
class BatteryData {
public:
  // Method exists but NOT called from MQTT battery_live handler
  static void update_battery_status(
    bool received, 
    int16_t soc_percent, 
    int32_t voltage_mV, 
    int32_t current_mA, 
    int16_t temperature_dC, 
    int32_t power_mW, 
    uint16_t max_charge_power_W, 
    uint16_t max_discharge_power_W, 
    uint8_t bms_status
  );
  
  static void update_charger_status(...);  // No MQTT handler calls this
  static void update_inverter_status(...); // No MQTT handler calls this
  static void update_system_status(...);   // No MQTT handler calls this (ESP-NOW only)
};
```

**Current MQTT → BatteryData flow:** 
- ✅ ESP-NOW handlers call `update_battery_status()` 
- ❌ MQTT handlers do NOT call `update_battery_status()`
- ❌ MQTT handlers do NOT call `update_charger_status()`, `update_inverter_status()`, `update_system_status()`

**Result:** When operating MQTT-only (no ESP-NOW), BatteryData snapshot remains stale for all telemetry fields.

---

### 1.3 API Consumption Impact

Both receivers expose telemetry via REST APIs that read from BatteryData snapshot:

**`espnowreceiver_LCD/lib/webserver_lcd/api/api_monitor_handlers.cpp`:**
```cpp
// Example: Monitor API endpoint reads snapshot
BatteryData::TelemetrySnapshot snapshot = BatteryData::get_snapshot();
// Returns: soc_percent, power_W, voltage_V
// These are STALE under MQTT-only (not updated by MQTT handlers)
```

**`espnowreceiver_2/lib/webserver/utils/telemetry_snapshot_utils.h`:**
```cpp
static void fill_snapshot_telemetry(...) {
  // Same issue: snapshot only populated by ESP-NOW handlers
  // MQTT battery_live handler does not populate snapshot
}
```

---

## Part 2: Alignment Gaps (Detailed)

### Gap 1: MQTT Battery Telemetry Not Stored to BatteryData

**Impact:** When transmitter publishes to `batt-emu/mqtt-v1/tx/state/battery_live` with full fields (SOC, voltage, current, power, BMS status), receiver ingests only temperature and discards the rest.

| Field | Transmitter MQTT publish | Receiver LCD MQTT handler | Receiver_2 MQTT handler | BatteryData snapshot | Status |
|---|---|---|---|---|---|
| `soc` | ✅ Present | ❌ Parsed, not stored | ❌ Not parsed | ❌ Stale | **GAP** |
| `power` | ✅ Present | ❌ Parsed, not stored | ❌ Not parsed | ❌ Stale | **GAP** |
| `voltage_mv` | ✅ Present | ❌ Parsed, not stored | ❌ Not parsed | ❌ Stale | **GAP** |
| `current_ma` | ✅ Present | ❌ Parsed, not stored | ❌ Not parsed | ❌ Stale | **GAP** |
| `bms_status` | ✅ Present | ❌ Parsed, not stored | ❌ Not parsed | ❌ Stale | **GAP** |
| `temperature_c` / `temperature_centi_c` | ✅ Present | ✅ Stored | ✅ Stored | ✅ Current | OK |
| `max_charge_power_w` / `max_discharge_power_w` | ✅ Present (optional) | ❌ Not parsed | ❌ Not parsed | ❌ Stale | **POTENTIAL GAP** |

**Fix required:** Both receivers must call `BatteryData::update_battery_status()` from `handleBatteryLive()` with extracted fields.

---

### Gap 2: LED Runtime State Not Updated from MQTT

**Impact:** WebUI queries LED status via `api_get_led_runtime_status_handler()`, which returns `ESPNow::current_led_color` and `ESPNow::current_led_effect` globals. These globals are ONLY updated by ESP-NOW handler, never by MQTT handlers. When operating MQTT-only, LED status is stale/incorrect.

**Current code path (ESP-NOW only):**
```
Transmitter publish msg_flash_led via ESP-NOW
    ↓
espnow_message_handlers.cpp: handle_flash_led_message()
    ↓
ESPNow::current_led_color = color; // ✅ Updates runtime global
ESPNow::current_led_effect = effect;
    ↓
WebUI queries api_get_led_runtime_status_handler()
    ↓
Returns current_color_name, current_effect_name ✅ (current)
```

**Missing code path (MQTT only):**
```
Transmitter publish tx/state/runtime/led via MQTT [NOT YET IMPLEMENTED]
    ↓
mqtt_client.cpp: handleLedRuntime() [DOES NOT EXIST]
    ❌ Missing handler
    ↓
ESPNow::current_led_color = color; // Never happens
ESPNow::current_led_effect = effect;
    ↓
WebUI queries api_get_led_runtime_status_handler()
    ↓
Returns stale/incorrect LED state ❌
```

**Fix required:** 
1. Transmitter must publish runtime LED color/effect to MQTT topic `batt-emu/mqtt-v1/tx/state/runtime/led`
2. Both receivers must implement `handleLedRuntime()` MQTT handler
3. Handler must update `ESPNow::current_led_color` and `ESPNow::current_led_effect` atomics

---

### Gap 3: System Status (Contactor/Relay State) Not Updated from MQTT

**Impact:** `BatteryData::system_status` section has fields (`contactor_state`, `error_flags`, `warning_flags`, `uptime_seconds`) that are ONLY updated by ESP-NOW handler `handle_system_status()`. MQTT handlers do NOT update this section.

**Current code path (ESP-NOW only):**
```
Transmitter publish msg_system_status via ESP-NOW
    ↓
espnow_message_handlers.cpp: handle_system_status()
    ↓
BatteryData::update_system_status(contactor_state, error_flags, warning_flags, uptime)
    ↓
BatteryData snapshot: system_status section populated ✅
    ↓
WebUI monitor API can display relay state ✅ (when ESP-NOW active)
```

**Missing code path (MQTT only):**
```
Transmitter publish tx/state/runtime/system via MQTT [NOT YET IMPLEMENTED]
    ↓
mqtt_client.cpp: handleSystemRuntime() [DOES NOT EXIST]
    ❌ Missing handler
    ↓
BatteryData::update_system_status(...) never called
    ↓
BatteryData snapshot: system_status fields remain at defaults (0x00) ❌
    ↓
WebUI monitor API displays incorrect relay state ❌
```

**Fix required:**
1. Transmitter must publish system status fields to MQTT topic `batt-emu/mqtt-v1/tx/state/runtime/system`
2. Both receivers must implement `handleSystemRuntime()` MQTT handler
3. Handler must call `BatteryData::update_system_status()` with parsed fields

---

### Gap 4: Charger & Inverter Runtime Status Not Updated from MQTT

**Current:** `msg_charger_status` and `msg_inverter_status` are sent via ESP-NOW and parsed by receivers. MQTT has no equivalent topics for these runtime states.

**Impact:** When operating MQTT-only, charger/inverter power/status/mode is unknown; only static spec information is available.

**BatteryData methods available but not called from MQTT:**
```cpp
BatteryData::update_charger_status(
  bool received, 
  uint16_t hv_voltage_mV, uint16_t hv_current_mA,
  uint16_t lv_voltage_mV, uint16_t lv_current_mA,
  uint16_t ac_voltage_mV, uint16_t ac_current_mA,
  int32_t power_mW, 
  uint8_t charger_status
);

BatteryData::update_inverter_status(
  bool received, 
  uint16_t ac_voltage_mV, uint16_t frequency_dHz, uint16_t ac_current_mA,
  int32_t power_mW, 
  uint8_t inverter_status
);
```

**Fix required:** 
1. Transmitter must publish charger/inverter runtime to MQTT topics `tx/state/runtime/charger` and `tx/state/runtime/inverter`
2. Both receivers must implement corresponding MQTT handlers
3. Handlers must call `BatteryData::update_charger_status()` and `BatteryData::update_inverter_status()`

---

## Part 3: Implementation Roadmap

### Phase 1: Harmonize MQTT Battery Telemetry Ingestion (P0 — Highest Priority)

**Objective:** Both receivers extract and store full battery telemetry from MQTT `battery_live` payload.

#### 1.1 Update `espnowreceiver_LCD/src/mqtt/mqtt_client.cpp`

**Location:** `handleBatteryLive()` function (lines 628–655)

**Current code:**
```cpp
void MqttClient::handleBatteryLive(const char* payload, unsigned int length) {
  DynamicJsonDocument doc(2048);
  // ... parse JSON ...
  
  const int16_t temperature_centi_c = doc["temperature_centi_c"].isNull() ? 
    static_cast<int16_t>(doc["temperature_c"] * 100) : 
    doc["temperature_centi_c"];
  
  TransmitterManager::storeTemperatureReport(true, seq, temperature_centi_c, uptime_ms);
  // MISSING: calls to BatteryData::update_battery_status()
}
```

**Required changes:**
1. Extract SOC, power, voltage, current, BMS status from JSON
2. Call `BatteryData::update_battery_status()` with all fields
3. Keep temperature storage via `TransmitterManager::storeTemperatureReport()`

**Pseudocode:**
```cpp
void MqttClient::handleBatteryLive(const char* payload, unsigned int length) {
  // ... existing JSON parse ...
  
  // Extract fields
  int16_t soc_percent = doc["soc"] | -1;
  int32_t voltage_mV = doc["voltage_mv"] | 0;
  int32_t current_mA = doc["current_ma"] | 0;
  int16_t temperature_dC = (temperature_centi_c + 5) / 10;  // Convert centi→deci
  int32_t power_mW = doc["power"] | 0;
  uint16_t max_charge_power_W = doc["max_charge_power_w"] | 0;
  uint16_t max_discharge_power_W = doc["max_discharge_power_w"] | 0;
  uint8_t bms_status = doc["bms_status"] | 0;
  
  // Store to BatteryData snapshot
  BatteryData::update_battery_status(
    true,                           // received = true (MQTT is authoritative)
    soc_percent,
    voltage_mV,
    current_mA,
    temperature_dC,
    power_mW,
    max_charge_power_W,
    max_discharge_power_W,
    bms_status
  );
  
  // Keep temperature report storage
  TransmitterManager::storeTemperatureReport(true, seq, temperature_centi_c, uptime_ms);
}
```

#### 1.2 Update `espnowreceiver_2/src/mqtt/mqtt_client.cpp`

**Location:** `handleBatteryLive()` function (lines 628–655)

**Current state:** Even more minimal than LCD version (temperature-only, no attempt to parse other fields)

**Required changes:** Implement same extraction logic as LCD version above (full battery telemetry → BatteryData)

**Additional consideration:** Verify that both implementations define `temperature_dC` correctly:
- MQTT payload may carry `temperature_centi_c` (hundredths of °C)
- BatteryData `update_battery_status()` expects `temperature_dC` (tenths of °C)
- Conversion: `temperature_dC = (temperature_centi_c + 5) / 10` (with rounding)

---

### Phase 2: Add Missing LED Runtime MQTT Topic & Handlers (P0)

**Objective:** Transmitter publishes runtime LED state to MQTT; both receivers ingest and update LED globals.

#### 2.1 Transmitter-side: Add LED Runtime Topic Publication

**Location:** `ESPnowtransmitter2/espnowtransmitter2/src/` (TBD based on transmitter architecture)

**New MQTT topic:** `batt-emu/mqtt-v1/tx/state/runtime/led`

**Payload format (example):**
```json
{
  "color": 1,
  "effect": 2,
  "status": "active",
  "ts_ms": 1234567890,
  "seq": 42
}
```

**Implementation location:** Integrate with existing `publish_data()` or new `publish_runtime_state()` function. Publish whenever LED color/effect changes or on periodic heartbeat.

#### 2.2 Receiver-side: Add LED Runtime MQTT Handler

**Location:** `espnowreceiver_LCD/src/mqtt/mqtt_client.cpp` and `espnowreceiver_2/src/mqtt/mqtt_client.cpp`

**New handler function:**
```cpp
void MqttClient::handleLedRuntime(const char* payload, unsigned int length) {
  DynamicJsonDocument doc(512);
  deserializeJson(doc, payload);
  
  uint8_t color = doc["color"] | 0;
  uint8_t effect = doc["effect"] | 0;
  
  // Validate ranges
  if (color > 3 || effect > 2) return;  // Invalid state
  
  // Update runtime globals
  ESPNow::current_led_color = color;
  ESPNow::current_led_effect = effect;
}
```

**Hook-up location:** Both receivers' topic dispatch (hash-based in receiver_2, if/else in LCD):
```cpp
// In handleMessage() or subscribeToTopics()
// Add new case for runtime/led topic
if (topic_hash == HASH("tx/state/runtime/led")) {
  handleLedRuntime(payload, length);
}
```

**Verification:** After this change, `api_get_led_runtime_status_handler()` will return MQTT-driven LED state when MQTT is active (no ESP-NOW dependency).

---

### Phase 3: Add Missing System Status MQTT Topic & Handlers (P0)

**Objective:** Transmitter publishes runtime system/relay state to MQTT; both receivers ingest and update BatteryData.

#### 3.1 Transmitter-side: Add System Status Topic Publication

**Location:** `ESPnowtransmitter2/espnowtransmitter2/src/` (TBD)

**New MQTT topic:** `batt-emu/mqtt-v1/tx/state/runtime/system`

**Payload format (example):**
```json
{
  "contactor_state": 5,
  "error_flags": 0,
  "warning_flags": 2,
  "uptime_seconds": 12345,
  "ts_ms": 1234567890,
  "seq": 42
}
```

**Implementation:** Extract current relay/contactor state from transmitter's battery emulator classes (BMW-IX, BMW-PHEV, etc.) and publish to MQTT.

#### 3.2 Receiver-side: Add System Status MQTT Handler

**Location:** `espnowreceiver_LCD/src/mqtt/mqtt_client.cpp` and `espnowreceiver_2/src/mqtt/mqtt_client.cpp`

**New handler function:**
```cpp
void MqttClient::handleSystemRuntime(const char* payload, unsigned int length) {
  DynamicJsonDocument doc(512);
  deserializeJson(doc, payload);
  
  uint8_t contactor_state = doc["contactor_state"] | 0;
  uint8_t error_flags = doc["error_flags"] | 0;
  uint8_t warning_flags = doc["warning_flags"] | 0;
  uint32_t uptime_seconds = doc["uptime_seconds"] | 0;
  
  // Store to BatteryData snapshot
  BatteryData::update_system_status(
    true,                 // received = true
    contactor_state,
    error_flags,
    warning_flags,
    uptime_seconds
  );
}
```

**Hook-up location:** Both receivers' topic dispatch:
```cpp
if (topic_hash == HASH("tx/state/runtime/system")) {
  handleSystemRuntime(payload, length);
}
```

**Verification:** After this change, `BatteryData::get_system_status()` will return current relay/contactor state from MQTT when operating MQTT-only.

---

### Phase 4: Optional — Add Charger & Inverter Runtime Topics (P1)

**Objective:** Transmitter publishes charger/inverter runtime state; receivers ingest and update BatteryData.

#### 4.1 Charger Runtime Topic

**Topic:** `batt-emu/mqtt-v1/tx/state/runtime/charger`

**Payload format (example):**
```json
{
  "hv_voltage_mv": 48000,
  "hv_current_ma": 100,
  "lv_voltage_mv": 13600,
  "lv_current_ma": 5000,
  "ac_voltage_mv": 230000,
  "ac_current_ma": 16000,
  "power_mw": 3680000,
  "charger_status": 3,
  "ts_ms": 1234567890,
  "seq": 42
}
```

**Receiver handler:**
```cpp
void MqttClient::handleChargerRuntime(const char* payload, unsigned int length) {
  // Parse JSON
  BatteryData::update_charger_status(
    true,
    doc["hv_voltage_mv"],
    doc["hv_current_ma"],
    doc["lv_voltage_mv"],
    doc["lv_current_ma"],
    doc["ac_voltage_mv"],
    doc["ac_current_ma"],
    doc["power_mw"],
    doc["charger_status"]
  );
}
```

#### 4.2 Inverter Runtime Topic

**Topic:** `batt-emu/mqtt-v1/tx/state/runtime/inverter`

**Payload format (example):**
```json
{
  "ac_voltage_mv": 230000,
  "frequency_dhz": 500,
  "ac_current_ma": 16000,
  "power_mw": 3680000,
  "inverter_status": 2,
  "ts_ms": 1234567890,
  "seq": 42
}
```

**Receiver handler:**
```cpp
void MqttClient::handleInverterRuntime(const char* payload, unsigned int length) {
  // Parse JSON
  BatteryData::update_inverter_status(
    true,
    doc["ac_voltage_mv"],
    doc["frequency_dhz"],
    doc["ac_current_ma"],
    doc["power_mw"],
    doc["inverter_status"]
  );
}
```

---

## Part 4: Verification & Testing

### 4.1 Unit-level verification

After each phase implementation, verify:

**Phase 1 (Battery telemetry):**
- [ ] Publish synthetic MQTT `battery_live` payload with all fields (SOC, voltage, current, power, BMS status, temperature)
- [ ] Verify BatteryData snapshot receives all fields (via breakpoint or log statement)
- [ ] Verify telemetry API endpoint (`/api/monitor`) returns current SOC/power/voltage (not stale)

**Phase 2 (LED runtime):**
- [ ] Publish synthetic MQTT `tx/state/runtime/led` payload with color/effect variations
- [ ] Verify `ESPNow::current_led_color` and `current_led_effect` atomics update immediately
- [ ] Verify `/api/get_led_runtime_status_handler()` returns MQTT-driven values

**Phase 3 (System status):**
- [ ] Publish synthetic MQTT `tx/state/runtime/system` payload
- [ ] Verify BatteryData snapshot `system_status` section contains contactor_state/error_flags/warning_flags
- [ ] Verify WebUI relay/contactor visualization reflects current state

### 4.2 Integration-level verification

**Test scenario: MQTT-only operation (no ESP-NOW)**
1. Disable/block ESP-NOW communication
2. Publish full MQTT telemetry stream (battery_live, runtime/led, runtime/system)
3. Verify all receiver APIs return current data (not stale ESP-NOW values)
4. Verify WebUI displays complete system state (LED, relay, battery telemetry)

**Test scenario: Dual-mode operation (ESP-NOW + MQTT)**
1. Enable both protocols
2. Transmit via both ESP-NOW and MQTT
3. Verify receiver snapshot reflects latest update (regardless of protocol source)
4. Verify no data conflicts or race conditions

---

## Part 5: Acceptance Criteria

### Criteria 1: Battery Telemetry Alignment
- [ ] Both receivers parse SOC, power, voltage, current, BMS status from MQTT `battery_live`
- [ ] Both receivers call `BatteryData::update_battery_status()` with all extracted fields
- [ ] BatteryData snapshot reflects current telemetry within 1 second of MQTT publish
- [ ] Monitor API endpoints return current telemetry under MQTT-only operation

### Criteria 2: LED Runtime State Alignment
- [ ] Transmitter publishes LED color/effect to MQTT topic `tx/state/runtime/led`
- [ ] Both receivers implement `handleLedRuntime()` handler
- [ ] LED runtime globals update from MQTT within 1 second
- [ ] WebUI and API return MQTT-driven LED state (not stale ESP-NOW)
- [ ] LED runtime state correctly reflects current simulation under MQTT-only operation

### Criteria 3: System Status Alignment
- [ ] Transmitter publishes contactor_state, error_flags, warning_flags to MQTT topic `tx/state/runtime/system`
- [ ] Both receivers implement `handleSystemRuntime()` handler
- [ ] BatteryData snapshot `system_status` section updates within 1 second of MQTT publish
- [ ] Contactor/relay state visualizations in WebUI reflect current MQTT state under MQTT-only operation

### Criteria 4: Code Parity
- [ ] `espnowreceiver_LCD` and `espnowreceiver_2` MQTT handlers for battery telemetry are functionally identical
- [ ] Both receivers implement identical LED and system status handlers
- [ ] Code review confirms no divergent business logic between receiver implementations

### Criteria 5: Backwards Compatibility
- [ ] Changes do not break existing ESP-NOW operation
- [ ] Dual-mode (ESP-NOW + MQTT) operation remains functional
- [ ] Legacy API contracts unchanged

---

## Appendix A: File Locations Reference

### espnowreceiver_LCD
- MQTT client: `lib/webserver_lcd/mqtt/mqtt_client.cpp` (handlers at lines 628+)
- LED API: `lib/webserver_lcd/api/api_led_handlers.cpp` (line 37 `api_get_led_runtime_status_handler()`)
- Monitor API: `lib/webserver_lcd/api/api_monitor_handlers.cpp` (snapshot consumption)
- LED runtime globals: `src/runtime/common_lcd.cpp` or `src/runtime/common_lcd.h`

### espnowreceiver_2
- MQTT client: `src/mqtt/mqtt_client.cpp` (handlers at lines 628+)
- BatteryData storage: `src/espnow/battery_data_store.h` (class definition) & `src/espnow/battery_data_store.cpp` (implementation)
- BatteryData snapshot utilities: `lib/webserver/utils/telemetry_snapshot_utils.h`
- LED runtime globals: `src/runtime/` (check for common_lcd or equivalent)
- ESP-NOW handlers (reference): `src/espnow/espnow_message_handlers.cpp` (lines 14–51 for LED, etc.)

---

## Appendix B: MQTT Payload Reference (Expected)

### `tx/state/battery_live` (current/complete)
```json
{
  "soc": 75,
  "power": 2500,
  "voltage_mv": 48000,
  "current_ma": 52,
  "bms_status": 0,
  "temperature_c": 25.5,
  "temperature_centi_c": 2550,
  "max_charge_power_w": 3680,
  "max_discharge_power_w": 3680,
  "ts_ms": 1234567890,
  "seq": 42,
  "eth_present": false
}
```

### `tx/state/runtime/led` (NEW — to be added by transmitter)
```json
{
  "color": 1,
  "effect": 2,
  "status": "active",
  "ts_ms": 1234567890,
  "seq": 42
}
```

### `tx/state/runtime/system` (NEW — to be added by transmitter)
```json
{
  "contactor_state": 5,
  "error_flags": 0,
  "warning_flags": 2,
  "uptime_seconds": 12345,
  "ts_ms": 1234567890,
  "seq": 42
}
```

---

## Appendix C: Field Conversion Reference

| Field | Unit | Type | Notes |
|-------|------|------|-------|
| `soc` | percent | `int16_t` | Range 0–100 |
| `power` | milliwatts | `int32_t` | Transmitter publishes as `power` or `power_w` (converted by receiver) |
| `voltage_mv` | millivolts | `int32_t` | From MQTT payload; BatteryData stores as mV |
| `current_ma` | milliamps | `int32_t` | From MQTT payload; BatteryData stores as mA |
| `temperature_centi_c` | 1/100 °C | `int16_t` | From MQTT; convert to `temperature_dC` for BatteryData: `(centi + 5) / 10` |
| `temperature_dC` | 1/10 °C | `int16_t` | BatteryData internal unit; convert from centi via: `(centi + 5) / 10` |
| `bms_status` | enum | `uint8_t` | Status code (0=normal, etc.) |
| `max_charge_power_w` | watts | `uint16_t` | Maximum charge power capability |
| `max_discharge_power_w` | watts | `uint16_t` | Maximum discharge power capability |
| `contactor_state` | bit flags | `uint8_t` | Relay/contactor state (0–255) |
| `error_flags` | bit flags | `uint8_t` | System error flags |
| `warning_flags` | bit flags | `uint8_t` | System warning flags |
| `uptime_seconds` | seconds | `uint32_t` | Transmitter uptime |

---

## Summary of Changes Required

| Component | File Path | Change Type | Description |
|-----------|-----------|-------------|-------------|
| **espnowreceiver_LCD** | `src/mqtt/mqtt_client.cpp` | Update | Expand `handleBatteryLive()` to extract & store full battery telemetry |
| **espnowreceiver_2** | `src/mqtt/mqtt_client.cpp` | Update | Expand `handleBatteryLive()` to match LCD behavior (full battery telemetry) |
| **espnowreceiver_LCD** | `src/mqtt/mqtt_client.cpp` | Add | New `handleLedRuntime()` handler; hook into topic dispatch |
| **espnowreceiver_2** | `src/mqtt/mqtt_client.cpp` | Add | New `handleLedRuntime()` handler; hook into topic dispatch |
| **espnowreceiver_LCD** | `src/mqtt/mqtt_client.cpp` | Add | New `handleSystemRuntime()` handler; hook into topic dispatch |
| **espnowreceiver_2** | `src/mqtt/mqtt_client.cpp` | Add | New `handleSystemRuntime()` handler; hook into topic dispatch |
| **ESPnowtransmitter2** | `src/` (TBD) | Add | Publish `tx/state/runtime/led` with LED color/effect |
| **ESPnowtransmitter2** | `src/` (TBD) | Add | Publish `tx/state/runtime/system` with contactor/relay/error/warning state |
| **Optional (Phase 4)** | Both receivers | Add | Charger/inverter runtime MQTT handlers and transmitter publication |

---

**End of Report**
