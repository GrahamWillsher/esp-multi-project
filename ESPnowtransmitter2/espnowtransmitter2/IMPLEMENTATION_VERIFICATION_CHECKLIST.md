# Implementation Verification Checklist

**Version**: 1.0  
**Date**: February 19, 2026  
**Target**: Verify all state machine changes are correctly implemented

---

## Pre-Compilation Verification

### Files Modified (5 total)

- [ ] **ethernet_manager.h**
  - [ ] Contains 9-state enum (UNINITIALIZED through ERROR_STATE)
  - [ ] Has `update_state_machine()` method
  - [ ] Has `is_fully_ready()` method
  - [ ] Has event callback registration methods
  - [ ] No old boolean `eth_connected` variable

- [ ] **ethernet_manager.cpp**
  - [ ] Has event handler with LINK_ACQUIRING on cable detect
  - [ ] Has LINK_LOST on cable disconnect
  - [ ] Has timeout checking per state
  - [ ] Has state transition methods
  - [ ] Has network configuration methods
  - [ ] No old simple event handler

- [ ] **heartbeat_manager.cpp**
  - [ ] Checks `EthernetManager::instance().is_fully_ready()` before sending
  - [ ] Maintains existing ESP-NOW connection check
  - [ ] Both checks present (dual gating)

- [ ] **main.cpp setup()**
  - [ ] Calls `EthernetManager::instance().init()`
  - [ ] Registers `on_connected` callback
  - [ ] Registers `on_disconnected` callback
  - [ ] Callback logging for visibility

- [ ] **main.cpp loop()**
  - [ ] Calls `EthernetManager::instance().update_state_machine()`
  - [ ] Call placed after WiFi.handle() / network code
  - [ ] No blocking calls in state machine update

---

## File-by-File Verification

### ethernet_manager.h

**Required Elements**:

```cpp
// Enum present
enum class EthernetState {
    UNINITIALIZED,
    PHY_RESET,
    CONFIG_APPLYING,
    LINK_ACQUIRING,
    IP_ACQUIRING,
    CONNECTED,
    LINK_LOST,
    RECOVERING,
    ERROR_STATE
};
```

**Verification Checklist**:
- [ ] Exactly 9 states in enum
- [ ] States in correct order (UNINITIALIZED first)
- [ ] No missing states

**Required Methods**:

```cpp
// Public methods
static EthernetManager& instance();
void init();
void update_state_machine();
EthernetState get_state() const;
const char* get_state_string() const;
bool is_fully_ready() const;  // Returns true ONLY when CONNECTED
void on_connected(std::function<void()> callback);
void on_disconnected(std::function<void()> callback);
```

**Verification Checklist**:
- [ ] `instance()` method exists (singleton pattern)
- [ ] `init()` method exists
- [ ] `update_state_machine()` exists and is called periodically
- [ ] `get_state()` returns current state enum
- [ ] `is_fully_ready()` returns true ONLY in CONNECTED state
- [ ] Callback registration methods present
- [ ] No blocking I/O in header file (all in cpp)

---

### ethernet_manager.cpp

**Event Handler**:

```cpp
// Must handle cable detection
// ARDUINO_EVENT_ETH_CONNECTED → set_state(LINK_ACQUIRING)
// ARDUINO_EVENT_ETH_DISCONNECTED → set_state(LINK_LOST)
// ARDUINO_EVENT_ETH_GOT_IP → set_state(CONNECTED)
```

**Verification Checklist**:
- [ ] Static event handler registered via `ETH.onEvent()`
- [ ] ARDUINO_EVENT_ETH_CONNECTED triggers LINK_ACQUIRING
- [ ] ARDUINO_EVENT_ETH_DISCONNECTED triggers LINK_LOST
- [ ] ARDUINO_EVENT_ETH_GOT_IP triggers CONNECTED
- [ ] Event handler calls appropriate state transitions

**State Transitions**:

```cpp
// In update_state_machine():
// PHY_RESET → CONFIG_APPLYING (after 100ms timeout)
// CONFIG_APPLYING → LINK_ACQUIRING (event or timeout)
// LINK_ACQUIRING → IP_ACQUIRING (event) or ERROR_STATE (after 30s timeout)
// IP_ACQUIRING → CONNECTED (event) or ERROR_STATE (after 60s timeout)
// CONNECTED → LINK_LOST (event)
// LINK_LOST → RECOVERING (after 1s)
// RECOVERING → LINK_ACQUIRING (event) or ERROR_STATE (after 30s)
// ERROR_STATE → stays ERROR_STATE (power cycle needed)
```

**Verification Checklist**:
- [ ] All transitions above implemented
- [ ] Timeout values correct per state
- [ ] State transitions logged to console
- [ ] No stuck states (except ERROR_STATE intentionally)

**Callback System**:

