# Architecture Diagrams & Visuals

Visual reference for the Ethernet state machine redesign.

---

## 1. Current Architecture (Problem)

```
setup()
  │
  ├─ WiFi init (100ms)
  │   └─ WiFi.disconnect()
  │
  ├─ ETH.begin() ← Returns immediately, async
  │   │ Event handler registers, but NOT called yet
  │   └─ (async hardware init)
  │
  ├─ Check is_connected() ← TOO EARLY!
  │   │ Returns false because event hasn't fired
  │   └─ MQTT NOT initialized ← PROBLEM
  │
  └─ Main loop
      └─ Eventually event fires: MQTT still disabled
  
TIMELINE:
  t=0s:   ETH.begin() returns, event handler registered
  t=3s:   ARDUINO_EVENT_ETH_GOT_IP fires, connected_=true
  t=6s:   Main checks is_connected() ← FAILS (early)
  t=∞:    MQTT/OTA never start
```

**Result**: MQTT disabled, OTA unavailable

---

## 2. Proposed Architecture (Solution)

```
setup()
  │
  ├─ WiFi init (500ms) ← Longer delay, explicit stop
  │   └─ esp_wifi_stop()
  │
  ├─ EthernetManager::init() ← Async, not waited on
  │   └─ (async hardware init)
  │
  ├─ EXPLICIT WAIT LOOP ← NEW!
  │   │
  │   ├─ while (state != CONNECTED)
  │   │   └─ update_state_machine()
  │   │   └─ delay(100)
  │   │
  │   └─ Timeout: 30 seconds max
  │
  ├─ Check is_fully_ready() ← GUARANTEED true here!
  │   │
  │   ├─ MQTT initialized ← WORKS!
  │   └─ OTA initialized ← WORKS!
  │
  └─ Main loop
      └─ update_state_machine() ← Monitors for recovery

TIMELINE:
  t=0s:   ETH.begin() returns
  t=1-4s: State machine waits for events
  t=5s:   ARDUINO_EVENT_ETH_GOT_IP fires
  t=5s:   State → CONNECTED, MQTT/OTA start ← FIXED!
  t=∞:    If link drops, auto-recovery attempted
```

**Result**: MQTT enabled, OTA available, recovery automatic

---

## 3. State Machine Diagram (9 States)

```
┌─────────────────────────────────────────────────────────────────┐
│                    ETHERNET STATE MACHINE                       │
└─────────────────────────────────────────────────────────────────┘

                         INITIALIZATION PHASE
                         
                    [UNINITIALIZED] (power-on)
                            │
                     (init() called)
                            ↓
                    [PHY_RESET]  ← Physical layer reset, ETH.begin()
                            │
                  (ETH.begin succeeds)
                            ↓
                [CONFIG_APPLYING] ← DHCP/Static config
                            │
          (ARDUINO_EVENT_ETH_START fired)
                            ↓
                [LINK_ACQUIRING] ← Waiting for link UP
                            │
        (ARDUINO_EVENT_ETH_CONNECTED fired)
                            ↓
                [IP_ACQUIRING] ← Waiting for IP assignment
                            │
           (ARDUINO_EVENT_ETH_GOT_IP fired)
                            ↓
                    [CONNECTED] ← ★ FULLY READY ★
                     │        ↑
                     │        │
                     │   (link stable)
                     │   (auto-recovery)
                     │        │
                     └────────┘
                     
                  CONNECTION LOSS / RECOVERY
                  
                    [LINK_LOST]
                   (link went down)
                            │
                    (2 seconds elapsed)
                            ↓
                   [RECOVERING]
                 (attempting restart)
                            │
                 ┌───────────┴───────────┐
                 │                       │
            (success)              (timeout 60s)
                 ↓                       ↓
            [CONNECTED]          [ERROR_STATE]
                                    │
                                   ??? 
                        (Manual intervention needed)
```

---

## 4. State Machine Table

