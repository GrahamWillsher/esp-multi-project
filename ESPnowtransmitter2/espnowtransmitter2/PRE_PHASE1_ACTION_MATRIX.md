# Pre-Phase 1 Action Items & Decision Matrix

**Document Purpose:** Clear path forward once user provides clarifications  
**Date:** February 17, 2026  
**Status:** Template ready for user input

---

## PART A: User Input Required

### Section A.1: Hardware Configuration

#### Question 1: BMS Type
**Status:** [ ] Not answered | [X] Pending user input

| Option | Recommended | Details |
|--------|-------------|---------|
| Pylon LiFePO4 | ✅ YES | Simple CAN protocol, 500kbps standard, well-documented |
| Nissan Leaf | Possible | Phase 2 additional support |
| Tesla Model 3 | Possible | Phase 2 additional support |
| Other: _______ | | |

**Your Answer:** ________________________

**If Multiple BMS Types Needed:**
- [ ] Hardcoded for Phase 1 (simplest)
- [ ] Runtime selection in Phase 2 (more flexible)

**If Pylon is Selected:**
- CAN Speed: 500 kbps ✅ (standard)
- BMS Status Updates: 100-200ms (typical)
- Expect fields: voltage, current, SOC, temperature, cell voltages

---

#### Question 2: Inverter System
**Status:** [ ] Not answered | [X] Pending user input

| Factor | Your System |
|--------|-------------|
| **Inverter Present?** | Yes / No |
| **If Yes, Model:** | _________________ |
| **CAN Connection?** | Yes / No |
| **Integration Timing:** | Phase 1 / Phase 2 |

**If Inverter Present (Phase 1):**
- [ ] SMA Tripower (will be supported)
- [ ] Growatt HV (will be supported)
- [ ] Pylon CAN (will be supported)
- [ ] Other: _________________

**Inverter Responsibilities (if in Phase 1):**
- Receive: Battery SOC, voltage, power limits, status
- Send: "OK to charge" or "must stop" commands
- All via CAN
- Transmitter handles this, receiver knows only status

**Your Answer:** Inverter Present? ____ | Model: ____ | Phase 1? ____

---

#### Question 3: Charger System
**Status:** [ ] Not answered | [X] Pending user input

| Factor | Your System |
|--------|-------------|
| **Charger Present?** | Yes / No |
| **If Yes, Type:** | _________________ |
| **CAN Connection?** | Yes / No / Modbus |

**Charger Role (Informational):**
- Updates: `datalayer.charger.status`
- Receiver Display: "Charger connected, 3000W available"
- Control: Minimal (just presence detection)

**Your Answer:** Charger Present? ____ | Type: ____ | Connection: ____

---

#### Question 4: Shunt/Current-Sensor
**Status:** [ ] Not answered | [X] Pending user input

| Factor | Your System |
|--------|-------------|
| **Shunt Present?** | Yes / No |
| **If Yes, Type:** | _________________ |
| **Connection:** | CAN / RS485 / Analog GPIO |

**Shunt Role:**
- Provides accurate current measurement
- BMS current may be less accurate
- For Phase 1: BMS current sufficient; shunt can be Phase 2

**Your Answer:** Shunt Present? ____ | Type: ____ | Connection: ____

---

### Section A.2: Safety Thresholds

#### Question 5: Voltage Limits
**Status:** [ ] Not answered | [X] Pending user input

**Typical Values:**
```
Max Cell Voltage:     4.2V (NCA)  | 4.3V (NMC)  | 3.65V (LFP) ← Pylon
Min Cell Voltage:     2.5V (most) | 2.7V (safe)
Max Pack Voltage:     Cells × Count (typical: 400-500V)
Min Pack Voltage:     Cells × Count (typical: 250-350V)
```

**Your System (Pylon Example):**
- Max cell voltage: [ ] 3.60V [ ] 3.65V [ ] Other: ____
- Min cell voltage: [ ] 2.50V [ ] 2.70V [ ] Other: ____
- Cell count: ________
- Max pack voltage: ________ V
- Min pack voltage: ________ V

**Impact:**
- Transmitter monitors continuously
- If exceeded: stops charging/discharging
- Receiver displays warning

---

#### Question 6: Temperature Limits
**Status:** [ ] Not answered | [X] Pending user input

**Typical Values:**
```
Max Safe Temperature:  60°C (typical) | 50°C (conservative)
Min Safe Temperature:  0°C (most systems)
Max Charge Temp:       40°C (restrict when hot)
Min Charge Temp:       5°C (restrict when cold)
```

**Your System:**
- Max safe temperature: ________ °C
- Min safe temperature: ________ °C
- Max charge temperature: ________ °C
- Min charge temperature: ________ °C

