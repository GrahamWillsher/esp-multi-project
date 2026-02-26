# Test Data Flow Diagram & Execution Timeline

## Current Data Flow (BROKEN)

```
┌─────────────────────────────────────────────────────────────────┐
│                    TRANSMITTER INITIALIZATION                   │
└─────────────────────────────────────────────────────────────────┘

TIME    STEP                                STATUS
────────────────────────────────────────────────────────────────────
boot    Load battery type from NVS
+50ms   Initialize battery (Nissan Leaf = 96S)          ✓ CORRECT
+100ms  datalayer.battery.info.number_of_cells = 96     ✓ CORRECT
        
+150ms  Initialize TestMode::initialize(96)            ✓ CORRECT
+200ms  TestMode::set_enabled(false)                    ✗ DISABLED!
        
+250ms  if (TestDataGenerator::is_enabled())           ✗ FALSE!
+300ms      TestDataGenerator::update()  ← NEVER CALLED
        
+350ms  DataSender::start()  
+400ms  {
          while(true) {
            send_test_data()  ← What data comes from here?
          }
        }

┌─────────────────────────────────────────────────────────────────┐
│            WHEN CELL DATA IS REQUESTED (e.g., HTTP)             │
└─────────────────────────────────────────────────────────────────┘

REQUEST COMES IN:
  Dashboard (receiver) → /api/get_event_logs
  ↓
  Transmitter HTTP handler needs cell voltages
  ↓
  datalayer.battery.status.cell_voltages_mV[0..95] ← 96 slots allocated
  ↓
  Problem: Array might be uninitialized or wrong size!
  ↓
  Fallback: Use TestDataGenerator defaults
  ↓
  TestDataGenerator::init() never called
  ↓
  Fallback to hard-coded 108 cell default!  ✗ WRONG!
```

---

## Sequence of Events (Timeline)

```
NORMAL FLOW:

BOOT
  │
  ├─ Setup Serial, Logging
  │
  ├─ Load Settings (battery type = NISSAN_LEAF)
  │   └─ user_selected_battery_type = NISSAN_LEAF ✓
  │
  ├─ Initialize CAN Driver
  │
  ├─ Initialize Battery (PRIMARY)
  │   └─ BatteryManager::init_primary_battery(NISSAN_LEAF)
  │       └─ setup_battery()  ← Battery Emulator sets number_of_cells = 96
  │           └─ datalayer.battery.info.number_of_cells = 96 ✓
  │
  ├─ Initialize Ethernet & Network
  │
  ├─ Initialize ESP-NOW
  │   ├─ Create RX queue
  │   ├─ Init esp_now library
  │   └─ Start message handler task
  │
  ├─ Initialize TestMode
  │   ├─ TestMode::initialize(96)  ✓ Correct cell count
  │   └─ TestMode::set_enabled(false)  ✗ Disabled
  │
  ├─ [CRITICAL MISSING STEP]
  │   ├─ if (TestDataGenerator::is_enabled())  ← FALSE!
  │   │   └─ TestDataGenerator::update()  ← NEVER CALLED
  │   │       └─ init()  ← NEVER CALLED
  │   │           └─ Uses datalayer.battery.info.number_of_cells
  │   └─ TestDataGenerator::initialized = false (forever)
  │
  ├─ Start DataSender Task
  │   └─ Every 2 seconds: send_test_data()
  │
  └─ ✓ Boot complete
  
  
RUNTIME (when cell data requested):

HTTP GET /api/battery_specs
  │
  ├─ Transmitter needs to send cell voltages
  │   └─ Reads from: datalayer.battery.status.cell_voltages_mV[]
  │
  ├─ Problem: Array uninitialized or wrong size
  │   ├─ Battery set number_of_cells = 96 ✓
  │   └─ But cell array initialized to ???
  │
  ├─ Response builder checks number_of_cells
  │   ├─ Gets 96 from datalayer.battery.info
  │   └─ But where are the 96 voltages?
  │
  ├─ If array uninitialized, fallback to TestDataGenerator
  │   └─ Calls TestDataGenerator::init()  [FIRST TIME!]
  │       ├─ Checks: datalayer.battery.info.number_of_cells
  │       ├─ If somehow = 0, uses fallback 108
  │       └─ If = 96, should use 96
  │
  └─ Response sent with ??? cells

HYPOTHESIS: number_of_cells gets reset to 0 somewhere, 
triggering the 108 fallback!
```

---

## Root Cause Flowchart

