# Battery Emulator Architecture - Comprehensive Analysis

## 1. Executive Summary

The Battery Emulator codebase has **two distinct logical layers** that are currently **tightly coupled**:

1. **CONTROL LAYER** (Stays on Transmitter)
   - Battery parsing and state management (BMS logic)
   - Inverter control protocols
   - Charger management
   - Contactor/precharge/safety logic
   - Datalayer (real-time state)
   - CAN communication
   - **MQTT real-time telemetry publishing** (external monitoring)

2. **DISPLAY LAYER** (Moves to Receiver)
   - HTML renderers for battery-specific UI
   - Webserver infrastructure
   - Web UI pages and scripts
   - Settings persistence (NVM - but transmitter keeps it, receiver queries it)

**Current Problem**: Battery control classes INCLUDE HTML renderer headers and implement rendering methods.

---

## 2. Directory Structure & Coupling Analysis

```
Software/src/
├── battery/                          [SPLIT NEEDED]
│   ├── Battery.h                     [COUPLED: includes BatteryHtmlRenderer.h]
│   ├── CanBattery.h                  [CONTROL ONLY]
│   ├── BMW-I3-BATTERY.h             [COUPLED: get_status_renderer() method]
│   ├── BMW-I3-HTML.h                [DISPLAY ONLY: HtmlRenderer impl]
│   ├── PYLON-BATTERY.h              [COUPLED: get_status_renderer() method]
│   ├── PYLON-BATTERY-HTML.h         [DISPLAY ONLY: HtmlRenderer impl]
│   └── ... (50+ battery types, all coupled)
│
├── inverter/                         [CONTROL ONLY]
│   ├── INVERTERS.h
│   ├── Inverter protocol implementations
│   └── ... (15+ inverter types)
│
├── charger/                          [CONTROL ONLY]
│   └── Charger implementations
│
├── communication/                    [SPLIT NEEDED]
│   ├── can/                         [CONTROL ONLY: CAN drivers]
│   ├── contactorcontrol/            [CONTROL ONLY: contactor logic]
│   ├── precharge_control/           [CONTROL ONLY: precharge logic]
│   ├── equipmentstopbutton/         [CONTROL ONLY]
│   ├── nvm/                         [CONTROL + PARTIAL DISPLAY]
│   └── rs485/                       [CONTROL ONLY]
│
├── datalayer/                        [CONTROL ONLY]
│   ├── datalayer.h                  [Real-time battery state]
│   ├── datalayer_extended.h         [Extended state for webserver]
│   └── ... (state management)
│
├── devboard/                         [SPLIT NEEDED]
│   ├── safety/                      [CONTROL ONLY: safety limits]
│   ├── hal/                         [HARDWARE SPECIFIC]
│   ├── utils/                       [MIXED]
│   ├── webserver/                   [DISPLAY ONLY]
│   │   ├── BatteryHtmlRenderer.h   [DISPLAY INTERFACE]
│   │   ├── advanced_battery_html.cpp [DISPLAY: calls get_status_renderer()]
│   │   ├── settings_html.cpp        [DISPLAY: settings UI]
│   │   └── ... (webserver pages)
│   ├── display/                     [DISPLAY ONLY]
│   ├── mqtt/                        [MIXED: telemetry]
│   ├── wifi/                        [NETWORK: either]
│   ├── debug/                       [DEBUG: either]
│   └── sdcard/                      [DEBUG: either]
│
└── system_settings.h                [CONTROL + DISPLAY]
```

---

## 3. Coupling Points (CRITICAL)

### 3.1 Battery Base Class Coupling

**File**: `battery/Battery.h`

```cpp
#include "../../src/devboard/webserver/BatteryHtmlRenderer.h"  // <- DISPLAY DEPENDENCY

class Battery {
    virtual BatteryHtmlRenderer& get_status_renderer() { 
        return defaultRenderer; 
    }
    
private:
    BatteryDefaultRenderer defaultRenderer;  // <- Renderer instance
};
```

**Impact**: Every battery implementation must handle rendering even if not displayed.

**Solution**: Move to OPTIONAL interface, provide stub for transmitter.

