# Battery Emulator Integration Architecture & Migration Plan
**Document Version:** 2.0 (Corrected)  
**Date:** February 17, 2026  
**Status:** Comprehensive Analysis with Hardware Specifications  
**Scope:** Migrating complete Battery Emulator framework to 2-device ESP32 architecture

---

## Executive Summary

This document provides a complete architectural analysis of Battery Emulator 9.2.4's core framework and defines how to integrate it into a purpose-designed 2-device architecture where **all control logic and heavy lifting occurs on the transmitter, and the receiver handles only UI and display**.

**Key Architecture Principle:** 
- **Transmitter (Control Device):** Olimex ESP32-POE2 + Waveshare RS485 CAN HAT (B) = Full Battery Emulator engine
- **Receiver (UI Device):** LilyGo T-Display-S3 = Display only, zero control logic
- **Migration Strategy:** Use Battery Emulator's existing proven code as-is. Only add thin integration layer for 2-device communication.
- **File Layout Rule (Mandatory):** Keep Battery Emulator's logic/control unchanged, but adapt includes and library structure to the new transmitter/receiver folder layout. This may require moving/adjusting includes and library metadata. See [PREFERENCES_FIX.md](PREFERENCES_FIX.md) for the canonical example of resolving layout-induced dependency issues without modifying core logic.

**Critical Success Factor:** Eliminate code duplication. Battery Emulator's datalayer, BMS parsers, inverter control are battle-tested—reuse them directly. Create minimal adapter layer to bridge device boundary.

---

## Part 1: Hardware Architecture

### 1.1 Transmitter System (Control Device)

**Primary Processor:** Olimex ESP32-POE2 (WROVER variant)
- **MCU:** ESP32 (dual-core, 240MHz)
- **RAM:** 520KB
- **Flash:** 4MB
- **Connectivity:** Ethernet (integrated W5500)
- **Pins Available:** GPIO 0-39 (some hardwired to Ethernet)

**Networking Interface:**
- Built-in Ethernet via RMII (hardwired GPIO)
- No WiFi needed (Ethernet is primary network)
- Used for: MQTT publishing, OTA updates, remote monitoring

**CAN Interface:** Waveshare RS485 CAN HAT (B)
- **Protocol:** CAN 2.0B (500kbps typical for Pylon)
- **Connection:** SPI bus to Olimex via HSPI pins
  - **SCK:** GPIO 14
  - **MOSI:** GPIO 13
  - **MISO:** GPIO 12
  - **CS:** GPIO 15
  - **INT:** GPIO 32
- **Power:** 5V from Olimex GPIO supply
- **Connector:** 4-pin CAN connector (can wire to Pylon/Inverter)

**Hardware Characteristics:**
- Designed for **stationary operation** (mounted with battery/inverter)
- Direct CAN bus access to all equipment
- Ethernet connection to home network
- High processing power (full Battery Emulator runs here)
- Can control relays/contactors via GPIO (future: safety circuits)

---

### 1.2 Receiver System (Display Device)

**Primary Processor:** LilyGo T-Display-S3
- **MCU:** ESP32-S3 (dual-core, 240MHz)
- **RAM:** 8MB PSRAM
- **Flash:** 16MB
- **Display:** 1.9" TFT LCD (320×170 pixels)
- **Connectivity:** WiFi (802.11b/g/n)
- **Battery:** 1000mAh Li-Po (can run headless with USB power)

**Networking Interface:**
- WiFi only (no Ethernet)
- ESP-NOW for lightweight battery data sync with transmitter
- Can also connect to home WiFi for web UI

**Hardware Characteristics:**
- Designed for **portable/display operation** (can hold in hand)
- No CAN bus access (no hardware for it)
- No Ethernet jack
- **Zero control logic** (display only)
- Can be powered by internal battery or USB
- Touch UI for configuration (future feature)

---

### 1.3 Network Topology

