# Battery Emulator Transmitter Build Architecture
**Document Version:** 1.0  
**Date:** February 18, 2026  
**Purpose:** Define correct compilation strategy for Battery Emulator on ESP-NOW Transmitter

---

## Executive Summary

The transmitter must include **ALL** Battery Emulator functionality EXCEPT the web UI/display components. The issue is not what to exclude, but how to make the optional Modbus inverters truly optional without breaking the core build.

### What IS Included on Transmitter
- ✅ **Datalayer & structs** - Real-time state container
- ✅ **BMS Parsers** (40+ battery types) - Read battery state from CAN
- ✅ **CAN Communication** - Multi-interface support (native, MCP2515, MCP2517FD)
- ✅ **Inverter Control** (15+ types including Modbus) - Write commands to inverter
- ✅ **Charger Support** - Parse charger status from CAN
- ✅ **Contact Control** - Contactor/precharge/isolation logic
- ✅ **NVM Settings** - Persistent configuration via Preferences
- ✅ **Safety Monitoring** - Temperature, voltage, current limits

### What is NOT Included
- ❌ **Web UI** (devboard/webserver/**) - No HTML rendering
- ❌ **Display rendering** (devboard/display/**) - No TFT graphics code
- ❌ **MQTT server** (devboard/mqtt/**) - Only MQTT client (in main transmitter app)
- ❌ **LED display handler** (devboard/utils/led_handler.cpp) - Transmitter has no status LEDs

---

## Architecture: Which Files to Compile

### Core Modules (ALWAYS Compile)

**Datalayer & Data Structures**
```
datalayer/datalayer.h/.cpp          ✅ Global state container
datalayer/datalayer_extended.h/.cpp ✅ Extended battery types
devboard/utils/types.h              ✅ Enums and structs
devboard/utils/compat.h             ✅ Compatibility macros (BMS_FAULT)
```

**CAN Communication Infrastructure**
```
communication/can/comm_can.h/.cpp              ✅ CAN driver & frame routing
communication/can/CanReceiver.h                ✅ Base class for receivers
communication/can/obd.cpp                      ✅ OBD protocol support
communication/CommunicationManager.h/.cpp      ✅ Receiver/transmitter registration
communication/Transmitter.h                    ✅ Base class for transmitters
communication/can/CanReceiver.h                ✅ Registration system
```

**Battery Parsers (BMS Implementations)**
```
battery/CanBattery.h/.cpp           ✅ Base class
battery/PYLON-BATTERY.h/.cpp        ✅ Pylon battery
battery/TESLA-BATTERY.h/.cpp        ✅ Tesla battery
battery/KIA-*.h/.cpp                ✅ Kia models
battery/BMW-*.h/.cpp                ✅ BMW models
battery/NISSAN-LEAF-BATTERY.h/.cpp  ✅ Nissan Leaf
battery/MEB-BATTERY.h/.cpp          ✅ VW MEB
battery/*.cpp                       ✅ All other BMS parsers (50+ types)
battery/Shunts.cpp                  ✅ Shunt implementations
battery/BATTERIES.cpp               ✅ Battery factory
```

**Inverter Control (CAN-based)**
```
inverter/CanInverterProtocol.h/.cpp ✅ Base class
inverter/PYLON-CAN.h/.cpp           ✅ Pylon inverter
inverter/SMA-*.h/.cpp               ✅ SMA models (CAN versions)
inverter/GROWATT-*-CAN.h/.cpp       ✅ Growatt CAN models
inverter/FOXESS-CAN.h/.cpp          ✅ Fox ESS
inverter/FERROAMP-CAN.h/.cpp        ✅ Ferroamp
inverter/SOLAX-CAN.h/.cpp           ✅ SolaX CAN
inverter/SOLXPOW-CAN.h/.cpp         ✅ SolaXPow CAN
inverter/SUNGROW-CAN.h/.cpp         ✅ Sungrow CAN
inverter/*.h (CAN models)           ✅ All CAN-based inverters
inverter/INVERTERS.cpp              ✅ Inverter factory (see below)
```

**Inverter Control (Modbus-based - CONDITIONAL)**
```
inverter/ModbusInverterProtocol.h/.cpp     ⚠️ Only if CONFIG_INVERTER_MODBUS=1
inverter/BYD-MODBUS.h/.cpp                 ⚠️ Only if CONFIG_INVERTER_MODBUS=1
inverter/KOSTAL-RS485.h/.cpp               ⚠️ Only if CONFIG_INVERTER_MODBUS=1
inverter/SMA-MODBUS.h/.cpp                 ⚠️ Only if CONFIG_INVERTER_MODBUS=1
inverter/GROWATT-MODBUS.h/.cpp             ⚠️ Only if CONFIG_INVERTER_MODBUS=1
inverter/SOFAR-MODBUS.h/.cpp               ⚠️ Only if CONFIG_INVERTER_MODBUS=1
inverter/VICTRON-MODBUS.h/.cpp             ⚠️ Only if CONFIG_INVERTER_MODBUS=1
inverter/FRONIUS-MODBUS.h/.cpp             ⚠️ Only if CONFIG_INVERTER_MODBUS=1
inverter/SOLARMAX-RS485.h/.cpp             ⚠️ Only if CONFIG_INVERTER_MODBUS=1
```

**Charger Support**
```
charger/CanCharger.h/.cpp           ✅ Base class
charger/NISSAN-LEAF-CHARGER.h/.cpp  ✅ Nissan Leaf charger
charger/CHEVY-VOLT-CHARGER.h/.cpp   ✅ Chevy Volt charger
charger/CHARGERS.cpp                ✅ Charger factory
```

**Contactor/Safety Control**
```
communication/contactorcontrol/comm_contactorcontrol.h/.cpp  ✅ Contactor control
communication/precharge_control/precharge_control.h/.cpp     ✅ Precharge logic
communication/equipmentstopbutton/comm_equipmentstopbutton.h ✅ Emergency stop
communication/rs485/comm_rs485.h/.cpp                        ✅ RS485 interface
```

**Settings & Configuration**
```
communication/nvm/comm_nvm.h/.cpp   ✅ NVS storage for settings
devboard/utils/common_functions.cpp ✅ Utility functions
devboard/utils/types.cpp            ✅ Type helpers
devboard/utils/logging.h/.cpp       ✅ Logging infrastructure
devboard/utils/timer.h/.cpp         ✅ Timer utilities
devboard/utils/events.h/.cpp        ✅ Event system
devboard/utils/debounce_button.h/.cpp ✅ Button debouncing (if HW needs it)
devboard/utils/millis64.h/.cpp      ✅ 64-bit millisecond timer
```

**Safety & Monitoring**
```
devboard/safety/safety.h/.cpp       ✅ Safety monitoring (temp, voltage limits)
```

**SD Card Support (optional)**
```
devboard/sdcard/sdcard.h/.cpp       ⚠️ Optional - needed only if logging to SD
```

**WiFi Support**
```
devboard/wifi/wifi.h/.cpp           ⚠️ Optional - MQTT client needs this
```

---

## Architecture: Files to EXCLUDE

### Web UI Components (ALWAYS Exclude)
```
devboard/webserver/**                  ❌ HTML/CSS rendering
devboard/display/**                    ❌ TFT graphics
devboard/mqtt/mqtt_server.cpp          ❌ MQTT broker server
devboard/utils/led_handler.cpp         ❌ Status LED display
```

### Board-Specific HAL (EXCLUDE except Transmitter HAL)
```
devboard/hal/hw_lilygo.cpp             ❌ LilyGo Lilygo HAL (not this board)
devboard/hal/hw_lilygo2can.cpp         ❌ LilyGo with 2 CAN HAL
devboard/hal/hw_lilygo_t_connect_pro.cpp ❌ LilyGo T-Connect HAL
devboard/hal/hw_stark.cpp              ❌ Stark board HAL
devboard/hal/hw_3LB.cpp                ❌ 3LB HAL
devboard/hal/hw_devkit.cpp             ❌ Generic DevKit HAL
devboard/hal/hw_olimex_esp32_poe2.cpp  ✅ INCLUDE THIS - it's our board
```

### Test Code (ALWAYS Exclude)
```
**/test/**                             ❌ Unit tests
**/tests/**                            ❌ Test suites
**/examples/**                         ❌ Example code
**/*.md                                ❌ Documentation
```

---

## Solution: Conditional Compilation

The key insight: **Modbus support is optional, not fundamental**. We need conditional compilation that:

1. **Always compiles:** Core Battery Emulator (datalayer, BMS, CAN inverters, contactors)
2. **Conditionally compiles:** Modbus support (only if CONFIG_INVERTER_MODBUS=1)

### Step 1: Update INVERTERS.h with Preprocessor Guards

**File:** `lib/battery_emulator_src/inverter/INVERTERS.h`

```cpp
#ifndef INVERTERS_H
#define INVERTERS_H

#include "InverterProtocol.h"
extern InverterProtocol* inverter;

// CAN-based inverters (ALWAYS included)
#include "AFORE-CAN.h"
#include "FERROAMP-CAN.h"
#include "FOXESS-CAN.h"
#include "GROWATT-HV-CAN.h"
#include "GROWATT-LV-CAN.h"
#include "GROWATT-WIT-CAN.h"
#include "PYLON-CAN.h"
#include "PYLON-LV-CAN.h"
#include "SCHNEIDER-CAN.h"
#include "SMA-BYD-H-CAN.h"
#include "SMA-BYD-HVS-CAN.h"
#include "SMA-LV-CAN.h"
#include "SMA-TRIPOWER-CAN.h"
#include "SOFAR-CAN.h"
#include "SOL-ARK-LV-CAN.h"
#include "SOLAX-CAN.h"
#include "SOLXPOW-CAN.h"
#include "SUNGROW-CAN.h"

// BYD CAN inverter (hybrid - has CAN and Modbus versions)
#include "BYD-CAN.h"

// Modbus-based inverters (ONLY if CONFIG_INVERTER_MODBUS enabled)
#ifdef CONFIG_INVERTER_MODBUS
  #include "BYD-MODBUS.h"
  #include "FRONIUS-MODBUS.h"
  #include "GROWATT-MODBUS.h"
  #include "KOSTAL-RS485.h"
  #include "PHOCOS-CAN.h"
  #include "PYLONFORCE-MODBUS.h"
  #include "SOLARMAX-RS485.h"
  #include "SMA-MODBUS.h"
  #include "SOFAR-MODBUS.h"
  #include "VICTRON-MODBUS.h"
#endif

// Call to initialize the build-time selected inverter
bool setup_inverter();

// ... rest of file ...
#endif
```

### Step 2: Update INVERTERS.cpp with Conditional Instantiation

**File:** `lib/battery_emulator_src/inverter/INVERTERS.cpp`

```cpp
#include "INVERTERS.h"
#include "InverterProtocol.h"

// Instantiate inverter based on build configuration
InverterProtocol* inverter = nullptr;

bool setup_inverter() {
#ifdef CONFIG_INVERTER_PYLON_CAN
  inverter = new PylonCanInverter();
  return true;
#elif defined(CONFIG_INVERTER_SMA_TRIPOWER)
  inverter = new SmaTriPowerCan();
  return true;
#elif defined(CONFIG_INVERTER_GROWATT_HV)
  inverter = new GrowattHVCan();
  return true;
// ... more CAN inverters ...

#ifdef CONFIG_INVERTER_MODBUS
  #ifdef CONFIG_INVERTER_BYD_MODBUS
    inverter = new BydModbusInverter();
    return true;
  #elif defined(CONFIG_INVERTER_SMA_MODBUS)
    inverter = new SmaModbusInverter();
    return true;
  // ... more Modbus inverters ...
  #endif
#endif

  // No inverter configured
  return false;
}
```

### Step 3: Update platformio.ini Build Flags

**File:** `platformio.ini`

For transmitter with CAN-based inverter (NO Modbus):
```ini
[env:olimex_esp32_poe2]
build_flags =
    -std=gnu++17
    ; ... other flags ...
    
    ; Inverter selection - CAN-based (NO Modbus)
    -D CONFIG_INVERTER_PYLON_CAN
    ; Do NOT define CONFIG_INVERTER_MODBUS
```

For transmitter with Modbus-based inverter:
```ini
[env:olimex_esp32_poe2_modbus]
build_flags =
    -std=gnu++17
    ; ... other flags ...
    
    ; Inverter selection - Modbus-based (includes eModbus)
    -D CONFIG_INVERTER_MODBUS
    -D CONFIG_INVERTER_SMA_MODBUS
    ; Now eModbus library can be linked
    
lib_deps =
    ; ... existing deps ...
    ; eModbus library (only needed for Modbus inverters)
    https://github.com/Bonaci/eModbus.git#release/1.4
```

### Step 4: Update library.json to Respect Config

**File:** `lib/battery_emulator_src/library.json`

```json
{
  "name": "battery_emulator_src",
  "version": "9.2.4",
  "description": "Battery Emulator core sources for ESP-NOW transmitter",
  "frameworks": ["arduino"],
  "platforms": ["espressif32"],
  "build": {
    "srcDir": ".",
    "includeDir": ".",
    "srcFilter": [
      "+<*>",
      "-<lib/>",
      "-<devboard/display/>",
      "-<devboard/webserver/>",
      "-<devboard/mqtt/>",
      "-<devboard/utils/led_handler.cpp>",
      "-<devboard/hal/hw_lilygo.cpp>",
      "-<devboard/hal/hw_lilygo2can.cpp>",
      "-<devboard/hal/hw_lilygo_t_connect_pro.cpp>",
      "-<devboard/hal/hw_stark.cpp>",
      "-<devboard/hal/hw_3LB.cpp>",
      "-<devboard/hal/hw_devkit.cpp>",
      "-<devboard/safety/>",
      "-<**/test/>",
      "-<**/tests/>",
      "-<**/examples/>",
      "-<**/extras/>",
      "-<**/docs/>",
      "-<**/data/>",
      "-<**/*.md>"
    ]
  },
  "dependencies": [
    {
      "name": "Preferences"
    }
  ]
}
```

**Note:** Modbus-specific files are NOT excluded from srcFilter. Instead, they're guarded by conditional includes in headers. The headers won't be included unless `CONFIG_INVERTER_MODBUS` is defined, so the .cpp files won't be compiled into the library anyway.

---

## Build Configuration for Olimex ESP32-POE2 Transmitter

### Recommended Configuration: CAN-Only Inverters (NO Modbus)

**Why:** Olimex board has Ethernet, so MQTT communication is available. No need for Modbus RS485 communication directly on board.

**platformio.ini:**
```ini
[env:olimex_esp32_poe2]
platform = espressif32@6.5.0
board = esp32-poe2
framework = arduino
lib_ldf_mode = chain+

build_flags = 
    -std=gnu++17
    -I../../esp32common
    -I${platformio.packages_dir}/framework-arduinoespressif32/libraries/FS/src
    -D TRANSMITTER_DEVICE
    -D FW_VERSION_MAJOR=2
    -D FW_VERSION_MINOR=0
    -D FW_VERSION_PATCH=0
    -D PIO_ENV_NAME=$PIOENV
    -D TARGET_DEVICE=TRANSMITTER
    -DCORE_DEBUG_LEVEL=3
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
    -DARDUINO_ARCH_ESP32
    -DHW_OLIMEX_ESP32_POE2
    -Wno-deprecated-declarations
    -Wno-builtin-macro-redefined
    -Wno-cpp
    -D COMPILE_LOG_LEVEL=LOG_INFO
    
    ; Inverter configuration - CAN-based only
    -D CONFIG_INVERTER_PYLON_CAN
    ; Do NOT include CONFIG_INVERTER_MODBUS

lib_deps = 
    knolleary/PubSubClient @ ^2.8
    bblanchon/ArduinoJson @ ^6.21.3
    https://github.com/me-no-dev/ESPAsyncWebServer.git
    https://github.com/me-no-dev/AsyncTCP.git
    marian-craciunescu/ESP32Ping @ ^1.7
    autowp/autowp-mcp2515 @ ^1.3.1
    ${platformio.packages_dir}/framework-arduinoespressif32/libraries/Preferences
    ${platformio.packages_dir}/framework-arduinoespressif32/libraries/FS

lib_extra_dirs = 
    ../../esp32common
    lib/battery_emulator_src
```

---

## Testing & Verification

### Build Verification
```bash
# Should compile without errors or warnings
platformio run -e olimex_esp32_poe2

# Check compilation (no Modbus)
platformio run -e olimex_esp32_poe2 --verbose | grep -i "modbus"
# Should show NO modbus references
```

### Runtime Verification
After flashing transmitter:
```cpp
// In setup()
Serial.printf("Inverter selected: %s\n", inverter ? "YES" : "NO");
if (inverter) {
  Serial.printf("  Type: %s\n", inverter->interface_name());
}

// Expected output (if CONFIG_INVERTER_PYLON_CAN):
// Inverter selected: YES
//   Type: Pylon CAN
```

---

## Summary: What Changed

**DO INCLUDE:**
- ✅ ALL battery parsers (battery/*.cpp)
- ✅ ALL CAN inverters (inverter/*-CAN.cpp)
- ✅ Contact control (communication/contactorcontrol/*)
- ✅ Charger support (charger/*)
- ✅ NVM settings (communication/nvm/*)
- ✅ Safety monitoring (devboard/safety/*)
- ✅ Logging (devboard/utils/logging.*)

**DO NOT INCLUDE:**
- ❌ Web server (devboard/webserver/**)
- ❌ TFT display (devboard/display/**)
- ❌ Status LEDs (devboard/utils/led_handler.cpp)
- ❌ Non-Olimex HAL files
- ❌ Test code

**CONDITIONALLY INCLUDE:**
- ⚠️ Modbus inverters - only if CONFIG_INVERTER_MODBUS=1
- ⚠️ eModbus library - only if CONFIG_INVERTER_MODBUS=1

This approach keeps the core Battery Emulator logic intact while allowing Modbus support to be truly optional.
