# How to Switch Between Test Mode and Live Mode on Transmitter

## Current Implementation

The transmitter starts in **LIVE MODE** by default and currently has **NO UI** to toggle it.

### Location in Source Code
**File:** [src/main.cpp](ESPnowtransmitter2/espnowtransmitter2/src/main.cpp#L330-L334)

```cpp
// Phase 1: Initialize test mode (disabled by default)
LOG_INFO("TEST_MODE", "Initializing test mode system...");
TestMode::initialize(96);  // Support 96 cells
TestMode::set_enabled(false);  // Start in live mode ← ALWAYS FALSE
LOG_INFO("TEST_MODE", "✓ Test mode initialized (disabled)");
```

---

## How to Change Mode

### Option 1: Modify Source Code (Current Method) ⚠️
**Edit:** [src/main.cpp](ESPnowtransmitter2/espnowtransmitter2/src/main.cpp#L333)

**Change from:**
```cpp
TestMode::set_enabled(false);  // Live mode
```

**To:**
```cpp
TestMode::set_enabled(true);   // Test mode (dummy data)
```

**Then rebuild and flash:**
```bash
cd ESPnowtransmitter2/espnowtransmitter2
pio run
pio run --target upload
```

---

### Option 2: Using #define Constant (Cleaner) ✅ **RECOMMENDED**

**Add to config file** (e.g., [src/config/task_config.h](ESPnowtransmitter2/espnowtransmitter2/src/config/task_config.h)):

```cpp
// At top of file, in namespace or before main():
#define ENABLE_TEST_MODE 0  // Set to 1 for test mode, 0 for live
```

**Then in main.cpp:**
```cpp
TestMode::set_enabled(ENABLE_TEST_MODE);  // Controlled by #define
```

**To toggle:**
- Change `#define ENABLE_TEST_MODE 0` → `1` for test mode
- Change `#define ENABLE_TEST_MODE 1` → `0` for live mode
- Rebuild and flash

---

### Option 3: NVS Persistent Storage (Future) 🚀
Currently NOT implemented, but would allow:
- Storing preference in transmitter's flash memory
- Loading on startup: `TestMode::set_enabled(NVS::getTestMode())`
- API endpoint to change without rebuild

---

## What Does Test Mode Do?

When `TestMode::set_enabled(true)`:

1. **Generates realistic test data:**
   - 96 cell voltages with balancing simulation
   - SOC drift (discharging/charging)
   - Power variations
   - Temperature changes

2. **Updates every 2 seconds** (via data_sender task)

3. **Sends via ESP-NOW and MQTT** to receiver

4. **Display shows:**
   - Green "Test Mode: ON" indicator in serial logs
   - Data drifts naturally (won't stay constant)
   - Cell voltages change realistically

---

## When to Use Each Mode

| Mode | When | Purpose |
|------|------|---------|
| **Live** (`false`) | Battery connected | Real CAN data from battery |
| **Test** (`true`) | Development/testing | Realistic dummy data for UI testing |

---

## Current Behavior by Build Config

### With CAN Enabled (`CONFIG_CAN_ENABLED`)
```cpp
if (CONFIG_CAN_ENABLED) {
    tx_data.soc = datalayer.battery.status.reported_soc / 100;  // Real data
}
```
- Gets real battery data via CAN bus
- Test mode flag is ignored (no CAN = no real data)

### Without CAN (`CONFIG_CAN_ENABLED` disabled)
```cpp
else {
    tx_data.soc = 50;  // Test data
}
```
- Starts at 50% SOC
- Test mode controls whether it drifts

---

## Quick Reference

**To enable test mode (dummy data):**

[src/main.cpp](ESPnowtransmitter2/espnowtransmitter2/src/main.cpp#L333) line 333:
```diff
- TestMode::set_enabled(false);
+ TestMode::set_enabled(true);
```

**To disable test mode (live data):**
```diff
- TestMode::set_enabled(true);
+ TestMode::set_enabled(false);
```

**Rebuild:** `pio run` then flash with `pio run --target upload`

---

## Future Enhancement Needed

Once we add web UI toggle to transmitter, this will become:
- Toggle switch on transmitter's `/transmitter` page
- Receiver can see indicator on its `/transmitter` page
- No rebuild/flash needed

