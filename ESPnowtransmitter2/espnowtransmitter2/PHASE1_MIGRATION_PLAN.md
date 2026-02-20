# Phase 1: Battery Emulator Migration Plan
**Status:** Starting  
**Date:** February 17, 2026  
**Objective:** Port Battery Emulator to 2-device architecture and get it working correctly

---

## Current State Assessment

### ✅ Already in Place
- **Battery Manager** (`src/battery/battery_manager.h/cpp`) - BMS integration skeleton
- **Datalayer** (`src/datalayer/datalayer.h`) - State container (copied from Battery Emulator)
- **CAN Driver** (`src/communication/can/can_driver.h/cpp`) - MCP2515 hardware abstraction
- **Battery Emulator Library** (`lib/battery_emulator_src/`) - Complete, unmodified
- **Main.cpp** - Framework in place, ready for integration
- **No compilation errors** - Project structure is valid

### ❌ Still Needed
1. **Implement battery_manager.cpp** - Create Battery instances, route CAN messages
2. **Verify system_settings.h** - Confirm it's loading Battery Emulator settings
3. **Test CAN reception** - Verify MCP2515 receives Pylon messages
4. **Verify data flow** - Confirm datalayer updates from CAN messages
5. **Test ESP-NOW transmission** - Verify snapshots sent to receiver
6. **Verify receiver display** - Confirm data renders correctly

---

## Phase 1 Implementation Steps

### STEP 0: Verification & Setup
**Goal:** Confirm hardware and configuration before coding

**0.1 User Decisions Required**
Before proceeding, user must answer 8 critical questions (see PRE_PHASE1_ACTION_MATRIX.md):
- [ ] BMS Type (Pylon? Other?)
- [ ] Inverter Present & Model?
- [ ] Charger Present?
- [ ] Shunt Present?
- [ ] Voltage Limits (min/max cell, min/max pack)
- [ ] Temperature Limits
- [ ] Current Limits
- [ ] Phase 1 Scope (monitoring-only or control?)

**0.2 Hardware Verification**
- [ ] Olimex ESP32-POE2 connected to Waveshare RS485/CAN HAT
- [ ] Waveshare HAT connected to Pylon battery CAN bus
- [ ] LilyGo T-Display-S3 available for receiver
- [ ] Serial connection established to transmitter for debugging

**0.3 Configuration File Setup**
Once user answers questions, create `src/system_settings.h`:
```cpp
// BMS selection
#define SELECTED_BMS_TYPE PYLON_BATTERY
#define CAN_SPEED_BMS 500  // kbps

// Safety thresholds (from Q5-Q7)
#define MAX_CELL_VOLTAGE_MV 3650
#define MIN_CELL_VOLTAGE_MV 2700
#define MAX_PACK_VOLTAGE_DV 4880
#define MIN_PACK_VOLTAGE_DV 2400
#define MAX_TEMPERATURE_DC 600
#define MAX_DISCHARGE_CURRENT_A 100
#define MAX_CHARGE_CURRENT_A 50

// Integration scope
#define HAVE_INVERTER 0
#define HAVE_CHARGER 0
#define HAVE_SHUNT 0
```

---

### STEP 1: Implement battery_manager.cpp
**Goal:** Create BMS instance, route CAN messages to Battery Emulator

**1.1 Create Battery Instance**
```cpp
// battery_manager.cpp - Constructor/Init
BatteryManager& BatteryManager::instance() {
  static BatteryManager mgr;
  return mgr;
}

bool BatteryManager::init(BatteryType battery_type) {
  current_battery_type_ = battery_type;
  
  // Create Battery Emulator instance
  switch(battery_type) {
    case BatteryType::PYLON_BATTERY:
      battery_ = new PYLON_BATTERY();  // From Battery Emulator
      break;
    case BatteryType::NISSAN_LEAF:
      battery_ = new NISSAN_LEAF();
      break;
    // ... other types
    default:
      return false;
  }
  
  // Call setup() - Battery Emulator will auto-register via CommunicationManager
  battery_->setup();
  
  LOG_INFO("BATTERY", "BMS initialized: %s", battery_->getBatteryType());
  return true;
}
```

