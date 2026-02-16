# Migration Comparison: Battery Emulator vs ESP32 Multi-Device Setup

**Date**: February 16, 2026  
**Purpose**: Identify missing components for successful Battery Emulator migration

---

## Architecture Comparison

### Battery Emulator 9.2.4 (Single Device)

```
┌─────────────────────────────────────────────────────┐
│          Battery Emulator (ESP32S3)                 │
├─────────────────────────────────────────────────────┤
│ ┌─────────────┐  ┌──────────────┐  ┌────────────┐ │
│ │  CAN Bus    │  │   Display    │  │  Web UI    │ │
│ │  (MCP2515)  │  │   (TFT/LED)  │  │  (WiFi)    │ │
│ └─────────────┘  └──────────────┘  └────────────┘ │
│         │                │                │         │
│         └────────────────┼────────────────┘         │
│                          │                          │
│              ┌───────────▼──────────┐               │
│              │    Datalayer         │               │
│              │  (Global State)      │               │
│              └───────────┬──────────┘               │
│                          │                          │
│         ┌────────────────┼────────────────┐         │
│         │                │                │         │
│    ┌────▼────┐     ┌────▼─────┐    ┌────▼────┐    │
│    │ Battery │     │ Charger  │    │Inverter │    │
│    │ Control │     │ Control  │    │ Control │    │
│    └─────────┘     └──────────┘    └─────────┘    │
│         │                │                │         │
│    ┌────▼────┐     ┌────▼─────┐    ┌────▼────┐    │
│    │Contactor│     │Precharge │    │ Safety  │    │
│    │ Driver  │     │ Logic    │    │ Monitor │    │
│    └─────────┘     └──────────┘    └─────────┘    │
│                                                     │
│               ┌──────────────┐                     │
│               │ MQTT Publish │                     │
│               └──────────────┘                     │
└─────────────────────────────────────────────────────┘
         ↑                    ↓
    CAN Messages         MQTT Topics
  (BMS/Charger/Inv)   (BE/info, BE/spec_data)
```

### ESP32 Multi-Device Setup (Current)

```
┌──────────────────────────────────┐   ESP-NOW   ┌──────────────────────────────────┐
│  Transmitter (ESP32-POE2)        │◄────────────►│  Receiver (T-Display-S3)         │
├──────────────────────────────────┤              ├──────────────────────────────────┤
│ ┌──────────┐  ┌───────────────┐ │              │ ┌──────────┐  ┌────────────────┐│
│ │ CAN Bus  │  │   Settings    │ │              │ │ Display  │  │   Web Server   ││
│ │(MCP2515) │  │   Manager     │ │              │ │ (TFT)    │  │   (WiFi AP)    ││
│ │          │  │   (NVS)       │ │              │ │          │  │                ││
│ └────┬─────┘  └───────┬───────┘ │              │ └────┬─────┘  └────────┬───────┘│
│      │                │          │              │      │                 │        │
│      │   ┌────────────▼────┐    │              │      │    ┌────────────▼────┐   │
│      │   │  ESP-NOW Sender │    │              │      │    │TransmitterManager│  │
│      │   └────────────┬────┘    │              │      │    │   (Cache)        │  │
│      │                │          │              │      │    └────────────┬────┘   │
│      │                │          │              │      │                 │        │
│  ┌───▼────────────────▼────┐    │              │  ┌───▼─────────────────▼────┐   │
│  │   ✗ MISSING:             │    │              │  │   ESP-NOW Receiver       │   │
│  │   - Datalayer            │    │              │  │   (Queue + Handlers)     │   │
│  │   - Battery Control      │    │              │  └────┬──────────┬──────────┘   │
│  │   - Charger Interface    │    │              │       │          │              │
│  │   - Inverter Interface   │    │              │  ┌────▼────┐  ┌──▼──────────┐  │
│  │   - Contactor Driver     │    │              │  │  LED    │  │    MQTT     │  │
│  │   - Precharge Logic      │    │              │  │ Display │  │   Client    │  │
│  │   - Safety Monitor       │    │              │  └─────────┘  │  (Optional) │  │
│  └──────────────────────────┘    │              │               └─────────────┘  │
│                                   │              │                                │
│  Currently: DUMMY DATA ONLY       │              │  Currently: DISPLAY + WEB UI   │
└──────────────────────────────────┘              └──────────────────────────────┘
         ↑                                                   │
    CAN Messages                                        WiFi Client
  (NOT IMPLEMENTED)                                   (Settings Editor)
```