```cpp
// When state transitions to CONNECTED:
for (auto& callback : connected_callbacks_) {
    callback();
}

// When state transitions to LINK_LOST:
for (auto& callback : disconnected_callbacks_) {
    callback();
}
```

**Verification Checklist**:
- [ ] Callback vector exists
- [ ] Callbacks triggered on CONNECTED transition
- [ ] Callbacks triggered on LINK_LOST transition
- [ ] No callbacks triggered on intermediate states

---

### heartbeat_manager.cpp

**Dual-Gating Implementation**:

```cpp
void HeartbeatManager::tick() {
    if (!m_initialized) return;
    
    // Gate 1: Ethernet
    if (!EthernetManager::instance().is_fully_ready()) {
        return;  // No cable or IP not assigned
    }
    
    // Gate 2: ESP-NOW
    if (EspNowConnectionManager::instance().get_state() != EspNowConnectionState::CONNECTED) {
        return;  // No receiver connection
    }
    
    // Both ready - send heartbeat
    uint32_t now = millis();
    if (now - m_last_send_time >= HEARTBEAT_INTERVAL_MS) {
        send_heartbeat();
        m_last_send_time = now;
    }
}
```

**Verification Checklist**:
- [ ] Both `is_fully_ready()` call present
- [ ] ESP-NOW state check still present
- [ ] Both checks before `send_heartbeat()`
- [ ] Order: Ethernet check first, then ESP-NOW check
- [ ] No heartbeat sent if either gate fails

---

### main.cpp setup()

**Ethernet Callback Registration** (around line 110):

```cpp
// Register callback for when Ethernet becomes ready
EthernetManager::instance().on_connected([] {
    LOG_INFO("MAIN", "✓ Ethernet fully ready - starting network services");
    // Services will auto-start via is_fully_ready() checks
});

// Register callback for when Ethernet disconnects
EthernetManager::instance().on_disconnected([] {
    LOG_WARN("MAIN", "✗ Ethernet disconnected - stopping network services");
});

// Initialize Ethernet state machine
EthernetManager::instance().init();
```

**Verification Checklist**:
- [ ] `on_connected()` callback registered
- [ ] `on_disconnected()` callback registered
- [ ] `init()` called
- [ ] Callbacks include logging for visibility
- [ ] Placement before main loop starts

---

### main.cpp loop()

**State Machine Update** (around line 350):

```cpp
// In main loop
void loop() {
    // ...existing code...
    
    // Update Ethernet state machine (checks timeouts, handles transitions)
    EthernetManager::instance().update_state_machine();
    
    // ...rest of loop...
}
```

**Verification Checklist**:
- [ ] `update_state_machine()` called in loop
- [ ] Called every iteration (not skipped)
- [ ] Placed early in loop (before long operations)
- [ ] No blocking operations in method
- [ ] No global state changes outside of method

---

## Compilation Verification

### Build Without Errors

```bash
# Run PlatformIO build
$ platformio run --target build

Expected Output:
  ...
  Archiving /esp32projects/espnowtransmitter2/.pio/build/esp32-poe/
  Linking /esp32projects/espnowtransmitter2/.pio/build/esp32-poe/firmware.elf
  ✓ Build successful
```

**Verification Checklist**:
- [ ] Build completes with 0 errors
- [ ] Build completes with 0 unresolved references
- [ ] No warnings about ethernet_manager
- [ ] No warnings about heartbeat_manager
- [ ] No warnings about main.cpp

### No Linker Errors

```bash
$ platformio run --target upload

Expected Output:
  Linking ...
  ✓ Uploading to device
```

**Verification Checklist**:
- [ ] No undefined reference errors
- [ ] No symbol redefinition errors
- [ ] All headers found (no file not found errors)

### Header Dependencies

**Verification Checklist**:
- [ ] ethernet_manager.h includes all needed headers
  - [ ] WiFi.h (for ETH object)
  - [ ] ETH.h (for Ethernet APIs)
  - [ ] Preferences (for NVS storage)
  - [ ] functional (for std::function)
- [ ] heartbeat_manager.cpp includes ethernet_manager.h
- [ ] main.cpp includes ethernet_manager.h

---

## Runtime Verification

### Boot Sequence Test

**Setup Phase** (First 5 seconds):

```
T=0.0s  Serial output starts
T=0.1s  "Initializing Ethernet..."
T=0.2s  "ETH: State transition UNINITIALIZED → PHY_RESET"
T=0.3s  "ETH: Waiting for cable..."
T=X.Xs  "ETH: Cable detected! Transitioning to LINK_ACQUIRING"
T=Y.Ys  "ETH: IP assigned! Transitioning to CONNECTED"
T=Y.Ys  "✓ Ethernet fully ready - starting network services"
T=Z.Zs  Services (NTP/MQTT/OTA) begin startup
```