---

### 3.2 Individual Battery Coupling (Example: BMW-I3)

**File**: `battery/BMW-I3-BATTERY.h`

```cpp
#include "BMW-I3-HTML.h"  // <- Direct HTML renderer inclusion

class BmwI3Battery : public CanBattery {
    BatteryHtmlRenderer& get_status_renderer() { 
        return renderer; 
    }
    
private:
    BmwI3HtmlRenderer renderer;  // <- Concrete renderer
};
```

**Pattern**: ALL 50+ battery implementations follow this pattern.

**Solution**: Remove `-HTML.h` includes, provide empty renderer for transmitter.

---

### 3.3 Webserver Calling Rendering

**File**: `devboard/webserver/advanced_battery_html.cpp`

```cpp
content += battery->get_status_renderer().get_status_html();  // <- Calls rendering
```

**Impact**: Webserver expects batteries to provide rendering.

**Solution**: Receiver implements alternate rendering interface; transmitter provides stub.

---

### 3.4 System Settings Coupling

**File**: `system_settings.h` / `communication/nvm/`

```cpp
// Used by BOTH:
// - Transmitter: to store and manage settings
// - Receiver: to display/modify settings
struct BatteryEmulatorSettings {
    // Battery config
    // Inverter config
    // CAN config
    // etc.
};
```

**Solution**: Transmitter keeps full settings; receiver queries via config sync message.

---

## 4. File Classification Matrix