```
┌─────────────────────────────────────────────────────────┐
│                   BATTERY SYSTEM                        │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  Pylon Battery      SMA Inverter      Charger          │
│  (CAN 0x4210)       (CAN 0x2000)      (CAN 0x1810)     │
│       │                   │                 │           │
│       └───────────────────┴─────────────────┘           │
│                     │                                    │
│            ┌────────▼─────────┐                         │
│            │  CAN Bus (500k)  │                         │
│            └────────┬─────────┘                         │
│                     │                                    │
│  ╔══════════════════╩══════════════════╗               │
│  ║                                      ║               │
│  ║        TRANSMITTER                  ║               │
│  ║    (Olimex + Waveshare)             ║               │
│  ║                                      ║               │
│  ║  ┌──────────────────────────────┐  ║               │
│  ║  │ Battery Emulator Engine      │  ║               │
│  ║  │ • Datalayer (state machine)  │  ║               │
│  ║  │ • 50+ BMS Parsers            │  ║               │
│  ║  │ • 15+ Inverter Control       │  ║               │
│  ║  │ • Real-time decisions        │  ║               │
│  ║  └──────────────────────────────┘  ║               │
│  ║           │         │               ║               │
│  ║      ┌────▼─┐   ┌──▼────┐         ║               │
│  ║      │ CAN  │   │Ethernet       ║               │
│  ║      │  HAT │   │Manager       ║               │
│  ║      └──────┘   └────────┘      ║               │
│  ╚═════════════════╤══════════════════╝               │
│                    │                                    │
│           ┌────────┼────────┐                          │
│           │        │        │                          │
│      MQTT │        │ ESP-NOW                          │
│      Broker        │    (UDP)                          │
│           │        │        │                          │
│           │   ┌────▼────────▼──────┐                  │
│           │   │   RECEIVER         │                  │
│           │   │ (LilyGo Display)   │                  │
│           │   │                    │                  │
│           │   │ • Battery Display  │                  │
│           │   │ • Web UI Config    │                  │
│           │   │ • Touch Interface  │                  │
│           │   │ • Zero Control     │                  │
│           │   └────────────────────┘                  │
│           │            │                               │
│           └─ MQTT Pub──┘                              │
│                        │                               │
│                   ┌────▼─────┐                        │
│                   │  MQTT    │                        │
│                   │  Broker  │                        │
│                   │ (Home    │                        │
│                   │  Assist) │                        │
│                   └──────────┘                        │
│                                                       │
└─────────────────────────────────────────────────────┘
```

---

## Part 2: Battery Emulator Architecture (As-Is)

**CRITICAL:** Battery Emulator code is NOT modified. Used as-is.

### 2.1 Core Component Hierarchy

```
BATTERY EMULATOR (On Transmitter Only)
    │
    ├─ Datalayer (global state)
    │   └─ battery, battery2, shunt, charger, system
    │
    ├─ Battery Parsers (50+ types)
    │   ├─ PYLON_BATTERY
    │   ├─ NISSAN_LEAF_BATTERY
    │   ├─ TESLA_MODEL_3_MODEL_Y
    │   └─ ... (47 more)
    │
    ├─ Inverter Control (15+ types)
    │   ├─ SMA_TRIPOWER_CAN
    │   ├─ PYLON_CAN
    │   └─ ... (13 more)
    │
    └─ CAN Communication Framework
        ├─ register_can_receiver() [auto-discovery]
        ├─ register_transmitter() [auto-discovery]
        └─ receive_can() / transmit_can() [main loop]
```

### 2.2 Key Design Pattern: Auto-Registration

Battery Emulator uses **constructor-based registration** (not factory pattern):

```cpp
// In transmitter setup()
PYLON_BATTERY bms;              // Constructor auto-registers as receiver
SMA_TRIPOWER_CAN inverter;      // Constructor auto-registers as transmitter

// In main loop
receive_can();                   // Routes frames to all registered receivers
// (BMS parser updates datalayer)

transmit_can();                  // Calls transmit() on all registered transmitters
// (Inverter sends control frames)
```

**No factory needed.** The global registration lists are maintained by Battery Emulator's `comm_can.h`.

### 2.3 Datalayer Structure (Unchanged from Battery Emulator)

```cpp
class DataLayer {
  DATALAYER_BATTERY_TYPE battery;      // Primary (always present)
  DATALAYER_BATTERY_TYPE battery2;     // Secondary (optional)
  DATALAYER_SHUNT_TYPE shunt;          // Power measurements
  DATALAYER_CHARGER_TYPE charger;      // Charger status
  DATALAYER_SYSTEM_TYPE system;        // System state (contactors, etc.)
};

extern DataLayer datalayer;           // Global singleton
```

**Size:** ~800 bytes (fits in SRAM)

**Update Frequency:** Real-time (every CAN message, typically 10-100Hz)

---

## Part 3: 2-Device Architecture

### 3.1 Device Responsibilities (Clear Separation)

**TRANSMITTER (Olimex ESP32-POE2 + Waveshare CAN HAT):**

**Control Layer:**
- ✅ Read CAN bus (Pylon battery, SMA inverter, charger)
- ✅ Maintain datalayer (Battery Emulator state machine)
- ✅ Run all 50+ BMS parsers (parse real CAN data)
- ✅ Run all 15+ inverter controllers (send control commands)
- ✅ Make real-time safety decisions (voltage limits, current limits, temperature)
- ✅ Manage contactors and precharge (GPIO control)

