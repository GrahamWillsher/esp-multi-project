# State Machine Hardening - Executive Summary

**Status:** CRITICAL ISSUES IDENTIFIED & FULLY DOCUMENTED  
**Date:** March 9, 2026

---

## The Problem (TL;DR)

Your ESP-NOW receiver and transmitter use **flag-based state tracking instead of proper state machines**. This causes:

✗ **Message duplication** - `data_received` flag never gets cleared properly  
✗ **Infinite hangs** - Fragment reassembly has no timeout mechanism  
✗ **Race conditions** - `volatile` is not a synchronization primitive  
✗ **Unclear retry semantics** - Transmitter retry logic is ad-hoc  
✗ **Production risk** - System is not suitable for deployment without fixing this

---

## The Solution (3 Phases)

### Phase 2C: State Machine Hardening (NEW - Must do this)

**Deliverables:**
1. RX State Machine (receiver message lifecycle + fragment reassembly)
2. TX State Machine (transmitter connection + message tracking)
3. Complete refactoring of callbacks and message handlers
4. Removal of all redundant flag variables
5. Comprehensive test suite

**Effort:** 3 weeks (single developer)

**Outcome:** 
- Clean, maintainable codebase
- Zero stale state issues
- Production-ready
- Proper timeout recovery

---

## Critical Findings (6 Major Issues)

### Issue #1: Receiver - ISR-to-Task Communication
**File:** `espnowreciever_2/src/espnow/espnow_callbacks.cpp`
**Problem:** Messages queued without consumption tracking  
**Risk:** Message loss on queue overflow, no retransmission  
**Fix:** State machine tracks QUEUED → PROCESSING → VALID → CONSUMED

### Issue #2: Receiver - Stale Flags
**File:** `espnowreciever_2/src/espnow/espnow_tasks.cpp`  
**Problem:** `data_received` flag never gets cleared  
**Risk:** Display updates with stale data, duplicate processing  
**Fix:** State machine with explicit consumption acknowledgment

### Issue #3: Receiver - Fragment Timeout Hang
**File:** `espnowreciever_2/src/espnow/` (implicit)  
**Problem:** No timeout for incomplete multi-fragment messages  
**Risk:** System hangs indefinitely if 1 of N fragments lost  
**Fix:** Fragment state machine with 5s timeout → auto-abandon

### Issue #4: Transmitter - Ad-Hoc Connection State
**File:** `espnowtransmitter2/src/espnow/message_handler.h`  
**Problem:** Connection state scattered across volatile flags  
**Risk:** Unclear connection semantics, poor error recovery  
**Fix:** TX Connection State Machine (DISCONNECTED → DISCOVERING → CONNECTED → TRANSMISSION_ACTIVE)

### Issue #5: Transmitter - Polling-Based Data Sender
**File:** `espnowtransmitter2/src/espnow/data_sender.cpp`  
**Problem:** Polls every 2 seconds whether to send (inefficient)  
**Risk:** Missed state transitions, wasted CPU cycles  
**Fix:** Event-driven approach with state machine notification

### Issue #6: Both - Unsafe Global Volatile Variables
**Files:** `globals.cpp` (receiver), `message_handler.h` (transmitter)  
**Problem:** `volatile` is not thread-safe across multiple variables  
**Risk:** Data corruption, undefined behavior  
**Fix:** All state managed by thread-safe state machines with mutexes

---

## What Gets Created (New Files)

### Receiver (NEW):
- `src/espnow/espnow_rx_state.h` - RX enums + structs
- `src/espnow/espnow_rx_state_machine.h` - RX state machine interface
- `src/espnow/espnow_rx_state_machine.cpp` - RX state machine implementation

### Transmitter (NEW):
- `src/espnow/espnow_tx_state.h` - TX enums + structs
- `src/espnow/espnow_tx_state_machine.h` - TX state machine interface
- `src/espnow/espnow_tx_state_machine.cpp` - TX state machine implementation

### Tests (NEW):
- `test/test_rx_state_machine.cpp` - RX state machine unit tests
- `test/test_tx_state_machine.cpp` - TX state machine unit tests
- Integration test updates for both projects

---

## What Gets Removed (Code Cleanup)

### Receiver:
- ❌ `volatile bool data_received` (globals.cpp)
- ❌ `DirtyFlags dirty_flags` struct (no longer needed)
- ❌ All ad-hoc timeout checks in espnow_tasks.cpp
- ❌ Manual message tracking variables
- ❌ Duplicate connection state variables

