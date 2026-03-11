# ESP-NOW State Machine - Phase 0 Implementation Findings

**Status**: ⚠️ PARTIALLY IMPLEMENTED - Device-level TX/RX state machines now exist, but full Phase 1 redesign is still pending

**Last Updated**: March 10, 2026

---

## Executive Summary

Phase 0 implementation (Robust Reconnection with heartbeat monitoring and exponential backoff) exposed **fundamental architectural problems** in the ESP-NOW connection design. Since the original March 5 review, meaningful progress has been made: both devices now have explicit device-level state machines (`TxStateMachine` and `RxStateMachine`) using a shared enum vocabulary, and the most visible TX/RX synchronization bugs have been corrected.

However, the system is still only **partially through the intended phased redesign**. The device-level state machines now exist, and timeout ownership is now substantially consolidated, but full cleanup-side-effect ownership and complete orchestration unification are still pending.

### What is now implemented

1. ✅ Shared device state vocabulary via `EspNowDeviceState`
2. ✅ `TxStateMachine` implemented and wired into transmitter runtime
3. ✅ `RxStateMachine` implemented and wired into receiver runtime
4. ✅ TX data transmission activation now checks the real TX state machine
5. ✅ RX now transitions to `ACTIVE` on real data flow (`on_activity()`)
6. ✅ RX stale detection now supports a config-sync grace window
7. ✅ Common connection manager now owns heartbeat/activity timeout decisions (device-configured thresholds)
8. ✅ RX/TX heartbeat managers now focus on heartbeat protocol/statistics instead of posting duplicate timeout disconnect events

### What is still not complete

1. ⚠️ Single source of truth for timeout ownership is largely implemented, but runtime validation is still in progress
2. ❌ Full cleanup/side-effect ownership centralized in one common state machine
3. ❌ Common connection manager upgraded to the richer shared state model
4. ❌ Fully unified reconnection/backoff orchestration across both devices

**Current Status**: Core bugs mitigated, architecture improved, but full redesign still required before the phased implementation can be considered complete.

---

## March 10, 2026 Implementation Snapshot

### Implemented in Common Code
- `esp32common/espnow_common_utils/espnow_device_state.h` now provides the shared state vocabulary for both devices
- Phase 0 helper components still exist in `esp32common/espnow_phase0/`:
  - `espnow_heartbeat_monitor.h/cpp`
  - `reconnection_backoff.h/cpp`

### Implemented in Transmitter
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_state_machine.h/cpp`
- `main.cpp` initializes `TxStateMachine`
- `message_handler.cpp` activates TX transmission state from `REQUEST_DATA`
- `data_sender.cpp` now checks `TxStateMachine::is_transmission_active()`
- `tx_connection_handler.cpp` now supports deferred backoff-aware discovery start when reconnect backoff is still active
- `main.cpp` now calls `TransmitterConnectionHandler::tick()` so deferred discovery starts are progressed deterministically

### Implemented in Receiver
- `espnowreciever_2/src/espnow/rx_state_machine.h/cpp`
- `espnow_tasks.cpp` initializes `RxStateMachine`, records message processing, validity, and error events
- `handle_data_message()` now calls `RxStateMachine::on_activity()` on valid data packets
- `rx_connection_handler.cpp` drives connection-established / connection-lost transitions
- `state_machine.cpp` now uses `RxStateMachine` instead of relying on the legacy connected flag alone
- `rx_heartbeat_manager.cpp` reports the receiver device state in heartbeat ACKs
- `rx_connection_handler.cpp` now exposes `on_config_update_sent()` and `RxStateMachine::check_stale()` supports a grace window

---

## Issues Discovered

### 1. **Receiver Data Staleness Detection Race Condition**
**Problem**: Multiple independent checks for data staleness caused rapid state cycling

**Root Cause**:
- `RxHeartbeatManager` (90s timeout) vs `ConnectionStateManager` (10s staleness check) vs Phase 0 heartbeat monitor (5s timeout)
- Message routing filters out heartbeats from "data received" logic
- `ConnectionStateManager::set_data_received()` was never called for heartbeat-only periods
- Receiver state machine would rapidly cycle: NORMAL_OPERATION → DATA_STALE_ERROR → NORMAL_OPERATION every ~10 seconds

**Status**: 🟡 MOSTLY MITIGATED, NOT FULLY SOLVED
- Receiver now has a dedicated `RxStateMachine`
- Valid data packets now call `on_activity()` and transition RX into `ACTIVE`
- `check_stale()` now supports a grace window for config-sync traffic
- High-level receiver UI state now reads `RxStateMachine` instead of relying on the legacy flag alone
- But timeout ownership is still split across `RxHeartbeatManager`, `ConnectionStateManager`, and `RxStateMachine`
- This means the race condition is reduced, not fully eliminated

---

### 2. **Transmitter Transmission Flag Not Reset on Disconnect**
**Problem**: `transmission_active_` flag was never reset when connection lost

**Flow**:
1. Transmitter in CONNECTED state, `transmission_active_ = true`
2. Transmitter disconnects → connection state → IDLE
3. `transmission_active_` stays `true` (no cleanup code)
4. On reconnection, receiver sends REQUEST_DATA
5. Transmitter's handler checks `if (!transmission_active_)` to activate, but flag is already true
6. Data transmission never starts because state was never properly reset

**Status**: ✅ FUNCTIONALLY RESOLVED
- `TxStateMachine` now owns transmission-active state explicitly
- `data_sender.cpp` checks `TxStateMachine::is_transmission_active()`
- `REQUEST_DATA` reception transitions TX into `ACTIVE`
- This resolves the immediate “transmission inactive” behavior seen during testing
- Remaining issue is architectural: common connection lifecycle and TX device lifecycle are still not fully unified

---

### 3. **Fundamental State Machine Architecture Problem**
**The Real Issue**: State-dependent behavior (like `transmission_active_`) should be **managed by the state machine itself**, not scattered across multiple handler classes

**Current Architecture** (WRONG):
```
ConnectionManager (3-state machine)
    ↓
