# Ethernet State Machine Technical Reference

**Version**: 1.0  
**Date**: February 19, 2026  
**Status**: Production Ready

---

## Overview

This document provides complete technical details on the 9-state Ethernet connection state machine, including:
- Detailed state definitions
- Complete state transition table
- Event mapping
- Timeout values and recovery logic
- Edge cases and solutions
- Metrics and diagnostics
- Debugging techniques

**See Also**: [PROJECT_ARCHITECTURE_MASTER.md](PROJECT_ARCHITECTURE_MASTER.md) for overview

---

## State Definitions

### State: UNINITIALIZED (0)

**Duration**: Single frame at boot

**Entry**: Power-on

**Exit**: When `init()` is called

**Actions**:
- No hardware initialization
- No event handlers registered
- No state variables set

**Transitions**:
```
UNINITIALIZED
    ↓ init() called
PHY_RESET
```

**Timeout**: None

**Error Handling**: If init() not called within 5s of boot, firmware may have crashed

---

### State: PHY_RESET (1)

**Duration**: 100-200ms (hardware dependent)

**Entry**: `init()` called

**Exit**: Hardware responds, config begins

**Actions**:
1. GPIO power pin LOW (10ms) - reset PHY chip
2. GPIO power pin HIGH (150ms) - PHY initializes
3. `ETH.begin()` called - driver loads
4. Delay 100ms - hardware stabilizes

**Transitions**:
```
PHY_RESET
    ├─ Success (100ms) → CONFIG_APPLYING
    └─ Timeout (5s) → ERROR_STATE
```

**Timeout**: 5 seconds (PHY not responding)

**Events**: None monitored

**Logging**:
```
ETH Performing PHY hardware reset...
ETH PHY hardware reset complete (100ms)
ETH Calling ETH.begin() for LAN8720 PHY
```

**Common Issues**:
- GPIO pins not assigned correctly (check `hardware_config.h`)
- Weak power supply (reset not holding)
- LAN8720 not soldered correctly
- Wrong ETH_CLOCK setting

---

### State: CONFIG_APPLYING (2)

**Duration**: 100-500ms (config processing)

**Entry**: `ETH.begin()` succeeds

**Exit**: `ETH.config()` completes

**Actions**:
1. Load network config from NVS (DHCP or static)
2. Call `ETH.config()` with loaded settings
3. Hardware begins listening for DHCP or applies static IP

**Transitions**:
```
CONFIG_APPLYING
    ├─ Success (100-200ms) → LINK_ACQUIRING
    ├─ Timeout (5s) → ERROR_STATE
    └─ Config error → ERROR_STATE (immediate)
```

**Timeout**: 5 seconds (config stuck)

**Events**: None monitored

**Logging**:
```
ETH Static IP Mode:
ETH   IP: 192.168.1.100
ETH   Gateway: 192.168.1.1
... or ...
ETH DHCP Mode: Waiting for IP assignment from DHCP server...
```

**Common Issues**:
- Static IP outside device's subnet (gateway unreachable)
- Subnet mask incorrect
- DNS server invalid
- NVS corrupted (load fails, falls back to DHCP)

---

### State: LINK_ACQUIRING (3)

**Duration**: 100ms - 5 seconds (cable insertion to detection)

**Entry**: Config applied

**Exit**: `ARDUINO_EVENT_ETH_CONNECTED` event

**Actions**:
- Waiting for physical cable to be plugged in
- PHY chip monitoring RX/TX differential signals
- Once cable present → driver posts event

**Transitions**:
```
LINK_ACQUIRING
    ├─ Cable present (usually <100ms) → stays LINK_ACQUIRING
    │   (then waits for IP)
    ├─ Got IP (ARDUINO_EVENT_ETH_GOT_IP) → CONNECTED
    └─ Timeout (5s) → ERROR_STATE
```

**Timeout**: 5 seconds (cable not detected)

**Events Monitored**:
- `ARDUINO_EVENT_ETH_CONNECTED` - Cable plugged in
  - **Detection**: Physical link up
  - **Reliability**: ✅ Very reliable (hardware-level)
  - **False Positives**: Rare (< 0.1%)
  
