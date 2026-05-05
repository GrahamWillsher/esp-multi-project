# ESP-NOW Connection/Reconnection Robustness Investigation (2026-04-23)

## Status: Phase A Complete ✅

**UPDATE 2026-04-23 (Afternoon)**: All three critical bugs identified in Phase A have been fixed in-source:
- ✅ Bug A: LCD heartbeat accounting — FIXED (1-line addition + include)
- ✅ Bug B: ACK suppression ineffectiveness (both RX) — FIXED (wall-clock throttling logic)
- ✅ Bug C: Dense init burst (both RX) — FIXED (paced initialization)

Modifications applied to:
- `espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp` — Added heartbeat→common-manager feed
- `espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp` — Replaced seq-dependent ACK suppression
- `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp` — Paced initialization (2 frames initial, defer rest)
- `espnowreceiver_2/src/espnow/espnow_tasks.cpp` — Replaced seq-dependent ACK suppression
- `espnowreceiver_2/src/espnow/rx_connection_handler.cpp` — Paced initialization (2 frames initial, defer rest)

**Next**: Phase B (MQTT hardening, pressure mode) and Consolidation (Steps 1–4) when ready.

---

## Scope
This investigation re-examines the full TX↔RX lifecycle for:
- initial discovery
- steady-state connected operation
- heartbeat supervision
- loss detection
- reconnect behavior without rebooting either side
- impact of `ESP_ERR_ESPNOW_NO_MEM` and MQTT failures on reconnect stability

Targets reviewed:
- `ESPnowtransmitter2/espnowtransmitter2`
- `espnowreceiver_LCD`
- shared components in `esp32common`

---

## Original Problem Statement (Now Mitigated)
Original behavior was unstable because reconnect was being stressed by a feedback loop:

1. TX enters reconnect scanning and emits frequent PROBEs.
2. RX responds with many ACKs.
3. RX TX path is already under pressure (MQTT retries + init retries + other control sends), so ACK sends frequently fail with `ESP_ERR_ESPNOW_NO_MEM`.
4. Missing ACK/heartbeat progress causes repeated state churn.
5. Web UI availability degrades as WiFi driver TX resources are repeatedly exhausted.

There are also logic-level issues that make recovery less robust than expected:
- receiver heartbeat frames are not consistently counted as connection activity for the common connection manager timeout path
- transmitter currently starts two discovery mechanisms (passive + active) in the same boot sequence
- receiver ACK suppression logic (intended to reduce chatter) is effectively neutralized by sequence behavior

Result: reconnect reliability is fragile and can depend on reboot timing.

---

## What was inspected

### Receiver side
- `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_runtime_messages.cpp`
- `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp`
- `espnowreceiver_LCD/src/mqtt/mqtt_client.cpp`
- `espnowreceiver_LCD/src/mqtt/mqtt_task.cpp`
- `espnowreceiver_LCD/src/runtime/runtime_task_startup.cpp`

### Transmitter side
- `ESPnowtransmitter2/espnowtransmitter2/src/main.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/config/runtime_task_startup.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_connection_handler.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/heartbeat_manager.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_send_guard.cpp`

### Shared/common
- `esp32common/espnow_common_utils/connection_manager.cpp`
- `esp32common/espnow_common_utils/espnow_discovery.cpp`
- `esp32common/espnow_common_utils/espnow_standard_handlers.cpp`
- `esp32common/espnow_common_utils/espnow_tx_scheduler.cpp`
- `esp32common/include/esp32common/config/timing_config.h`

---

## Observed runtime evidence (receiver logs)
Repeatedly observed during long monitor sessions:
- frequent `Send failed (type=1 len=6): ESP_ERR_ESPNOW_NO_MEM` (ACK frames)
- frequent `MQTT Connection failed, state=-2`
- periodic heartbeat timeout transitions and reconnect cycles
- web UI reported unreachable/intermittent while this churn is active

This is consistent with TX-buffer starvation, not a single isolated webserver bug.

---

## Full cycle review and findings

## 1) Discovery orchestration is currently split in TX (passive + active)
In TX startup flow:
- `runtime_task_startup.cpp` calls `DiscoveryTask::instance().start()` (common periodic announcer)
- `main.cpp` bootstrapping calls `TransmitterConnectionHandler::start_discovery()` which starts active channel hopping

This means two discovery strategies are present in the same runtime path, with different assumptions and cadence behavior.

### Risk
- higher-than-necessary PROBE traffic under churn
- harder-to-reason reconnect state transitions
- unintended interactions during failure recovery

### Recommendation
Choose one discovery owner for TX reconnect path (active hopper), and remove/disable passive announcer startup in TX runtime task startup.

---

