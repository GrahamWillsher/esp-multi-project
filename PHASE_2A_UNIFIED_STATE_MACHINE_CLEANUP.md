# Phase 2A: Unified Cross-Codebase State Machine & Redundancy Cleanup

**Date:** March 10, 2026  
**Status:** IMPLEMENTATION READY  
**Scope:** Eliminate volatile flags, consolidate state ownership, unify TX/RX state machines into common architecture

---

## Executive Summary

Phase 2A completes the architectural unification started in Phase 1. Both TX and RX now have state machines, but redundant volatile flags and split state ownership still exist. This phase **removes all legacy volatile flags** and **consolidates state management into the common library**.

**Outcome:** Single source of truth for all connection/transmission/message state across both codebases.

---

## Redundant Code to Remove

### Receiver Volatile Flags (to remove)
**File:** `espnowreciever_2/src/common.h`
```cpp
extern volatile bool data_received;           // ← REMOVE (use RxStateMachine.message_state())
extern volatile uint8_t received_soc;         // ← CONVERT to const accessor via RxStateMachine
extern volatile int32_t received_power;       // ← CONVERT to const accessor via RxStateMachine
extern volatile uint32_t received_voltage_mv; // ← CONVERT to const accessor via RxStateMachine
extern volatile int wifi_channel;             // ← MOVE to channel manager
extern volatile bool transmitter_connected;   // ← REMOVE (use RxStateMachine.connection_state())
```

### Transmitter Flags (already cleaned, verify no stragglers)
**Files:** `ESPnowtransmitter2/src/espnow/message_handler.h`, `data_sender.cpp`
```cpp
volatile bool transmission_active_;  // Already using TxStateMachine instead
volatile bool receiver_connected_;   // Already using TxStateMachine instead
```

### Receiver Connection State Managers (consolidate)
**Files:** 
- `espnowreciever_2/src/state/connection_state_manager.cpp` (redundant if RxStateMachine exists)
- `espnowreciever_2/src/espnow/rx_connection_handler.cpp` (should own connection lifecycle)

---

## Phase 2A Deliverables

### 1. Unified State Machine in Common Library
**File:** `esp32common/espnow_common_utils/espnow_state_machine_unified.h/cpp`

**Purpose:** Single source of truth for TX and RX state semantics
- TX uses `TxStateMachine` (device-local)
- RX uses `RxStateMachine` (device-local)
- Both report state to this common enum/interface
- Common manager makes timeout/reconnection decisions based on unified state model

### 2. Receiver Cleanup (Remove Volatile Flags)
**Files to modify:**
- `espnowreciever_2/src/common.h` - Remove volatile declarations
- `espnowreciever_2/src/state/connection_state_manager.cpp` - Migrate to RxStateMachine
- `espnowreciever_2/src/display/display_engine.cpp` - Change from flag reads to state queries
- `espnowreciever_2/src/espnow/espnow_tasks.cpp` - Remove flag assignments

**Changes:**
```cpp
// BEFORE (WRONG)
if (ESPNow::data_received) {
    ESPNow::data_received = false;
    uint8_t soc = ESPNow::received_soc;
}

// AFTER (RIGHT)
auto state = RxStateMachine::instance().message_state();
if (state == RxStateMachine::MessageState::VALID) {
    uint8_t soc = display_context.last_battery_data.soc;  // cached in context
}
```

### 3. Transmitter Cleanup (Verify No Stragglers)
**Files to audit:**
- `ESPnowtransmitter2/src/espnow/message_handler.h` - Verify no volatile flags
- `ESPnowtransmitter2/src/espnow/data_sender.cpp` - Verify uses TxStateMachine
- `ESPnowtransmitter2/src/main.cpp` - Verify state machine initialization

**Changes:** (likely none needed, already done in Phase 1C)

### 4. Create State Access Layer in Common
**File:** `esp32common/espnow_common_utils/espnow_state_accessor.h`

**Purpose:** Unified way to query state across both codebases

```cpp
class EspNowStateAccessor {
public:
    // TX device interface
    static TxStateMachine::ConnectionState tx_connection_state();
    static TxStateMachine::Stats tx_stats();
    
    // RX device interface  
    static RxStateMachine::ConnectionState rx_connection_state();
    static RxStateMachine::MessageState rx_message_state();
    static RxStateMachine::Stats rx_stats();
    
    // Unified heartbeat/activity interface
    static uint32_t last_activity_ms();
    static uint32_t activity_age_ms();
    static bool is_stale(uint32_t threshold_ms);
};
```

### 5. Update Documentation
- Phase findings: Mark Phase 2A complete
- Update state machine architecture diagram
- Document cleanup checklist with before/after code samples

---

## Implementation Order

### Step 1: Receiver Cleanup (3 hours)
1. [ ] Audit all uses of `volatile bool data_received` in codebase
2. [ ] Create list of all reader locations (display, tasks, handlers)
3. [ ] Replace flag reads with `RxStateMachine::message_state()` queries
4. [ ] Replace flag writes with state machine transitions
5. [ ] Remove `data_received` from `common.h`
6. [ ] Rebuild receiver, verify no compilation errors
7. [ ] Verify display still updates correctly (no race conditions)

