# Battery Emulator Transmitter Integration - Comprehensive Architecture

**Document Version:** 1.0  
**Date:** February 18, 2026  
**Purpose:** Define clean architecture for integrating Battery Emulator into transmitter without file structure modifications or shims  
**Status:** Design Phase - Ready for Implementation

---

## Executive Summary

The transmitter needs **Battery Emulator's core logic** (datalayer + BMS parsing + inverter control) but must work within a **new file layout** that separates transmitter from receiver. This document defines how to achieve this through **proper header architecture** rather than exclusions and fudges.

**Core Principle:** Preserve Battery Emulator's control logic and data flow intact. Adapt only the build configuration and optional feature includes to match the transmitter's hardware constraints.

---

## Part 1: Transmitter Functional Requirements

### 1.1 What the Transmitter Needs (Must-Have)

1. **Datalayer** (core data container)
   - `datalayer.battery.status` - real-time battery state (voltage, current, SOC, temp)
   - `datalayer.system.status` - system state (contactors, precharge, faults)
   - `datalayer.inverter.status` - inverter state (current, power, limits)

2. **BMS Parsers** (40+ battery types)
   - Read CAN frames from battery
   - Parse into datalayer.battery
   - Examples: Pylon, Tesla, BMW, Kia, Nissan Leaf

3. **CAN Communication** (multi-interface support)
   - Native CAN (Olimex ESP32-POE2 has one native CAN)
   - MCP2515 add-on (for battery if needed)
   - MCP2518 FD add-on (optional)
   - Receiver registration system (auto-discovery)

4. **Inverter Control** (15+ inverter types)
   - **CAN-based only** for transmitter:
     - SMA Tripower CAN
     - Growatt HV/LV CAN
     - Pylon CAN
     - BYD CAN
     - Solax CAN
     - SOFAR CAN
     - Sungrow CAN
     - (Others as needed)
   - **NOT Modbus-based** (requires eModbus library)
     - BYD Modbus (uses eModbus)
     - Kostal RS485 (uses eModbus)
     - Growatt Modbus (uses eModbus)
     - SMA Modbus (uses eModbus)
     - Fronius Modbus (uses eModbus)

5. **System Control** (Optional for Phase 1)
   - Contactor control (may be hardcoded for specific hardware)
   - Precharge control (may be hardcoded)
   - NVM settings (Preferences library - supported)

### 1.2 What the Transmitter Does NOT Need

