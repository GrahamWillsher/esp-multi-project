# Receiver Hardware Abstraction Layer Analysis
## ESP32 T-Display-S3 Hardware Configuration Review

**Date:** February 25, 2026  
**Scope:** Hardware pin definitions, TFT_eSPI configuration, and potential conflicts  
**Findings Level:** ⚠️ IMPORTANT - Multiple architectural inconsistencies identified

---

## Executive Summary

The receiver codebase uses a **two-tier hardware definition system** that creates potential conflicts and maintenance issues:

1. **Hard-coded pin constants** in `src/common.h` (Display namespace)
2. **TFT_eSPI build flags** in `platformio.ini`

Both systems attempt to configure the same hardware pins for the ST7789 display, creating:
- **Risk of inconsistency** between compile-time definitions
- **Poor maintainability** (GPIO definitions scattered across files)
- **No centralized hardware configuration**
- **Incomplete pin coverage** (some pins only in build flags)

---

## Hardware Structure Analysis

### 1. Pin Definition Locations

#### **Location A: src/common.h (Display Namespace)**
```cpp
namespace Display {
    // Pin definitions
    constexpr uint8_t PIN_POWER_ON = 15;  // Display power control
    constexpr uint8_t PIN_LCD_BL = 38;    // Backlight (PWM control)
    
    // Display dimensions
    constexpr int SCREEN_WIDTH = 320;
    constexpr int SCREEN_HEIGHT = 170;
}
```

**Used by:**
- `src/display/display_core.cpp` - GPIO initialization & backlight PWM
- `src/display/display_led.cpp` - Backlight control
- `src/display/display_splash.cpp` - Fade animations

#### **Location B: platformio.ini (Build Flags)**
```ini
; TFT_eSPI configuration for ST7789 controller
-D TFT_DC=7           ; Data/Command pin
-D TFT_RST=5          ; Reset pin
-D TFT_CS=6           ; Chip Select
-D TFT_WR=8           ; Write pin
-D TFT_RD=9           ; Read pin
-D TFT_D0=39  through -D TFT_D7=48  ; 8-bit parallel data bus (8 pins)
-D TFT_BL=38          ; Backlight (DUPLICATE definition with common.h!)
-D TFT_BACKLIGHT_ON=HIGH  ; Backlight active state
```

**Consumed by:**
- TFT_eSPI library (during initialization)
- Hardware initialization macros

---

## Critical Issues Identified

### ⚠️ **Issue #1: DUPLICATE BACKLIGHT PIN DEFINITION**

**Conflict:** GPIO 38 is defined in TWO places with different contexts:

| Location | Definition | Purpose | Context |
|----------|-----------|---------|---------|
| `src/common.h` | `PIN_LCD_BL = 38` | App-level backlight control | Local display control |
| `platformio.ini` | `-D TFT_BL=38` | TFT_eSPI library initialization | Library hardware abstraction |

**Risk:** If a developer changes one definition and forgets the other, the build could compile but fail at runtime with:
- Incorrect backlight initialization
- PWM channel configuration mismatches
- Silent hardware failures

**Current Status:** ✅ Values match (both = 38), but **no mechanism to prevent divergence**

---

### ⚠️ **Issue #2: INCOMPLETE HARDWARE ABSTRACTION**

**Missing Pin Definitions in src/common.h:**

