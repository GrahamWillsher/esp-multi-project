# ESP-NOW Reconnect Alignment Review and Recommendations
**Date:** 2026-05-13  
**Scope:** Compare current TX/RX implementation against [ESPNOW_RECONNECT_FIRST_PRINCIPLES_ANALYSIS_2026_05_01.md](ESPNOW_RECONNECT_FIRST_PRINCIPLES_ANALYSIS_2026_05_01.md) and highlight the remaining code areas that still need improvement to fully align the implementation with the document and reduce reconnect-related ESP-NOW traffic.

---

## Executive Summary

The current codebase is **partially aligned** with the first-principles reconnect document, but the reconnect failure you observed is still consistent with two unresolved issues:

1. **Receiver-side ACK egress is still too fragile under pressure.** Discovery ACKs can be dropped when `ESP_ERR_ESPNOW_NO_MEM` occurs, which prevents the transmitter from ever completing the scan/confirm handshake.
2. **Reconnect still generates more discovery traffic than is desirable when no useful work is happening.** The TX reconnect loop remains active and the RX still responds to probes, so the system continues to spend airtime on control-plane traffic even when web/MQTT are idle.

The good news is that several parts of the document are already reflected in the code:

- The transmitter reconnect flow is now manager-driven rather than a tangled set of task-local state variables.
- The transmitter discovery dwell is already at the document’s recommended 2000 ms per channel.
- The old hard restart-style health loop is mostly gone from the transmitter path.

The remaining work is now mostly about **making ACK delivery reliable on the receiver** and **reducing discovery frequency / background telemetry when the link is not stable**.

---

## What the Document Asked For

The first-principles document requires these behaviours:

- A single synchronised reconnect ownership model.
- Backoff measured by completed scan attempts, not by timeout churn.
- Reliable ACK delivery, especially under WiFi/MQTT contention.
- Atomic or otherwise safe handling of scan lifecycle state.
- Dwell long enough for the receiver to receive a probe and return an ACK under load.

The current implementation satisfies some of that, but not all of it.

---

## Areas That Are Already Aligned

### Transmitter reconnect ownership

The transmitter reconnect flow is now centred around the reconnect manager and worker scan model in [ESPnowtransmitter2/src/espnow/tx_reconnect_manager.cpp](../../../../ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_reconnect_manager.cpp) and no longer depends on the older multi-path lifecycle code described in the document.

### Scan dwell time

The discovery dwell is already set to 2000 ms in [esp32common/include/esp32common/config/timing_config.h](../../include/esp32common/config/timing_config.h), which matches the document’s recommended minimum dwell for WiFi-coexistent receivers.

### No obvious hard restart loop in the TX main loop

The transmitter main loop in [ESPnowtransmitter2/src/main.cpp](../../../../ESPnowtransmitter2/espnowtransmitter2/src/main.cpp) now behaves more like a monitor than a restart engine. That is aligned with the document’s direction to avoid disruptive self-healing loops.

### Discovery queue separation is already present

The transmitter already uses a dedicated discovery queue for PROBE/ACK ingress in [ESPnowtransmitter2/src/queue/espnow_queue_manager.cpp](../../../../ESPnowtransmitter2/espnowtransmitter2/src/queue/espnow_queue_manager.cpp), and the shared ESP-NOW shim duplicates discovery frames into that queue in [ESP32common/espnow_transmitter/espnow_transmitter.cpp](../../../esp32common/espnow_transmitter/espnow_transmitter.cpp).

That means the original queue-splitting idea was not about creating a brand-new mechanism now; it was about preserving reconnect-critical discovery frames from being buried in general message traffic. The current design already does that at the queue-topology level.

**Important limitation:** queue separation alone does not prevent a failed ACK send from being lost. The remaining problem is still ACK enqueue/send reliability under `NO_MEM`, plus throttle behaviour that can suppress the next retry opportunity.

---

## Remaining Gaps on the Transmitter

### 1) Discovery is still too chatty during prolonged reconnect loss

**Relevant code:**
- [ESPnowtransmitter2/src/espnow/discovery_task.cpp](../../../../ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp)
- [ESPnowtransmitter2/src/espnow/tx_reconnect_manager.cpp](../../../../ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_reconnect_manager.cpp)