```
┌──────────────────┬──────────────────────────────┬────────────────────┐
│ State            │ What's Happening             │ Next State         │
├──────────────────┼──────────────────────────────┼────────────────────┤
│ UNINITIALIZED    │ Power-on, before init()      │ → PHY_RESET        │
│ PHY_RESET        │ Physical layer reset, init() │ → CONFIG_APPLYING  │
│ CONFIG_APPLYING  │ Setting up DHCP/Static IP    │ → LINK_ACQUIRING   │
│ LINK_ACQUIRING   │ Waiting for link UP event    │ → IP_ACQUIRING     │
│ IP_ACQUIRING     │ Waiting for IP assignment    │ → CONNECTED        │
│ CONNECTED ★      │ Fully ready (link+IP+GW)     │ ↔ LINK_LOST        │
│ LINK_LOST        │ Link went down, recovering   │ → RECOVERING       │
│ RECOVERING       │ Retry sequence in progress   │ → CONNECTED/ERROR  │
│ ERROR_STATE      │ Hardware failure or timeout  │ (stuck)            │
└──────────────────┴──────────────────────────────┴────────────────────┘

★ = CONNECTED state: Only state where MQTT/OTA services should be initialized
```

---

## 5. Timing Comparison: Before vs After

### BEFORE (Current - Problem)
```
0 ┤
  │                    ETH.begin()
  │                       ↓
  │                    (async)
  │ ┌──────────────────────────────┐
  │ │ Hardware initialization      │ 
  │ │ (DHCP negotiation happens)   │
1 ├─┤                              ├─
  │ │                              │
  │ │                              │
2 ├─┤                              ├─
  │ │                              │
  │ │                              ├─ ARDUINO_EVENT_ETH_GOT_IP
3 ├─┤                              ├─ fires here (t=3s)
  │ │                              │ connected_=true
  │ │                              │
4 ├─┤                              ├─
  │ │                              │
  │ └──────────────────────────────┘
5 ├─ is_connected() check ← TOO EARLY!
  │   Returns FALSE (event not processed yet)
  │   MQTT NOT initialized
  │
6 ├─ Later: Event processed, but MQTT already skipped
  │
  │ RESULT: MQTT/OTA disabled forever ✗
```

### AFTER (Proposed - Solution)
```
0 ┤
  │ ETH.begin()  Explicit wait loop starts
  │    ↓          ↓
  │    ├─────────┤
1 ├─  │         │ update_state_machine()
  │   │ (async) │ every 100ms
  │   │ PHY     │
2 ├─  │ Reset   ┌──────────┐
  │   │ │Physical layer  │
  │   │ │init + DHCP     │
3 ├─  │ │                │ ← ARDUINO_EVENT_ETH_GOT_IP
  │   │ │          │ 
  │   │ └──────────┘
4 ├─  │         │ State → CONNECTED
  │   └─────────┤
  │             │ is_fully_ready() check ← GUARANTEED TRUE
5 ├─────────────┤ MQTT initialized ✓
  │             │ OTA initialized ✓
  │             │
  │ RESULT: MQTT/OTA operational in 5s ✓✓
```

---

## 6. Event Flow Diagram

### Current (Broken)
```
main()
  │
  ├─ ETH.begin()
  │   └─ (returns immediately)
  │
  ├─ is_connected()? ← Checked too early!
  │   └─ FALSE (event not fired yet)
  │       └─ MQTT SKIPPED ✗
  │
  └─ loop() continues forever
      │
      └─ (Eventually async event fires)
          │
          └─ connected_=true (but too late!)
              └─ MQTT already disabled ✗
```

### Proposed (Fixed)
```
main()
  │
  ├─ ETH.begin()
  │   └─ (returns immediately)
  │
  ├─ Wait loop:
  │   ├─ Check state every 100ms
  │   ├─ (async events fire: ETH_CONNECTED, ETH_GOT_IP)
  │   └─ when state==CONNECTED, break ← CORRECT!
  │
  ├─ is_fully_ready()? ← Checked at right time
  │   └─ TRUE ✓
  │       └─ MQTT INITIALIZED ✓
  │       └─ OTA INITIALIZED ✓
  │
  └─ loop() starts with everything ready
      │
      └─ update_state_machine() monitors for recovery ✓
```

