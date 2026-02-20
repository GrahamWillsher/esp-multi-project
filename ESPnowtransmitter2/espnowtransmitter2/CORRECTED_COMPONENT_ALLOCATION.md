# Corrected Transmitter/Receiver Component Allocation

## Summary: What Goes Where After Decoupling

### TRANSMITTER (Olimex ESP32-POE2) - KEEP

**Core Control Components** ✓
- ✓ `battery/` - All 50+ battery parsers (control logic only, no HTML)
- ✓ `inverter/` - All 15+ inverter control drivers
- ✓ `charger/` - Charger implementations
- ✓ `communication/can/` - CAN bus drivers (native + MCP2515/FD)
- ✓ `communication/contactorcontrol/` - Contactor/precharge/isolation control
- ✓ `communication/precharge_control/` - Precharge sequencing
- ✓ `communication/equipmentstopbutton/` - E-stop monitoring
- ✓ `communication/nvm/` - Settings storage (NVS Preferences)
- ✓ `communication/rs485/` - Modbus support for inverters
- ✓ `datalayer/` - Real-time battery state management
- ✓ `devboard/safety/` - Safety monitoring and limits enforcement
- ✓ `devboard/hal/hw_olimex_esp32_poe2.cpp` - Olimex-specific hardware init
- ✓ `devboard/utils/` - Core utilities (logging, events, timers, types)
- ✓ `devboard/mqtt/` - **Real-time telemetry publishing to external MQTT broker**
- ✓ `devboard/wifi/` - MII Ethernet driver (native to Olimex board)
  - **Note**: Future development should move to `devboard/ethernet/` for clarity (not WiFi)
- ✓ `devboard/debug/` - Debug utilities
- ✓ `devboard/sdcard/` - SD card logging (optional)
- ✓ `system_settings.h` - Full settings definitions
  - **Note**: Bidirectional via cache architecture (see Inter-Device Communication below)

**Result**: Full Battery Emulator functionality. Produces:
- Real-time battery state updates
- Sends datalayer snapshots via ESP-NOW to receiver
- Publishes telemetry to external MQTT broker for Home Assistant/Grafana/etc.

---

### RECEIVER (LilyGo T-Display-S3) - ADD

**Display Components** ✓
- ✓ `battery/*-HTML.h` / `battery/*-HTML.cpp` - All 50+ battery type renderers
- ✓ `devboard/webserver/` - Web UI infrastructure and pages
- ✓ `devboard/display/` - TFT display driver for minimal display (LED state indicator, SOC, power in/out)
- ✓ `devboard/mqtt/` - MQTT client for dual battery operation scenarios (ESP-NOW insufficient for multiple batteries)
- ✓ `devboard/hal/hw_lilygo_t_display_s3.cpp` - LilyGo-specific hardware init
- ✓ `devboard/utils/` - Display-safe utilities (logging, timing)
- ✓ `communication/nvm/` - NVS for receiver's own local settings (managed via cache)
- ✓ `system_settings.h` - Settings definitions (bidirectional via cache architecture)

**NOT included**:
- ✗ No battery control code (no CAN parsing, no state management)
- ✗ No inverter drivers (no control)
- ✗ No charger support (no control)
- ✗ No safety monitoring (transmitter handles)
- ✗ No transmitter-side NVM writes (receiver NVM is local settings only)

**Result**: Display and monitoring interface. Consumes:
- Receives datalayer snapshots via ESP-NOW from transmitter (primary for single battery)
- Subscribes to MQTT for dual battery scenarios (when ESP-NOW insufficient)
- Uses battery type from transmitter to select correct HTML renderer
- **TFT display**: Minimal information (unchanged for single/dual): LED state indicator, SOC, power in/out
- **Web UI**: Adapts automatically:
  - Single battery: Standard battery-specific renderer
  - Dual battery: Aggregate summary + individual battery details
- Settings UI for user configuration:
  - Writes changes to local cache
  - Transmitter-related settings: Forwarded to transmitter for NVS update
  - Receiver-related settings: Applied locally to receiver NVS

---

## Inter-Device Communication Protocol

### Transmitter → Receiver (ESP-NOW) - EXISTING IMPLEMENTATION

**Status**: Already successfully implemented and working as a state machine. No changes needed.

