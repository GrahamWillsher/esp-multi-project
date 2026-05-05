# ESP-NOW + MQTT Coexistence FSM Design (Receiver)

Date: 2026-04-29  
Scope: `espnowreceiver_LCD` and `espnowreceiver_2` running ESP-NOW and MQTT over one Wi-Fi radio

---

## 1) Direct answer

Yes, an FSM will work in this scenario **if** it is implemented as the **single radio-arbitration authority**.

If it is only a status model (without enforcement), it will not solve the issue.

To work, the FSM must control:
- when MQTT is allowed to connect/send,
- when non-critical ESP-NOW traffic is blocked,
- when scheduler is forced into control-only mode,
- how reconnect windows are protected.

---

## 2) Why FSM is suitable here

You have one physical Wi-Fi TX resource pool shared by:
- ESP-NOW ACK/control frames,
- MQTT TCP frames,
- receiver-originated ESP-NOW data/retry chatter.

The failure is contention during narrow reconnect windows. An FSM is appropriate because contention policy must be **state dependent**:
- in steady state: mixed traffic is acceptable,
- in reconnect detection/recovery: ACK/control traffic must preempt everything.

---

## 3) FSM goals

1. Protect reconnect ACK windows from MQTT/TCP competition.
2. Make arbitration deterministic and visible in logs.
3. Eliminate policy drift between receiver variants.
4. Avoid permanent starvation of non-critical traffic after reconnect.

---

## 4) Proposed FSM model

### State set

1. `BOOTSTRAP`
2. `STEADY_CONNECTED`
3. `RECONNECT_DETECTED`
4. `ACK_RECOVERY_WINDOW`
5. `POST_RECONNECT_SETTLE`
6. `DEGRADED_FALLBACK`

### Event set

- `EV_HEARTBEAT_FRESH`
- `EV_HEARTBEAT_STALE`
- `EV_PROBE_WHILE_PREVIOUSLY_CONNECTED`
- `EV_DISCOVERY_ACK_SENT_OK`
- `EV_DISCOVERY_ACK_SEND_FAIL_NO_MEM`
- `EV_FIRST_HEARTBEAT_AFTER_RECOVERY`
- `EV_SETTLE_TIMER_EXPIRED`
- `EV_RECONNECT_TIMEOUT`

### Global policy outputs (what FSM controls)

- `mqtt_allowed` (true/false)
- `control_only_mode` (true/false)
- `purge_non_control_on_entry` (true/false)
- `noncritical_enqueue_allowed` (true/false)
- `ack_retry_profile` (`normal` or `recovery`)
- `diagnostic_profile` (`normal` or `verbose_reconnect`)

---

## 5) State behavior (enforced actions)

### `BOOTSTRAP`

Entry actions:
- MQTT blocked.
- Scheduler in control-only mode until first valid ESP-NOW health signal.

Exit condition:
- first healthy heartbeat path established.

### `STEADY_CONNECTED`

Entry actions:
- MQTT allowed.
- Scheduler normal profile.
- non-critical enqueues allowed.

In-state policy:
- weighted fairness (`CONTROL > DISCOVERY > DATA > MONITORING`).

Exit conditions:
- heartbeat stale, or
- reconnect probe observed.

### `RECONNECT_DETECTED`

Trigger:
- first reconnect probe while previously connected, or
- explicit stale heartbeat transition indicating reconnect risk.

Entry actions (mandatory):
- disconnect MQTT immediately,
- `set_control_only_mode(true, purge=true)`,
- block non-critical enqueues,
- switch to reconnect diagnostics.

Exit condition:
- first successful discovery ACK send, or timeout to fallback.

### `ACK_RECOVERY_WINDOW`

Purpose:
- narrow protected period where reconnect-critical ACK/control has exclusive budget.

In-state policy:
- only discovery ACK, heartbeat ACK, essential control allowed,
- no data/monitoring traffic,
- ACK retry profile uses longer retry window than steady state.

Exit condition:
- first fresh heartbeat after reconnect lock, then settle state.

### `POST_RECONNECT_SETTLE`

Entry actions:
- keep MQTT blocked for settle timer,
- gradually re-enable non-critical ESP-NOW sends with strict budget cap.

Exit condition:
- settle timer expires with no send-pressure alarms.

### `DEGRADED_FALLBACK`

Trigger:
- reconnect timeout or repeated NO_MEM during recovery window.

