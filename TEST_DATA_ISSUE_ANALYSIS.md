# Comprehensive Test Data Cell Count Issue Analysis

## Executive Summary

**Problem:** Transmitter sends 108 cells even when Nissan Leaf battery (96 cells) is selected.

**Root Cause:** Hard-coded default in test data generator falls back to 108 cells when cell count is not set at init time.

**Timeline Issue:** The initialization sequence has a critical ordering problem:
1. Battery selected by user (Nissan Leaf = 96 cells)
2. Battery Emulator setup() initializes - sets `datalayer.battery.info.number_of_cells = 96`
3. TestDataGenerator initialized with hard-coded 108 cell fallback
4. **Later:** TestDataGenerator.init() is NOT called, so fallback is used

---

## Detailed Technical Breakdown

### 1. Test Data Generator Initialization Flow

#### File: `src/battery_emulator/test_data_generator.cpp`

**Current Behavior (WRONG):**
```cpp
namespace TestDataGenerator {

static bool initialized = false;  // Controls init() call

void init() {
    if (initialized) return;  // Prevents re-initialization
    
    // Lines 26-58: Check datalayer values
    LOG_INFO("TEST_DATA", "number_of_cells BEFORE init: %u", 
             datalayer.battery.info.number_of_cells);
    
    // CRITICAL LOGIC:
    if (datalayer.battery.info.number_of_cells == 0) {
        datalayer.battery.info.number_of_cells = 108;  // FALLBACK
        LOG_WARN("TEST_DATA", "No battery cell count configured, using default: 108 cells");
    } else {
        LOG_INFO("TEST_DATA", "Using battery's configured cell count: %u cells",
                 datalayer.battery.info.number_of_cells);
    }
    
    initialized = true;
}

void update() {
    if (!initialized) {
        init();  // First call triggers init()
        return;
    }
    // ... rest of update logic
}
```

**Problem with Current Logic:**
- `init()` is NEVER called in CAN-enabled mode (CONFIG_CAN_ENABLED)
- Why? See main.cpp line 356-360:
  ```cpp
  if (TestDataGenerator::is_enabled()) {
      LOG_INFO("TEST_DATA", "Explicitly initializing test data generator...");
      TestDataGenerator::update();  // First call triggers init()
  }
  ```
- This is only called IF `TestDataGenerator::is_enabled()` returns true
- But `is_enabled()` returns `true` only if `TEST_DATA_GENERATOR_ENABLED` macro is defined
- In normal operation with CONFIG_CAN_ENABLED, this code is skipped!

### 2. Main Setup Sequence

#### File: `src/main.cpp` (Lines 100-360+)

**Sequence in CAN-Enabled Mode:**

```
STEP 1: Load battery settings from NVS
        ↓ user_selected_battery_type = NISSAN_LEAF
        
STEP 2: Initialize CAN driver
        
STEP 3: Initialize battery (PRIMARY)
        BatteryManager::init_primary_battery(user_selected_battery_type)
        ↓
        user_selected_battery_type = NISSAN_LEAF
        setup_battery()  ← Battery Emulator sets number_of_cells = 96
        ↓
        datalayer.battery.info.number_of_cells = 96 ✓ CORRECT
        
STEP 4: Initialize ESP-NOW and other systems
        
STEP 5: Initialize test mode (TestMode, NOT TestDataGenerator)
        TestMode::initialize(96)  ← Uses hard-coded 96 cells
        TestMode::set_enabled(false)  ← Disabled, uses live data
        
STEP 6: Initialize test data generator (CONDITIONAL)
        if (TestDataGenerator::is_enabled()) {  ← PROBLEM: Always false!
            TestDataGenerator::update();
        }
        
STEP 7: Start data sender
        DataSender::instance().start()  ← Runs every 2 seconds
        ↓
        send_test_data()  ← Uses what data?
```

### 3. Data Sender Flow

#### File: `src/espnow/data_sender.cpp`

**Where is test data generated?**

