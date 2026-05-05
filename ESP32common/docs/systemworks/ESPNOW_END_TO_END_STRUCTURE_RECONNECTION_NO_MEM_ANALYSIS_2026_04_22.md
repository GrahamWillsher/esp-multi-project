# ESP-NOW End-to-End Structure, Reconnection, and `ESP_ERR_ESPNOW_NO_MEM` Analysis
**Date:** 2026-04-22  
**Scope:** `ESPnowtransmitter2` + `espnowreceiver_2` + `espnowreceiver_LCD` + shared `esp32common` ESP-NOW stack  
**Focus requested:** full ESPNOW structure, reconnection process, state machines, receiver `NO_MEM` failures, robustness improvements

---

## Executive Summary

The current architecture is strong in intent and modularity (shared connection primitives, dedicated runtime handlers, and per-role state machines), but robustness is currently limited by four structural issues:

1. **Receiver-side outbound ESP-NOW congestion under pressure** (`ESP_ERR_ESPNOW_NO_MEM`) can still collapse control-plane messaging (ACK / heartbeat-ACK / config requests), even when inbound link traffic is healthy.
2. **Two transmitter discovery paradigms can run concurrently** (common periodic discovery + active channel-hopping), producing avoidable contention around broadcast peer/channel ownership.
3. **State ownership is split across multiple layers** (`EspNowConnectionManager`, role-specific state machine, app/system state), and transitions are not always explicitly synchronized.
4. **Reconnect behavior is asymmetrical**: transmitter uses deferred backoff-aware discovery, receiver is effectively immediate reconnect; this can produce churn under transient memory/radio stress.

If you implement the recommendations in the priority order at the end of this document, the system should become materially more resilient to high-load reconnect storms and memory pressure while preserving your current architecture.

---

## 1) Codebase Structure: ESPNOW Architecture (TX + RX + Shared)

## 1.1 Shared Core (`esp32common`)

### Core connection/state infrastructure
- `espnow_common_utils/connection_manager.cpp`
- `espnow_common_utils/connection_event_processor.cpp`
- `espnow_common_utils/channel_manager.cpp`
- `espnow_phase0/reconnection_backoff.cpp`
- `espnow_phase0/espnow_heartbeat_monitor.cpp`

### Shared transport helpers
- `espnow_common_utils/espnow_tx_scheduler.cpp`
- `espnow_common_utils/espnow_standard_handlers.cpp`
- `espnow_common_utils/espnow_send_utils.cpp`
- `espnow_common_utils/espnow_message_router.cpp`

### Key architecture properties
- Shared **3-state connection machine** (`IDLE → CONNECTING → CONNECTED`) via queued events.
- Shared **channel lock/unlock manager** with NVS persistence.
- Shared **scheduler-based outbound TX path** with NO_MEM retry logic.
- Shared **standard PROBE/ACK handlers** used by both roles.

---

## 1.2 Transmitter (`ESPnowtransmitter2`)

Primary orchestration:
- `src/main.cpp`

Connection/discovery/state:
- `src/espnow/tx_connection_handler.cpp`
- `src/espnow/tx_state_machine.cpp`
- `src/espnow/discovery_task.cpp`

Message ingress/control plane:
- `src/espnow/message_handler.cpp`
- `src/espnow/message_routes.cpp`

Data/heartbeat egress:
- `src/espnow/data_sender.cpp`
- `src/espnow/transmission_task.cpp`
- `src/espnow/heartbeat_manager.cpp`
- `src/espnow/tx_send_guard.cpp`

Observations:
- Uses shared connection manager + own `TxStateMachine` (device-state vocabulary).
- Uses active channel-hopping discovery task for fast lock.
- Uses guarded sender (`TxSendGuard`) to protect against channel mismatch and repeated send failure.

---

## 1.3 Receiver variants

### `espnowreceiver_2`
- Bootstrap: `src/main.cpp`
- Runtime startup: `src/config/runtime_task_startup.cpp`
- Worker: `src/espnow/espnow_tasks.cpp`
- Connection handling: `src/espnow/rx_connection_handler.cpp`
- Heartbeat: `src/espnow/rx_heartbeat_manager.cpp`
- Device state: `src/espnow/rx_state_machine.cpp`

### `espnowreceiver_LCD`
- Runtime core: `src/espnow/espnow_runtime.cpp`
- Ingress parse/validate/dispatch split:
  - `src/espnow/espnow_runtime_ingress.cpp`
  - `src/espnow/espnow_runtime_messages.cpp`
  - `src/espnow/espnow_runtime_routes.cpp`
- Connection handling: `src/espnow/rx_connection_handler.cpp`
- Heartbeat: `src/espnow/rx_heartbeat_manager.cpp`
- Runtime startup: `src/runtime/runtime_task_startup.cpp`

Observations:
- LCD variant is cleaner in ingress layering and route isolation.
- `_2` variant still carries a larger monolithic worker with broader concerns in one task.

---

## 2) State Machines and Ownership

## 2.1 Shared Connection Manager (authoritative link state)

`EspNowConnectionManager` owns:
- Event queue (`CONNECTION_START`, `PEER_FOUND`, `PEER_REGISTERED`, `CONNECTION_LOST`, etc.)
- State transitions: `IDLE`, `CONNECTING`, `CONNECTED`
- Optional heartbeat timeout ownership
- Optional auto-reconnect (`IDLE` transition posts `CONNECTION_START`)

Strengths:
- Deterministic, central transition logic.
- Shared by TX and RX.

Risks:
- Event queue depth fixed at 10; transient event bursts can be dropped/blocked.
- Some call sites assume event acceptance but do not always react to queue-post failure.

---

## 2.2 Transmitter `TxStateMachine`

Device state vocabulary (`DISCONNECTED`, `DISCOVERING`, `CONNECTED`, `ACTIVE`, `STALE`, `RECONNECTING`, `FAILED`) is maintained independently from connection-manager 3-state.

Strengths:
- Explicit transition validation table.
- Backoff exponent for recovery workflows.

Risk:
- Split-brain possibility if `TxStateMachine` and `EspNowConnectionManager` are not moved in lockstep under exceptional paths.

---

## 2.3 Receiver `RxStateMachine`

Receiver tracks message-processing validity and data activity freshness (`ACTIVE` vs `STALE`) in a separate machine.

Strengths:
- Explicit stale detection and grace-window support.
- Helpful for UI/system-state degradation signaling.

Risk:
- Multi-layer state ownership (`ConnectionManager` + `RxStateMachine` + app/system state manager) raises consistency burden.

---

## 3) Reconnection Process (Observed Design)

## 3.1 Transmitter reconnect path

1. Link loss observed (typically heartbeat ACK inactivity or explicit connection loss event).
2. Shared manager transitions to `IDLE`, auto-posts `CONNECTION_START`.
3. Handler enters `CONNECTING` branch.
4. Backoff gate checked (`should_attempt_reconnect()`); may defer discovery start.
5. Discovery launches (active hopping task), scans channels, receives ACK, registers peer.
6. Posts `PEER_FOUND` / `PEER_REGISTERED` to shared manager.
7. On `CONNECTED`, channel is locked and services resume.

Good:
- Backoff-aware reconnect avoids tight retry storm.

Fragile:
- Discovery owner ambiguity (see §5.2) can interfere with reliable scan/ACK exchange.

---

## 3.2 Receiver reconnect path

1. Shared timeout ownership may post `CONNECTION_LOST` when no activity/heartbeat window exceeded.
2. Shared manager transitions `CONNECTED → IDLE`, auto-posts `CONNECTION_START`.
3. Handler resumes discovery and unlocks channel as needed.
4. New PROBE/peer activity drives `PEER_FOUND` / `PEER_REGISTERED`.
5. On `CONNECTED`, receiver sends initialization requests (`REQUEST_DATA`, config requests, version, LED sync, catalog queries).

Good:
- Fast regain when transmitter is active.

Fragile:
- Initialization burst can heavily load outbound queue exactly at reconnect time, when memory/radio are least stable.

**Revised init sequence with `INFRA_STATE` gating (§6.2.1):**
1. ESP-NOW peer registered → TX immediately sends `INFRA_STATE` as first message (before any data).
2. RX receives `INFRA_STATE`, populates `TxInfraShadow`.
3. RX immediately sends only locally-sourced requests (`REQUEST_DATA`, `REQUEST_CELL_DATA`) — these are always valid.
4. RX checks shadow flags before sending any infrastructure-dependent requests (config, MQTT, version, catalog).
5. If shadow is stale or never received, RX sends `INFRA_STATE_REQUEST` and holds infrastructure requests until reply arrives.
6. Result: init burst is both ordered and gated — no futile requests, no unnecessary queue pressure.

---

## 4) Receiver `ESP_ERR_ESPNOW_NO_MEM`: Current Mechanism

## 4.1 What `NO_MEM` means in this codebase context

At call sites, `ESP_ERR_ESPNOW_NO_MEM` can represent two practical conditions:

1. **Driver-side immediate send allocation/resource failure** (`esp_now_send` path), and/or
2. **Scheduler queue saturation** (`EspnowTxScheduler::send` returns NO_MEM when enqueue fails).

So operationally it means: **control/data packet could not enter the transmit path now**.

---

## 4.2 Where NO_MEM pressure is amplified

Receiver outbound send producers include:
- PROBE ACK responses (`espnow_standard_handlers.cpp`)
- Heartbeat ACKs (`rx_heartbeat_manager.cpp`)
- Initialization requests on connect (`rx_connection_handler.cpp`)
- Settings re-fetch requests (`espnow_settings_sync.cpp`)
- UI-initiated control messages (`espnow_send.cpp`)

The reconnect window is the worst-case burst moment:
- peer found
- ACK / heartbeat activity starts
- receiver immediately pushes multiple init/control requests
- scheduler queue and/or driver path can saturate

---

## 4.3 Variant comparison relevant to NO_MEM

### `espnowreceiver_2`
- TX scheduler queue depth configured at 24 (`runtime_task_startup.cpp`)
- `no_mem_retry_attempts = 6`

### `espnowreceiver_LCD`
- TX scheduler queue depth configured at 4 (`runtime_task_startup.cpp`)
- `no_mem_retry_attempts = 3`
- MQTT + memory sampler are intentionally disabled in that reconnect-stability profile

Interpretation:
- LCD profile reduced scheduler memory footprint, but much shallower queue means less burst tolerance.
- `_2` has higher burst tolerance but larger memory footprint.
- Neither variant currently differentiates high-priority control traffic from best-effort traffic inside one shared TX queue.

---

## 5) High-Impact Robustness Findings

## 5.1 Reconnect initialization burst is unprioritized

All reconnect-time sends compete in a single FIFO scheduler queue.
Control-plane packets (ACK, heartbeat-ACK, REQUEST_DATA) can be delayed or dropped behind lower-priority packets.

**Impact:** reconnects can appear to “flap” even though RF/channel lock succeeded.

---

## 5.2 Transmitter discovery ownership overlap

Transmitter starts active channel-hopping discovery, while common discovery machinery can also be active.
Both paths can send probe traffic and manipulate broadcast peer/channel context.

**Impact:** avoidable contention and nondeterminism during scan/relock windows.

---

## 5.3 State-machine layering lacks explicit reconciliation contract

The system uses:
- shared connection state (3-state)
- role-specific device state (`TxStateMachine` / `RxStateMachine`)
- app/system state manager(s)

There is no single explicit reconciliation/assertion layer ensuring these remain coherent under every failure edge.

**Impact:** difficult post-mortem diagnosis and occasional contradictory state combinations.

---

## 5.4 Ethernet coupling inside transmitter heartbeat send path

Transmitter heartbeat sending was previously gated by `EthernetManager::is_fully_ready()`.
When Ethernet is down but ESP-NOW link is valid, halting heartbeats causes RX to timeout and declare `CONNECTION_LOST`
— triggering a full ESP-NOW reconnect storm every time Ethernet bounces.

**Impact:** unnecessary ESPNOW reconnect churn caused by a non-radio dependency halting the keepalive signal.

**Resolution (addressed in §6.2 and §6.2.1):** The heartbeat now serves two explicit, separate purposes:
- **ESP-NOW keepalive pulse** — always fires when a peer is registered, regardless of Ethernet state. This keeps the radio link alive.
- **Infrastructure state carrier** — the heartbeat payload always contains live `INFRA_STATE` flags so RX knows exactly what TX can currently provide.