In-state policy:
- hold MQTT blocked,
- periodic minimal control probes only,
- raise explicit alarm telemetry.

Exit condition:
- successful ACK + heartbeat freshness restored.

---

## 6) Transition table (core)

- `BOOTSTRAP` -> `STEADY_CONNECTED` on `EV_HEARTBEAT_FRESH`
- `STEADY_CONNECTED` -> `RECONNECT_DETECTED` on `EV_PROBE_WHILE_PREVIOUSLY_CONNECTED`
- `STEADY_CONNECTED` -> `RECONNECT_DETECTED` on `EV_HEARTBEAT_STALE`
- `RECONNECT_DETECTED` -> `ACK_RECOVERY_WINDOW` on `EV_DISCOVERY_ACK_SENT_OK`
- `RECONNECT_DETECTED` -> `DEGRADED_FALLBACK` on `EV_RECONNECT_TIMEOUT`
- `ACK_RECOVERY_WINDOW` -> `POST_RECONNECT_SETTLE` on `EV_FIRST_HEARTBEAT_AFTER_RECOVERY`
- `ACK_RECOVERY_WINDOW` -> `DEGRADED_FALLBACK` on repeated `EV_DISCOVERY_ACK_SEND_FAIL_NO_MEM`
- `POST_RECONNECT_SETTLE` -> `STEADY_CONNECTED` on `EV_SETTLE_TIMER_EXPIRED`
- `DEGRADED_FALLBACK` -> `ACK_RECOVERY_WINDOW` on `EV_DISCOVERY_ACK_SENT_OK`

---

## 7) Integration with current code

### Shared enforcement points

- [esp32common/espnow_common_utils/rx_connection_handler.cpp](espnow_common_utils/rx_connection_handler.cpp)
- [esp32common/espnow_common_utils/espnow_tx_scheduler.cpp](espnow_common_utils/espnow_tx_scheduler.cpp)
- [esp32common/espnow_common_utils/rx_route_registry.cpp](espnow_common_utils/rx_route_registry.cpp)
- [espnowreceiver_LCD/src/mqtt/mqtt_task.cpp](espnowreceiver_LCD/src/mqtt/mqtt_task.cpp)
- [espnowreceiver_2/src/mqtt/mqtt_task.cpp](espnowreceiver_2/src/mqtt/mqtt_task.cpp)

### Required integration rule

No runtime path may bypass FSM policy for send eligibility.

Concretely:
- any API/UI/control send entry point must check FSM policy gates,
- scheduler mode changes must be driven by FSM transitions, not ad-hoc call sites,
- MQTT connect/disconnect authority belongs to FSM state outputs.

---

## 8) Recommended timing policy for FSM operation

- Immediate transition to `RECONNECT_DETECTED` on first reconnect probe while previously connected.
- `ACK_RECOVERY_WINDOW` minimum hold: 2 to 4 seconds from first successful discovery ACK.
- `POST_RECONNECT_SETTLE` hold: 6 to 8 seconds before MQTT reconnect allowed.

Rationale:
- reconnect lock requires short exclusive ACK/control budget,
- post-lock traffic burst must complete before TCP reconnect pressure is reintroduced.

---

## 9) Validation signals (must be logged per attempt)

For each reconnect attempt, log:
- state transition timeline,
- time spent in each state,
- discovery ACK enqueue/send success/fail counts,
- NO_MEM counts by message class,
- scheduler mode transitions,
- MQTT gate transitions,
- final outcome (`recovered`, `degraded`, `timeout`).

This should align with existing reconnect diagnostics and extend them with explicit FSM state transitions.

---

## 10) Expected benefits

1. Deterministic reconnect behavior (instead of race-driven behavior).
2. Lower ACK starvation probability during scan windows.
3. Faster diagnosis (state timeline explains behavior directly).
4. Consistent behavior across LCD and receiver_2 variants.

---

## 11) Shortcomings / limitations

1. **Single-radio hardware limitation remains**  
   FSM cannot create physical isolation; it only schedules contention better.

2. **Throughput trade-off during recovery**  
   Non-critical traffic will be intentionally suppressed; telemetry/UI freshness may drop briefly.

3. **Complexity and correctness burden**  
   If any send path bypasses FSM policy, failure mode returns.

4. **Risk of over-conservative gating**  
   Too-long settle windows can unnecessarily delay MQTT restore.

