# ESP-NOW Full Codebase Failure Analysis (2026-04-27)

## Purpose

This report is a full codebase-level analysis of why ESP-NOW reconnect and stability still fail intermittently under load, and what must be changed to make reconnect deterministic.

This analysis covers:

- shared/common ESP-NOW layers in `esp32common`
- transmitter stack in `ESPnowtransmitter2`
- both receiver variants (`espnowreceiver_LCD` and `espnowreceiver_2`)
- scheduler, discovery, heartbeat, connection state, and MQTT coexistence paths

---

## High-Confidence Summary

The failures are **not one bug**. They are a system interaction problem:

1. reconnect-critical frames (ACK + heartbeat ACK) compete in a single Wi-Fi TX buffer pool,
2. queueing and ownership are still inconsistent across the wider codebase,
3. ingress queues can still drop the exact discovery ACK that would end scanning,
4. state semantics and gating differ between modules/receiver variants.

The result is a reproducible field pattern:

- TX scans,
- RX sees PROBE,
- ACK path still hits `ESP_ERR_ESPNOW_NO_MEM` in bad windows,
- TX misses the lock window and continues scanning.

---

## What Is Already Correct (and should be kept)

The following major fixes are present and are directionally correct:

- discovery ACK now routed through scheduler in shared standard handler
- heartbeat ACK paths on both receivers moved to scheduler-only send
- scheduler supports control-only mode and non-control purge
- control priority uses front-of-queue behavior
- receiver quiet mode is entered immediately on reconnect PROBE
- transmitter discovery uses adaptive/two-phase scan

These were necessary. They are not yet sufficient for full robustness.

---

## Full-System Failure Mechanisms

## 1) Single radio / shared TX pool remains the fundamental bottleneck

All ESP-NOW and MQTT/TCP traffic still contend in the same Wi-Fi driver TX resources. During reconnect windows, tiny ACK frames can still lose allocation races if any non-critical traffic bursts.

This is expected on single-radio ESP32 and is the base physical constraint.

---

## 2) Send ownership is still mixed in the wider codebase (architectural drift)

Even after ACK-path fixes, many paths still call raw `esp_now_send()` directly instead of going through one arbiter path.

Examples found in current tree include:

- receiver web/API handlers (network/settings/control/sse paths)
- transmitter settings ACK/notification paths
- transmitter data cache flush path
- legacy/common discovery and send utility paths

Impact:

- ordering and priority guarantees are bypassed,
- control-only mode cannot fully protect reconnect windows,
- burst contention can reappear from “side-channel” senders.

This is the biggest remaining architecture risk.

---

## 3) Discovery ACK can still be lost after RF receipt (ingress bottleneck)

Transmitter callback mirrors PROBE/ACK into discovery queue via non-blocking enqueue (`xQueueSend(..., 0)`).

If queue is full in that moment, a valid over-the-air ACK is dropped before discovery logic consumes it.

Current counters help visibility, but this remains a functional drop path exactly in reconnect-critical windows.

---

## 4) Receiver implementation drift between LCD and receiver_2

Receiver LCD path suppresses generic link-activity updates for discovery traffic.

Receiver 2 worker still applies `on_link_activity()` before discovery filtering in the main queue consumer path. This semantic difference can influence freshness/retry behavior and makes behavior less deterministic across the two receiver variants.

---

## 5) Timing geometry amplifies every miss

Current reconnect geometry:

- 1 second dwell per channel
- 13-channel scan
- one missed ACK window can cost ~13 seconds to next chance

So even short `NO_MEM` bursts or queue drops produce long reconnect delays.

---

## 6) Status semantics are still multi-layer and can mislead diagnosis

There are still distinct notions of:

- connection-manager `CONNECTED`
- RX state-machine active/data freshness
- heartbeat freshness gate (MQTT coexistence)

These can legitimately disagree in transition windows. Without a dedicated reconnect state marker, logs can look contradictory.

---

## 7) Observability is improved but still incomplete for closure

Needed counters still fragmented across modules. We still lack one concise reconnect report per attempt that merges:

- first PROBE seen timestamp
- ACK enqueue attempts/failures by class
- `NO_MEM` by message type
- discovery-queue mirror drops
- reconnect duration outcome

Without this, “fixed vs improved” remains hard to prove quickly.