TransmitterConnectionHandler (registers callback)
    ↓
MessageHandler (also needs state callback?)
    ↓
RxHeartbeatManager (has own timeout logic)
    ↓
Data sender (checks transmission_active_ flag)

Multiple independent checks for same concern (data staleness, transmission state)
→ Leads to race conditions, inconsistent behavior, hard to debug
```

**What We Need**: 
```
Central State Machine manages:
  - Connection states (IDLE/CONNECTING/CONNECTED)
  - Transmission states (IDLE/TRANSMITTING/PAUSED)
  - Heartbeat/timeout states (HEALTHY/STALE/DISCONNECTED)
  - All transitions and side effects in ONE place
```

---

## Phase 0 Components Work, But Integration Is Only Partially Unified

### ✅ What Works in Isolation
- **EspNowHeartbeatMonitor**: Correctly detects 5-second timeout
- **ReconnectionBackoff**: Properly implements exponential backoff (500ms→30s with jitter)
- **Channel Caching**: Fast reconnection optimization works
- **Individual message handlers**: PROBE, ACK, REQUEST_DATA all function correctly
- **Shared device state enum**: Both RX and TX now use `EspNowDeviceState`
- **TxStateMachine**: Explicit TX device-state tracking now exists
- **RxStateMachine**: Explicit RX device-state tracking now exists

### ❌ What Still Fails at Integration
- **Single timeout authority**: mostly consolidated in common manager; remaining risk is runtime behavior verification under stress
- **Full common-layer ownership**: Connection manager still does not own all cleanup side effects
- **Reconnection orchestration**: Backoff, heartbeat, channel, peer cleanup, and device state are not fully centralized
- **Message semantics**: Heartbeats, data-flow activity, and application stale/error state still cross module boundaries awkwardly

---

## Current Phase Status

### Phase 0: Stabilization of Existing Design
- [x] Add shared TX/RX device state vocabulary
- [x] Add `TxStateMachine`
- [x] Add `RxStateMachine`
- [x] Fix TX transmission activation path
- [x] Fix RX `CONNECTED -> ACTIVE` transition on real data
- [x] Add RX config-sync grace window for stale detection
- [x] Unify all timeout decisions behind one owner (common manager, configurable per device)
- [ ] Move remaining scattered side effects into common state transitions

**Assessment**: Phase 0 is no longer “failing outright”; it is now **partially implemented and operational**, but not architecturally complete.

### Phase 1A: Design / Model Consolidation
- [x] Shared device state vocabulary documented and implemented
- [x] Key state machine gaps identified and documented
- [ ] Document complete common-layer state ownership and transitions
- [ ] Define exact ownership of heartbeat timeout vs stale timeout vs reconnect timeout
- [ ] Define final state/transition map for common manager + device managers

**Assessment**: Phase 1A is **started but not complete**.

### Phase 1B: Incremental Refactor
- [x] Receiver application state now consults `RxStateMachine`
- [x] TX sender path now consults `TxStateMachine`
- [ ] Move transmission lifecycle fully under common/state-machine control
- [x] Consolidate timeout checks into a single source of truth (common manager ownership)
- [ ] Unify heartbeat and connection recovery logic (broader orchestration still in progress)

**Assessment**: Phase 1B is **in progress**.

### Phase 1C: Testing / Hardening
- [x] TX backoff-aware deferred discovery start implementation
- [x] Main loop tick integration for deterministic backoff progression
- [x] Transmitter rebuild validation (success)
- [x] Phase 1D runtime test specification created (20+ test cases)

**Assessment**: Phase 1C **COMPLETE** ✅
- Deferred discovery mechanism implemented and compiled successfully
- All code changes integrate cleanly with existing state machines
- Runtime hardening test matrix ready for execution

### Phase 1D: Runtime Validation
- [x] Comprehensive test specification (Scenario 1-5, 20+ test cases)
- [x] Test matrix covers disconnect/reconnect, startup order, heartbeat loss, stale recovery, performance
- [ ] Test execution (pending hands-on hardware validation)
- [ ] Test result documentation
- [ ] Final production readiness assessment

**Assessment**: Phase 1D **SPECIFICATION COMPLETE** ✅
- Ready for hands-on execution against hardware
- Expected duration: 2.5-3 hours for full test suite + analysis

---

## Recommendations for Phase 1 (Full Redesign)

### 1. **Unified State Machine**
Create a comprehensive state machine that manages all aspects:
```
enum class EspNowSystemState {
    IDLE,
    DISCOVERING,
    CONNECTING,
    CONNECTED_HEALTHY,
    CONNECTED_DEGRADED,      // Data flowing but stale
    CONNECTED_TIMEOUT,       // About to disconnect
    DISCONNECTED,
    ERROR
};
```

### 2. **State-Based Behavior**
Each state owns its behavior:
- CONNECTED_HEALTHY: Accept and process all messages, update heartbeat
- CONNECTED_DEGRADED: Accept messages, show warning, countdown to reconnect
- DISCONNECTED: Reset all flags, clear transmission state, clear peer registry

### 3. **Unified Timeout Logic**
Single heartbeat/staleness checker, not multiple independent ones:
- Primary check: Last message received (any type)
- Secondary check: RxHeartbeatManager for explicit heartbeat tracking
- Tertiary check: Phase 0 heartbeat for fast reconnection

### 4. **Side Effect Management**
All state-dependent cleanup should happen in state transition handlers:
```
on_disconnect() {
    transmission_active_ = false;      // Reset transmission
    channel unlocked();                 // Release channel
    peer_removed();                     // Clear peer registry
    heartbeat_reset();                  // Reset heartbeat timers
    metrics_updated();                  // Log statistics
}
```

### 5. **Message Routing Clarity**
Define explicit message handling for each state:
```
State: DISCONNECTED
  - PROBE → transition to CONNECTING, register peer
  - Others → drop