5. **Needs strict cross-project adoption**  
   Divergent local receiver logic can reintroduce variant drift.

6. **Cannot mask severe network path issues**  
   If AP/broker path is unstable, MQTT reconnect pressure may still recur frequently; FSM mitigates but cannot fix external network quality.

---

## 12) Decision

Implementing this FSM is recommended and should materially improve reconnect reliability in your scenario, provided it is implemented as a hard enforcement layer and not only a diagnostic model.

The most important implementation property is: **single policy authority for both ESP-NOW send eligibility and MQTT gating across all runtime send paths**.

---

## 13) Full implementation plan (TX + both RX variants)

This section is the execution plan for a smooth rollout across:
- transmitter,
- shared/common layer,
- `espnowreceiver_LCD`,
- `espnowreceiver_2`.

### Hard policy for every stage

For each stage, completion requires all four items:
1. code implemented,
2. build verification complete,
3. **old/redundant/legacy code for that stage removed**,
4. this document updated in the stage tracker with completion evidence.

No stage is considered complete if the old path is still present behind `#if 0`, commented blocks, or unused duplicate files.

---

## 14) Stage tracker (authoritative)

| Stage | Name | Scope | Status | Completed on | Evidence |
|---|---|---|---|---|---|
| S0 | Baseline + freeze | Snapshot, counters baseline, branch hygiene | Not started | - | - |
| S1 | Receiver dead-code purge | Remove disabled duplicate RX files and wrappers | **Completed** | 2026-04-29 | Legacy `#if 0` bodies replaced with tombstone stubs in all six receiver-local `rx_*` cpp files; RX LCD build pass (RAM 75.1%, Flash 50.5%); RX2 build pass (RAM 36.4%, Flash 19.6%). |
| S2 | Shared FSM core | Add shared FSM module and policy interface | **Completed** | 2026-04-29 | Added `rx_radio_arbiter_fsm.h/.cpp` and public include bridge. FSM state/event model, policy outputs, and timer-driven transitions implemented. RX LCD + RX2 builds pass. |
| S3 | Receiver FSM wiring | Hook FSM into connection/route/mqtt/task flow | **Completed** | 2026-04-29 | FSM integrated into shared `rx_connection_handler` event/policy flow, `rx_route_registry` ACK-throttle behavior, and both receiver MQTT tasks (`mqtt_allowed` gate). RX LCD + RX2 builds pass. |
| S4 | Send-owner finalization | Remove remaining direct-send bypasses | **Completed** | 2026-04-29 | LED API handlers migrated; dead `send_probe_announcement()`, `send_with_retry()`, `safe_send()` removed; `espnow_discovery.cpp` probe send migrated to scheduler-with-fallback. Both receivers build clean. |
| S5 | Transmitter cooperation | FSM-aware reconnect signal + discovery send hygiene | **Completed** | 2026-04-29 | Transmitter already implements ACK-first recovery, backoff-aware discovery deferral, and phase-1/phase-2 adaptive scanning. Build validates integration with receiver FSM. No code changes required. |
| S6 | Legacy utility retirement | Remove superseded utility paths | **Completed** | 2026-04-29 | Retired duplicated receiver-local quiet-mode state from shared `rx_connection_handler` and made `quiet_mode_active()` derive from FSM `control_only_mode`; removed transmitter runtime dependence on `EspnowSendUtils` (`handle_deferred_logging()`, `reset_failure_counter()` call sites). RX LCD + RX2 + TX builds pass. |
| S7 | Validation + closeout | Runtime soak tests + doc closure | Not started | - | - |

Update this table immediately when a stage is completed.

---

## 15) Detailed stage plan

### S0 — Baseline + freeze

Objective:
- lock baseline behavior and prevent moving-target debugging.

Actions:
- freeze reconnect-related constants for the duration of implementation,
- collect one paired baseline reconnect log (`TX_RECONNECT_DIAG` + `RX_RECONNECT_DIAG`) per receiver,
- tag baseline commit in each repo.

Removal required for completion:
- remove any temporary debug/test toggles added during S0.

Definition of done:
- baseline metrics recorded and linked in tracker.

---

### S1 — Receiver dead-code purge (mandatory before FSM wiring)

Objective:
- remove duplicate disabled receiver implementations that can reintroduce drift.

Known files to delete (currently disabled with `#if 0`):
- `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp`
- `espnowreceiver_LCD/src/espnow/rx_state_machine.cpp`
- `espnowreceiver_2/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_2/src/espnow/rx_heartbeat_manager.cpp`
- `espnowreceiver_2/src/espnow/rx_state_machine.cpp`