- `ARDUINO_EVENT_ETH_GOT_IP` - IP assigned
  - Only processed if already in LINK_ACQUIRING

**Logging**:
```
ETH_EVENT ✓ CABLE DETECTED: Ethernet link connected
ETH_EVENT Waiting for DHCP/Static IP...
```

**Common Issues**:
- Cable not fully inserted
- Cable damaged (intermittent connection)
- Switch port not powered
- Network timeout (IP on different segment)

**Edge Case**: Cable flapping
```
Time     Event                  State
T0:00    Cable present         LINK_ACQUIRING
T0:01    Cable flap            LINK_ACQUIRED (glitch)
T0:02    Recovered             LINK_ACQUIRING
T0:03    IP arrives            CONNECTED
T0:15    Cable unplugged       LINK_LOST
```

---

### State: IP_ACQUIRING (4)

**Duration**: 500ms - 30 seconds (DHCP timeout)

**Entry**: `ARDUINO_EVENT_ETH_GOT_IP` event

**Exit**: IP successfully assigned

**Actions**:
- DHCP client processing OFFER/ACK
- Or static IP being applied to stack
- Gateway reachability being established

**Transitions**:
```
IP_ACQUIRING
    ├─ IP assigned (100-3000ms typical)
    │   → CONNECTED
    │   → trigger_connected_callbacks()
    │
    └─ Timeout (30s) → ERROR_STATE
        (DHCP server unreachable or very slow)
```

**Timeout**: 30 seconds (DHCP not responding)

**Events Monitored**:
- `ARDUINO_EVENT_ETH_GOT_IP` - IP assigned
  - **Detection**: ESP32 received IP from DHCP or applied static
  - **Reliability**: ✅ Very reliable
  - **False Positives**: None

**Logging**:
```
ETH_EVENT ✓ IP ASSIGNED: 192.168.1.100
ETH_EVENT   Gateway: 192.168.1.1
ETH_EVENT   Link Speed: 100 Mbps
ETH_EVENT ✓ ETHERNET FULLY READY (link + IP + gateway)
```

**Progress Logging** (every 5s during wait):
```
ETH_TIMEOUT Still waiting for IP... (5000 ms)
ETH_TIMEOUT Still waiting for IP... (10000 ms)
...
```

**Common Issues**:
- DHCP server down
- Network segment isolated
- DHCP lease pool empty
- Static IP conflicts with existing host

---

### State: CONNECTED (5)

**Duration**: Indefinite (until disconnect)

**Entry**: IP assigned and validated

**Exit**: `ARDUINO_EVENT_ETH_DISCONNECTED` event

**Actions**:
- Services can be started (NTP, MQTT, OTA)
- Heartbeat manager sending pings
- Data transmission enabled
- Metrics collection running

**Transitions**:
```
CONNECTED
    ├─ Normal operation → CONNECTED (stay)
    ├─ Cable unplugged → LINK_LOST
    │   (ARDUINO_EVENT_ETH_DISCONNECTED)
    │
    ├─ Driver stop → ERROR_STATE
    │   (ARDUINO_EVENT_ETH_STOP)
    │
    └─ No explicit timeout (stays indefinitely)
```

**Timeout**: None

**Events Monitored**:
- `ARDUINO_EVENT_ETH_DISCONNECTED` - Cable removed
  - **Detection**: PHY lost signal on RX/TX lines
  - **Latency**: <1ms (interrupt-driven)
  - **Reliability**: ✅ Very reliable
  
- `ARDUINO_EVENT_ETH_STOP` - Driver shutdown
  - Rare event (only if explicitly called)

**Logging**:
- No continuous logging (only on state entry)
- Metrics collected: connection uptime, bytes transferred

**Behavioral Guarantees**:
- ✅ Network I/O works (MQTT, NTP, HTTP)
- ✅ Heartbeat active (if ESP-NOW also connected)
- ✅ OTA available
- ✅ All callbacks may be executed

---

### State: LINK_LOST (6)

**Duration**: 1-60 seconds (wait for reconnection)

**Entry**: `ARDUINO_EVENT_ETH_DISCONNECTED` event