**Communication Layer:**
- ✅ Send battery snapshot via ESP-NOW every 100ms to receiver
- ✅ Publish datalayer to MQTT broker every 5 seconds
- ✅ Receive configuration changes from receiver (via MQTT)
- ✅ Apply configuration to datalayer and hardware

**Data Model:**
- Full datalayer (everything)
- Real-time state (< 1ms latency from CAN)
- Single source of truth

---

**RECEIVER (LilyGo T-Display-S3):**

**Display Layer:**
- ✅ Display battery gauge (SOC, voltage, power)
- ✅ Display system status (BMS connected, inverter ready)
- ✅ Show temperature range and cell count
- ✅ Configurable display modes (compact, detailed, graphs)
- ✅ Touch UI for settings (future)

**Configuration Layer:**
- ✅ Web UI for settings (NVS persistence on transmitter)
- ✅ Receive configuration via MQTT from transmitter
- ✅ Send configuration changes to transmitter (MQTT)
- ✅ Local cache of receiver-specific settings (LittleFS)

**Data Model:**
- Snapshot datalayer (battery.status only, synced every 100ms)
- **NO datalayer.battery.info** (static, rarely changes)
- **NO datalayer.inverter** (control only, receiver can't control)
- **NO datalayer.system.contactors** (receiver has no GPIO)

**What It Cannot Do (By Design):**
- ❌ Parse CAN messages (no CAN hardware)
- ❌ Make control decisions (no real-time data)
- ❌ Trigger contactors (no GPIO)
- ❌ Manage charging (inverter control on transmitter)
- ❌ Run Battery Emulator code (not needed)

---

### 3.2 Communication Protocol

**Transmitter → Receiver (100ms interval, ESP-NOW):**
```cpp
struct BatterySnapshot {
  uint16_t voltage_dV;              // 3850 = 385.0V
  int16_t current_dA;               // 125 = 12.5A (positive = charge)
  int32_t active_power_W;           // -3500W = discharging
  uint16_t reported_soc;            // 6500 = 65%
  int16_t temperature_max_dC;       // 250 = 25.0°C
  int16_t temperature_min_dC;       // 240 = 24.0°C
  uint32_t max_charge_power_W;      // Limit
  uint32_t max_discharge_power_W;   // Limit
  uint8_t bms_status;               // Enum: ACTIVE/STANDBY/FAULT
  uint16_t cell_count;              // 192
  uint16_t crc;                     // Validation
};
```

**Receiver → Transmitter (On-demand, MQTT):**
```cpp
struct ConfigCommand {
  enum Type { SET_BMS_TYPE, SET_INVERTER_TYPE, SET_LIMITS, APPLY_SCALE };
  Type command;
  uint32_t value1, value2;
  uint16_t crc;
};
```

---

### 3.3 Task Distribution

**TRANSMITTER - FreeRTOS Tasks:**

1. **CAN_RX_TASK (Priority: HIGH, 10ms)**
   - Calls: `receive_can()`
   - Effect: BMS parsers update datalayer
   - Battery Emulator code runs here (auto-registered receivers)

2. **INVERTER_CONTROL_TASK (Priority: NORMAL, 200ms)**
   - Calls: `transmit_can()`
   - Effect: Inverter controllers send CAN frames
   - Battery Emulator code runs here (auto-registered transmitters)

3. **DATA_SYNC_TASK (Priority: NORMAL, 100ms)**
   - Reads: datalayer.battery.status
   - Action: Sends ESP-NOW snapshot to receiver
   - Minimal processing (just memcpy)

4. **MQTT_PUBLISH_TASK (Priority: LOW, 5000ms)**
   - Reads: datalayer (everything)
   - Action: Publish to MQTT broker
   - Non-blocking (queue-based)

**RECEIVER - FreeRTOS Tasks:**

1. **ESPNOW_RX_TASK (Priority: HIGH, event-driven)**
   - Receives: BatterySnapshot from transmitter
   - Action: Update local mirror datalayer
   - Trigger display refresh

2. **DISPLAY_TASK (Priority: NORMAL, 500ms)**
   - Reads: Local mirror datalayer
   - Action: Redraw TFT screen
   - No Battery Emulator code

3. **WEB_UI_TASK (Priority: NORMAL, 1000ms)**
   - HTTP server for configuration
   - Action: Serve web pages
   - No Battery Emulator code

4. **CONFIG_SYNC_TASK (Priority: LOW, 10000ms)**
   - Action: Publish local settings to transmitter
   - MQTT-based

---

## Part 4: HAL Architecture

### 4.1 Hardware Abstraction Strategy

**Question Addressed:** How to differentiate between control board and UI board in HAL?

**Decision:** Two separate HAL implementations, selected at compile-time.

#### A. Transmitter HAL (`src/hal/transmitter_hal.h`)

**Purpose:** Abstract Olimex + Waveshare hardware

**Components:**
```cpp
namespace HAL {
  namespace CAN {
    bool init(uint32_t speed_kbps);    // Init Waveshare MCP2515
    bool send_frame(CAN_frame* frame);
    bool read_frame(CAN_frame* frame);
    // MCP2515 on HSPI: GPIO 12/13/14/15
  }
  
  namespace GPIO {
    bool set_output(gpio_num_t pin, bool level);  // Future: relays, contactors
    bool read_input(gpio_num_t pin);
    // For: contactor control, precharge, status LEDs
  }
  
  namespace Ethernet {
    bool init();                       // W5500 on Olimex
    bool is_connected();
    IPAddress get_ip();
  }
  
  namespace NVS {
    bool read(const char* key, void* value, size_t size);
    bool write(const char* key, const void* value, size_t size);
  }
}
```

**Pin Mapping (Transmitter):**
- **Ethernet (hardwired):** GPIO 0/18/23/21/26/27/28/29/30
- **CAN HSPI:** GPIO 12/13/14/15/32
- **Future GPIO (for relays):** GPIO 2/4/5/25/26/27/33/34/35/36/37/38/39

**Key Properties:**
- Low-level hardware access only
- No control logic
- Minimal abstraction (thin wrapper over drivers)

---

#### B. Receiver HAL (`src/hal/receiver_hal.h`)

**Purpose:** Abstract LilyGo T-Display-S3 hardware

**Components:**
```cpp
namespace HAL {
  namespace Display {
    bool init();                       // Init TFT_eSPI
    void draw_rectangle(int x, int y, int w, int h, uint32_t color);
    void draw_text(int x, int y, const char* text, uint32_t color);
    void refresh();
  }
  
  namespace Touch {
    bool init();                       // Capacitive touch (future)
    bool get_touch_point(int& x, int& y, bool& pressed);
  }
  
  namespace WiFi {
    bool init(const char* ssid, const char* password);
    bool is_connected();
    IPAddress get_ip();
  }
  
  namespace LittleFS {
    bool init();
    bool read(const char* path, void* buffer, size_t size);
    bool write(const char* path, const void* buffer, size_t size);
  }
  
  namespace Battery {
    uint8_t get_level();              // Internal Li-Po battery
    bool is_charging();
  }
}
```

**Pin Mapping (Receiver):**
- **Display:** SPI (pins hardwired in TFT_eSPI)
- **Touch:** I2C (GPIO 3/4)
- **WiFi:** Internal (ESP32-S3)
- **Battery:** ADC on internal GPIO

**Key Properties:**
- Display-specific functions
- No CAN, no Ethernet
- Zero control logic

---

### 4.2 HAL Selection Pattern

**At Compile-Time:**

```cpp
// platformio.ini
[env:transmitter]
build_flags = -DHAVE_TRANSMITTER_HAL

[env:receiver]
build_flags = -DHAVE_RECEIVER_HAL
```

**In Code:**
```cpp
#ifdef HAVE_TRANSMITTER_HAL
  #include "hal/transmitter_hal.h"
  using HAL = TransmitterHAL;
#endif

#ifdef HAVE_RECEIVER_HAL
  #include "hal/receiver_hal.h"
  using HAL = ReceiverHAL;
#endif
```

**Usage (Device-Agnostic Application Code):**
```cpp
// This code works on both devices (uses appropriate HAL at compile-time)
if (HAL::CAN::init(500)) {
  // Transmitter: Initializes MCP2515
  // Receiver: Compiles to empty function (CAN not available)
}

if (HAL::Display::draw_text(10, 10, "Battery OK", 0xFFFF)) {
  // Receiver: Draws on TFT
  // Transmitter: Compiles to empty function (display not available)
}
```

---

### 4.3 HAL File Structure

```
src/
├── hal/
│   ├── hal_common.h              # Shared definitions
│   ├── transmitter_hal.h         # Transmitter-specific
│   ├── transmitter_hal.cpp
│   ├── receiver_hal.h            # Receiver-specific
│   ├── receiver_hal.cpp
│   └── hal_gpio_defs.h           # GPIO pin definitions
```

**Note:** Battery Emulator's `devboard/hal/hal.h` is NOT used (it's for original single board). Our HAL replaces it.