| Component | Location | Transmitter | Receiver | Coupling | Action |
|-----------|----------|-------------|----------|----------|--------|
| Battery base class | battery/Battery.h | KEEP | - | HIGH | Remove HTML include, provide stub |
| Battery implementations (50+) | battery/*-BATTERY.h | KEEP | - | HIGH | Remove `get_status_renderer()` impl |
| Battery HTML renderers | battery/*-HTML.h/.cpp | REMOVE | ADD TO RX | HIGH | Delete from TX, move to RX |
| CanBattery | battery/CanBattery.h | KEEP | - | NONE | Keep as-is |
| Shunt classes | battery/Shunt.h | KEEP | - | LOW | Keep as-is |
| Inverter protocol drivers | inverter/ | KEEP | - | NONE | Keep as-is |
| Charger protocol drivers | charger/ | KEEP | - | NONE | Keep as-is |
| CAN drivers | communication/can/ | KEEP | - | NONE | Keep as-is |
| Contactor control | communication/contactorcontrol/ | KEEP | - | NONE | Keep as-is |
| Precharge logic | communication/precharge_control/ | KEEP | - | NONE | Keep as-is |
| Safety monitoring | devboard/safety/ | KEEP | - | NONE | Keep as-is |
| NVM settings | communication/nvm/ | KEEP | - | MEDIUM | Keep; receiver queries state |
| Datalayer | datalayer/ | KEEP | - | NONE | Keep as-is |
| Webserver | devboard/webserver/ | REMOVE | ADD TO RX | HIGH | Delete from TX, move to RX |
| Display drivers | devboard/display/ | REMOVE | ADD TO RX | HIGH | Delete from TX, move to RX |
| MQTT telemetry | devboard/mqtt/ | KEEP | - | NONE | Real-time data publishing; independent of webserver |
| HAL abstraction | devboard/hal/ | KEEP (Olimex) | KEEP (LilyGo) | HIGH | Platform-specific; keep both separate |
| WiFi/Ethernet | devboard/wifi/ | KEEP (Ethernet) | KEEP (WiFi) | MEDIUM | Platform-specific; keep separate |
| Utilities | devboard/utils/ | KEEP | PARTIAL | MEDIUM | Keep safe utilities, remove display |
| MQTT broker | devboard/mqtt/ | REMOVE | KEEP | HIGH | Delete from TX (client only), keep on RX |

---

## 5. Transmitter (Keep in Compile)

```
✓ battery/
  ✓ Battery.h (without HTML renderer include)
  ✓ CanBattery.h
  ✓ Shunt.h
  ✓ All 50+ battery implementations
    ✓ *-BATTERY.h/.cpp (CONTROL CODE ONLY)
    ✗ *-HTML.h/.cpp (DELETE - DISPLAY ONLY)
  
✓ inverter/
  ✓ All inverter protocol drivers
  
✓ charger/
  ✓ All charger protocol drivers
  
✓ communication/
  ✓ can/ (CAN drivers)
  ✓ contactorcontrol/
  ✓ precharge_control/
  ✓ equipmentstopbutton/
  ✓ nvm/ (settings management)
  ✓ rs485/ (Modbus support)
  
✓ datalayer/ (real-time state)

✓ devboard/
  ✓ safety/ (safety limits)
  ✓ hal/hw_olimex_esp32_poe2.cpp (Olimex HAL only)
  ✓ utils/ (except display-related)
  ✗ webserver/ (DELETE - DISPLAY ONLY)
  ✗ display/ (DELETE - DISPLAY ONLY)
  ✓ mqtt/ (KEEP - publishes real-time telemetry, independent of webserver)
  ✓ wifi/ (Ethernet driver actually)
  ✓ debug/
  ✓ sdcard/
  
✓ system_settings.h (keep full)
```

---

## 6. Receiver (Add to Compile)

```
FROM Battery Emulator:
✓ battery/
  ✓ Battery.h (display version with HTML renderer interface)
  ✗ CanBattery.h (not needed - no CAN)
  ✗ Shunt.h (not needed - no hardware)
  ✓ All 50+ *-HTML.h/.cpp (DISPLAY RENDERERS ONLY)
  ✗ *-BATTERY.h/.cpp (DELETE except for type definitions)
  
✗ inverter/ (DELETE - no control)
✗ charger/ (DELETE - no control)
✗ communication/ (mostly DELETE)
  ✗ can/ (DELETE)
  ✗ contactorcontrol/ (DELETE)
  ✗ nvm/ (DELETE - read-only from transmitter)
  
✗ datalayer/ (DELETE - receives via ESP-NOW)

✓ devboard/
  ✓ webserver/ (DISPLAY)
  ✓ display/ (TFT driver)
  ✓ mqtt/ (MQTT client for web UI)
  ✓ hal/hw_lilygo_t_display_s3.cpp (LilyGo HAL only)
  ✓ utils/ (display-safe utilities)
  ✗ safety/ (DELETE)
  ✗ sdcard/ (not needed)
  
✓ system_settings.h (read-only version for display)
```

---

## 7. Decoupling Strategy (Step-by-Step)

### Phase 1: Stub the HTML Renderer Interface on Transmitter

1. In `battery/Battery.h`:
   - Remove `#include "devboard/webserver/BatteryHtmlRenderer.h"`
   - Add stub `BatteryHtmlRenderer` class definition (empty)
   - Keep `get_status_renderer()` method returning stub

2. In all `battery/*-BATTERY.h` files:
   - Remove `#include "*-HTML.h"` includes
   - Keep `get_status_renderer()` method (unused but safe)
   - Remove `-HTML.h/.cpp` files from disk

### Phase 2: Remove Display-Only Code from Transmitter

3. Delete entire directories:
   - `devboard/webserver/`
   - `devboard/display/`
   - `devboard/mqtt/mqtt.cpp` (keep mqtt client only)

4. In `devboard/hal/`:
   - Delete non-Olimex HAL files (hw_lilygo.cpp, etc.)

### Phase 3: Implement Receiver Display Layer

5. On receiver, keep only:
   - Battery HTML renderers (`*-HTML.h/.cpp`)
   - Battery.h with full interface
   - Webserver infrastructure
   - Display drivers

### Phase 4: Inter-Device Communication

6. Transmitter sends:
   - Selected battery type
   - Real-time status (already via datalayer snapshot)

7. Receiver uses battery type to:
   - Select correct HTML renderer
   - Display device-specific information

---

## 8. Key Dependencies to Verify

### Battery Control Requires:
```
Battery.h
├── devboard/utils/types.h (just enums, safe)
├── CanBattery.h
│   └── communication/can/ (CAN driver)
└── get_status_renderer() (stub, unused)

Each *-BATTERY.h:
├── Battery.h or CanBattery.h
├── communication/can/comm_can.h
├── inverter/INVERTERS.h
├── charger/CHARGERS.h (some)
└── devboard/safety/ (safety limits)
```

### None of the control logic depends on:
- devboard/webserver/
- devboard/display/
- Any HTML renderer
- Any UI framework

**Verification Plan**:
1. Remove all webserver includes from battery files
2. Verify transmitter still compiles
3. No circular dependencies will be broken

---

## 9. NVM (Settings) Handling

**Current**: NVM is tightly integrated with webserver settings update flow

**Transmitter keeps**: 
- Full NVM read/write
- All settings persistence
- Settings validation

**Receiver**:
- Reads selected settings from transmitter
- Displays in UI
- Sends back changes via command message
- Transmitter applies and persists

**No code movement needed** - just architectural change in how receiver accesses settings.

---

## 10. Safety Considerations

**CRITICAL**: When removing HTML renderers from transmitter, ensure:

1. Battery control logic is NOT in `-HTML.h` files
   - Verify with grep: `battery_manager.cpp` calls to control methods
   - Check that all control code is in `-BATTERY.h/.cpp`

2. No circular dependency removal breaks transmitter
   - Datalayer still has access to battery state
   - Battery still has access to datalayer
   - No new includes added

3. Receiver doesn't recompile transmitter control code
   - Receivers only get HTML renderers and display drivers
   - Receivers do NOT get CAN drivers or inverter implementations

---

## 11. Verification Checklist

- [ ] All `*-HTML.h` files contain ONLY HTML rendering code
- [ ] No `-HTML.h` file includes control logic headers
- [ ] No control logic (CAN, inverter, safety) is in `-HTML.cpp` files
- [ ] Battery.h compiles without webserver includes
- [ ] All 50+ battery implementations still compile on transmitter with stub renderer
- [ ] Receiver can independently compile HTML renderers with battery type info
- [ ] No include paths between transmitter and receiver

---

## 12. Mapping of All ~50+ Battery Files

### Batteries with HTML Renderers (ALL must be handled):
1. BMW-I3 (BMW-I3-BATTERY, BMW-I3-HTML)
2. BMW-IX (BMW-IX-BATTERY, BMW-IX-HTML)
3. BMW-PHEV
4. PYLON (PYLON-BATTERY, PYLON-BATTERY-HTML)
5. RENAULT-ZOE-GEN1/GEN2
6. NISSAN-LEAF
7. TESLA (Model 3/Y, Model S/X)
8. VOLVO-SPA (including Hybrid)
9. MEB (VW ID. series)
10. KIA-E-GMP
11. KIA-64FD
12. HYUNDAI-IONIQ-28
13. BYD-ATTO-3
14. ECMP
15. GEELY-GEOMETRY-C
16. CMP-SMART-CAR
17. CMFA-EV
18. CHADEMO-BATTERY
19. CELLPOWER-BMS
20. BOLT-AMPERA
... and ~30 more

**Each follows same pattern**: `BATTERY-TYPE.h` includes `BATTERY-TYPE-HTML.h`

---

## 13. Summary Table: What Moves Where

| Category | Transmitter | Receiver | Notes |
|----------|-------------|----------|-------|
| Battery parsers | ✓ Keep | ✗ Delete | Control logic on TX |
| Battery HTML renderers | ✗ Delete | ✓ Add | Display logic on RX |
| Inverter drivers | ✓ Keep | ✗ Delete | TX controls inverters |
| CAN/RS485 drivers | ✓ Keep | ✗ Delete | TX handles all comms |
| Safety logic | ✓ Keep | ✗ Delete | TX monitors safety |
| Contactor control | ✓ Keep | ✗ Delete | TX controls contactors |
| Settings storage (NVM) | ✓ Keep | (Query) | TX persists settings |
| MQTT telemetry | ✓ Keep | ✗ Delete | TX publishes real-time data to external MQTT broker |
| Webserver/UI | ✗ Delete | ✓ Add | RX displays only |
| Datalayer snapshot | ✓ Keep | ✓ Receive | TX owns state, RX displays |

