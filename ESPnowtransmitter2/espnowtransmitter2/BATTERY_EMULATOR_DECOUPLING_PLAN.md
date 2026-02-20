# Battery Emulator Web UI Decoupling - Comprehensive Migration Plan

**Document Version:** 1.0  
**Date:** February 18, 2026  
**Status:** Pre-implementation Analysis  
**Scope:** Systematically decouple web UI from control logic for transmitter/receiver architecture

---

## Executive Summary

Battery Emulator 9.2.4 has **tightly coupled** web UI rendering logic embedded directly in the battery control code. This document provides a detailed, file-by-file decoupling strategy to:

1. **Keep** control logic and data parsing on the **Transmitter**
2. **Move** HTML rendering and visualization to the **Receiver**
3. **Maintain** the existing Battery Emulator architecture with minimal changes
4. **Execute** systematically, one component at a time

---

## Part 1: UI/Control Coupling Analysis

### 1.1 Coupling Points Identified

#### **Location 1: Battery Base Class Header**
- **File:** `battery/Battery.h`
- **Issue:** Includes `devboard/webserver/BatteryHtmlRenderer.h`
- **Method:** Virtual `get_status_renderer()` returns HTML renderer object
- **Impact:** Every battery type inherits this, forcing inclusion of webserver header

```cpp
// Current coupling in Battery.h
#include "devboard/webserver/BatteryHtmlRenderer.h"  // ← WEBSERVER DEPENDENCY

class Battery {
    virtual BatteryHtmlRenderer& get_status_renderer() = 0;  // ← UI METHOD
    // ... control logic
};
```

#### **Location 2: Each Battery Implementation**
- **Files:** `battery/PYLON-BATTERY.h`, `battery/NISSAN-LEAF-BATTERY.h`, etc. (50+ files)
- **Issue:** Each implements `get_status_renderer()` with battery-specific HTML
- **Pattern:** Return either default renderer or custom `BatteryHtmlRenderer` subclass
- **Impact:** Battery classes contain HTML generation code mixed with control logic

```cpp
// Current coupling in PYLON-BATTERY.h
class PylonBattery : public CanBattery {
    PylonBatteryHtmlRenderer renderer;  // ← HTML OBJECT EMBEDDED
    BatteryHtmlRenderer& get_status_renderer() override {
        return renderer;  // ← RETURNS HTML RENDERER
    }
    // ... BMS parsing logic
};
```

#### **Location 3: Battery HTML Implementation Files**
- **Files:** `battery/*-HTML.cpp` (50+ files, e.g., `BMW-I3-HTML.cpp`)
- **Issue:** Each battery type has dedicated HTML generation code
- **Content:** Renders HTML tables/divs with battery-specific status information
- **Impact:** Large code files that serve zero purpose on the transmitter

```cpp
// Current coupling in BMW-I3-HTML.cpp
String BmwI3BatteryHtmlRenderer::get_status_html() {
    String html = "<div>BMW i3 Status:</div>";
    html += "<div>Voltage: " + String(datalayer.battery.status.voltage_dV) + " dV</div>";
    // ... 100+ lines of HTML generation
    return html;
}
```

#### **Location 4: Webserver Code**
- **Files:** `devboard/webserver/advanced_battery_html.cpp`
- **Issue:** Calls battery methods to render UI
- **Code Pattern:** `battery->get_status_renderer().get_status_html()`
- **Impact:** Webserver cannot function without battery objects

```cpp
// Current coupling in advanced_battery_html.cpp
String advanced_battery_processor(const String& var) {
    if (battery) {
        content += battery->get_status_renderer().get_status_html();  // ← CALLS UI METHOD
        render_command_buttons(battery, 0);
    }
}
```

---

## Part 2: Decoupling Strategy

### 2.1 High-Level Architecture Change

**Before (Current - Tightly Coupled):**
```
┌──────────────────────────────────────────┐
│         Battery.h (abstract)             │
├──────────────────────────────────────────┤
│  #include BatteryHtmlRenderer.h          │
│  get_status_renderer() = 0               │
│  update_values()                         │
│  receive_can_frame()                     │
└──────────────────────────────────────────┘
    ▲
    │ inherits
    │
┌──────────────────────────────────────────┐
│      PylonBattery (concrete)             │
├──────────────────────────────────────────┤
│  PylonBatteryHtmlRenderer renderer;      │  ← HTML embedded
│  get_status_renderer() {                 │
│    return renderer;                      │
│  }                                       │
│  receive_can_frame(CAN_frame* frame) {   │  ← Control logic
│    // Parse Pylon protocol               │
│  }                                       │
└──────────────────────────────────────────┘
```