**Message Structure**:
```cpp
struct BatterySnapshot {
    uint8_t battery_type;              // Identifies which HTML renderer to use
    
    // Real-time status
    float soc_percent;                 // State of Charge
    float voltage_V;                   // Pack voltage
    float current_A;                   // Current (positive=discharge, negative=charge)
    int32_t power_W;                   // Power (positive=discharge, negative=charge)
    float temperature_C_max;           // Maximum temperature
    float temperature_C_min;           // Minimum temperature
    uint16_t max_charge_power_W;       // Available charge power
    uint16_t max_discharge_power_W;    // Available discharge power
    
    // Battery info (static)
    uint32_t total_capacity_Wh;
    uint16_t max_design_voltage_dV;
    uint16_t min_design_voltage_dV;
    uint16_t max_cell_voltage_mV;
    uint16_t min_cell_voltage_mV;
    uint8_t number_of_cells;
    
    // Status flags
    uint8_t bms_status;                // BMS online/offline/error
    uint8_t error_flags;               // Safety/error flags
};

Frequency: Every 100ms
```

**Note**: This implementation handles single battery operation successfully. For dual battery scenarios, MQTT provides supplementary real-time monitoring.

### Automatic Mode Transition: Single Battery ↔ Dual Battery

**Transition Mechanism** (automatic detection):

```cpp
// Transmitter detects battery configuration change
struct ModeNotification {
    uint8_t operating_mode;  // SINGLE_BATTERY (0x01) or DUAL_BATTERY (0x02)
    uint8_t mqtt_enabled;    // 0 = ESP-NOW only, 1 = MQTT required
    char mqtt_topic_prefix[32]; // e.g., "battery1/", "battery2/"
};
```

**Single Battery → Dual Battery Transition**:
```
Transmitter detects second battery connected
  ↓
Transmitter sends ModeNotification via ESP-NOW
  operating_mode = DUAL_BATTERY
  mqtt_enabled = 1
  ↓
Receiver receives notification
  ↓
Receiver automatically:
  1. Subscribes to MQTT topics for both batteries
     - battery1/status, battery1/info, battery1/events
     - battery2/status, battery2/info, battery2/events
  2. Continues receiving ESP-NOW (for low-latency state updates)
  3. Merges data from both sources
  ↓
Receiver formats dual battery display:
  - Combines data from both MQTT streams
  - Shows aggregate: total SOC, total power, combined status
  - Shows individual: per-battery details on web UI
  ↓
Receiver outputs formatted data to:
  - TFT display (NO CHANGE - same minimal display: LED indicator, SOC, power)
  - Web UI (DUAL BATTERY VIEW - aggregate summary + individual battery details)
```

**Dual Battery → Single Battery Transition**:
```
Transmitter detects battery disconnection
  ↓
Transmitter sends ModeNotification via ESP-NOW
  operating_mode = SINGLE_BATTERY
  mqtt_enabled = 0
  ↓
Receiver receives notification
  ↓
Receiver automatically:
  1. Unsubscribes from MQTT topics
  2. Reverts to ESP-NOW-only data source
  3. Switches to single battery display format
  ↓
Receiver returns to single battery operation
```

**Data Priority Logic**:
```cpp
if (mode == SINGLE_BATTERY) {
    // Primary: ESP-NOW (100ms updates)
    tft_display_data = espnow_snapshot;  // LED, SOC, Power
    web_ui_data = espnow_snapshot;
    
} else if (mode == DUAL_BATTERY) {
    // TFT: NO CHANGE - same minimal display from primary battery
    tft_display_data = espnow_snapshot;  // LED, SOC, Power (unchanged)
    
    // Web UI: Aggregate + detailed dual battery views
    web_ui_data.battery1 = mqtt_battery1_data;
    web_ui_data.battery2 = mqtt_battery2_data;
    web_ui_data.aggregate = calculate_totals(battery1, battery2);
}
```

**Automatic Data Formatting**:
- **TFT Display**: No changes in dual battery mode - continues showing minimal single battery view
- **Web UI**: Automatically adapts for dual battery:
  - Receiver detects battery type from MQTT messages
  - Selects appropriate HTML renderer for each battery (may be different types)
  - Formats aggregate view: Total SOC = (Battery1_SOC × Capacity1 + Battery2_SOC × Capacity2) / (Capacity1 + Capacity2)
  - Web UI automatically shows:
    - Summary page: Combined totals (aggregate SOC, power, voltage)
    - Detail pages: Individual battery renderers (BMW + Tesla, Pylon + Renault, etc.)