| Pin | GPIO | Function | Defined Where | Status |
|-----|------|----------|--------|--------|
| DC (Data/Command) | 7 | TFT control | platformio.ini only | ⚠️ MISSING |
| RST (Reset) | 5 | TFT control | platformio.ini only | ⚠️ MISSING |
| CS (Chip Select) | 6 | TFT control | platformio.ini only | ⚠️ MISSING |
| WR (Write) | 8 | TFT control | platformio.ini only | ⚠️ MISSING |
| RD (Read) | 9 | TFT control | platformio.ini only | ⚠️ MISSING |
| D0-D7 (Data Bus) | 39-48 | 8-bit parallel data | platformio.ini only | ⚠️ MISSING |
| BL (Backlight) | 38 | Backlight PWM | Both (see Issue #1) | ✅ REDUNDANT |
| POWER_ON | 15 | Display power | src/common.h only | ⚠️ MISSING from build flags |

**Problem:** Code has no programmatic access to TFT control pins, making it impossible to:
- Validate pin assignments at compile-time
- Create comprehensive pin conflict checks
- Document hardware mapping in code
- Support alternative board variants

---

### ⚠️ **Issue #3: PLATFORM-SPECIFIC CONFIGURATION LEAK**

**Build Flag Issue:** platformio.ini mixes TFT_eSPI library configuration with board-specific constants:

```ini
-D TFT_WIDTH=170          ; Display dimension (should be named TFT_HEIGHT for orientation)
-D TFT_HEIGHT=320         ; Display dimension (should be named TFT_WIDTH for orientation)
```

**Problem:**
- Width/Height are swapped due to `setRotation(1)` in display_core.cpp (line 20)
- Comments in platformio.ini don't explain the rotation dependency
- If someone changes TFT_WIDTH/TFT_HEIGHT without adjusting rotation, display renders incorrectly
- **No validation** that TFT dimensions match actual device capabilities

---

### ⚠️ **Issue #4: SCATTERED HARDWARE INITIALIZATION**

Hardware initialization logic split across multiple files:

| File | Responsibility | Line | Status |
|------|-----------------|------|--------|
| `src/common.h` | Pin constant definitions | 64-65 | Global state |
| `src/display/display_core.cpp` | GPIO setup, backlight PWM | 13-41 | Hardcoded IDF version checks |
| `src/display/display_splash.cpp` | Backlight fade control | 134, 152 | Uses hardcoded PWM channel 0 |
| `src/display/display_led.cpp` | LED rendering (not GPIO) | - | Uses calculated positions |
| `src/main.cpp` | Orchestration | 59 | Calls init_display() |

**No HAL abstraction layer** = Multiple places where GPIO assumptions are embedded

---

### ⚠️ **Issue #5: IDF VERSION COMPATIBILITY CHECK EMBEDDED IN APP CODE**

**Location:** src/display/display_core.cpp lines 36-41

```cpp
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcSetup(0, 2000, 8);              // Old ESP-IDF API
    ledcAttachPin(Display::PIN_LCD_BL, 0);
#else
    ledcAttach(Display::PIN_LCD_BL, 200, 8);  // New ESP-IDF API
#endif
```

**Problems:**
- Version-specific code scattered in application logic
- Duplicated in multiple files (display_core.cpp, display_splash.cpp)
- No centralized compatibility layer
- Difficult to test and maintain
- PWM frequency hardcoded (2000 Hz vs 200 Hz - why the difference?)

---

### ⚠️ **Issue #6: NO PIN CONFLICT DETECTION**

**Current Scenario:**
- GPIO 15 (POWER_ON) - Power control
- GPIO 38 (BL) - Backlight PWM
- GPIO 7, 5, 6, 8, 9 - TFT control (DC, RST, CS, WR, RD)
- GPIO 39-48 - 8-bit parallel data bus

**No mechanism to:**
- ✗ Detect GPIO sharing between modules
- ✗ Validate against ESP32-S3 pin capabilities
- ✗ Check for USB/JTAG pin conflicts
- ✗ Verify SPI/I2C bus availability

---

## Hardware Specification Reference

### LilyGo T-Display-S3 Pin Assignment

**Display Interface (ST7789 8-bit parallel):**
- Data Bus (8-bit): GPIO 39-48
- Control Signals: GPIO 5, 6, 7, 8, 9
- Backlight (PWM): GPIO 38
- Power: GPIO 15

**Other on-board features:**
- USB-Serial: GPIO 43, 44 (strap pin control)
- SD Card (if populated): GPIO 1, 2, 14, 17
- Button: GPIO 0 (BOOT button)

**ESP32-S3 considerations:**
- GPIO 46 has on-board pull-down (strapping pin - affects boot mode)
- GPIO 0 has on-board pull-up (BOOT button)
- PSRAM: GPIO 33, 34 (built-in)

---

## Best Practice Violations

### 1. **Hardware Abstraction Not Enforced**
❌ Should have a dedicated `hardware_config.h` in a `hal/` directory  
✅ Single source of truth for all GPIO assignments

### 2. **Build Flags Mixed with Code**
❌ TFT_eSPI configuration buried in platformio.ini  
✅ Should reference external header or config file

### 3. **Version-Specific Code in Application**
❌ IDF compatibility checks scattered across display code  
✅ Should be isolated in a compatibility layer

### 4. **No Runtime Validation**
❌ No checks for pin conflicts or invalid assignments  
✅ Should validate at boot time

### 5. **Documentation Gaps**
❌ No pinout diagram or hardware mapping document  
✅ Should have visual reference and verification script

---

## Recommendations for Improvement

### **Priority 1: Create Hardware Abstraction Layer (HAL)**

Create new file: `src/hal/hardware_config.h`

```cpp
#pragma once

/**
 * LilyGo T-Display-S3 Hardware Configuration
 * Single source of truth for all GPIO assignments
 */

namespace HardwareConfig {
    // Display power and backlight
    constexpr uint8_t GPIO_DISPLAY_POWER = 15;
    constexpr uint8_t GPIO_BACKLIGHT = 38;
    
    // TFT_eSPI control pins (ST7789)
    constexpr uint8_t GPIO_TFT_DC = 7;
    constexpr uint8_t GPIO_TFT_RST = 5;
    constexpr uint8_t GPIO_TFT_CS = 6;
    constexpr uint8_t GPIO_TFT_WR = 8;
    constexpr uint8_t GPIO_TFT_RD = 9;
    
    // TFT_eSPI data bus (8-bit parallel)
    constexpr uint8_t GPIO_TFT_D0 = 39;
    constexpr uint8_t GPIO_TFT_D1 = 40;
    // ... D2-D7 ...
    constexpr uint8_t GPIO_TFT_D7 = 48;
    
    // Display dimensions (ST7789)
    constexpr uint16_t DISPLAY_WIDTH = 320;
    constexpr uint16_t DISPLAY_HEIGHT = 170;
    constexpr uint8_t DISPLAY_ROTATION = 1;  // Landscape
    
    // Backlight PWM
    constexpr uint16_t BACKLIGHT_FREQUENCY_HZ = 200;
    constexpr uint8_t BACKLIGHT_RESOLUTION_BITS = 8;
    
    // Validation
    static_assert(GPIO_DISPLAY_POWER != GPIO_BACKLIGHT, "GPIO conflict: power and backlight");
    // Add more conflict checks...
}
```

**Implementation:**
1. Update `platformio.ini` to reference this header
2. Replace all hardcoded GPIO references with `HardwareConfig::GPIO_*`
3. Remove duplicate definitions from `src/common.h`

---

### **Priority 2: Create Display Initialization HAL**

Create new file: `src/hal/display_hal.h`

```cpp
#pragma once

namespace DisplayHAL {
    // Version-agnostic backlight PWM setup
    void setupBacklightPWM(uint8_t gpio, uint16_t frequency, uint8_t resolution);
    void setBacklightBrightness(uint8_t level);  // 0-255
    
    // Display power control
    void powerOn();
    void powerOff();
    
    // TFT initialization validation
    bool validateHardwarePins();  // Check for conflicts
    void initializeTFT();
}
```

**Benefits:**
- Centralizes ESP-IDF version compatibility
- Single point for PWM frequency/resolution management
- Enables hardware validation at boot
- Decouples display code from IDF version

---

### **Priority 3: Add Hardware Validation**

Create new file: `src/hal/hardware_validator.h`

```cpp
#pragma once

namespace HardwareValidator {
    /**
     * Run at boot to detect hardware configuration errors
     * @return true if all checks pass, false otherwise
     */
    bool validateConfiguration();
    
    // Individual checks
    bool checkPinConflicts();           // No GPIO used twice
    bool checkTFTDimensions();          // Width/height match rotation
    bool checkBacklightPWM();           // PWM channel available
    bool checkReservedPins();           // No reserved pin usage
    bool validateUSBSerialPins();       // USB pins not conflicting
}
```

---

### **Priority 4: Document Hardware Mapping**

Create new file: `HARDWARE_PINOUT.md`

```markdown
# LilyGo T-Display-S3 Hardware Pinout

## Display Interface (ST7789 Controller)

| Function | GPIO | Purpose | Notes |
|----------|------|---------|-------|
| Power Enable | 15 | Display power rail | Active HIGH |
| Backlight | 38 | Display brightness | PWM 200Hz, 8-bit |
| DC (Data/Cmd) | 7 | TFT control | Active HIGH |
| Reset | 5 | TFT reset | Active LOW |
| Chip Select | 6 | TFT chip select | Active LOW |
| Write Enable | 8 | Parallel write strobe | Active LOW |
| Read Enable | 9 | Parallel read strobe | Active LOW |
| D0-D7 | 39-48 | 8-bit parallel data | MSB first |

## Display Specifications

- **Controller:** ST7789 (320×170 pixels)
- **Interface:** 8-bit parallel (not SPI)
- **Color Depth:** 16-bit RGB565
- **Rotation:** Landscape (PCLK edge: rising, RGB order: BGR)
```

---

### **Priority 5: Single HAL File, No Build Flags**

**Goal:** Move *all* display hardware configuration into a single HAL header and remove **all TFT-related build flags** from `platformio.ini`.

**Recommended approach (single-file HAL, no build flags):**
1) Create `src/hal/hardware_config.h` as the **only** source of truth for GPIOs, display dimensions, and rotation.
2) Create a project-local TFT_eSPI `User_Setup.h` that **includes** `hardware_config.h` and defines the required TFT_eSPI macros.
3) Remove all TFT pin macros from `platformio.ini` (and remove duplicate GPIO constants from `src/common.h`).