### Transmitter:
- ❌ `volatile bool transmission_active_` (message_handler.h)
- ❌ `volatile bool receiver_connected_` (message_handler.h)
- ❌ Ad-hoc retry counters and timeout checks
- ❌ Polling-based loop logic (replaced with events)

---

## State Machines Overview

### RX Message State Machine
```
IDLE → QUEUED → PROCESSING → VALID → CONSUMED
              ↘              ↙
                    ERROR
                    
TIMEOUT: Any state → TIMEOUT (if age > 5s)
```

### RX Fragment State Machine
```
IDLE → RECEIVING → COMPLETE → [consumed by app]

TIMEOUT: RECEIVING → ABANDONED (if age > 5s)
FAILURE: RECEIVING → ERROR (on CRC/format error)
```

### TX Connection State Machine
```
DISCONNECTED → DISCOVERING → CONNECTED → AUTHENTICATED → TRANSMISSION_ACTIVE
                                     ↘                          ↙
                                        RECONNECTING
```

### TX Message State Machine
```
IDLE → QUEUED → SENDING → WAITING_ACK → ACK_RECEIVED
                                      ↘              ↙
                                        TIMEOUT/FAILED
                                        (with retry logic)
```

---

## Migration: Before vs After

### Before (Broken - Current)
```cpp
// Consumer code
if (ESPNow::data_received) {
    uint8_t soc = ESPNow::received_soc;
    int32_t power = ESPNow::received_power;
    ESPNow::data_received = false;  // BUG: Race condition!
}
```

**Problems:**
- Race condition between ISR and main task
- Flag never cleared if exception occurs
- No way to know if message was actually consumed
- Duplicates indistinguishable from new messages

### After (Fixed - State Machine)
```cpp
// Option 1: Active consumption
auto* msg = EspnowRXStateMachine::instance().begin_processing();
if (msg && msg->is_valid()) {
    uint8_t soc = extract_soc(msg);
    int32_t power = extract_power(msg);
    EspnowRXStateMachine::instance().on_message_consumed(msg->sequence_number);
    // ✓ No race condition
    // ✓ Auto-timeout if not consumed
    // ✓ Duplicate detection via sequence number
}

// Option 2: Event callback (recommended)
EspnowRXStateMachine::instance().on_message_consumed_callback = 
    [](const RXMessageContext& msg) {
        uint8_t soc = extract_soc(&msg);
        // Framework auto-clears flag
    };
```

---

## Timeline & Effort

| Phase | Duration | Effort | Deliverable |
|-------|----------|--------|-------------|
| **Foundation** | Week 1 | 24 hours | All state machine files + basic functionality |
| **Receiver Integration** | Week 2 | 14 hours | Callback + task refactoring, flag removal |
| **Transmitter Integration** | Week 3 | 10 hours | Message handler + data sender refactoring |
| **Testing & Polish** | Week 3 | 8 hours | Unit tests, integration tests, documentation |
| **TOTAL** | 3 weeks | ~56 hours | Production-ready state machine system |

---

## What This Solves

| Current Problem | Root Cause | Solution | Benefit |
|-----------------|-----------|----------|---------|
| Message duplication | Stale flag | Consumption tracking | Zero duplicates |
| Fragment timeout hang | No timeout logic | Fragment state machine | Auto-recovery |
| Race conditions | Unsafe volatile | Semaphore-protected states | Data safety |
| Unclear retry behavior | Ad-hoc logic | TX state machine | Clear semantics |
| Inefficient polling | No events | Event-driven approach | Lower latency + CPU |
| Redundant flag tracking | Scattered code | Centralized state machine | Maintainability |

---

## Next Steps (For You)

1. **Review** the full specification document: `ESPNOW_STATE_MACHINE_HARDENING_SPECIFICATION.md`
2. **Decide** on implementation timeline (3 weeks of focused development)
3. **Schedule** a code review session for the state machine design
4. **Begin** implementation with Week 1 deliverables (foundation)

---

## Recommendation

**DO NOT DEPLOY** the current system to production without this work. The stale state issues are fundamental design flaws that will cause:

- Data corruption under network stress
- Unpredictable behavior in production
- Difficult-to-debug timing issues
- Potential data loss

**This work must be completed before any production deployment.**