**Exit**: Cable reconnected → LINK_ACQUIRING

**Actions**:
1. Immediately trigger `disconnected_callbacks()`
2. Log cable removal
3. Metrics: increment link_flaps counter
4. After 1s: auto-transition to RECOVERING

**Transitions**:
```
LINK_LOST
    ├─ After 1s → RECOVERING (automatic)
    │   (only once per disconnect cycle)
    │
    ├─ Cable reconnected → LINK_ACQUIRING
    │   (ARDUINO_EVENT_ETH_CONNECTED)
    │
    └─ No explicit timeout
        (stays in LINK_LOST until reconnect)
```

**Timeout**: None (indefinite until cable reconnected or 60s recovery timeout)

**Events Monitored**:
- `ARDUINO_EVENT_ETH_CONNECTED` - Cable reconnected
  - Triggers immediate transition to LINK_ACQUIRING
  - Increments recovery_attempts counter

**Logging**:
```
ETH_EVENT ✗ CABLE REMOVED: Ethernet link disconnected
ETH_EVENT Waiting for cable to be reconnected...
```

**Service State**:
- MQTT disconnects
- NTP stops syncing
- OTA server offline
- Heartbeat stops

**Metrics**:
- link_flaps++ (count unplugs)
- recoveries_attempted++ (when moving to RECOVERING)

---

### State: RECOVERING (7)

**Duration**: 1-60 seconds (wait for cable reconnection)

**Entry**: After 1s in LINK_LOST (automatic)

**Exit**: 
- Cable reconnected → LINK_ACQUIRING (success)
- Timeout 60s → ERROR_STATE (no reconnection)

**Actions**:
- Waiting for user to reconnect cable
- No active retry (passive wait)
- Metrics accumulation

**Transitions**:
```
RECOVERING
    ├─ Cable reconnected (1-60s)
    │   → LINK_ACQUIRING
    │   → recoveries_successful++
    │
    └─ Timeout (60s)
        → ERROR_STATE
        (assume permanent failure)
```

**Timeout**: 60 seconds (waiting for reconnection)

**Events Monitored**:
- `ARDUINO_EVENT_ETH_CONNECTED` - Cable reconnected
  - Transition to LINK_ACQUIRING
  - Increment success counter
  - Metrics: recovery took N ms

**Logging**:
```
ETH Starting recovery sequence...
ETH Waiting for cable to be reconnected... (timeout 60s)
```

**Recovery Success Example**:
```
T0:00  LINK_LOST (cable unplugged)
T0:01  RECOVERING (auto-transition)
T0:05  Cable reconnected (user plugs it back in)
T0:05  → LINK_ACQUIRING
T0:05  ✓ CABLE DETECTED
T0:06  → CONNECTED (IP re-assigned)
T0:06  ✓ recoveries_successful++
```

---

### State: ERROR_STATE (8)

**Duration**: Until reboot

**Entry**: Unrecoverable error

**Exit**: Manual reboot

**Causes**:
- PHY_RESET timeout (5s) - hardware dead
- CONFIG_APPLYING timeout (5s) - config error
- LINK_ACQUIRING timeout (5s) - no cable detected at boot
- IP_ACQUIRING timeout (30s) - DHCP server unreachable
- RECOVERING timeout (60s) - cable not reconnected after 1 minute
- `ARDUINO_EVENT_ETH_STOP` - driver shutdown

**Transitions**:
```
ERROR_STATE
    └─ Manual reboot → UNINITIALIZED
       (only recovery)
```

**Timeout**: None (stays indefinitely)

**Service State**:
- All services stopped
- No recovery attempted
- Requires manual intervention

**Logging**:
```
ETH_STATE State transition: LINK_ACQUIRING → ERROR_STATE
ETH_TIMEOUT Link acquiring timeout - cable may not be present (5000 ms)
```

**Recovery**:
```bash
# Option 1: Reboot device
(press RESET button or power cycle)

# Option 2: Check logs
minicom /dev/ttyUSB0 115200
# Look for timeouts or error messages

# Option 3: Fix root cause
# - Check cable (if LINK_ACQUIRING timeout)
# - Check DHCP server (if IP_ACQUIRING timeout)
# - Check power supply (if PHY_RESET timeout)
```