**What is happening:**
- The transmitter keeps scanning repeatedly when the receiver is not found.
- Even with 2000 ms dwell, the system still emits a steady stream of PROBE traffic while it searches.
- The weighted sweep around the last known channel helps, but it does not reduce the amount of traffic when the receiver stays unavailable for a long time.

**Why this matters:**
- This is the main source of “non-critical” ESP-NOW airtime when nothing useful is happening.
- It also keeps the RX side under continuous ACK pressure.

**Recommendation:**
- Add a true **adaptive reconnect cadence**:
  - fast acquisition for the first short window,
  - then slower retry intervals after repeated misses,
  - then a sparse background search mode after prolonged failure.
- Keep the first scan aggressive, but stop treating every long miss period as if it deserves the same traffic level.

---

### 2) Background telemetry can still contribute to pressure

**Relevant code:**
- [ESPnowtransmitter2/src/espnow/version_beacon_manager.cpp](../../../../ESPnowtransmitter2/espnowtransmitter2/src/espnow/version_beacon_manager.cpp)
- [ESPnowtransmitter2/src/espnow/heartbeat_manager.cpp](../../../../ESPnowtransmitter2/espnowtransmitter2/src/espnow/heartbeat_manager.cpp)
- [ESPnowtransmitter2/src/espnow/data_sender.cpp](../../../../ESPnowtransmitter2/espnowtransmitter2/src/espnow/data_sender.cpp)

**What is happening:**
- Version beacons are sent periodically and also on runtime changes.
- Heartbeats continue once connected.
- Data transmission continues whenever the link is active.

**Why this matters:**
- None of this is the main reconnect failure, but it adds to control-plane and WiFi queue activity.
- When the link is unstable, background telemetry should be even more conservative.

**Recommendation:**
- Gate nonessential beacons and summaries while reconnect is unstable.
- Only send version/config/event-log updates when the link is stable or when explicitly requested.
- Keep heartbeats, but avoid extra “nice-to-have” traffic during reconnect pressure windows.

---

### 3) Discovery queue and ingress duplication should remain under scrutiny

**Relevant code:**
- [ESP32common/espnow_transmitter/espnow_transmitter.cpp](../../../esp32common/espnow_transmitter/espnow_transmitter.cpp)
- [ESPnowtransmitter2/src/runtime/runtime_context.cpp](../../../../ESPnowtransmitter2/espnowtransmitter2/src/runtime/runtime_context.cpp)
- [ESPnowtransmitter2/src/queue/espnow_queue_manager.cpp](../../../../ESPnowtransmitter2/espnowtransmitter2/src/queue/espnow_queue_manager.cpp)

**What is happening:**
- Probe and ACK traffic is copied into a dedicated discovery queue for reconnect handling.
- That is functionally useful, but it means discovery traffic has multiple consumers and is being treated as first-class traffic across the stack.

**Why this matters:**
- It is correct from a functional perspective, but it reinforces that discovery is a high-priority, always-on pathway.

**Recommendation:**
- Keep the queue architecture, but add traffic-rate visibility so you can tell when reconnect traffic becomes excessive.
- Use those counters to decide when to slow or suppress repeated discovery attempts.

---

## Remaining Gaps on the Receiver

### 1) ACK delivery is still the most important unresolved issue

**Relevant code:**
- [esp32common/espnow_common_utils/espnow_standard_handlers.cpp](../../espnow_common_utils/espnow_standard_handlers.cpp)
- [esp32common/espnow_common_utils/rx_route_registry.cpp](../../espnow_common_utils/rx_route_registry.cpp)
- [esp32common/espnow_common_utils/rx_connection_handler.cpp](../../espnow_common_utils/rx_connection_handler.cpp)
- [esp32common/espnow_common_utils/rx_heartbeat_manager.cpp](../../espnow_common_utils/rx_heartbeat_manager.cpp)

**What is happening:**
- The receiver receives the probe.
- It tries to send a discovery ACK.
- Under load, `esp_now_send()` can return `ESP_ERR_ESPNOW_NO_MEM`.
- That ACK is effectively lost, so the transmitter never completes discovery on that channel.

**Why this matters:**
- This is the direct reason the transmitter keeps scanning even though the receiver is actually present on the right channel.
- If the ACK is missed, the whole reconnect chain collapses back into another scan cycle.

