# Battery Emulator Integration Architecture & Migration Plan
**Document Version:** 2.0  
**Date:** February 17, 2026  
**Status:** Comprehensive Analysis & Resolution Strategy  
**Scope:** Migrating complete Battery Emulator framework to 2-device ESP32 architecture

---

## ⚠️ SCOPE CLARIFICATION - PHASE 1 FOCUS

**Phase 1 (Current):** Porting & Integration Only
- ✅ Port Battery Emulator code to 2-device structure
- ✅ Get it working correctly with transmitter/receiver architecture
- ✅ Verify data flows correctly via ESP-NOW
- ✅ Validate battery monitoring functionality
- ❌ **NOT in scope:** Code refactoring, system improvements, or enhancements
- ❌ **NOT in scope:** UI/UX improvements
- ❌ **NOT in scope:** Performance optimization
- ❌ **NOT in scope:** Additional feature development

**Phase 2+ (Later - Future Work):**
- Code refactoring and cleanup
- System improvements and optimizations
- UI/UX enhancements
- Additional features (multi-battery support, advanced filtering, etc.)
- Performance tuning

**This document focuses exclusively on Phase 1 requirements.** Refactoring suggestions and improvement ideas are documented separately for future consideration.

---

## Executive Summary

This document provides a complete architectural analysis of Battery Emulator 9.2.4's core framework and defines a step-by-step migration strategy for integration into a 2-device architecture (Transmitter + Receiver over ESP-NOW).

**Key Decision:** Keep Battery Emulator's proven control logic and datalayer architecture intact. Layer it on top of the 2-device ESP-NOW communication model rather than trying to refactor the internals.

**Critical Success Factor:** The two-device boundary must be transparent to Battery Emulator's core logic—control decisions remain on the transmitter, data flows to receiver via ESP-NOW.

**Phase 1 Goal:** "Transmitter receives real battery data via CAN, sends snapshots to receiver every 100ms, receiver displays it correctly." All code remains in Battery Emulator's original structure—no refactoring, no rewrites.

---

## Part 1: Battery Emulator Architecture Deep Dive

### 1.1 Core Component Hierarchy

Battery Emulator's architecture is built around **three independent but interconnected systems**:

```
┌─────────────────────────────────────────────────────────────┐
│                    BATTERY EMULATOR CORE                    │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────────┐      ┌──────────────┐   ┌──────────────┐ │
│  │   DATALAYER  │      │   BATTERY    │   │  INVERTER    │ │
│  │  (Singleton) │◄────►│   MANAGER    │◄─►│  MANAGER     │ │
│  │              │      │  (BMS Logic) │   │ (Control)    │ │
│  └──────────────┘      └──────────────┘   └──────────────┘ │
│       ▲  ▲                    ▲                    ▲         │
│       │  │                    │                    │         │
│  ┌────┴──┴────────────────────┴────────────────────┴────┐   │
│  │           CAN COMMUNICATION FRAMEWORK              │   │
│  ├─────────────────────────────────────────────────────┤   │
│  │  • CAN Receiver (40+ BMS implementations)          │   │
│  │  • CAN Transmitter (15+ Inverter protocols)        │   │
│  │  • Registration system (auto-discovery)            │   │
│  │  • Multi-interface support (CAN, CANFD, RS485)     │   │
│  └─────────────────────────────────────────────────────┘   │
│                       ▲         ▲                           │
└───────────────────────┼─────────┼───────────────────────────┘
                        │         │
              ┌─────────┘         └─────────┐
              │                             │
         ┌────▼───┐               ┌────────▼────┐
         │CAN Bus │               │ Local HAL   │
         │(Pylon, │               │(GPIO, SPI)  │
         │Inverter)               │             │
         └────────┘               └─────────────┘
```

### 1.2 Component Descriptions

#### A. Datalayer (Global State Container)

**Files:** `datalayer/datalayer.h`, `datalayer_extended.h`

**Purpose:** Single global state container for all battery/inverter/charger/system information

**Key Structure:**
```cpp
class DataLayer {
  DATALAYER_BATTERY_TYPE battery;        // Primary battery state
  DATALAYER_BATTERY_TYPE battery2;       // Secondary battery (dual-battery systems)
  DATALAYER_SHUNT_TYPE shunt;            // Shunt measurements (voltage, current, power)
  DATALAYER_CHARGER_TYPE charger;        // Charger state and limits
  DATALAYER_SYSTEM_TYPE system;          // System status (contactors, precharge, etc.)
};
```

**Key Fields in `battery.status`:**
- `voltage_dV`, `current_dA`, `active_power_W` - Real-time measurements
- `reported_soc`, `real_soc` - State of charge (pppt format: percent × 100)
- `temperature_max_dC`, `temperature_min_dC` - Temperature range
- `cell_voltages_mV[192]` - Individual cell voltages (up to 192 cells)
- `max_charge_power_W`, `max_discharge_power_W` - Power limits
- `real_bms_status`, `bms_status` - Connection and system status enums
- `CAN_battery_still_alive` - Heartbeat counter

**Critical Properties:**
- Global singleton: `extern DataLayer datalayer;`
- Updated in real-time by BMS parsers via CAN
- Read by inverter control code to make power decisions
- Synchronously accessed (no locking in Battery Emulator)

---

#### B. Battery Management System (40+ Implementations)

**Location:** `battery/` directory (50+ files)