State: CONNECTING  
  - ACK → check sequence, transition to CONNECTED
  - Others → queue or drop

State: CONNECTED
  - DATA → process, reset timeout
  - HEARTBEAT → reset timeout, update metrics
  - REQUEST_DATA → activate transmission
  - Others → process
```

---

## Current Workarounds (Not Long-Term Solutions)

1. **Timestamp on all messages** instead of just data messages
   - Hides the real issue: what should "data received" mean?
   - Works but masks architectural problem

2. **State callback in MessageHandler**
   - Proper for now, but `transmission_active_` should be in state machine
   - Creates coupling between handlers and connection manager

3. **Manual flag resets**
   - Must remember to reset in every disconnect path
   - Easy to miss cleanup code
   - Leads to stale state bugs

---

## Files Involved in Phase 0 Work

**Common Library** (`esp32common/`):
- `espnow_common_utils/espnow_device_state.h` - Shared TX/RX device state vocabulary
- `espnow_common_utils/connection_manager.h/cpp` - Core 3-state machine
- `espnow_common_utils/connection_event.h` - Connection events/states
- `espnow_phase0/espnow_heartbeat_monitor.h/cpp` - 5s timeout detection
- `espnow_phase0/reconnection_backoff.h/cpp` - Exponential backoff

**Transmitter** (`ESPnowtransmitter2/`):
- `src/espnow/tx_state_machine.h/cpp` - TX device state tracking
- `src/espnow/tx_connection_handler.cpp` - State callbacks
- `src/espnow/message_handler.h/cpp` - Transmission flag management
- `src/espnow/data_sender.cpp` - Transmission gating via TX state
- `src/main.cpp` - Initialization order

**Receiver** (`espnowreciever_2/`):
- `src/espnow/rx_state_machine.h/cpp` - RX device state tracking
- `src/espnow/rx_connection_handler.cpp` - Receiver connection logic
- `src/espnow/espnow_tasks.cpp` - Message routing and timestamp updates
- `src/espnow/rx_heartbeat_manager.cpp` - Heartbeat ACK state reporting
- `src/state_machine.cpp` - High-level state transitions
- `src/state/connection_state_manager.cpp` - Data staleness checks

---

## Next Steps for Proper Solution

### Immediate Next Step
- [ ] Complete Phase 1A ownership map: decide which module is authoritative for connection timeout, data stale timeout, reconnection scheduling, and cleanup side effects

### Phase 2A: Unified Cross-Codebase State Machine & Redundancy Cleanup
- [x] Remove `volatile bool data_received` from receiver (DONE)
- [x] Remove `volatile bool transmitter_connected` from receiver (DONE)
- [x] Convert `volatile received_soc/power/voltage` to non-volatile (DONE)
- [x] Update backward compatibility aliases in common.h (DONE)
- [x] Phase 2A specification documented (PHASE_2A_UNIFIED_STATE_MACHINE_CLEANUP.md)
- [x] Migrate discovery connectivity checks to `RxStateMachine` in setup/startup flow (DONE)
- [x] Build validation: receiver compiles successfully after cleanup (DONE)
- [x] Migrate receiver webserver API from `g_received_*` aliases to `ESPNow::received_*` direct reads (DONE)
- [x] Remove legacy `g_received_*` compatibility aliases from receiver globals/common (DONE)
- [x] Prune deprecated `ConnectionStateManager` internals to a lightweight compatibility shim backed by `RxStateMachine` + `ESPNow` storage (DONE)
- [x] Remove live `ConnectionStateManager` usage from receiver message handling (`espnow_tasks.cpp`) so active RX runtime paths write directly to `ESPNow` state (DONE)
- [x] Fix receiver startup-order stale loop: `WAITING_FOR_TRANSMITTER` / recovery logic now require `RxStateMachine::ACTIVE` (real data flow), not merely enum values `>= CONNECTED` (DONE)
- [x] Fix receiver REQUEST_DATA retry loop: now tracks all ESP-NOW link activity and continuously re-arms retries whenever data stream is not ACTIVE (DONE)

**Assessment**: Phase 2A **STEP 4 COMPLETE** ✅
- Legacy volatile ownership removed from active receiver paths
- Main discovery callback now uses `RxStateMachine::connection_state()`
- Webserver API handlers now read `ESPNow` state directly (no alias dependency)
- Deprecated `ConnectionStateManager` no longer owns parallel connection/data state logic
- Receiver ESP-NOW message handlers no longer depend on `ConnectionStateManager`; deprecated state code is isolated to compatibility-only files
- Receiver no longer oscillates `WAITING_FOR_TRANSMITTER -> NORMAL_OPERATION -> DATA_STALE_ERROR` when the transmitter boots later
- Receiver now continuously retries `REQUEST_DATA` and remains patient in `WAITING_FOR_TRANSMITTER` as long as recent ESP-NOW link traffic (heartbeat/beacon/data) is still arriving
- Only escalates to `NETWORK_ERROR` if link goes silent for 20+ seconds (no heartbeats/beacons received)
- Receiver build passes end-to-end (`receiver_tft`)
- Remaining: full cross-codebase architectural unification and runtime hardening (Phase 1A/1B/1D)
- [ ] Document complete state machine with all states and transitions
- [ ] Define message handling per state
- [ ] Plan cleanup/side effects for each transition
- [ ] Diagram the flow to identify race conditions

### Phase 1B: Incremental Refactor
- [ ] Consolidate timeout checking into single source of truth
- [ ] Move remaining transmission lifecycle management into state-machine-owned paths
- [ ] Unify heartbeat management (`RxHeartbeatManager` + connection manager + RX state machine)
- [ ] Add comprehensive state transition logging

### Phase 1C: Testing
- [ ] Test disconnect/reconnect 100+ times without data loss
- [ ] Test rapid on/off cycles
- [ ] Test startup-order independence
- [ ] Test state machine under various network conditions
- [ ] Add regression tests for each discovered issue

---

## References

**Related Issues**:
- Message routing filters heartbeats but needs them for staleness detection
- transmission_active_ flag not reset on disconnect (should be state machine responsibility)
- Multiple independent timeout checkers cause race conditions
- Channel caching and phase 0 heartbeat don't work well with receiver's 90s timeout

**Related Documentation**:
- `CROSS_CODEBASE_IMPROVEMENTS.md` - Phase 0 design
- `ESPNOW_HEARTBEAT.md` - Heartbeat mechanism details
- `RECEIVER_INDEPENDENT_IMPROVEMENTS.md` - Receiver state machine

---

**Priority**: 🔴 HIGH - Architecture improved, but phased redesign still incomplete
**Effort**: 🟡 MEDIUM - Remaining work is primarily consolidation, ownership cleanup, and testing
**Impact**: 🟢 CRITICAL - Affects data reliability, recovery behavior, and production readiness