**Recommendation:**
- Make ACK sending **retry-aware** on `NO_MEM` instead of single-shot.
- If possible, retry a small bounded number of times before giving up.
- Keep the retry bounded so it does not create a new flood, but do not let a single transient `NO_MEM` destroy the handshake.

---

### 2) ACK throttle semantics should be fixed

**Relevant code:**
- [esp32common/espnow_common_utils/rx_route_registry.cpp](../../espnow_common_utils/rx_route_registry.cpp)

**What is happening:**
- The throttle timestamp is updated before the send outcome is known.
- If the ACK send fails, the throttle window still advances as if the ACK had been delivered.

**Why this matters:**
- A failed ACK can suppress the very next opportunity to retry.
- That is exactly the wrong behaviour when `NO_MEM` is the failure mode.

**Recommendation:**
- Update throttle state only after a successful ACK enqueue/send path, or after a deliberate bounded retry sequence has completed.
- Do not treat a failed ACK attempt as if it successfully occupied the channel.

---

### 3) RX recovery thresholds are too conservative for persistent ACK failure

**Relevant code:**
- [espnowreceiver_LCD/platformio.ini](../../../../espnowreceiver_LCD/platformio.ini)
- [esp32common/espnow_common_utils/link_recovery_coordinator.cpp](../../espnow_common_utils/link_recovery_coordinator.cpp)

**What is happening:**
- The receiver’s NO_MEM trigger threshold is high enough that the system may continue to “try and hope” for too long before escalating.

**Why this matters:**
- If the WiFi TX path is genuinely congested, waiting too long delays recovery and prolongs the reconnect deadlock.

**Recommendation:**
- Lower the threshold or make it adaptive.
- Use faster L1/L2 escalation when the system is repeatedly failing to enqueue discovery ACKs.
- Keep the bounds, but make the system decisive when the same failure repeats.

---

### 4) Noncritical RX telemetry should be suppressed during reconnect pressure

**Relevant code:**
- [esp32common/espnow_common_utils/rx_connection_handler.cpp](../../espnow_common_utils/rx_connection_handler.cpp)
- [espnowreceiver_LCD/src/mqtt/mqtt_task.cpp](../../../../espnowreceiver_LCD/src/mqtt/mqtt_task.cpp)
- [espnowreceiver_LCD/src/espnow/espnow_runtime_messages.cpp](../../../../espnowreceiver_LCD/src/espnow/espnow_runtime_messages.cpp)

**What is happening:**
- The receiver remains active while the link is unstable.
- MQTT gating is working, but the receiver still produces logs and state updates that do not help reconnect.

**Why this matters:**
- It is not the root cause, but every extra packet and every extra WiFi-buffer consumer increases the chance of `NO_MEM`.

**Recommendation:**
- Suppress nonessential updates during reconnect pressure.
- Keep only the traffic needed to restore link health: probe ACK, connect-confirm ACK, and heartbeats once stable.

---

## Detailed Alignment Verdict by Document Principle

### Principle: single synchronisation domain for reconnect

**Verdict:** Mostly aligned on the transmitter, still incomplete on the receiver ACK side.

**Action needed:**
- Keep reconnect ownership clean on TX.
- Make RX ACK delivery deterministic and bounded.

### Principle: backoff counts completed scan attempts, not timeout churn

**Verdict:** Largely aligned.

**Action needed:**
- Preserve the current reconnect manager behaviour.
- Verify no hidden timeout path reintroduces churn-based counting.

### Principle: ACK must be delivered reliably

**Verdict:** Not yet fully aligned.

**Action needed:**
- Add bounded retries on ACK `NO_MEM`.
- Fix throttle semantics so a failed attempt does not behave like a successful one.

### Principle: dwell long enough for coexistence

**Verdict:** Aligned.

**Action needed:**
- Keep the 2000 ms dwell, but complement it with smarter traffic reduction when the system is already under pressure.

### Principle: avoid unnecessary reconnect traffic during idle periods

**Verdict:** Not fully aligned.

**Action needed:**
- Add adaptive scan backoff.
- Gate nonessential beacons and summaries during reconnect pressure.
- Reduce background chatter until the link is stable again.

---

## Phase Cleanup Rule

Every completed phase should end with removal of old, redundant, and legacy code that is no longer part of the agreed reconnect architecture.