---

## Part 5: Migration Path (What to Keep, What to Remove)

### 5.1 Battery Emulator Code - KEEP AS-IS

**These directories are copied unchanged from Battery Emulator 9.2.4:**

```
lib/battery_emulator_src/
├── battery/                       ✅ KEEP
│   ├── Battery.h/cpp
│   ├── CanBattery.h/cpp
│   ├── BATTERIES.h/cpp
│   ├── PYLON-BATTERY.h/cpp        (50+ BMS types)
│   └── ... (47 more)
│
├── inverter/                      ✅ KEEP
│   ├── InverterProtocol.h
│   ├── CanInverterProtocol.h/cpp
│   ├── INVERTERS.h/cpp
│   ├── SMA-TRIPOWER-CAN.h/cpp     (15+ inverter types)
│   └── ... (13 more)
│
├── communication/                 ✅ KEEP
│   ├── can/
│   │   ├── CanReceiver.h
│   │   ├── comm_can.h/cpp
│   │   └── Transmitter.h
│   └── (other subdirs)
│
├── datalayer/                     ✅ KEEP
│   ├── datalayer.h
│   ├── datalayer.cpp
│   ├── datalayer_extended.h/cpp
│   └── (unchanged from Battery Emulator)
│
└── devboard/                      ⚠️  PARTIAL KEEP
    ├── utils/
    │   ├── types.h               ✅ Keep (with renamed enums)
    │   └── types.cpp             ✅ Keep
    └── hal/                       ❌ DELETE
        ├── hal.h                 (replaced by our HAL)
        ├── hal_minimal.h         ❌ ALREADY DELETED
        └── ...
```