**Why this still works without build flags:**
TFT_eSPI reads its configuration from `User_Setup.h` at compile time. If you provide a project-local `User_Setup.h` (in your codebase) and ensure it is the one the library uses, you no longer need any `-D TFT_*` build flags.

**Example `src/hal/tft_espi_user_setup.h`:**
```cpp
#pragma once
#include "hardware_config.h"

#define ST7789_DRIVER 1
#define TFT_PARALLEL_8_BIT 1
#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERSION_ON 1

#define TFT_WIDTH  HardwareConfig::DISPLAY_WIDTH
#define TFT_HEIGHT HardwareConfig::DISPLAY_HEIGHT
#define TFT_DC     HardwareConfig::GPIO_TFT_DC
#define TFT_RST    HardwareConfig::GPIO_TFT_RST
#define TFT_CS     HardwareConfig::GPIO_TFT_CS
#define TFT_WR     HardwareConfig::GPIO_TFT_WR
#define TFT_RD     HardwareConfig::GPIO_TFT_RD
#define TFT_D0     HardwareConfig::GPIO_TFT_D0
#define TFT_D1     HardwareConfig::GPIO_TFT_D1
#define TFT_D2     HardwareConfig::GPIO_TFT_D2
#define TFT_D3     HardwareConfig::GPIO_TFT_D3
#define TFT_D4     HardwareConfig::GPIO_TFT_D4
#define TFT_D5     HardwareConfig::GPIO_TFT_D5
#define TFT_D6     HardwareConfig::GPIO_TFT_D6
#define TFT_D7     HardwareConfig::GPIO_TFT_D7
#define TFT_BL     HardwareConfig::GPIO_BACKLIGHT
```