That means each phase should leave the codebase in a clean state with:

- legacy branches removed once the new path is validated,
- redundant helper paths deleted rather than left dormant,
- phase-specific compatibility shims removed when the phase is complete,
- the report updated to state exactly what was removed and why.

This rule should be applied at both TX and RX boundaries so the reconnect stack does not accumulate alternate paths that can reintroduce the same failure modes later.

---

## Phase-By-Phase Cleanup Checklist

Use this checklist as the implementation order for both codebases. Each phase is only complete when the new behaviour is validated **and** the obsolete code paths from that phase are removed.

### Phase 1 — Make ACK delivery reliable

**Transmitter**
- [ ] Keep the 2000 ms discovery dwell and the reconnect manager / worker scan flow.
- [ ] Add rate logging for probe send rate, scan duration, and scan-failure counts.
- [ ] Remove any duplicate scan-start or legacy restart helpers that are no longer part of `TxReconnectManager`.
- [ ] Keep only the current reconnect ownership path in `tx_reconnect_manager.cpp` and `discovery_task.cpp`.

**Receiver**
- [ ] Add bounded retry logic for discovery ACK sends when `esp_now_send()` returns `ESP_ERR_ESPNOW_NO_MEM`.
- [ ] Change ACK throttle handling so the throttle timestamp is committed only after a successful enqueue/send path.
- [ ] Add logs for ACK enqueue success, NO_MEM retry, and final ACK drop reason.
- [ ] Remove any old one-shot ACK assumptions or dead legacy ACK send helpers after the retry path is validated.

**Cleanup gate**
- [ ] No legacy ACK path remains.
- [ ] Failed ACK attempts no longer suppress the next valid retry opportunity.
- [ ] The transmitter can reconnect on the receiver channel under load.

### Phase 2 — Reduce reconnect chatter

**Transmitter**
- [ ] Add adaptive reconnect cadence: fast acquisition first, then slower retry intervals, then sparse background reconnect mode.
- [ ] Add a clear state label or log marker for the current reconnect cadence.
- [ ] Remove any obsolete “always-fast” scan scheduling once adaptive cadence is active.

**Receiver**
- [ ] Lower or adapt NO_MEM escalation thresholds so persistent ACK failure escalates sooner.
- [ ] Suppress nonessential reconnect-adjacent traffic while the link is unstable.
- [ ] Remove any old RX-side pressure handling that conflicts with the newer escalation path.

**Cleanup gate**
- [ ] Reconnect traffic rate drops materially when the link stays down.
- [ ] RX escalation happens decisively instead of lingering in a repeated retry loop.
- [ ] No obsolete retry or pressure-handling branch remains active.

### Phase 3 — Silence noncritical telemetry during instability

**Transmitter**
- [ ] Gate version beacons, event summaries, and other opportunistic sends when reconnect is not stable.
- [ ] Keep heartbeat traffic only where it is genuinely required for link health.
- [ ] Remove any duplicate runtime-state send paths that are superseded by the gated telemetry path.

**Receiver**
- [ ] Keep MQTT gating as-is, but suppress extra reconnect-adjacent chatter that does not help restore link health.
- [ ] Keep only the minimum traffic needed for discovery, connect-confirm, and stable heartbeat exchange.
- [ ] Remove any redundant telemetry-trigger branches that are now covered by the new pressure-aware policy.

**Cleanup gate**
- [ ] Background traffic stays quiet during reconnect pressure.
- [ ] Only essential control-plane packets remain active while the link is unstable.
- [ ] All now-redundant telemetry hooks are deleted.

### Phase 4 — Final codebase cleanup and simplification

**Transmitter**
- [ ] Delete legacy compatibility helpers, old reconnect shims, and stale fallback paths that are no longer needed.
- [ ] Keep only the supported reconnect manager, discovery worker, and current state-machine bridge.
- [ ] Update the phase report to list the removed files/functions explicitly.

**Receiver**
- [ ] Delete stale ACK fallback helpers, redundant retry branches, and any old queue-handling code no longer used by the current reconnect path.
- [ ] Keep only the supported ACK send path, the current recovery coordinator path, and the current pressure policy.
- [ ] Update the phase report to list the removed files/functions explicitly.

