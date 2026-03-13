# Implementation Tracking Checklist

**Project**: ESP32 Transmitter/Receiver Enhancement  
**Status**: Ready for Development  
**Date Started**: [TBD]  
**Completion Target**: [Within 2 weeks]

---

## Phase 1: Transmitter Test Mode (3-4 hours)

### Planning
- [ ] Review `REPORT_TEST_LIVE_TOGGLE_MIGRATION.md` Phase 1 section
- [ ] Create feature branch: `feature/test-mode-migration`
- [ ] Set up development environment

### Implementation
- [ ] Create `src/test_mode/test_mode.h` with API definitions
- [ ] Implement `src/test_mode/test_mode.cpp` with data generation
  - [ ] Implement `TestMode::initialize()`
  - [ ] Implement `TestMode::generate_values()`
  - [ ] Simulate realistic SOC drift (0.1%/second)
  - [ ] Simulate temperature changes (±0.5°C/second)
  - [ ] Simulate cell voltage variations (±5mV range)
- [ ] Integrate into `src/main.cpp`
  - [ ] Create FreeRTOS task for test mode
  - [ ] Add data source selection logic
- [ ] Create web UI toggle in `lib/webserver/pages/settings_page.cpp`
- [ ] Create API endpoint in `lib/webserver/api/api_handlers.cpp`
  - [ ] GET `/api/test_mode` - return current state
  - [ ] POST `/api/test_mode` - toggle state

### Testing
- [ ] Transmitter compiles without warnings
- [ ] Test mode toggle button appears in settings
- [ ] Toggle test mode ON → receiver shows test data
- [ ] Toggle test mode OFF → receiver shows live data
- [ ] Test data varies realistically (not static)
- [ ] Data visible on receiver within 2 seconds
- [ ] No crashes when toggling rapidly

### Validation Checklist
- [ ] Phase 1 test scenarios all pass (from report)
- [ ] Memory usage stable
- [ ] No console errors
- [ ] Commit with message: "Phase 1: Add transmitter test mode"

---

## Phase 1.5: MQTT Topic Prefixing (1-2 hours) ⚠️ CRITICAL

### Planning
- [ ] Review `REPORT_TEST_LIVE_TOGGLE_MIGRATION.md` Phase 1.5 section
- [ ] Note all 4 transmitter files to modify (lines provided in report)
- [ ] Note receiver file to modify
- [ ] Backup current configuration

### Implementation - Transmitter Files

**File 1: `src/config/network_config.h`**
- [ ] Change all 7 topic definitions from `BE/*` to `transmitter/BE/*`
  - [ ] `spec_data` → `transmitter/BE/spec_data`
  - [ ] `spec_data_2` → `transmitter/BE/spec_data_2`
  - [ ] `battery_specs` → `transmitter/BE/battery_specs`
  - [ ] `cell_data` → `transmitter/BE/cell_data`
  - [ ] Other topics: [list any others found]
- [ ] Verify no syntax errors
- [ ] Compile check

**File 2: `src/network/mqtt_manager.cpp`**
- [ ] Update publish() calls (4 locations)
  - [ ] Line 111: publish spec_data
  - [ ] Line 140: publish battery_specs
  - [ ] Line 178: publish cell_data
  - [ ] Line 207: publish spec_data_2
- [ ] Verify all use new topic prefix
- [ ] Compile check

**File 3: `src/network/mqtt_task.cpp`**
- [ ] Update LOG_INFO messages (4 locations)
  - [ ] Line 87: Update log message
  - [ ] Line 90: Update log message
  - [ ] Line 93: Update log message
  - [ ] Line 133: Update log message
- [ ] Verify topics in logs show new prefix
- [ ] Compile check

### Implementation - Receiver File

**File 4: `src/mqtt/mqtt_client.cpp`**
- [ ] Update topic subscriptions
  - [ ] Change subscription from `BE/spec_data` → `transmitter/BE/spec_data`
  - [ ] Change subscription from `BE/spec_data_2` → `transmitter/BE/spec_data_2`
  - [ ] Update any topic matching logic
  - [ ] Update any hardcoded topic strings
- [ ] Verify receiver builds successfully
- [ ] Compile check