---

## Component-by-Component Comparison

### ✓ Already Implemented

| Component | Battery Emulator | Transmitter | Receiver | Status |
|-----------|------------------|-------------|----------|--------|
| **WiFi** | ✓ AP mode | ✓ STA mode | ✓ STA+AP | ✓ Working |
| **Web Server** | ✓ HTTP API | ✗ N/A | ✓ HTTP API | ✓ Working |
| **Settings Storage** | ✓ NVS | ✓ NVS | ✓ NVS | ✓ Working |
| **Display** | ✓ TFT/LED | ✗ No display | ✓ TFT | ✓ Working |
| **ESP-NOW** | ✗ Not used | ✓ Sender | ✓ Receiver | ✓ Working |
| **MQTT Client** | ✓ Publisher | ✗ N/A | ⚠ Partial | ⚠ Documented only |
| **OTA Updates** | ✓ Web upload | ✓ Web upload | ✓ Web upload | ✓ Working |
| **Version Tracking** | ✓ String | ✓ Semantic | ✓ Semantic | ✓ Working |

### ✗ Missing on Transmitter (Critical Path)

| Component | Battery Emulator | Transmitter | Impact |
|-----------|------------------|-------------|--------|
| **CAN Driver** | ✓ MCP2515 lib | ✗ Not implemented | **BLOCKER** - No battery data |
| **Datalayer** | ✓ Global state | ✗ Not implemented | **BLOCKER** - No data storage |
| **BMS Interface** | ✓ Multi-BMS | ✗ Not implemented | **BLOCKER** - No battery reading |
| **Charger Interface** | ✓ Multi-charger | ✗ Not implemented | **HIGH** - No charger control |
| **Inverter Interface** | ✓ Multi-inverter | ✗ Not implemented | **HIGH** - No inverter control |
| **Contactor Driver** | ✓ GPIO control | ✗ Not implemented | **CRITICAL** - No contactors |
| **Precharge Logic** | ✓ State machine | ✗ Not implemented | **CRITICAL** - Safety hazard |
| **Safety Monitor** | ✓ Limits check | ✗ Not implemented | **CRITICAL** - No protection |
| **Control Loop** | ✓ 10ms task | ✗ Dummy data only | **BLOCKER** - No real control |

### ⚠ Optional/Future

| Component | Battery Emulator | Transmitter | Receiver | Priority |
|-----------|------------------|-------------|----------|----------|
| **SD Card Logging** | ✓ Yes | ✗ No | ✗ No | Low |
| **MQTT Publishing** | ✓ Yes | ✗ No | ⚠ Subscribe | Medium |
| **Dual Battery** | ✓ Supported | ⚠ Settings only | ⚠ Settings only | Low |
| **RS485** | ✓ Optional | ⚠ HAL only | ✗ No | Low |
| **mDNS** | ✓ Yes | ✗ No | ✗ No | Low |

---

## Data Flow Comparison

### Battery Emulator (Single Device)

```
┌───────────┐
│ CAN Bus   │
│ (BMS)     │
└─────┬─────┘
      │ 500kbps
      ▼
┌─────────────────┐
│  CAN Message    │
│  Parser         │
└─────┬───────────┘
      │
      ▼
┌─────────────────┐     ┌──────────────┐
│  Datalayer      │────►│  Web Server  │
│  (Global State) │     │  JSON API    │
└─────┬───────────┘     └──────────────┘
      │                         │
      ├─────────────────────────┤
      │                         │
      ▼                         ▼
┌──────────────┐      ┌──────────────┐
│   Display    │      │  MQTT Client │
│   Update     │      │  (Publish)   │
└──────────────┘      └──────────────┘
```