---

## 7. Comparison Matrix: Current vs Proposed

```
╔═══════════════════════════════════════════════════════════════╗
║              CURRENT         │         PROPOSED              ║
╠═══════════════════════════════════════════════════════════════╣
║ State Visibility:             │ State Visibility:             ║
║   - boolean: connected_       │   - enum: 9 states           ║
║   - No info on status         │   - Clear progression        ║
║                               │                              ║
║ Initialization Check:         │ Initialization Check:        ║
║   - if (is_connected())       │   - while (state != READY)   ║
║   - ✗ Race condition          │   - ✓ Explicit wait         ║
║                               │                              ║
║ MQTT Startup:                 │ MQTT Startup:                ║
║   - ✗ SKIPPED (checked early) │   - ✓ WORKS (at 5s)         ║
║   - ✓ Would work if we waited │   - ✓ Guaranteed ready      ║
║                               │                              ║
║ OTA Availability:             │ OTA Availability:            ║
║   - ✗ DISABLED                │   - ✓ ENABLED (at 5s)       ║
║                               │                              ║
║ Recovery Capability:          │ Recovery Capability:         ║
║   - ✗ Manual reboot needed    │   - ✓ Auto-recovery        ║
║                               │                              ║
║ Timeout Protection:           │ Timeout Protection:          ║
║   - ✗ None (no max wait time) │   - ✓ 30s max timeout      ║
║                               │                              ║
║ Architecture Pattern:         │ Architecture Pattern:        ║
║   - ✗ Different from ESP-NOW  │   - ✓ Matches ESP-NOW      ║
║   - ✗ Inconsistent codebase   │   - ✓ Consistent quality   ║
╚═══════════════════════════════════════════════════════════════╝
```

---

## 8. Timeout Handling Flow

```
                    ETHERNET WAIT LOOP

            is_fully_ready() → return CONNECTED state


                    [Wait Loop Start]
                            │
                    (Check every 100ms)
                            ↓
                   ┌─────────────────┐
                   │  State = ?      │
                   └─────────────────┘
                    │  │  │  │  │  │
           ┌────────┘  │  │  │  │  └────────┐
           ↓           ↓  ↓  ↓  ↓           ↓
        [OTHER]  [OTHER] ... [OTHER]  [CONNECTED]
           │           │  │  │  │           │
         (wait)      (wait)...wait)       (READY!)
           │           │  │  │  │           │
           └─┬─────────┘  │  │  └─────┬─────┘
             │            │  │        │
           (continue)   (continue)  (break)
                        loop()       EXIT
                        
        
        TIMEOUT PROTECTION:

            elapsed = millis() - start_time
            if (elapsed > 30000) {
                timeout_occurred = true
                break  ← Exit loop after 30s max
            }
            
        RESULT: 
            - If CONNECTED: MQTT starts
            - If timeout: Continue without network services
            - If error: Device continues (degraded mode)
```

---

## 9. ESP-NOW vs Ethernet Architecture Alignment

### ESP-NOW (Existing - Good Pattern)
```
17-STATE MACHINE
├─ UNINITIALIZED
├─ INITIALIZING
├─ DISCOVERING
├─ WAITING_FOR_ACK
├─ CHANNEL_LOCKING
├─ CONNECTED ← Target state
├─ DEGRADED
├─ RECONNECTING
└─ ERROR_STATE

Features:
✓ Explicit state progression
✓ State transitions logged
✓ Timeout detection
✓ Recovery capability
✓ Metrics tracking
```

### Ethernet (Current - Poor Pattern)
```
1-STATE DESIGN
├─ false (not connected)
└─ true (connected)

Issues:
✗ No visibility
✗ Race conditions
✗ No timeout protection
✗ Silent failures
```