```
                           ┌─────────────────┐
                           │   APP STARTUP   │
                           └────────┬────────┘
                                    │
                           ┌────────▼────────┐
                           │  Load Settings  │
                           │   Battery Type  │
                           │  NISSAN_LEAF    │
                           └────────┬────────┘
                                    │
                           ┌────────▼────────┐
                           │  Init Battery   │
                           │   96 cells OK   │
                           └────────┬────────┘
                                    │
                    ┌───────────────┴───────────────┐
                    │                               │
          ┌─────────▼─────────┐        ┌───────────▼──────────┐
          │  TestMode Init    │        │ TestDataGenerator ??  │
          │  ✓ 96 cells       │        │   SHOULD INIT HERE    │
          │  ✗ Set disabled   │        │  ✗ NEVER CALLED       │
          └───────────────────┘        └──────────────────────┘
                    │                           │
          ┌─────────▼─────────┐        ┌───────▼───────────┐
          │  TestMode.enabled │        │ Later: Request    │
          │  = false          │        │ cell data         │
          │  (UNUSED)         │        │                   │
          └───────────────────┘        └───────┬───────────┘
                                               │
                                    ┌──────────▼──────────┐
                                    │ TestDataGenerator   │
                                    │ init() CALLED NOW   │
                                    │ (FIRST TIME!)       │
                                    └──────────┬──────────┘
                                               │
                          ┌────────────────────┴─────────────────┐
                          │                                       │
          ┌───────────────▼───────────────┐      ┌──────────────▼────────┐
          │ number_of_cells == 96?        │      │ number_of_cells == 0? │
          │ YES (should be)               │      │ If so, fallback       │
          └───────────────┬───────────────┘      └──────────────┬────────┘
                          │                                      │
                    ┌─────▼──────┐                     ┌─────────▼────────┐
                    │ Use 96      │                     │ Use 108 DEFAULT  │
                    │ (should be) │                     │ ✗ WRONG!         │
                    └─────┬──────┘                      └────────┬────────┘
                          │                                      │
                    ┌─────▼────────────────────────────┬────────▼──────┐
                    │                                  │               │
              ┌─────▼──────┐                    ┌──────▼────────┐    │
              │ Send 96     │                    │ Send 108      │    │
              │ cells ✓     │                    │ cells ✗       │    │
              └──────────────┘                   └──────────────┘    │
                                                        │             │
                                                        ▼             │
                                        ╔════════════════════════╗    │
                                        ║ THIS IS WHAT HAPPENS!  ║    │
                                        ║ We see 108 cells even  ║    │
                                        ║ though 96 selected     ║    │
                                        ╚════════════════════════╝    │
                                                                      ▼
                                                ┌──────────────────────┐
                                                │ SOLUTION: Initialize │
                                                │ TestDataGenerator    │
                                                │ RIGHT AFTER battery  │
                                                │ is set up (96 cells) │
                                                └──────────────────────┘
```

---

## Side-by-Side Comparison: Before vs After Fix

### BEFORE FIX (Current - Broken)

```
TRANSMITTER:
  Battery Init  → number_of_cells = 96 ✓
  TestMode Init → initialized with 96 ✓
  TestMode      → DISABLED ✗
  TestDataGen   → NEVER INITIALIZED ✗
  
DATALAYER:
  number_of_cells = 96 ✓
  cell_voltages_mV = ??? (not populated)
  
HTTP RESPONSE:
  number_of_cells = 96 ✓
  cells = 108 values ✗ MISMATCH!
  
RECEIVER SEES:
  Nissan Leaf battery selected ✓
  96 cells configured ✓
  108 cell voltage values ✗ WRONG!
```

### AFTER FIX (Option 1 - Recommended)

```
TRANSMITTER:
  Battery Init  → number_of_cells = 96 ✓
  TestDataGen   → Initialize(96) ✓ FIXED!
  TestMode Init → (unused but ok)
  
DATALAYER:
  number_of_cells = 96 ✓
  cell_voltages_mV[0..95] = [populated] ✓
  
HTTP RESPONSE:
  number_of_cells = 96 ✓
  cells = 96 values ✓ MATCHES!
  
RECEIVER SEES:
  Nissan Leaf battery selected ✓
  96 cells configured ✓
  96 cell voltage values ✓ CORRECT!
```

---

## Key Code Locations

### Where cell count is set (CORRECT ✓)
```
src/battery_emulator/Battery.cpp / NISSAN-LEAF-BATTERY.cpp
  ↓
  datalayer.battery.info.number_of_cells = 96 ✓
```

### Where cell count is NOT used (BUG ✗)
```
src/battery_emulator/test_data_generator.cpp (Line 50-58)
  
  if (datalayer.battery.info.number_of_cells == 0) {
      datalayer.battery.info.number_of_cells = 108;  ✗ FALLBACK USED
  }
  
  BUT THIS NEVER RUNS BECAUSE:
  init() IS NEVER CALLED!
```

### Where init() should be called (MISSING CALL ✗)
```
src/main.cpp (Line 154-160)

  if (BatteryManager::instance().init_primary_battery(...)) {
      LOG_INFO("BATTERY", "✓ Battery initialized: %u cells",
               datalayer.battery.info.number_of_cells);
      
      // MISSING: TestDataGenerator::update();  ← ADD THIS!
  }
```

### Where cell data is used (WRONG COUNT ✗)
```
src/datalayer/static_data.cpp (Line 135+)

  uint16_t cell_count = datalayer.battery.info.number_of_cells;  // = 96 ✓
  
  for (uint16_t i = 0; i < cell_count && i < MAX_AMOUNT_CELLS; i++) {
      // Uses 96, but if array was initialized to 108 slots...
      doc["cells"][i] = dummy_voltages[i];
  }
  
  // What if dummy_voltages initialized to 108 somewhere?
```