### Testing
- [ ] Transmitter compiles without warnings
- [ ] Receiver compiles without warnings
- [ ] MQTT broker connection successful
- [ ] Transmitter publishes to `transmitter/BE/*` topics
- [ ] Receiver subscribes to `transmitter/BE/*` topics
- [ ] Data flows from transmitter to receiver
- [ ] No errors in MQTT logs

### Verification with MQTT Tools
```bash
# In terminal, monitor new topics:
mosquitto_sub -t "transmitter/BE/#" -v

# Should see:
# transmitter/BE/spec_data {...}
# transmitter/BE/spec_data_2 {...}
# transmitter/BE/battery_specs {...}
# transmitter/BE/cell_data {...}

# Old topics should NOT appear:
mosquitto_sub -t "BE/#" -v
# (Should see nothing or other devices' data)
```

### Validation Checklist
- [ ] Phase 1.5 test scenarios all pass
- [ ] No MQTT collision warnings
- [ ] Both transmitter and receiver use new topics
- [ ] Old `BE/*` topics no longer published
- [ ] Receiver receives data correctly
- [ ] Commit with message: "Phase 1.5: Migrate MQTT topics to transmitter namespace"

---

## Phase 2: Transmission Method Selection (2-3 hours)

### Planning
- [ ] Review `REPORT_TEST_LIVE_TOGGLE_MIGRATION.md` Phase 2 section
- [ ] Understand decision tree: payload size < 250B → ESP-NOW, else → MQTT
- [ ] Review TransmissionSelector pseudocode in report

### Implementation

**File 1: Create `src/network/transmission_selector.h`**
- [ ] Define `enum TransmissionMethod`
  - [ ] METHOD_ESPNOW
  - [ ] METHOD_MQTT
  - [ ] METHOD_BOTH (redundancy)
- [ ] Define `class TransmissionSelector`
  - [ ] Static method: `select(payload_size, receiver_local)`
  - [ ] Static method: `get_last_method()`
  - [ ] Static method: `set_redundancy_mode(bool enabled)`
- [ ] Add configuration constants
  - [ ] ESPNOW_PAYLOAD_MAX = 250
  - [ ] MQTT_PAYLOAD_MAX = 4096

**File 2: Create `src/network/transmission_selector.cpp`**
- [ ] Implement selection logic
  - [ ] If payload < 250B AND receiver_local → METHOD_ESPNOW
  - [ ] Else → METHOD_MQTT
  - [ ] If redundancy_mode → METHOD_BOTH
- [ ] Add logging: `ESP_LOGI("TX_SELECT", "Payload: %d bytes → %s", ...)`
- [ ] Handle edge cases (network errors, receiver unavailable)

**File 3: Modify `src/config/task_config.h`**
- [ ] Add transmission timing constants
  - [ ] ESPNOW_INTERVAL_MS = 2000
  - [ ] MQTT_INTERVAL_MS = 10000

**File 4: Modify `src/network/mqtt_task.cpp`**
- [ ] At start of mqtt_loop task:
  - [ ] Calculate payload size
  - [ ] Call TransmissionSelector::select()
  - [ ] Log selected method
- [ ] Implement dual-path logic
  - [ ] If METHOD_ESPNOW or METHOD_BOTH → transmit via ESP-NOW (2s interval)
  - [ ] If METHOD_MQTT or METHOD_BOTH → publish via MQTT (10s interval)
- [ ] Add timing variables:
  - [ ] `static unsigned long last_espnow_publish = 0`
  - [ ] `static unsigned long last_mqtt_publish = 0`

### Testing
- [ ] Transmitter compiles without warnings
- [ ] Small payload data uses ESP-NOW (check logs)
- [ ] Large payload data uses MQTT (check logs)
- [ ] Cell data (96 cells ~400B) uses MQTT
- [ ] Both transmission methods work
- [ ] Logs show correct method selection
- [ ] Receiver displays data from both methods correctly

### Logging Verification
```
Look for log messages:
[TX_SELECT] Payload size: 120 bytes, Using: ESP-NOW
[TX_SELECT] Payload size: 400 bytes, Using: MQTT
[MQTT] Publishing to transmitter/BE/spec_data (400 bytes)
```

### Validation Checklist
- [ ] Phase 2 test scenarios all pass
- [ ] Small/large payload routing works correctly
- [ ] No transmission failures
- [ ] Logs show method selection
- [ ] Receiver receives data on both paths
- [ ] Commit with message: "Phase 2: Add transmission method selector"