The `EspnowDualGate` gates only infrastructure-dependent *responses* (config, MQTT proxy, cloud queries), never the keepalive heartbeat itself. RX gating decisions are driven by the *content* of heartbeats, not by their presence or absence.

---

## 5.5 Event queue backpressure visibility is limited

Connection event queue depth is small and event posting failures are not uniformly surfaced as first-class counters/telemetry.

**Impact:** hard to distinguish logic bugs from event-drop pressure in the field.

---

## 5b) Recommendations (Prioritized)

## P0 — Protect control-plane traffic from NO_MEM collapse (must-do)

1. **Introduce TX priority classes** in `EspnowTxScheduler`:
   - Class A (critical): ACK, heartbeat ACK, REQUEST_DATA, probe ACK
   - Class B (important): config/version sync
   - Class C (best-effort): telemetry/event summaries
2. Use either:
   - separate queues per class, or
   - single queue with priority insertion policy.
3. Reserve headroom for Class A under pressure (cannot be starved by Class C).

---

## P0 — Add adaptive send shedding on receiver

When internal memory and/or queue pressure is high:
- drop/defer best-effort traffic first,
- keep ACK/heartbeat/REQUEST_DATA alive.

Implement a simple “pressure state” from:
- `EspnowTxScheduler` stats (`enqueue_drop`, `no_mem_retry`, queue depth)
- heap/internal-block telemetry.

---

## P1 — Enforce single discovery owner on transmitter

Define one authoritative discovery mode at a time:
- active hopping **or** common periodic discovery.

Recommendation:
- keep active hopping as owner during reconnect/discovery windows,
- suspend/stop common periodic discovery while active hopping is running,
- resume passive periodic only after stable connected state if still needed.

---

## P1 — Add reconnect burst shaping

After receiver enters `CONNECTED`:
- send only minimal bootstrap set immediately (ACK path already active, then `REQUEST_DATA`),
- stagger non-critical init messages with jittered intervals.

Suggested phased order:
1. `REQUEST_DATA` (critical)
2. heartbeat ACK normal flow (already reactive)
3. metadata/config section requests
4. catalog/interface requests
5. LED sync retries

This reduces initial peak queue occupancy.

---

## P1 — Align receiver variants with one explicit profile policy

Instead of ad-hoc `_2` vs `_LCD` queue/retry values, define profiles:
- `MEMORY_TIGHT_PROFILE`
- `BALANCED_PROFILE`
- `THROUGHPUT_PROFILE`

Each profile should set:
- scheduler queue depth
- NO_MEM retry count/backoff
- non-critical task enablement
- max init burst rate

This makes behavior intentional and testable.

---

## P2 — Add state coherence assertions/telemetry

Create a lightweight periodic “state coherence checker”:
- expected mapping between shared connection state and role state,
- report mismatches as structured counters/events,
- expose via diagnostics endpoint/log snapshot.

This will dramatically speed root-cause analysis during field logs.

---

## P2 — Decouple transmitter heartbeat from Ethernet-ready gate

**This item is superseded by the full design in §6.2 and §6.2.1.**

The correct implementation:
- Heartbeat **always** fires when ESP-NOW peer is registered (keepalive is independent of Ethernet).
- Every heartbeat payload carries embedded `INFRA_STATE` flags.
- `EspnowDualGate` gates only infrastructure-dependent *responses*, not the heartbeat.
- RX uses `TxInfraShadow` (populated by every heartbeat) to gate what it requests.

This resolves both the false-reconnect problem and the futile-request problem in a single coherent design.

---

## P2 — Improve event queue observability and resilience

1. Increase connection event queue depth modestly (e.g., 10 → 20) if memory permits.
2. Add explicit counters for failed event posts by event type.
3. Emit periodic queue occupancy watermark logs (rate-limited).

---

## 7) Suggested Validation Plan

## 7.1 Reconnect stress
- Force repeated TX/RX power-cycle permutations.
- Include cases where receiver boots late and transmitter boots late.
- Verify reconnect convergence time and no permanent stuck state.

## 7.2 NO_MEM soak test
- Simulate high UI/MQTT traffic while inducing reconnects.
- Confirm Class A control-plane packets remain successful under pressure.
- Track `no_mem_retry`, enqueue drops, and reconnect flap frequency.

## 7.3 State consistency audit
- Record shared + role + app state snapshot every 5s during stress.
- Verify no illegal or contradictory combinations persist.

---

## 8) Bottom Line

The ESPNOW architecture is close to robust production shape, but the current weak point is **transport prioritization under constrained memory/radio conditions during reconnect bursts**.

If you implement:
1. control-plane traffic prioritization,
2. single discovery ownership,
3. reconnect burst shaping,
4. better state/queue observability,

you should eliminate most reconnection flapping and materially reduce/contain receiver `ESP_ERR_ESPNOW_NO_MEM` incidents without needing a full architectural rewrite.

---

## 6) Advanced Architectural Improvements (Revised)

### 6.1 Separate Message Queues by Priority with Per-Subtype Send Cadence

**Current State:**
- All ESP-NOW message types are routed through a single 3-queue system (message, discovery, rx).
- No priority dispatch; queue depths and pacing behaviour are effectively uniform.
- High-rate telemetry can crowd out control-plane traffic during reconnect bursts.

**The Timing Policy Clarification:**

The timing policy here is about **how often each message subtype is allowed to be sent** (cadence/rate), not how long it is kept in a queue.

So each subtype gets:
- a **target send interval** (`target_period_ms`) for periodic traffic,
- a **minimum inter-send gap** (`min_gap_ms`) for event-driven traffic,
- a **retry interval + retry count** for request/response reliability,
- optional **burst caps** to prevent radio saturation.

**Proposed Enhancement:**

Use the same priority queue tiers (P0..P3), but add a per-subtype transmit policy that gates when a message is eligible to send. If the message is not yet due, it is deferred/requeued by policy, not discarded for age.

---

**Per-Message-Subtype Send Frequency Policy (Outbound)**

| Message Type | Priority | Send Policy | Target Period | Min Gap | Retry Policy |
|---|---|---|---|---|---|
| `MSG_TYPE_HEARTBEAT` | CONTROL (P0) | Periodic always-on | **1000 ms** | 800 ms | 0 retries |
| `MSG_TYPE_INFRA_STATE` | CONTROL (P0) | Event-driven on flag change + piggyback in heartbeat | N/A | **250 ms** | 2 retries @ 100 ms |
| `MSG_TYPE_INFRA_STATE_REQUEST` | CONTROL (P0) | On-demand request | N/A | 500 ms | 3 retries @ 250 ms |
| `MSG_TYPE_ACK` | CONTROL (P0) | Immediate response | N/A | 10 ms | 0 retries |
| `MSG_TYPE_PROBE_ACK` | CONTROL (P0) | Immediate response | N/A | 20 ms | 1 retry @ 50 ms |
| `MSG_TYPE_HEARTBEAT_ACK` | CONTROL (P0) | Immediate response | N/A | 20 ms | 1 retry @ 100 ms |
| `MSG_TYPE_PROBE` | DISCOVERY (P1) | Discovery sweep | **500 ms** during discovery window | 250 ms | 3 retries @ 250 ms |
| `MSG_TYPE_PEER_REGISTERED` | DISCOVERY (P1) | State transition event | N/A | 500 ms | 2 retries @ 500 ms |
| `MSG_TYPE_REQUEST_DATA` | DATA (P2) | Periodic pull | **2000 ms** | 1000 ms | 2 retries @ 500 ms |
| `MSG_TYPE_REQUEST_CELL_DATA` | DATA (P2) | Periodic pull | **3000 ms** | 1500 ms | 2 retries @ 750 ms |
| `MSG_TYPE_BATTERY_DATA` | DATA (P2) | Periodic telemetry push | **2000 ms** | 1000 ms | 0 retries |
| `MSG_TYPE_POWER_DATA` | DATA (P2) | Periodic telemetry push | **1000 ms** | 500 ms | 0 retries |
| `MSG_TYPE_CELL_DATA` | DATA (P2) | Periodic telemetry push | **3000 ms** | 1500 ms | 0 retries |
| `MSG_TYPE_CONFIG_SYNC_REQUEST` | DATA (P2) | On-demand sync | N/A | 3000 ms | 3 retries @ 2000 ms |
| `MSG_TYPE_CONFIG_RESPONSE` | DATA (P2) | Response to request | N/A | 500 ms | 2 retries @ 500 ms |
| `MSG_TYPE_MQTT_STATE_QUERY` | DATA (P2) | Poll when UI needs status | **5000 ms** | 2000 ms | 2 retries @ 1000 ms |
| `MSG_TYPE_MQTT_PUBLISH` | DATA (P2) | Event-driven publish | N/A | 200 ms | 3 retries @ 1000 ms |
| `MSG_TYPE_LED_CATALOG_REQUEST` | DATA (P2) | On-demand fetch | N/A | 5000 ms | 2 retries @ 2000 ms |
| `MSG_TYPE_NTP_TIME_REQUEST` | DATA (P2) | Periodic sync | **60000 ms** | 30000 ms | 2 retries @ 5000 ms |
| `MSG_TYPE_VERSION_BEACON` | MONITORING (P3) | Low-rate beacon | **300000 ms** (5 min) | 60000 ms | 0 retries |
| `MSG_TYPE_DIAGNOSTICS` | MONITORING (P3) | Best-effort periodic/report | **30000 ms** | 10000 ms | 0 retries |

> **Key insight:** `MSG_TYPE_POWER_DATA` should be sent more frequently than `MSG_TYPE_BATTERY_DATA` because instantaneous power is more dynamic. This is a **cadence** decision (1s vs 2s), not a queue-retention decision.

---

**Queue Tier + Cadence Summary**

```
┌─ Control Plane Queue (Priority 0 – critical)
│  ├─ HEARTBEAT:           periodic 1000ms (always on when peer is registered)
│  ├─ INFRA_STATE:         event-driven, min_gap=250ms
│  ├─ INFRA_STATE_REQUEST: on-demand, retry 3x @250ms
│  ├─ ACK/PROBE_ACK/HB_ACK: immediate response, tiny min_gap (10–20ms)
│  └─ Behaviour: never starved by lower tiers
│
├─ Discovery Queue (Priority 1 – timely)
│  ├─ PROBE:               periodic 500ms during discovery window
│  └─ PEER_REGISTERED:     event-driven, min_gap=500ms
│
├─ Data Queue (Priority 2 – operational)
│  ├─ POWER_DATA:          1000ms
│  ├─ BATTERY_DATA:        2000ms
│  ├─ CELL_DATA:           3000ms
│  ├─ REQUEST_DATA:        2000ms (pull)
│  ├─ CONFIG/MQTT/LED req: on-demand with subtype-specific min_gap + retries
│  └─ Behaviour: rate-limited so data cannot flood control traffic
│
└─ Monitoring Queue (Priority 3 – informational)
   ├─ DIAGNOSTICS:         30000ms
   └─ VERSION_BEACON:      300000ms
```

---

**Implementation: Per-Subtype Send Policy**

```cpp
// File: esp32common/espnow_common_utils/espnow_send_policy.h (new)

struct MessageSendPolicy {
    MessagePriority priority;
    uint32_t target_period_ms;   // 0 => not periodic (event/request driven)
    uint32_t min_gap_ms;         // minimum elapsed time since last send of this subtype
    uint8_t  max_retries;
    uint32_t retry_interval_ms;
    uint8_t  max_burst_per_sec;  // 0 => unlimited
};

const MessageSendPolicy& get_send_policy(uint8_t msg_type);
```

```cpp
// esp32common/espnow_common_utils/espnow_tx_scheduler.cpp (modify)

bool EspnowTxScheduler::is_due(uint8_t msg_type, uint32_t now_ms) {
    const auto& p = get_send_policy(msg_type);
    uint32_t last = last_send_ms_[msg_type];

    if (now_ms - last < p.min_gap_ms) return false;
    if (p.max_burst_per_sec > 0 && burst_window_[msg_type].would_exceed(now_ms, p.max_burst_per_sec))
        return false;

    return true;
}

void EspnowTxScheduler::scheduler_task(void*) {
    espnow_queue_msg_t msg{};
    MessagePriority prio{};

    while (true) {
        if (!priority_queue_.receive_next_message(msg, prio, pdMS_TO_TICKS(10))) {
            taskYIELD();
            continue;
        }

        uint8_t msg_type = msg.data[0];
        uint32_t now_ms = esp_timer_get_time() / 1000;

        if (!is_due(msg_type, now_ms)) {
            priority_queue_.defer_message(prio, msg);   // requeue/defer, do not drop on age
            continue;
        }

        esp_err_t err = esp_now_send(msg.dest_mac, msg.data, msg.len);
        if (err == ESP_OK) {
            last_send_ms_[msg_type] = now_ms;
        } else if (err == ESP_ERR_ESPNOW_NO_MEM) {
            schedule_retry(msg, get_send_policy(msg_type));
        }
    }
}
```