### Step 2: Receiver Data Accessors (2 hours)
1. [ ] Identify all reads of `volatile received_soc`, `received_power`, etc.
2. [ ] Create data cache in `RxStateMachine` or display context
3. [ ] Update all readers to use cached values instead of volatile
4. [ ] Remove volatile declarations from `common.h`
5. [ ] Rebuild, verify data display correct

### Step 3: Transmitter Audit (1 hour)
1. [ ] Grep for any remaining `volatile` in transmitter espnow code
2. [ ] Verify `transmission_active_` not in message_handler.h
3. [ ] Verify all checks use `TxStateMachine`
4. [ ] Rebuild transmitter as sanity check

### Step 4: Create Common State Accessor (2 hours)
1. [ ] Add `espnow_state_accessor.h` to common library
2. [ ] Implement unified query interface
3. [ ] Add to both receiver and transmitter projects
4. [ ] Update connection manager to use accessor for state decisions

### Step 5: Update Documentation (1 hour)
1. [ ] Update phase findings with completion status
2. [ ] Create "Cleanup Checklist" document showing all removed code
3. [ ] Document new state query patterns
4. [ ] Update architecture diagrams

---

## Verification Checklist

### Compilation
- [ ] Receiver compiles without errors (`pio run -e receiver_tft`)
- [ ] Transmitter compiles without errors (`pio run -e esp32-poe2`)
- [ ] No warnings about undefined references to removed volatiles

### Functionality
- [ ] Receiver display updates on message receipt
- [ ] No race condition on display update (no flickering)
- [ ] TX/RX connection state transitions logged correctly
- [ ] Stale detection still works (messages stop, display shows STALE after 90s)
- [ ] Reconnection still works (disconnect, wait 3s, reconnect, resume)

### Code Quality
- [ ] Zero `volatile bool` in espnow subsystem
- [ ] All state accessed via state machine or accessor
- [ ] No direct global variable reads outside initialization
- [ ] Consistent state query patterns across both codebases

---

## Timeline

| Phase | Duration | Effort |
|-------|----------|--------|
| **Receiver Cleanup** | 3 hours | Flag removal, state queries |
| **Receiver Data Accessors** | 2 hours | Volatile data removal |
| **Transmitter Audit** | 1 hour | Verification only |
| **Common Accessor Layer** | 2 hours | New unified interface |
| **Documentation** | 1 hour | Completion doc + cleanup checklist |
| **TOTAL** | **9 hours** | Single developer sprint |

---

## Success Criteria

✅ **Phase 2A is COMPLETE** when:
- [ ] All `volatile bool` flags removed from receiver espnow code
- [ ] All `volatile` data accessors replaced with state machine queries
- [ ] Transmitter verified clean (no stragglers)
- [ ] Common state accessor layer implemented and used
- [ ] Both codebases compile without warnings
- [ ] All functionality tests PASS
- [ ] Documentation updated

---

## Expected Outcome

### Code Quality Improvement
- **Before:** 6+ volatile flags scattered across 4+ files
- **After:** 0 volatile flags, single source of truth via state machines
- **Cleaner APIs:** Query state machine instead of reading volatile globals

### Architecture Improvement
- **Before:** TX/RX state machines exist but not integrated into common
- **After:** Unified state interface accessible from both codebases
- **Easier Testing:** State machine testable in isolation

### Maintainability Improvement
- **Before:** State changes scattered across multiple locations
- **After:** All state transitions centralized in state machine
- **Easier Debugging:** Clear audit trail of state changes

---

## Related Files Summary

### To Remove (Lines of Code)
- `espnowreciever_2/src/common.h` - volatile bool declarations (~6 lines)
- `espnowreciever_2/src/state/connection_state_manager.cpp` - data_received flag logic (~20 lines)

### To Create (New Lines of Code)
- `esp32common/espnow_common_utils/espnow_state_accessor.h` (~80 lines)
- `esp32common/espnow_common_utils/espnow_state_machine_unified.h` (~100 lines)

### Net Change
- **Removed:** ~26 lines of redundant/unsafe volatile code
- **Added:** ~180 lines of safe, testable state accessor code
- **Net:** +154 lines for a significantly safer architecture

---

## Recommendation

**PROCEED WITH PHASE 2A IMMEDIATELY**

This is a high-impact cleanup that:
1. ✅ Removes unsafe volatile flags
2. ✅ Centralizes state ownership
3. ✅ Improves code maintainability
4. ✅ Enables better testing
5. ✅ Provides foundation for Phase 2B (orchestration unification)

**Estimated effort:** 1 developer, 2-day sprint
**Risk:** LOW (mostly removal of unused code, no new functionality)
**Value:** HIGH (cleaner architecture, easier debugging)

---

## Next After Phase 2A

**Phase 2B:** Unified Orchestration
- Consolidate reconnection/backoff logic
- Unified message routing based on state
- Single source of truth for timeout decisions
- Common cleanup/side-effect handlers

**Phase 3:** Full State Machine Redesign (if needed)
- If Phase 2A+2B don't sufficiently consolidate, redesign with true unified state machine
- More comprehensive refactor (~40 hours)
- Necessary only if Phase 2A leaves significant architectural gaps

---

## Status: READY FOR IMPLEMENTATION ✅