### Receiver → Transmitter (ESP-NOW, optional)

**Settings Update via Cache Architecture** (already implemented):

```cpp
struct SettingsUpdate {
    uint8_t field_id;
    uint32_t value;
};
```

**Bidirectional Settings Flow**:
```
User updates settings via receiver web UI
  ↓
Receiver writes to local cache
  ↓
  ├─→ Transmitter settings? → Send to transmitter via ESP-NOW
  │                            Transmitter validates and updates NVS
  │
  └─→ Receiver settings? → Receiver updates local NVS directly
```

**Cache Architecture** (already implemented):
- Receiver maintains local cache of all settings
- Changes written to cache immediately (fast UI response)
- Cache determines routing: transmitter vs receiver NVS
- Transmitter receives updates and validates before persisting
- Both devices maintain their own NVS independently

### MQTT (Transmitter Broadcasting + Receiver Subscribing)

**Transmitter** (independent flow):
```
Transmitter reads datalayer every 10 seconds
  ↓
Publishes JSON to external MQTT broker
  ↓
Topics:
  Single battery: battery/info, battery/events, battery/status
  Dual battery:   battery1/info, battery1/events, battery1/status
                  battery2/info, battery2/events, battery2/status
  ↓
External systems (Home Assistant, Grafana, etc.) subscribe
```

**Receiver** (dual battery scenarios):
```
Receiver MQTT client subscribes to transmitter topics
  ↓
For single battery: Uses ESP-NOW (primary), MQTT optional
For dual battery: Automatically subscribes to MQTT (ESP-NOW insufficient bandwidth)
  ↓
Receiver receives mode notification
  ↓
Automatically subscribes/unsubscribes based on mode
  ↓
Pulls data from MQTT, formats for web display
  ↓
Outputs to web UI (aggregate + detailed views)
TFT display remains unchanged (minimal single battery view)
```

---

## Implementation Notes

### Settings Management Architecture

**Current Implementation** (already working):
- `system_settings.h` provides shared settings definitions for both devices
- Settings changes flow bidirectionally through cache architecture:
  - Receiver caches all settings locally
  - User modifications written to cache first
  - Cache logic routes updates to correct device (transmitter vs receiver)
  - Each device maintains independent NVS storage
  - No "read-only" limitation - full bidirectional operation via cache

**Cache Routing Logic**:
```cpp
if (setting_affects_transmitter) {
    // Forward to transmitter via ESP-NOW
    transmitter.update_setting(field_id, value);
    // Transmitter validates and writes to its NVS
} else if (setting_affects_receiver) {
    // Apply locally
    receiver.update_local_nvs(field_id, value);
}
```

### Future Improvements

**Ethernet Folder Reorganization** (clarity enhancement):
- Current: `devboard/wifi/` contains MII Ethernet driver
- Future: Move to `devboard/ethernet/` to avoid WiFi/Ethernet confusion
- Reason: Olimex uses native MII Ethernet, not WiFi hardware
- Impact: Documentation clarity, no functional change

---

## Why This Allocation is Correct

| Reason | Implication |
|--------|-------------|
| Transmitter has CAN buses physically connected | Must run all battery parsers there |
| Transmitter needs to make safety decisions | Must keep safety monitoring there |
| Transmitter needs to control inverter/charger | Must keep all control drivers there |
| Battery state originates on transmitter | Datalayer lives on transmitter |
| Receiver has no way to parse CAN (no drivers) | Cannot run battery logic on receiver |
| Receiver only needs to display data | Only needs HTML renderers + webserver |
| Users may want external monitoring | MQTT stays with real-time state source (transmitter) |
| Receiver is optional | System works without receiver, just external MQTT monitoring |
| Dual battery needs extra bandwidth | MQTT provides supplementary real-time channel to receiver |

---

## Verification Checklist

### Before Starting Decoupling

- [ ] Battery HTML files contain ONLY display code (verified)
- [ ] Battery control files contain NO HTML rendering code (verified)
- [ ] MQTT module is independent of webserver (verified)
- [ ] No control logic depends on webserver includes (verified)
- [ ] All 50+ battery types follow same coupling pattern (verified)
- [ ] ESP-NOW state machine messaging working satisfactorily (verified - no changes needed)
- [ ] Receiver display shows: LED indicator, SOC, power in/out (verified)