**1.2 Route CAN Messages**
```cpp
// battery_manager.cpp - CAN routing
void BatteryManager::process_can_message(uint32_t can_id, const uint8_t* data, uint8_t dlc) {
  if (!battery_) return;
  
  // Route to ALL registered receivers (Battery Emulator's CommunicationManager)
  // This is how Battery Emulator's auto-registration works:
  // When battery_->setup() is called, it calls:
  //   CommunicationManager::instance().register_can_receiver(...)
  // So this call distributes to ALL registered receivers:
  CommunicationManager::instance().push_CAN_RX_ID(can_id);
  CommunicationManager::instance().push_CAN_RX_DATA(data, dlc);
}
```

**1.3 Update Periodic Transmitters**
```cpp
// battery_manager.cpp - Periodic updates
void BatteryManager::update_transmitters(unsigned long currentMillis) {
  if (!battery_) return;
  
  // Battery Emulator uses periodic CAN transmitters
  // This triggers all registered transmitters to send their CAN messages
  CommunicationManager::instance().send_can_messages();
}
```

**Validation:**
- [ ] Compiles without errors
- [ ] BatteryManager::init(BatteryType::PYLON_BATTERY) succeeds
- [ ] No undefined references

---

### STEP 2: Integrate Battery Manager into main.cpp
**Goal:** Initialize Battery Manager and CAN driver in startup

**2.1 Initialize CAN Driver**
In `main.cpp` setup():
```cpp
// After Ethernet initialization
LOG_INFO("CAN", "Initializing CAN driver...");
if (!CANDriver::instance().init()) {
  LOG_ERROR("CAN", "CAN driver initialization failed!");
  // Continue anyway - CAN is not critical for initial testing
}
```

**2.2 Initialize Battery Manager**
```cpp
// After CAN driver initialization
LOG_INFO("BATTERY", "Initializing Battery Manager...");
BatteryType selected_bms = BatteryType::PYLON_BATTERY;  // From system_settings.h later
if (!BatteryManager::instance().init(selected_bms)) {
  LOG_ERROR("BATTERY", "Battery Manager initialization failed!");
  return;  // Critical error
}
```

**2.3 Create CAN RX Task**
```cpp
// In main.cpp - create CAN reception task
void can_rx_task(void* param) {
  while (true) {
    CANDriver::instance().process_can_messages();
    // Process any pending CAN messages and route to BatteryManager
    vTaskDelay(pdMS_TO_TICKS(10));  // 100Hz CAN processing
  }
}

// In setup(), create task:
xTaskCreatePinnedToCore(
  can_rx_task,
  "CAN_RX",
  2048,
  nullptr,
  3,  // Priority
  nullptr,
  0   // Core 0
);
```

**2.4 Create BMS Update Task**
```cpp
// In main.cpp - periodic BMS updates (transmitters)
void bms_transmitter_task(void* param) {
  unsigned long last_update = 0;
  while (true) {
    unsigned long now = millis();
    
    // Update periodic BMS transmitters every 100ms
    if (now - last_update >= 100) {
      BatteryManager::instance().update_transmitters(now);
      last_update = now;
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// In setup(), create task:
xTaskCreatePinnedToCore(
  bms_transmitter_task,
  "BMS_TX",
  2048,
  nullptr,
  2,  // Priority
  nullptr,
  0   // Core 0
);
```

**Validation:**
- [ ] Project compiles
- [ ] CAN driver initializes successfully
- [ ] Battery Manager initializes successfully
- [ ] Serial log shows: "CAN initialized", "Battery initialized"
- [ ] No task overflow warnings

---

### STEP 3: Verify CAN Reception
**Goal:** Confirm MCP2515 receives Pylon battery messages

**3.1 Add Debug Logging**
```cpp
// In CANDriver::process_can_messages()
void CANDriver::process_can_messages() {
  can_frame frame;
  while (mcp2515_.readMessage(&frame) == MCP2515::ERROR_OK) {
    LOG_DEBUG("CAN", "RX: ID=0x%04X DLC=%d", frame.can_id, frame.can_dlc);
    
    // Route to Battery Manager
    BatteryManager::instance().process_can_message(
      frame.can_id,
      frame.data,
      frame.can_dlc
    );
  }
}
```

**3.2 Monitor Serial Output**
Connect to transmitter via serial and look for:
```
[CAN] Initializing CAN driver...
[CAN] CAN driver initialized
[BATTERY] Initializing Battery Manager...
[BATTERY] BMS initialized: PYLON_BATTERY
[CAN] RX: ID=0x4210 DLC=8
[CAN] RX: ID=0x4220 DLC=8
[CAN] RX: ID=0x4230 DLC=4
```