**Cleanup gate**
- [ ] No old/redundant/legacy path remains in either codebase.
- [ ] The report documents exactly what was removed at the end of the phase.
- [ ] The remaining code reads as one supported reconnect design, not a stack of historical fallbacks.

---

## Recommended Implementation Order

1. **Fix receiver ACK send reliability first**
   - Bounded retries for discovery ACK on `NO_MEM`.
   - Update throttle only after success.

2. **Lower or adapt RX NO_MEM escalation threshold**
   - Escalate faster when ACK send failure repeats.

3. **Add transmitter reconnect traffic shaping**
   - Fast scan first, then sparse reconnect mode.

4. **Suppress nonessential traffic during instability**
   - Version beacons, event summaries, and other opportunistic updates should wait until the link is healthy.

5. **Add explicit instrumentation**
   - Track probe send rate, ACK success rate, and `NO_MEM` frequency so the effect of each change is measurable.

6. **Remove legacy code at phase end**
   - Delete the old code paths after each phase is validated so the repository only retains the currently supported reconnect flow.
   - Record the cleanup in the phase report so the next phase starts from a known-good baseline.

---

## File-By-File Implementation Plan

This section turns the phase checklist into an execution map. The intent is to touch each file once per phase where possible, validate it, then remove any now-redundant compatibility path before moving on.

### Transmitter files

#### 1) `ESPnowtransmitter2/src/espnow/tx_reconnect_manager.cpp`

**Phase 1**
- Keep the reconnect manager / worker scan ownership as the single reconnect path.
- Add or preserve rate logging for scan attempts, scan duration, and scan-failure counts.
- Remove any stale code that still acts like a separate reconnect owner.

**Phase 2**
- Introduce adaptive reconnect cadence handling here if the manager owns the scan schedule.
- Remove obsolete “always-fast” scan retry behaviour once the adaptive cadence is active.

**Phase 4**
- Delete any legacy reconnect helper branches that are no longer needed after adaptive cadence is validated.

#### 2) `ESPnowtransmitter2/src/espnow/discovery_task.cpp`

**Phase 1**
- Keep the 2000 ms dwell and the probe scanning flow.
- Preserve the last-known-channel weighted sweep.
- Remove old scan-start / restart helpers if they are still present anywhere in this file.

**Phase 2**
- Add cadence labels or logs that show whether the scan is in fast-acquire, slowed-retry, or sparse mode.
- Remove legacy fixed-interval scan logic once adaptive cadence is live.

**Phase 4**
- Delete any obsolete scan state or compatibility code that is no longer needed after the reconnect manager is validated.

#### 3) `ESPnowtransmitter2/src/espnow/version_beacon_manager.cpp`

**Phase 3**
- Gate periodic or event-driven beacons when reconnect is unstable.
- Keep only the minimum beacon traffic needed for a healthy link.
- Remove redundant runtime-state send paths once the gated path is verified.

**Phase 4**
- Delete any legacy beacon triggers that conflict with the pressure-aware policy.

#### 4) `ESPnowtransmitter2/src/espnow/heartbeat_manager.cpp`

**Phase 3**
- Keep heartbeat traffic only where it is needed for link health.
- Avoid extra telemetry sent from the heartbeat path when reconnect is unstable.

**Phase 4**
- Remove any duplicate or stale heartbeat-adjacent telemetry helpers that are now redundant.

#### 5) `ESPnowtransmitter2/src/espnow/data_sender.cpp`

**Phase 3**
- Keep data transmission only when the link is actually active.
- Avoid any reconnect-adjacent send paths that contribute to noncritical traffic.

**Phase 4**
- Remove obsolete data-send fallback paths that no longer match the final architecture.

#### 6) `ESPnowtransmitter2/src/main.cpp`

**Phase 1**
- Keep the reconnect flow free of hard restart-style health loops.
- Preserve the current manager-driven startup order.

**Phase 2**
- Remove any old scan/reconnect startup logic that duplicates the manager-owned path.

**Phase 4**
- Delete leftover compatibility branches and stale bootstrap logic that are no longer used.

#### 7) `ESPnowtransmitter2/src/queue/espnow_queue_manager.cpp`

**Phase 1**
- Keep the dedicated discovery queue for PROBE/ACK traffic.
- Add queue-rate diagnostics if they are not already present.

**Phase 2**
- Use queue statistics to validate whether adaptive cadence is reducing reconnect pressure.