### ESP32 Multi-Device (Target)

```
Transmitter                    Receiver
┌───────────┐                  
│ CAN Bus   │                  
│ (BMS)     │                  
└─────┬─────┘                  
      │ 500kbps                
      ▼                        
┌─────────────────┐            
│  CAN Message    │            
│  Parser         │            
└─────┬───────────┘            
      │                        
      ▼                        
┌─────────────────┐            
│  Datalayer      │            
│  (Local State)  │            
└─────┬───────────┘            
      │                        
      │ ESP-NOW (2.4GHz)       
      ├───────────────────────►┌──────────────────┐
      │                        │ TransmitterMgr   │
      │                        │ (Cache)          │
      │                        └────────┬─────────┘
      │                                 │
      │                        ┌────────┼─────────┐
      │                        │        │         │
      │                        ▼        ▼         ▼
      │                  ┌─────────┐ ┌────────┐ ┌──────┐
      │                  │ Display │ │Web API │ │ MQTT │
      │                  └─────────┘ └────────┘ │Client│
      │                                          └──────┘
      │                                              │
      │◄─────────────────────────────────────────────┘
      │             Settings Updates (ESP-NOW)
      │
      ▼
┌─────────────────┐
│  Settings       │
│  Manager (NVS)  │
└─────────────────┘
```

---

## Critical Missing Components (Detailed)

### 1. CAN Driver (MCP2515)

**Battery Emulator Has**:
```cpp
// Software/src/communication/can/comm_can.cpp
#include <mcp_can.h>  // Seeed CAN library
MCP_CAN CAN0(MCP_CAN_CS);  // CS pin

void init_CAN() {
    if (CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) == CAN_OK) {
        CAN0.setMode(MCP_NORMAL);
        // Set filters for BMS/Charger/Inverter IDs
    }
}

void receive_can() {
    long unsigned int rxId;
    unsigned char len = 0;
    unsigned char rxBuf[8];
    
    if (!digitalRead(CAN_INT_PIN)) {
        CAN0.readMsgBuf(&rxId, &len, rxBuf);
        parse_can_message(rxId, rxBuf, len);
    }
}
```

**Transmitter Has**:
```cpp
// NOTHING - Not implemented yet
// Need to create:
// - src/communication/can/can_driver.h/cpp
// - src/communication/can/mcp2515_init.cpp
// - Add MCP2515 library to platformio.ini
```

**Gap**: Complete CAN driver stack missing

---

### 2. Datalayer (Global State)

**Battery Emulator Has**:
```cpp
// Software/src/datalayer/datalayer.h
struct DATALAYER_BATTERY_STATUS_TYPE {
    uint32_t remaining_capacity_Wh;
    uint32_t max_discharge_power_W;
    uint32_t max_charge_power_W;
    int32_t active_power_W;
    uint16_t voltage_dV;
    uint16_t real_soc;
    uint16_t reported_soc;
    uint16_t cell_max_voltage_mV;
    uint16_t cell_min_voltage_mV;
    int16_t temperature_max_dC;
    int16_t temperature_min_dC;
    int16_t current_dA;
    uint8_t bms_status;  // ACTIVE, FAULT, etc.
    uint16_t cell_voltages_mV[MAX_AMOUNT_CELLS];
    bool cell_balancing_status[MAX_AMOUNT_CELLS];
};

extern DATALAYER datalayer;  // Global instance
```

**Transmitter Has**:
```cpp
// PARTIAL - Only settings storage, no runtime data
// SettingsManager has:
// - battery_capacity_wh_
// - battery_max_voltage_mv_
// - battery_min_voltage_mv_
// - battery_max_charge_current_a_
// - battery_max_discharge_current_a_

// MISSING:
// - Real-time battery status (voltage, current, SOC, power)
// - Cell-level data (96 cell voltages, balancing status)
// - Charger status (HV/LV voltage, current, state)
// - Inverter status (AC voltage, frequency, power)
// - BMS fault/alarm tracking
```