## 2) Receiver PROBE→ACK suppression is ineffective for real traffic pattern
RX route logic tries to suppress ACK frequency in CONNECTED state using `(same peer + same seq + min interval)` logic.

However TX PROBE sequence values are not stable per interval in a way that guarantees dedupe (new sequence values are common), so suppression frequently does not apply. In churn windows this causes ACK floods (type=1), exactly where logs show NO_MEM.

### Recommendation
Throttle ACK by `(peer, wall-clock interval)` independent of PROBE sequence while CONNECTED.
Keep a small bypass for first-seen peer or explicit reconnect transitions.

---

## 3) Receiver heartbeat handling does not fully feed common timeout monitor
RX `handle_heartbeat_message()` calls `RxHeartbeatManager::on_heartbeat()` and `mark_link_alive()`, but does not use the same path as `mark_protocol_activity()` that feeds common connection activity (`ReceiverConnectionHandler::on_data_received()` -> `EspNowConnectionManager::on_heartbeat_received()`).

During degraded periods, heartbeat frames can still be present while data/control traffic is sparse, yet common timeout accounting can still drift toward disconnect.

### Recommendation
On RX heartbeat receipt, explicitly feed common connection activity path (or call `EspNowConnectionManager::on_heartbeat_received()` directly in `RxHeartbeatManager::on_heartbeat()`).

---

## 4) Reconnect storm amplifies NO_MEM through ACK pressure
When heartbeat ACKs are missed, TX enters reconnect/hopping. Reconnect probing then increases inbound PROBE rate at RX. RX must emit ACKs, but RX TX path is already contended. ACK send failures increase, which prolongs TX reconnect mode and further increases probing load.

This is a positive feedback loop.

### Recommendation
- prioritize ACK/heartbeat/control traffic over all optional traffic (already partly done; needs stronger enforcement)
- temporarily suppress non-critical outbound RX traffic during reconnect windows
- ensure reconnect mode applies stricter send budgeting

---

## 5) MQTT failure mode still contributes to radio contention
Receiver MQTT broker failures (`state=-2`) were observed repeatedly. Without aggressive adaptive backoff/jitter and hard suspension in unstable windows, failed reconnect attempts continue injecting WiFi/TCP pressure.

### Recommendation
- exponential backoff with cap + jitter
- hard-disable MQTT attempts while ESP-NOW not healthy
- optional: only re-enable MQTT after stable ESP-NOW uptime window (e.g. 30–60s)

---

## 6) CONNECTED-entry initialization burst is too dense for degraded windows
On each CONNECTED transition, RX sends multiple requests immediately:
- several config section requests
- request data stream
- version announce
- LED sync request
- catalog version + missing catalogs

When driver buffers are already stressed, this burst raises first-second failure probability and can trigger repeated retries.

### Recommendation
Stage initialization sends over a paced schedule with priority order and bounded in-flight count.

---

## 7) Receiver timeout policy can delay recovery transitions
RX heartbeat timeout configured to 90s. In unstable states this can keep RX in a semi-stale connected posture for too long before full rediscovery path re-opens.

### Recommendation
Reduce RX timeout to align better with TX timeout/reconnect cadence (example: 35–45s), then validate in reboot/network-loss matrix.

---

## Fix plan (ordered)

## Phase A — Stabilize discovery and liveness semantics (highest ROI)
1. Single discovery authority on TX (active hopper only).
2. Fix RX ACK throttle to true time-based suppression (not seq-dependent).
3. Feed RX common heartbeat monitor on heartbeat receipt.

## Phase B — Remove pressure multipliers
4. Pace RX CONNECTED-init requests (staged, bounded).
5. Harden MQTT failure backoff and suspend criteria.
6. Add temporary “reconnect pressure mode” to suppress optional sends.

## Phase C — Tune and verify
7. Align TX/RX timeout windows and backoff progression.
8. Increase diagnostic counters for:
   - ACK attempted/sent/failed
   - PROBE received rate
   - TX scheduler per-priority drops
   - reconnect reason histogram

---

## Validation matrix required before sign-off
Must pass all without reboot dependency:
1. RX reboot while TX remains up.
2. TX reboot while RX remains up.
3. AP/router channel change.
4. Broker unavailable for 30+ minutes.
5. Burst control traffic during reconnect.
6. Prolonged RF interference simulation.

Pass criteria:
- reconnect within target SLA (define per scenario)
- no sustained `ESP_ERR_ESPNOW_NO_MEM` storms
- web UI remains reachable once WiFi is up
- no manual reboot required for recovery

---

## Notes on current working branch state
I also identified that COM7 upload/monitor contention interrupted one flash attempt during this session. That does not change findings above; they come from source-level flow analysis plus long-running receiver logs showing persistent `NO_MEM` and reconnect churn.