**Architecture:**
```cpp
Battery (abstract base class)
    ├── void setup()                    // Hardware initialization
    ├── void update_values()            // Parse CAN/RS485, update datalayer
    └── virtual bool supports_*()       // Capability flags

CanBattery (extends Battery, CanReceiver, Transmitter)
    ├── void receive_can_frame()        // Parse incoming CAN message
    ├── void transmit()                 // Send periodic CAN messages
    └── Auto-registers on construction
        ├── register_can_receiver(this, CAN_Interface, speed)
        └── register_transmitter(this)

Concrete implementations (50+ battery types):
    ├── PYLON_BATTERY
    ├── NISSAN_LEAF_BATTERY
    ├── TESLA_MODEL_3_MODEL_Y
    ├── BmwI3Battery
    ├── BydAtto3Battery
    ├── KiaHyundaiHybridBattery
    └── ... (44 more)
```

**Critical Pattern:** 
- Each BMS class constructor **auto-registers** itself as a CAN receiver
- No factory or initialization code needed—just `new PYLON_BATTERY()`
- All parsing happens in `receive_can_frame(CAN_frame* frame)`
- Direct datalayer field access: `datalayer.battery.status.voltage_dV = ...`

**50+ BMS Types Included:**
1. PYLON-BATTERY (LiFePO4 energy storage system)
2. NISSAN-LEAF-BATTERY (Nissan EV models)
3. TESLA-BATTERY (Tesla Model 3/Y/S/X)
4. BMW-I3-BATTERY, BMW-IX-BATTERY, BMW-PHEV
5. BYD-ATTO-3-BATTERY (BYD passenger vehicle)
6. HYUNDAI-IONIQ-28-BATTERY, KIA variants
7. RENAULT-ZOE-GEN1/2, RENAULT-KANGOO, RENAULT-TWIZY
8. VW MEB-BATTERY (Volkswagen ID family)
9. FORD-MACH-E-BATTERY
10. JAGUAR-IPACE-BATTERY
11. RIVIAN-BATTERY
12. And 35+ more (including ORION, DALY, Chevy BOLT/AMPERA, etc.)

---

#### C. Inverter Control System (15+ Protocols)

**Location:** `inverter/` directory

**Architecture:**
```cpp
InverterProtocol (abstract base)
    ├── void setup()
    ├── void send_can_messages()        // Write battery state to inverter
    └── void transmit()

CanInverterProtocol (extends InverterProtocol, Transmitter)
    ├── Sends periodic CAN frames based on datalayer state
    ├── Updates inverter with: SOC, power limits, safety status
    └── Auto-registers as Transmitter

Concrete implementations (15+ inverter types):
    ├── PYLON-CAN, PYLON-LV-CAN
    ├── BYD-CAN, BYD-MODBUS
    ├── GROWATT-HV-CAN, GROWATT-LV-CAN
    ├── SMA-BYD-H-CAN, SMA-TRIPOWER-CAN
    ├── SOLAX-CAN
    ├── SUNGROW-CAN
    ├── SOFAR-CAN
    └── ... (8+ more)
```

**Critical Pattern:**
- Runs **periodically** (e.g., every 200ms)
- Reads from datalayer (which was updated by BMS)
- Sends formatted CAN messages to inverter
- **Inverter control decisions** are made here based on battery state

**Example Flow:**
```
BMS Parser (CAN RX)           Inverter Controller (CAN TX)
    │                              │
    ├─ Parse CAN frame             │
    └─ datalayer.battery.          │
       status.voltage_dV = 3850    │
                                   ├─ Read: voltage_dV = 3850
                                   ├─ Read: max_charge_power = 5000W
                                   ├─ Read: bms_status = ACTIVE
                                   └─ Send "OK to charge, limit 5000W"
                                      to inverter via CAN
```

---

#### D. CAN Communication Framework

**Location:** `communication/can/`

**Two-Tier Registration System:**

```
Register Phase (at startup):
  PYLON_BATTERY bms;          // Constructor auto-registers
    → register_can_receiver(this, CAN_BATTERY_INTERFACE, 500kbps)
  SMA_TRIPOWER_CAN inv;        // Constructor auto-registers
    → register_transmitter(this)

Runtime Phase (main loop):
  init_CAN()                   // Initialize all registered interfaces
  receive_can()                // Route RX frames to registered receivers
  transmit_can()               // Call transmit() on all registered transmitters
```

**Receiver Registration:**
```cpp
void register_can_receiver(
    CanReceiver* receiver,           // Object implementing receive_can_frame()
    CAN_Interface interface,         // BATTERY, INVERTER, CHARGER, SHUNT, BATTERY2
    CAN_Speed speed = 500kbps        // 100-1000 kbps
);
```

**Transmitter Registration:**
```cpp
void register_transmitter(Transmitter* transmitter);  // Calls transmit(millis)
```

**Key Property:** This is **non-blocking discovery**—modules self-register by simply instantiating.

---

#### E. Charger System

**Location:** `charger/` directory

**Architecture:**
```cpp
CanCharger (extends CanReceiver)
    ├── Parses charger status from CAN
    ├── Updates: datalayer.charger.status
    └── Updates: datalayer.battery.info.max_charge_power_W

Implementations:
    ├── NISSAN-LEAF-CHARGER
    └── CHEVY-VOLT-CHARGER
```

**Purpose:** Track charger availability and state, inform battery of charging limits.

---

### 1.3 Datalayer Deep Structure

**Total Size:** ~800 bytes (fits in SRAM)