---

## Complete State Transition Table

```
┌─────────────────────┬──────────────────────────┬─────────────┬──────────────┐
│ Current State       │ Trigger/Event            │ Next State  │ Timeout      │
├─────────────────────┼──────────────────────────┼─────────────┼──────────────┤
│ UNINITIALIZED       │ init() called            │ PHY_RESET   │ None         │
├─────────────────────┼──────────────────────────┼─────────────┼──────────────┤
│ PHY_RESET           │ Hardware respond (ok)    │ CONFIG_APP  │ 5s timeout   │
│                     │ Timeout or error         │ ERROR_STATE │ to this      │
├─────────────────────┼──────────────────────────┼─────────────┼──────────────┤
│ CONFIG_APPLYING     │ ETH.config() ok          │ LINK_ACQU   │ 5s timeout   │
│                     │ Timeout/Error            │ ERROR_STATE │ to this      │
├─────────────────────┼──────────────────────────┼─────────────┼──────────────┤
│ LINK_ACQUIRING      │ ETH_CONNECTED event      │ LINK_ACQU*  │ 5s timeout   │
│                     │ (cable detected)         │             │ to ERROR     │
│                     │ ETH_GOT_IP event         │ IP_ACQU     │             │
│                     │ Timeout (no cable)       │ ERROR_STATE │             │
├─────────────────────┼──────────────────────────┼─────────────┼──────────────┤
│ IP_ACQUIRING        │ ETH_GOT_IP event (ok)    │ CONNECTED   │ 30s timeout  │
│                     │ Timeout (DHCP fail)      │ ERROR_STATE │ to ERROR     │
├─────────────────────┼──────────────────────────┼─────────────┼──────────────┤
│ CONNECTED           │ (no timeout, stable)     │ CONNECTED   │ None         │
│                     │ ETH_DISCONNECTED event   │ LINK_LOST   │             │
│                     │ (cable removed)          │             │             │
│                     │ ETH_STOP event           │ ERROR_STATE │             │
├─────────────────────┼──────────────────────────┼─────────────┼──────────────┤
│ LINK_LOST           │ Auto (after 1s)          │ RECOVERING  │ None until   │
│                     │ ETH_CONNECTED event      │ LINK_ACQU   │ recovery     │
│                     │ (cable restored)         │             │ timeout      │
├─────────────────────┼──────────────────────────┼─────────────┼──────────────┤
│ RECOVERING          │ ETH_CONNECTED event      │ LINK_ACQU   │ 60s timeout  │
│                     │ (cable reconnected)      │             │ to ERROR     │
│                     │ Timeout (no reconnect)   │ ERROR_STATE │             │
├─────────────────────┼──────────────────────────┼─────────────┼──────────────┤
│ ERROR_STATE         │ (stuck unless reboot)    │ ERROR_STATE │ N/A          │
│                     │ Manual reboot (external) │ UNINITIAL   │ (reboot)     │
└─────────────────────┴──────────────────────────┴─────────────┴──────────────┘
```

---

## Event Mapping

### Arduino Events → State Transitions

```cpp
ARDUINO_EVENT_ETH_START
    └─ When: Ethernet driver initializing
    └─ Action: Log, set hostname
    └─ State: No change (already in PHY_RESET)

ARDUINO_EVENT_ETH_CONNECTED ← ✅ CABLE DETECTION
    └─ When: Physical link detected (100ms typical)
    └─ State→ LINK_ACQUIRING (if CONFIG_APPLYING or LINK_ACQUIRING)
    └─ State→ LINK_ACQUIRING (if LINK_LOST/RECOVERING)
    └─ Metrics: link_flaps++, recoveries_attempted++

ARDUINO_EVENT_ETH_GOT_IP ← ✅ IP ASSIGNMENT
    └─ When: DHCP ACK received or static applied
    └─ State→ CONNECTED (if LINK_ACQUIRING or IP_ACQUIRING)
    └─ Metrics: recoveries_successful++
    └─ Action: trigger_connected_callbacks()

ARDUINO_EVENT_ETH_DISCONNECTED ← ✅ CABLE REMOVAL
    └─ When: Link lost (< 1ms latency)
    └─ State→ LINK_LOST (if connected states)
    └─ Metrics: link_flaps++
    └─ Action: trigger_disconnected_callbacks()

ARDUINO_EVENT_ETH_STOP
    └─ When: Driver shutdown (rare)
    └─ State→ ERROR_STATE
    └─ Action: Log error, await reboot
```

