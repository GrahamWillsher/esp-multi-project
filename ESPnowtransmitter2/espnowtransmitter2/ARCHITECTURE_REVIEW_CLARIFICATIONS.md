# Architecture Review: Items Requiring Clarification

**Document:** Comprehensive Review of Updated Migration Architecture  
**Date:** February 17, 2026  
**Focus:** Identifying remaining gaps and clarifications needed before Phase 1 build

---

## Overview

The updated architecture document now clearly separates concerns between transmitter (control) and receiver (display). However, several items require user clarification before implementation can proceed.

---

## CRITICAL DECISIONS NEEDED

### 1. BMS Type Selection & Configuration

**Current State:** Document assumes Pylon BMS is primary target

**Questions:**
- [ ] **Q1.1:** Is the connected battery definitely a Pylon LiFePO4 system?
- [ ] **Q1.2:** Or is battery type variable (need runtime selection)?
- [ ] **Q1.3:** Should receiver web UI allow BMS type change?
  - Option A: Hardcoded at compile-time (secure, simpler)
  - Option B: Runtime selection via web UI (flexible, more complex)
- [ ] **Q1.4:** Are there CAN bus speed differences per BMS type?
  - Most use 500kbps
  - Some use 250kbps or 1000kbps
  - Decision: Standard to 500kbps, make configurable in Phase 2?

**Impact on Implementation:**
- **If A:** BMS type hardcoded in `battery_adapter.cpp` during init
- **If B:** NVS stores selection, transmitted to receiver via MQTT

**Recommended:** Option A (Phase 1: Pylon hardcoded, Phase 2: runtime selection)

---

### 2. Inverter System Integration

**Current State:** Document references 15+ inverter protocols, but unclear if inverter is actually connected

**Questions:**
- [ ] **Q2.1:** Is an inverter connected to the CAN bus?
- [ ] **Q2.2:** If yes, which model?
  - SMA Tripower?
  - Growatt?
  - Pylon CAN?
  - Other?
- [ ] **Q2.3:** Is inverter control in Phase 1 or Phase 2?
  - Option A: Phase 1 (must initialize inverter controller)
  - Option B: Phase 2 (monitor-only for Phase 1)
- [ ] **Q2.4:** Are there contactors/relays controlled by transmitter?
  - For: precharge, main contactor, charge-only
  - GPIO control required?

**Impact on Implementation:**
- **If inverter in Phase 1:**
  - Create inverter instance in `battery_adapter::init_transmitter()`
  - Inverter class auto-registers as transmitter
  - 200ms control loop calls `transmit_can()`
  - Tests need real inverter or simulator

- **If inverter in Phase 2:**
  - Skip inverter initialization for now
  - BMS parser still updates datalayer
  - System still receives battery data
  - Simpler Phase 1 testing

**Recommended:** Clarify whether real inverter is connected. If yes, which model. Suggest Phase 1 = monitor-only, Phase 2 = control loop.

---

### 3. Charger System

**Current State:** Battery Emulator includes charger parsers, but not mentioned for this system

**Questions:**
- [ ] **Q3.1:** Is a charger connected to the CAN bus?
- [ ] **Q3.2:** If yes, which model?
  - Nissan Leaf charger?
  - Generic Modbus charger?
  - Other?
- [ ] **Q3.3:** Charger role in Phase 1?
  - Option A: Parse charger status (optional info)
  - Option B: Skip charger (not needed for monitoring)

**Impact on Implementation:**
- Charger updates `datalayer.charger.status`
- Informational only (receiver displays "Charger connected")
- Zero control impact

**Recommended:** Include charger parser if available, skip if not. Non-blocking for Phase 1.

---

### 4. Shunt (Power Measurement) Integration

**Current State:** Battery Emulator includes shunt support (external current sensors)

**Questions:**
- [ ] **Q4.1:** Is a shunt/current-sensor module connected?
- [ ] **Q4.2:** If yes, which protocol?
  - CAN direct?
  - RS485 Modbus?
  - Analog input?
- [ ] **Q4.3:** Shunt data in datalayer?
  - Updates `datalayer.shunt.status`
  - Used for power calculation?

**Impact on Implementation:**
- If CAN: Shunt parser similar to BMS
- If RS485: Need separate RS485 driver
- If analog: Custom GPIO ADC code