**Gap**: No runtime data storage, only static configuration

---

### 3. BMS Interface

**Battery Emulator Has**:
```cpp
// Software/src/battery/BATTERIES.h
// Multiple BMS implementations:
// - NISSAN_LEAF_BATTERY
// - TESLA_MODEL_3_BATTERY
// - BYD_ATTO_3_BATTERY
// - PYLON_BATTERY
// - etc. (20+ BMS types)

// Each implements:
void receive_can_battery(CAN_frame rx_frame) {
    switch (rx_frame.ID) {
        case 0x355:  // BMS status
            battery_voltage = (rx_frame.data[0] << 8) | rx_frame.data[1];
            battery_current = (rx_frame.data[2] << 8) | rx_frame.data[3];
            break;
        case 0x356:  // Cell data
            for (int i = 0; i < 12; i++) {
                cell_voltages[i] = (rx_frame.data[i*2] << 8) | rx_frame.data[i*2+1];
            }
            break;
    }
}
```

**Transmitter Has**:
```cpp
// NOTHING - No BMS interface code at all
```

**Gap**: Complete BMS driver implementation missing

---

### 4. Contactor Control + Precharge

**Battery Emulator Has**:
```cpp
// Software/src/communication/contactorcontrol/comm_contactorcontrol.cpp
enum ContactorState {
    CONTACTOR_OPEN,
    PRECHARGE_START,
    PRECHARGE_WAIT,
    CONTACTOR_CLOSE,
    FAULT
};

void handle_contactors() {
    static ContactorState state = CONTACTOR_OPEN;
    
    switch (state) {
        case CONTACTOR_OPEN:
            if (datalayer.battery.status.bms_status == ACTIVE) {
                digitalWrite(PRECHARGE_PIN, HIGH);
                state = PRECHARGE_START;
                precharge_start_time = millis();
            }
            break;
            
        case PRECHARGE_START:
            if (millis() - precharge_start_time > PRECHARGE_DURATION_MS) {
                state = PRECHARGE_WAIT;
            }
            break;
            
        case PRECHARGE_WAIT:
            uint16_t voltage_diff = abs(pack_voltage - inverter_voltage);
            if (voltage_diff < PRECHARGE_TOLERANCE_MV) {
                digitalWrite(MAIN_CONTACTOR_PIN, HIGH);
                digitalWrite(PRECHARGE_PIN, LOW);
                state = CONTACTOR_CLOSE;
            }
            break;
            
        case CONTACTOR_CLOSE:
            // Monitor voltage/current/temperature
            if (fault_detected()) {
                digitalWrite(MAIN_CONTACTOR_PIN, LOW);
                state = FAULT;
            }
            break;
    }
}
```

**Transmitter Has**:
```cpp
// NOTHING - No contactor control code
// SettingsManager has contactor_control_enabled_ flag but no driver
```

**Gap**: Complete contactor driver + precharge sequence missing

---

### 5. Safety Monitor

**Battery Emulator Has**:
```cpp
// Software/src/devboard/utils/events.cpp
void check_battery_limits() {
    // Overvoltage
    if (datalayer.battery.status.voltage_dV > datalayer.battery.info.max_design_voltage_dV) {
        set_event(EVENT_OVERVOLTAGE, voltage);
        datalayer.battery.status.bms_status = FAULT;
    }
    
    // Undervoltage
    if (datalayer.battery.status.voltage_dV < datalayer.battery.info.min_design_voltage_dV) {
        set_event(EVENT_UNDERVOLTAGE, voltage);
        datalayer.battery.status.bms_status = FAULT;
    }
    
    // Overcurrent
    if (abs(datalayer.battery.status.current_dA) > datalayer.battery.settings.max_user_set_discharge_dA) {
        set_event(EVENT_OVERCURRENT, current);
        datalayer.battery.status.bms_status = FAULT;
    }
    
    // Overtemperature
    if (datalayer.battery.status.temperature_max_dC > BATTERY_MAX_TEMP_DC) {
        set_event(EVENT_OVERTEMPERATURE, temp);
        datalayer.battery.status.bms_status = FAULT;
    }
    
    // Cell voltage deviation
    uint16_t cell_diff = datalayer.battery.status.cell_max_voltage_mV - 
                         datalayer.battery.status.cell_min_voltage_mV;
    if (cell_diff > datalayer.battery.info.max_cell_voltage_deviation_mV) {
        set_event(EVENT_CELL_IMBALANCE, cell_diff);
    }
}
```