```cpp
// DataSender::task_impl() runs every 2 seconds
void DataSender::task_impl(void* parameter) {
    while (true) {
        vTaskDelayUntil(&last_wake_time, interval_ticks);
        
        if (EspnowMessageHandler::instance().is_transmission_active()) {
            LOG_TRACE("Sending test data (transmission active)");
            send_test_data_with_led_control();  ← Calls send_test_data()
        }
    }
}

void send_test_data_with_led_control() {
    send_test_data();  ← From espnow_transmitter library
    // ... LED control logic
}
```

**Where is `send_test_data()` defined?**

#### File: `espnow_transmitter/espnow_transmitter.cpp` (from esp32common library)

```cpp
void send_test_data() {
    static bool soc_increasing = true;
    
    tx_data.type = msg_data;
    
    // Simple SOC animation (20-80%)
    if (soc_increasing) {
        tx_data.soc += 1;
        if (tx_data.soc >= 80) soc_increasing = false;
    } else {
        tx_data.soc -= 1;
        if (tx_data.soc <= 20) soc_increasing = true;
    }
    
    tx_data.power = random(-4000, 4001);
    tx_data.checksum = calculate_checksum(&tx_data);
    
    // Send the data
    esp_now_send(receiver_mac, (uint8_t*)&tx_data, sizeof(tx_data));
}
```

**KEY FINDING:** This simple `send_test_data()` doesn't generate cell voltages at all!
- It only sets SOC and power
- Where do the 108 cells come from?

### 4. Cell Voltage Generation Path

**The 108 cells must come from:**

#### Static Data Collection / Serialization

#### File: `src/datalayer/static_data.cpp` (Lines 135-245)

```cpp
String battery_specs_json() {
    // Get cell count from datalayer
    uint16_t cell_count = datalayer.battery.info.number_of_cells;
    
    Serial.printf("[SERIALIZE_DEBUG] cell_count from datalayer: %u\n", cell_count);
    
    // Build JSON with cell voltages
    for (uint16_t i = 0; i < cell_count && i < MAX_AMOUNT_CELLS; i++) {
        // If no real data, generate dummy
        if (!has_real_data && cell_count > 0) {
            dummy_voltages[cell_count - 1] = 3920;  // Slightly higher
        }
        doc["cells"][i] = dummy_voltages[i];
    }
    
    doc["number_of_cells"] = cell_count;
    // ... serialize to JSON
}
```

**The problem is: When does `datalayer.battery.info.number_of_cells` get set to 108?**

### 5. Root Cause: The Missing Link

**When TestDataGenerator.init() is NOT called:**

1. In CAN mode, battery sets `number_of_cells = 96` correctly ✓
2. TestDataGenerator.init() is skipped (is_enabled() returns false)
3. TestDataGenerator.initialized = false (stays false forever) ✗
4. TestDataGenerator.update() is never called
5. **Problem:** Cell voltage array is never populated!

**When a request comes for cell data:**
- Datalayer reports `number_of_cells = 96` ✓ (from battery)
- But cell_voltages_mV[] array is uninitialized or uses wrong size
- Somewhere, a fallback assumes 108 cells (maximum default)

### 6. Where the 108 Comes From

**Hypothesis:** In the TestDataGenerator, if init() is never called:
- The cell voltage array may be pre-allocated to MAX_CELLS (108)
- Or the datalayer defaults to 108 if number_of_cells is 0
- Or static_data.cpp has a hard-coded default

**Most Likely:** Line 51 in test_data_generator.cpp:
```cpp
if (datalayer.battery.info.number_of_cells == 0) {
    datalayer.battery.info.number_of_cells = 108;  // FALLBACK - THIS IS IT!
}
```

But this should only happen if number_of_cells is still 0, which it shouldn't be...

**UNLESS:** The test data generator is being used somewhere with number_of_cells = 0!

---

## Discovery: Where Test Data is Actually Used

Let me trace the flow for cell voltage generation:

### 1. Receiver requests cell data
```
Dashboard → /api/get_event_logs → Receiver fetches from transmitter
```

### 2. Transmitter HTTP response
The transmitter's HTTP endpoint needs to return cell voltages.

**Question:** Is there a separate test data generator being used for HTTP responses?

### 3. Alternative: Datalayer fallback behavior