**Recommended:** Clarify shunt integration. For Phase 1, BMS `current_dA` is sufficient; shunt can be Phase 2.

---

### 5. Real-Time Safety Constraints

**Current State:** Architecture assumes safety decisions on transmitter

**Questions:**
- [ ] **Q5.1:** What are the hard limits?
  - Max cell voltage? (typical: 4.3V)
  - Min cell voltage? (typical: 2.7V)
  - Max temperature? (typical: 60°C)
  - Max discharge current? (varies: 50-200A)
  - Max charge current? (varies: 10-50A)

- [ ] **Q5.2:** Response to fault?
  - Cut contactors immediately?
  - Log event?
  - Alert user?

- [ ] **Q5.3:** Latency requirement?
  - Sub-second? (< 100ms)
  - Immediate? (< 10ms)
  - Or just informational?

**Impact on Implementation:**
- Values drive datalayer configuration
- Inverter control code reads these limits
- Safety monitoring runs on transmitter only

**Recommended:** Define all safety thresholds. Transmitter monitors, receiver displays status only.

---

## ARCHITECTURAL CLARITY ITEMS

### 6. HAL Separation Strategy Confirmation

**Current Approach:** Compile-time HAL selection via platformio.ini flags

**Confirmation Needed:**
- [ ] **Q6.1:** Is compile-time separation acceptable?
  - Alternative: Runtime detection (more complex)
  - Alternative: Single unified HAL (code duplication)
- [ ] **Q6.2:** Should transmitter and receiver be in same repository?
  - Current: Separate projects (espnowtransmitter2 vs espnowreciever_2)
  - Alternative: Single monorepo with two targets
- [ ] **Q6.3:** Is GPIO differentiation sufficient?
  - Transmitter: CAN, Ethernet
  - Receiver: Display, Touch
  - Or do we need runtime board detection?

**Recommendation:** Compile-time separation is clean and efficient. Keep separate projects.

---

### 7. Datalayer Synchronization Scope

**Current Approach:** Receiver syncs only `datalayer.battery.status` (every 100ms)

**Confirmation Needed:**
- [ ] **Q7.1:** Is battery.status sufficient?
  - Includes: voltage, current, SOC, power, temperature, cell count, limits, status
  - Excludes: cell voltages array (192 entries = large)
  - Excludes: battery.info (static, rarely changes)
  - Excludes: battery.settings (configuration)

- [ ] **Q7.2:** Should we sync cell voltages?
  - Current: No (array is 192×2 bytes = 384 bytes per frame)
  - Alternative: Sync on-demand via MQTT
  - Alternative: Summary only (min/max voltage)

- [ ] **Q7.3:** Sync frequency?
  - Current: 100ms
  - Could be: 500ms (lower power on receiver)
  - Could be: 1000ms (less responsive display)

**Impact:**
- Larger payloads = more power/bandwidth
- Faster updates = more responsive display

**Recommendation:** Keep 100ms for current (responsive). Cell voltages via MQTT on-demand (Phase 2).

---

### 8. Error Handling & Failsafe Behavior

**Current Assumptions:**
- If ESP-NOW link drops: Receiver shows stale data, no safety impact
- If CAN link drops: Transmitter stops control, safe state
- If MQTT drops: No telemetry, local operation continues

**Confirmation Needed:**
- [ ] **Q8.1:** Is this failsafe behavior acceptable?
  - Or should receiver command transmitter to stop?
  - Or should there be a heartbeat watchdog?

- [ ] **Q8.2:** What's "safe state" when CAN drops?
  - Stop charging/discharging?
  - Open contactors?
  - Alert user only?

- [ ] **Q8.3:** How long can data be stale?
  - Receiver shows "last update: 500ms ago" after 500ms
  - After 5 seconds, disconnect warning?
  - After 30 seconds, disable discharge?

**Impact:**
- Affects CAN error handling code
- Affects ESP-NOW retry logic
- Affects safety monitor implementation

**Recommendation:** Define timeout thresholds. Transmitter is always-safe (local decisions). Receiver is best-effort display (not safety-critical).

---

### 9. Configuration Persistence Model

**Current Approach:** 
- Transmitter: NVS (non-volatile storage)
- Receiver: LittleFS local

**Confirmation Needed:**
- [ ] **Q9.1:** Config change authority?
  - Who decides BMS type: transmitter, receiver, or both?
  - Recommendation: Transmitter decides, receiver reads
  
