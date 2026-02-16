# Gap Analysis: Current Implementation vs Battery Emulator

**Date**: February 16, 2026  
**Status**: Comprehensive assessment of missing components and resolution plan

---

## Executive Summary

The transmitter currently has a solid **ESP-NOW communication framework** with settings management, but is **missing all battery control logic** from Battery Emulator. This document maps each gap, priority, and resolution path.

**SCOPE**: **TRANSMITTER ONLY** — All CAN/control logic is transmitter-specific. The receiver is wireless-only (no CAN) and already has display/monitoring working.

**Critical Path Items** (blocking Phase 4 implementation on transmitter):
1. ✓ Hardware Abstraction Layer (HAL) - **RESOLVED** - See [HARDWARE_HAL.md](HARDWARE_HAL.md)
2. ✗ CAN Bus Driver (MCP2515 implementation)
3. ✗ Datalayer Structure (BMS/Charger/Inverter data storage)
4. ✗ Battery Control Logic (contactors, precharge, safety)
5. ✗ MQTT Integration (receiver-side optional logging)

---

## Gap Assessment by Category

### Category 1: Hardware Abstraction Layer (HAL)

#### Status: ✓ RESOLVED

**What's Missing**: GPIO configuration and hardware initialization sequence
- **Transmitter**: Olimex ESP32-POE2 + Waveshare RS485/CAN HAT (B)
  - CAN Controller: MCP2515 (SPI interface)
  - CAN Transceiver: TJA1050
- **Receiver**: LilyGo T-Display-S3 (display only, no CAN)

**Current State**:
- ✓ Transmitter HAL documented: [HARDWARE_HAL_TRANSMITTER.md](HARDWARE_HAL_TRANSMITTER.md)
- ✓ Receiver HAL documented: [HARDWARE_HAL_RECEIVER.md](HARDWARE_HAL_RECEIVER.md)
- ✓ Master HAL overview: [HARDWARE_HAL.md](HARDWARE_HAL.md)
- ✓ `src/config/hardware_config.h` exists with Ethernet pins
- ✗ Missing CAN GPIO pins (CLK, MOSI, MISO, CS, INT)
- ✗ Missing CAN initialization code
- ✗ Missing RS485 configuration

**How to Resolve** (transmitter only):
- ✓ Created [HARDWARE_HAL_TRANSMITTER.md](HARDWARE_HAL_TRANSMITTER.md) with complete documentation
- ✓ Created [HARDWARE_CONFIG_SUMMARY.md](HARDWARE_CONFIG_SUMMARY.md) with quick reference
- [ ] Update transmitter `src/config/hardware_config.h` with CAN GPIO constants (items below)
- [ ] Create transmitter `src/communication/can/can_config.h` with MCP2515 initialization

**Files to Update/Create** (transmitter-only):
```cpp
// Update: ESPnowtransmitter2/espnowtransmitter2/src/config/hardware_config.h
namespace hardware {
    // ... existing Ethernet config ...
    
    // CAN Interface (NEW)
    constexpr int CAN_SPI_CLK = 18;
    constexpr int CAN_SPI_MOSI = 23;
    constexpr int CAN_SPI_MISO = 19;
    constexpr int CAN_SPI_CS = 5;
    constexpr int CAN_INT = 32;
    constexpr int CAN_SPI_FREQ = 10000000;  // 10 MHz
    
    // RS485 (NEW - optional for future)
    constexpr int RS485_RX = 16;
    constexpr int RS485_TX = 17;
    constexpr int RS485_DE = 25;
}

// Create: ESPnowtransmitter2/espnowtransmitter2/src/communication/can/can_config.h
namespace can {
    constexpr uint32_t BAUDRATE_500K = 500000;  // Standard
    constexpr uint32_t BAUDRATE_250K = 250000;  // Alternative
}
```

**Note**: Receiver (LilyGo T-Display-S3) does not have CAN hardware. No receiver changes needed for CAN.

**Effort**: 2-3 hours (documentation already done)

---

### Category 2: CAN Bus Driver Implementation

#### Status: ✗ MISSING - **HIGH PRIORITY**

**What's Missing**: Complete CAN driver for MCP2515 communication
- SPI-CAN bridge initialization
- CAN message reading (from BMS, charger, inverter)
- CAN message writing (commands to devices)
- Interrupt handling for message reception
- Message parsing and validation

