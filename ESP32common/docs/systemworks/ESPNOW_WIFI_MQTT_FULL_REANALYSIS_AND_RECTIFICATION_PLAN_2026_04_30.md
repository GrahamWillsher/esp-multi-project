# ESP-NOW/Wi-Fi/MQTT Full Reanalysis and Rectification Plan (2026-04-30)

## Purpose
This document is the requested full reset analysis after the latest reconnect patches made behavior worse. It combines:

1. Current code-path review across TX, RX LCD, RX2, and shared `esp32common`
2. Retrospective of failed fixes (what was wrong and why)
3. Internet-backed guidance (Espressif official docs/examples)
4. Concrete rectification plan with implementation order and validation criteria

---

## Executive Findings

1. **Primary failure is coexistence timing, not simple retry counts.**  
   Receivers run both MQTT (Wi-Fi TCP) and ESP-NOW on the same radio. Discovery ACKs are attempted during Wi-Fi/TCP pressure windows, causing `ESP_ERR_ESPNOW_NO_MEM`.

2. **Recent fixes over-optimized “send more ACKs faster” and worsened contention.**  
   Reducing ACK throttle and forcing reconnect behavior earlier increased send pressure while resource contention still existed.

3. **Scheduler behavior is not aligned with Espressif’s recommended pacing model.**  
   Official guidance recommends pacing sends based on send-callback progression for heavy traffic. Current scheduler uses queue + retries + inter-frame delays, but not strict callback-gated in-flight control for discovery-critical exchanges.

4. **Reconnect correctness requires deterministic radio-ownership phases.**  
   During reconnect, receivers must guarantee ESP-NOW control traffic gets priority and MQTT teardown is fully settled before discovery ACK bursts are attempted.

---

## What We Got Wrong in Previous Attempts

### Mistake A — Treating symptoms as root cause
We tuned:
- retry counts,
- min-gap values,
- ACK throttle windows,
- FSM trigger sensitivity,
without enforcing deterministic radio/resource ownership windows.

Result: behavior changed, but root race remained.

### Mistake B — Increasing ACK attempt rate during unstable resource windows
Changes to permissive probe ACK behavior and faster retry cadence increased call frequency to `esp_now_send()` while Wi-Fi resources were still constrained.

Result: higher `NO_MEM` frequency and worse reconnect reliability.

### Mistake C — Forcing reconnect FSM paths too aggressively
Treating nearly every post-session probe as reconnect trigger (including BOOTSTRAP-inclusive recovery behavior) increased transitions and side effects before the system had stabilized.

Result: more churn, not more determinism.

### Mistake D — Not validating against official pacing guidance early enough
We should have anchored earlier on Espressif recommendations for:
- handling `ESP_ERR_ESPNOW_NO_MEM` with delayed retry,
- callback-light handlers,
- paced send progression,
- channel/key consistency,
- modem-sleep caveats in station mode.

---

## Internet-Backed Constraints (Espressif)

From Espressif ESP-NOW docs/example:

1. `esp_now_send()` can return `ESP_ERR_ESPNOW_NO_MEM`; app should delay before retry.
2. For high traffic, app should send next frame after prior send callback returns.
3. send/recv callbacks run in high-priority Wi-Fi task context; handlers must stay lightweight.
4. Peers must match channel/keys and be correctly registered.
5. If receiver is station-only and connected to AP, modem sleep can break ESPNOW receive reliability unless disabled.

References:
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html
- https://github.com/espressif/esp-idf/tree/v6.0.1/examples/wifi/espnow

---

## Corrective Architecture (Rectification)

### R1. Reconnect Quiet Mode must be a hard resource boundary
On receiver reconnect entry:
1. Disconnect MQTT
2. Enter scheduler `control_only_mode` immediately
3. Purge non-control queues immediately
4. Hold for a bounded settle delay before aggressive ACK retries

This must happen as one atomic policy action, not as reactive follow-up after enqueue failures.

### R2. Discovery ACK path must be callback-paced when in reconnect mode
For discovery-critical frames (`msg_ack` in reconnect window):
- allow only one in-flight discovery ACK per peer,
- schedule next attempt only after send callback or timeout token release,
- cap queue accumulation.

This follows Espressif’s callback-paced recommendation for high-pressure windows.

### R3. Retry window should be broader, attempt frequency lower
Current strategy tends to hammer quickly. Instead:
- fewer immediate retries per burst,
- longer total retry window,
- callback-paced progression,
- no tight-loop send pressure.

Goal: reduce instantaneous allocator contention while still spanning teardown delays.

### R4. Keep callbacks minimal and defer heavy logic
`on_data_recv`/send callbacks should only enqueue compact work items and return.  
No reconnect-policy heavy logic in callback context.