**Project integration (no build flags):**
- Place the custom `User_Setup.h` in the TFT_eSPI search path used by the project (e.g., a local `lib/TFT_eSPI/User_Setup.h` override).
- Ensure the library uses that file by default (remove `USER_SETUP_LOADED` and all `TFT_*` defines from `platformio.ini`).

**Cleanup after migration:**
- Remove `PIN_POWER_ON`, `PIN_LCD_BL`, and any other display GPIO constants from `src/common.h`.
- Replace all direct pin references in code with `HardwareConfig::` values.
- Keep only non-hardware global display state in `src/common.h` (colors, dimensions, state variables).

---

## Implementation Priority

| Priority | Task | Impact | Effort |
|----------|------|--------|--------|
| 1 | Create `hardware_config.h` | Centralize definitions, enable validation | 2 hours |
| 2 | Create `display_hal.h` | Isolate compatibility code | 3 hours |
| 3 | Create `hardware_validator.h` | Boot-time error detection | 2 hours |
| 4 | Document pinout | Knowledge preservation | 1 hour |
| 5 | Refactor build config | Reduce duplication | 1 hour |
| **Total** | | **Eliminate HAL fragmentation** | **9 hours** |

---

## Current Risk Assessment

| Risk | Severity | Likelihood | Mitigation |
|------|----------|------------|-----------|
| GPIO conflict on refactoring | High | Medium | Implement Priority 3 validator |
| Silent backlight failure | Medium | Low | Add Priority 2 HAL |
| Rotation/dimension mismatch | Medium | Low | Add Priority 3 validator |
| IDF version incompatibility | Low | Low | Implement Priority 2 HAL |
| Maintenance burden | Medium | High | Implement Priority 1 (hardware_config.h) |

---

## Code Smell Indicators

```
🚩 Magic numbers (GPIO pins) embedded in multiple files
🚩 Duplicate definitions (GPIO 38 in two places)
🚩 Build flags doing work that code should do
🚩 Version-specific code in application logic
🚩 No runtime validation of hardware assumptions
🚩 No centralized documentation of hardware mapping
```

---

## Conclusion

The receiver's hardware abstraction is **functionally working** but **architecturally fragmented**. The current approach creates maintenance risk and makes it difficult to:
- Support alternative board variants
- Debug hardware configuration issues
- Validate pin assignments
- Document hardware dependencies

**Recommendation:** Implement Priority 1 (create `hardware_config.h`) immediately as a minimal breaking change. This consolidates definitions and enables future validation.

The remaining priorities should be completed incrementally to build a proper HAL layer that separates hardware concerns from application logic.

---

**Report Generated:** February 25, 2026  
**Files Analyzed:** 8 source files, 1 build configuration  
**Total Issues Found:** 6 architectural patterns requiring attention