**Why Keep:**
- Proven, tested code
- 50+ BMS parsers
- 15+ inverter protocols
- Auto-registration mechanism
- Datalayer state machine

**Why Delete Battery Emulator HAL:**
- Designed for original Lilygo single board
- Conflicts with our Olimex + Waveshare hardware
- We have different GPIO mappings

---

### 5.2 NEW Code - MINIMAL Layer Only

**What we ADD (not modify Battery Emulator):**

#### File: `src/hal/transmitter_hal.h/cpp`
- Initializes Olimex + Waveshare
- Wraps MCP2515 driver
- Wraps Ethernet driver
- ~200 lines total

#### File: `src/hal/receiver_hal.h/cpp`
- Initializes TFT_eSPI display
- Wraps touch interface
- ~200 lines total

#### File: `src/battery/battery_adapter.h/cpp` (Thin Integration Layer)
```cpp
// Only purpose: Initialize Battery Emulator on transmitter
class BatteryAdapter {
  bool init_transmitter(BatteryType type);  // Create BMS + Inverter instances
  void process_can_messages();               // Call receive_can()
  void update_control_loop();                // Call transmit_can()
};
```
- ~100 lines total

#### File: `src/communication/espnow_adapter.h/cpp`
- Reads datalayer.battery.status
- Sends via ESP-NOW to receiver
- Receives config from receiver
- ~150 lines total

#### File: `src/communication/mqtt_adapter.h/cpp`
- Publishes datalayer to MQTT
- Subscribed to config topics
- ~200 lines total

**Total NEW Code:** ~850 lines (thin integration layer, not duplicating logic)

---

### 5.3 Code Removal Checklist

