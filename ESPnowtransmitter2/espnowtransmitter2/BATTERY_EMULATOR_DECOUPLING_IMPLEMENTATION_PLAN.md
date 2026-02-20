# Battery Emulator UI Decoupling - Detailed Step-by-Step Plan

## Overview

This plan decouples the Battery Emulator's web UI display logic from the core control logic, allowing:
- **TRANSMITTER** (Olimex ESP32-POE2): Runs COMPLETE Battery Emulator core (all parsers, control, safety)
- **RECEIVER** (LilyGo T-Display-S3): Gets DISPLAY-ONLY components (HTML renderers, webserver UI)

**Key Principle**: No control logic changes. Only UI is relocated.

---

## Part A: Analysis & Verification Phase

### Step A1: Verify Clean Separation of Concerns

**Objective**: Confirm that HTML files contain ONLY display code, ZERO control logic.

**Status**: ✓ VERIFIED
- HTML files are pure display renderers
- Battery control files are pure control logic
- Clean separation exists

---

## Part B: Transmitter Preparation (Decoupling)

### Step B1: Create Stub HTML Renderer for Transmitter

**File to Create**: `src/battery_emulator/devboard/webserver/BatteryHtmlRenderer_Stub.h`

```cpp
#ifndef _BATTERY_HTML_RENDERER_STUB_H
#define _BATTERY_HTML_RENDERER_STUB_H

#include <Arduino.h>

// STUB: Used by transmitter to avoid compiling actual webserver
// Receiver will use full BatteryHtmlRenderer.h with real implementations

class BatteryHtmlRenderer {
public:
    virtual ~BatteryHtmlRenderer() = default;
    virtual String get_status_html() { return ""; }
};

class BatteryDefaultRenderer : public BatteryHtmlRenderer {
public:
    String get_status_html() override { return ""; }
};

#endif
```

---

### Step B2-B10: Delete All HTML and Webserver Files from Transmitter

**Files to DELETE**:
- All `battery/*-HTML.h` (50+ files)
- All `battery/*-HTML.cpp` (50+ files)
- Entire `devboard/webserver/` directory
- Entire `devboard/display/` directory
- `devboard/mqtt/mqtt.cpp`

**Files to MODIFY**:
- `battery/Battery.h`: Remove `#include "devboard/webserver/BatteryHtmlRenderer.h"`
- All `battery/*-BATTERY.h`: Remove `#include "*-HTML.h"` and remove renderer member variables

---

## Part C: Receiver Gets Display Components

**Copy to Receiver**:
- All `battery/*-HTML.h` files
- All `battery/*-HTML.cpp` files
- All `devboard/webserver/` files
- All `devboard/display/` files

**Create on Receiver**:
```cpp
// BatteryRendererFactory.h - selects correct renderer based on battery type
class BatteryRendererFactory {
    static BatteryHtmlRenderer* createRenderer(BatteryType type) {
        // Factory implementation using battery type to instantiate correct renderer
    }
};
```

---

## Summary of Changes

| Component | Current State | Transmitter | Receiver |
|-----------|---------------|------------|----------|
| Battery control (50+ types) | Coupled with HTML | KEEP | DELETE |
| Battery HTML renderers (50+ pairs) | Included in battery headers | DELETE | ADD |
| Webserver infrastructure | In battery emulator | DELETE | ADD |
| Display drivers | In battery emulator | DELETE | ADD |
| MQTT telemetry publisher | Real-time data publishing | **KEEP** | DELETE |
| Safety monitoring | In control | KEEP | DELETE |
| CAN drivers | In control | KEEP | DELETE |
| Inverter drivers | In control | KEEP | DELETE |

This is the complete understanding needed to proceed with the decoupling. Ready to start implementation when you approve.