```cpp
// BATTERY INFO (Static/Slowly changing)
struct DATALAYER_BATTERY_INFO_TYPE {
  uint32_t total_capacity_Wh;          // e.g., 30000
  uint16_t max_design_voltage_dV;      // e.g., 5000 (500.0V)
  uint16_t min_design_voltage_dV;      // e.g., 2500 (250.0V)
  uint16_t max_cell_voltage_mV;        // e.g., 4300 (4.300V)
  uint16_t min_cell_voltage_mV;        // e.g., 2700 (2.700V)
  uint8_t number_of_cells;             // e.g., 192
  battery_chemistry_enum chemistry;    // NCA/NMC/LFP/Auto
};

// BATTERY STATUS (Real-time, updated every CAN message)
struct DATALAYER_BATTERY_STATUS_TYPE {
  uint32_t remaining_capacity_Wh;      // Calculated from SOC
  uint32_t max_discharge_power_W;      // From BMS
  uint32_t max_charge_power_W;         // From BMS
  int32_t active_power_W;              // Calculated: voltage × current
  int32_t total_charged_battery_Wh;    // Cumulative
  int32_t total_discharged_battery_Wh; // Cumulative
  
  uint16_t max_discharge_current_dA;   // Calculated from power limit
  uint16_t max_charge_current_dA;      // Calculated from power limit
  uint16_t soh_pptt;                   // State of Health (pptt)
  uint16_t voltage_dV;                 // Instantaneous (dV = 0.1V)
  uint16_t cell_max_voltage_mV;        // Highest cell
  uint16_t cell_min_voltage_mV;        // Lowest cell
  uint16_t real_soc;                   // From BMS (pptt: % × 100)
  uint16_t reported_soc;               // To inverter (pppt, may be scaled)
  uint16_t CAN_error_counter;          // CAN CRC errors
  
  int16_t temperature_max_dC;          // Max pack temp (dC = 0.1°C)
  int16_t temperature_min_dC;          // Min pack temp
  int16_t current_dA;                  // Instantaneous (dA = 0.1A)
  
  uint8_t CAN_battery_still_alive;     // Heartbeat counter (decremented/sec)
  enum bms_status_enum bms_status;     // ACTIVE, INACTIVE, FAULT, etc.
  enum real_bms_status_enum real_bms_status;  // Connection state
  
  uint16_t cell_voltages_mV[MAX_AMOUNT_CELLS];  // Array for all cells
  bool cell_balancing_status[MAX_AMOUNT_CELLS];
};

// BATTERY SETTINGS (User-configurable)
struct DATALAYER_BATTERY_SETTINGS_TYPE {
  bool soc_scaling_active;
  uint8_t soc_min_scaling_percent;
  uint8_t soc_max_scaling_percent;
  // ... 20+ configuration options
};

// Top-level container
struct DATALAYER_BATTERY_TYPE {
  DATALAYER_BATTERY_INFO_TYPE info;
  DATALAYER_BATTERY_STATUS_TYPE status;
  DATALAYER_BATTERY_SETTINGS_TYPE settings;
};

// System also contains: shunt, charger, inverter structures
```

---

### 1.4 Control Flow Example: Pylon Battery

**Sequence when CAN message arrives (0x4210 voltage frame):**

```
1. CANbus driver receives frame (0x4210, 8 bytes)
   └─> receive_can() calls registered receivers

2. PYLON_BATTERY::receive_can_frame() is called
   ├─ Frame ID = 0x4210?
   ├─ Extract: voltage (2 bytes), current (2 bytes), SOC (1 byte), etc.
   ├─ Validate checksum
   └─ Update datalayer fields:
      ├─ datalayer.battery.status.voltage_dV = 3850
      ├─ datalayer.battery.status.current_dA = 125
      ├─ datalayer.battery.status.reported_soc = 6500 (65%)
      └─ datalayer.battery.status.CAN_battery_still_alive = 60

3. (Simultaneously in another FreeRTOS task)
   SMA_TRIPOWER_CAN::transmit() is called periodically
   ├─ Read datalayer.battery.status.voltage_dV
   ├─ Read datalayer.battery.status.max_charge_power_W
   ├─ Read datalayer.battery.status.real_bms_status
   ├─ Format CAN message for SMA inverter
   └─ Send: "Battery 385V, 65% SOC, OK to charge"

4. Receiver (on different ESP32)
   Receives ESP-NOW message with:
   ├─ SOC = 65%
   ├─ Power = -3500W (discharging)
   └─ Display updates battery gauge
```

---

## Part 2: Current 2-Device Architecture

### 2.1 Device Roles

**Transmitter (Olimex ESP32-POE2):**
- Hardware: Ethernet, CAN (MCP2515 via HSPI), MQTT
- Software:
  - Reads CAN bus (Battery Emulator BMS parsers)
  - Updates datalayer (Battery Emulator state)
  - Sends battery data via ESP-NOW to receiver
  - Publishes MQTT updates to broker
  - Manages configuration (NVS)