### R5. Reconnect-phase MQTT gating must be identical across RX LCD and RX2
No variant drift. Both receivers must enforce same rules for:
- heartbeat freshness gating,
- discovery-only traffic handling,
- transition to/from quiet mode,
- post-reconnect settle durations.

### R6. Channel and power-save invariants must be explicitly guarded
At runtime, log and validate:
- current channel before/after reconnect,
- AP channel drift events,
- modem-sleep status on receiver STA mode.

---

## Concrete Code-Level Rectification Plan

### Phase 1 — Stabilize (rollback harmful aggressiveness)
1. In shared probe ACK gating, remove BOOTSTRAP-overpermissive behavior and revert to conservative reconnect gates.
2. Revert ultra-fast ACK cadence adjustments that increase `NO_MEM` burst pressure.
3. Keep discovery ACK retries sufficient, but avoid “fast hammer” behavior.

### Phase 2 — Enforce deterministic reconnect boundaries
1. In `ReceiverConnectionHandler::enter_reconnect_quiet_mode(...)`:
   - force `EspnowTxScheduler::set_control_only_mode(true, true)` immediately,
   - disconnect MQTT,
   - apply bounded settle wait,
   - then allow discovery ACK workflow.
2. Ensure this path is single-entry/idempotent to avoid repeated toggles.

### Phase 3 — Discovery ACK callback pacing
1. Add reconnect-mode token/in-flight guard per peer for discovery ACK sends.
2. Release token on send callback or timeout watchdog.
3. Block duplicate ACK queueing while token is held.

### Phase 4 — Receiver parity and instrumentation
1. Ensure RX LCD and RX2 run identical reconnect policy semantics.
2. Add structured counters:
   - ACK attempts,
   - ACK callback success/fail,
   - `NO_MEM` counts by phase (steady/reconnect/settle),
   - control-only mode entry/exit timestamps,
   - MQTT disconnect start/settled timestamps.

### Phase 5 — Validation gates before accepting fix
A fix is accepted only if all pass:
1. 20 forced reconnect cycles on RX LCD: 0 stuck scan loops.
2. 20 forced reconnect cycles on RX2: 0 stuck scan loops.
3. `NO_MEM` may occur transiently, but discovery ACK success must occur within same TX channel dwell in >= 95% cycles.
4. No regression in steady-state telemetry/event throughput.

---

## Immediate Recommendations

1. **Do not continue tuning random retry/throttle constants in isolation.**
2. Implement deterministic reconnect ownership boundaries first.
3. Implement callback-paced discovery ACK progression second.
4. Validate with cycle-based acceptance criteria, not one-off reconnect anecdotes.

---

## Re-Review Outcome (2026-04-30, Two-Device Focus: 1 TX + 1 RX)

This section re-checks the plan against the current implementation and the practical two-device ESP-NOW topology.

### A) Robustness verdict

**Verdict: directionally correct, but not yet a complete robust fix.**

Why:
1. The document’s architecture is correct (FSM as sole arbitration authority).
2. Recent rollback changes removed harmful side-path aggressiveness.
3. However, two key hardening items are still pending in code:
   - callback-paced discovery ACK send progression,
   - explicit reconnect-boundary settle sequencing guarantees.

So the document is correct as a plan, but it should not be treated as “fully implemented and validated” yet.

### B) Two-device behavior impact (TX Ethernet + RX Wi-Fi)

For a two-device link:
- TX is generally not radio-contended by MQTT (Ethernet path), so reconnect reliability is dominated by RX behavior.
- RX shares Wi-Fi resources between MQTT TCP and ESP-NOW, so reconnect windows remain the critical risk.
- The state-machine approach is the right model because it can enforce receiver radio ownership by phase.

Net effect:
- If FSM gates are strict and callback pacing is added, two-device reconnect should become deterministic enough for repeated scan cycles.
- If callback pacing is not added, `ESP_ERR_ESPNOW_NO_MEM` bursts can still probabilistically break reconnect.

### C) Implementation status vs this plan

1. **R1 (hard reconnect boundary):** **Partially implemented**  
   FSM policy drives `control_only_mode` and MQTT gate, and recent rollback removed over-eager side triggers.  
   Remaining gap: reconnect entry still needs explicit bounded settle semantics as a strict contract.

2. **R2 (callback-paced discovery ACK):** **Not implemented**  
   Scheduler remains queue/retry/cadence based; no explicit per-peer in-flight token released by send callback.

3. **R3 (broader retry window, lower burst pressure):** **Partially implemented**  
   Harmful “faster hammer” behavior was rolled back, but policy is still static and not callback-coupled.