**Validation:**
- [ ] CAN messages arriving at ~10-20 frames/second
- [ ] Message IDs match Pylon CAN protocol (0x4210, 0x4220, 0x4230, etc.)
- [ ] No MCP2515 timeout errors
- [ ] No SPI communication errors

---

### STEP 4: Verify Datalayer Updates
**Goal:** Confirm Battery Emulator parses messages and updates datalayer

**4.1 Add Datalayer Monitoring**
```cpp
// In main.cpp - new monitoring task
void datalayer_monitor_task(void* param) {
  unsigned long last_log = 0;
  while (true) {
    unsigned long now = millis();
    
    // Log datalayer every 2 seconds
    if (now - last_log >= 2000) {
      extern DataLayer datalayer;  // From Battery Emulator
      
      LOG_INFO("DATALAYER",
        "Voltage=%.1fV Current=%.1fA SOC=%d%% Temp=%d°C",
        datalayer.battery.status.voltage_dV / 10.0,
        datalayer.battery.status.current_dA / 10.0,
        datalayer.battery.status.reported_soc / 100,
        datalayer.battery.status.temperature_max_dC / 10
      );
      
      last_log = now;
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// In setup():
xTaskCreatePinnedToCore(
  datalayer_monitor_task,
  "DATALAYER",
  2048,
  nullptr,
  1,
  nullptr,
  0
);
```

**3.2 Monitor Serial Output**
Expected output:
```
[DATALAYER] Voltage=400.5V Current=12.3A SOC=65% Temp=25°C
[DATALAYER] Voltage=400.6V Current=12.2A SOC=65% Temp=25°C
[DATALAYER] Voltage=400.7V Current=12.4A SOC=65% Temp=25°C
```

**Validation:**
- [ ] Voltage is non-zero and in reasonable range (300-500V for LFP)
- [ ] Current updates when charging/discharging
- [ ] SOC is between 0-100%
- [ ] Temperature is in valid range (0-100°C)
- [ ] Values are stable (not jittering)
- [ ] Values match what BMS display shows

---

### STEP 5: Verify ESP-NOW Transmission
**Goal:** Confirm transmitter sends datalayer snapshot every 100ms

**5.1 Check data_sender.cpp**
```cpp
// In espnow/data_sender.cpp - verify it reads datalayer
void DataSender::send_battery_data() {
  extern DataLayer datalayer;
  
  EnhancedCache cache;
  cache.voltage_dV = datalayer.battery.status.voltage_dV;
  cache.current_dA = datalayer.battery.status.current_dA;
  cache.soc = datalayer.battery.status.reported_soc / 100;
  
  // Send via ESP-NOW
  espnow_send_utils::send(&cache);
}
```

**5.2 Verify Transmission Rate**
Add timing validation:
```cpp
// In DataSender::send_battery_data()
static unsigned long last_send = 0;
unsigned long now = millis();
unsigned long interval = now - last_send;

if (interval > 150 || interval < 50) {
  LOG_WARN("ESPNOW", "Irregular send interval: %ldms", interval);
}
last_send = now;
```

**Expected behavior:**
- Snapshot sent every 100ms ± 10ms
- No "Irregular send interval" warnings
- Receiver gets ~95% of frames

**Validation:**
- [ ] Transmitter sends ~10 frames/second
- [ ] Send interval is stable (100ms ± 10ms)
- [ ] No transmission errors in log

---

### STEP 6: Verify Receiver Display
**Goal:** Confirm receiver displays correct battery data

**6.1 Check Receiver Compilation**
Build receiver in `espnowreciever_2/`:
```bash
cd espnowreciever_2
platformio run --target upload
```

**6.2 Monitor Receiver Output**
Connect receiver serial and verify:
```
[ESPNOW] Received packet from transmitter
[DISPLAY] Voltage: 400.5V
[DISPLAY] Current: 12.3A
[DISPLAY] SOC: 65%
[DISPLAY] Temp: 25°C
```

**6.3 Physical Display Check**
Look at LilyGo T-Display-S3 screen:
- Should show battery voltage, current, SOC, temperature
- Should update smoothly (~2Hz for 500ms refresh)
- Values should match transmitter serial log