### Ethernet (Proposed - Aligned Pattern)
```
9-STATE MACHINE
├─ UNINITIALIZED
├─ PHY_RESET
├─ CONFIG_APPLYING
├─ LINK_ACQUIRING
├─ IP_ACQUIRING
├─ CONNECTED ← Target state
├─ LINK_LOST
├─ RECOVERING
└─ ERROR_STATE

Features:
✓ Explicit state progression (matches ESP-NOW)
✓ State transitions logged
✓ Timeout detection
✓ Recovery capability
✓ Metrics tracking
```

**Result**: Consistent architecture across entire codebase ✓

---

## 10. Failure Recovery Loop (If Link Drops)

```
Normal Operation
    │
    ├─ CONNECTED
    │   └─ MQTT publishing
    │   └─ OTA available
    │   └─ Services running
    │
    └─ Link UP, IP assigned
    
    
Link Loss Detected
    │
    ├─ ARDUINO_EVENT_ETH_DISCONNECTED fired
    │
    └─ State → LINK_LOST
        │
        └─ Automatic recovery initiated (in update_state_machine)
            │
            ├─ Wait 2 seconds
            │
            └─ State → RECOVERING
                │
                ├─ (Hardware re-initializes automatically)
                │
                └─ (Events fire: ETH_CONNECTED, ETH_GOT_IP)
                    │
                    └─ State → CONNECTED
                        │
                        └─ MQTT auto-reconnects
                            │
                            └─ Services resume ✓
                            
                            
                        If recovery fails after 60s:
                        │
                        └─ State → ERROR_STATE
                            │
                            └─ Manual intervention needed
```

---

## 11. Code Organization Diagram

```
PROJECT STRUCTURE

esp32projects/
├── espnowtransmitter2/
│   ├── src/
│   │   ├── main.cpp ← MODIFY: Add Ethernet wait loop
│   │   │
│   │   ├── network/
│   │   │   ├── ethernet_manager.h ← MODIFY: Add state tracking
│   │   │   ├── ethernet_manager.cpp ← MODIFY: Update event handler
│   │   │   ├── ethernet_state_machine.h ← CREATE: New file
│   │   │   ├── mqtt_manager.h
│   │   │   └── mqtt_manager.cpp
│   │   │
│   │   ├── espnow/
│   │   │   ├── transmitter_connection_manager.h (17-state machine)
│   │   │   └── ... (reference pattern for our design)
│   │   │
│   │   └── ... (other components)
│   │
│   ├── ETHERNET_SUMMARY.md ← Read first (5 min)
│   ├── ETHERNET_TIMING_ANALYSIS.md ← Full analysis (25 min)
│   ├── ETHERNET_STATE_MACHINE_IMPLEMENTATION.md ← How-to (reference)
│   ├── CODE_CHANGES_REFERENCE.md ← Exact changes (15 min)
│   └── README_INVESTIGATION.md ← This guide
```

---

## Summary Visual

### The Problem (In One Picture)
```
CURRENT BEHAVIOR:

Ethernet Ready at t=3s:     ✓ ✓ ✓
                            IP assigned, link up

Check at t=6s:              ✗ ✗ ✗
                            Too late! Event not processed
                            
Result:                     ✗ MQTT disabled
                            ✗ OTA disabled
                            ✗ Services unavailable
```

### The Solution (In One Picture)
```
PROPOSED BEHAVIOR:

Start at t=0s:              Explicit wait loop
                            
At t=5s:                    ✓ ✓ ✓
                            CONNECTED state reached
                            
Initialize services:        ✓ MQTT online
                            ✓ OTA available
                            ✓ NTP sync working
                            
Result:                     ✓ Services operational
                            ✓ Full functionality
                            ✓ Production ready
```

---

*Visual documentation for Ethernet State Machine redesign. All diagrams represent the proposed architecture changes.*