4. **R4 (lightweight callbacks):** **Mostly implemented**  
   Receive/send callbacks are queue/log centric; keep this invariant.

5. **R5 (RX parity):** **Implemented at policy gate level**  
   Both receiver MQTT tasks gate by shared FSM policy.

6. **R6 (channel/power-save invariants):** **Not fully implemented**  
   Additional runtime instrumentation is still required.

### D) What must be true before calling it “robust”

All of the following are required:
1. Discovery ACK callback pacing implemented in reconnect windows.
2. Reconnect boundary sequencing enforced as a single idempotent state-transition action.
3. Structured reconnect metrics prove success across repeated cycles on both receiver variants.
4. Acceptance gate from this document (20-cycle tests and >=95% same-dwell ACK success) passes.

Until then, this remains a high-confidence correction plan, not a completed final fix.

---

## Hardening Profile (Mandatory for Robustness)

This section upgrades the plan from “good direction” to an implementation contract.

### H1) Non-negotiable invariants

1. **FSM is the single authority for radio coexistence policy.**
   - No send eligibility decisions outside FSM policy outputs.
   - No ad-hoc reconnect flags may override FSM outputs.

2. **Reconnect entry is atomic and idempotent.**
   - One transition action performs: MQTT disconnect request, scheduler control-only enable, non-control purge, reconnect settle timer start.
   - Repeated triggers during active reconnect must not re-run purge/disconnect loops.

3. **Discovery ACK is callback-paced in reconnect states.**
   - Per-peer single in-flight token.
   - Token release only on send callback or watchdog timeout.
   - Duplicate ACK enqueue blocked while token held.

4. **Callback context remains minimal.**
   - No heavy logic, no blocking waits, no reconnect orchestration in Wi-Fi callbacks.

5. **State transition observability is mandatory.**
   - Every FSM transition logs state/event/time.
   - Every reconnect attempt emits one bounded diagnostic record at exit.

### H2) Explicit anti-patterns (forbidden)

1. Changing ACK throttle/retry constants without proving impact against reconnect-cycle metrics.
2. Adding BOOTSTRAP special-case fast paths for probe ACKs.
3. Using `had_connected_session`-style historical flags as reconnect authority.
4. Re-introducing non-FSM MQTT gates in task code.
5. Treating single successful reconnect as validation.

### H3) Receiver-side timing contract (recommended baseline)

These are baseline targets for robustness and should only be changed with metrics evidence:

1. Recovery probe-ACK throttle: 200 ms
2. Steady-state probe-ACK throttle: 1200 ms
3. Post-reconnect settle window: 8000 ms
4. Reconnect quiet entry should complete policy application in one scheduler tick budget (no unbounded loops)

### H4) Failure-injection validation matrix (required)

For both RX variants, run all tests with TX active scan behavior:

1. **Broker reachable, low RTT** (LAN)
2. **Broker reachable, higher RTT** (simulated delay)
3. **Broker intermittently unreachable** (disconnect/reconnect bursts)
4. **AP channel change during runtime**
5. **RX reboot during reconnect window**
6. **TX reboot during RX post-reconnect settle**

Pass criteria per scenario:
- No stuck CONNECTING loop after 20 cycles.
- Discovery ACK success within same dwell in >=95% cycles.
- FSM exits reconnect states correctly (no state wedging).
- No sustained growth in `NO_MEM` failure density across cycles.

### H5) Promotion gates (must pass before release)

1. **Code gate**
   - Callback-paced ACK path implemented.
   - Atomic reconnect entry path implemented.
   - No side-path reconnect overrides found in review.

2. **Metrics gate**
   - Per-phase counters present and persisted for session diagnostics.
   - Transition timeline proves expected FSM path sequence.

3. **System gate**
   - RX LCD and RX2 pass identical reconnect campaign.
   - No throughput regression in steady-state data paths.

### H6) Operational runbook requirements

1. If reconnect failure occurs in field, capture:
   - FSM transition timeline,
   - reconnect diagnostic record,
   - scheduler send-fail counters by class,
   - MQTT disconnect start/settled timestamps,
   - channel at reconnect start and ACK success.

2. Any hotfix must include:
   - explicit statement of which invariant changed,
   - before/after reconnect-cycle metrics,
   - rollback condition.

---

## Final Assessment

The previous patch direction failed because it increased send aggressiveness without first controlling shared radio/resource ownership. The right fix is architectural sequencing:

- establish hard reconnect quiet boundaries,
- prioritize control traffic deterministically,
- pace discovery ACKs by send-completion progression,
- keep callbacks lightweight,
- validate with repeated reconnect-cycle tests.

This is the rectification path most consistent with both the observed failures and Espressif’s official ESP-NOW behavior guidance.