**After (Decoupled - Transmitter/Receiver):**

**Transmitter Side:**
```
┌──────────────────────────────────────────┐
│    Battery.h (abstract - NO UI)          │
├──────────────────────────────────────────┤
│  // NO BatteryHtmlRenderer.h include     │
│  // NO get_status_renderer() method      │
│  update_values()                         │
│  receive_can_frame()                     │
└──────────────────────────────────────────┘
    ▲
    │ inherits
    │
┌──────────────────────────────────────────┐
│   PylonBattery (control logic only)      │
├──────────────────────────────────────────┤
│  // NO renderer member                   │
│  receive_can_frame(CAN_frame* frame) {   │
│    // Parse Pylon protocol               │
│    // Update datalayer                   │
│  }                                       │
└──────────────────────────────────────────┘
```

**Receiver Side (NEW):**
```
┌──────────────────────────────────────────┐
│    BatteryUiRenderer (interface)         │
├──────────────────────────────────────────┤
│  // Takes battery type + datalayer subset│
│  String render_status_html(...)          │
│  String render_cell_voltages_html(...)   │
└──────────────────────────────────────────┘
    ▲
    │ implements
    │
┌──────────────────────────────────────────┐
│  PylonBatteryUiRenderer (concrete)       │
├──────────────────────────────────────────┤
│  String render_status_html() {           │
│    String html = "<div>...";             │
│    // Generate Pylon-specific UI         │
│    return html;                          │
│  }                                       │
└──────────────────────────────────────────┘
```

### 2.2 Implementation Approach

**Phase A: Prepare Transmitter**
1. Remove `BatteryHtmlRenderer` include from `Battery.h`
2. Remove `get_status_renderer()` method declaration from `Battery.h`
3. Remove all `*-HTML.cpp` files from transmitter build (srcFilter in lib_extra_dirs)
4. Rename/stub `BatteryHtmlRenderer.h` in transmitter build

**Phase B: Create Receiver UI Layer**
1. Copy `BatteryHtmlRenderer.h` to receiver
2. Create receiver UI rendering classes (one per battery type, or generic)
3. Implement `render_status_html()` for each battery type
4. Create webserver pages that call these renderers

**Phase C: Update Data Flow**
1. Transmitter sends datalayer snapshot via ESP-NOW (no UI data)
2. Receiver receives snapshot, updates local datalayer
3. Receiver renders UI using received datalayer + local UI renderers

---

## Part 3: File-by-File Decoupling Plan

### 3.1 Battery Header Files (50+ Files)

**Current Pattern:**
```cpp
// PYLON-BATTERY.h
#pragma once
#include "Battery.h"
#include "devboard/webserver/BatteryHtmlRenderer.h"  // ← REMOVE

class PylonBattery : public CanBattery {
    PylonBatteryHtmlRenderer renderer;               // ← REMOVE
    BatteryHtmlRenderer& get_status_renderer() override {  // ← REMOVE
        return renderer;
    }
    // Control logic (KEEP)
};
```

**Required Changes (All 50+ Battery Headers):**
- [ ] Remove `#include "devboard/webserver/BatteryHtmlRenderer.h"`
- [ ] Remove renderer member variable (`BatteryXyzRenderer renderer;`)
- [ ] Remove `get_status_renderer()` method declaration and implementation
- [ ] KEEP all control logic (BMS parsing, CAN communication)

**Files to Modify:**
1. `battery/Battery.h` - Base class
2. `battery/CanBattery.h` - Intermediate class
3. `battery/PYLON-BATTERY.h`
4. `battery/NISSAN-LEAF-BATTERY.h`
5. `battery/TESLA-MODEL-3-BATTERY.h`
6. ... (46 more battery types)

---

### 3.2 Battery Implementation Files (50+ CPP Files)

**Current Pattern:**
```cpp
// PYLON-BATTERY.cpp
#include "PYLON-BATTERY.h"
#include "devboard/webserver/BatteryHtmlRenderer.h"

PylonBattery::receive_can_frame(CAN_frame* frame) {
    // Parse CAN data
    // Update datalayer.battery.status
}
```