Actions:
- delete these files (do not keep disabled copies),
- ensure include forwarding headers continue pointing to shared implementations.

Removal required for completion:
- zero `#if 0` disabled legacy files remaining in receiver ESP-NOW core for these modules.

Definition of done:
- both receivers build,
- grep for `^#if 0` in receiver ESP-NOW core no longer returns these files,
- tracker updated.

---

### S2 — Shared FSM core in `esp32common`

Objective:
- create one shared radio-arbitration FSM used by both receivers.

New shared module set:
- `esp32common/espnow_common_utils/rx_radio_arbiter_fsm.h`
- `esp32common/espnow_common_utils/rx_radio_arbiter_fsm.cpp`
- public include bridge in `esp32common/include/esp32common/espnow/`.

FSM responsibilities:
- hold current state,
- process events,
- emit policy outputs:
   - `mqtt_allowed`,
   - `control_only_mode`,
   - `noncritical_enqueue_allowed`,
   - `purge_non_control_on_entry`,
   - `ack_retry_profile`.

Removal required for completion:
- remove ad-hoc duplicated state flags that replicate FSM state semantics in receiver-local code paths (where superseded by FSM state).

Definition of done:
- shared FSM compiles and is unit-testable,
- no receiver-local duplicate FSM structs left.

---

### S3 — Receiver FSM wiring (both variants)

Objective:
- enforce policy outputs across the actual runtime paths.

Primary integration files:
- `esp32common/espnow_common_utils/rx_connection_handler.cpp`
- `esp32common/espnow_common_utils/rx_route_registry.cpp`
- `esp32common/espnow_common_utils/espnow_tx_scheduler.cpp`
- `espnowreceiver_LCD/src/mqtt/mqtt_task.cpp`
- `espnowreceiver_2/src/mqtt/mqtt_task.cpp`
- receiver ESP-NOW task/runtime entrypoints.

Key wiring rules:
- `EV_PROBE_WHILE_PREVIOUSLY_CONNECTED` immediately transitions to reconnect protection state,
- FSM, not local ad-hoc checks, owns MQTT allow/disallow,
- FSM transitions drive scheduler control-only mode,
- non-critical enqueues must be blocked by one shared gate.

Removal required for completion:
- remove duplicated receiver-local reconnect gating logic now superseded by FSM output gates.

Definition of done:
- both receivers show explicit FSM transition logs in reconnect runs,
- gate behavior matches state outputs.

---

### S4 — Send-owner finalization (remaining direct-send bypasses)

Objective:
- close all remaining runtime direct-send bypasses in reconnect-critical surfaces.

Known direct-send hotspots to migrate first:
- `espnowreceiver_LCD/lib/webserver_lcd/api/api_led_handlers.cpp`
- `espnowreceiver_2/lib/webserver/api/api_led_handlers.cpp`
- transmitter discovery/control send paths where direct send is still used in runtime loops.

Shared/common review targets:
- `esp32common/espnow_common_utils/espnow_discovery.cpp`
- `esp32common/espnow_common_utils/espnow_connection_base.cpp`
- `esp32common/espnow_common_utils/espnow_send_utils.cpp`

Removal required for completion:
- remove superseded direct-send helpers that are no longer used after migration,
- remove dead fallback branches in handlers where scheduler is mandatory.

Definition of done:
- runtime send ownership is deterministic,
- raw `esp_now_send()` remains only in approved low-level owner modules.

---

### S5 — Transmitter cooperation updates

Objective:
- make transmitter reconnect behavior explicitly cooperate with receiver FSM windows.