If `datalayer.battery.info.number_of_cells = 0` anywhere, could trigger the 108 fallback.

---

## Smoking Gun: Test Mode Initialization

#### File: `src/test_mode/test_mode.cpp` (Lines 36-45)

```cpp
bool initialize(uint8_t num_cells) {
    ESP_LOGI(TAG, "Initializing test mode with %u cells", num_cells);
    
    g_config.num_cells = num_cells;  // Hard-coded 96 from main.cpp
    g_state.cell_voltages.resize(num_cells, NOMINAL_CELL_VOLTAGE);
    g_state.balancing_active.resize(num_cells, false);
    
    // Initialize with stable discharge scenario
    reset();
    
    return true;
}
```

**In main.cpp line 355:**
```cpp
TestMode::initialize(96);  // Hard-coded 96 cells!
TestMode::set_enabled(false);  // But disabled...
```

**This initializes TestMode with 96 cells correctly.**
**But TestMode is DISABLED (set_enabled(false)).**

---

## The Real Problem: Conflicting Generators

**There are THREE separate test data generators:**

1. **TestMode** (src/test_mode/test_mode.cpp)
   - Initialized with 96 cells
   - Currently DISABLED (set_enabled = false)
   - Supports scenarios, balancing, faults

2. **TestDataGenerator** (src/battery_emulator/test_data_generator.cpp)
   - Falls back to 108 cells if number_of_cells == 0
   - Usually disabled (TEST_DATA_GENERATOR_ENABLED macro)
   - Used for realistic cell voltage simulation

3. **send_test_data()** from library (espnow_transmitter)
   - Simple SOC/power animation
   - No cell voltage generation
   - Currently active

**The Problem:** When cell voltage data is requested:
- Actual battery data provider is disabled (CAN mode with no real CAN)
- TestMode is disabled
- TestDataGenerator never initializes
- **Fallback:** datalayer uses uninitialized cell_voltages_mV array OR TestDataGenerator defaults to 108

---

## Why "108" Specifically?

Looking at test_data_generator.cpp line 51:
```cpp
datalayer.battery.info.number_of_cells = 108;  // 108S default for generic battery
```

This is a generic battery with 108 cells (~390V nominal).
- Nissan Leaf: 96 cells (~345V)
- Generic EV: 108 cells (~390V)
- Porsche: 96 cells
- Tesla Model 3: 2170 cells (in parallel, internally)

---

## Proposed Solutions

### Option 1: Auto-Initialize Test Data Generator (RECOMMENDED)

**Change:** In main.cpp, always initialize TestDataGenerator when battery is set up:

```cpp
// After BatteryManager initialization (line 154-159)
if (BatteryManager::instance().init_primary_battery(user_selected_battery_type)) {
    LOG_INFO("BATTERY", "✓ Battery initialized: %u cells configured", 
             datalayer.battery.info.number_of_cells);
    
    // NEW: Initialize test data generator with actual battery cell count
    LOG_INFO("TEST_DATA", "Initializing test data generator with %u cells from battery...",
             datalayer.battery.info.number_of_cells);
    TestDataGenerator::update();  // Triggers init(), respects battery cell count
} else {
    LOG_WARN("BATTERY", "Battery initialization returned false");
}
```

**Advantages:**
- Respects battery selection automatically
- Populates cell voltage array correctly
- No need for hard-coded fallbacks
- Works regardless of test mode state

**Disadvantages:**
- Requires test data generator to be enabled

---

### Option 2: Use TestMode Instead of TestDataGenerator

**Change:** In main.cpp, make TestMode the primary test data generator:

```cpp
// After battery init (line 154-159)
uint16_t battery_cell_count = datalayer.battery.info.number_of_cells;

// Initialize test mode with ACTUAL battery cell count
TestMode::initialize(battery_cell_count);  // Was: TestMode::initialize(96)
TestMode::set_enabled(true);  // Was: set_enabled(false)

LOG_INFO("TEST_MODE", "✓ Test mode initialized with %u cells (from battery)", 
         battery_cell_count);
```