**Required Changes (All 50+ Battery CPP Files):**
- [ ] Remove `#include` statements for webserver/HTML files
- [ ] KEEP all BMS parsing logic
- [ ] KEEP CAN receive/transmit logic
- [ ] Remove any HTML generation code if present

---

### 3.3 Battery HTML Files (50+ Files - DELETE from Transmitter)

**Files Pattern:** `battery/*-HTML.cpp`, `battery/*-HTML.h`

**Examples:**
- `battery/BMW-I3-HTML.h`, `battery/BMW-I3-HTML.cpp`
- `battery/NISSAN-LEAF-HTML.h`, `battery/NISSAN-LEAF-HTML.cpp`
- ... (50+ files)

**Action on Transmitter:**
- [ ] **DELETE** these files from transmitter completely
- [ ] They will be COPIED to receiver and adapted for receiver UI

**Action on Receiver:**
- [ ] COPY these HTML files to receiver
- [ ] ADAPT them to work without battery object inheritance
- [ ] Create `BatteryUiRegistry` to map battery type → renderer

---

### 3.4 Core Battery Emulator Files (KEEP on Both)

**Files that DON'T CONTAIN UI CODE:**

**On Transmitter (KEEP):**
- `datalayer/datalayer.h` - Data container ✓
- `datalayer/datalayer_extended.h` - Extended data ✓
- `battery/Battery.h` (after UI removal)
- `battery/CanBattery.h` (after UI removal)
- All 50 battery .cpp files (control logic only)
- `battery/Shunt.h`, `battery/Shunt.cpp` ✓
- `inverter/INVERTERS.h`, `inverter/*.cpp` ✓
- `charger/CHARGERS.h`, `charger/*.cpp` ✓
- `communication/CAN/*` ✓
- `communication/contactorcontrol/*` ✓
- `communication/nvm/*` ✓
- `devboard/safety/*` ✓

**On Receiver (SUBSET):**
- `datalayer/datalayer.h` (subset - battery.status only)
- Battery UI renderers (newly created)
- Webserver UI pages (modified to use new renderers)

---

## Part 4: Implementation Sequence (Step-by-Step)

### **Step 1: Create Clean Battery.h (Transmitter)**
**File:** `src/battery_emulator/battery/Battery.h`

**Action:**
```cpp
// BEFORE
class Battery {
    virtual BatteryHtmlRenderer& get_status_renderer() = 0;  // REMOVE
    // ...
};

// AFTER
class Battery {
    // HTML method REMOVED
    // All control methods preserved
    virtual void update_values() = 0;
    virtual bool receive_can_frame(CAN_frame* frame) = 0;
    // ...
};
```

**Impact:** All 50 battery classes need this change
**Effort:** HIGH (50+ files to update)

---

### **Step 2: Remove HTML-Specific Methods from Concrete Batteries**

**Files:** `battery/PYLON-BATTERY.h`, etc. (50 files)

**Action for EACH:**
```cpp
// BEFORE
class PylonBattery : public CanBattery {
    PylonBatteryHtmlRenderer renderer;                      // REMOVE
    BatteryHtmlRenderer& get_status_renderer() override {   // REMOVE
        return renderer;
    }
    // ... control methods (KEEP)
};

// AFTER
class PylonBattery : public CanBattery {
    // HTML code REMOVED
    // ... control methods (KEEP)
};
```

**Impact:** All 50 battery class headers
**Effort:** HIGH (tedious but straightforward)

---

### **Step 3: Remove HTML Includes from Battery CPP Files**

**Files:** `battery/PYLON-BATTERY.cpp`, etc. (50 files)

**Action for EACH:**
```cpp
// BEFORE
#include "PYLON-BATTERY.h"
#include "devboard/webserver/BatteryHtmlRenderer.h"  // REMOVE
#include "devboard/webserver/PYLON-BATTERY-HTML.h"  // REMOVE

// AFTER
#include "PYLON-BATTERY.h"
// HTML includes REMOVED
```

**Impact:** All 50 battery cpp files
**Effort:** MEDIUM (find and remove includes)

---

### **Step 4: Delete HTML-Only Files from Transmitter Build**

**Files:** `battery/*-HTML.h`, `battery/*-HTML.cpp` (50+ files)