- [ ] **Q9.2:** Config sync timing?
  - Every startup?
  - Only when changed?
  - Continuous MQTT subscription?

- [ ] **Q9.3:** Factory reset support?
  - How: Button combo? MQTT command?
  - What resets: Just receiver? Both?

**Impact:**
- Code complexity in config managers
- MQTT topic structure

**Recommendation:** Transmitter is source of truth. Receiver reads and caches. Changes go through receiver web UI → MQTT → transmitter NVS.

---

### 10. Display Content & Refresh Rate

**Current Approach:** 
- Receiver displays: SOC, voltage, current, power, temperature, status
- Refresh: 500ms

**Confirmation Needed:**
- [ ] **Q10.1:** Additional metrics to display?
  - Cell count? (informational)
  - Max charge power? (limit info)
  - Cell voltages? (requires separate request)
  - Charger status? (if charger connected)

- [ ] **Q10.2:** Display modes?
  - Compact (current design)
  - Detailed (all metrics)
  - Graph (power over time)

- [ ] **Q10.3:** Refresh rate?
  - Current: 500ms (smooth, lower power)
  - Could be: 100ms (matches data rate)
  - Could be: 1000ms (very smooth, lower power)

**Impact:**
- Screen rendering time
- Battery drain on receiver
- User experience

**Recommendation:** Compact mode default, 500ms refresh rate. Detailed/graph modes Phase 2.

---

## VERIFICATION POINTS

### 11. Battery Emulator Code Integration Verification

**What needs verification:**
- [ ] **V11.1:** All BMS parsers compile without errors
  - Target: PYLON_BATTERY at minimum
  - Nice to have: All 50+ BMS types available
  
- [ ] **V11.2:** Auto-registration works
  - Constructor calls `register_can_receiver()`
  - No external factory needed
  - All global lists populated correctly

- [ ] **V11.3:** Datalayer updates correctly
  - CAN frame → BMS parser → datalayer
  - All fields populate (voltage, current, SOC, temp, etc.)
  - No memory corruption
  - No race conditions (multi-task access)

- [ ] **V11.4:** Inverter control works (if in Phase 1)
  - Datalayer → inverter controller → CAN frame
  - Real inverter receives control commands
  - Inverter changes operating state

**Testing:**
- Real hardware test with Pylon battery
- Monitor serial logs for data accuracy
- Compare values with BMS display unit

---

### 12. Device Communication Verification

**What needs verification:**
- [ ] **V12.1:** ESP-NOW transmission works
  - Transmitter sends snapshot every 100ms
  - Receiver gets 95%+ delivery rate
  - Latency < 200ms

- [ ] **V12.2:** MQTT publishing works
  - Transmitter connects to broker
  - Publishes datalayer fields
  - Receiver subscribes and receives

- [ ] **V12.3:** Configuration sync works
  - Changes propagate correctly
  - No corruption in NVS/LittleFS
  - Survives reboot

---

### 13. Power Consumption Verification

**Targets (to be confirmed):**
- [ ] **P13.1:** Transmitter power draw?
  - Ethernet active: ~100-200mA
  - CAN active: ~50mA
  - WiFi off: saves ~50mA
  - Target total: < 300mA

- [ ] **P13.2:** Receiver power draw?
  - Display on (TFT): ~50-100mA
  - WiFi active: ~80mA
  - ESP-NOW RX: ~80mA
  - Internal battery: 1000mAh, run time ~4-6 hours

**Impact:**
- Transmitter: Powered from Pylon or external PSU
- Receiver: Can use internal battery, or USB power

---

## DOCUMENTATION GAPS

### 14. System Settings Definition

**Missing:** `src/system_settings.h` for transmitter

**Should Define:**
```cpp
// CAN speeds
#define CAN_SPEED_BMS 500          // kbps
#define CAN_SPEED_INVERTER 500
#define CAN_SPEED_CHARGER 500

// MCP2515 crystal (from Waveshare datasheet)
#define MCP2515_CRYSTAL_MHZ 8

// Safety limits (should come from user input)
#define MAX_CELL_VOLTAGE_MV 4300   // 4.3V
#define MIN_CELL_VOLTAGE_MV 2700   // 2.7V
#define MAX_PACK_TEMP_DC 600       // 60.0°C
#define MAX_DISCHARGE_CURRENT_A 100
#define MAX_CHARGE_CURRENT_A 50

// Sync rates
#define ESPNOW_SYNC_INTERVAL_MS 100
#define MQTT_PUBLISH_INTERVAL_MS 5000
```