**Impact:**
- Transmitter reads `datalayer.battery.status.temperature_max_dC`
- If exceeded: stop charging/discharging
- Receiver displays warning

---

#### Question 7: Current Limits
**Status:** [ ] Not answered | [X] Pending user input

**Typical Values:**
```
Max Discharge Current: 100A (typical) | varies by battery
Max Charge Current:    50A (typical) | varies by battery
```

**Your System:**
- Max discharge current: ________ A
- Max charge current: ________ A
- Who decides: BMS / User settings / Fixed

**Impact:**
- Inverter reads limits from datalayer
- Power calculated as: Power = Voltage × Current_limit
- Receiver displays limits

---

### Section A.3: Integration Scope

#### Question 8: Phase 1 Scope
**Status:** [ ] Not answered | [X] Pending user input

**Option A: Monitoring Only (Simpler, Recommended for Phase 1)**
```
Phase 1:
  ✅ Parse BMS CAN messages
  ✅ Display battery status on receiver
  ✅ Publish to MQTT
  ❌ Inverter control
  ❌ Contactor control
  ❌ Precharge sequence
```

**Option B: Include Control (More Complex, Phase 2)**
```
Phase 1:
  ✅ Parse BMS CAN messages
  ✅ Display battery status
  ✅ Inverter control loop
  ✅ Send control commands to inverter
  ❌ Contactor control
  ❌ Precharge sequence
```

**Your Preference:** [ ] A (Monitoring) | [ ] B (Control)

**If Option B (Inverter Control):**
- Must have: Real inverter or simulator for testing
- Phase 1 Testing: "Can inverter receive commands?"
- Phase 1 Success: "Inverter responds to power limit changes"

---

#### Question 9: Display Content
**Status:** [ ] Not answered | [X] Pending user input

**Compact Mode (Current, Recommended):**
```
Battery Status
Voltage:   385.0V
Current:   12.5A (charging)
Power:     4.8 kW
SOC:       65%
Temp:      25°C (max)
Status:    ✓ OK
```

**Detailed Mode (Phase 2):**
```
[All from compact]
+ Cell Count: 192
+ Max Charge Power: 5000W
+ Max Discharge Power: 8000W
+ Cell Voltage Range: 3.20-3.65V
```

**Graph Mode (Phase 2+):**
```
[Timeline graph of SOC, Power, Temp over last hour]
```

**Your Preference for Phase 1:** [ ] Compact | [ ] Detailed | [ ] Other

---

#### Question 10: Update Rates
**Status:** [ ] Not answered | [X] Pending user input

| Component | Current Setting | Your Preference |
|-----------|-----------------|-----------------|
| **CAN RX** | Every frame (~10-100Hz) | (Fixed) |
| **BMS Update** | Real-time | (Fixed) |
| **ESP-NOW TX** | 100ms (10Hz) | [ ] 50ms | [ ] 100ms | [ ] 200ms |
| **Display Refresh** | 500ms (2Hz) | [ ] 100ms | [ ] 500ms | [ ] 1000ms |
| **MQTT Publish** | 5000ms (0.2Hz) | [ ] 1s | [ ] 5s | [ ] 10s |

**Trade-offs:**
- Faster = More responsive, higher power draw
- Slower = Less responsive, lower power draw

**Recommended:** Keep defaults (100ms ESP-NOW, 500ms display, 5s MQTT)

---

## PART B: Derived Implementation Details (Auto-Generated from Answers)

### When User Provides Answers Above:

**Step 1: Create system_settings.h**
```cpp
// Generated from user answers
#define SELECTED_BMS_TYPE PYLON_BATTERY          // From Q1
#define CAN_SPEED_BMS 500                        // User or default

#define MAX_CELL_VOLTAGE_MV 3650                 // From Q5
#define MIN_CELL_VOLTAGE_MV 2700                 // From Q5
#define MAX_PACK_VOLTAGE_DV 4880                 // From Q5 (488.0V)
#define MIN_PACK_VOLTAGE_DV 2400                 // From Q5 (240.0V)

#define MAX_TEMPERATURE_DC 600                   // From Q6 (60.0°C)
#define MIN_TEMPERATURE_DC 0                     // From Q6

#define MAX_DISCHARGE_CURRENT_A 100              // From Q7
#define MAX_CHARGE_CURRENT_A 50                  // From Q7

#define HAVE_INVERTER (0 or 1)                   // From Q2
#if HAVE_INVERTER
  #define SELECTED_INVERTER_TYPE SMA_TRIPOWER_CAN
  #define CAN_SPEED_INVERTER 500
  #define INVERTER_CONTROL_INTERVAL_MS 200
#endif

#define ESPNOW_SYNC_INTERVAL_MS 100              // From Q10
#define DISPLAY_REFRESH_INTERVAL_MS 500          // From Q10
#define MQTT_PUBLISH_INTERVAL_MS 5000            // From Q10
```