**Verification Checklist**:
- [ ] Initial state is UNINITIALIZED
- [ ] Transitions logged to serial
- [ ] Cable detection logged
- [ ] IP assignment logged
- [ ] Callbacks logged
- [ ] All messages appear in correct order

### Cable Detection Test

**With Cable** (from boot):

```
T=0s    Device powers on with cable plugged
T=0-3s  Transitions through states
T=3s    CONNECTED
T=3s    "✓ Ethernet fully ready"
T=3s    Services start
```

**Verification Checklist**:
- [ ] Transitions complete within 5 seconds
- [ ] CONNECTED reached
- [ ] Services start
- [ ] Network operational (can ping)

**Without Cable** (from boot):

```
T=0s    Device powers on, no cable
T=0-5s  LINK_ACQUIRING timeout reached
T=5s    "ETH: Link acquiring timeout, transitioning to ERROR_STATE"
T=5s    "✗ Ethernet disconnected" (callback)
T=5s    Services remain stopped
```

**Verification Checklist**:
- [ ] Timeout after 30 seconds in LINK_ACQUIRING
- [ ] ERROR_STATE reached
- [ ] Callback triggered (or logged)
- [ ] Services do NOT start

**Cable Inserted After Boot**:

```
T=0s    Device booted with no cable
T=5s    ERROR_STATE reached
T=10s   [Plug cable in]
T=10s   "ARDUINO_EVENT_ETH_CONNECTED event"
T=10s   "ETH: State transition ERROR_STATE → LINK_ACQUIRING"
T=10s   "ETH: Cable detected! Transitioning to LINK_ACQUIRING"
T=11s   IP assigned
T=11s   "✓ Ethernet fully ready"
T=11s   Services start
```

**Verification Checklist**:
- [ ] ERROR_STATE can transition out (on cable detect)
- [ ] Services restart after cable insertion
- [ ] Network operational

### Heartbeat Verification

**Both Networks Ready**:

```
T=0s    Ethernet CONNECTED
T=0s    ESP-NOW CONNECTED
T=1s    "Heartbeat sent" (appears in log)
T=6s    "Heartbeat sent" (5-second interval)
T=11s   "Heartbeat sent"
```

**Verification Checklist**:
- [ ] Heartbeat sends periodically when both ready
- [ ] Interval correct (5 seconds)
- [ ] Logged to console

**Ethernet Disconnected**:

```
T=0s    Heartbeat sending every 5s
T=10s   [Unplug cable]
T=10s   "✗ Ethernet disconnected"
T=10s   Heartbeat STOPS sending
T=15s   (No heartbeat log entry)
T=20s   (No heartbeat log entry)
```

**Verification Checklist**:
- [ ] Heartbeat stops immediately on Ethernet loss
- [ ] Verifies dual-gating works
- [ ] No errors in log when trying to send

**ESP-NOW Disconnected**:

```
T=0s    Heartbeat sending every 5s
T=10s   [ESP-NOW connection lost]
T=10s   Heartbeat STOPS sending
T=15s   (No heartbeat log entry)
```

**Verification Checklist**:
- [ ] Heartbeat stops on ESP-NOW loss
- [ ] Ethernet state doesn't affect this
- [ ] Both gates working independently

### Stress Test

**Rapid Cable Connects/Disconnects** (if P1.1 debouncing NOT implemented):

```
[Rapidly plug/unplug cable 5 times in 10 seconds]

Expected (v1.0 - no debouncing):
T=0s    CONNECTED
T=1s    LINK_LOST → RECOVERING
T=2s    CONNECTED (re-plugged)
T=2s    services restart
T=3s    LINK_LOST → RECOVERING
T=4s    CONNECTED
T=4s    services restart
T=5s    LINK_LOST → RECOVERING
...
Services restart every 1-2 seconds (thrashing)
```

**Verification Checklist**:
- [ ] Device doesn't crash
- [ ] Services restart each time
- [ ] Log shows each transition
- [ ] No system freeze/deadlock
- [ ] NOTE: This is expected behavior in v1.0 (improved in v2.0 with P1.1)

---

## Integration Verification

### Service Startup Verification

**NTP Service**:

```
T=3s    "✓ Ethernet fully ready"
T=3s    [NTP callback triggers]
T=3.1s  "NTP: Syncing time..."
T=10s   "NTP: ✓ Time synced: <current time>"
```

**Verification Checklist**:
- [ ] NTP starts on Ethernet CONNECTED
- [ ] NTP completes within 10 seconds
- [ ] Time is correct (after 2020-01-01)

**MQTT Service** (if enabled):

```
T=3s    "✓ Ethernet fully ready"
T=3.1s  "MQTT: Connecting to broker..."
T=5s    "MQTT: ✓ Connected"
```