**Transmitter Has**:
```cpp
// NOTHING - No safety monitoring code
// Settings exist (max_voltage, max_current) but no enforcement
```

**Gap**: Safety limit checking and fault isolation missing

---

## ESP-NOW Message Types (Already Working)

### Data Messages (Transmitter → Receiver)

| Message Type | Status | Notes |
|--------------|--------|-------|
| `battery_status_msg_t` | ✓ Defined | SOC, power, voltage, current, timestamp |
| `battery_settings_full_msg_t` | ✓ Defined | Capacity, voltage limits, current limits, SOC limits |
| `charger_status_msg_t` | ✓ Defined | HV/LV voltage, current, power, state |
| `inverter_status_msg_t` | ✓ Defined | AC voltage, current, frequency, power, state |
| `network_info_msg_t` | ✓ Defined | IP, MAC, gateway, DNS |

### Settings Update Messages (Receiver → Transmitter)

| Message Type | Status | Notes |
|--------------|--------|-------|
| `settings_update_msg_t` | ✓ Working | Category, field, value, version |
| `settings_ack_msg_t` | ✓ Working | Success/failure acknowledgment |

### Control Messages

| Message Type | Status | Notes |
|--------------|--------|-------|
| `msg_request_data` | ✓ Working | Start data transmission |
| `msg_abort_data` | ✓ Working | Stop data transmission |
| `msg_reboot` | ✓ Working | Reboot transmitter |
| `msg_flash_led` | ✓ Working | Trigger LED on receiver |

---

## Implementation Priorities (by Blocking Dependency)

### Phase 4a: CAN + Datalayer (5-7 days) **[IMMEDIATE]**

**Goal**: Get real battery data flowing from CAN bus to datalayer

1. **CAN Driver** (2 days)
   - Add MCP2515 library to `platformio.ini`
   - Create `src/communication/can/can_driver.h/cpp`
   - Initialize SPI + MCP2515
   - Implement interrupt-driven message reception
   - **Test**: CAN messages received and parsed

2. **Datalayer Structure** (2 days)
   - Create `src/datalayer/datalayer.h/cpp`
   - Define `BatteryStatus`, `ChargerStatus`, `InverterStatus` structs
   - Add cell voltage arrays (96 cells)
   - **Test**: Datalayer populates from dummy CAN messages

3. **BMS Interface** (2 days)
   - Create `src/battery/bms_driver.h`
   - Implement parsers for key BMS messages (0x305, 0x306, etc.)
   - Wire to datalayer updates
   - **Test**: Real BMS data updates datalayer

4. **ESP-NOW Integration** (1 day)
   - Update `DataSender` to read from datalayer (not dummy data)
   - Send real battery status via ESP-NOW
   - **Test**: Receiver displays real battery data

**Deliverable**: Transmitter reads CAN bus, stores in datalayer, sends to receiver via ESP-NOW

---

### Phase 4b: Control Logic (7-10 days) **[HIGH PRIORITY]**

**Goal**: Implement safe battery control (contactors, precharge, safety)

1. **Contactor Driver** (2 days)
   - Create `src/control/contactors/contactor_driver.h/cpp`
   - GPIO configuration for main+, main-, precharge contactors
   - State control (open/close)
   - **Test**: Contactors respond to commands

2. **Precharge Controller** (2 days)
   - Create `src/control/precharge/precharge_controller.h/cpp`
   - Implement precharge sequence (ramp, monitor, close)
   - Timeout + fault detection
   - **Test**: Precharge completes without faults