---

## Phase 2.5: Receiver Simplification (1-2 hours)

### Planning
- [ ] Review `REPORT_TEST_LIVE_TOGGLE_MIGRATION.md` Phase 2.5 section
- [ ] Verify Phase 1 complete on transmitter first

### Implementation

**File 1: Modify `src/globals.cpp`**
- [ ] Locate TestMode namespace
- [ ] Delete entire namespace:
  ```cpp
  // DELETE:
  // namespace TestMode {
  //     bool enabled = false;
  //     volatile int soc = 50;
  //     ...
  // }
  ```
- [ ] Delete backward compatibility aliases:
  ```cpp
  // DELETE:
  // bool& test_mode_enabled = TestMode::enabled;
  // volatile int& g_test_soc = TestMode::soc;
  // ...
  ```
- [ ] Compile check

**File 2: Modify `lib/webserver/api/api_handlers.cpp`**
- [ ] Remove test mode conditionals from API handlers
  - [ ] Find: `const char* mode = test_mode_enabled ? "simulated" : "live"`
  - [ ] Replace with: `const char* mode = "live"`
  - [ ] Remove all test_mode_enabled checks
- [ ] Simplify data selection (always use g_received_*)
- [ ] Update any documentation/comments
- [ ] Compile check

**File 3: Modify receiver settings UI (if exists)**
- [ ] Remove test mode toggle button from settings page
- [ ] Remove test mode section from receiver web UI
- [ ] Clean up any test data UI elements

### Testing
- [ ] Receiver compiles without warnings
- [ ] Settings page loads without errors
- [ ] Receiver displays transmitted data correctly
- [ ] No "test mode" option in UI
- [ ] No console errors

### Validation Checklist
- [ ] Phase 2.5 test scenarios all pass
- [ ] Receiver is "pure display device" now
- [ ] No test mode logic remaining
- [ ] Receiver displays data regardless of source (transmitter controls test vs live)
- [ ] Commit with message: "Phase 2.5: Remove test mode from receiver"

---

## Phase 3: Testing & Validation (1-2 hours)

### Integration Testing
- [ ] Power cycle both devices
- [ ] Verify transmitter connects to WiFi
- [ ] Verify transmitter connects to MQTT broker
- [ ] Verify receiver connects to transmitter (ESP-NOW)
- [ ] Verify receiver connects to MQTT broker

### Functional Testing
- [ ] Test live mode: Transmitter sends live data → Receiver displays correctly
- [ ] Test test mode: Transmitter sends test data → Receiver displays correctly
- [ ] Test toggle: Switch test mode ON/OFF multiple times → No crashes
- [ ] Test MQTT topics: Verify all use `transmitter/BE/*` prefix
- [ ] Test transmission methods: Small data via ESP-NOW, large data via MQTT

### Extended Runtime Test (1+ hour)
- [ ] Run both devices for 1+ hour without restart
- [ ] Monitor for crashes or hangs
- [ ] Check for memory leaks (memory should be stable)
- [ ] Check for data corruption (values should be consistent)
- [ ] Observe CPU usage (should be <10%)

### Performance Baseline
- [ ] Measure ESP-NOW transmission latency: 2s ± 100ms
- [ ] Measure MQTT transmission latency: 10s ± 1s
- [ ] Verify bar graph renders <50ms
- [ ] Verify hover response <100ms
- [ ] Monitor memory usage (should be stable)

### Test Matrix (From Report)
- [ ] Normal operation: Live mode ON → Receiver shows live data ✓
- [ ] Test mode enabled → Receiver shows test data ✓
- [ ] Toggle OFF → Switches to live data ✓
- [ ] Toggle ON → Switches to test data ✓
- [ ] BMS disconnect with test mode OFF → Error shown ✓
- [ ] BMS disconnect with test mode ON → Test data continues ✓
- [ ] MQTT topics with prefix → No collisions ✓
- [ ] Cell data (large) → Uses MQTT ✓
- [ ] Basic data (small) → Uses ESP-NOW ✓

### Validation Checklist
- [ ] All test scenarios pass
- [ ] No crashes or hangs
- [ ] No memory leaks
- [ ] Extended runtime stable (1+ hour)
- [ ] Performance baselines met
- [ ] Commit with message: "Phase 3: Testing and validation complete"