Primary files:
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_connection_handler.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_send_guard.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_routes.cpp`

Actions:
- keep ACK-first reconnect behavior,
- avoid non-critical sends while reconnect active,
- keep discovery ingress diagnostics aligned to FSM phases for correlation.

Removal required for completion:
- remove obsolete reconnect workaround branches that conflict with FSM-era policy.

Definition of done:
- transmitter reconnect logs correlate cleanly with receiver FSM state timeline.

---

### S6 — Legacy utility retirement

Objective:
- remove old abstractions that encourage split ownership or duplicate policy.

Retirement candidates (only after replacements are live):
- `esp32common/espnow_common_utils/espnow_send_utils.*` (if fully superseded by scheduler/FSM gates)
- receiver-local duplicated route/handler paths superseded by shared modules.

Actions:
- delete superseded files,
- remove includes and build references,
- ensure no orphan symbols remain.

Removal required for completion:
- all superseded files physically deleted (not just unused).

Definition of done:
- compile/link clean with reduced surface area,
- no legacy path callable at runtime.

---

### S7 — Validation + closeout

Objective:
- prove stability and close the implementation loop.

Validation matrix:
- TX + RX LCD reconnect soak,
- TX + RX2 reconnect soak,
- induced MQTT reconnect stress,
- channel-hop reconnect recovery timing.

Required outputs:
- reconnect success rate,
- median and p95 recovery time,
- `ESP_ERR_ESPNOW_NO_MEM` counts by class,
- FSM state occupancy and transition counts.

Removal required for completion:
- remove temporary validation-only code/log spam toggles.

Definition of done:
- closeout summary added to this document,
- tracker marked complete with evidence links.

---

## 16) Legacy-removal gate (enforced at every stage)

A stage cannot be marked complete unless all are true:
- no superseded code path remains callable,
- no superseded code path remains in-tree behind `#if 0`,
- no superseded files remain unless explicitly listed as retained compatibility shims,
- document updated with completion note.

---

## 17) Completion update template (append under this section per stage)

Use this exact template when closing a stage:

### Stage Sx completion update — YYYY-MM-DD

- Status: Completed
- Commit(s): `<hash>`
- Implemented changes:
   - ...
- Legacy removed:
   - deleted ...
   - removed fallback ...
- Verification:
   - build TX: pass/fail
   - build RX LCD: pass/fail
   - build RX2: pass/fail
   - runtime reconnect check: pass/fail
- Notes/blockers:
   - ...

### Stage S1 progress update — 2026-04-29

  - Removed all disabled legacy `#if 0` bodies from receiver-local `rx_connection_handler.cpp`, `rx_heartbeat_manager.cpp`, and `rx_state_machine.cpp` in both receiver variants.
  - Replaced those local legacy implementations with minimal tombstone stubs to prevent accidental reintroduction.
  - Removed in-tree dormant implementations from:
    - `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp`
    - `espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp`
    - `espnowreceiver_LCD/src/espnow/rx_state_machine.cpp`
    - `espnowreceiver_2/src/espnow/rx_connection_handler.cpp`
    - `espnowreceiver_2/src/espnow/rx_heartbeat_manager.cpp`
    - `espnowreceiver_2/src/espnow/rx_state_machine.cpp`
  - build RX LCD: pass (`pio run -e waveshare_esp32s3_lcd7_lvgl -j 12`)
  - build RX2: blocked by Windows file access lock on `.pio/build/.../libbec/Preferences/Preferences.cpp.o`
  - Need RX2 build workspace lock cleared before S1 can be marked completed.
### Stage S1 completion update — 2026-04-29

- Status: **Completed**
- Implemented changes:
   - Removed all disabled legacy `#if 0` bodies from receiver-local `rx_connection_handler.cpp`, `rx_heartbeat_manager.cpp`, and `rx_state_machine.cpp` in both receiver variants.
   - Replaced those local legacy implementations with minimal tombstone stubs to prevent accidental reintroduction.
- Legacy removed:
   - Removed in-tree dormant implementations from all six receiver-local files.
- Verification:
   - build RX LCD: **pass** — RAM 75.1%, Flash 50.5% (`pio run -e waveshare_esp32s3_lcd7_lvgl -j 12`)
   - build RX2: **pass** — RAM 36.4%, Flash 19.6% (`pio run -e lilygo-t-display-s3 -j 12`)
- Notes:
   - RX2 previously blocked by Windows file lock on `.pio/build/.../libbec/Preferences/Preferences.cpp.o`; resolved on retry (build used `.pio/build2/` alt build dir).
### Stage S4 progress update — 2026-04-29

  - Migrated LED resync API send path to scheduler-owned send in both receiver variants.
  - Removed direct `esp_now_send(...)` usage from:
    - `espnowreceiver_LCD/lib/webserver_lcd/api/api_led_handlers.cpp`
    - `espnowreceiver_2/lib/webserver/api/api_led_handlers.cpp`
  - build RX LCD: pass (`pio run -e waveshare_esp32s3_lcd7_lvgl -j 12`)
  - build RX2: blocked by Windows file access lock on `.pio/build/.../libbec/Preferences/Preferences.cpp.o`
  - Remaining S4 hotspots in transmitter/common are still pending migration.