---

## The Three Test Generators (Confusion Point)

```
┌────────────────────────────────────────────────────────────────┐
│                   THREE TEST DATA GENERATORS                    │
└────────────────────────────────────────────────────────────────┘

1. send_test_data() from library (ACTIVE)
   ├─ File: esp32common/espnow_transmitter/espnow_transmitter.cpp
   ├─ What it does: Animates SOC (20-80%), randomizes power
   ├─ Cell voltages: DOESN'T GENERATE ANY
   ├─ Status: Currently active every 2 seconds
   └─ Problem: No cell data

2. TestMode (DISABLED)
   ├─ File: src/test_mode/test_mode.cpp
   ├─ What it does: Full simulation with scenarios, balancing, faults
   ├─ Cell voltages: GENERATES with scenarios
   ├─ Status: Initialized with 96 cells but DISABLED
   └─ Why disabled: set_enabled(false) in main.cpp line 356

3. TestDataGenerator (LAZY INIT)
   ├─ File: src/battery_emulator/test_data_generator.cpp
   ├─ What it does: Realistic data with voltage curves, temp simulation
   ├─ Cell voltages: GENERATES (if initialized!)
   ├─ Status: Has fallback to 108 cells if init() never called
   └─ Problem: init() never called, so fallback used!

WHICH ONE IS USED?
  → send_test_data() → only SOC/power
  → TestMode → NOT USED (disabled)
  → TestDataGenerator → NOT USED (never init)
  → Result: Cell voltage array uninitialized
  → HTTP response: Falls back to TestDataGenerator defaults = 108
```

---

## Power Level Diagram

```
Priority/Usage Levels:

┌──────────────────────────────────────┐
│ LEVEL 1: Real CAN Bus Data (IF set)  │  ← Highest priority
│ From real vehicle via CAN interface  │
├──────────────────────────────────────┤
│ LEVEL 2: TestDataGenerator (IF init) │  
│ Realistic simulation with bounds     │
├──────────────────────────────────────┤
│ LEVEL 3: TestMode (IF enabled)       │
│ Scenario-based simulation            │
├──────────────────────────────────────┤
│ LEVEL 4: send_test_data() (ACTIVE)   │  ← Currently active
│ Simple SOC/power animation           │     (but no cells!)
├──────────────────────────────────────┤
│ LEVEL 5: Uninitialized defaults      │  ← Fallback to 108!
│ Whatever memory contains             │
└──────────────────────────────────────┘

CURRENT STATE:
Levels 1, 2, 3 not used → Falls to Level 4 (no cell data)
→ Falls to Level 5 (uninitialized, uses 108 default)
```

---

## Implementation Solution (One-Line Fix)

```cpp
// LOCATION: src/main.cpp, Line 159 (after BatteryManager init)

BEFORE:
────────────────────────────────────────
    if (BatteryManager::instance().init_primary_battery(user_selected_battery_type)) {
        LOG_INFO("BATTERY", "✓ Battery initialized: %u cells configured", 
                 datalayer.battery.info.number_of_cells);
    } else {
        LOG_WARN("BATTERY", "Battery initialization returned false (may be None type)");
    }


AFTER:
────────────────────────────────────────
    if (BatteryManager::instance().init_primary_battery(user_selected_battery_type)) {
        LOG_INFO("BATTERY", "✓ Battery initialized: %u cells configured", 
                 datalayer.battery.info.number_of_cells);
        
        // FIXED: Initialize test data generator with actual battery cell count
        LOG_INFO("TEST_DATA", "Initializing test data generator with battery's %u cells...",
                 datalayer.battery.info.number_of_cells);
        TestDataGenerator::update();  // Triggers init(), respects battery cell count ← ADD THIS!
        
    } else {
        LOG_WARN("BATTERY", "Battery initialization returned false (may be None type)");
    }
```

**That's it! One function call fixes the entire issue.**

---

## Expected Outcome After Fix

```
LOGS SHOULD SHOW:
────────────────────────────────────────
[BATTERY] ✓ Battery initialized: 96 cells configured
[TEST_DATA] Initializing test data generator with battery's 96 cells...
[TEST_DATA] ========== INIT() CALLED ==========
[TEST_DATA] number_of_cells BEFORE init: 96
[TEST_DATA] number_of_cells AFTER init: 96
[TEST_DATA] ✓ Test data initialized: 75000Wh, 96S, SOC=65%
[MAIN] ===== PHASE 4a: REAL BATTERY DATA =====
[MAIN] Using simulated test data (CAN disabled)
[MAIN] ✓ Data sender started

HTTP RESPONSE SHOWS:
────────────────────────────────────────
{
  "success": true,
  "number_of_cells": 96,
  "cells": [3600, 3605, 3598, ... 96 total],
  "balancing": [false, false, ... 96 total]
}

RECEIVER DASHBOARD SHOWS:
────────────────────────────────────────
Battery: Nissan Leaf
Cells: 96 ✓ (was 108 ✗)
Cell Voltages: [properly populated]
```