**Verification Checklist**:
- [ ] MQTT starts on Ethernet CONNECTED
- [ ] Connects successfully
- [ ] Can publish/receive messages

**OTA Service** (if enabled):

```
T=3s    "✓ Ethernet fully ready"
T=3.1s  "OTA: Server started on port 8080"
T=3.1s  "OTA: Ready to receive updates"
```

**Verification Checklist**:
- [ ] OTA server starts on Ethernet CONNECTED
- [ ] Accessible from network (can ping)
- [ ] Can upload firmware

### Logging Verification

**Required Log Entries**:

```
"Initializing Ethernet..."
"ETH: State transition UNINITIALIZED → PHY_RESET"
"ETH: Waiting for cable..."
"ETH: Cable detected! Transitioning to LINK_ACQUIRING"
"ETH: IP assigned! Transitioning to CONNECTED"
"✓ Ethernet fully ready - starting network services"
```

**Verification Checklist**:
- [ ] All entries appear in serial monitor
- [ ] Entries in correct order
- [ ] Timestamps make sense (no negative deltas)
- [ ] No garbled text (encoding issues)

---

## Performance Verification

### Boot Time

**Measurement**:
```
T=0s    Power on
T=Xs    Ethernet CONNECTED
Boot time = X seconds
```

**Expected**:
- With cable: 2-5 seconds
- Without cable: ~30 seconds (timeout)

**Verification Checklist**:
- [ ] With cable: < 5 seconds ✓
- [ ] Without cable: ~30 seconds ✓
- [ ] Consistent across reboots ✓

### Memory Usage

**Check Heap Memory**:

```cpp
// Add to loop() occasionally
uint32_t free_heap = esp_get_free_heap_size();
LOG_DEBUG("MEM", "Free heap: %u bytes", free_heap);
```

**Expected**:
- Free heap should remain stable
- No gradual decrease (no leak)

**Verification Checklist**:
- [ ] Free heap > 50KB after boot
- [ ] Free heap remains constant after 1 hour
- [ ] No memory leak detected

### CPU Usage

**Monitoring**:
```
- State machine update() should take < 1ms
- No blocking operations in loop
- Main loop yields regularly
```

**Verification Checklist**:
- [ ] Main loop executes frequently (> 100 Hz)
- [ ] No watchdog resets
- [ ] No "tick" delays logged

---

## Documentation Verification

### Master Document

- [ ] PROJECT_ARCHITECTURE_MASTER.md exists
- [ ] Contains system overview
- [ ] Explains state machine architecture
- [ ] Links to technical references
- [ ] Ready for handoff to ops team

### Technical Reference

- [ ] ETHERNET_STATE_MACHINE_TECHNICAL_REFERENCE.md exists
- [ ] Contains all 9 states with timeouts
- [ ] Explains all transitions
- [ ] Includes edge cases
- [ ] Includes testing procedures

### Service Integration Guide

- [ ] SERVICE_INTEGRATION_GUIDE.md exists
- [ ] Shows how to integrate services
- [ ] Includes examples (NTP, MQTT, OTA)
- [ ] Explains dual-gating pattern
- [ ] Includes troubleshooting

### Post-Release Improvements

- [ ] POST_RELEASE_IMPROVEMENTS.md exists
- [ ] Lists prioritized improvements
- [ ] Shows timeline
- [ ] Ready for team planning

---

## Final Checklist

### Code Quality

- [ ] No compiler warnings
- [ ] No unused variables
- [ ] No unused includes
- [ ] No hardcoded values (except timeouts)
- [ ] All functions have error handling
- [ ] No blocking I/O in callbacks

### Architecture

- [ ] State machine is the source of truth
- [ ] Services gate on state
- [ ] Callbacks don't block
- [ ] FreeRTOS tasks remain isolated
- [ ] No main loop blocking

### Documentation

- [ ] Master document complete
- [ ] Technical references complete
- [ ] Service integration guide complete
- [ ] Post-release improvements documented
- [ ] All files have headers and version info

### Testing

- [ ] Boot with cable: ✓
- [ ] Boot without cable: ✓
- [ ] Cable inserted after boot: ✓
- [ ] Services start/stop correctly: ✓
- [ ] Heartbeat dual-gating works: ✓
- [ ] No crashes after 24 hours: ✓

---

## Sign-Off

| Item | Status | Verified By | Date |
|------|--------|-------------|------|
| Code compiles | [ ] | — | — |
| Tests pass | [ ] | — | — |
| Documentation complete | [ ] | — | — |
| Ready for production | [ ] | — | — |

---

**Next Steps After Verification**:
1. ✓ Code review by team
2. ✓ Hardware integration testing
3. ✓ 24-hour stability testing
4. ✓ Customer deployment

---

**Document Status**: COMPLETE - Ready for verification team  
**Last Updated**: February 19, 2026  
**Prepared By**: Automated Analysis Agent