1. **Display/UI Components**
   - `devboard/display/` - Not compiled (no display on transmitter)
   - `devboard/webserver/` - Not compiled (replaced by ESP-NOW + MQTT)
   - `devboard/mqtt/` - Not compiled (handled by transmitter's own MQTT)

2. **Modbus Protocol Support**
   - `lib/eModbus-eModbus/` - Not needed (no Modbus inverters in transmitter)
   - ModbusInverterProtocol.cpp/h
   - BYD-MODBUS.cpp/h
   - KOSTAL-RS485.cpp/h
   - GROWATT-MODBUS.cpp/h
   - FRONIUS-MODBUS.cpp/h
   - SOLARMAX-RS485.cpp/h
   - SMA-MODBUS.cpp/h
   - SOFAR-MODBUS.cpp/h
   - VICTRON-MODBUS.cpp/h
   - PHOCOS-CAN.cpp/h (uses ModbusServer.h)

3. **Receiver-Specific Control**
   - `communication/contactorcontrol/` - Only on device with contactors
   - `communication/nvm/` - Optional for Phase 1

---

## Part 2: The Core Problem

### 2.1 Current Dependency Chain

```
Shunts.h (battery shunt measurements)
  ├─ includes INVERTERS.h
  │   ├─ includes BYD-CAN.h ✓ (OK - CAN based)
  │   ├─ includes BYD-MODBUS.h ✗ (PROBLEM - pulls ModbusInverterProtocol.h)
  │   ├─ includes KOSTAL-RS485.h ✗ (PROBLEM - pulls ModbusInverterProtocol.h)
  │   ├─ includes GROWATT-MODBUS.h ✗ (PROBLEM)
  │   ...
  │
  ├─ ModbusInverterProtocol.h (for Modbus inverters)
  │   └─ ModbusServer.h (eModbus library - NOT linked)
  │       └─ ModbusMessage class (undefined symbols)
```

**Result:** Even if we only use Pylon CAN inverter, including `INVERTERS.h` pulls in ALL inverter headers, including ones that depend on eModbus.

### 2.2 Why srcFilter Exclusions Don't Work

PlatformIO's library system compiles files into intermediate libraries based on folder structure. Even if we exclude `BYD-MODBUS.cpp` from compilation, the header chain still exists:

- `INVERTERS.h` includes `BYD-MODBUS.h` (header-only, always evaluated)
- `BYD-MODBUS.h` includes `ModbusInverterProtocol.h`
- `ModbusInverterProtocol.h` includes `ModbusServer.h`
- Compiler tries to validate the type definitions in `ModbusServer.h`
- Even though no code uses ModbusMessage, the linker later fails when static initializers reference it

---

## Part 3: The Proper Solution

### 3.1 Strategy: Header Isolation via Build Flags

Instead of file exclusions, use **preprocessor directives** to conditionally include optional inverter headers.

**Key Insight:** The transmitter project (not Battery Emulator) decides which inverters to support. Battery Emulator provides the infrastructure; the transmitter specifies the configuration.

### 3.2 Implementation: Create Transmitter Configuration Header

**File:** `espnowtransmitter2/include/inverter_config.h`

```cpp
#ifndef INVERTER_CONFIG_H
#define INVERTER_CONFIG_H

/**
 * Transmitter Inverter Support Configuration
 * 
 * This file defines which Battery Emulator inverter types are compiled
 * for the transmitter. Modbus-based inverters require eModbus library
 * and are excluded to reduce dependencies.
 * 
 * Supported Inverter Categories:
 * - CAN-based inverters: Fully supported (no external dependencies)
 * - Modbus-based inverters: Excluded (require eModbus library not in scope)
 */

// ============================================
// CAN-BASED INVERTERS (Fully Supported)
// ============================================

#define SUPPORT_AFORE_CAN 1
#define SUPPORT_BYD_CAN 1
#define SUPPORT_FERROAMP_CAN 1
#define SUPPORT_FOXESS_CAN 1
#define SUPPORT_GROWATT_HV_CAN 1
#define SUPPORT_GROWATT_LV_CAN 1
#define SUPPORT_GROWATT_WIT_CAN 1
#define SUPPORT_PYLON_CAN 1
#define SUPPORT_PYLON_LV_CAN 1
#define SUPPORT_SCHNEIDER_CAN 1
#define SUPPORT_SMA_BYD_H_CAN 1
#define SUPPORT_SMA_BYD_HVS_CAN 1
#define SUPPORT_SMA_LV_CAN 1
#define SUPPORT_SMA_TRIPOWER_CAN 1
#define SUPPORT_SOFAR_CAN 1
#define SUPPORT_SOL_ARK_LV_CAN 1
#define SUPPORT_SOLAX_CAN 1
#define SUPPORT_SOLXPOW_CAN 1
#define SUPPORT_SUNGROW_CAN 1

// ============================================
// MODBUS-BASED INVERTERS (Excluded)
// ============================================
// These require eModbus library which is not
// in the transmitter's scope. They can be added
// in Phase 2 if needed.

#define SUPPORT_BYD_MODBUS 0        // Needs eModbus
#define SUPPORT_KOSTAL_RS485 0      // Needs eModbus
#define SUPPORT_GROWATT_MODBUS 0    // Needs eModbus
#define SUPPORT_FRONIUS_MODBUS 0    // Needs eModbus
#define SUPPORT_SOLARMAX_RS485 0    // Needs eModbus
#define SUPPORT_SMA_MODBUS 0        // Needs eModbus
#define SUPPORT_SOFAR_MODBUS 0      // Needs eModbus
#define SUPPORT_VICTRON_MODBUS 0    // Needs eModbus
#define SUPPORT_PHOCOS_CAN 0        // Uses ModbusServer.h

#endif // INVERTER_CONFIG_H
```

### 3.3 Modify Battery Emulator Headers

**File:** `lib/battery_emulator_src/inverter/INVERTERS.h`

Replace the unconditional includes with conditional ones based on the transmitter's configuration:

```cpp
#ifndef INVERTERS_H
#define INVERTERS_H

#include "InverterProtocol.h"
extern InverterProtocol* inverter;

// Include transmitter configuration to determine which inverters to support
#ifdef ARDUINO
  #include "../../espnowtransmitter2/include/inverter_config.h"
#endif

// ============================================
// CAN-BASED INVERTERS (Always Safe)
// ============================================

#include "AFORE-CAN.h"
#include "BYD-CAN.h"
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

// ============================================
// MODBUS-BASED INVERTERS (Conditional)
// ============================================
// Only included if eModbus is available
// and transmitter explicitly enables them

#if defined(SUPPORT_BYD_MODBUS) && SUPPORT_BYD_MODBUS
  #include "BYD-MODBUS.h"
#endif

#if defined(SUPPORT_KOSTAL_RS485) && SUPPORT_KOSTAL_RS485
  #include "KOSTAL-RS485.h"
#endif

#if defined(SUPPORT_GROWATT_MODBUS) && SUPPORT_GROWATT_MODBUS
  #include "GROWATT-MODBUS.h"
#endif

#if defined(SUPPORT_FRONIUS_MODBUS) && SUPPORT_FRONIUS_MODBUS
  #include "FRONIUS-MODBUS.h"
#endif

#if defined(SUPPORT_SOLARMAX_RS485) && SUPPORT_SOLARMAX_RS485
  #include "SOLARMAX-RS485.h"
#endif

#if defined(SUPPORT_SMA_MODBUS) && SUPPORT_SMA_MODBUS
  #include "SMA-MODBUS.h"
#endif

#if defined(SUPPORT_SOFAR_MODBUS) && SUPPORT_SOFAR_MODBUS
  #include "SOFAR-MODBUS.h"
#endif

#if defined(SUPPORT_VICTRON_MODBUS) && SUPPORT_VICTRON_MODBUS
  #include "VICTRON-MODBUS.h"
#endif

#if defined(SUPPORT_PHOCOS_CAN) && SUPPORT_PHOCOS_CAN
  #include "PHOCOS-CAN.h"
#endif

// ... rest of file remains unchanged
#endif
```

### 3.4 Update platformio.ini

Add the inverter configuration to build flags:

```ini
build_flags = 
    -std=gnu++17
    -I../../esp32common/espnow_transmitter
    -I../../esp32common
    -I$PROJECT_INCLUDE_DIR
    -I${platformio.packages_dir}/framework-arduinoespressif32/libraries/FS/src
    
    ; Transmitter Inverter Configuration
    -I./include/inverter_config.h
    
    ; ... rest of flags
```

Or simpler, just add the defines directly:

```ini
build_flags = 
    -std=gnu++17
    -DSUPPORT_AFORE_CAN=1
    -DSUPPORT_BYD_CAN=1
    -DSUPPORT_PYLON_CAN=1
    -DSUPPORT_SMA_TRIPOWER_CAN=1
    -DSUPPORT_SOLAX_CAN=1
    -DSUPPORT_SOFAR_CAN=1
    -DSUPPORT_SUNGROW_CAN=1
    -DSUPPORT_BYD_MODBUS=0
    -DSUPPORT_KOSTAL_RS485=0
    ; ... etc
```

### 3.5 Update Communication Support Files

**File:** `lib/battery_emulator_src/communication/comm_contactorcontrol.cpp`

Currently this file includes ModbusServer.h indirectly. Check if it's truly needed for transmitter:

```cpp
// At top of file, add:
#ifndef SUPPORT_MODBUS_INVERTERS
#define SUPPORT_MODBUS_INVERTERS (SUPPORT_BYD_MODBUS || SUPPORT_KOSTAL_RS485 || \
                                   SUPPORT_GROWATT_MODBUS || SUPPORT_FRONIUS_MODBUS || \
                                   SUPPORT_SOLARMAX_RS485 || SUPPORT_SMA_MODBUS || \
                                   SUPPORT_SOFAR_MODBUS || SUPPORT_VICTRON_MODBUS)
#endif

#if SUPPORT_MODBUS_INVERTERS
  // Include ModbusServer.h and other Modbus dependencies
  #include "../lib/eModbus-eModbus/ModbusServer.h"
  // ... rest of Modbus code
#endif
```

Similarly for `comm_nvm.cpp` and other optional features.

---

## Part 4: Implementation Steps

### Step 1: Create Transmitter Configuration
- Create `espnowtransmitter2/include/inverter_config.h` with flags above
- Add to platformio.ini build_flags

### Step 2: Update INVERTERS.h
- Add conditional includes based on `SUPPORT_*` flags
- Ensure Modbus-based inverters are guarded

### Step 3: Update Optional Communication Files
- `comm_contactorcontrol.cpp` - guard Modbus includes
- `comm_nvm.cpp` - guard Modbus includes
- Wrap static initializers with `#if` guards

### Step 4: Clean Library.json
- Remove srcFilter exclusions (they don't work reliably)
- Use header guards instead (more explicit and works)

### Step 5: Test Build
```bash
cd espnowtransmitter2
platformio run -e olimex_esp32_poe2
```

---

## Part 5: Benefits of This Approach

1. **Preserves Battery Emulator Integrity**
   - No modifications to core Battery Emulator logic
   - No stub implementations or shims
   - Core library remains clean and reusable

2. **Explicit Configuration**
   - Transmitter defines its own capabilities
   - Clear, documented feature set
   - Easy to add/remove inverter support later

3. **Scalable**
   - Receiver can have different inverter_config.h (display-only = zero inverters)
   - Other projects can use Battery Emulator with their own configurations
   - Phase 2 can add eModbus and Modbus inverters by changing one flag

4. **No Compile Workarounds**
   - Uses standard C++ preprocessor
   - No file exclusions or PlatformIO-specific tricks
   - Portable to other build systems

5. **Clear Error Messages**
   - If Modbus inverter enabled without eModbus, compiler clearly states why
   - No mysterious linker errors about undefined symbols

---

## Part 6: Future Phases

### Phase 2: Add Modbus Support (If Needed)
1. Add eModbus library to platformio.ini
2. Set `SUPPORT_BYD_MODBUS = 1` (or whichever inverter)
3. Everything else works automatically

### Phase 3: Multi-Device Configuration
1. Create `receiver_config.h` (zero inverters, display only)
2. Receiver project uses different configuration
3. Share Battery Emulator library between projects with different configs

### Phase 4: Dual Batteries
1. Enable `datalayer.battery2`
2. Instantiate two BMS parsers
3. Extend ESP-NOW payload to include both batteries

---

## Summary

**Current Approach (Wrong):** "Exclude files X, Y, Z from compilation"
- Leaves header dependencies in place
- Linker still tries to resolve symbols
- Results in mysterious undefined reference errors

**Correct Approach:** "Configure which features to include via preprocessor"
- Headers explicitly guard optional includes
- Compiler never sees optional code paths
- Build succeeds cleanly
- Transmitter specifies its own capabilities

This aligns with the architectural principle: **"Preserve Battery Emulator's logic, adapt only file/build configuration to the new hardware structure."**