**Validation:**
- [ ] Receiver compiles without errors
- [ ] Receiver receives all transmitted packets
- [ ] Display updates smoothly
- [ ] Values match transmitter datalayer
- [ ] No display glitches or freezing

---

## Success Criteria for Phase 1

### Transmitter (CAN ← Battery)
- [ ] CAN driver initializes without errors
- [ ] CAN messages received from Pylon battery (10-20 fps)
- [ ] Message IDs match Pylon protocol
- [ ] No CAN timeout or SPI errors
- [ ] Datalayer fields update correctly (voltage, current, SOC, temp)
- [ ] Values are stable and accurate (within sensor tolerance)
- [ ] Periodic updates sent every 100ms ± 10ms

### Receiver (Display ← ESP-NOW)
- [ ] ESP-NOW packets received (95%+ reception rate)
- [ ] Display shows battery data correctly
- [ ] Display updates smoothly
- [ ] Values match transmitter datalayer

### System Integration
- [ ] No memory leaks or stack overflows
- [ ] No watchdog resets
- [ ] Both devices can run continuously for 1+ hour
- [ ] Data accuracy: voltage within 0.1V, current within 1A, SOC within 1%

---

## Troubleshooting Guide

### Issue: CAN messages not received
**Symptoms:** Log shows "CAN timeout" or no RX messages
**Solutions:**
1. Verify Waveshare HAT GPIO connections (14/13/12/15/32)
2. Check CAN bus termination (should have 120Ω resistor)
3. Verify Pylon battery CAN is connected correctly
4. Check MCP2515 clock: should be 8MHz
5. Verify CAN speed: Pylon uses 500kbps

### Issue: Datalayer not updating
**Symptoms:** Voltage/current/SOC all zero
**Solutions:**
1. Verify CAN messages are being received (check log)
2. Confirm Battery Emulator library is linked correctly
3. Verify system_settings.h is loaded
4. Check that battery_->setup() was called
5. Verify battery type matches actual CAN messages

### Issue: ESP-NOW transmission failing
**Symptoms:** Receiver never gets packets
**Solutions:**
1. Verify receiver is in pairing mode or has correct transmitter MAC
2. Check WiFi is disabled on transmitter (can interfere with ESP-NOW)
3. Verify ESP-NOW channel configuration matches receiver
4. Check for out-of-memory errors (reduce log level if needed)

### Issue: Receiver display not updating
**Symptoms:** Display shows old/zero data
**Solutions:**
1. Verify receiver is receiving ESP-NOW packets
2. Check display code is reading from correct data structure
3. Verify display refresh rate (should be ~2Hz)
4. Check for display driver errors in serial log

---

## Timeline Estimates

**Step 0 (Verification):** 1 day
- User answers 8 questions
- Create system_settings.h
- Verify hardware connections

**Step 1 (Battery Manager):** 1-2 days
- Implement battery_manager.cpp
- Integrate with main.cpp
- Test compilation

**Step 2 (CAN Reception):** 1-2 days
- Debug CAN RX
- Verify message format
- Test with real Pylon battery

**Step 3 (Datalayer):** 1 day
- Verify data population
- Compare with BMS display
- Validate accuracy

**Step 4 (ESP-NOW):** 1 day
- Verify transmission timing
- Check reception rate
- Monitor signal quality

**Step 5 (Receiver):** 1-2 days
- Build receiver
- Display validation
- End-to-end testing

**Total Phase 1: 6-10 days**

---

## Next Steps

1. **User provides answers** to 8 critical questions (PRE_PHASE1_ACTION_MATRIX.md)
2. **Create system_settings.h** with confirmed parameters
3. **Implement battery_manager.cpp** (Step 1)
4. **Follow steps 2-6** sequentially
5. **Validate** against success criteria
6. **Document findings** in new Phase1_Implementation_Results.md
7. **Proceed to Phase 2** (enhancements) once Phase 1 complete

---

## Notes

- **No refactoring:** Keep Battery Emulator code exactly as-is
- **Thin integration layer only:** Only touch battery_manager.*, can_driver.*, main.cpp
- **Serial logging is your friend:** Use detailed logging for debugging
- **One step at a time:** Don't skip verification steps
- **Hardware matters:** Issues are usually GPIO, CAN bus, or timing

Phase 1 goal: **Working system first, perfect code later.**