**Action:**
- [ ] Update `src/battery_emulator/library.json` (if used) OR `platformio.ini`
- [ ] Add srcFilter exclusion: `-<**/battery/*-HTML.*>`
- [ ] Alternatively, just don't copy these files to transmitter

**Impact:** Transmitter build excludes UI files
**Effort:** LOW (1-2 lines in build config)

---

### **Step 5: Stub BatteryHtmlRenderer for Transmitter (Build Compatibility)**

**File:** `src/battery_emulator/devboard/webserver/BatteryHtmlRenderer.h`

**Action:**
```cpp
// Minimal stub for transmitter (which doesn't use it)
#pragma once
#include <WString.h>

class BatteryHtmlRenderer {
 public:
  virtual String get_status_html() = 0;
};

class BatteryDefaultRenderer : public BatteryHtmlRenderer {
 public:
  String get_status_html() { return String(""); }  // Empty on transmitter
};
```

**Impact:** Allows code that may still reference the interface to compile (if any)
**Effort:** LOW (just a stub header)

---

### **Step 6: Create Receiver UI Renderer Base Class**

**File:** `espnowreciever_2/lib/webserver/battery_ui_renderers/BatteryUiRenderer.h` (NEW)

**Action:**
```cpp
#pragma once
#include <Arduino.h>

// Receiver-side UI rendering (NOT tied to battery object)
class BatteryUiRenderer {
 public:
  virtual ~BatteryUiRenderer() = default;
  
  // Pure virtual methods for UI rendering
  virtual String render_status_html(uint8_t battery_type, const void* datalayer_subset) = 0;
  virtual String render_cell_voltages_html(uint8_t battery_type, const void* datalayer_subset) = 0;
};
```

**Impact:** Receiver has new UI architecture
**Effort:** LOW (new file, ~50 lines)

---

### **Step 7: Create Receiver UI Renderers for Each Battery Type**

**Files:** Create new receiver library files
- `espnowreciever_2/lib/webserver/battery_ui_renderers/PylonBatteryUiRenderer.h`
- `espnowreciever_2/lib/webserver/battery_ui_renderers/PylonBatteryUiRenderer.cpp`
- ... (repeat for each battery type as needed)

**Action:**
```cpp
// PylonBatteryUiRenderer.cpp (Receiver)
String PylonBatteryUiRenderer::render_status_html(const BatteryStatus& status) {
    String html = "<div>Pylon LiFePO4 Status:</div>";
    html += "<div>Voltage: " + String(status.voltage_dV / 10.0) + " V</div>";
    html += "<div>Current: " + String(status.current_dA / 10.0) + " A</div>";
    html += "<div>SOC: " + String(status.reported_soc / 100.0) + " %</div>";
    return html;
}
```

**Impact:** Receiver can render UI for each battery type
**Effort:** HIGH (create files for each supported battery type)

---

### **Step 8: Update Receiver Webserver**

**Files:** `espnowreciever_2/lib/webserver/pages/battery_monitor_page.cpp`

**Action:**
```cpp
// BEFORE (calls battery->get_status_renderer())
if (battery) {
    content += battery->get_status_renderer().get_status_html();
}

// AFTER (uses receiver UI renderers)
if (battery_data_received) {
    BatteryUiRenderer* renderer = BatteryUiRegistry::get_renderer(battery_type);
    if (renderer) {
        content += renderer->render_status_html(battery_type, &datalayer_subset);
    }
}
```

**Impact:** Receiver webserver uses new architecture
**Effort:** MEDIUM (update multiple handler functions)

---

### **Step 9: Test Transmitter Build**

**Action:**
```bash
cd espnowtransmitter2
platformio run -e olimex_esp32_poe2
# Should compile without webserver/HTML errors
```

**Expected Result:**
- ✓ All battery control logic compiles
- ✓ No HTML/webserver dependencies
- ✓ Binary size smaller
- ✓ CAN communication functional

---

### **Step 10: Test Receiver Build**

**Action:**
```bash
cd espnowreciever_2
platformio run
# Receiver compiles with UI rendering capability
```

**Expected Result:**
- ✓ Receiver displays battery data correctly
- ✓ Web UI renders battery-specific information
- ✓ Can handle multiple battery types via UI registry

---

## Part 5: Challenges & Solutions

### Challenge 1: "What if a battery class still references HTML code?"

**Solution:**
- Compile transmitter after Step 4
- Fix any remaining #include errors in individual battery classes
- Add `#ifdef RECEIVER_BUILD` guards if needed (though not preferred)