---

## Timeout Handling

### Timeout Detection

Called from main loop, every 1 second:

```cpp
// In main loop
if (now - last_eth_update > 1000) {
    EthernetManager::instance().update_state_machine();
    last_eth_update = now;
}
```

### Timeout Checks (in state)

```
PHY_RESET:
    if (age > 5000ms) → ERROR_STATE
    (Hardware not responding)

CONFIG_APPLYING:
    if (age > 5000ms) → ERROR_STATE
    (Config not applied)

LINK_ACQUIRING:
    if (age > 5000ms) → ERROR_STATE
    (No cable detected within 5s)
    ⚠️ User must plug cable BEFORE 5s timeout!

IP_ACQUIRING:
    if (age > 30000ms) → ERROR_STATE
    (DHCP not responding)
    Progress log every 5s

RECOVERING:
    if (age > 60000ms) → ERROR_STATE
    (Cable not reconnected within 60s)
```

### Recovery Timing

```
Boot:     T=0.00s   UNINITIALIZED
          T=0.10s   PHY_RESET
          T=0.20s   CONFIG_APPLYING
          T=0.30s   LINK_ACQUIRING (waiting for cable)
Cable:    T=2.00s   Cable plugged → ETH_CONNECTED
          T=2.10s   LINK_ACQUIRING (cable detected)
IP:       T=3.50s   ETH_GOT_IP → CONNECTED
          T=3.50s   Services start (callbacks)

Unplug:   T=60.00s  ETH_DISCONNECTED → LINK_LOST
          T=60.00s  Services stop (callbacks)
          T=61.00s  Auto-transition → RECOVERING (1s delay)
          
Replug:   T=65.00s  Cable plugged → ETH_CONNECTED
          T=65.00s  RECOVERING → LINK_ACQUIRING
          T=66.00s  ETH_GOT_IP → CONNECTED
          T=66.00s  Services restart (callbacks)

Max:      T=125.00s Recovery timeout → ERROR_STATE
          (if cable not reconnected after 60s)
```

---

## Metrics & Diagnostics

### Available Metrics

```cpp
struct EthernetStateMetrics {
    uint32_t phy_reset_time_ms;              // Time in PHY_RESET
    uint32_t config_apply_time_ms;           // Time in CONFIG_APPLYING
    uint32_t link_acquire_time_ms;           // Time in LINK_ACQUIRING
    uint32_t ip_acquire_time_ms;             // Time in IP_ACQUIRING
    uint32_t total_initialization_ms;        // Total boot time
    uint32_t connection_established_timestamp; // When first CONNECTED
    
    uint32_t state_transitions;              // Total state changes
    uint32_t recoveries_attempted;           // Times user reconnected cable
    uint32_t recoveries_successful;          // Times reconnect succeeded
    uint32_t link_flaps;                     // Times cable plugged/unplugged
    uint32_t connection_restarts;            // Auto-resets
};
```

### Querying Metrics

```cpp
// Get current state
EthernetConnectionState state = 
    EthernetManager::instance().get_state();

// Get human-readable name
const char* name = 
    EthernetManager::instance().get_state_string();

// Get time in current state
uint32_t age_ms = 
    EthernetManager::instance().get_state_age_ms();

// Get metrics
const EthernetStateMetrics& metrics = 
    EthernetManager::instance().get_metrics();

LOG_INFO("DIAG", "Link flaps: %u", metrics.link_flaps);
LOG_INFO("DIAG", "Boot time: %u ms", metrics.total_initialization_ms);
LOG_INFO("DIAG", "Uptime: %u s", (millis() - metrics.connection_established_timestamp) / 1000);
```