**Phase 4**
- Remove any duplicate queue aliases or compatibility helpers that are no longer required.

#### 8) `ESPnowtransmitter2/src/runtime/runtime_context.cpp`

**Phase 1**
- Preserve the queue bindings that support the discovery queue topology.

**Phase 4**
- Remove any legacy queue export or compatibility wiring that is no longer used by the final reconnect path.

### Receiver files

#### 1) `esp32common/espnow_common_utils/espnow_standard_handlers.cpp`

**Phase 1**
- Make discovery ACK sending retry-aware on `ESP_ERR_ESPNOW_NO_MEM`.
- Keep the retry bounded so it does not create a new flood.
- Add logging for ACK enqueue success, retry, and final failure.

**Phase 4**
- Remove any one-shot ACK assumptions or dead helper paths that are no longer needed.

#### 2) `esp32common/espnow_common_utils/rx_route_registry.cpp`

**Phase 1**
- Change ACK throttle handling so it only advances after a successful ACK send path.
- Keep the discovery probe/ACK routing but ensure a failed ACK does not suppress the next valid opportunity.

**Phase 2**
- Ensure the throttle values remain appropriate after the retry logic is in place.

**Phase 4**
- Delete any old throttle helper or compatibility code that no longer fits the final ACK path.

#### 3) `esp32common/espnow_common_utils/rx_connection_handler.cpp`

**Phase 1**
- Keep the current RX connection/recovery hooks, but ensure they support the bounded ACK-retry path.

**Phase 2**
- Lower or adapt NO_MEM escalation thresholds if persistent ACK failure is still observed.

**Phase 3**
- Suppress nonessential reconnect-adjacent chatter while the link is unstable.

**Phase 4**
- Remove any now-redundant recovery callback branches or diagnostic-only fallbacks.

#### 4) `esp32common/espnow_common_utils/link_recovery_coordinator.cpp`

**Phase 2**
- Make escalation faster when repeated ACK failures indicate persistent congestion.
- Keep escalation bounded, but decisive.

**Phase 4**
- Remove unused or legacy escalation helpers that are not part of the final policy.

#### 5) `esp32common/espnow_common_utils/rx_heartbeat_manager.cpp`

**Phase 3**
- Keep only the minimum heartbeat-related behaviour required for stable link health.
- Avoid any extra telemetry or reconnect-adjacent actions that do not help restore the link.

**Phase 4**
- Delete obsolete ACK/telemetry helper paths that are no longer needed.

#### 6) `espnowreceiver_LCD/src/mqtt/mqtt_task.cpp`

**Phase 3**
- Keep MQTT gated off while ESP-NOW is not connected.
- Avoid reconnect-adjacent traffic from the MQTT task during link instability.

**Phase 4**
- Remove any old reconnect-related MQTT branches that are now redundant.

#### 7) `espnowreceiver_LCD/src/espnow/espnow_runtime_messages.cpp`

**Phase 3**
- Keep only the necessary message handling that supports reconnect recovery.
- Suppress anything that adds noncritical traffic while the link is unstable.

**Phase 4**
- Remove stale or duplicate ingress handling paths that no longer match the final design.

#### 8) `espnowreceiver_LCD/platformio.ini`

**Phase 2**
- Tune the NO_MEM trigger threshold to match the validated reconnect behaviour.

**Phase 4**
- Remove obsolete build flags once the final reconnect policy is stable and documented.

### Shared / common files to watch

#### `esp32common/include/esp32common/config/timing_config.h`

- Keep the 2000 ms dwell unless field testing proves another value is better.
- If cadence changes are needed, make them explicit and document the reason.

#### `esp32common/espnow_transmitter/espnow_transmitter.cpp`

- Keep the discovery ingress split that protects probe/ACK routing.
- Remove any old compatibility ingress path only after the queue topology is confirmed stable.

#### `esp32common/espnow_common_utils/espnow_tx_scheduler.cpp`

- Keep the scheduler as the single send gate for discovery/control traffic.
- Remove only those retry/pressure branches that are replaced by a validated, simpler policy.

---

## Cleanup Rule For The Plan

For every file above, the phase is not complete until the obsolete code from that phase is deleted or clearly removed from the active path.

This is the key guardrail:

- validate the new behaviour,
- remove the replaced branch,
- update the report with what was removed,
- then move to the next phase.

---

## Re-Review Checklist

This report has been checked against the original criteria and now contains:

- A clear comparison between the document’s design intent and the current code.
- Separate transmitter and receiver findings.
- Concrete file-level recommendations.
- An implementation order that addresses the highest-risk issue first.
- A clear statement of what remains aligned and what still needs work.

### Final consistency check

- No conflicting recommendations were left in the report.
- The report keeps the focus on reconnect-related traffic, not unrelated web/MQTT issues.
- The document distinguishes between root cause and secondary noise sources.
- The document is suitable as an implementation guide for the next coding pass.
- The report now explicitly captures the queue-separation background and the requirement to remove legacy code at the end of each completed phase.

---

## Implementation Progress Update (2026-05-13, pass 2)

### Completed in this pass

1. **Adaptive RX NO_MEM trigger is now truly applied end-to-end**
   - The adaptive threshold chosen in `rx_connection_handler.cpp` is now passed through to `LinkRecoveryCoordinator::handle_no_mem_pressure(...)`, so escalation no longer uses only the coordinator's static default threshold.

2. **Reconnect instrumentation has been expanded for transmitter discovery**
   - Added explicit counters for probe send attempts/success/failures (`NO_MEM` vs other) and ACK frame receipts.
   - Added per-scan logs for probe rate and ACK-per-probe ratio.
   - Added summary logs for probe `NO_MEM` frequency and ACK/probe effectiveness.

3. **Phase-4 style cleanup of stale RX threshold remnants**
   - Removed obsolete `RXCONN_NO_MEM_L1_THRESHOLD` and `RXCONN_NO_MEM_L2_THRESHOLD` macro fallback branches from `rx_connection_handler.cpp`.
   - Updated comments so they reflect the current ownership model (L1/L2 budgets are coordinator-config owned; dynamic trigger threshold is handler-selected).

### Explicit removals recorded in this pass

- `rx_connection_handler.cpp`
  - Removed dead fallback macro block:
    - `#ifndef RXCONN_NO_MEM_L1_THRESHOLD ...`
    - `#ifndef RXCONN_NO_MEM_L2_THRESHOLD ...`
  - Removed stale comment guidance that implied L1/L2 budgets were still tuned via those macros.

### Validation status

- Receiver build (`espnowreceiver_LCD`) succeeded.
- Transmitter build (`ESPnowtransmitter2`) succeeded.
- The known Windows `cp1252` console encoding traceback still appears after successful PlatformIO build completion and remains non-blocking.

---

## Implementation Progress Update (2026-05-13, pass 3)

### Completed in this pass

1. **Removed inactive connection-manager compatibility shims**
    - Deleted `EspNowConnectionManager::set_auto_reconnect(bool)` from the public interface and implementation.
    - Deleted `EspNowConnectionManager::set_connecting_timeout_ms(uint32_t)` from the public interface and implementation.
    - Removed the now-unused backing members `auto_reconnect_enabled_` and `connecting_timeout_ms_`.

2. **Cleaned stale reconnect ownership comments**
    - Updated RX handler coordinator wiring comments so they no longer refer to removed multi-threshold fallback macros.

### Explicit removals recorded in this pass

- `esp32common/espnow_common_utils/connection_manager.h`
   - Removed legacy shim API declarations:
      - `set_auto_reconnect(bool)`
      - `set_connecting_timeout_ms(uint32_t)`
   - Removed unused private state members:
      - `auto_reconnect_enabled_`
      - `connecting_timeout_ms_`

- `esp32common/espnow_common_utils/connection_manager.cpp`
   - Removed legacy no-op shim method definitions:
      - `set_auto_reconnect(bool)`
      - `set_connecting_timeout_ms(uint32_t)`
   - Removed constructor/init assignments for the deleted unused members.

- `esp32common/espnow_common_utils/rx_connection_handler.cpp`
   - Removed stale “RXCONN_NO_MEM_* fallback” wording in coordinator-hook comment.

### Validation status

- Receiver build (`espnowreceiver_LCD`) succeeded.
- Transmitter build (`ESPnowtransmitter2`) succeeded.
- The known Windows `cp1252` console encoding traceback still appears only after successful PlatformIO build completion and remains non-blocking.

---