### Challenge 2: "The HTML files use datalayer.battery.status fields directly"

**Solution:**
- Receiver UI renderers accept `BatteryStatus*` pointer (or subset structure)
- Receive datalayer snapshot from transmitter via ESP-NOW
- Renderers work with received data, not live objects

### Challenge 3: "Some batteries have complex HTML (BMW i3, Tesla, etc.)"

**Solution:**
- Start with simple battery types (Pylon, basic status)
- Complex types can use generic renderer initially
- Create specialized renderers later as needed

### Challenge 4: "Build system expects battery classes to have HTML methods"

**Solution:**
- If compilation fails, check where `.get_status_renderer()` is called
- On transmitter, it should NEVER be called (not needed)
- Remove those calls or guard them with `#ifndef TRANSMITTER_BUILD`

---

## Part 6: Risk Mitigation

### Risk: Breaking Existing Battery Emulator Code

**Mitigation:**
- Test on a branch first
- Compare original Battery Emulator vs. modified version
- Only remove UI code, leave control logic untouched

### Risk: Receiver UI Doesn't Match Original Battery Emulator

**Mitigation:**
- Copy HTML generation logic directly from original files
- Test with same datalayer input values
- Keep original aesthetic/layout

### Risk: Missing Battery Types in Receiver UI

**Mitigation:**
- Create generic `DefaultBatteryUiRenderer` for unsupported types
- Add more specific types as needed
- Use battery type enum + registry lookup

---

## Part 7: Success Criteria

### Transmitter Build:
- [ ] Compiles without webserver errors
- [ ] Binary runs and receives CAN messages
- [ ] Sends datalayer snapshot via ESP-NOW every 100ms
- [ ] Inverter control still works
- [ ] Safety monitoring still works
- [ ] No HTML/UI code in transmitter binary

### Receiver Display:
- [ ] Receives ESP-NOW packets from transmitter
- [ ] Displays battery status (SOC, power, voltage)
- [ ] Renders battery-type-specific UI
- [ ] Webserver pages load correctly
- [ ] All UI elements display correct data

### Code Quality:
- [ ] No coupling between battery control and UI rendering
- [ ] Transmitter and Receiver are independently compilable
- [ ] Clean separation of concerns
- [ ] Minimal code duplication

---

## Summary: Execution Order

1. ✅ Analyze coupling (DONE - this document)
2. ⏳ **NEXT:** Remove HTML methods from `Battery.h`
3. ⏳ **NEXT:** Remove HTML members from all 50 battery classes
4. ⏳ **NEXT:** Remove HTML includes from all 50 battery implementations
5. ⏳ **NEXT:** Exclude `*-HTML.cpp` files from transmitter build
6. ⏳ **NEXT:** Test transmitter compilation
7. ⏳ **NEXT:** Create receiver UI renderer base class
8. ⏳ **NEXT:** Implement receiver UI renderers (start with Pylon, generic fallback)
9. ⏳ **NEXT:** Update receiver webserver to use new renderers
10. ⏳ **NEXT:** Test receiver display
11. ⏳ **NEXT:** Validate end-to-end data flow

---

## Estimated Effort

| Task | Files | Complexity | Time |
|------|-------|-----------|------|
| Remove HTML from base classes | 2 | HIGH | 2h |
| Remove HTML from battery headers | 50 | MEDIUM | 6h |
| Remove HTML from battery CPP | 50 | MEDIUM | 4h |
| Exclude HTML files from build | 1 | LOW | 0.5h |
| Test transmitter | 1 | MEDIUM | 1h |
| Create receiver UI renderers | 5-10 | HIGH | 8h |
| Update receiver webserver | 3 | MEDIUM | 2h |
| Test receiver | 1 | MEDIUM | 1h |
| Validate end-to-end | 1 | MEDIUM | 2h |
| **TOTAL** | | | **~26.5h** |

---

## Next Steps

1. **Approve this plan** - Does it align with your vision?
2. **Identify priority battery types** - Which types must work first? (e.g., Pylon for receiver UI)
3. **Start with Step 1-4** - Clean up Battery Emulator for transmitter
4. **Validate transmitter compilation** - Ensure control logic intact
5. **Then move to Steps 5-10** - Receiver UI implementation

---

**Document Status:** Ready for implementation
**Last Updated:** February 18, 2026