---

## Root Cause Statement (Final)

The dominant failure is an **ACK-starvation system condition** under constrained Wi-Fi TX resources, amplified by narrow scan windows and residual multi-owner send behavior.

In short: the platform needs strict, enforced send arbitration and reconnect-window isolation across the entire active send surface, not only in the main ACK handlers.

---

## Remediation Plan

## P0 (must do next)

1. **Enforce single send-owner architecture in active runtime paths**
   - Route all receiver-originated ESP-NOW sends through a unified wrapper (scheduler/guarded owner).
   - Remove direct `esp_now_send()` from active receiver API handlers and transmitter runtime control paths.
   - Keep direct send only in explicitly whitelisted boot/low-level code.

2. **Close discovery ingress loss path on transmitter**
   - Replace blind non-blocking mirror behavior with deterministic strategy:
     - dedicated high-priority discovery queue reserve, and/or
     - controlled overwrite/drop-oldest policy for discovery-only mirror path, and
     - explicit drop reason counters.

3. **Unify receiver discovery activity semantics**
   - Align `espnowreceiver_2` with LCD behavior: discovery frames must not feed generic activity/liveness paths.

4. **Harden quiet-mode contract**
   - Keep control-only mode asserted from first reconnect PROBE until heartbeat freshness recovery + hold conditions are satisfied.
   - Explicitly gate non-critical web/API initiated sends while quiet mode is active.

## P1 (next stabilization)

5. **Create one reconnect diagnostic block per attempt**
   - start/end timestamp
   - first probe seen
   - ACK enqueue success/fail counts
   - `ESP_ERR_ESPNOW_NO_MEM` counts by message type
   - discovery mirror drop counts
   - reconnect success/failure reason

6. **Tighten reconnect scan policy adaptively**
   - keep phase-1 fast sweep
   - increase weighted dwell only where discovery evidence exists
   - avoid global dwell inflation that harms mean reacquire time.

7. **Introduce explicit receiver state label for reconnect window**
   - `RECONNECT_QUIET` or equivalent
   - surface in status logs/UI to eliminate state ambiguity.

## P2 (architecture debt retirement)

8. **Add compile-time guardrails**
   - central send facade in shared code,
   - static checks/review rule: no new runtime `esp_now_send()` outside approved files.

9. **Converge duplicate receiver logic**
   - extract common reconnect/liveness policy into shared module to prevent future variant drift.

---

## Concrete File Targets for P0

### Shared/common

- `esp32common/espnow_common_utils/espnow_tx_scheduler.cpp`
- `esp32common/espnow_common_utils/espnow_standard_handlers.cpp`
- `esp32common/espnow_transmitter/espnow_transmitter.cpp`

### Receiver LCD

- `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp`
- `espnowreceiver_LCD/src/mqtt/mqtt_task.cpp`
- `espnowreceiver_LCD/lib/webserver_lcd/api/*.cpp` (replace raw sends)

### Receiver 2

- `espnowreceiver_2/src/espnow/espnow_tasks.cpp` (discovery/activity parity)
- `espnowreceiver_2/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_2/src/espnow/rx_heartbeat_manager.cpp`
- `espnowreceiver_2/src/mqtt/mqtt_task.cpp`
- `espnowreceiver_2/lib/webserver/api/*.cpp` (replace raw sends)

### Transmitter

- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/heartbeat_manager.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_espnow.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/data_cache.cpp`

---

## Acceptance Criteria (Definition of Done)

1. Reconnect success rate: >= 99% over 100 forced reconnect cycles.
2. Median reconnect time materially improved vs current baseline.
3. `ESP_ERR_ESPNOW_NO_MEM` on discovery/heartbeat ACK reduced to near-zero during reconnect windows.
4. Discovery queue mirror drops observed and bounded (no silent-loss ambiguity).
5. No direct runtime send paths remain outside approved send-owner modules.
6. LCD and receiver_2 show equivalent reconnect behavior under identical test conditions.

---

## Immediate Practical Next Step

Implement P0 items first (single-owner enforcement + discovery ingress hardening + receiver parity), then run one controlled reconnect stress campaign with paired TX/RX logs and per-attempt diagnostics.

This is the shortest path from “improved but intermittent” to predictable reconnect behavior.