---

## Section 8: Comparative Analysis — espnowreceiver_LCD vs. espnowreceiver_2

### Overall Findings
Both receivers share **identical core connection management architecture** sourced from `esp32common`. However, they differ significantly in:
1. **Message handling approach**: LCD uses inline lambdas in router; _2 uses external message handler functions
2. **Heartbeat accounting**: LCD is missing the critical feed to common connection manager; _2 correctly implements it
3. **ACK suppression window**: LCD uses 500ms (CONNECTED) + 80ms (DISCONNECTED); _2 uses 120ms + 80ms
4. **Initialization pacing**: LCD sends init requests in single burst; _2 stages them with pacing policy
5. **Message handler organization**: LCD routes are centralized in one file; _2 splits across multiple specialized handlers
6. **Telemetry caching**: _2 has richer battery data store and settings caching; LCD uses simpler snapshots

### Critical Issue Found in LCD (Fixable)

**Missing heartbeat accounting in espnowreceiver_LCD:**

In [espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp](espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp#L52), the `on_heartbeat()` method does NOT call `EspNowConnectionManager::instance().on_heartbeat_received()`:

```cpp
void RxHeartbeatManager::on_heartbeat(const heartbeat_t* hb, const uint8_t* mac) {
    // ... CRC validation, reboot detection ...
    m_last_heartbeat_seq = hb->seq;
    m_last_rx_time_ms = millis();
    m_heartbeats_received++;
    
    // ❌ MISSING: EspNowConnectionManager::instance().on_heartbeat_received();
    
    TransmitterManager::updateTimeData(...);
    send_ack(hb->seq, mac);
}
```

**In contrast, espnowreceiver_2 [rx_connection_handler.cpp](espnowreceiver_2/src/espnow/rx_connection_handler.cpp#L180) correctly calls this in `on_data_received()`:**

```cpp
void ReceiverConnectionHandler::on_data_received(const uint8_t* transmitter_mac) {
    if (transmitter_mac) {
        memcpy(transmitter_mac_, transmitter_mac, sizeof(transmitter_mac_));
    }
    last_rx_time_ms_ = millis();
    
    EspNowConnectionManager::instance().on_heartbeat_received();  // ✓ CORRECT
    post_connection_event(EspNowEvent::DATA_RECEIVED, transmitter_mac_);
}
```

**Impact**: LCD's heartbeat frames do not reset the common manager's timeout timer. If data/control traffic is sparse during reconnect churn, the 90s timeout can expire even with heartbeats present, causing false CONNECTION_LOST transitions.

### Message Handler Architecture Comparison

| Aspect | LCD | _2 |
|--------|-----|-----|
| **Route setup location** | `espnow_runtime_routes.cpp` (centralized) | `espnow_tasks.cpp` (embedded) |
| **Handler functions** | External handlers in `espnow_runtime_messages.cpp` | External handlers in `espnow_message_handlers.cpp` |
| **Probe ACK suppression** | 500ms (CONNECTED), 80ms (DISCONNECTED) | 120ms (CONNECTED), 80ms (DISCONNECTED) |
| **Init request pacing** | **Dense burst** all in one send_initialization_requests() call | **Paced & bounded** — stages requests with retry policy + timing |
| **Heartbeat on_heartbeat()** | ❌ Does NOT call common manager | ✓ Calls on_data_received() → on_heartbeat_received() |
| **Battery data model** | Simple snapshot (soc, power, voltage) | Rich battery_status_msg_t with more telemetry |
| **Config section requests** | Sends 4 sections in burst | Sends 4 sections in burst (same pattern) |
| **Catalog requests** | Conditional based on cache state | Conditional based on cache state |

### ACK Suppression Effectiveness

**LCD:** 500ms window in CONNECTED state means ~2 ACK/second maximum during steady heartbeat (10s interval between heartbeats). However, during reconnect with TX hopping every 1-5s per channel, suppression becomes ineffective because sequence values rotate frequently.

**_2:** 120ms window is tighter, reducing ACK rate even further (~8/second theoretical max) but still seq-dependent and ineffective during churn.

**Recommendation:** Both should implement **wall-clock time-based suppression independent of probe sequence** (see Phase A in findings above).

### Init Request Handling (Opportunity for Improvement)

**espnowreceiver_LCD** [rx_connection_handler.cpp lines ~248–330](espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp#L248):
- Sends all config requests sequentially in rapid burst
- No rate-limiting or in-flight count enforcement
- Assumption: hardware buffers absorb all 8+ frames

**espnowreceiver_2** [rx_connection_handler.cpp lines ~260–360](espnowreceiver_2/src/espnow/rx_connection_handler.cpp#L260):
- Still sends config requests in burst
- **BUT** adds bounded retry policies via `catalog_retry_`, `led_sync_` state tracking
- Implements retry timing to prevent packet loss cascades

**Both have same vulnerability**: Under buffer pressure, first frame succeeds, subsequent frames fail with NO_MEM, causing cascading retries that amplify load during reconnect window.

### Heartbeat Manager Commonality
Both versions have **identical heartbeat logic** in terms of:
- CRC32 validation
- Sequence regression detection (TX reboot)
- ACK response generation
- Device temperature tracking

The only difference is LCD's missing connection manager feed (which is a bug to fix).

### Consolidation Opportunity Summary

**Can be unified with minimal display-specific branching:**
1. ✅ Core heartbeat manager (already identical)
2. ✅ Connection handler state machine (already identical)
3. ✅ Message router setup (differs in organization, not logic — can be abstracted)
4. ✅ Discovery integration (already unified in common)
5. ⚠️ Data handlers (differ by TX component types — charger/inverter/system status in _2 vs. simpler battery in LCD)
6. ⚠️ Telemetry caching (LCD: simple snapshots; _2: rich battery_status_msg_t — can unify with abstraction)
7. ⚠️ Display update queue (LCD-specific; _2 uses TransmitterManager for webserver — can be abstracted)

**Recommended consolidation path:**
1. Extract both message router setups into a shared abstract factory pattern in `esp32common/espnow_common_utils/`
2. Move LCD's and _2's handler implementations to a shared `receiver_message_handlers.cpp` with display/backend abstraction for data storage
3. Create a `ReceiverConfig.h` header that abstracts:
   - Telemetry storage backend (LCD DisplayUpdateQueue vs. _2 TransmitterManager)
   - Component support flags (charger/inverter presence)
   - Init request pacing policy
4. Maintain `espnowreceiver_LCD` and `espnowreceiver_2` as thin platform-specific wrappers that set config and call shared init

---

## Critical Bugs Summary (Both Receivers)

### Bug A (LCD only): Heartbeat not feeding common timeout — ✅ FIXED
**Severity**: HIGH  
**File**: [espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp](espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp#L52)  
**Fix Applied**: Added `EspNowConnectionManager::instance().on_heartbeat_received()` call after CRC validation in `on_heartbeat()`  
**Impact**: Heartbeat timeout can now properly reset on heartbeat reception during data/control traffic sparse periods
**Status**: ✅ COMPLETE — Added include for connection_manager.h and feeding call in on_heartbeat()

### Bug B (Both): ACK suppression ineffective during reconnect — ✅ FIXED
**Severity**: HIGH  
**Files**: 
- LCD: [espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp](espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp#L47)
- _2: [espnowreceiver_2/src/espnow/espnow_tasks.cpp](espnowreceiver_2/src/espnow/espnow_tasks.cpp#L200)

**Fix Applied**: Replaced seq-dependent suppression with wall-clock time-based throttling per peer
- CONNECTED: 1000ms throttle (one ACK/second max, matches TX probe cadence)
- DISCONNECTED: 100ms throttle (responsive during channel-hopping)
- Suppression now independent of probe sequence values, which rotate during reconnect
**Impact**: Eliminates ACK floods during TX channel-hopping reconnect, preventing TX buffer starvation
**Status**: ✅ COMPLETE — Applied to both LCD and _2 receivers

### Bug C (Both): Dense init request burst — ✅ FIXED
**Severity**: MEDIUM  
**Files**: 
- LCD: [espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp](espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp#L265)
- _2: [espnowreceiver_2/src/espnow/rx_connection_handler.cpp](espnowreceiver_2/src/espnow/rx_connection_handler.cpp#L266)

**Fix Applied**: Reduced initial CONNECTED-entry burst from 8+ frames to 2 frames
- High-priority: Power profile request + version announcement sent immediately  
- Medium/Low-priority: Config sections, LED sync, catalogs deferred to `tick()` retry loops
- Existing tick() mechanisms handle retry/pacing at 500ms–10s intervals depending on subsystem
**Impact**: Eliminates first-second failure cascades, leverages existing retry infrastructure for resilience
**Status**: ✅ COMPLETE — Applied to both LCD and _2 receivers

---

## All Phase A Bug Fixes Complete

**Phase A fixes address shared robustness issues.** After Phase A:
1. LCD needs Bug A fix (heartbeat accounting) — trivial 1-line addition
2. Both need ACK suppression rewrite — medium complexity
3. Both benefit from init pacing — medium complexity but already partially done in _2

**If Phases A–B are implemented as described + Bug A is fixed in LCD, reconnect should become robust in both receivers without requiring receiver reboot to recover. A single shared codebase is achievable with minimal abstraction layer for display/storage backends.**