**Current State**:
- ✗ No CAN driver code exists
- ✗ No MCP2515 library integrated
- ✗ No CAN message definitions
- ✓ SPI peripheral available on ESP32

**How to Resolve**:

**Step 1**: Add MCP2515 library to `platformio.ini`
```ini
lib_deps =
    # ... existing ...
    tonton81/MCP2515 @ ^1.0.2  # Or equivalent SPI CAN library
```

**Step 2**: Create CAN driver files
```
src/communication/can/
├── can_driver.h           (NEW - high-level API)
├── can_driver.cpp         (NEW - initialization & I/O)
├── mcp2515_config.h       (NEW - bit timing, filters)
├── mcp2515_init.cpp       (NEW - hardware setup)
└── can_message_types.h    (NEW - message ID definitions)
```

**Step 3**: Implement CAN initialization
```cpp
// src/communication/can/can_driver.cpp
class CANDriver {
public:
    bool begin();                              // Initialize MCP2515
    bool read_message(CAN_message_t* msg);   // Read from RX buffer
    bool write_message(const CAN_message_t* msg);  // Send on bus
    void handle_interrupt();                  // ISR for message RX
    
private:
    MCP2515 mcp2515_;                        // Controller instance
};
```

**Step 4**: Wire to main control loop
```cpp
// src/main.cpp
CANDriver can_driver;
void setup() {
    // ... existing Ethernet init ...
    if (!can_driver.begin()) {
        LOG_ERROR("CAN", "Failed to initialize MCP2515!");
        // Retry logic or failover
    }
}

void battery_control_loop() {
    CAN_message_t msg;
    while (can_driver.read_message(&msg)) {
        parse_can_message(&msg);  // See Category 3
    }
}
```

**Expected Messages from Equipment**:
- **BMS** (ID 0x305): SOC, voltage, current, temperature, status flags
- **Charger** (ID 0x623): HV/LV voltage, charging current, state
- **Inverter** (ID 0x351): AC voltage/current, AC frequency, state

**Testing**:
- [ ] MCP2515 responds to SPI reads (status register)
- [ ] CAN messages received from test equipment
- [ ] Interrupt pin toggles on message RX
- [ ] Message parsing is correct
- [ ] Control loop timing unaffected (<10ms)

**Effort**: 4-5 days

**Dependencies**:
- Category 1 (HAL) - ✓ DONE
- Settings for CAN frequency/filtering (Category 5 partial)

---

### Category 3: Datalayer Structure & BMS/Charger/Inverter Drivers

#### Status: ✗ MISSING - **HIGH PRIORITY**

**What's Missing**: Central data storage structure matching Battery Emulator
- Battery datalayer (SOC, voltage, current, temperature, cell data)
- BMS-specific implementations (LEAF, LiFePO4, Tesla, etc.)
- Charger status tracking
- Inverter status tracking
- Cell voltage arrays (96 cells)
- Fault/alarm conditions

**Current State**:
- ✗ No datalayer.h/cpp
- ✗ No BMS implementations
- ✗ `settings_manager.h` has CAN settings but no CAN data storage
- ✗ No cell data structure
- ✓ Framework exists (settings_manager can be extended)

**How to Resolve**:

**Step 1**: Create datalayer structure
```cpp
// Create: src/datalayer/datalayer.h
namespace datalayer {
    // Battery status (core metrics)
    struct BatteryStatus {
        uint16_t soc_percent;           // 0-10000 (0.01% resolution)
        int32_t voltage_mv;              // Pack voltage in mV
        int32_t current_ma;              // Current in mA (positive=discharge)
        int16_t temp_min_dc;             // Min cell temp in 0.1°C
        int16_t temp_max_dc;             // Max cell temp in 0.1°C
        uint16_t cell_max_voltage_mv;    // Max cell voltage
        uint16_t cell_min_voltage_mv;    // Min cell voltage
        uint8_t bms_status;              // OK/WARNING/FAULT
        uint32_t timestamp_ms;           // When this status was last updated
    };
    
    // Cell data (optional - on-request)
    struct CellData {
        uint16_t cell_voltages[96];      // Individual cell voltages (mV)
        int8_t cell_temps[16];           // Cell temperatures (0.1°C)
        uint8_t balancing_active[12];    // Balancing cell bitmap
    };
    
    // Charger status
    struct ChargerStatus {
        uint16_t hv_voltage_v;           // High voltage DC
        uint16_t lv_voltage_v;           // Low voltage DC
        int16_t current_a;               // Charging current
        uint16_t power_w;                // Power
        uint8_t state;                   // IDLE/CHARGING/FAULT
    };
    
    // Inverter status
    struct InverterStatus {
        uint16_t ac_voltage_v;           // AC output
        int16_t ac_current_a;            // AC current
        int16_t ac_power_w;              // AC power
        uint16_t ac_freq_hz;             // AC frequency
        uint8_t state;                   // IDLE/INVERTING/FAULT
    };
    
    // Global datalayer instance
    extern BatteryStatus battery_status;
    extern ChargerStatus charger_status;
    extern InverterStatus inverter_status;
}
```