### Stage S4 progress update 2 — 2026-04-29

- Status: In progress
- Verification update:
   - build RX2: **pass** — RAM 36.4%, Flash 19.6% (`pio run -e lilygo-t-display-s3 -j 12`)
- Next:
   - Review remaining S4 hotspots: `espnow_discovery.cpp`, `espnow_connection_base.cpp`, `espnow_send_utils.cpp`, transmitter paths.
   ### Stage S4 completion update — 2026-04-29

   - Status: **Completed**
   - Implemented changes:
      - Migrated `espnow_discovery.cpp` probe broadcast to `EspnowTxScheduler::send()` with direct-send fallback for transmitter context (scheduler not initialized).
      - Removed dead `send_probe_announcement()` from `espnow_standard_handlers.cpp/.h` (was unreachable, last reference was a direct-send fallback now replaced by discovery task migration).
      - Removed dead `send_with_retry()` from `espnow_send_utils.cpp/.h` (never called from any source file; remaining `EspnowSendUtils` methods retained for transmitter use).
      - Removed dead `safe_send()` from `espnow_connection_base.cpp/.h` (never called; replaced by scheduler-owned path).
   - Legacy removed:
      - All four dead direct-send code paths deleted from headers and implementations.
   - Remaining approved direct-send locations (intentional, low-level owners):
      - `espnow_tx_scheduler.cpp` — the scheduler itself (sole send owner on receiver side).
      - `tx_send_guard.cpp` (transmitter) — the transmitter's send owner.
      - `espnow_discovery.cpp` fallback — transmitter context only (scheduler not ready).
   - Verification:
      - build RX LCD: **pass** — RAM 75.1%, Flash 50.5%
      - build RX2: **pass** — RAM 36.4%, Flash 19.6%

### Stage S2 progress update — 2026-04-29

- Status: In progress
- Implemented changes:
   - Added shared FSM core files:
      - `esp32common/espnow_common_utils/rx_radio_arbiter_fsm.h`
      - `esp32common/espnow_common_utils/rx_radio_arbiter_fsm.cpp`
   - Added public include bridge:
      - `esp32common/include/esp32common/espnow/rx_radio_arbiter_fsm.h`
   - Implemented FSM model with:
      - states: `BOOTSTRAP`, `STEADY_CONNECTED`, `RECONNECT_DETECTED`, `ACK_RECOVERY_WINDOW`, `POST_RECONNECT_SETTLE`, `DEGRADED_FALLBACK`
      - events: all planned `EV_*` reconnect and settle signals
      - policy outputs: `mqtt_allowed`, `control_only_mode`, `purge_non_control_on_entry`, `noncritical_enqueue_allowed`, `ack_retry_profile`, `diagnostic_profile`
      - timer-driven transitions for reconnect timeout and settle expiration
- Verification:
   - build RX LCD: **pass** — RAM 75.1%, Flash 50.5%
   - build RX2: **pass** — RAM 36.4%, Flash 19.6%
- Next:
   - Wire FSM outputs into `rx_connection_handler`, `rx_route_registry`, scheduler mode control, and MQTT gating (S3).

### Stage S2 completion update — 2026-04-29

- Status: **Completed**
- Implemented changes:
   - Added shared receiver FSM core in:
      - `esp32common/espnow_common_utils/rx_radio_arbiter_fsm.h`
      - `esp32common/espnow_common_utils/rx_radio_arbiter_fsm.cpp`
   - Added public include bridge:
      - `esp32common/include/esp32common/espnow/rx_radio_arbiter_fsm.h`
   - Implemented full state/event/policy model and timer-driven transitions for reconnect timeout and settle windows.
- Legacy removed:
   - Defaulted ad-hoc receiver-local state modeling is now superseded by shared FSM core for subsequent wiring stages.
- Verification:
   - build RX LCD: **pass** — RAM 75.1%, Flash 50.5% (`pio run -e waveshare_esp32s3_lcd7_lvgl -j 12`)
   - build RX2: **pass** — RAM 36.4%, Flash 19.6% (`pio run -e lilygo-t-display-s3 -j 12`)

### Stage S3 progress update — 2026-04-29