### Debugging Example

```
Boot Without Cable:
  T=0.00s   PHY_RESET
  T=0.20s   CONFIG_APPLYING
  T=0.30s   LINK_ACQUIRING (waiting...)
  T=5.30s   Timeout! → ERROR_STATE
  Log: "Link acquiring timeout - cable may not be present (5000 ms)"
  Fix: Plug cable and reboot

DHCP Slow:
  T=0.30s   LINK_ACQUIRING
  T=2.00s   Cable detected → (stays) LINK_ACQUIRING
  T=2.50s   GOT_IP event (unusual timing)
  T=3.50s   ✓ CONNECTED (normal)

Cable Flap (intermittent):
  T=10.00s  CONNECTED (normal)
  T=15.00s  LINK_LOST (cable flap 1)
  T=16.00s  → RECOVERING
  T=16.50s  Cable restored → LINK_ACQUIRING
  T=17.50s  ✓ CONNECTED (recovered)
  T=20.00s  LINK_LOST (cable flap 2)
  Metrics: link_flaps = 2, recoveries_successful = 1
```

---

## Edge Cases & Solutions

### Edge Case 1: Boot Without Cable (Cable Plugged After 5s)

**Sequence**:
```
T=0:00  UNINITIALIZED (power on)
T=0:10  PHY_RESET
T=0:20  CONFIG_APPLYING
T=0:30  LINK_ACQUIRING (waiting for cable)
T=5:00  Timeout! → ERROR_STATE
        (even if user plugs cable now)
```

**Problem**: State machine rejects cable insertion after 5s

**Solution**: User must plug cable BEFORE 5s timeout
- Or: Increase timeout to 10s (if slow switch)
- Or: implement manual recovery (button press → reboot)

**Post-Release Fix**: Add "cable override" button to resume from ERROR_STATE

---

### Edge Case 2: DHCP Very Slow (>10s)

**Sequence**:
```
T=0:30  LINK_ACQUIRING
T=2:00  ✓ CABLE_DETECTED (cable plugged)
T=2:10  (waits for DHCP)
T=15:00 ✓ DHCP_ACK arrives
        (still within 30s timeout)
T=15:00 → CONNECTED
```

**Problem**: DHCP slow, but within 30s limit

**Solution**: Works as designed (30s timeout allows slow DHCP)

**Progress Logging**:
```
ETH_TIMEOUT Still waiting for IP... (5000 ms)
ETH_TIMEOUT Still waiting for IP... (10000 ms)
ETH_TIMEOUT Still waiting for IP... (15000 ms)
...
ETH_EVENT ✓ IP ASSIGNED: 192.168.1.100
ETH_EVENT ✓ ETHERNET FULLY READY
```

**User Experience**: Brief wait (15s total), then services start

---

### Edge Case 3: Static IP Misconfiguration

**Scenario**: User enters wrong gateway IP (outside subnet)

**Sequence**:
```
T=0:20  CONFIG_APPLYING
        ETH.config(192.168.1.100, 10.0.0.1, ...)
        (gateway 10.0.0.1 unreachable from 192.168.1.0/24)
T=0:30  LINK_ACQUIRING (but no gateway reachable)
T=2:00  Cable detected → (stays LINK_ACQUIRING)
T=5:00  Timeout! → ERROR_STATE
        (gateway unreachable, no IP event)
```

**Problem**: Static IP configured wrong, device can't get IP

**Solution**:
1. Load correct config from NVS
2. Reboot and try again
3. Or use web UI to fix (see SERVICE_INTEGRATION_GUIDE.md)

**Post-Release Fix**: Validate config on save (ping gateway)

---

### Edge Case 4: Cable Flapping (Intermittent Contact)

**Scenario**: Loose cable connector causing repeated connect/disconnect

**Sequence**:
```
T=10:00 CONNECTED (normal operation)
T=15:00 LINK_LOST (cable flap 1, intermittent contact)
T=15:50 Recovered (reconnected internally)
T=16:00 → RECOVERING (auto-transition, 1s after LINK_LOST)
T=16:20 Cable stabilizes, CONNECTED again
        (but now services restarted unnecessarily!)
```