**Step 2: Configure Battery Adapter**
```cpp
// In battery_adapter.cpp
bool BatteryAdapter::init_transmitter(BatteryType type) {
  // From Q1
  Battery* bms = create_battery(SELECTED_BMS_TYPE);
  
  // From Q2 (if yes)
  #if HAVE_INVERTER
    InverterProtocol* inverter = create_inverter(SELECTED_INVERTER_TYPE);
  #endif
  
  // From Q3 (if yes)
  #if HAVE_CHARGER
    CanCharger* charger = new SelectedChargerType();
  #endif
  
  return true;
}
```

**Step 3: Configure Safety Monitor**
```cpp
// In safety_monitor.cpp
void SafetyMonitor::check_limits() {
  // From Q5
  if (datalayer.battery.status.cell_max_voltage_mV > MAX_CELL_VOLTAGE_MV) {
    fault_over_voltage();
  }
  
  // From Q6
  if (datalayer.battery.status.temperature_max_dC > MAX_TEMPERATURE_DC) {
    fault_over_temperature();
  }
  
  // From Q7 (control loop)
  #if HAVE_INVERTER
    uint32_t limit = min(
      datalayer.battery.status.max_charge_power_W,
      MAX_CHARGE_CURRENT_A * datalayer.battery.status.voltage_dV / 10
    );
  #endif
}
```

---

## PART C: Verification Checklist (After Answers Provided)

### Pre-Build Verification

- [ ] **System Settings File Created**
  - Location: `src/system_settings.h`
  - Contains all Q1-Q10 answers
  - Reviewed by user

- [ ] **Hardware Connections Verified**
  - Transmitter CAN HAT: GPIO 12/13/14/15/32 verified
  - Battery connected to CAN bus
  - Inverter connected (if applicable)
  - Charger connected (if applicable)

- [ ] **BMS Documentation Ready**
  - CAN message format documented (Q1)
  - Message IDs listed (0x4210, 0x4220, etc.)
  - Refresh rate confirmed
  - Battery EMulator parser verified to support BMS type

- [ ] **Safety Thresholds Reviewed**
  - All limits (Q5-Q7) entered
  - Compared with BMS documentation
  - Verified with user

- [ ] **Integration Scope Confirmed**
  - Q8: Phase 1 = Monitoring or Control?
  - Q9: Display = Compact or Detailed?
  - Q10: Update rates confirmed
  - Testable in available hardware

---

## PART D: Build Readiness Checklist

**When Ready to Build (All above complete):**

- [ ] User answers questions A.1-A.3 (10 questions)
- [ ] system_settings.h created and reviewed
- [ ] Battery Emulator code tested (compilation)
- [ ] Hardware connections physical verified
- [ ] Test plan defined (what to check after build)
- [ ] Estimated timeline: Phase 1 = 3-5 days

---

## PART E: Testing Strategy (Post-Build)

### Test 1: CAN Communication
```
Expected: Transmitter receives BMS messages
Verify:
  ✓ Serial log shows: "[CAN] RX: 42 frames/sec"
  ✓ Datalayer fields updating
  ✓ No CAN errors
  ✓ Values match BMS display
```

### Test 2: ESP-NOW Transmission
```
Expected: Receiver displays battery data
Verify:
  ✓ Snapshot sent every 100ms
  ✓ Receiver receives ~95% of frames
  ✓ Display updates smoothly
  ✓ Values stable (no jitter)
```

### Test 3: Data Accuracy
```
Expected: Transmitted values match BMS
Verify:
  ✓ Voltage within 0.1V of BMS display
  ✓ SOC within 1% of BMS display
  ✓ Current within 1A of BMS display
```

### Test 4: Safety Limits (if applicable)
```
Expected: Transmitter enforces limits
Verify:
  ✓ Over-voltage → charging stops
  ✓ Over-current → discharge stops
  ✓ Over-temperature → control stops
```

---

## FINAL SIGN-OFF

**Ready for Phase 1 Build When:**

- [ ] All 10 questions answered (Part A)
- [ ] system_settings.h created
- [ ] Hardware verification complete
- [ ] Test strategy agreed
- [ ] User confirms: "Proceed with Phase 1"

**Expected Phase 1 Duration:** 3-5 days

**Expected Phase 1 Outcome:** "Transmitter sends real battery data, receiver displays it correctly"

---

**Next Step:** User completes Part A (answers 10 questions), then we generate Part B automatically.