---

## Phase 4 (Optional): Cell Monitor UI Enhancement (4-6 hours)

### Planning
- [ ] Review `REPORT_CELL_MONITOR_ENHANCEMENTS.md` all phases
- [ ] Compare current vs. Battery Emulator reference implementation
- [ ] Check current hover scale (should be 1.05x)

### Implementation

**File: `lib/webserver/pages/cellmonitor_page.cpp` (Receiver)**

**Part 1: Add HTML Structure**
- [ ] Add bar graph container div:
  ```html
  <div id='barGraphContainer' style='...'>
  ```
- [ ] Add value display div:
  ```html
  <div id='valueDisplay'>
  ```
- [ ] Add legend div with color boxes
- [ ] Verify HTML nesting correct

**Part 2: Add CSS Styling**
- [ ] Style #barGraphContainer (flexbox, 220px height, border)
- [ ] Style .voltage-bar (flex, background, border, hover effects)
- [ ] Style .balancing (cyan color)
- [ ] Style .min-max (red border)
- [ ] Style #valueDisplay (centered, gold color)
- [ ] Verify CSS doesn't break existing layout

**Part 3: Add JavaScript Functions**
- [ ] Implement `mapValue()` function (maps value to pixel range)
- [ ] Implement `renderBarGraph()` function:
  - [ ] Find min/max voltages
  - [ ] Create bar for each cell
  - [ ] Set bar height based on voltage
  - [ ] Apply colors (balancing, min/max)
  - [ ] Add hover events
- [ ] Update cell hover to show bar:
  - [ ] Change scale from 1.05x to 1.15x
  - [ ] Highlight corresponding bar
  - [ ] Update valueDisplay
- [ ] Add bar hover to show cell:
  - [ ] Highlight corresponding cell
  - [ ] Update valueDisplay

**Part 4: Integration**
- [ ] Call renderBarGraph() after cell data updates
- [ ] Ensure bar graph renders before/with cell grid
- [ ] Update on both initial load and data refresh

### Testing
- [ ] Receiver compiles without warnings
- [ ] Cell monitor page loads
- [ ] Bar graph appears below cell grid
- [ ] Bars have correct heights (not all same)
- [ ] Min/max bars have red borders
- [ ] Balancing cells show cyan
- [ ] Cell hover enlarges to 1.15x (test with transform measurement)
- [ ] Hover on cell highlights bar
- [ ] Hover on bar highlights cell
- [ ] Value display updates correctly
- [ ] Works with 4, 16, 96 cells
- [ ] Responsive on mobile/tablet/desktop
- [ ] No console errors

### Validation Checklist
- [ ] All Phase 4 test scenarios pass (from report)
- [ ] Bar graph performance acceptable (<50ms render)
- [ ] No impact on other receiver functions
- [ ] Commit with message: "Phase 4: Add cell monitor bar graph and enhanced hover"

---

## Final Verification

### Code Quality
- [ ] All code compiles without warnings
- [ ] No console errors on any page
- [ ] No memory leaks (verified with 1+ hour runtime test)
- [ ] All files properly formatted
- [ ] Comments added where needed

### Documentation
- [ ] All phases documented in reports
- [ ] README updated with new features
- [ ] API documentation updated (if applicable)
- [ ] Code comments explain key logic

### Git Hygiene
- [ ] Feature branch clean and rebased on main
- [ ] Commits have clear, descriptive messages
- [ ] All changes tracked in git
- [ ] Ready for pull request review

### Release Readiness
- [ ] All test scenarios pass
- [ ] All validation checklists complete
- [ ] Performance baselines met
- [ ] Backward compatibility maintained
- [ ] Rollback procedure documented

---

## Sign-Off

**Developer**: _________________ **Date**: _________

**Code Review**: _________________ **Date**: _________

**QA Verification**: _________________ **Date**: _________

**Project Lead**: _________________ **Date**: _________

---

## Notes & Issues

Use this section to track any issues encountered during implementation:

### Issues Found
1. [Issue]: [Description] [Resolution/Status]
2. [Issue]: [Description] [Resolution/Status]

### Blockers
- [None identified]

### Deviations from Plan
- [None identified]

---

**Status**: ✅ Ready to Begin Implementation