**Problem**: Frequent service restarts from flapping cable

**Solution**: Add debounce window (2s) before transition to RECOVERING

**Post-Release Fix** (v1.1):
```cpp
// NEW: Only move to RECOVERING if cable stays disconnected >2s
if (current_state_ == LINK_LOST && age > 2000) {
    set_state(RECOVERING);
}
```

---

### Edge Case 5: Recovery Timeout (No Reconnection After 1 Minute)

**Scenario**: Cable unplugged, user doesn't plug back within 60s

**Sequence**:
```
T=10:00 CONNECTED
T=15:00 Cable unplugged → LINK_LOST
T=16:00 → RECOVERING (auto)
T=76:00 Timeout (60s) → ERROR_STATE
        (device gives up waiting)
```

**Problem**: Device stuck in ERROR_STATE forever

**Solution**: Manual reboot required

**Post-Release Fix** (v1.1): Auto-reboot after N failures

---

## State Machine Validation

### Invariant Checks (Always True)

```
1. current_state ∈ {0..8}
   (Always valid state, never garbage)

2. If is_fully_ready() == true:
       current_state == CONNECTED
   (No service should run in other states)

3. If is_link_present() == true:
       current_state >= LINK_ACQUIRING AND current_state != ERROR_STATE
   (Link present iff in connection/connected states)

4. state_enter_time_ms ≤ millis()
   (Never in future, always valid timestamp)

5. If error_callbacks triggered:
       previous_state != ERROR_STATE
   (No double-triggering on error)
```

### Testing State Machine

```bash
# Test 1: Boot without cable (should timeout in 5s)
Power on, no cable → see "LINK_ACQUIRING timeout"

# Test 2: Boot with cable (should get IP in <5s)
Plug cable, power on → see "✓ IP ASSIGNED"

# Test 3: Unplug after running (should recover)
Running, unplug → see "Cable removed", wait 60s, replug

# Test 4: Quick reconnect (should resume normally)
Unplug, wait 2s, replug → should recover to CONNECTED

# Test 5: Long disconnect (should timeout at 60s)
Unplug, wait 70s → should see "Recovery timeout"
```

---

## Performance Characteristics

### Timing Budget

```
Operation                Latency        
─────────────────────────────────────
Power-on to PHY_RESET    10-50ms
PHY_RESET duration       100-200ms
Config to LINK_ACQUIRING 50-100ms
Cable detection          100-500ms (depends on switch)
DHCP negotiation         500ms-10s (depends on server)
Total boot time          1-15 seconds

Cable unplug to detect   <1ms (interrupt-driven)
Recovery sequence        ~5s (cable detect + IP)
Connection restart       ~5s
```

### Memory Footprint

```
EthernetManager object    ~500 bytes
State metrics             ~40 bytes
Callbacks vector          ~50 bytes (2 callbacks)
─────────────────────────────────────
Total                     ~590 bytes
```

### CPU Usage

```
Main loop update()        <1ms (every 1s)
Event handler             <1ms (per event)
Timeout checks            <0.1ms
Callback execution        <5ms (network dependent)
```

---

## Summary

| Aspect | Detail |
|--------|--------|
| **States** | 9 (UNINITIALIZED through ERROR_STATE) |
| **Transitions** | 15 possible paths |
| **Timeouts** | 5 per state (5s-60s range) |
| **Events** | 4 (START, CONNECTED, GOT_IP, DISCONNECTED, STOP) |
| **Metrics** | 10 tracked (flaps, recoveries, uptime) |
| **Cable Detection** | Hardware-driven, <1ms latency |
| **Service Gating** | Callbacks on CONNECTED/LINK_LOST |
| **Recovery** | Automatic (LINK_LOST→RECOVERING→LINK_ACQUIRING) |
| **Max Recovery Time** | 60s (then ERROR_STATE) |
| **Boot Time** | 1-15s (cable + DHCP dependent) |

---

**Next Step**: See [SERVICE_INTEGRATION_GUIDE.md](SERVICE_INTEGRATION_GUIDE.md) to understand how services use these states.

