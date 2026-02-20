# Battery Emulator Migration: Quick Reference & Action Items

**Document Date:** February 17, 2026  
**Focus:** Immediate action items to complete Battery Emulator integration  

---

## Key Architectural Decisions (FINAL)

### 1. Device Responsibilities

**Transmitter (Olimex ESP32-POE2):**
- ✅ Reads CAN bus (Battery Emulator BMS parsers parse real data)
- ✅ Maintains global datalayer (Battery Emulator's state machine)
- ✅ Runs inverter control logic (makes power decisions)
- ✅ Sends battery snapshot via ESP-NOW every 100ms
- ✅ Publishes state to MQTT broker
- ✅ Handles configuration via NVS

**Receiver (LilyGo T-Display-S3):**
- ✅ Receives ESP-NOW battery snapshots
- ✅ Displays battery status on TFT screen
- ✅ Provides web UI for configuration
- ❌ Cannot parse CAN (no CAN hardware)
- ❌ Cannot run inverter control (no CAN to inverter)
- ❌ Cannot trigger contactors (no GPIO)

**Result:** Device boundary is **transparent to Battery Emulator logic**. All control stays on transmitter.

---

## Critical Issues Resolved

### Issue 1: HAL Conflicts ✅
**Resolution:** Delete `hal_minimal.h` and `hal_minimal.cpp`  
**Status:** DONE (already removed)

### Issue 2: Enum Naming Conflicts ✅
**Resolution:** Rename Battery Emulator enums to avoid conflicts
```cpp
bms_status_enum → BatteryEmulator_bms_status_enum
real_bms_status_enum → BatteryEmulator_real_bms_status_enum
BMS_FAULT = 3 → BMS_FAULT_EMULATOR = 3
```
**Status:** DONE (already updated)

### Issue 3: Datalayer Field Access ✅
**Resolution:** Update existing code to use `datalayer.battery.status.*` instead of `datalayer.status.*`
```cpp
// OLD: datalayer.status.reported_soc
// NEW: datalayer.battery.status.reported_soc

// OLD: datalayer.status.active_power_W  
// NEW: datalayer.battery.status.active_power_W

// OLD: datalayer.initialized
// NEW: Added compatibility getter to DataLayer class
```
**Status:** DONE (already updated in main.cpp, data_sender.cpp, can_driver.cpp)

### Issue 4: Inverter Control Location ✅
**Resolution:** Keep inverter control on transmitter only (correct design)  
**Impact:** Receiver cannot make safety decisions—only displays data  
**Status:** CONFIRMED (no changes needed)

---

## Immediate Action Items (Next 24 Hours)

### 1. Create `system_settings.h` for Transmitter

**Location:** `src/system_settings.h`

**Content:**
```cpp
#ifndef SYSTEM_SETTINGS_H
#define SYSTEM_SETTINGS_H

// CAN Configuration for Olimex ESP32-POE2 + MCP2515
#define CAN_SPEED_BMS 500       // Pylon uses 500kbps
#define CAN_SPEED_CHARGER 500
#define CAN_SPEED_INVERTER 500

// MCP2515 Settings
#define MCP2515_CRYSTAL_MHZ 8   // 8MHz crystal on Waveshare HAT
#define MCP2515_CS_PIN 15       // Chip Select
#define MCP2515_INT_PIN 32      // Interrupt

// Transmitter Configuration
#define SELECTED_BMS_TYPE PYLON_BATTERY
#define SELECTED_INVERTER_TYPE SMA_TRIPOWER_CAN
#define DUAL_BATTERY_ENABLED false

#endif
```

**Why:** Battery Emulator includes don't have this—we must define it.

---

### 2. Verify Compilation

**Command:**
```bash
cd c:\Users\GrahamWillsher\ESP32Projects\ESPnowtransmitter2\espnowtransmitter2
platformio run
```

**Expected:** ✅ Successful build (no errors or warnings)

**If Failures Occur:**
1. Check for missing includes: `#include "system_settings.h"` where needed
2. Check for GPIO enum conflicts: Search for `GPIO_NUM` redefinitions
3. Check for function symbol conflicts: `register_can_receiver`, `register_transmitter`

---

### 3. Test Compilation Output

**Success Indicators:**
```
Processing olimex_esp32_poe2
...
Compiling src/battery/battery_manager.cpp
Compiling lib/battery_emulator_src/battery/PYLON-BATTERY.cpp
Compiling lib/battery_emulator_src/communication/CommunicationManager.cpp
...
Linking .pio/build/olimex_esp32_poe2/firmware.elf
✓ BUILD SUCCESSFUL
RAM: 17% | Flash: 58%
```

**Failure Indicators to Fix:**
- `error: 'GPIO_NUM_0' expected '(' before numeric constant` → Check hal includes
- `error: 'class DataLayer' has no member named 'status'` → Check datalayer field usage
- `error: 'register_can_receiver' was not declared` → Check includes
- `error: multiple definition of 'DataLayer datalayer'` → Check header guards

---

## Week-by-Week Plan

### Week 1: Foundation (THIS WEEK)
- [ ] Day 1: Verify compilation works
- [ ] Day 2: Test with real Pylon CAN data (if available)
- [ ] Day 3-4: Debug any compilation/runtime issues
- [ ] Day 5: Document any issues found

### Week 2: BMS Integration
- [ ] Add second BMS type support (Nissan Leaf, Tesla, BYD)
- [ ] Implement BMS selection via NVS config
- [ ] Test switching between BMS types

### Week 3: Inverter Control
- [ ] If inverter connected: integrate inverter control
- [ ] Test inverter receives correct power limits
- [ ] Implement failsafe (stop control if BMS offline)

### Week 4: Display & MQTT
- [ ] Extend ESP-NOW payload with full battery status
- [ ] Update receiver display with voltage, current, temp
- [ ] Implement MQTT publishing of datalayer

---

## File Changes Summary

### Modified Files ✅
- `src/datalayer/datalayer.h` - Added compatibility getters
- `src/main.cpp` - Updated datalayer field access
- `src/espnow/data_sender.cpp` - Updated field access
- `src/communication/can/can_driver.cpp` - Updated field access
- `src/battery/battery_manager.h` - Added BatteryType enum
- `src/battery/battery_manager.cpp` - Fixed enum values
- `lib/battery_emulator_src/devboard/utils/types.h` - Renamed enums

### New Files to Create
- `src/system_settings.h` - Transmitter-specific settings

### Deleted Files ✅
- `lib/battery_emulator_src/devboard/hal/hal_minimal.h` - DELETED
- `lib/battery_emulator_src/devboard/hal/hal_minimal.cpp` - DELETED

---

## Build Command (Quick Reference)

```bash
# Clean build
cd espnowtransmitter2 && platformio run --target clean && platformio run

# With verbose output
platformio run -v

# Build specific target
platformio run -e olimex_esp32_poe2
```

---

## Runtime Testing Checklist

### Phase 1A: Power-On (Hardware Verification)
- [ ] Transmitter boots without reset
- [ ] Serial output shows firmware version
- [ ] Ethernet link LED on
- [ ] No compilation errors in log

### Phase 1B: CAN Bus (With Pylon Battery)
```
Expected log output:
[CAN] CAN driver initialized (500kbps)
[BMS] Initializing Battery Emulator...
[BMS] ✓ Battery Emulator initialized (Pylon)
[DATALAYER] ✓ Datalayer initialized
[DATA_SENDER] Sending test data...
[CAN] Stats: RX=42, TX=0, Errors=0, BMS=connected
```

- [ ] Serial shows "CAN driver initialized"
- [ ] Serial shows "Battery Emulator initialized"
- [ ] CAN RX message counter increases every second
- [ ] No CAN errors reported
- [ ] "BMS=connected" status appears

### Phase 1C: Datalayer Verification (With CAN Data)
```cpp
// Add to main loop for testing
if (millis() % 5000 == 0) {
  LOG_INFO("TEST", "Voltage: %.1f V", 
    datalayer.battery.status.voltage_dV / 10.0);
  LOG_INFO("TEST", "SOC: %d%%", 
    datalayer.battery.status.reported_soc / 100);
  LOG_INFO("TEST", "Power: %d W", 
    datalayer.battery.status.active_power_W);
}
```

- [ ] Voltage value changes (not stuck at 0)
- [ ] SOC value changes based on BMS
- [ ] Power value positive/negative based on charging/discharging

---

## Troubleshooting Guide

### Build Fails with "expected identifier before numeric constant"
**Cause:** HAL GPIO macro conflict  
**Solution:** Verify `hal_minimal.h` is deleted
```bash
ls -la lib/battery_emulator_src/devboard/hal/hal_minimal.h  # Should NOT exist
```

### Build Fails with "'DataLayer' has no member named 'status'"
**Cause:** Old code still using wrong field name  
**Solution:** Search for `datalayer.status.` and replace with `datalayer.battery.status.`
```bash
grep -r "datalayer\.status\." src/
# Should return ZERO results
```

### Build Fails with "multiple definition of 'datalayer'"
**Cause:** Datalayer instantiated in multiple files  
**Solution:** Verify `extern` declaration in datalayer.cpp
```cpp
DataLayer datalayer;  // Should be in datalayer.cpp ONLY
// Not in header, not in other .cpp files
```

### Runtime: "BMS=disconnected" in logs
**Cause:** No CAN messages received  
**Solution:**
1. Check CAN hardware connections (SCK, MOSI, MISO, CS, INT)
2. Verify Pylon battery is on and connected to CAN
3. Check CAN speed: 500kbps matches Pylon
4. Look for CAN error counter increasing

### Runtime: Datalayer fields not updating
**Cause:** BMS parser not called  
**Solution:**
1. Add debug logging to PYLON_BATTERY::receive_can_frame()
2. Verify CAN messages are received (check RX counter)
3. Check message filtering (Pylon uses 0x4210, 0x4220, etc.)

---

## Success Metrics

| Metric | Target | Current | Status |
|--------|--------|---------|--------|
| Compilation | 0 errors | TBD | ⏳ |
| Build time | < 60s | TBD | ⏳ |
| Flash size | < 1.2MB | TBD | ⏳ |
| CAN RX rate | > 10 msg/sec | TBD | ⏳ |
| Datalayer update latency | < 2ms | TBD | ⏳ |
| ESP-NOW TX rate | 10 Hz | TBD | ⏳ |
| Data accuracy vs BMS | > 99% | TBD | ⏳ |
| System uptime | > 24h | TBD | ⏳ |

---

## Critical Do's and Don'ts

### ✅ DO:
- Keep Battery Emulator logic unchanged (it's proven)
- Keep inverter control on transmitter only
- Test with real hardware as soon as possible
- Log all datalayer access for debugging
- Document any workarounds

### ❌ DON'T:
- Modify Battery Emulator's core files (especially Battery.h, CanBattery.h)
- Try to run BMS parsers on receiver (no CAN bus)
- Try to make datalayer changes without testing on hardware
- Ignore compilation warnings (they become bugs later)
- Assume enum names are consistent (rename if conflicts)

---

## Documentation Files Created

1. **BATTERY_EMULATOR_MIGRATION_ARCHITECTURE.md** (20KB)
   - Complete architectural analysis
   - Datalayer structure (800 bytes)
   - 50+ BMS parser types
   - 15+ inverter protocols
   - Step-by-step integration plan

2. **BATTERY_EMULATOR_QUICK_REFERENCE.md** (this file)
   - Action items
   - Build commands
   - Testing checklist
   - Troubleshooting guide

---

## Contact Points for Help

### For CAN/BMS Issues:
- Battery Emulator source: `lib/battery_emulator_src/battery/PYLON-BATTERY.cpp`
- Datasheet: Look for CAN message definitions (0x4210, 0x4220, etc.)

### For Communication Issues:
- Battery Emulator source: `lib/battery_emulator_src/communication/can/comm_can.h`
- Registration system in `register_can_receiver()` function

### For Integration Issues:
- Check datalayer sync between devices
- Verify ESP-NOW payload structure
- Monitor for data corruption via checksums

---

**Next Action:** Build transmitter and verify compilation. If successful, test with real Pylon CAN data.