3. **Safety Monitor** (2 days)
   - Create `src/control/safety/safety_monitor.h/cpp`
   - Voltage/current/temperature limit checking
   - Fault detection and isolation (open contactors on fault)
   - **Test**: Faults trigger correctly, contactors isolate

4. **State Machine** (2 days)
   - Create `src/control/state_machine/battery_state_machine.h/cpp`
   - States: IDLE, PRECHARGING, ACTIVE, CHARGING, DISCHARGING, FAULT
   - Transition logic
   - **Test**: State transitions valid

5. **Control Loop Task** (2 days)
   - Create 10ms control loop task
   - Read datalayer → Check safety → Execute state machine → Update contactors
   - **Test**: Control loop maintains 10ms timing

**Deliverable**: Transmitter controls battery contactors safely with precharge

---

### Phase 4c: MQTT Integration (Receiver) (4-5 days) **[MEDIUM PRIORITY]**

**Goal**: Receiver subscribes to Battery Emulator MQTT topics for persistent logging

1. **MQTT Client Library** (1 day)
   - Add `PubSubClient` to receiver `platformio.ini`
   - Create `lib/mqtt_client/mqtt_client.h/cpp`
   - Initialize, connect, subscribe to BE/info, BE/spec_data, etc.
   - **Test**: MQTT client connects to broker

2. **Message Handlers** (2 days)
   - Create `lib/mqtt_client/mqtt_handlers.h/cpp`
   - Parse JSON from BE/info (battery status)
   - Parse JSON from BE/spec_data (cell voltages)
   - Update TransmitterManager cache
   - **Test**: MQTT messages update cache

3. **Settings Page** (1 day)
   - Create `lib/webserver/pages/mqtt_settings_page.cpp`
   - HTML form: broker IP, port, username, password
   - Connection test button
   - **Test**: Settings save/load from NVS

4. **Integration** (1 day)
   - Wire MQTT client into main receiver loop
   - Notify SSE when MQTT data updated
   - **Test**: Web UI displays MQTT data

**Deliverable**: Receiver logs battery data from MQTT (dual source: ESP-NOW + MQTT)

---

## Summary: What's Missing

| Category | Missing Components | Blocking? | Effort |
|----------|-------------------|-----------|--------|
| **CAN Communication** | Driver, initialization, interrupt handling | ✓ YES | 2 days |
| **Data Storage** | Datalayer structure, BMS/Charger/Inverter data | ✓ YES | 2 days |
| **BMS Interface** | Message parsers, cell data handling | ✓ YES | 2 days |
| **Control Logic** | Contactor driver, precharge, state machine | ✓ YES | 7 days |
| **Safety** | Limit checking, fault isolation | ✓ YES | 2 days |
| **MQTT (Receiver)** | Client library, message handlers, settings | ✗ NO | 5 days |
| **Charger Interface** | CAN message parsing, status tracking | ⚠ PARTIAL | 1 day |
| **Inverter Interface** | CAN message parsing, status tracking | ⚠ PARTIAL | 1 day |

**Total Effort**: ~24-29 days (assuming serial implementation)

**Critical Path**: CAN Driver → Datalayer → BMS Interface → Control Logic (~18 days)

---

## What Already Works (No Migration Needed)

✓ **Settings Management**: Transmitter has full settings storage + ESP-NOW sync  
✓ **Network Communication**: Ethernet on transmitter, WiFi on receiver  
✓ **ESP-NOW Framework**: Reliable transmission, handshakes, version beacons  
✓ **Web UI**: Receiver has complete web interface with SSE updates  
✓ **Display**: Receiver has TFT display with real-time updates  
✓ **OTA**: Both devices support OTA firmware updates  
✓ **Version Tracking**: Semantic versioning with build dates  

---

## Next Steps

1. **Update GAP_ANALYSIS.md** with this comparison data
2. **Start Phase 4a**: CAN Driver + Datalayer implementation
3. **Test incrementally**: Each component tested before moving to next
4. **Document as we go**: Update implementation logs

---

**Document Version**: 1.0  
**Created**: February 16, 2026  
**Status**: Ready for Phase 4 implementation planning