**Step 2**: Create BMS driver interface
```cpp
// Create: src/communication/can/bms_driver.h
class BMSDriver {
public:
    virtual bool parse_can_message(const CAN_message_t* msg) = 0;
    virtual void update_datalayer() = 0;
};

// Create specialized drivers for each BMS type
class LEAFBMSDriver : public BMSDriver { /* ... */ };
class LiFePO4BMSDriver : public BMSDriver { /* ... */ };
class TeslaBMSDriver : public BMSDriver { /* ... */ };
class PylontechBMSDriver : public BMSDriver { /* ... */ };
```

**Step 3**: Update CAN handler to parse messages
```cpp
// Update: src/communication/can/can_driver.cpp
void CANDriver::handle_interrupt() {
    CAN_message_t msg;
    if (read_message(&msg)) {
        switch (msg.id) {
            case 0x305:  // BMS Status
                bms_driver_->parse_can_message(&msg);
                break;
            case 0x623:  // Charger Status
                charger_driver_->parse_can_message(&msg);
                break;
            case 0x351:  // Inverter Status
                inverter_driver_->parse_can_message(&msg);
                break;
        }
    }
}
```

**Step 4**: Wire datalayer to ESP-NOW senders
```cpp
// Update: src/espnow/transmission_task.cpp
void send_battery_status_espnow() {
    battery_status_msg_t msg;
    msg.soc = datalayer::battery_status.soc_percent / 100;
    msg.voltage = datalayer::battery_status.voltage_mv / 1000;
    // ... populate other fields
    esp_now_send(receiver_mac, (uint8_t*)&msg, sizeof(msg));
}
```

**Testing**:
- [ ] Datalayer initializes with default values
- [ ] CAN messages update datalayer correctly
- [ ] Cell voltage arrays populated correctly
- [ ] Charger/inverter status updates
- [ ] ESP-NOW messages reflect datalayer data

**Effort**: 5-7 days (depends on BMS complexity)

**Dependencies**:
- Category 2 (CAN driver) - Must be partially working
- Battery Emulator source code (for message definitions)

---

### Category 4: Control Logic (Contactors, Precharge, Safety)

#### Status: ✗ MISSING - **HIGH PRIORITY**

**What's Missing**: Real-time control logic for battery system
- Contactor control (main+, main-, precharge, charger contactors)
- Precharge sequence (ramp voltage, monitor cell voltages)
- Safety checks (voltage limits, current limits, temperature limits)
- Fault detection and isolation
- State machine (IDLE, CHARGING, DISCHARGING, FAULT)

**Current State**:
- ✗ No contactor driver code
- ✗ No precharge logic
- ✗ No safety monitoring
- ✗ No fault recovery
- ✓ Task framework exists (10ms control loop possible)

**How to Resolve**:

**Step 1**: Create contactor driver
```cpp
// Create: src/control/contactors/contactor_driver.h
enum ContactorType { MAIN_PLUS, MAIN_MINUS, PRECHARGE, CHARGER };
enum ContactorState { OPEN, CLOSED, FAULT };

class ContactorDriver {
public:
    bool initialize();
    bool set_state(ContactorType type, ContactorState state);
    ContactorState get_state(ContactorType type);
};
```

**Step 2**: Implement precharge logic
```cpp
// Create: src/control/precharge/precharge_controller.h
class PrechargeController {
public:
    bool begin_precharge();      // Start precharge sequence
    PrechargeState get_state();  // IDLE, IN_PROGRESS, COMPLETE, FAILED
    bool tick();                 // Called from 10ms loop
};
```