### After Decoupling Implementation

**Transmitter Build**:
- [ ] All 50+ battery control files compile
- [ ] All inverter drivers compile
- [ ] CAN, safety, contactor logic compiles
- [ ] Datalayer compiles
- [ ] MQTT publishes correct telemetry
- [ ] ESP-NOW sends battery snapshots
- [ ] No webserver code in binary
- [ ] No display code in binary

**Receiver Build**:
- [ ] All 50+ HTML renderer files compile
- [ ] Webserver infrastructure compiles
- [ ] Display drivers compile (minimal display: LED, SOC, power)
- [ ] Factory selects correct renderer by battery type
- [ ] MQTT client compiles (for dual battery scenarios)
- [ ] Local NVS writes for receiver settings only
- [ ] No control logic compiled
- [ ] No CAN driver code in binary
- [ ] Receives ESP-NOW messages correctly (working state machine)
- [ ] Receives MQTT messages when needed (dual battery fallback)
- [ ] Mode transition works automatically (single ↔ dual battery)
- [ ] Dual battery data formatted and displayed correctly

---

## Final Architecture Diagram

```
┌─────────────────────────────────────────────┐
│         TRANSMITTER (Olimex POE2)           │
│                                             │
│  ┌──────────────────────────────────────┐   │
│  │     Battery Emulator Core            │   │
│  ├──────────────────────────────────────┤   │
│  │ • 50+ Battery Parsers (CAN)          │   │
│  │ • 15+ Inverter Control Drivers       │   │
│  │ • Safety Monitoring & Limits         │   │
│  │ • Contactor/Precharge Control        │   │
│  │ • Real-time Datalayer                │   │
│  │ • Settings Storage (NVM)             │   │
│  └──────────────────────────────────────┘   │
│                   │                         │
│         ┌─────────┴─────────┐               │
│         ↓                   ↓               │
│    ┌─────────┐         ┌──────────┐        │
│    │ ESP-NOW │         │  MQTT    │        │
│    │ Sender  │         │Publisher │        │
│    │         │         │ (ext'l   │        │
│    │Battery  │         │ telemetry)        │
│    │Snapshot │         │          │        │
│    └────┬────┘         └──────┬───┘        │
└─────────┼──────────────────────┼───────────┘
          │                      │
          │                      │
          ↓                      ↓
    ┌──────────────┐      ┌─────────────┐
    │ RECEIVER     │      │External     │
    │ (LilyGo      │      │MQTT Broker  │
    │ T-Display)   │      │(Home Asst,  │
    │              │      │Grafana,etc.)│
    │ ┌──────────┐ │      │             │
    │ │ESP-NOW   │ │      └─────────────┘
    │ │Receiver  │ │
    │ │(State    │ │
    │ │Machine)  │ │
    │ │+ Mode    │ │
    │ │Detector  │ │
    │ ├──────────┤ │
    │ │MQTT      │ │
    │ │Subscriber│ │
    │ │(Auto-    │ │
    │ │enable on │ │
    │ │dual batt)│ │
    │ ├──────────┤ │
    │ │Data      │ │
    │ │Formatter │ │
    │ │(Aggregate│ │
    │ │+ Detail) │ │
    │ ├──────────┤ │
    │ │TFT       │ │
    │ │Display   │ │
    │ │(Minimal) │ │
    │ ├──────────┤ │
    │ │Local NVS │ │
    │ │Settings  │ │
    │ └──────────┘ │
    └──────────────┘
```

---

## Files Summary

### DELETE from Transmitter
```
battery/*-HTML.h             (all ~50 files)
battery/*-HTML.cpp           (all ~50 files)
devboard/webserver/          (entire directory)
devboard/display/            (entire directory)
devboard/hal/hw_lilygo*.cpp  (non-Olimex HAL files)
```

### KEEP on Transmitter
```
Everything else in Battery Emulator source directory
Includes: all battery control, inverter drivers, CAN, safety, MQTT, NVM
```

### ADD to Receiver
```
Copy ALL deleted -HTML files to receiver lib
Copy devboard/webserver/ to receiver lib
Copy devboard/display/ to receiver lib
Create BatteryRendererFactory on receiver
```