**Implementation Path:**

**File**: `esp32common/espnow_common_utils/espnow_priority_queue_manager.h` (new)

```cpp
enum class MessagePriority : uint8_t {
    CONTROL    = 0,
    DISCOVERY  = 1,
    DATA       = 2,
    MONITORING = 3
};

struct PriorityQueueConfig {
    MessagePriority priority;
    size_t          depth;
    bool            drop_oldest_on_full;
};

class EspnowPriorityQueueManager {
public:
    bool init(const std::vector<PriorityQueueConfig>& configs);
    bool send_message(MessagePriority priority, const espnow_queue_msg_t& msg);
    bool receive_next_message(espnow_queue_msg_t& msg, MessagePriority& priority,
                              TickType_t wait = portMAX_DELAY);
    bool defer_message(MessagePriority priority, const espnow_queue_msg_t& msg);

    size_t   get_queue_depth(MessagePriority priority) const;
    uint32_t get_queue_drop_count(MessagePriority priority) const;
    uint32_t get_defer_count(MessagePriority priority) const;
    void     flush_by_priority(MessagePriority priority);
};
```

**Integration Points (Outbound — TX outbound scheduler and RX outbound ACK/request sender):**
- Replace `EspnowQueueManager` with `EspnowPriorityQueueManager` as the send path.
- Add `get_send_policy(msg_type)` lookup used by scheduler gating (`is_due()`).
- Apply policy uniformly to TX and RX outbound messages.
- Keep `HEARTBEAT` periodic and independent from Ethernet status (peer-registered gate only).
- Track `sent_count`, `defer_count`, `retry_count`, `policy_block_count` per subtype.

**Benefits (Outbound):**
- Timing policy now explicitly controls **send frequency by subtype**.
- Control-plane messages keep low-latency cadence even under telemetry load.
- Data messages are smooth-rate limited instead of bursty.
- Discovery traffic is bounded to discovery windows and cannot flood runtime traffic.

---

### 6.1.1 Receiver Inbound Queue Priority (Frequency Policy Aligned)

**The Gap Not Covered by §6.1:**

§6.1 defines outbound cadence policy. The receiver still needs inbound priority ordering so control-plane state updates are processed before bulk data.

**Why This Matters:**

If `INFRA_STATE` and telemetry arrive in the same burst, the receiver must process control-plane updates first so handlers do not make requests against stale infrastructure assumptions.

```
Current flat FIFO (problem):
Receive callback enqueues in arrival order:
  [DATA_MSG_1] [DATA_MSG_2] [INFRA_STATE eth=0] [DATA_MSG_3]

Worker processes DATA_MSG_1 first, shadow still eth=1,
sends CONFIG_SYNC_REQUEST while TX has no Ethernet.
```

**Proposed: Receiver Inbound Priority Queue**

The callback classifies by subtype into P0/P1/P2/P3, and worker drains strict priority order:

```
Receiver inbound queues:

┌─ P0 CONTROL: HEARTBEAT, INFRA_STATE, INFRA_STATE_REQUEST, ACK/PROBE_ACK
├─ P1 DISCOVERY: PROBE, PEER_REGISTERED
├─ P2 DATA: BATTERY_DATA, POWER_DATA, CELL_DATA, CONFIG_RESPONSE, ...
└─ P3 MONITORING: VERSION_BEACON, DIAGNOSTICS
```

**Receive Callback Classification:**

```cpp
inline MessagePriority classify_inbound(uint8_t msg_type) {
    switch (msg_type) {
        case MSG_TYPE_HEARTBEAT:
        case MSG_TYPE_INFRA_STATE:
        case MSG_TYPE_INFRA_STATE_REQUEST:
        case MSG_TYPE_ACK:
        case MSG_TYPE_PROBE_ACK:
            return MessagePriority::CONTROL;
        case MSG_TYPE_PROBE:
        case MSG_TYPE_PEER_REGISTERED:
            return MessagePriority::DISCOVERY;
        case MSG_TYPE_VERSION_BEACON:
        case MSG_TYPE_DIAGNOSTICS:
            return MessagePriority::MONITORING;
        default:
            return MessagePriority::DATA;
    }
}
```

**Inbound Worker (no age-expiry policy):**

```cpp
void espnow_worker_task(void*) {
    espnow_queue_msg_t msg{};
    MessagePriority priority{};

    while (true) {
        if (!EspnowPriorityQueueManager::instance().receive_next_message(msg, priority,
                                                                          pdMS_TO_TICKS(10))) {
            taskYIELD();
            continue;
        }

        if (!validate_message_crc(&msg)) {
            LOG_WARN("RX_WORKER", "CRC fail priority=%d, dropping", (int)priority);
            continue;
        }

        if (!EspnowMessageRouter::instance().route_message(msg)) {
            LOG_WARN("RX_WORKER", "No handler for msg_type=0x%02X", msg.data[0]);
        }
    }
}
```

**Important separation of concerns:**
- Outbound timing policy = **send cadence** (`target_period_ms`, `min_gap_ms`, retries).
- Inbound timing policy = **priority order only** (P0 first), not per-message retention expiry.

**Integration Points (Inbound — receiver only):**
- Add `classify_inbound()` in `esp32common/espnow_common_utils/espnow_rx_classifier.h`.
- Route incoming packets to the corresponding priority queue in `espnow_receive_cb`.
- Use strict-priority drain in receiver worker loops.
- Export per-priority inbound queue depth + drop counters for observability.

**Combined Queue Architecture Summary:**

```
┌──────────────────────────────────────────────────────┐
│  Receiver ESP-NOW Queue Architecture (both directions)│
├──────────────────────────────────────────────────────┤
│                                                      │
│  INBOUND (received from TX)          OUTBOUND (sent to TX)  │
│  ─────────────────────────────       ────────────────────── │
│  P0: Heartbeat, INFRA_STATE,         P0: ACK, Heartbeat-ACK,│
│      ACK, PROBE_ACK                      INFRA_STATE_REQUEST│
│                                                      │
│  P1: PROBE, PEER_REGISTERED          P1: PROBE (if RX sends)│
│                                                      │
│  P2: Battery, Power, Cell,           P2: CONFIG_SYNC,       │
│      Config responses                    MQTT_STATE_QUERY,  │
│                                          REQUEST_DATA        │
│                                                      │
│  P3: Version, Diagnostics            P3: Version, Diag dump │
│                                                      │
│  Worker drains P0 → P1 → P2 → P3    Scheduler drains P0 first│
│  in strict priority order            with backpressure shed  │
└──────────────────────────────────────────────────────┘
```

**Benefits (Inbound):**
- `INFRA_STATE` and heartbeat flags always processed before data messages in the same burst.
- Shadow is guaranteed up to date before any handler can trigger an infrastructure-dependent request.
- Data message drops (full P2 queue) never prevent `INFRA_STATE` delivery (separate P0 queue).
- Drop counters per priority make it immediately visible which tier is under pressure.

---

### 6.2 Decouple Ethernet from ESP-NOW on Transmitter

**Current State:**
- Ethernet and ESP-NOW services are independent.
- No explicit gating between them.
- Transmitter can send heartbeats/probes while Ethernet is down, causing pointless link traffic.

**The Infrastructure Dependency Problem:**

Ethernet is **wired and definitive** – it represents the hard constraint of whether the system can reach cloud, MQTT broker, or external services. Unlike WiFi (which has transient intermittency), Ethernet either has link or it doesn't.

ESP-NOW, by contrast, is a local mesh radio protocol. It can connect two devices in the same room even when infrastructure (Ethernet, cloud, MQTT) is completely offline. This creates a critical distinction:

- **ESP-NOW link state** = "Can the two devices talk to each other right now?"
- **Ethernet link state** = "Can either device reach infrastructure services?"

**Why Decoupling Matters:**

Currently, the transmitter has no awareness of whether infrastructure is ready. It sends heartbeats and control probes continuously based only on ESP-NOW connectivity. But those messages pass through ESP-NOW, and the receiver sees them.

The receiver's perspective: *"Transmitter is sending heartbeats, so it must be healthy. I'll initiate config sync, MQTT state query, cloud handshake, battery catalog fetch…"*