**Step 3**: Add safety monitoring
```cpp
// Create: src/control/safety/safety_monitor.h
class SafetyMonitor {
public:
    void check_limits();         // Verify voltage/current/temp limits
    bool has_fault() const;      // Check for any active fault
    void clear_fault();          // Reset fault after correction
};
```

**Step 4**: Create state machine
```cpp
// Create: src/control/state_machine/battery_state_machine.h
enum BatteryState { IDLE, PRECHARGING, ACTIVE, CHARGING, DISCHARGING, FAULT, SHUTDOWN };

class BatteryStateMachine {
public:
    void transition_to(BatteryState new_state);
    BatteryState get_state() const;
    bool can_transition_to(BatteryState target);
};
```

**Step 5**: Integrate into main control loop
```cpp
// Update: src/main.cpp
void battery_control_loop(void* parameter) {
    while (true) {
        // 1. Read latest data from datalayer
        // 2. Check safety limits
        if (safety_monitor.has_fault()) {
            battery_sm.transition_to(BatteryState::FAULT);
            contactors.open_all();
        }
        
        // 3. Execute state machine logic
        battery_sm.tick();
        
        // 4. Update contactor states
        switch (battery_sm.get_state()) {
            case BatteryState::PRECHARGING:
                precharge_controller.tick();
                break;
            case BatteryState::ACTIVE:
                // Monitor and control power flow
                break;
        }
        
        vTaskDelayUntil(&last_wake, 10ms);
    }
}
```

**Critical Safety Rules**:
- [ ] No contactor closing without precharge
- [ ] No rapid contactor state changes (debounce)
- [ ] Automatic fault isolation (open contactors on error)
- [ ] Overvoltage protection
- [ ] Overcurrent protection
- [ ] Overtemperature protection

**Testing**:
- [ ] Precharge sequence completes successfully
- [ ] Safety limits trigger correctly
- [ ] Contactors respond to state commands
- [ ] Control loop maintains 10ms timing
- [ ] Fault conditions isolate battery automatically
- [ ] State transitions are valid

**Effort**: 7-10 days (complex safety logic)

**Dependencies**:
- Category 3 (Datalayer) - Required for data access
- Category 2 (CAN driver) - For charger/inverter feedback

---

### Category 5: Settings Management Extension

#### Status: ⚠ PARTIAL - **MEDIUM PRIORITY**

**What's Missing**: Extension of existing settings framework for battery control
- Battery control limits (min/max voltage, current limits, temperature)
- Charger configuration (charge algorithm, target voltage)
- Inverter configuration (power limits, efficiency curves)
- BMS-specific settings (cell balancing, thermal management)
- MQTT configuration (broker, credentials, topics)

**Current State**:
- ✓ SettingsManager exists with battery/charger/inverter/CAN categories
- ✓ NVS storage framework working
- ✓ Settings sync via ESP-NOW
- ✗ Missing MQTT settings (receiver-side only)
- ✗ Some fields incomplete (charger/inverter)

**How to Resolve**:

**Step 1**: Extend existing settings structures
```cpp
// Update: src/settings/settings_manager.h
struct BatterySettings {
    // ... existing fields ...
    
    // Safety limits (NEW)
    uint16_t max_pack_voltage_mv{5000};      // 50.00V
    uint16_t min_pack_voltage_mv_{4000};     // 40.00V
    uint16_t max_charge_current_a_{50};      // 50A
    uint16_t max_discharge_current_a_{100};  // 100A
    uint16_t max_temp_dc_{450};              // 45.0°C
    
    // Cell balancing (NEW)
    uint16_t balance_start_voltage_mv_{4100}; // Start at 4.1V
    uint16_t balance_target_voltage_mv_{4150};// Target 4.15V
};

struct ChargerSettings {
    // ... existing fields ...
    uint16_t charge_algorithm_{0};           // Algorithm selection
    uint16_t target_voltage_mv_{4200};       // Target cell voltage
    uint16_t charge_ramp_time_s_{60};        // Ramp duration
};

struct MQTTSettings {  // NEW
    bool enabled_{false};
    char broker_ip_[64];
    uint16_t broker_port_{1883};
    char username_[32];
    char password_[64];
    char topic_prefix_[16];  // Default "BE"
};
```