**DELETE from transmitter project (was added before, now removed):**
- ❌ Any custom BMS parsers (we use Battery Emulator's)
- ❌ Any custom datalayer definitions (we use Battery Emulator's)
- ❌ `src/battery/battery_manager.h/cpp` (not needed, use Battery Emulator directly)
- ❌ `lib/battery_emulator_src/communication/CommunicationManager.h/cpp` (not needed)
- ❌ Custom registration system (Battery Emulator has it)
- ❌ `hal_minimal.h/cpp` (already deleted)
- ❌ Any "adapter" code that duplicates Battery Emulator logic

**KEEP from current codebase:**
- ✅ Ethernet manager (works with our setup)
- ✅ MQTT manager (works with datalayer)
- ✅ ESP-NOW data sender (send battery snapshots)
- ✅ Enhanced cache (transient data management)
- ✅ All display and UI code (receiver)

---

## Part 6: Initialization Sequence

### 6.1 Transmitter Startup

```cpp
// src/main.cpp (Transmitter)

void setup() {
  // 1. Initialize HAL (hardware drivers)
  HAL::init();  // Initializes Olimex + Waveshare (in transmitter_hal.cpp)
  
  // 2. Initialize Ethernet
  HAL::Ethernet::init();
  
  // 3. Initialize CAN (Battery Emulator expects this to work)
  HAL::CAN::init(500);  // MCP2515 at 500kbps
  
  // 4. Initialize Battery Emulator
  //    (constructor-based registration, no factory needed)
  BatteryAdapter adapter;
  adapter.init_transmitter(BatteryType::PYLON_BATTERY);
  // This creates:
  //   - PYLON_BATTERY instance (auto-registers as CAN receiver)
  //   - SMA_TRIPOWER_CAN instance (auto-registers as transmitter)
  
  // 5. Initialize communication
  HAL::NVS::init();
  init_mqtt();
  init_espnow();
  
  // 6. Create FreeRTOS tasks
  xTaskCreate(can_rx_task, "can_rx", ...);
  xTaskCreate(inverter_control_task, "inv_ctrl", ...);
  xTaskCreate(data_sync_task, "sync", ...);
  xTaskCreate(mqtt_task, "mqtt", ...);
}

void loop() {
  // FreeRTOS handles everything
  vTaskDelay(portMAX_DELAY);
}
```

### 6.2 Receiver Startup

```cpp
// src/main.cpp (Receiver)

void setup() {
  // 1. Initialize HAL (display drivers)
  HAL::Display::init();
  HAL::Touch::init();
  HAL::WiFi::init("ssid", "password");
  HAL::LittleFS::init();
  
  // 2. Initialize UI
  display_show_splash_screen();
  
  // 3. Initialize ESP-NOW
  init_espnow();
  
  // 4. Create FreeRTOS tasks
  xTaskCreate(espnow_rx_task, "rx", ...);
  xTaskCreate(display_task, "display", ...);
  xTaskCreate(webui_task, "webui", ...);
  
  // 5. NO Battery Emulator code here
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
```

---

## Part 7: Clarifications & Open Questions

### Question 1: Where does Battery Emulator code run?

**Answer:** Only on **TRANSMITTER** (Olimex + Waveshare)

**Verification:**
- Receiver project does NOT `#include "battery/BATTERIES.h"`
- Receiver project does NOT create any Battery instances
- Receiver project does NOT access `extern DataLayer datalayer`

---

### Question 2: What about dual battery support?

**Answer:** Phase 2 enhancement. Single battery support is sufficient for Phase 1.

**For Now:**
- Only sync `datalayer.battery` (not `battery2`)
- ESP-NOW payload is smaller
- Code simpler

**Phase 2 Implementation:**
- Extend payload: `battery2.status` fields
- Extend display: Two gauges side-by-side
- Minimal code change (add same fields to snapshot struct)

---

### Question 3: How is HAL differentiated between control and UI board?

**Answer:** Compile-time selection + function stubs

**Implementation:**
- `platformio.ini` sets build flag: `HAVE_TRANSMITTER_HAL` or `HAVE_RECEIVER_HAL`
- Each HAL implementation has all functions (others are empty stubs)
- Compiler optimizes away unused functions
- Zero runtime overhead

**Example:**
```cpp
// transmitter_hal.h
bool HAL::CAN::init(uint32_t speed) {
  // Real implementation: init MCP2515
}

// receiver_hal.h
bool HAL::CAN::init(uint32_t speed) {
  return false;  // Stub: CAN not available on receiver
}
```

---

### Question 4: What new code needs to be written?

**Answer:** Only thin integration layer (~850 lines)

**Breakdown:**
- `transmitter_hal.h/cpp` (200 lines) - Hardware abstraction
- `receiver_hal.h/cpp` (200 lines) - Display abstraction
- `battery_adapter.h/cpp` (100 lines) - Create BMS/Inverter instances
- `espnow_adapter.h/cpp` (150 lines) - Sync datalayer snapshot
- `mqtt_adapter.h/cpp` (200 lines) - Publish to broker

**NOT WRITTEN:**
- BMS parsers (use Battery Emulator's)
- Datalayer (use Battery Emulator's)
- Inverter control (use Battery Emulator's)
- CAN framework (use Battery Emulator's)

**Total Code Reuse:** 50+ BMS types + 15+ inverter types + datalayer + CAN framework (from Battery Emulator 9.2.4)

---

### Question 5: How do we avoid duplication?

**Answer:** Direct inclusion, no copying

**Strategy:**
1. Copy Battery Emulator source to `lib/battery_emulator_src/`
2. Add to `platformio.ini`: `lib_extra_dirs = lib/battery_emulator_src`
3. Include directly: `#include "battery/BATTERIES.h"`
4. No wrapper classes, no adapters for Battery logic
5. Only adapters for device boundary (ESP-NOW sync, HAL)

**Result:** Single source of truth for all Battery Emulator code

---

### Question 6: What's the compilation process?

**Answer:** Transmitter and Receiver build independently

**Transmitter Build:**
```bash
cd espnowtransmitter2
platformio run -e olimex_esp32_poe2
# Includes: battery_emulator_src/* (all BMS, inverter, datalayer)
# Final size: ~1.2MB
```

**Receiver Build:**
```bash
cd espnowreciever_2
platformio run -e lilygo-t-display-s3
# Excludes: battery_emulator_src/* (not compiled)
# Includes: display, web UI, config
# Final size: ~800KB
```

---

### Question 7: How is Configuration Managed?

**Answer:** Centralized on Transmitter, synced to Receiver via MQTT

**Transmitter (NVS Persistence):**
- BMS type selected: `nvs.setInt("bms_type", PYLON_BATTERY)`
- Inverter type: `nvs.setInt("inv_type", SMA_TRIPOWER)`
- Power limits: `nvs.set("max_charge_w", 5000)`
- Survives reboot

**Receiver (LittleFS Local Cache):**
- Preferred display mode: `littlefs.write("/config/display_mode", ...)`
- WiFi SSID: `littlefs.write("/config/wifi_ssid", ...)`
- Web UI settings: Stored locally

**Sync Protocol (MQTT):**
- Transmitter publishes: `battery/config/bms_type → "PYLON"`
- Receiver subscribes and caches locally
- Receiver sends changes: `receiver/config/display_mode → "DETAILED"`
- Transmitter ignores (receiver-only setting)

---

### Question 8: What about Real-Time Safety Constraints?

**Answer:** All critical safety decisions on Transmitter

**On Transmitter (Real-time, CAN):**
- ✅ Detect under-voltage (< 2.5V cell)
- ✅ Detect over-voltage (> 4.3V cell)
- ✅ Detect over-temperature (> 60°C)
- ✅ Detect over-current (> 100A discharge)
- ✅ Open contactors immediately if fault
- ✅ All decisions < 1ms latency

**On Receiver (Informational only):**
- ✅ Display "FAULT - Under Voltage"
- ✅ Show reason to user
- ✅ Cannot trigger safety action (no GPIO)

**Communication Loss Handling:**
- If ESP-NOW drops: Receiver shows "last update Xs ago", no control impact
- If CAN drops: Transmitter stops charging/discharging (failsafe mode)
- System remains safe regardless of device boundary

---

## Part 8: Remaining Clarifications Needed

### 8.1 Items Requiring User Decision

**1. BMS Type Selection**
- [ ] Primary BMS for transmitter: Pylon? Nissan Leaf? Tesla?
- [ ] Should receiver allow BMS type selection via web UI?
- [ ] Or hardcoded at compile-time for transmitter?

**2. Inverter Integration**
- [ ] Is inverter actually connected to CAN bus?
- [ ] Which model: SMA Tripower? Growatt? Pylon CAN?
- [ ] Or is it added in Phase 2?

**3. Display Update Frequency**
- [ ] Receiver display refresh: 500ms (current) or faster?
- [ ] ESP-NOW sync: 100ms (current) or different?
- [ ] Trade-off: Power vs. responsiveness?

**4. MQTT Broker**
- [ ] Is MQTT broker available (Home Assistant)?
- [ ] Or start without MQTT in Phase 1?
- [ ] Receivers can work without MQTT (just ESP-NOW)?

**5. Dual Battery Support Timeline**
- [ ] Phase 1 (now): Single battery only?
- [ ] Phase 2 (week 2): Add dual battery?
- [ ] Or skip dual battery entirely?

**6. Contact Control (Future)**
- [ ] Will transmitter have GPIO relay boards?
- [ ] Or is contactor control in Phase 2+?
- [ ] For now: Monitor-only mode?

---

### 8.2 Verification Checklist

**Before proceeding to Phase 1 build:**

- [ ] Confirm HAL approach is acceptable (compile-time selection)
- [ ] Confirm no Battery Emulator code is modified
- [ ] Confirm thin integration layer scope (~850 lines)
- [ ] Confirm single battery support sufficient for Phase 1
- [ ] Confirm receiver is display-only (zero Battery Emulator code)
- [ ] Confirm transmitter handles all control logic
- [ ] Confirm device boundary is at ESP-NOW/MQTT level
- [ ] Confirm project structure is correct

---

## Part 9: File Structure (Final)

### Transmitter Project Structure

```
espnowtransmitter2/
├── platformio.ini                           (add: HAVE_TRANSMITTER_HAL)
├── src/
│   ├── main.cpp                             (updated: uses battery_adapter)
│   ├── hal/
│   │   ├── hal_common.h                     (shared definitions)
│   │   ├── transmitter_hal.h                (NEW: Olimex + Waveshare)
│   │   ├── transmitter_hal.cpp
│   │   ├── receiver_hal.h                   (stubs for compilation)
│   │   └── receiver_hal.cpp
│   ├── battery/
│   │   ├── battery_adapter.h                (NEW: thin integration)
│   │   └── battery_adapter.cpp
│   ├── communication/
│   │   ├── espnow_adapter.h                 (NEW: sync battery snapshot)
│   │   ├── espnow_adapter.cpp
│   │   ├── mqtt_adapter.h                   (NEW: publish datalayer)
│   │   └── mqtt_adapter.cpp
│   ├── datalayer/
│   │   ├── datalayer_mirror.h               (subset for transmitter)
│   │   └── datalayer_mirror.cpp
│   └── ... (existing code: network, espnow, settings)
│
└── lib/
    └── battery_emulator_src/                (FROM Battery Emulator 9.2.4)
        ├── battery/                         ✅ KEPT AS-IS
        │   ├── Battery.h/cpp
        │   ├── CanBattery.h/cpp
        │   ├── PYLON-BATTERY.h/cpp
        │   └── ... (49 more BMS types)
        ├── inverter/                        ✅ KEPT AS-IS
        │   ├── InverterProtocol.h
        │   ├── CanInverterProtocol.h/cpp
        │   ├── SMA-TRIPOWER-CAN.h/cpp
        │   └── ... (14 more inverter types)
        ├── communication/                   ✅ KEPT AS-IS
        │   └── can/
        │       ├── CanReceiver.h
        │       ├── comm_can.h/cpp
        │       └── Transmitter.h
        ├── datalayer/                       ✅ KEPT AS-IS
        │   ├── datalayer.h
        │   ├── datalayer.cpp
        │   └── datalayer_extended.h/cpp
        └── devboard/
            └── utils/
                ├── types.h                  ✅ KEPT (renamed enums)
                └── types.cpp
```

### Receiver Project Structure

```
espnowreciever_2/
├── platformio.ini                           (add: HAVE_RECEIVER_HAL)
├── src/
│   ├── main.cpp                             (NO Battery Emulator code)
│   ├── hal/
│   │   ├── hal_common.h                     (shared)
│   │   ├── receiver_hal.h                   (NEW: display + touch)
│   │   ├── receiver_hal.cpp
│   │   └── transmitter_hal.h                (stubs)
│   ├── datalayer/
│   │   ├── datalayer_mirror.h               (subset sync'd from transmitter)
│   │   └── datalayer_mirror.cpp
│   ├── display/
│   │   ├── display_core.cpp                 (draw battery gauge)
│   │   └── ... (existing display code)
│   ├── espnow/
│   │   ├── espnow_rx.cpp                    (receive snapshots)
│   │   └── ... (existing ESP-NOW code)
│   └── ... (webui, config, etc.)
│
└── lib/
    ├── (NO battery_emulator_src)             ✅ NOT INCLUDED
    └── (standard Arduino/FreeRTOS libraries)
```

---

## Part 10: What Changed from Document Version 1.0?

| Item | Version 1.0 | Version 2.0 |
|------|-------------|------------|
| HAL Approach | Minimal stub | Compile-time selection |
| Code Duplication | High (adapters) | Minimal (thin layer) |
| Device Differentiation | Not clear | Clear + compile-time flags |
| Transmitter CPU | "Light workload" | Full Battery Emulator engine |
| Receiver Role | Display + some logic | Display only |
| BMS Parser Location | Unclear | Transmitter only |
| Inverter Control | Unclear | Transmitter only |
| Dual Battery | Phase 1 | Phase 2 |
| New Code Lines | ~3000 | ~850 |
| Code Reuse | Partial | Maximum |
| Integration Pattern | Factory | Direct inclusion |

---

## Conclusion

**Philosophy:** Don't rewrite what's proven. Battery Emulator is production-tested across 50+ BMS types and 15+ inverter protocols. Use it as-is.

**Architecture:** Clean 2-device boundary at communication layer (ESP-NOW + MQTT), not at logic layer. Transmitter runs full Battery Emulator engine. Receiver is pure display client.

**Implementation:** Thin integration layer (~850 lines) to bridge the device boundary. All Battery Emulator code unchanged.

**Next Step:** Get user feedback on clarifications before proceeding to Phase 1 build.