But here's the problem:
1. **Transmitter sends heartbeat** (Ethernet is down, but TX doesn't know/care).
2. **Receiver receives heartbeat** and interprets it as "transmitter is ready for control-plane traffic."
3. **Receiver sends CONFIG_REQUEST** (or other control messages) via ESP-NOW.
4. **Transmitter receives CONFIG_REQUEST** and tries to respond – but this is futile because:
   - Configuration state might depend on Ethernet being ready.
   - Services that would handle the config update (MQTT, cloud sync, etc.) are unreachable.
   - The transmitter wastes radio/CPU cycles and fills outbound queues with responses to requests it cannot actually fulfill.

**Issue Example (Detailed):**

Scenario: Ethernet cable unplugged on transmitter.

| Time | Event | Consequence |
|------|-------|-------------|
| T=0 | Ethernet link lost on TX | TX is still ESP-NOW connected; no one tells RX. |
| T=1 | TX sends heartbeat (normal cycle) | RX receives it and thinks everything is normal. |
| T=2 | RX sends CONFIG_REQUEST, MQTT_STATE_QUERY, BATTERY_STATUS_REQUEST via ESP-NOW | All land in TX's outbound queue. |
| T=3 | TX receives requests and tries to process | TX logic: *"I got a CONFIG_REQUEST, but Ethernet is down. Can't talk to MQTT to validate settings. Should I respond or wait?"* |
| T=4–T=10 | TX queues pile up; retries trigger; `NO_MEM` errors begin | RX is also retrying; both devices are doing unnecessary work. |
| T=11 | Ethernet cable plugged back in | Services wake up; now BOTH devices flood the system with initialization. Resource contention. |
| T=12+ | Recovery is delayed; both devices compete for radio/CPU. | Could take 5–10s longer than necessary. |

**The Missing Signal:**

RX has no way to know that Ethernet is down on TX. TX has no incentive to tell it (TX is not designed to propagate infrastructure state via heartbeat). So RX keeps sending control-plane requests that TX cannot usefully fulfill.

**Solution: Dual Gate for Control Plane**

Introduce a **control-plane gate** that requires BOTH conditions:

```
Control-plane messages can flow if:
  - ESP-NOW link is CONNECTED (radio/mesh is OK)
  - AND Ethernet is READY (infrastructure dependency is satisfied)

Data plane messages can flow if:
  - ESP-NOW link is CONNECTED (only condition needed for local mesh data)
```

> **Critical Design Constraint:** The heartbeat keepalive pulse is **not** gated by the control-plane gate.
> Heartbeats must always flow whenever the ESP-NOW peer is registered, regardless of Ethernet state.
> Stopping heartbeats when Ethernet is down causes RX to timeout and trigger a full ESP-NOW reconnect — creating
> a reconnect storm on every Ethernet bounce. The heartbeat *payload* carries `INFRA_STATE` flags; it is those
> flags that gate RX behaviour, not heartbeat presence or absence.

**Implementation:**

File: `src/espnow/espnow_dual_gate.h` (new)

```cpp
class EspnowDualGate {
public:
    static EspnowDualGate& instance();
    
    // Control-plane gate: heartbeat, config, discovery
    // Requires BOTH conditions
    bool can_send_control_plane() const {
        bool esp_now_ok = EspNowConnectionManager::instance().is_connected();
        bool ethernet_ready = EthernetManager::instance().is_fully_ready();
        
        return esp_now_ok && ethernet_ready;
    }
    
    // Data-plane gate: power data, telemetry
    // Requires only ESP-NOW (local mesh data doesn't need infrastructure)
    bool can_send_data() const {
        return EspNowConnectionManager::instance().is_connected();
    }
    
    // Diagnostic: why is control plane blocked?
    const char* get_blockage_reason() const {
        bool esp_now_ok = EspNowConnectionManager::instance().is_connected();
        bool ethernet_ready = EthernetManager::instance().is_fully_ready();
        
        if (!esp_now_ok) return "ESP_NOW_DISCONNECTED";
        if (!ethernet_ready) return "ETHERNET_NOT_READY";
        return "OK";
    }
};
```

**Update Transmitter's Heartbeat Manager:**

File: `src/espnow/heartbeat_manager.cpp` (modify)

```cpp
void HeartbeatManager::tick() {
    // Check dual gate before sending heartbeat
    if (!EspnowDualGate::instance().can_send_control_plane()) {
        const char* reason = EspnowDualGate::instance().get_blockage_reason();
        LOG_DEBUG("HEARTBEAT", "Deferring (gate blocked: %s)", reason);
        return;
    }
    
    // Safe to send – infrastructure is ready
    send_heartbeat();
}
```

> ⚠️ **The above version is incorrect.** Blocking the heartbeat on the DualGate causes RX to declare
> `CONNECTION_LOST` when Ethernet is down, triggering an unnecessary full ESP-NOW reconnect.
> The corrected version below always fires the heartbeat keepalive and embeds infrastructure state in the payload.

**Corrected Heartbeat Manager (always fires, carries live infrastructure flags):**

File: `src/espnow/heartbeat_manager.cpp` (modify)

```cpp
void HeartbeatManager::tick() {
    // KEEPALIVE: always fire when a peer is registered.
    // Stopping heartbeats when Ethernet is down triggers a false ESP-NOW reconnect.
    if (!EspNowConnectionManager::instance().is_connected()) {
        return;  // No peer registered – nothing to heartbeat to
    }
    
    heartbeat_payload_t payload{};
    payload.msg_type   = MSG_TYPE_HEARTBEAT;
    payload.seq_number = next_seq_++;
    
    // INFRA_STATE FLAGS: embed live infrastructure state in every heartbeat.
    // RX uses these flags (not heartbeat presence/absence) to gate data requests.
    payload.infra.ethernet_link  = EthernetManager::instance().has_link();
    payload.infra.ethernet_ip    = EthernetManager::instance().has_ip();
    payload.infra.mqtt_connected = MqttManager::instance().is_connected();
    payload.infra.cloud_sync_ok  = CloudSyncManager::instance().is_available();
    payload.infra.time_synced    = TimeManager::instance().is_synced();
    payload.infra.config_valid   = ConfigManager::instance().is_valid();
    payload.uptime_ms  = esp_timer_get_time() / 1000;
    payload.crc8       = compute_crc8(&payload, sizeof(payload) - 1);
    
    EspnowTxScheduler::instance().send(CONTROL_PRIORITY, peer_mac_, &payload, sizeof(payload));
    
    if (!payload.infra.ethernet_link) {
        LOG_DEBUG("HEARTBEAT", "Keepalive sent (Ethernet down – RX requests gated by flags)");
    }
}
```

**Update Transmitter's Config/Control Handler (this is correctly DualGate-blocked):**

```cpp
bool TxConnectionHandler::send_config_response(const config_request_t& req) {
    // Control-plane response: needs dual gate
    if (!EspnowDualGate::instance().can_send_control_plane()) {
        LOG_WARN("TX_CONFIG", "Deferring response (infrastructure not ready)");
        return false;  // Let caller retry; don't queue futile response
    }
    
    // Safe to respond
    return send_via_scheduler(CONTROL_PRIORITY, config_response);
}
```

**Receiver Side Implication:**

Because heartbeats continue regardless of Ethernet state (carrying live `INFRA_STATE` flags), the receiver never loses the
ESP-NOW link due to an Ethernet outage. Instead:
- RX receives a heartbeat with `eth=0` and immediately suppresses Ethernet-dependent requests.
- The ESP-NOW keepalive timer resets on every heartbeat — the link stays alive.
- When Ethernet recovers, TX sends a standalone `INFRA_STATE` update immediately (on `IP_OBTAINED` callback).
- RX shadow updates to `eth=1` and RX resumes Ethernet-dependent requests — without any reconnect.
- The entire Ethernet outage window is handled purely through flag changes in heartbeat payloads.

**Benefits:**

1. **Stops futile queueing**: TX doesn't fill its outbound queue with responses to requests it can't fulfill.
2. **Clearer startup sequencing**: When Ethernet recovers, RX sees the heartbeat as a signal that infrastructure is ready again.
3. **Reduces NO_MEM events**: Fewer unnecessary messages → less queue pressure during infrastructure transitions.
4. **Aligns messaging semantics**: Heartbeat now means "I am healthy AND infrastructure is ready," not just "I'm alive."
5. **Diagnostic clarity**: Logs show `ETHERNET_NOT_READY` when heartbeat is deferred, making root-cause analysis immediate.

> ⚠️ **Note on benefit #4 (revised):** The corrected design redefines heartbeat semantics as:
> *"I am alive. My infrastructure state is: [flags]."* The heartbeat is not suppressed when infrastructure is
> unavailable — it continues to arrive, carrying the current state. RX acts on the flags, not on heartbeat presence.

**Edge Cases Handled:**

- **RX-only scenario (no TX Ethernet)**: RX continues sending data via ESP-NOW (data plane works). RX will suppress Ethernet-dependent requests automatically once it has received and stored TX's infrastructure state. Both devices remain stable.
- **TX Ethernet recovers first**: TX broadcasts an `INFRA_STATE` update via ESP-NOW. RX receives it, updates its local shadow of TX state, and resumes Ethernet-dependent control-plane requests.
- **Both go down**: Both devices gracefully degrade. RX knows TX has no Ethernet; TX continues keepalive heartbeat (with `eth=0` flags) and suppresses infrastructure-dependent responses. No thrashing.
- **Receiver reboots during TX Ethernet outage**: RX will request `INFRA_STATE` on connect (as part of the init sequence). TX will reply with current infrastructure state immediately, so RX never enters an uninformed polling loop.
- **TX Ethernet down — heartbeat continues**: Heartbeats carry `eth=0` flags. RX suppresses Ethernet-dependent requests
    immediately. ESP-NOW link stays alive throughout the outage. No reconnect triggered.
- **TX reboots unexpectedly**: RX detects `uptime_ms` in a received heartbeat or `INFRA_STATE` is *lower* than the
    previously seen value. RX must: (1) immediately invalidate `TxInfraShadow`; (2) treat connection as a fresh reconnect
    (re-request `INFRA_STATE`, restart init sequence from step 1); (3) log the reboot event.
- **Old TX firmware (no `INFRA_STATE` support)**: If RX has not received any `INFRA_STATE` within `INFRA_SHADOW_STALE_MS`
    (15s) of connecting and heartbeats are arriving, it applies a **conservative default assumption**:
    `eth=1, mqtt=0, cloud=0, config=1`. This allows battery data and config fetch but suppresses MQTT and cloud requests.
    RX logs `WARN: TX_INFRA shadow never populated – applying conservative defaults`.
- **Co-recovery (both ESP-NOW AND Ethernet lost simultaneously)**: Recovery is staged:
    (1) ESP-NOW reconnects first (radio typically faster than wired services coming up).
    (2) TX sends heartbeat with `eth=0` — RX restricts to local data requests only.
    (3) Ethernet recovers on TX — TX sends standalone `INFRA_STATE` on `IP_OBTAINED` callback.
    (4) RX shadow updates progressively (`eth=1` → then `mqtt=1`) — infrastructure requests resume in stages, not as a burst.
- **Both devices rebooted simultaneously (full power cycle)**: Whichever boots second initiates discovery.
    The reconnect-then-INFRA_STATE-then-staged-init sequence handles this correctly regardless of boot order.

---

### 6.2.1 ESP-NOW Infrastructure State Messaging Protocol

**The Core Problem:**

Without an explicit message, RX has no knowledge of TX's infrastructure state. RX's only signal was the presence or absence of heartbeats — but that is binary and slow (it takes a full heartbeat timeout window to detect absence). We need TX to actively tell RX what infrastructure is available, so RX can immediately adjust what it asks for.

**Design Principle:**

> The transmitter's heartbeat serves two non-negotiable purposes simultaneously:
> 1. **ESP-NOW keepalive** — must always fire when a peer is registered, regardless of infrastructure state.
>    Stopping heartbeats causes RX to declare link loss and trigger unnecessary full reconnects.
> 2. **Infrastructure state carrier** — the heartbeat payload always contains current `INFRA_STATE` flags.
>    RX reads these flags on every heartbeat to maintain a fresh shadow of TX's capabilities.
>
> RX gating decisions are driven by the *content* of heartbeats, never by their *presence or absence*.

---

**New Message: `INFRA_STATE` Payload**

Add a new infrastructure state field to the existing heartbeat payload, or as a dedicated lightweight message type:

```cpp
// File: esp32common/include/espnow_message_types.h (add)

// Sent by TX on every heartbeat and on any infrastructure state change.
// Received by RX to limit data availability assumptions.

struct EspnowInfraStatePayload {
    uint8_t  msg_type;          // MSG_TYPE_INFRA_STATE (new message type)
    uint8_t  protocol_version;  // For forward compatibility
    
    // Infrastructure availability flags (bit-packed)
    struct {
        uint8_t ethernet_link   : 1;  // Physical Ethernet link present
        uint8_t ethernet_ip     : 1;  // IP address obtained
        uint8_t mqtt_connected  : 1;  // MQTT broker reachable and authenticated
        uint8_t cloud_sync_ok   : 1;  // Cloud/remote sync services available
        uint8_t time_synced     : 1;  // NTP or equivalent time sync complete
        uint8_t config_valid    : 1;  // TX local config is loaded and consistent
        uint8_t reserved        : 2;  // Future use
    } flags;
    
    uint32_t uptime_ms;             // TX uptime (RX can detect TX reboots)
    uint16_t ethernet_loss_count;   // How many times Ethernet has dropped (diagnostic)
    uint16_t mqtt_loss_count;       // How many times MQTT has dropped
    uint8_t  crc8;                  // Integrity check
} __attribute__((packed));
```

**When TX Sends `INFRA_STATE`:**

1. **On every heartbeat** – embed flags in the heartbeat payload (or piggyback as companion message).
2. **On any flag change** – TX detects a transition (Ethernet gained/lost, MQTT up/down) and immediately sends a standalone `INFRA_STATE` message without waiting for the next heartbeat cycle. This gives RX sub-second awareness.
3. **On RX connect/reconnect** – TX sends `INFRA_STATE` as the first message after peer registration, before any data flow begins.
4. **On RX explicit request** – RX can request current state with `MSG_TYPE_INFRA_STATE_REQUEST`.
5. **On TX boot completion** – TX sends `INFRA_STATE` as soon as the ESP-NOW stack initialises (before any heartbeat tick).
    The `uptime_ms` will be low; RX uses a decrease in `uptime_ms` relative to the previously seen value to detect a TX reboot
    and immediately invalidate its shadow, restarting the init sequence.

```
TX sends INFRA_STATE when:
  ┌──────────────────────────────────────────────┐
  │  1. Any heartbeat tick (embedded flags)       │
  │  2. EthernetManager state transition          │
  │  3. MQTT connect/disconnect callback          │
  │  4. On PEER_REGISTERED event (init)           │
  │  5. On RX explicit INFRA_STATE_REQUEST        │
  └──────────────────────────────────────────────┘
```

---

**Receiver: Infrastructure State Shadow**

RX maintains a local **shadow copy** of TX's last reported infrastructure state. This shadow is the single source of truth for what RX is permitted to request from TX:

The shadow is populated from **two sources**, both calling `TxInfraShadow::instance().update()` identically:
- The `INFRA_STATE` flags embedded in every received heartbeat payload.
- Standalone `INFRA_STATE` messages sent on state changes, at peer registration, or on explicit request.

The shadow does not distinguish the source — any update refreshes the age timer and updates all flags atomically.
Embedded heartbeat flags provide continuous low-latency refresh; standalone messages provide immediate sub-second
propagation on state changes.

```cpp
// File: espnowreceiver_2/src/espnow/tx_infra_shadow.h (new)
// File: espnowreceiver_LCD/src/espnow/tx_infra_shadow.h (new)

class TxInfraShadow {
public:
    static TxInfraShadow& instance();
    
    // Called when INFRA_STATE message received from TX
    void update(const EspnowInfraStatePayload& payload);
    
    // Data availability gates – RX uses these before sending requests
    bool tx_has_ethernet()       const { return shadow_.flags.ethernet_link && shadow_.flags.ethernet_ip; }
    bool tx_has_mqtt()           const { return shadow_.flags.mqtt_connected; }
    bool tx_has_cloud_sync()     const { return shadow_.flags.cloud_sync_ok; }
    bool tx_has_valid_config()   const { return shadow_.flags.config_valid; }
    bool tx_is_time_synced()     const { return shadow_.flags.time_synced; }
    
    // Composite gates for common request types
    bool can_request_mqtt_data()    const { return tx_has_mqtt(); }
    bool can_request_config_sync()  const { return tx_has_ethernet() && tx_has_valid_config(); }
    bool can_request_cloud_data()   const { return tx_has_cloud_sync(); }
    bool can_request_battery_data() const { return true; }  // Local data, always available
    
    // How old is our shadow? (detect stale state after reconnect)
    uint32_t age_ms() const;
    bool is_stale() const { return age_ms() > INFRA_SHADOW_STALE_MS; }
    
    // If shadow is stale, we should request fresh state before sending
    bool should_request_refresh() const { return is_stale() || !received_at_least_once_; }

private:
    EspnowInfraStatePayload shadow_{};
    uint32_t last_update_ms_{0};
    bool received_at_least_once_{false};
    
    static constexpr uint32_t INFRA_SHADOW_STALE_MS = 15000;  // 15s without update = stale
};
```

---

**Receiver: Data Availability Constraints**

The shadow gates directly control what categories of data RX can legitimately request from TX:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ RX Data Request Permission Table                                            │
├───────────────────────────┬────────────────────────────┬────────────────────┤
│ Request Type              │ Gate Condition             │ If Gate Fails      │
├───────────────────────────┼────────────────────────────┼────────────────────┤
│ BATTERY_STATUS_REQUEST    │ ESP-NOW connected (always) │ N/A (local data)   │
│ POWER_DATA_REQUEST        │ ESP-NOW connected (always) │ N/A (local data)   │
│ CELL_DATA_REQUEST         │ ESP-NOW connected (always) │ N/A (local data)   │
│ CONFIG_SYNC_REQUEST       │ tx_has_ethernet()          │ Defer + log        │
│                           │ AND tx_has_valid_config()  │                    │
│ MQTT_STATE_QUERY          │ tx_has_mqtt()              │ Show offline badge │
│ MQTT_PUBLISH_REQUEST      │ tx_has_mqtt()              │ Queue locally      │
│ CLOUD_DATA_REQUEST        │ tx_has_cloud_sync()        │ Defer + log        │
│ VERSION_CHECK_REQUEST     │ tx_has_ethernet()          │ Use cached version │
│ NTP_TIME_REQUEST          │ tx_is_time_synced()        │ Use local RTC      │
│ LED_CATALOG_REQUEST       │ tx_has_valid_config()      │ Use cached catalog │
└───────────────────────────┴────────────────────────────┴────────────────────┘
```

**Shadow freshness gate:**

Before sending any infrastructure-dependent request, RX checks `TxInfraShadow::is_stale()`. If the shadow has not been
updated within `INFRA_SHADOW_STALE_MS` (15s — should comfortably exceed the heartbeat interval), RX sends
`INFRA_STATE_REQUEST` first and defers the infrastructure request until the reply arrives.

**`INFRA_STATE` and heartbeat priority classification:**

| Message | Priority Class | Rationale |
|---------|---------------|-----------|
| Heartbeat (with embedded flags) | CONTROL (P0) | Keepalive + state carrier; if dropped, RX link times out |
| `MSG_TYPE_INFRA_STATE` (standalone) | CONTROL (P0) | State change; must arrive before any data request triggered by it |
| `MSG_TYPE_INFRA_STATE_REQUEST` | CONTROL (P0) | RX is blocked waiting for state; cannot wait behind telemetry |
| `REQUEST_DATA` / `REQUEST_CELL_DATA` | DATA (P2) | Local data; lower priority than control signals |
| Config/MQTT/cloud requests | DATA (P2) | Infrastructure-dependent; only sent when gate allows |

**Implementation in RX Connection Path (both receiver variants):**

```cpp
// espnowreceiver_2/src/espnow/rx_connection_handler.cpp (modify init sequence)
// espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp (modify init sequence)

void RxConnectionHandler::on_connected() {
    auto& shadow = TxInfraShadow::instance();
    
    // Step 1: Always request battery/power data (local to TX, always available)
    send_request(MSG_REQUEST_DATA);
    send_request(MSG_REQUEST_CELL_DATA);
    
    // Step 2: If we have a fresh shadow, gate the rest. If stale, ask TX first.
    if (shadow.should_request_refresh()) {
        LOG_INFO("RX_INIT", "Requesting TX infra state before sending control requests");
        send_request(MSG_TYPE_INFRA_STATE_REQUEST);
        // Defer remaining init until INFRA_STATE reply received
        pending_init_after_infra_state_ = true;
        return;
    }
    
    // Step 3: Gate config/MQTT/cloud requests on TX's reported state
    if (shadow.can_request_config_sync()) {
        send_request(MSG_CONFIG_SYNC_REQUEST);
    } else {
        LOG_INFO("RX_INIT", "Skipping config sync (TX Ethernet not available)");
    }
    
    if (shadow.can_request_mqtt_data()) {
        send_request(MSG_MQTT_STATE_QUERY);
    } else {
        LOG_INFO("RX_INIT", "Skipping MQTT query (TX MQTT not connected)");
        ui_set_mqtt_badge(BADGE_OFFLINE);
    }
    
    // Version/catalog always use cache if infra unavailable
    if (shadow.can_request_config_sync()) {
        send_request(MSG_LED_CATALOG_REQUEST);
    }
}
```

**Handler for INFRA_STATE reply:**

```cpp
// Registered in message routes:
void handle_infra_state_message(const espnow_queue_msg_t& msg) {
    auto payload = reinterpret_cast<const EspnowInfraStatePayload*>(msg.data);
    TxInfraShadow::instance().update(*payload);
    
    LOG_INFO("RX_INFRA", "TX infra state: eth=%d mqtt=%d cloud=%d config=%d",
             payload->flags.ethernet_link,
             payload->flags.mqtt_connected,
             payload->flags.cloud_sync_ok,
             payload->flags.config_valid);
    
    // If we deferred init sequence waiting for this, resume now
    if (RxConnectionHandler::instance().has_pending_init()) {
        RxConnectionHandler::instance().resume_init_sequence();
    }
    
    // Update any UI elements that reflect TX infrastructure health
    ui_update_infrastructure_status(*payload);
}
```

---

**TX: Sending INFRA_STATE on Infrastructure Changes**

```cpp
// ESPnowtransmitter2/src/espnow/tx_infra_state_broadcaster.cpp (new)

void TxInfraStateBroadcaster::on_ethernet_state_changed(EthernetState new_state) {
    // Immediately notify RX – don't wait for next heartbeat
    broadcast_current_state();
}

void TxInfraStateBroadcaster::on_mqtt_state_changed(bool connected) {
    broadcast_current_state();
}

void TxInfraStateBroadcaster::broadcast_current_state() {
    if (!EspNowConnectionManager::instance().is_connected()) {
        return;  // No point sending if ESP-NOW is down
    }
    
    EspnowInfraStatePayload payload{};
    payload.msg_type         = MSG_TYPE_INFRA_STATE;
    payload.protocol_version = INFRA_STATE_PROTOCOL_VERSION;
    payload.flags.ethernet_link  = EthernetManager::instance().has_link();
    payload.flags.ethernet_ip    = EthernetManager::instance().has_ip();
    payload.flags.mqtt_connected = MqttManager::instance().is_connected();
    payload.flags.cloud_sync_ok  = CloudSyncManager::instance().is_available();
    payload.flags.time_synced    = TimeManager::instance().is_synced();
    payload.flags.config_valid   = ConfigManager::instance().is_valid();
    payload.uptime_ms            = esp_timer_get_time() / 1000;
    payload.ethernet_loss_count  = EthernetManager::instance().loss_count();
    payload.mqtt_loss_count      = MqttManager::instance().loss_count();
    payload.crc8                 = compute_crc8(&payload, sizeof(payload) - 1);
    
    // Send with CONTROL priority – this is a state change notification
    EspnowTxScheduler::instance().send(
        CONTROL_PRIORITY, 
        peer_mac_,
        &payload, 
        sizeof(payload)
    );
    
    LOG_INFO("TX_INFRA", "Broadcast infra state: eth=%d/%d mqtt=%d cloud=%d",
             payload.flags.ethernet_link,
             payload.flags.ethernet_ip,
             payload.flags.mqtt_connected,
             payload.flags.cloud_sync_ok);
}
```

---

**Complete Flow Diagram: Ethernet Loss and Recovery**

```
TX                              ESP-NOW                         RX
──────────────────────────────────────────────────────────────────────────────
[Ethernet cable unplugged]
EthernetManager → LINK_LOST
on_ethernet_state_changed()
broadcast_current_state()
  flags: eth=0, mqtt=0         ──── INFRA_STATE ────────────►  update shadow
                                                                eth=0, mqtt=0
                                                                suppress config/mqtt requests
                                                                ui: show "TX offline" badge

[heartbeat tick]
DualGate: eth=0, BLOCKED        (no heartbeat sent)
                               
[heartbeat tick]
DualGate: eth=0, BLOCKED        (no heartbeat sent)

                                              ◄─── INFRA_STATE_REQUEST ────   (after stale window if 
                                                                               no heartbeat received)
broadcast_current_state()
  flags: eth=0, mqtt=0         ──── INFRA_STATE ────────────►  shadow refreshed, still eth=0
                                                                no requests sent

[Ethernet cable plugged in]
EthernetManager → IP_OBTAINED
on_ethernet_state_changed()
broadcast_current_state()
  flags: eth=1, mqtt=0         ──── INFRA_STATE ────────────►  shadow: eth=1, mqtt=0
                                                                can_request_config_sync() = true
                                                                send CONFIG_SYNC_REQUEST ─────────►
                                                                
[MQTT reconnect completes]
on_mqtt_state_changed(true)
broadcast_current_state()
  flags: eth=1, mqtt=1         ──── INFRA_STATE ────────────►  shadow: eth=1, mqtt=1
                                                                can_request_mqtt_data() = true
                                                                send MQTT_STATE_QUERY ────────────►
                                                                ui: show "MQTT online" badge

[DualGate now allows heartbeat]
send_heartbeat()               ──── HEARTBEAT ──────────────►  RX heartbeat timer reset
```

> ⚠️ **Above diagram is the old incorrect design** (heartbeats blocked when Ethernet is down).
> The corrected flow diagram is below.

**Corrected Flow: Ethernet Loss (Heartbeat Continues, ESP-NOW Link Unbroken)**

```
TX                              ESP-NOW                         RX
──────────────────────────────────────────────────────────────────────────────
[Ethernet cable unplugged]
EthernetManager → LINK_LOST
on_ethernet_state_changed()
broadcast_current_state()
    flags: eth=0, mqtt=0         ──── INFRA_STATE (CTRL P0) ───►  shadow: eth=0, mqtt=0
                                                                                                                                     suppress config/mqtt requests
                                                                                                                                     ui: badge "TX Ethernet offline"
                                                                                                                                     ESP-NOW link still active ✓

[heartbeat tick – Ethernet still down]
payload.infra.eth_link=0        ──── HEARTBEAT (eth=0) ───────►  shadow refreshed (still eth=0)
                                                                                                                                     keepalive timer reset ✓
                                                                                                                                     no config/mqtt requests sent ✓

[heartbeat tick – Ethernet still down]
payload.infra.eth_link=0        ──── HEARTBEAT (eth=0) ───────►  shadow refreshed, link maintained ✓

[Ethernet cable plugged in]
EthernetManager → IP_OBTAINED
on_ethernet_state_changed()
broadcast_current_state()
    flags: eth=1, mqtt=0         ──── INFRA_STATE (CTRL P0) ───►  shadow: eth=1, mqtt=0
                                                                                                                                     can_request_config_sync()=true
                                                                                                                ◄──────── CONFIG_SYNC_REQUEST (DATA P2)
TX processes config response    ──── CONFIG_RESPONSE ─────────►  config updated

[MQTT reconnect completes on TX]
on_mqtt_state_changed(true)
broadcast_current_state()
    flags: eth=1, mqtt=1         ──── INFRA_STATE (CTRL P0) ───►  shadow: eth=1, mqtt=1
                                                                                                                                     can_request_mqtt_data()=true
                                                                                                                ◄──────── MQTT_STATE_QUERY (DATA P2)
                                                                                                                                     ui: badge "MQTT online"

[heartbeat tick – all services up]
payload.infra all=1             ──── HEARTBEAT (all flags 1) ──►  shadow fully populated, all gates open

─── RESULT: ESP-NOW link unbroken throughout. Recovery is staged, not bursty. ───
```

**Co-Recovery Flow: Both ESP-NOW AND Ethernet Lost Simultaneously**

```
TX                              ESP-NOW                         RX
──────────────────────────────────────────────────────────────────────────────
[Both devices lose power / full network interruption]

[TX boots, ESP-NOW stack initialised]
TX: no peer yet
TX queues INFRA_STATE (eth=0)   (held – no peer registered)
TX begins discovery (channel hopping)

[RX boots, ESP-NOW stack initialised]
RX: waiting for PROBE

[ESP-NOW link re-established]
TX: PEER_REGISTERED
TX sends queued INFRA_STATE     ──── INFRA_STATE (eth=0) ─────►  shadow: eth=0 (TX Ethernet still down)
                                                                                                                                     RX: only local requests permitted

RX sends local init requests:
                                                                ◄──── REQUEST_DATA (DATA P2) ──   battery / power (always valid)
                                                                ◄──── REQUEST_CELL_DATA ────────

TX processes and responds:      ──── DATA_RESPONSE ───────────►  RX display updates with live data ✓

[TX Ethernet link comes up]
EthernetManager → IP_OBTAINED
broadcast_current_state()
    flags: eth=1, mqtt=0          ──── INFRA_STATE (CTRL P0) ───►  shadow: eth=1, mqtt=0
                                                                ◄──── CONFIG_SYNC_REQUEST ──────  config now requestable

[TX MQTT reconnects]
MqttManager → CONNECTED
broadcast_current_state()
    flags: eth=1, mqtt=1          ──── INFRA_STATE (CTRL P0) ───►  shadow: eth=1, mqtt=1
                                                                ◄──── MQTT_STATE_QUERY ──────────  MQTT now requestable
                                                                                                                                     Full service restored ✓

─── No burst. No reconnect storm. Recovery is ordered and gated at every step. ───
```

---

**Benefits of Explicit Infrastructure State Messaging:**

1. **RX never sends futile requests**: Gates are driven by TX's actual reported state, not inferred from heartbeat presence/absence.
2. **Sub-second state propagation**: TX pushes state on change, not just on heartbeat cycle.
3. **Clean UI state**: RX can show accurate badges (Ethernet offline, MQTT offline) because it has real data.
4. **Deterministic init sequence**: On reconnect, RX knows exactly which requests are valid before sending a single one.
5. **Diagnostic richness**: `ethernet_loss_count` and `mqtt_loss_count` counters give field diagnostics without additional logging.
6. **Minimal overhead**: `EspnowInfraStatePayload` is 8 bytes packed. Sent at heartbeat frequency + on-change. Negligible traffic.

7. **ESP-NOW link survives Ethernet outages**: Heartbeat continues with `eth=0` flags. No false `CONNECTION_LOST` triggered.
8. **Staged co-recovery**: When both links fail, recovery is ordered (ESP-NOW first, Ethernet services progressively) with no burst.
9. **TX reboot detection**: `uptime_ms` decrease in heartbeat/INFRA_STATE tells RX immediately that TX rebooted.
10. **Firmware compatibility fallback**: Conservative shadow defaults prevent futile requests against old TX firmware.

---

### 6.3 Unified Single State Machine for ESP-NOW Messages

**Current State:**
- `EspNowConnectionManager` owns link state (IDLE/CONNECTING/CONNECTED).
- `TxStateMachine` (on TX) owns application state (DISCOVERING/CONNECTED/ACTIVE).
- `RxStateMachine` (on RX) owns data-validity state.
- **Three separate ownership chains, some redundant.**

**Risk:**
- Split-brain scenarios if machines diverge during edge cases.
- Observers don't know which machine is the "source of truth."

**Proposed Unified Model:**
Consolidate to a **single master state machine** used by both TX and RX:

```
                ┌─────────────────────┐
                │  ESP-NOW_MASTER_FSM │
                └─────────────────────┘
                         │
        ┌────────────────┼────────────────┐
        │                │                │
    ┌───▼──┐         ┌──▼────┐      ┌───▼────┐
    │IDLE  │────────▶│CONNECT│─────▶│ACTIVE  │
    └──┬───┘         └──┬────┘      └───┬────┘
       ▲                │                │
       │                ▼                │
       └────────────────┴────────────────┘
           (LOST / TIMEOUT / ERROR)
```

**States and Transitions:**

```cpp
// File: esp32common/espnow/master_state_machine.h (new)

enum class EspnowMasterState : uint8_t {
    // Discovery and Link Phases
    IDLE = 0,                 // No active connection attempt
    DISCOVERING = 1,          // Scanning for peer or waiting for peer
    PEER_FOUND = 2,           // Peer discovered, waiting for registration
    LINK_ESTABLISHED = 3,     // Peer registered, channel locked
    
    // Application Ready Phases
    READY_FOR_DATA = 4,       // Link OK, app can send/recv data
    DATA_ACTIVE = 5,          // Data flowing (RX: power data confirmed, TX: ACTIVE state)
    
    // Recovery Phases
    LINK_LOST = 6,            // Timeout or explicit disconnect
    RECOVERING = 7,           // Backoff/retry in progress
    
    // Error Terminal States
    ERROR_STATE = 8           // Unrecoverable
};

class EspnowMasterStateMachine {
public:
    static EspnowMasterStateMachine& instance();
    
    // Query current state
    EspnowMasterState get_state() const { return current_state_; }
    
    // Quick state predicates
    bool is_connected() const { 
        return current_state_ >= LINK_ESTABLISHED && current_state_ <= DATA_ACTIVE; 
    }
    bool is_discovering() const { 
        return current_state_ == DISCOVERING || current_state_ == PEER_FOUND; 
    }
    bool can_send_data() const { 
        return current_state_ == DATA_ACTIVE; 
    }
    
    // Transition with validation and logging
    bool transition(EspnowMasterState new_state, const char* reason);
    
    // Register observer callbacks
    void on_state_changed(std::function<void(EspnowMasterState old, EspnowMasterState new)> cb);
    
    // Diagnostic
    const char* state_to_string(EspnowMasterState state) const;
    uint32_t state_duration_ms() const;
    uint32_t total_uptime_ms() const;
};
```

**Migration Strategy:**
1. Keep `EspNowConnectionManager` and device-specific machines functional (don't break existing code).
2. Introduce `EspnowMasterStateMachine` as a **parallel observer** that mirrors primary state.
3. Gradually move decision logic to master FSM.
4. Retire old machines once master FSM handles all paths.

**Benefits:**
- **Single source of truth** for link readiness.
- Observers see one state → one behavior expectation.
- Easier to add new states without breaking existing logic.
- Clearer for firmware developers to reason about.

---

### 6.4 Alignment Between espnowreceiver_2 and espnowreceiver_LCD

**Current State Analysis:**

| Aspect | LCD | _2 | Status |
|--------|-----|-----|--------|
| **Ingress architecture** | Clean split (ingress/messages/routes) | Monolithic worker | LCD cleaner |
| **Message parsing** | Separate module per phase | Inline in worker loop | LCD more testable |
| **Message routing** | Router-based dispatch | Inline if/else in worker | LCD more extensible |
| **State machines** | RxStateMachine + RxHeartbeat | RxStateMachine + RxHeartbeat | Equivalent |
| **Send path** | Via scheduler | Via scheduler | Equivalent |
| **Initialization burst** | Explicit init sequence | Same but less visible | Equivalent |
| **Display/UI** | LVGL deferred, snapshot-based | TFT immediate or deferred | LCD more robust |

**Key Gaps in _2 to Close:**

1. **Worker task monolithism** – _2's `espnow_tasks.cpp` is ~1000 lines doing parse, validate, route, metrics, state management, all in one task.
   - **Fix:** Extract parse/validate/route into separate header/module like LCD does.
   - **File:** Create `espnowreceiver_2/src/espnow/espnow_message_routes.cpp` and split responsibilities.

2. **Ingress message handling** – _2 uses inline message type dispatch; LCD uses a router pattern.
   - **Fix:** Use `EspnowMessageRouter::instance().route_message()` (already in shared lib) more explicitly.
   - **Benefit:** Easier to add new message handlers without touching worker loop.

3. **Message validation** – _2 lacks explicit CRC/checksum validation in the ingress path (done downstream per-handler).
   - **Fix:** Add unified CRC check in ingress, before dispatch, like LCD does in `espnow_runtime_ingress.cpp`.
   - **Benefit:** Single validation point, clearer error handling.

4. **State visibility** – _2 does not log state machine transitions as clearly as LCD.
   - **Fix:** Add explicit transition logging in RxStateMachine callback.
   - **Benefit:** Easier field debugging and log analysis.

**Recommended Alignment Tasks:**

**Task A1: Extract message router to _2**
```cpp
// espnowreceiver_2/src/espnow/espnow_message_routes.cpp (new)

void setup_message_routes() {
    auto& router = EspnowMessageRouter::instance();
    
    router.register_handler(msg_data, handle_data_message);
    router.register_handler(msg_battery_status, handle_battery_status);
    // etc.
}
```

**Task A2: Move CRC validation to ingress**
```cpp
// espnowreceiver_2/src/espnow/espnow_tasks.cpp (modify worker)

while (xQueueReceive(espnow_message_queue, &msg, portMAX_DELAY)) {
    // Validate CRC first (uniform, before dispatch)
    if (!validate_message_crc(&msg)) {
        LOG_WARN("RX", "Invalid CRC, dropping");
        continue;
    }
    
    // Route to handler
    if (!EspnowMessageRouter::instance().route_message(msg)) {
        LOG_WARN("RX", "No handler for message type %d", msg_type);
    }
}
```

**Task A3: Improve state logging**
```cpp
// espnowreceiver_2/src/espnow/rx_state_machine.cpp (modify)

void RxStateMachine::transition(ConnectionState new_state) {
    if (new_state == current_connection_state_) return;
    
    LOG_INFO("RX_STATE", "Transition: %s → %s (duration in prior: %lu ms)",
             state_to_string(current_connection_state_),
             state_to_string(new_state),
             millis() - state_entered_ms_);
    
    current_connection_state_ = new_state;
    state_entered_ms_ = millis();
}
```

**Benefits of Full Alignment:**
- Both receivers can share test utilities.
- Feature changes in one can be trivially ported to the other.
- Reduced maintenance burden (single mental model).
- LCD's superior ingress design applies to TX as well (same shared lib).

---

## 7) Revised Robustness Recommendations (Updated)

Building on the above advanced improvements, here is the **complete priority roadmap**:

### P0: Foundation (Do First – 1–2 weeks)

1. **Decouple Ethernet from ESP-NOW on TX** (§6.2)
   - Add `EspnowDualGate` class, update `HeartbeatManager::tick()`.
   - **Effort:** 3h
   - **Blocks:** Data sender stability improvements.
    - **Critical:** DualGate gates infrastructure-dependent *responses* only. Heartbeat keepalive is NOT gated. See corrected `HeartbeatManager::tick()` in §6.2.

2. **Implement Priority Queues** (§6.1)
   - Add `EspnowPriorityQueueManager`.
   - Update message callers to specify priority.
   - **Effort:** 12h
   - **Impact:** ACK starvation eliminated, NO_MEM behavior class-specific.
    - **INFRA_STATE, INFRA_STATE_REQUEST, and heartbeat messages must be classified CONTROL (P0).**

3. **Align espnowreceiver_2 Ingress** (§6.4, Tasks A1–A3)
   - Extract router, add CRC validation, improve logging.
   - **Effort:** 6h
   - **Impact:** _2 becomes testable and maintainable parity with LCD.

4. **Implement `INFRA_STATE` messaging** (§6.2.1) — **new P0 task**
    - Add `EspnowInfraStatePayload` struct to shared `esp32common/include/espnow_message_types.h`.
    - Embed `INFRA_STATE` flags in the heartbeat payload struct (both TX and RX heartbeat types).
    - Add `TxInfraStateBroadcaster` to transmitter: hook into `EthernetManager` and `MqttManager` callbacks.
    - Add `TxInfraShadow` class to both receivers: `update()` called from both heartbeat handler and standalone `INFRA_STATE` handler.
        - Update RX init sequence in both receiver variants:
            - `espnowreceiver_2/src/espnow/rx_connection_handler.cpp`
            - `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp`
            to gate infrastructure-dependent requests on shadow.
    - Add TX reboot detection: compare incoming `uptime_ms` to last seen value; on decrease, invalidate shadow and re-init.
    - Add firmware fallback: apply conservative defaults if shadow never populated within `INFRA_SHADOW_STALE_MS` of connecting.
    - **Effort:** 8h
    - **Impact:** Eliminates all futile requests; ESP-NOW link survives Ethernet outages; staged co-recovery; TX reboot detection.

5. **Implement receiver inbound priority queue** (§6.1.1) — **new P0 task**
   - Add `classify_inbound()` to `esp32common/espnow_common_utils/espnow_rx_classifier.h`.
   - Modify `espnow_receive_cb` to post to priority queue tier instead of single flat queue.
   - Modify both receiver worker loops to use `receive_next_message()` with strict priority-order drain (P0 before P1 before P2 before P3).
   - Wire inbound drop counters into the same per-priority telemetry as outbound.
   - **Effort:** 5h (shares `EspnowPriorityQueueManager` from task #2; incremental work only)
   - **Impact:** Guarantees `INFRA_STATE`/heartbeat flags are processed before data messages in the same burst; shadow is current before any handler runs. Closes the inbound-ordering gap that could cause futile requests even after INFRA_STATE messaging is added.

### P1: State Convergence (Weeks 3–4)

6. **Unified Master FSM rollout** (§6.3)
    - Introduce `EspnowMasterStateMachine` in shadow mode, mirror existing transitions, then migrate decision points.
    - **Effort:** 20h
    - **Impact:** Eliminates split-brain risk between connection manager and role-specific state machines.

### P2: Robustness Hardening (Next Sprint – ongoing)

7. **Control-Plane Prioritization** (from §5)
   - Use priority queues to ensure ACK/heartbeat-ACK never starved by telemetry.
   - **Effort:** 4h (leverages priority queue infrastructure from P0 task #2).
   - **Impact:** Receiver connection stability under memory pressure.

8. **Reconnect Burst Shaping** (from §5)
   - Implement exponential backoff with jitter on reconnect attempts.
   - **Effort:** 3h
   - **Impact:** Avoid thundering herd during mass reconnect events.

9. **Discovery Ownership Consolidation** (from §5)
   - Ensure only one discovery task runs per device at a time.
   - **Effort:** 2h
   - **Impact:** Avoid broadcast peer/channel contention.

### P3: Observability and Testing (Continuous)

10. **Enhanced Telemetry**
   - Add metrics: message drop count per priority, queue depth histogram, state transition log.
   - **Effort:** 5h
   - **Impact:** Visibility into failures, easier root-cause analysis.

11. **Integration Tests**
   - Simulate reconnect storms, NO_MEM conditions, Ethernet outages.
   - **Effort:** 10h
   - **Impact:** Regression detection, confidence in deployments.

---

## 8) Summary: Path to Production Robustness

**Short-term (Next 2 weeks):** Do P0 tasks.
- Ethernet decoupling removes spurious traffic.
- Priority queues eliminate ACK starvation.
- Alignment of _2 makes it as maintainable as LCD.

**Medium-term (Next 4–6 weeks):** Do P1 and P2 tasks.
- Master state machine eliminates split-brain risk.
- Burst shaping and discovery consolidation reduce reconnect churn.
- Enhanced telemetry gives visibility.

**Result:**
- Receiver no longer loses ACK/heartbeat under NO_MEM pressure.
- Transmitter reconnect is deterministic and backoff-aware.
- Both devices gracefully handle Ethernet + ESP-NOW co-recovery.
- Codebases are aligned, testable, and documented.

---

## 9) Failure Mode Coverage Summary

| Failure Mode | Handled By | Expected Behaviour |
|---|---|---|
| Ethernet down, ESP-NOW up | `INFRA_STATE` flags in heartbeat | Heartbeat continues (eth=0); RX gates requests; no ESP-NOW reconnect |
| MQTT down, Ethernet up | `INFRA_STATE` flags | RX suppresses MQTT requests; battery/power data continues to flow |
| ESP-NOW link lost | `EspNowConnectionManager` backoff + TX channel-hopping | TX rediscovers; RX waits; clean reconnect then INFRA_STATE exchange |
| Both ESP-NOW + Ethernet lost | Staged recovery: ESP-NOW first, then Ethernet services via INFRA_STATE | No burst; requests gated progressively as each service recovers |
| TX reboots unexpectedly | `uptime_ms` decrease detection in `TxInfraShadow` | RX invalidates shadow immediately; restarts init sequence |
| RX reboots during TX Ethernet outage | `INFRA_STATE_REQUEST` in RX init sequence | TX replies with current state; RX never sends blocked requests |
| Old TX firmware (no `INFRA_STATE`) | Conservative shadow defaults after 15s | Battery/power data flows; infra requests suppressed; WARN logged |
| NO_MEM pressure during reconnect | Priority queues outbound (CONTROL P0 immune to starvation) | Heartbeat/INFRA_STATE/ACK always delivered; telemetry shed first |
| INFRA_STATE arrives same burst as data | Inbound priority queue (§6.1.1) | Worker drains P0 first; shadow updated before any data handler fires; no futile requests |
| Discovery contention (dual owner) | Single discovery owner enforcement (P1) | No broadcast peer collision; deterministic channel lock |
| Reconnect burst (RX floods requests) | Shadow gating + burst shaping | Only permitted requests sent; init is staged and non-bursty |
| Heartbeat delayed (not lost) | Shadow stale check → `INFRA_STATE_REQUEST` sent | RX refreshes shadow before sending infrastructure requests |
| Simultaneous power cycle (both devices) | Boot-order-agnostic discovery + INFRA_STATE-first init | Correct final state regardless of which device boots first |

---

## Files reviewed for this analysis

- `esp32common/espnow_common_utils/connection_manager.cpp`
- `esp32common/espnow_common_utils/channel_manager.cpp`
- `esp32common/espnow_common_utils/espnow_tx_scheduler.cpp`
- `esp32common/espnow_common_utils/espnow_standard_handlers.cpp`
- `esp32common/espnow_phase0/reconnection_backoff.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/main.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_connection_handler.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_state_machine.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/heartbeat_manager.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_handler.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_routes.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_send_guard.cpp`
- `espnowreceiver_2/src/main.cpp`
- `espnowreceiver_2/src/config/runtime_task_startup.cpp`
- `espnowreceiver_2/src/espnow/espnow_tasks.cpp`
- `espnowreceiver_2/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_2/src/espnow/rx_heartbeat_manager.cpp`
- `espnowreceiver_2/src/espnow/rx_state_machine.cpp`
- `espnowreceiver_LCD/src/runtime/runtime_task_startup.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_runtime_ingress.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_runtime_messages.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp`
- `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp`
- `espnowreceiver_LCD/src/espnow/rx_state_machine.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_send.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_settings_sync.cpp`
- `espnowreceiver_2/src/espnow/espnow_settings_sync.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/network/ethernet_manager.cpp`
- `esp32common/espnow_common_utils/espnow_message_router.cpp`

**Advanced Architectural Analysis Added (2026-04-22):**

Comprehensive architectural improvement recommendations:
- **§6.1:** Priority queue architecture with per-subtype send cadence for control/discovery/data/monitoring
- **§6.2:** Ethernet/ESP-NOW decoupling via `EspnowDualGate` pattern
- **§6.2.1:** ESP-NOW Infrastructure State Messaging Protocol (`INFRA_STATE` embedded in heartbeat, `TxInfraShadow`, TX reboot detection, firmware fallback)
- **§6.3:** Master state machine unification (8 states, single source of truth)
- **§6.4:** espnowreceiver_2 alignment with LCD (ingress architecture, message routing, validation)
- **§7:** Revised robustness roadmap (P0/P1/P2/P3 phased delivery)
- **§8:** Production readiness timeline (6–8 weeks)
- **§9:** Failure mode coverage table (13 failure scenarios, expected behaviour for each)

**Implementation Effort:**
- **P0 (Foundation, Weeks 1–2):** 34 hours
    - Ethernet decoupling (3h), Priority queues (12h), _2 alignment (6h), `INFRA_STATE` messaging (8h), inbound priority queue (5h)
- **P1 (Master FSM, Weeks 3–4):** 20 hours
  - Design + migration of state machine consolidation
- **P2 (Robustness, ongoing):** 9 hours
  - Control-plane prioritization, burst shaping, discovery consolidation
- **P3 (Observability, continuous):** 15 hours
  - Enhanced telemetry, integration tests

**Expected Outcomes:**
✅ ACK/heartbeat immune to telemetry-induced NO_MEM pressure
✅ Transmitter reconnect deterministic and backoff-aware  
✅ Coordinated Ethernet + ESP-NOW co-recovery
✅ Aligned, testable, maintainable codebases
✅ Improved diagnostics and failure visibility

---

## Implementation Progress (Phased Rollout)

### Phase 1 — TX heartbeat decoupling from Ethernet gate ✅ Completed (2026-04-22)

**Implemented changes:**
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/heartbeat_manager.cpp`
    - Removed Ethernet readiness gating from `HeartbeatManager::tick()`.
    - Heartbeat keepalive now depends only on ESP-NOW connected state.
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/heartbeat_manager.h`
    - Removed unused legacy constant `MAX_UNACKED_HEARTBEATS`.
    - Updated stale responsibility comments to match current behavior.

**Legacy/redundant code removed in this phase:**
- Removed obsolete include dependency on `EthernetManager` from TX heartbeat manager.
- Removed dead, unused unacked-heartbeat threshold constant.

**Behavioral result:**
- Ethernet outages no longer suppress heartbeat keepalive traffic.
- RX can maintain ESP-NOW liveness while using infra flags/gates for infrastructure-dependent requests.

### Phase 2 — Shared outbound priority + cadence scheduler ✅ Completed (2026-04-22)

**Implemented changes:**
- `esp32common/espnow_common_utils/espnow_tx_scheduler.cpp`
    - Replaced legacy single FIFO queue with 4 strict-priority queues (CONTROL/DISCOVERY/DATA/MONITORING).
    - Added per-message subtype cadence policy (`min_gap_ms`) and retry policy overrides.
    - Added defer/requeue behavior when messages are not yet cadence-eligible.
    - Added lossy overflow shedding for non-control priorities (drop oldest then enqueue new).
    - Added per-priority enqueue-drop telemetry and cadence-defer telemetry.
- `esp32common/espnow_common_utils/espnow_tx_scheduler.h`
    - Extended `Stats` with cadence and per-priority drop counters.

**Legacy/redundant code removed in this phase:**
- Removed the legacy single-queue (`g_queue`) scheduler path in favor of explicit priority-tier queues.

**Behavioral result:**
- Control-plane traffic is no longer blocked behind bulk data in the shared scheduler.
- Outbound send timing now follows subtype cadence policy rather than pure FIFO timing.

### Phase 3 — Next implementation target

- Implement explicit `INFRA_STATE` transport and `TxInfraShadow` in both receivers.
- Gate receiver init/request flow in both `_2` and `_LCD` using shadow freshness and capability flags.
- Remove legacy request paths that assume infrastructure availability without `INFRA_STATE` validation.

---

**Document Status:** ✅ Complete and ready for implementation planning
**Last Updated:** 2026-04-22
**Review Target:** May 2026 sprint planning

---

## Addendum — MQTT + ESP-NOW Coexistence Analysis (2026-04-22)

### Problem Statement

When `task_mqtt_client` is re-enabled in `espnowreceiver_LCD`, the system produces a flood of `ESP_ERR_ESPNOW_NO_MEM` errors on outbound `msg_ack` (type=1, len=6) and MQTT broker connection fails with `state=-2`. This document section analyses the root causes and specifies the full multi-layer fix so that MQTT and ESP-NOW can coexist without radio degradation.

The previous working state in `espnowreceiver_2` (which runs MQTT alongside ESP-NOW with no `NO_MEM` issues) provides a confirmed reference baseline.

---

### Root Cause Analysis

#### RC-1 — WiFi Driver TX Buffer Pool Exhaustion

`esp_now_send()` and MQTT TCP data (SYN, MQTT CONNECT, TCP ACKs, MQTT keepalive) both compete for the same ESP32 WiFi driver dynamic TX buffer pool (`CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM`, default 32 slots). Each buffer slot is ~1600 bytes.

When MQTT attempts a broker connection, the WiFi driver emits a TCP SYN packet, TCP ACK, MQTT CONNECT packet, and subsequent keepalives in a short burst. If simultaneous ESP-NOW ACK frames are being queued (e.g., during a probe burst from the transmitter), the pool saturates and `esp_now_send()` returns `ESP_ERR_ESPNOW_NO_MEM` before the frame ever reaches the internal ESP-NOW TX queue.

**Evidence:** `type=1 len=6` is `msg_ack` (the smallest ESP-NOW frame). The failure is happening at the WiFi driver level, not at the application scheduler level.

**Comparison:** `espnowreceiver_LCD/sdkconfig.defaults` currently sets `CONFIG_ESP_WIFI_ESPNOW_MAX_PENDING_LEN=20` (already doubled from default 10), but the underlying TX buffer count remains at the platform default of 32.

#### RC-2 — LCD TX Scheduler Under-provisioned

The LCD TX scheduler is deliberately constrained (`queue_depth=4`) to minimise internal RAM during reconnect-stability work. With `queue_depth=4`, the priority queue formula produces:

| Queue | Formula | Effective depth |
|-------|---------|----------------|
| CONTROL (msg_ack) | 30% of 4 = 1.2 → 1 | **1** |
| DISCOVERY (msg_probe) | 20% of 4 = 0.8 → 1 (clamped) | **1** |
| DATA | 40% of 4 = 1.6 → 1 | **1** |
| MONITORING | remainder = 1 | **1** |

A CONTROL queue of depth 1 means any second ACK arriving before the first is sent gets dropped from the scheduler. Combined with RC-1 (direct path also failing), ACK loss is near-certain under any concurrent load.

**Reference comparison:** `espnowreceiver_2` uses `queue_depth=24` (CONTROL depth ~7) and `no_mem_retry_attempts=6`. It does not exhibit NO_MEM under MQTT load.

#### RC-3 — Probe ACK Suppression Too Aggressive for MQTT Load

Current LCD connected-state suppression is 120 ms. The transmitter sends probes periodically; under reconnect the rate can approach 80–250 ms. This can generate 5–8 ACK attempts per second. Each attempt goes through the direct `esp_now_send()` path first (up to 3 retries with 2 ms delays), producing up to 24 `esp_now_send()` calls per second. Under MQTT TCP load, this is sufficient to keep the TX buffer pool marginal.

#### RC-4 — MQTT Task Pinned to Core 1 (Application Core)

All LCD tasks are pinned to Core 1 (`WORKER_CORE = 1`). The WiFi and lwIP stacks run on Core 0. When MQTT calls `mqtt_client_.connect()` or `mqtt_client_.loop()` from Core 1, the socket operations wake the lwIP event loop on Core 0 via inter-core interrupt. The subsequent TCP packet TX then contends with ESP-NOW TX already queued from Core 1 callbacks — both stacks are fighting to push frames through the same WiFi driver queue but from different CPU contexts, increasing collision probability.

**Reference comparison:** `espnowreceiver_2` has the same MQTT task on `WORKER_CORE = 1` but larger TX queues absorb the contention — this is a contributing factor but not the sole cause.

#### RC-5 — MQTT Connection Attempted During ESP-NOW Reconnect Windows

The most damaging scenario is MQTT attempting a TCP broker connection at the same time ESP-NOW is in `CONNECTING` state (channel-hopping, high probe/ACK rate). These are both radio-intensive phases. There is no gate between them. The combined TX burst (probe broadcasts + ACK responses + TCP SYN) reliably saturates the TX buffer pool.

#### RC-6 — `register_transmitter_mac()` Logs on Every Unsuppressed Probe

`TransmitterIdentity::register_mac()` emits `LOG_INFO("TX_MGR", "MAC registered: %s")` on **every call**, regardless of whether the MAC has changed. With 120 ms probe suppression, this fires 8 times per second during steady-state CONNECTED operation. Although not directly causing NO_MEM, it adds CPU and UART I/O load at exactly the wrong time, and obscures real error logs.

---

### Fix Architecture

All six root causes require coordinated changes across five files. No single change is sufficient; all must be applied together.

```
┌──────────────────────────────────────────────────────────────────┐
│                  espnowreceiver_LCD — Fix Map                    │
├──────────────────┬───────────────────────────────────────────────┤
│ Root Cause       │ Fix                                           │
├──────────────────┼───────────────────────────────────────────────┤
│ RC-1 TX pool     │ sdkconfig.defaults:                           │
│ exhaustion       │   CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=48    │
├──────────────────┼───────────────────────────────────────────────┤
│ RC-2 Scheduler   │ runtime_task_startup.cpp:                     │
│ under-provisioned│   queue_depth 4 → 16                          │
│                  │   no_mem_retry_attempts 3 → 6                 │
├──────────────────┼───────────────────────────────────────────────┤
│ RC-3 ACK burst   │ espnow_runtime_routes.cpp:                    │
│ rate too high    │   min_interval_ms CONNECTED: 120 → 500 ms     │
├──────────────────┼───────────────────────────────────────────────┤
│ RC-4 MQTT on     │ task_config.h: MQTT_CORE = 0                  │
│ wrong core       │ runtime_task_startup.cpp: pin MQTT to Core 0  │
├──────────────────┼───────────────────────────────────────────────┤
│ RC-5 No MQTT/    │ mqtt_task.cpp (LCD):                          │
│ ESP-NOW gate     │   Skip loop/connect when ESP-NOW ≠ CONNECTED  │
│                  │   Disconnect MQTT if ESP-NOW drops            │
├──────────────────┼───────────────────────────────────────────────┤
│ RC-6 Log spam    │ transmitter_identity.cpp:                     │
│                  │   Guard LOG_INFO to only emit on MAC change   │
└──────────────────┴───────────────────────────────────────────────┘
```

#### Why queue_depth=16 is sufficient for LCD

With `queue_depth=16`, the priority queue formula gives:
- CONTROL: 30% of 16 = 4 (ACK + heartbeat)
- DISCOVERY: 20% of 16 = 3 (probe)
- DATA: 40% of 16 = 6 (normal data)
- MONITORING: remainder = 3

CONTROL queue depth 4 means 4 ACK frames can be buffered while the direct send path is temporarily busy with MQTT TCP. With `no_mem_retry_attempts=6` and retry delays up to 24 ms, this covers a 150 ms MQTT TCP burst window cleanly.

#### Why 500 ms connected-state ACK suppression is safe

The transmitter's reconnect discovery cycle uses a probe interval of at minimum `TimingConfig::ANNOUNCEMENT_INTERVAL_MS`. In the connected state, the transmitter sends heartbeats (not probes) for keepalive. Probes in the CONNECTED state arrive only when the transmitter is re-validating the channel (typically every 1–5 seconds). Responding to the first probe per 500 ms window is sufficient to maintain channel lock; suppressing duplicates within that window eliminates the ACK burst pattern entirely.

#### Why gating MQTT on ESP-NOW state prevents RC-5 precisely

The state machine gate means:
- During CONNECTING / STALE / IDLE: MQTT is paused (no TCP, no keepalives, no reconnect timer)
- On transition to CONNECTED: MQTT starts after the next poll cycle (100 ms delay)
- On transition from CONNECTED to any other state: `MqttClient::disconnect()` is called to terminate the TCP socket immediately, freeing TX buffer slots for ESP-NOW recovery ACKs

This creates a clean radio resource hand-off: reconnect gets full TX budget; MQTT only runs when the radio is stable.

#### Why Core 0 for MQTT task

`PubSubClient::loop()` is a thin wrapper over `WiFiClient::read()` and `write()`, which are lwIP socket calls. On ESP32, lwIP's socket layer is interrupt-driven from the WiFi event loop on Core 0. When MQTT calls `connect()` or `loop()` from Core 1, every socket operation triggers an inter-core IPC call to schedule the lwIP work on Core 0. Running the MQTT task directly on Core 0 eliminates this inter-core handoff and keeps MQTT TCP entirely within the WiFi subsystem's own CPU context, reducing the scheduling pressure on Core 1 (which handles ESP-NOW message processing).

---

### Why `espnowreceiver_2` Does Not Exhibit This Problem

| Parameter | espnowreceiver_2 | espnowreceiver_LCD |
|-----------|------------------|--------------------|
| TX scheduler queue_depth | 24 | 4 (insufficient) |
| no_mem_retry_attempts | 6 | 3 (insufficient) |
| CONTROL queue depth | ~7 | 1 (insufficient) |
| MQTT task status | Always enabled | Disabled (workaround) |
| MQTT/ESP-NOW state gate | None (but queues absorb it) | Not present |
| WiFi TX buffer | Default (32) | Default (32) |

`espnowreceiver_2` survives because its larger scheduler queues absorb the burst window without ACK loss. The LCD variant was specifically constrained to save RAM for the LVGL framebuffer, creating the imbalance. The fix restores parity on the critical parameters while adding the state-gate and core-affinity improvements that were never present in either variant.

---

### Implementation Checklist

- [ ] `espnowreceiver_LCD/sdkconfig.defaults` — Add `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=48`
- [ ] `espnowreceiver_LCD/include/task_config.h` — Add `constexpr uint8_t MQTT_CORE = 0`
- [ ] `espnowreceiver_LCD/src/runtime/runtime_task_startup.cpp` — Raise `queue_depth` to 16, `no_mem_retry_attempts` to 6; re-enable `task_mqtt_client` pinned to `MQTT_CORE`
- [ ] `espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp` — Increase connected-state min ACK suppression to 500 ms
- [ ] `espnowreceiver_LCD/src/mqtt/mqtt_task.cpp` — Gate MQTT operations on `RxStateMachine::ConnectionState::CONNECTED` or `ACTIVE`; call `MqttClient::disconnect()` on state drop
- [ ] `espnowreceiver_LCD/lib/webserver_lcd/utils/transmitter_identity.cpp` — Guard `LOG_INFO("TX_MGR", "MAC registered:")` to only emit when MAC actually changes

### Phase 4 — MQTT + ESP-NOW Coexistence ✅ Implemented (2026-04-22)

**Implemented changes:**

- `espnowreceiver_LCD/sdkconfig.defaults`
    - Added `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=48` — increases shared WiFi driver TX buffer pool from default 32 to 48, giving headroom for concurrent ESP-NOW and TCP frames without pool exhaustion (RC-1).

- `espnowreceiver_LCD/include/task_config.h`
    - Added `constexpr uint8_t MQTT_CORE = 0` — dedicated core constant for MQTT task so it runs on the WiFi/lwIP core, eliminating inter-core IPC overhead for socket operations (RC-4).

- `espnowreceiver_LCD/src/runtime/runtime_task_startup.cpp`
    - Raised TX scheduler `queue_depth` from 4 → 16 (CONTROL queue depth 1 → 4) (RC-2).
    - Raised `no_mem_retry_attempts` from 3 → 6 (RC-2).
    - Re-enabled `task_mqtt_client` pinned to `MQTT_CORE` (Core 0) (RC-4).

- `espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp`
    - Increased connected-state probe-ACK suppression interval from 120 ms → 500 ms, reducing ACK send rate from ~8/s to ≤2/s in steady CONNECTED state (RC-3).

- `espnowreceiver_LCD/src/mqtt/mqtt_task.cpp`
    - Added include of `espnow/rx_state_machine.h`.
    - Added guard: MQTT loop/connect only executes when ESP-NOW state is `CONNECTED` or `ACTIVE`.
    - Added: calls `MqttClient::disconnect()` if ESP-NOW drops out of connected states, freeing TX buffer slots for reconnect ACKs (RC-5).

- `espnowreceiver_LCD/lib/webserver_lcd/utils/transmitter_identity.cpp`
    - Guard `LOG_INFO("TX_MGR", "MAC registered:")` to only emit when the registered MAC actually changes (RC-6).

**Behavioral result:**
- `task_mqtt_client` runs permanently on Core 0 alongside the WiFi/lwIP stack.
- MQTT broker connect/loop only executes while ESP-NOW is in CONNECTED or ACTIVE state.
- MQTT disconnects immediately when ESP-NOW state drops, returning TX buffers for reconnect use.
- Probe-ACK rate in CONNECTED state reduced to ≤ 2/s (safe for both ESP-NOW and MQTT TCP).
- TX scheduler can buffer 4 concurrent ACK frames before any are dropped.
- WiFi TX buffer pool (48 slots) provides enough margin for MQTT TCP bursts without evicting ESP-NOW frames.
- `/events` page receives MQTT event log batches as intended when ESP-NOW link is stable.