**Step 2**: Add MQTT settings to transmitter (receiver has it already)
```cpp
// Create: src/settings/mqtt_settings.h
class MQTTSettings {
public:
    bool is_enabled() const;
    const char* get_broker_ip() const;
    uint16_t get_broker_port() const;
    // ... getters for all fields
    
    bool save_to_nvs();
    bool load_from_nvs();
};
```

**Step 3**: Update web API endpoints (if transmitter web UI exists)
```cpp
// API: /api/battery_limits
{
    "max_voltage_mv": 5000,
    "min_voltage_mv": 4000,
    "max_charge_current_a": 50,
    "max_discharge_current_a": 100,
    "max_temp_c": 45
}
```

**Testing**:
- [ ] Settings load from NVS on startup
- [ ] Settings changes persist across reboot
- [ ] Settings sync via ESP-NOW to receiver
- [ ] MQTT settings configurable (if added to transmitter)

**Effort**: 2-3 days

**Dependencies**:
- Settings framework already exists (low effort)
- Optional MQTT on transmitter (deferred - receiver has it)

---

### Category 6: MQTT Integration (Receiver-Side)

#### Status: ⚠ PARTIAL - **MEDIUM PRIORITY**

**What's Missing**: Complete MQTT client on receiver for Battery Emulator topics
- MQTT client library integration
- Subscription to BE/info, BE/spec_data, BE/spec_data_2, BE/status
- JSON parsing of Battery Emulator payloads
- Cache update from MQTT messages
- SSE push to web UI
- MQTT configuration in web settings page

**Current State**:
- ✓ Documentation complete (MQTT_TOPICS_REFERENCE.md)
- ✓ NVS keys defined (mqtt_en, mqtt_broker, etc.)
- ✗ No MQTT client code
- ✗ No JSON parsers for BE/info, etc.
- ✗ No web UI MQTT settings page
- ✗ No MQTT cache in TransmitterManager

**How to Resolve**:

**Step 1**: Add MQTT library to receiver platformio.ini
```ini
[env:lilygo_t_display_s3]
lib_deps =
    # ... existing ...
    knolleary/PubSubClient @ ^2.8  # MQTT client
    bblanchon/ArduinoJson @ ^6.21.3  # JSON parsing
```

**Step 2**: Create MQTT client wrapper
```cpp
// Create: espnowreciever_2/lib/mqtt_client/mqtt_client.h
class MQTTClient {
public:
    bool begin();                              // Initialize MQTT
    bool connect();                            // Connect to broker
    bool is_connected() const;
    void subscribe_to_battery_topics();        // Subscribe to BE/*
    void handle_message(const char* topic, const char* payload);  // Callback
    void publish_message(const char* topic, const char* payload); // Optional
};
```

**Step 3**: Create message parsers
```cpp
// Create: espnowreciever_2/lib/mqtt_client/mqtt_handlers.h
class MQTTHandlers {
public:
    static void handle_be_info(const char* payload);           // Battery status
    static void handle_be_spec_data(const char* payload);      // Cell voltages
    static void handle_be_spec_data_2(const char* payload);    // Dual battery cells
    static void handle_be_status(const char* payload);         // LWT availability
};

// Implementation example:
void MQTTHandlers::handle_be_info(const char* payload) {
    DynamicJsonDocument doc(512);
    deserializeJson(doc, payload);
    
    // Update receiver cache
    TransmitterManager::set_mqtt_battery_soc(doc["SOC"]);
    TransmitterManager::set_mqtt_battery_voltage(doc["battery_voltage"]);
    // ... populate remaining fields
    
    // Notify web UI
    notify_sse_mqtt_updated();
}
```

**Step 4**: Create MQTT settings page
```cpp
// Create: espnowreciever_2/lib/webserver/pages/mqtt_settings_page.cpp
void handle_mqtt_settings_page(httpd_req_t *req) {
    // HTML form with fields:
    // - MQTT enable checkbox
    // - Broker IP/Port
    // - Username/password
    // - Topic prefix (default "BE")
    // - Connection test button
    // - Connection status
}
```

