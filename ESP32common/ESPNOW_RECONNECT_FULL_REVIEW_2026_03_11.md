# ESP-NOW Reconnection Full Review (Receiver Running, Transmitter Reboot)

**Date:** 2026-03-11  
**Scope:** Cross-codebase review of reconnect behavior across:
- `esp32common/espnow_common_utils`
- `espnowreciever_2/src/espnow`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow`

---

## 1) Executive Summary

The current reconnect stack is **close**, but still has structural race paths and duplicate handshake paths that make recovery unreliable when one side reboots while the other keeps running.

### Key conclusion
The reconnect bug is **not one single defect**. It is the combination of:
1. **Duplicate connection paths** (standard handler callback + worker-driven peer registration),
2. **False “new connection” callback firing** when `connection_flag` is unset,
3. **Dropped “deferred” registration events** (logged as deferred but not actually queued),
4. **Disconnect cleanup callback ordering issues** (state callback runs after peer MAC is cleared),
5. **Timeout ownership still split conceptually across layers** even though much improved.

Result: the system can look “connected enough” while internal handshake ownership is inconsistent, producing warnings like:
- `on_peer_registered() called in state 2 (expected CONNECTING), deferring event`
- repeated `Transmitter connected via PROBE`

---

## 2) What the supplied receiver log proves

From your log:
- Receiver remains in `CONNECTED` at common manager level and keeps receiving traffic (`DATA_RECEIVED`, heartbeats, version beacons).
- `PEER_FOUND` events while already connected are ignored (expected in current 3-state common FSM).
- `on_peer_registered()` is being called while already in `CONNECTED` (`state 2`), then only “deferred” by log text.

This pattern means the reconnect flow is being triggered from **multiple sources**, some of which are stale/duplicate for the current state.

---

## 3) Findings (root causes)

## F1. Standard handler `on_connection` callback can fire repeatedly when `connection_flag == nullptr`
In `espnow_common_utils/espnow_standard_handlers.cpp`, `handle_probe()` and `handle_ack()` compute:
- `was_connected = false` when no `connection_flag` is provided.
- Then `on_connection(...)` fires whenever callback is set.

Receiver route config currently sets `connection_flag = nullptr` but still uses `on_connection` callback, so callback semantics become “fire often”, not “fire on state transition”.

**Impact:** Repeated “connected via PROBE” and repeated calls into registration path while already connected.

---

## F2. Receiver has two parallel registration triggers
Receiver currently can call `on_peer_registered()` from:
1. PROBE `on_connection` callback route, and
2. Worker queue peer-registration path.

These two paths race each other and are not state-coordinated.

**Impact:** Redundant state events, warning spam, non-deterministic ordering.

---

## F3. “Deferring event” is not a real deferred queue
`ReceiverConnectionHandler::on_peer_registered()` logs “deferring event” when not in `CONNECTING`, but no queue/latch is stored.

**Impact:** If this event was actually needed for progress in that moment, it is lost.

---

## F4. Disconnect callback uses cleared peer MAC from common manager
In common manager transition to `IDLE`, peer MAC is cleared before callbacks execute. RX/TX disconnect callbacks read peer MAC from manager during callback.

**Impact:** cleanup/removal can target zero MAC instead of last real peer, reducing deterministic cleanup and reconnect hygiene.

---

## F5. `ReceiverConnectionHandler::on_connection_lost()` exists but is not part of the transition callback flow
The method resets retry/first-data flags, but the main state callback path does not consistently invoke it.

**Impact:** stale local flags can survive disconnect/reconnect cycles.

---

## F6. Common FSM is only 3 states, while device FSM is richer (7 states)
Common manager: `IDLE/CONNECTING/CONNECTED`  
Device managers: `DISCONNECTED/DISCOVERING/CONNECTED/ACTIVE/STALE/...`

Bridging logic is now better than before, but boundary semantics are still fragile under reboot races.

---

## F7. Event processor cadence (100 ms) and asynchronous posting leave race windows
This is acceptable for throughput but requires strict idempotent event semantics. Current event producers are not fully idempotent.

---

## F8. Boot-order independence is not yet guaranteed by a single handshake contract
“Either side can start first” requires one unambiguous source of truth for:
- peer discovery,
- peer registration,
- stream activation (`REQUEST_DATA`),
- reconnect cleanup.

Current implementation still has split ownership.

---

## 4) Definitive solution architecture (recommended)

## S1. Single handshake owner per device
Choose exactly one path to emit `PEER_REGISTERED`:
- **Receiver:** worker path (message queue + peer check) should own it.
- PROBE/ACK route callbacks should only update telemetry/MAC cache, not connection transitions.

## S2. Make `on_connection` callback semantics explicit in common handlers
In `espnow_standard_handlers`:
- If no `connection_flag` is provided, do **not** assume new connection.
- Option A: only fire `on_connection` when flag transitions false→true.
- Option B: introduce two callbacks: `on_seen` (every time) and `on_connected_transition` (edge only).

## S3. Implement real deferred/latch behavior for peer registration
If `on_peer_registered()` arrives in non-`CONNECTING` states and can be meaningful, latch it (MAC + timestamp + reason) and consume deterministically when state allows.

## S4. Fix disconnect cleanup source of truth
On disconnect callback:
- use handler-cached peer MAC (last valid), not manager `get_peer_mac()` after transition.
- always reset local connection/retry/init flags in one place.

## S5. Tighten state invariant rules
- `NORMAL_OPERATION` requires RX `ACTIVE` only (already corrected).
- `NETWORK_ERROR` requires both data silence and link silence (already improved).
- `REQUEST_DATA` retries continue while link traffic exists and RX not `ACTIVE` (already improved).

## S6. Add reconnect generation ID
Introduce monotonically increasing `connection_epoch` on each `CONNECTED` entry.
Tag retries/requests/logs with epoch to prevent stale actions from previous sessions.

## S7. Build a deterministic reconnect test harness
Add scripted tests for all boot orders and mid-session reboots:
1. RX first → TX starts
2. TX first → RX starts
3. RX+TX connected → TX reboot
4. RX+TX connected → RX reboot
5. TX reboot during stale/recovery window

Pass criteria: convergence to `ACTIVE` without manual reset in all cases.

---

## 5) Immediate high-priority fixes (short list)

1. Remove duplicate RX registration trigger from PROBE `on_connection` callback path.  
2. Ensure `ReceiverConnectionHandler::on_connection_lost()` is called on `CONNECTED -> IDLE`.  
3. Use cached peer MAC for disconnect cleanup (not post-transition manager MAC).  
4. Adjust standard handler connection callback behavior so `connection_flag=nullptr` does not imply “new connection”.

These four changes will remove the warning loop and make reconnect behavior much more deterministic.

---

## 6) Why this will solve the “receiver running, transmitter rebooted” case

After TX reboot, RX may receive probe/heartbeat/version traffic before full stream resumes. With the above architecture:
- No duplicate registration path races.
- No false “new connection” callback spam.
- Retry loop remains armed until RX becomes `ACTIVE`.
- Disconnect/reconnect cleanup is deterministic per connection epoch.

Therefore the pair self-recovers regardless of which side starts first.

---

## 7) Recommended implementation plan

### Phase A (stability, low risk)
- Apply immediate 4 fixes above.
- Add structured logs with `connection_epoch`, state, event source (`route`, `worker`, `timeout`).

### Phase B (semantic hardening)
- Refactor standard handlers to edge-triggered connection callback semantics.
- Introduce explicit deferred registration latch with expiry.

### Phase C (verification)
- Run automated reboot matrix tests (minimum 50 cycles each scenario).
- Capture and compare convergence metrics:
  - time-to-`CONNECTED`
  - time-to-`ACTIVE`
  - false `NETWORK_ERROR` count
  - dropped-event count

---

## Final verdict

Your diagnosis request is correct: reconnect is still not fully robust yet.  
The system now has good core components, but **handshake ownership and callback semantics must be unified** to make reconnect truly boot-order independent and deterministic.

---

## 8) Implementation status (completed)

The review recommendations have now been implemented in code.

### ✅ Implemented fixes

1. **Standard handler callback semantics hardened (S2/F1)**
  - `esp32common/espnow_common_utils/espnow_standard_handlers.cpp`
  - `on_connection` now fires only on an explicit false→true transition when `connection_flag` is provided.
  - If `connection_flag == nullptr`, handlers no longer synthesize connection transitions.

2. **Single RX handshake owner enforced (S1/F2)**
  - `espnowreciever_2/src/espnow/espnow_tasks.cpp`
  - Removed redundant connection-trigger path from standard-handler `on_connection` callback.
  - Receiver now uses worker/connection-handler path as the authoritative owner for peer registration flow.

3. **Real deferred peer-registration latch added (S3/F3)**
  - `espnowreciever_2/src/espnow/rx_connection_handler.h`
  - `espnowreciever_2/src/espnow/rx_connection_handler.cpp`
  - Added deferred-latch state (`MAC + timestamp + TTL`) and `flush_deferred_peer_registered()`.
  - Added deduplication gate to prevent repeated `PEER_REGISTERED` posts while `CONNECTING`.

4. **Disconnect cleanup source fixed (S4/F4)**
  - `espnowreciever_2/src/espnow/rx_connection_handler.cpp`
  - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_connection_handler.cpp`
  - Disconnect callbacks now use handler-cached peer MAC, not manager-cleared MAC.