**Needed from User:**
- [ ] Exact safety thresholds
- [ ] Exact BMS type
- [ ] Exact inverter model (if applicable)

---

### 15. Protocol Specifications

**Missing:** Detailed message formats

**What's Needed:**
- [ ] **M15.1:** CAN frame IDs for Pylon BMS
  - 0x4210 voltage/current/SOC
  - 0x4220 charge/discharge limits
  - 0x4230 cell voltage min/max
  - 0x4240 temperature min/max

- [ ] **M15.2:** CAN frame IDs for SMA inverter (if applicable)
  - Control message format
  - Response format
  - Timing requirements

- [ ] **M15.3:** ESP-NOW payload format
  - Battery snapshot struct (already defined)
  - Command struct (from receiver to transmitter)
  - ACK mechanism

- [ ] **M15.4:** MQTT topic structure
  - `battery/voltage_dv`
  - `battery/current_da`
  - `battery/soc_pppt`
  - `config/bms_type`
  - etc.

---

## RECOMMENDED NEXT STEPS

### Phase 0: Clarification (Before Build)

1. **Answer all CRITICAL DECISIONS (Section 1-5)**
   - BMS type and configuration
   - Inverter presence and model
   - Charger presence
   - Shunt presence
   - Safety thresholds

2. **Confirm ARCHITECTURAL CHOICES (Section 6-10)**
   - HAL separation approach
   - Datalayer sync scope
   - Error handling
   - Display design

3. **Create SYSTEM SETTINGS FILE**
   - CAN speeds
   - Safety limits
   - Sync rates
   - Define MQTT topics

4. **Verify PROTOCOL SPECS**
   - Pylon CAN message formats
   - Inverter formats (if applicable)
   - ESP-NOW payload
   - MQTT topics

### Phase 1: Build & Verify (After Clarifications)

1. **Compile transmitter**
   - With hardcoded PYLON_BATTERY
   - Verify no compilation errors
   - Check binary size

2. **Test with real hardware**
   - Connect Pylon battery to CAN bus
   - Monitor datalayer updates
   - Verify values match BMS display

3. **Compile receiver**
   - Verify display renders correctly
   - Test ESP-NOW reception
   - Test local cache

4. **System integration test**
   - Send battery snapshot → receiver displays
   - Verify data accuracy
   - Monitor for data corruption

### Phase 2: Enhancement (After Phase 1 Success)

- Add inverter control (if not in Phase 1)
- Add second BMS support
- Add cell voltage monitoring
- Add graph display
- Add configuration UI

---

## SUMMARY TABLE

| Item | Status | Owner Decision | Impact |
|------|--------|----------------|--------|
| BMS Type | Assumed Pylon | **NEEDED** | Compilation, testing |
| Inverter | Assumed absent | **NEEDED** | Task structure |
| Charger | Assumed absent | Optional | Display content |
| Shunt | Assumed absent | Optional | Data accuracy |
| Safety Limits | Assumed defaults | **NEEDED** | Control logic |
| HAL Approach | Compile-time | Confirmed | Implementation |
| Datalayer Sync | 100ms, status only | **NEEDS CONFIRMATION** | Bandwidth, latency |
| Display Refresh | 500ms | **NEEDS CONFIRMATION** | Power, responsiveness |
| Failsafe Behavior | Transmitter decides | Confirmed | Safety model |
| Code Reuse | Maximum (Battery Emulator) | Confirmed | Development time |

---

## CONCLUSION

The updated architecture is **sound and clear**, but **cannot proceed to implementation without clarifications** on:

1. **Hardware Configuration** (BMS type, inverter, charger, shunt)
2. **Safety Thresholds** (voltage, current, temperature limits)
3. **Integration Scope** (Phase 1 vs Phase 2 features)
4. **Display Content** (what metrics to show)
5. **Performance Targets** (sync rates, refresh rates)

**Next Action:** User provides answers to questions in Sections 1-5. Then proceed to Phase 0 setup (system_settings.h) and Phase 1 build.