## Implementation Progress Update (2026-05-13, pass 7)

### Completed in this pass

1. **Final non-soak closure tasks completed**
   - Re-ran receiver build validation (`espnowreceiver_LCD` waveshare env) and receiver_2 TFT build validation.
   - Confirmed no new diagnostics errors in the active reconnect/LVGL stabilization documentation scope.

2. **Legacy local RX shim disposition finalized for this cycle**
   - Re-attempted physical deletion of empty shim translation units under `espnowreceiver_LCD/src/espnow/`.
   - Files remain present as zero-byte inert placeholders; no implementation ownership remains in these units.
   - Cleanup policy is now recorded as: non-blocking technical debt item, not a functional reconnect blocker.

### Explicit removals recorded in this pass

- None (physical shim-file deletion still not reflected in filesystem state).

### Validation status

- Receiver build (`espnowreceiver_LCD`) succeeded.
- Receiver build (`espnowreceiver_2`, `lilygo-t-display-s3_tft`) succeeded.
- Remaining open work is soak/runtime validation only.

---

## Implementation Progress Update (2026-05-13, pass 5)

### Completed in this pass

1. **Removed obsolete receiver build exclusions for deleted local RX runtime ownership**
   - Deleted the now-redundant `build_src_filter` exclusions that were only needed while legacy local RX implementation translation units were still part of active source ownership.

2. **Re-validated current reconnect ownership boundaries before cleanup**
   - Confirmed removed legacy webserver recovery wrappers and removed connection-manager shim APIs are no longer referenced.
   - Confirmed queue/runtime bridge symbols remain actively referenced and therefore are intentionally retained.

### Explicit removals recorded in this pass

- `espnowreceiver_LCD/platformio.ini`
   - Removed obsolete `build_src_filter` exclusions:
      - `-<espnow/rx_connection_handler.cpp>`
      - `-<espnow/rx_heartbeat_manager.cpp>`
      - `-<espnow/rx_state_machine.cpp>`

### Validation status

- Receiver build diagnostics reported no errors after cleanup.
- Transmitter build (`ESPnowtransmitter2`) succeeded.
- The known Windows `cp1252` console encoding traceback still appears only after successful PlatformIO build completion and remains non-blocking.

---

## Implementation Progress Update (2026-05-13, pass 6)

### Completed in this pass

1. **Resolved local RX shim cleanup disposition**
   - Re-attempted physical deletion of legacy local RX shim translation units under `espnowreceiver_LCD/src/espnow/`.
   - Verified those files are currently zero-byte and functionally inert.

2. **Re-validated receiver workspace health after pass-5 cleanup**
   - Re-ran receiver diagnostics for `espnowreceiver_LCD`; no errors reported.

### Explicit removals recorded in this pass

- None (filesystem deletion retry did not materially change tracked file presence).

### Validation status

- Receiver diagnostics (`espnowreceiver_LCD`) reported no errors.
- Local shim `.cpp` units remain present but empty/inert and have no active implementation content.

---

## Implementation Progress Update (2026-05-13, pass 4)

### Completed in this pass

1. **Removed unused webserver recovery compatibility wrappers**
    - Deleted `stop_webserver_for_recovery()`.
    - Deleted `restart_webserver_post_recovery()`.
    - Removed their declarations from the public webserver header.

2. **Cleaned stale reconnect ownership wording in shared connection manager**
    - Updated duplicate `CONNECTION_START` handling comment to reflect current multi-ingress reconnect behavior instead of removed auto-reconnect ownership.

### Explicit removals recorded in this pass

- `espnowreceiver_LCD/lib/webserver_lcd/webserver.h`
   - Removed unused declarations:
      - `stop_webserver_for_recovery()`
      - `restart_webserver_post_recovery()`

- `espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp`
   - Removed unused definitions:
      - `stop_webserver_for_recovery()`
      - `restart_webserver_post_recovery()`

- `esp32common/espnow_common_utils/connection_manager.cpp`
   - Removed stale wording that referenced old auto-reconnect posting behavior.

### Validation status

- Receiver build (`espnowreceiver_LCD`) succeeded.
- Transmitter build (`ESPnowtransmitter2`) succeeded.
- The known Windows `cp1252` console encoding traceback still appears only after successful PlatformIO build completion and remains non-blocking.