**Receiver (LilyGo T-Display-S3):**
- Hardware: Display (1.9" TFT), WiFi, ESP-NOW, LittleFS
- Software:
  - Receives battery data via ESP-NOW
  - Displays battery gauge, SOC, power, etc.
  - Web UI for configuration
  - Local settings cache (LittleFS)

### 2.2 Communication Model

**Transmitter → Receiver (Primary Flow):**
```
CAN Bus                ESP-NOW               Display
───────               ──────────            ─────────
Pylon BMS  ──CAN──>  Transmitter  ──ESP-NOW──> Receiver
                       (datalayer)   (payload)   (UI)
```

**Receiver → Transmitter (Secondary Flow):**
```
Web UI / Settings  ──HTTP──>  Transmitter  ──NVS──>  MQTT/CAN
                    (Config)   (persists)    (apply)   (devices)
```

### 2.3 ESP-NOW Payload Structure (Current)

**From transmitter's data_sender.cpp:**
```cpp
tx_data.soc = datalayer.battery.status.reported_soc / 100;  // 0-100
tx_data.power = datalayer.battery.status.active_power_W;    // Watts
tx_data.checksum = calculate_checksum(&tx_data);
```

**EnhancedCache:**
- Transient data (battery readings): FIFO queue, 250 entries
- State data (configuration): Versioned slots
- Non-blocking: 10ms mutex timeout for control code

### 2.4 Integration Point

**Transmitter main loop:**
```cpp
void loop() {
    // 1. Receive CAN messages from battery/inverter
    CAN_Driver.update();  
    → Calls BMS parsers via registered receivers
    → Updates datalayer.battery.status.*
    
    // 2. Every 100ms, transmit battery data
    DataSender.send_test_data_with_led_control()
    → Reads datalayer.battery.status
    → Sends via ESP-NOW
    
    // 3. Inverter control runs periodically
    (Currently implemented in Battery Emulator for original board)
}
```

---

## Part 3: Migration Issues & Blockers

### 3.1 Critical Architectural Mismatch

**Issue #1: Inverter Control Logic Lives on Transmitter, But Needs Real-Time Access**

**Problem:**
- Battery Emulator inverter control code (SMA, Growatt, etc.) runs in real-time
- Makes decisions based on datalayer state: voltage, SOC, power limits, temperature
- **Current System:** Transmitter only sends snapshot data via ESP-NOW (100ms intervals)
- **Receiver:** Cannot make control decisions (no datalayer, no inverter code)

**Impact:**
- Inverter control decisions must remain on transmitter
- **This is actually correct**—transmitter has the real-time data
- But receiver may need to send commands back (e.g., "override limits") via WiFi/MQTT

**Resolution:** Keep inverter control on transmitter. Receiver is data-display device only.

---

**Issue #2: Datalayer Synchronization**

**Problem:**
- Battery Emulator assumes single **global datalayer** on one device
- 2-device system has:
  - Transmitter datalayer (complete, real-time, contains inverter state)
  - Receiver datalayer (needs to sync with transmitter periodically)
- CAN bus only connected to transmitter → only transmitter has fresh battery data

**Impact:**
- Receiver cannot run Battery Emulator's BMS parsers (no CAN bus)
- Receiver cannot run inverter control code (no CAN connection to inverter)
- Receiver gets **subset** of datalayer (battery status + some state)

**Resolution:** 
- **Transmitter:** Full Battery Emulator datalayer (everything)
- **Receiver:** Subset datalayer (battery.status only) for display purposes
- Sync via ESP-NOW every 100-200ms

---

**Issue #3: Registered Receiver/Transmitter Auto-Discovery**

**Problem:**
- Battery Emulator classes auto-register on construction: `new PYLON_BATTERY()`
- Uses global functions: `register_can_receiver()`, `register_transmitter()`
- Works fine on transmitter, but needs clear separation from receiver

**Impact:**
- Transmitter creates all Battery instances, they auto-register ✓ (works)
- Receiver cannot create Battery instances (no CAN bus to register with)
- Need clear initialization order: CAN first, then battery, then inverter

**Resolution:** 
- Transmitter: Explicit initialization sequence in main()
  ```cpp
  init_CAN();                           // Set up CAN interfaces
  PYLON_BATTERY bms;                    // Auto-registers as receiver
  SMA_TRIPOWER_CAN inverter;            // Auto-registers as transmitter
  ```
- Receiver: Don't create any Battery instances (only display)

---

**Issue #4: HAL (Hardware Abstraction Layer) Conflicts**

**Problem:**
- Battery Emulator's `hal.h` (for original single-device Lilygo board):
  - Defines GPIO pins for CAN, relays, contactors
  - Used by ChademoBattery, precharge control, etc.
- **Current approach:** Created `hal_minimal.h` stub, caused GPIO conflicts

**Impact:**
- We need Battery Emulator's **datalayer + BMS parsing logic**
- But **NOT** Battery Emulator's board-specific HAL
- Each device has its own HAL:
  - Transmitter: Olimex ESP32-POE2 (Ethernet + CAN via MCP2515)
  - Receiver: LilyGo T-Display-S3 (TFT display only)

**Resolution:**
- **Delete hal_minimal.h** ✓ (already done)
- **Use transmitter's own HAL** for GPIO control
- BMS parsers that access HAL (ChademoBattery) are disabled on transmitter
- Only use BMS types that don't need GPIO (Pylon, Nissan Leaf, Tesla, BYD, etc.)

---

**Issue #5: Transmitter vs. Receiver Task Scheduling**

**Problem:**
- Battery Emulator's original code runs in Arduino `loop()`
- Our system uses FreeRTOS tasks with priorities:
  - CAN receive task (real-time)
  - Inverter control task (periodic)
  - Data sender task (100ms interval)
  - MQTT task (network I/O)

**Impact:**
- CAN driver update must be in high-priority task
- Inverter control must run with consistent timing (200ms)
- Data sender can batch updates (100ms interval)
- MQTT publishing is low-priority (blocking network I/O)

**Resolution:**
- Create FreeRTOS task structure:
  ```
  Task: CAN_RX (Priority: HIGH)
    - Runs every main loop iteration
    - Calls CAN driver receive_can()
    - Updates datalayer via BMS parsers
  
  Task: INVERTER_CONTROL (Priority: NORMAL)
    - Runs every 200ms
    - Calls transmitter.transmit() for all registered inverter objects
    - Sends CAN frames to real inverter
  
  Task: DATA_SENDER (Priority: NORMAL)
    - Runs every 100ms
    - Reads datalayer.battery.status
    - Caches and sends via ESP-NOW
  
  Task: MQTT_PUBLISH (Priority: LOW)
    - Runs periodically
    - Publishes datalayer state to MQTT broker
  ```

---

**Issue #6: No Contactor/Precharge Control on Receiver**

**Problem:**
- Battery Emulator includes contactors and precharge control
- Receiver has no GPIO for contactors (display-only device)
- Control must remain on transmitter

**Impact:**
- Cannot implement full Battery Emulator control flow on receiver
- Receiver is **display device only**
- All control decisions stay on transmitter

**Resolution:**
- Receiver displays: battery status, SOC, power, temperatures
- Receiver **cannot** trigger contactor closes, precharge, etc.
- All control remains on transmitter (correct design)

---

**Issue #7: Dual Battery Support (battery2)**

**Problem:**
- Battery Emulator supports dual batteries: `datalayer.battery` + `datalayer.battery2`
- Current ESP-NOW payload only sends primary battery
- Receiver only displays primary battery

**Impact:**
- Dual battery systems (e.g., Nissan Leaf + Pylon storage) not fully supported
- Need to extend ESP-NOW payload to include battery2

**Resolution (Phase 2):**
- Extend battery payload to include battery2.status
- Receiver displays both batteries on separate gauge
- For now: Support single battery only

---

### 3.2 Integrating Battery Emulator's Control Logic

**Issue #8: Inverter Control Must Run on Transmitter**

**Problem:**
- Inverter control code (SMA, Growatt, Pylon, etc.) is CanInverterProtocol subclass
- Makes decisions: "Can I charge?", "How much power?", "Any faults?"
- Needs real-time access to datalayer
- Must run on device with CAN connection to real inverter

**Impact:**
- Transmitter must instantiate inverter control object
- Inverter object must be registered with transmitter's CAN driver
- Control loop runs at 200ms intervals

**Example:**
```cpp
// In transmitter setup()
init_CAN();                      // Initialize CAN interfaces

// Register BMS (reads battery state)
PYLON_BATTERY* bms = new PYLON_BATTERY();    
// Auto-registers on CAN_BATTERY_INTERFACE at 500kbps

// Register Inverter (writes control commands)
SMA_TRIPOWER_CAN* inverter = new SMA_TRIPOWER_CAN();
// Auto-registers as transmitter (calls transmit() every loop)

// Initialization complete—CAN loop handles everything
```

**Resolution:** Keep as-is once initialized. No changes to Battery Emulator control code.

---

**Issue #9: Charger Support**

**Problem:**
- Charger classes (NISSAN_LEAF_CHARGER) parse charger status
- Updates `datalayer.charger.status` with charger limits
- Also affects `datalayer.battery.info.max_charge_power_W`

**Impact:**
- If charger is connected to CAN bus, transmitter must parse it
- Charger state affects battery charge power limits
- Receiver needs to display "Charger connected, 3000W available"

**Resolution:**
- Charger classes already supported—just instantiate if charger on CAN
- Example: `new NISSAN_LEAF_CHARGER();` auto-registers
- Updates datalayer like any other component

---

**Issue #10: System Settings (hardware configuration)**

**Problem:**
- `system_settings.h` defines constants: crystal frequency, CAN speeds, GPIO pins
- Transmitter uses different settings than Battery Emulator original

**Impact:**
- Must define transmitter-specific settings:
  - CAN speed: 500kbps (Pylon uses this)
  - Crystal: 8MHz (for MCP2515)
  - GPIO pins: Olimex ESP32-POE2 specific
- Receiver needs NO settings (display only)

**Resolution:**
- Create `src/system_settings.h` in transmitter project
- Define constants for Olimex + MCP2515 configuration
- Copy from Battery Emulator's version, adapt for hardware

---

## Part 4: Step-by-Step Migration Plan

### Phase 1: Foundation (Current State)

**Status: PARTIALLY COMPLETE**

**✅ Already Done:**
- Copied Battery Emulator datalayer to `src/datalayer/`
- Fixed enum conflicts (BMS_FAULT, bms_status_enum naming)
- Created battery_manager.h/cpp factory
- Updated main.cpp to read datalayer.battery.status

**⏳ In Progress:**
- Resolve GPIO conflicts in hal.h
- Build and verify compilation
- Test with real Pylon battery CAN messages

---

### Phase 2: Core Battery Emulator Integration (Week 1)

**Objective:** Get first BMS parser (PYLON) working with real data

**Step 1: Transmitter CAN Driver Integration**

```cpp
// src/communication/can/can_driver.cpp
void CANDriver::update() {
  // Receive all pending CAN frames
  while (mcp2515.readMessage(&rxFrame) == MCP2515::ERROR_OK) {
    // Create Battery Emulator CAN_frame struct
    CAN_frame battery_frame = {
      .FD = false,
      .ext_ID = (rxFrame.can_id > 0x7FF),
      .DLC = rxFrame.can_dlc,
      .ID = rxFrame.can_id
    };
    
    // Copy data
    for (uint8_t i = 0; i < rxFrame.can_dlc; i++) {
      battery_frame.data.u8[i] = rxFrame.data[i];
    }
    
    // Route to registered receivers (BMS parsers)
    BatteryManager::instance().process_can_message(
      rxFrame.can_id, 
      rxFrame.data, 
      rxFrame.can_dlc
    );
  }
}
```

**Step 2: Battery Manager Initialization**

```cpp
// In transmitter main.cpp setup()
void setup() {
  // ... other initialization ...
  
  // Phase 4: Battery Emulator
  LOG_INFO("BMS", "Initializing Battery Emulator...");
  
  // Must initialize CAN FIRST
  if (!CAN_Driver.init()) {
    LOG_ERROR("CAN", "CAN driver failed");
    return;
  }
  
  // Then initialize Battery Manager (BMS will auto-register)
  if (!BatteryManager::instance().init(BatteryType::PYLON_BATTERY)) {
    LOG_ERROR("BMS", "Battery initialization failed");
    return;
  }
  
  datalayer.initialized = true;
}
```

**Step 3: Verify Compilation**

```bash
cd espnowtransmitter2
platformio run  # Should complete without errors
```

**Expected Output:**
```
[CAN] CAN driver initialized (500kbps)
[BMS] Initializing Battery Emulator...
[BMS] ✓ Battery Emulator initialized (Pylon)
[DATALAYER] Datalayer ready
```

---

### Phase 2 Continued: Add Inverter Control

**Step 4: Inverter Integration (if inverter on CAN bus)**

```cpp
// In transmitter main.cpp setup(), after Battery initialization

#include "lib/battery_emulator_src/inverter/INVERTERS.h"

void setup() {
  // ... CAN + Battery initialization ...
  
  // Initialize inverter if connected
  if (INVERTER_TYPE == INVERTER_SMA_TRIPOWER) {
    inverter = new SMA_TRIPOWER_CAN();
    // Auto-registers as transmitter
    LOG_INFO("INVERTER", "SMA Tripower registered");
  }
}
```

**Transmitter Main Loop:**

```cpp
void loop() {
  // 1. Handle CAN receive (highest priority)
  CAN_Driver.update();
  
  // 2. Periodically send datalayer to receiver (every 100ms)
  if (millis() - last_data_send > 100) {
    DataSender::instance().send_battery_data();
    last_data_send = millis();
  }
  
  // 3. Periodically update registered transmitters (inverters)
  if (millis() - last_inverter_update > 200) {
    BatteryManager::instance().update_transmitters(millis());
    last_inverter_update = millis();
  }
}
```

---

### Phase 3: Receiver Side (Week 2)

**Objective:** Receiver displays real Battery Emulator data from transmitter

**Step 1: Extend Datalayer Sync**

**Current:** Receiver only knows SOC + Power  
**Target:** Receiver syncs complete battery.status

```cpp
// Transmitter sends (every 100ms):
struct SyncedDatalayer {
  uint16_t voltage_dV;           // 3850 = 385.0V
  int16_t current_dA;            // 125 = 12.5A
  int32_t active_power_W;        // -3500
  uint16_t reported_soc;         // 6500 = 65%
  int16_t temperature_max_dC;    // 250 = 25.0°C
  int16_t temperature_min_dC;    // 240 = 24.0°C
  uint32_t max_charge_power_W;   // 5000
  uint32_t max_discharge_power_W;// 8000
  uint8_t bms_status;            // ACTIVE/STANDBY/FAULT
  uint16_t cell_count;           // 192 cells
};
```

**Step 2: Receiver Display Update**

```cpp
// In receiver's display code
void display_battery_stats() {
  // From synced datalayer
  display.printf("Voltage: %.1f V\n", synced_data.voltage_dV / 10.0);
  display.printf("Current: %.1f A\n", synced_data.current_dA / 10.0);
  display.printf("Power: %d W\n", synced_data.active_power_W);
  display.printf("SOC: %d%%\n", synced_data.reported_soc / 100);
  display.printf("Temp: %.1f°C\n", synced_data.temperature_max_dC / 10.0);
  display.printf("Status: %s\n", 
    synced_data.bms_status == BMS_ACTIVE ? "OK" : "FAULT");
}
```

---

### Phase 4: Advanced Features (Week 3-4)

**A. Add Second BMS Support**

```cpp
// In transmitter main.cpp, make BMS type configurable
BatteryType selected_bms = (BatteryType)NVS.getInt("bms_type", PYLON_BATTERY);
BatteryManager::instance().init(selected_bms);
```

**B. Dual Battery Support**

```cpp
// Extend ESP-NOW payload to include battery2
struct ExtendedBatteryPayload {
  BatteryStatus battery1;   // Primary
  BatteryStatus battery2;   // Secondary (optional)
  bool has_battery2;        // Flag
};
```

**C. MQTT Publishing**

```cpp
// In transmitter, publish datalayer to MQTT
void mqtt_publish_battery() {
  char topic[64];
  sprintf(topic, "battery/voltage_dv");
  mqtt.publish(topic, datalayer.battery.status.voltage_dV);
  
  sprintf(topic, "battery/soc");
  mqtt.publish(topic, datalayer.battery.status.reported_soc / 100);
  
  // ... more fields ...
}
```

**D. Cell Voltage Monitoring**

```cpp
// Send individual cell voltages periodically (if space allows)
// Or on-demand via MQTT query
struct CellVoltageRequest {
  uint8_t start_index;  // Cell 0-191
  uint8_t count;        // How many cells
};

// Receiver queries: "Give me cells 0-47"
// Transmitter responds with array of voltages
```

---

## Part 5: Implementation Checklist

### Pre-Build Validation

- [ ] Remove all `hal_minimal.h` references ✓
- [ ] Rename Battery Emulator enums (`bms_status_enum` → `BatteryEmulator_bms_status_enum`) ✓
- [ ] Create system_settings.h for transmitter
- [ ] Verify include paths are correct
- [ ] Check for missing header files

### Phase 1: Build & Compile

- [ ] Transmitter compiles without errors
- [ ] Transmitter compiles without warnings
- [ ] Receiver compiles without errors
- [ ] No linking errors for Battery Emulator libraries

### Phase 2: Hardware Testing

- [ ] Transmitter CAN driver receives test frames
- [ ] Pylon BMS parser updates datalayer fields
- [ ] Datalayer voltage/current/SOC values are correct
- [ ] CAN error counter remains at 0 (no CRC errors)

### Phase 3: ESP-NOW Integration

- [ ] Transmitter sends battery data to receiver every 100ms
- [ ] Receiver receives and displays battery stats
- [ ] Receiver display updates smoothly (no jitter)
- [ ] Low power consumption (< 100mA transmitter, < 200mA receiver)

### Phase 4: Advanced Features

- [ ] Support for Nissan Leaf, Tesla, BYD BMS types
- [ ] Inverter control loop runs at 200ms intervals
- [ ] MQTT publishing works
- [ ] Cell voltage monitoring functional

---

## Part 6: File Structure & Organization

### Transmitter (`ESPnowtransmitter2/espnowtransmitter2/`)

```
src/
├── main.cpp                           # Main loop, FreeRTOS task creation
├── config/
│   ├── hardware_config.h              # Olimex + MCP2515 pins
│   ├── network_config.h               # Ethernet + MQTT settings
│   └── system_settings.h              # NEW: BE settings (CAN speed, etc.)
├── datalayer/
│   ├── datalayer.h                    # (from Battery Emulator, copied)
│   ├── datalayer.cpp                  # (from Battery Emulator, copied)
│   └── datalayer_extended.h           # (from Battery Emulator, copied)
├── communication/
│   └── can/
│       ├── can_driver.h               # MCP2515 driver
│       └── can_driver.cpp             # CAN receive/transmit logic
├── battery/
│   ├── battery_manager.h              # NEW: Factory for Battery instances
│   └── battery_manager.cpp            # NEW: Routes CAN to BMS parsers
├── espnow/
│   ├── data_sender.cpp                # Reads datalayer, sends via ESP-NOW
│   └── enhanced_cache.cpp             # Cache management
├── network/
│   ├── mqtt_manager.cpp               # MQTT publishing
│   └── ethernet_manager.cpp           # Ethernet setup
└── lib/
    └── battery_emulator_src/          # (from Battery Emulator 9.2.4)
        ├── battery/
        │   ├── Battery.h/cpp
        │   ├── BATTERIES.h/cpp
        │   ├── PYLON-BATTERY.h/cpp
        │   ├── NISSAN-LEAF-BATTERY.h/cpp
        │   ├── ... (50+ BMS types)
        ├── inverter/
        │   ├── InverterProtocol.h
        │   ├── SMA-TRIPOWER-CAN.h/cpp
        │   ├── PYLON-CAN.h/cpp
        │   ├── ... (15+ inverter types)
        ├── datalayer/
        │   ├── datalayer.h
        │   └── datalayer.cpp
        ├── communication/
        │   └── can/
        │       ├── CanReceiver.h
        │       ├── comm_can.h/cpp
        │       ├── CommunicationManager.h  # NEW: Manages registration
        │       └── Transmitter.h
        └── devboard/
            └── utils/
                └── types.h             # (with renamed enums)
```

### Receiver (`espnowreciever_2/`)

```
src/
├── main.cpp                           # Main loop, WiFi + ESP-NOW setup
├── datalayer/
│   ├── datalayer_mirror.h             # Subset of transmitter datalayer
│   └── datalayer_mirror.cpp           # Sync logic from ESP-NOW messages
├── espnow/
│   ├── espnow_callbacks.cpp           # Receive battery data
│   └── rx_sync_manager.cpp            # NEW: Manages datalayer sync
├── display/
│   ├── display_core.cpp               # Draw battery gauge, stats
│   ├── display_led.cpp                # LED control
│   └── display_splash.cpp             # Startup screen
└── lib/
    └── webserver/                     # Configuration web UI
```

---

## Part 7: Known Limitations & Workarounds

### Limitation 1: HAL-Dependent BMS Types

**Affected:** ChademoBattery (uses relay GPIO), some others requiring GPIO control

**Workaround:** Don't instantiate these BMS types. Use BMS types that are CAN-only:
- PYLON_BATTERY ✓
- NISSAN_LEAF_BATTERY ✓
- TESLA_MODEL_3_MODEL_Y ✓
- BydAtto3Battery ✓
- BmwI3Battery ✓
- KiaHyundaiHybridBattery ✓
- ... (30+ others)

**ChademoBattery:** Can be enabled IF we implement the GPIO control layer in transmitter's HAL

---

### Limitation 2: Synchronous Datalayer Access

**Current:** Battery Emulator code accesses datalayer directly without locking

```cpp
datalayer.battery.status.voltage_dV = 3850;  // No mutex
```

**In FreeRTOS multi-task system:** Could have race conditions
- Task A (CAN RX): Updates voltage_dV
- Task B (Data Sender): Reads voltage_dV
- Race: Task B might read partial write

**Workaround:** 
- Current: Accept the race condition (very unlikely given timing)
- Proper: Add FreeRTOS mutex around datalayer access
- For now: Monitor for data corruption in logs

---

### Limitation 3: No Real Contactor Control from Receiver

**Current:** Only transmitter can control contactors (has GPIO)

**Receiver Limitation:** Cannot directly close contactors

**Workaround:** 
- Receiver sends commands via WiFi/MQTT
- Transmitter receives command and executes

Example:
```
User clicks "Close Contactor" on receiver → 
HTTP request to transmitter: /api/contactor/close →
Transmitter GPIO drives relay closed →
Response back to receiver
```

---

### Limitation 4: Latency on Receiver Display

**Current:** 100ms between CAN message and receiver display update

**Receiver sees:**
- CAN message received on transmitter (0ms)
- Datalayer updated (0-1ms)
- ESP-NOW sent (1-2ms)
- Receiver displays (100-110ms total)

**Impact:** ~100ms latency is acceptable for battery monitoring (not safety-critical)

**Note:** Inverter control happens on transmitter in real-time (< 1ms)

---

## Part 8: Testing Strategy

### Unit Tests (Per Component)

**A. CAN Driver Test**
```cpp
TEST(CANDriver, ReceivesFrame_UpdatesDatalayer) {
  CAN_frame test_frame = {
    .ID = 0x4210,
    .DLC = 8,
    .data.u8 = {0x0F, 0x0E, 0x00, 0x00, 0x41, 0, 0, 0}
  };
  
  BMS_Parser.receive_can_frame(&test_frame);
  
  ASSERT_EQ(datalayer.battery.status.voltage_dV, 3850);  // 385.0V
  ASSERT_EQ(datalayer.battery.status.real_soc, 65);      // 65%
}
```

**B. Battery Manager Test**
```cpp
TEST(BatteryManager, InitializesPylon) {
  BatteryManager mgr;
  bool result = mgr.init(BatteryType::PYLON_BATTERY);
  
  ASSERT_TRUE(result);
  ASSERT_TRUE(mgr.is_initialized());
  ASSERT_EQ(mgr.get_battery_type(), BatteryType::PYLON_BATTERY);
}
```

### Integration Tests (Full Flow)

**A. CAN → Datalayer → ESP-NOW**

1. Send CAN frame (0x4210 voltage)
2. Verify datalayer.battery.status.voltage_dV updated
3. Verify DataSender reads correct value
4. Verify ESP-NOW payload contains correct value
5. Verify receiver receives and displays

### Hardware Tests (With Real Equipment)

**A. Real Pylon Battery**
- Connect Pylon BMS to transmitter CAN bus
- Monitor datalayer updates in logs
- Verify all fields populate correctly
- Check accuracy vs. BMS display

**B. Real Inverter (SMA/Growatt)**
- Connect inverter to transmitter CAN bus
- Verify control commands sent from transmitter
- Monitor inverter display for state changes

---

## Part 9: Risk Mitigation

### Risk 1: Data Corruption (Race Conditions)

**Probability:** Low  
**Impact:** High (incorrect power decisions)

**Mitigation:**
- Add detailed logging of datalayer access
- Implement checksum verification
- Add watchdog for heartbeat

### Risk 2: CAN Bus Errors

**Probability:** Medium  
**Impact:** Medium (battery data becomes stale)

**Mitigation:**
- Track CAN error counter
- Display "BMS offline" if no messages for 3 seconds
- Don't allow charging/discharging when BMS offline

### Risk 3: ESP-NOW Dropout

**Probability:** Low (within home WiFi range)  
**Impact:** Medium (receiver shows stale data)

**Mitigation:**
- Cache last valid reading on receiver
- Show "last updated: Xs ago"
- Alert user if data > 2 seconds old

### Risk 4: Inverter Control Failure

**Probability:** Low  
**Impact:** High (system malfunction)

**Mitigation:**
- Implement failsafe: stop charging/discharging if inverter offline
- Add redundant CAN interface (can switch between two inverters)
- Log all control commands

---

## Part 10: Success Criteria

### Build Success
- [ ] Transmitter compiles without errors or warnings
- [ ] Receiver compiles without errors or warnings
- [ ] No linking errors
- [ ] Binary size reasonable (< 1.5MB for transmitter)

### Functional Success
- [ ] Transmitter receives real Pylon CAN messages
- [ ] Datalayer fields update correctly
- [ ] Receiver displays battery voltage, current, SOC, temp
- [ ] Data latency < 200ms
- [ ] No data corruption or checksum errors
- [ ] System runs for 24 hours without reset

### Control Success (Phase 2)
- [ ] Inverter receives control commands from transmitter
- [ ] Inverter respects power limits from datalayer
- [ ] Precharge sequence completes successfully
- [ ] Contactor closes when conditions are met

---

## Conclusion

This migration strategy keeps Battery Emulator's proven architecture intact while integrating it into a 2-device system:

1. **Transmitter:** Runs complete Battery Emulator (BMS parsers, datalayer, inverter control)
2. **Receiver:** Displays battery status from transmitter, provides UI for configuration
3. **Communication:** ESP-NOW carries real-time battery snapshot every 100ms
4. **Control:** Decisions made on transmitter (has real CAN data)
5. **Safety:** Failsafes protect system if communication lost

The key insight: **Keep the complex parts (Battery Emulator logic) where the data is (transmitter with CAN bus).** The receiver is a display device that shows snapshots of transmitter state.

---

**Next Steps:**
1. Verify compilation (Phase 1)
2. Test with real Pylon battery (Phase 2)  
3. Add inverter control (Phase 2)
4. Extend receiver display (Phase 3)
5. Add second BMS support (Phase 4)