**Step 5**: Integrate into main receiver task
```cpp
// Update: espnowreciever_2/src/main.cpp
void setup() {
    // ... existing WiFi/ESP-NOW setup ...
    
    mqtt_client.begin();  // Initialize MQTT
    mqtt_client.subscribe_to_battery_topics();  // Subscribe
}

void loop() {
    // ... existing ESP-NOW handling ...
    
    if (mqtt_client.is_connected()) {
        mqtt_client.loop();  // Handle MQTT messages
    }
}
```

**Testing**:
- [ ] MQTT client connects to test broker
- [ ] Subscriptions active (topic list visible)
- [ ] BE/info messages received and parsed
- [ ] Cell data (BE/spec_data) cached correctly
- [ ] Web UI displays MQTT data (via SSE)
- [ ] Settings page saves/loads MQTT config
- [ ] LWT indicator shows connection status

**Effort**: 4-5 days

**Dependencies**:
- Settings framework (partial, MQTT keys already defined)
- Web UI infrastructure (existing)

---

## Summary by Priority & Timeline

### Critical Path (Blocking Phase 4)

| # | Item | Status | Effort | Blocker For |
|---|------|--------|--------|------------|
| 1 | Hardware HAL | ✓ DONE | 0h | Everything |
| 2 | CAN Driver | ✗ TODO | 4-5d | Phase 4a (battery data) |
| 3 | Datalayer | ✗ TODO | 5-7d | Phase 4b (control logic) |
| 4 | Control Logic | ✗ TODO | 7-10d | Phase 4c (safety) |

### Important But Not Blocking

| # | Item | Status | Effort | Impact |
|---|------|--------|--------|--------|
| 5 | Settings Ext. | ⚠ PARTIAL | 2-3d | Battery limits, safety config |
| 6 | MQTT (RX) | ⚠ PARTIAL | 4-5d | Persistent logging, dual battery |

---

## Implementation Roadmap

### Week 1 (Days 1-5)
- ✓ Category 1: HAL - DONE
- [ ] Category 2: CAN Driver (Days 1-5)

### Week 2 (Days 6-10)
- [ ] Category 3: Datalayer (Days 6-10)

### Week 3 (Days 11-17)
- [ ] Category 4: Control Logic (Days 11-17)

### Week 4+ (Days 18+)
- [ ] Category 5: Settings Extension (Days 18-20)
- [ ] Category 6: MQTT Integration (Days 21-25)

---

## Verification Checklist

Before moving to Phase 5 (LED migration):

**Hardware Tier**:
- [ ] SPI CAN communication verified (MCP2515 status register readable)
- [ ] CAN messages received from test equipment
- [ ] Interrupt pin toggles on message RX

**Datalayer Tier**:
- [ ] BMS data parsed correctly into datalayer
- [ ] Cell voltage arrays populated
- [ ] Charger/inverter status updates
- [ ] No memory corruption or leaks

**Control Tier**:
- [ ] Precharge sequence completes without faults
- [ ] Contactors respond to state changes
- [ ] Safety checks trigger correctly on limit violations
- [ ] Control loop timing never exceeds 10ms

**Integration Tier**:
- [ ] ESP-NOW transmits datalayer to receiver
- [ ] Receiver displays live battery data
- [ ] Web UI updates in real-time (SSE)
- [ ] MQTT (if implemented) logs data persistently

---

## References

- **Battery Emulator Source**: `C:\Users\GrahamWillsher\Downloads\Battery-Emulator-9.2.4\`
- **Hardware HAL Overview**: [HARDWARE_HAL.md](HARDWARE_HAL.md)
- **Transmitter Hardware HAL**: [HARDWARE_HAL_TRANSMITTER.md](HARDWARE_HAL_TRANSMITTER.md)
- **Receiver Hardware HAL**: [HARDWARE_HAL_RECEIVER.md](HARDWARE_HAL_RECEIVER.md)
- **Hardware Config Summary**: [HARDWARE_CONFIG_SUMMARY.md](HARDWARE_CONFIG_SUMMARY.md)
- **Migration Plan**: [BATTERY_EMULATOR_MIGRATION_PLAN.md](BATTERY_EMULATOR_MIGRATION_PLAN.md)
- **MQTT Topics**: [MQTT_TOPICS_REFERENCE.md](MQTT_TOPICS_REFERENCE.md)

---

**Document Version**: 1.0  
**Created**: February 16, 2026  
**Status**: Ready for implementation planning