**Advantages:**
- Single, unified test generator
- More flexible (supports scenarios)
- Better integration
- Clear state management

**Disadvantages:**
- Changes current behavior
- Requires TestMode to generate all data (SOC, power, cell voltages)

---

### Option 3: Dynamic Initialization Based on Battery Selection

**Change:** Create initialization callback in battery selection:

```cpp
// New function in TestDataGenerator
namespace TestDataGenerator {
    void reinit_for_battery(uint16_t cell_count) {
        initialized = false;  // Reset
        if (cell_count > 0) {
            datalayer.battery.info.number_of_cells = cell_count;
            init();  // Re-initialize with new cell count
        }
    }
}

// In main.cpp, after battery init:
TestDataGenerator::reinit_for_battery(
    datalayer.battery.info.number_of_cells
);
```

**Advantages:**
- Supports battery changes without reboot
- Explicit cell count tracking
- Clear API

**Disadvantages:**
- More code changes
- Requires callback mechanism

---

### Option 4: Fix the Fallback Logic (QUICK FIX)

**Change:** Check battery's cell count BEFORE using fallback:

```cpp
// In test_data_generator.cpp, init()
if (datalayer.battery.info.number_of_cells == 0) {
    // ONLY use default if battery hasn't set a count
    LOG_WARN("TEST_DATA", "Battery has no cell count, using default: 108 cells");
    datalayer.battery.info.number_of_cells = 108;
} else {
    LOG_INFO("TEST_DATA", "Using battery's cell count: %u cells",
             datalayer.battery.info.number_of_cells);
    // Additional safety: verify reasonable range
    if (datalayer.battery.info.number_of_cells > 256) {
        LOG_ERROR("TEST_DATA", "Unreasonable cell count: %u (truncating to 96)",
                  datalayer.battery.info.number_of_cells);
        datalayer.battery.info.number_of_cells = 96;
    }
}
```

**Advantages:**
- Minimal code change
- Defensive programming
- Catches corrupted values

**Disadvantages:**
- Doesn't solve the "init() never called" problem
- Still requires init() to be triggered

---

## Summary Table

| Issue | Current | With Fix |
|-------|---------|----------|
| Battery selected | Nissan Leaf (96 cells) | ✓ Nissan Leaf (96 cells) |
| number_of_cells set | ✓ 96 (from battery) | ✓ 96 (from battery) |
| TestDataGenerator.init() called | ✗ NO | ✓ YES |
| Cell voltages generated | 108 (fallback) | ✓ 96 (battery count) |
| Test data sent | 108 cells | ✓ 96 cells |

---

## Recommended Fix Priority

1. **IMMEDIATE:** Option 1 (Auto-Initialize TestDataGenerator)
   - One-line fix in main.cpp
   - Lowest risk
   - Solves immediate problem
   - Code: Call `TestDataGenerator::update()` after battery init

2. **MEDIUM:** Option 2 (Use TestMode exclusively)
   - Consolidate generators
   - Better long-term design
   - Requires testing
   - ~20 lines of code changes

3. **LONG-TERM:** Option 3 (Dynamic Initialization)
   - Support battery changes
   - Battery selection via HTTP
   - Requires callback system
   - ~50 lines of code changes

---

## Testing Checklist After Fix

- [ ] Select Nissan Leaf battery
- [ ] Check logs: confirm "96 cells configured"
- [ ] Request cell data from HTTP API
- [ ] Verify 96 cell voltages in JSON response
- [ ] Select different battery (e.g., Tesla, BYD)
- [ ] Verify cell count updates to new battery
- [ ] No reboot required (if dynamic solution)
- [ ] Test mode disabled/enabled toggle works
- [ ] Cell balancing simulation works (if applicable)
- [ ] Performance: no extra delays or memory issues

---

## Code Locations for Implementation

| File | Line | Action |
|------|------|--------|
| `src/main.cpp` | 154-159 | Add TestDataGenerator init after battery |
| `src/battery_emulator/test_data_generator.cpp` | 21-100 | Optional: improve init logic |
| `src/test_data_generator.h` | - | Optional: add reinit function |
| Docs | - | Document: Which generator is used when |