5. **Connection-lost reset path unified (S4/F5)**
  - `espnowreciever_2/src/espnow/rx_connection_handler.cpp`
  - `ReceiverConnectionHandler::on_connection_lost()` is now explicitly invoked on `CONNECTED -> IDLE`.
  - Local retry/init/deferred flags are reset in one place.

6. **Symmetric TX hardening added (cross-device robustness)**
  - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_connection_handler.h`
  - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_connection_handler.cpp`
  - Added deduplication + deferred-latch handling for `on_peer_registered()` on transmitter side too.

7. **Old/redundant code removed**
  - Removed unused RX initialization-flag plumbing from:
    - `espnowreciever_2/src/espnow/espnow_tasks.cpp`
     - `g_initialization_sent_ptr`
     - `reset_initialization_flag()`
     - dead `initialization_sent` tracking
  - Standard-handler config structs are now explicitly reset (`= {}`) before route wiring to avoid stale/uninitialized fields.

### ✅ Validation

- Receiver build passed after changes:
  - Environment: `receiver_tft`
  - Output binary produced successfully

### ⚠ Remaining to complete “once and for all”

1. Execute reboot-matrix runtime tests (RX-first, TX-first, TX reboot while RX live, etc.).
2. Optionally add `connection_epoch` tagging (S6) for final stale-session immunity and auditability.
3. Build/flash transmitter with runtime verification on hardware logs.