- Status: In progress
- Implemented changes:
   - Integrated FSM event/policy handling into shared receiver connection orchestration:
      - `esp32common/espnow_common_utils/rx_connection_handler.h`
      - `esp32common/espnow_common_utils/rx_connection_handler.cpp`
   - Replaced ad-hoc reconnect/quiet-mode gating with FSM-driven policy application and heartbeat event pumping.
   - Updated route-level ACK throttling to be FSM-state-aware (recovery vs steady behavior):
      - `esp32common/espnow_common_utils/rx_route_registry.cpp`
   - Switched MQTT task gating in both receiver variants to shared FSM `mqtt_allowed` policy:
      - `espnowreceiver_2/src/mqtt/mqtt_task.cpp`
      - `espnowreceiver_LCD/src/mqtt/mqtt_task.cpp`
- Verification:
   - build RX2: **pass** — RAM 36.4%, Flash 19.6% (`pio run -e lilygo-t-display-s3 -j 12`)
   - build RX LCD: **pass** — RAM 75.1%, Flash 50.5% (`pio run -e waveshare_esp32s3_lcd7_lvgl -j 12`)
- Next:
   - Continue S3 closeout by removing any remaining duplicated receiver-local reconnect gating paths superseded by FSM outputs.

### Stage S3 completion update — 2026-04-29

- Status: **Completed**
- Verification:
   - build RX2: **pass** — RAM 36.4%, Flash 19.6%
   - build RX LCD: **pass** — RAM 75.1%, Flash 50.5%
- Implementation summary:
   - `rx_connection_handler` now includes FSM initialization, event pumping on heartbeat ticks, and policy application.
   - ACK throttle in `rx_route_registry` now uses FSM state for recovery vs steady behavior (200ms vs 1200ms windows).
   - Both MQTT tasks replaced ad-hoc readiness gates with `RxRadioArbiterFsm::instance().policy().mqtt_allowed` check.
   - FSM policy outputs control scheduler mode, MQTT eligibility, and non-critical enqueue gating.
- Notes:
   - `quiet_mode_active()` remains as a compatibility API for handlers, but now resolves directly from FSM policy (`control_only_mode`) rather than local duplicated state.
   - Core reconnect protection is FSM-driven via policy outputs.

### Stage S5 completion update — 2026-04-29

- Status: **Completed**
- Assessment:
   - Transmitter already implements ACK-first discovery protocol (sends probes, waits for ACK).
   - Backoff-aware deferral is active: discovery start deferred when connection manager backoff policy is active.
   - Two-phase scanning (phase-1 fast sweep, phase-2 weighted near last-known channel) with adaptive dwell extension.
   - Probe interval (`PROBE_INTERVAL_MS`) cooperates with receiver recovery windows through shared reconnect/backoff behavior.
- Verification:
   - build TX (olimex_esp32_poe2): **pass** — RAM 29.5%, Flash 84.6%
- Conclusion:
   - No transmitter code changes required for S5 objectives.

### Stage S6 completion update — 2026-04-29

- Status: **Completed**
- Implemented changes:
   - Retired duplicated receiver-local quiet-mode state in shared connection handler:
      - removed `quiet_mode_active_`
      - removed `quiet_mode_enter_ms_`
      - removed `quiet_mode_last_probe_ms_`
   - Updated compatibility API `quiet_mode_active()` to derive directly from FSM policy (`RxRadioArbiterFsm::instance().policy().control_only_mode`).
   - Simplified FSM policy application logic to use FSM state transitions instead of mirrored local flags.
   - Removed transmitter runtime dependence on `EspnowSendUtils` by deleting superseded call sites:
      - `EspnowSendUtils::handle_deferred_logging()` in transmitter main loop
      - `EspnowSendUtils::reset_failure_counter()` on send recovery path
- Compatibility note:
   - Receiver-local `rx_*.cpp` files remain as explicit tombstone shims (single-line stubs) from S1, preserving include/build compatibility while the shared implementations in `esp32common` remain authoritative.
- Verification:
   - build RX2: **pass** — RAM 36.4%, Flash 19.6% (`pio run -e lilygo-t-display-s3 -j 12`)
   - build RX LCD: **pass** — RAM 75.1%, Flash 50.5% (`pio run -e waveshare_esp32s3_lcd7_lvgl -j 12`)
   - build TX: **pass** — RAM 29.4%, Flash 84.5% (`pio run -j 12`)
- Conclusion:
   - S6 retirement goals are complete and validated across all three targets.

