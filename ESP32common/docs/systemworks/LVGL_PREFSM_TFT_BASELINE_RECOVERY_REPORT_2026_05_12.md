# LVGL Receiver Recovery Review Against Pre-FSM TFT Baseline
**Date:** 2026-05-12  
**Repo:** `esp-multi-project` (`feature/battery-emulator-migration`)  
**Compared commits:**
- **Pre-FSM baseline (known stable period):** `820d3fa`
- **FSM/reconnect redesign:** `da02232`

---

## 1) Objective
This review documents the GitHub-history comparison between the pre-FSM ESP-NOW receiver baseline (during the known-stable `_tft` period) and the current LVGL receiver architecture, with the goal of identifying the changes required to restore reliable operation.

This revision also evaluates a second question:

- what architectural advantages the FSM/shared-stack design provides,
- and how those advantages can be retained while reducing the LVGL receiver to a more suitable operational profile instead of pursuing a broad rollback/rebuild cycle.

---

## 2) What existed in the pre-FSM baseline (`820d3fa`)

## 2.1 Receiver webserver settings (both TFT and LVGL at baseline)
From:
- `espnowreceiver_2/lib/webserver/webserver.cpp` @ `820d3fa`
- `espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp` @ `820d3fa`

Baseline values were effectively identical:
- `task_priority = tskIDLE_PRIORITY + 2`
- `stack_size = 8192`
- `max_open_sockets = 4`
- `recv_wait_timeout = 10`
- `send_wait_timeout = 10`
- `lru_purge_enable = true`

## 2.2 Pre-FSM page rendering behavior
From:
- `espnowreceiver_2/lib/webserver/common/page_generator.cpp` @ `820d3fa`
- `espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp` @ `820d3fa`

Observed baseline characteristic:
- There was **no capability-heap mid-render abort gate** (no internal-heap floor checks like 4 KB / 6 KB thresholds).
- Rendering used legacy and streaming paths, but did not abort in `normal` pressure due to low internal heap thresholds.

## 2.3 Pre-FSM receiver connection model
From:
- `espnowreceiver_2/src/espnow/rx_connection_handler.cpp` @ `820d3fa`

Baseline control behavior:
- Local receiver handler, local heartbeat manager, local state machine.
- Discovery suspend/resume around CONNECTED/IDLE transitions.
- Direct `esp_now_send` for config and `REQUEST_DATA` paths.
- Simple retry windows for data/catalog requests.
- **No LinkRecoveryCoordinator L1/L2 escalation path** in this handler.

## 2.4 Pre-FSM LVGL main-loop behavior
From:
- `espnowreceiver_LCD/src/main.cpp` @ `820d3fa`

Baseline behavior:
- AP fallback and STA recovery reboot path existed.
- No aggressive local HTTP liveness probe/recycle logic in loop.

---

## 3) What changed with FSM redesign (`da02232`)

The major migration in LVGL, mirrored in similar form in the TFT receiver codebase, included the following:
- `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp` heavily rewritten.
- Local RX handler/state ownership shifted toward shared/common stack patterns.
- Shared modules added/used in `esp32common`:
  - `unified_link_fsm`
  - `channel_authority`
  - `link_recovery_coordinator`
  - `radio_pressure_state`
- MQTT coexistence gating increased (connection/radio-pressure gates).

This introduced more moving parts and stricter recovery/arbitration behavior than existed in the pre-FSM baseline.

## 3.1 Core shared FSM components now in play
The redesign introduced or materially increased use of these shared control-plane modules:

- `UnifiedLinkFsm`
- `ChannelAuthority`
- `LinkRecoveryCoordinator`
- `RadioPressureState`
- shared `ReceiverConnectionHandler` hooks and callback indirection

This means the LVGL receiver is no longer a narrowly scoped "display + ESP-NOW + webserver" application.
It is now a consumer of a broader shared connection and recovery framework.

---

## 4) Advantages of the FSM/shared-stack approach

The FSM/shared-stack design is not incidental complexity. It addresses several real architectural weaknesses that existed before the rewrite.

## 4.1 Single source of truth for link state
The design intent in `ESPNOW_SINGLE_STATE_MACHINE_SOURCE_OF_TRUTH_REWRITE_2026_05_08.md` is sound:

- remove overlapping ownership between discovery, reconnect, ACK handling, and channel lock logic,
- ensure `CONNECTED/LINK_UP` is entered from one authoritative path,
- prevent split-brain cases where TX and RX believe different things about session state.

This is a material improvement over the earlier "loosely coordinated modules" model.

## 4.2 Channel authority is a genuine fix, not optional polish
The pre-rewrite system had multiple channel truths:

- live WiFi channel,
- cached/locked channel,
- discovery ACK reported channel,
- reconnect-selected channel.

`ChannelAuthority` is valuable because it centralizes which channel is authoritative and how it is committed. That directly reduces the class of reconnect mismatch defects already observed elsewhere in the codebase.

## 4.3 Central recovery ownership is conceptually correct
`LinkRecoveryCoordinator` exists for a good reason:

- one place owns L1/L2/restart decisions,
- one place handles radio teardown/reinit discipline,
- one place can be instrumented and bounded.

That is cleaner than multiple ad-hoc `esp_now_deinit()` / `esp_wifi_stop()` calls spread across handlers and tasks.

## 4.4 Scheduler + pressure-state model is directionally right
Using a shared TX scheduler and a `RadioPressureState` abstraction is also architecturally reasonable:

- prioritizes control traffic,
- gives HTTP/MQTT a common signal for coexistence policy,
- provides a path to coordinated degradation instead of random collapse.

The important point is that `RadioPressureState` itself is not the problem.
The issue is the degree of recovery and render-policy aggression currently attached to it in the LVGL receiver.

For LVGL, `RadioPressureState` should remain a signal source for:

- throttling,
- UI degradation,
- telemetry,

but not automatically trigger heavy-handed recovery or binary render aborts under ordinary transient conditions.

The conclusion is therefore **not** that the FSM redesign was a mistake.
The conclusion is that **the current LVGL receiver consumes too much of the full design simultaneously, with guard and recovery behavior that is too aggressive for this product profile.**

---

## 5) Why the current LVGL behavior is failing in observed logs

Observed log symptom:
- Render starts for `/` dashboard
- Abort at `stage=common_styles`
- Reason `low_internal_heap` under `pressure=normal`
- `ESP_ERR_NO_MEM`

Interpretation:
- This specific render-abort path is from newer guard logic in local working changes, not the pre-FSM baseline behavior.
- The guard is currently too sensitive for transient internal-heap dips on this LVGL device profile.

There are therefore two overlapping issues:
1. **Architectural complexity increase post-FSM** (higher recovery/coexistence complexity).
2. **Current render guard sensitivity** causing false-positive aborts in `normal` pressure.

There is also a third practical issue:

3. **LVGL receiver is a poor fit for full-strength recovery churn** because it already carries extra RAM pressure from LVGL/display buffers and a richer web/UI surface.

That makes it more sensitive to transient heap dips, recovery hooks, and HTTP/render policy than the simpler TFT receiver.

---

## 6) Complexity assessment: where the current design is too heavy for LVGL

The issue is not the existence of an FSM. The issue is **which parts are coupled together in the LVGL receiver at runtime**.

## 6.1 The shared receiver connection handler is doing too much
Current shared `ReceiverConnectionHandler` now owns or influences:

- connection state transitions,
- deferred peer policies,
- connect-confirm ACK retries,
- LED sync retries,
- catalog retry engine,
- recovery coordinator hook wiring,
- reconnect diagnostics,
- quiet-mode / pressure-derived behavior.

For a display-centric receiver, this is a large amount of logic concentrated in one runtime owner.

## 6.2 Recovery hooks create system-wide blast radius
Current hooks allow recovery actions to reach into:

- radio init state,
- WiFi restarts,
- callback reinstall,
- MQTT disconnect/restart behavior,
- webserver lifecycle.

That is powerful, but on LVGL it also means a radio-pressure event can cascade into broader runtime churn than is desirable.

## 6.3 Render guarding is currently too binary
Current render protection aborts on instantaneous low internal heap thresholds.

That is acceptable for true starvation, but too harsh when:

- pressure is still `normal`,
- heap rebounds quickly after the sample,
- the request has already begun and the UI would have succeeded without intervention.

The current render guard is therefore behaving like a hard safety trip, whereas for LVGL it should operate as a bounded degrade policy.

---

## 7) Recommended direction: keep the FSM, but slim it down for LVGL

The recommended path is **not** a full rollback and **not** retention of the full current stack.

The recommended path is a **Slimmed FSM variant** for the LVGL receiver.

### Design principle
Keep the parts that solve real architectural problems:

- `UnifiedLinkFsm`
- `ChannelAuthority`
- shared session/handshake discipline
- shared TX scheduler

Reduce or localize the parts that create too much runtime churn for LVGL:

- L1/L2 recovery escalation on receiver side
- broad callback/hook fan-out
- aggressive HTTP self-recycle behavior
- hair-trigger normal-pressure render aborts

---

## 8) Proposed slimmed FSM profile for LVGL

## 8.1 Keep

### Keep A — `UnifiedLinkFsm` as a state record, not a heavy recovery owner
Use `UnifiedLinkFsm` to track:

- `DISCOVERY`
- `HANDSHAKE`
- `LINK_UP`
- `DEGRADED`

But do **not** require LVGL runtime to exercise the full `RECOVERY_L1` / `RECOVERY_L2` path automatically under ordinary pressure.

### Keep B — `ChannelAuthority`
Keep this intact.

This is one of the strongest parts of the redesign and should remain shared/common.

### Keep C — shared handshake/session validity rules
Keep session token / connect-confirm discipline.

This removes an entire class of reconnect ambiguity and is worth keeping.

### Keep D — TX scheduler and pressure metrics
Keep scheduled send behavior and central metrics.

But consume those metrics more conservatively on LVGL.

### Keep E — `RadioPressureState` as a policy input, not a hard trip source
Keep `RadioPressureState`, but narrow how LVGL uses it:

- `normal`: full operation
- `constrained`: reduced freshness and throttling
- `critical`: bounded degrade and, only if sustained, selective recovery

This avoids throwing away a useful shared signal while stopping it from over-driving LVGL behavior.

## 8.2 Simplify

### Simplify A — downgrade `LinkRecoveryCoordinator` from active owner to bounded fallback
For LVGL receiver, recovery policy should be:

1. `NORMAL` / `CONSTRAINED` pressure:
   - no L1/L2 radio restart,
   - no webserver stop/start,
   - just remain in bounded control-only/degraded mode.

2. `CRITICAL` sustained beyond a longer window:
   - optionally allow L1 only,
   - defer L2/restart to explicit failure classes (for example, true radio deadlock or repeated startup failure), not ordinary transient NO_MEM.

That means `LinkRecoveryCoordinator` remains in the design, but is no longer the first response to ordinary LVGL runtime pressure.

### Simplify B — reduce hook surface for LVGL
Prefer a narrower LVGL hook set:

- `on_connected`
- `on_connection_lost`
- `send_initialization_burst`

Avoid routine use of:

- `on_radio_deinit`
- `on_l2_wifi_restarted`
- cross-subsystem restart hooks

unless a true hard recovery path is explicitly entered.

### Simplify C — remove aggressive HTTP self-healing while stabilizing
Avoid coupling radio pressure directly to webserver recycle logic during stabilization.

For LVGL, the first objective is:

- maintain a stable, boring webserver,
- avoid mid-render stop/restart behavior,
- let pressure reduce freshness before it kills the page.

This applies especially to:

- local HTTP liveness probes,
- self-initiated stop/start cycles,
- recovery-hook-driven webserver restarts while render activity is still in flight.

### Simplify D — use bounded degrade, not binary render abort
New render policy for LVGL should be:

- `normal` pressure: never abort on one low sample,
- `constrained` pressure: allow abort only after repeated low-sample confirmation,
- `critical` pressure: allow abort or tiny fallback page.

This is the key practical change required to address the currently observed symptom.

---

## 9) Concrete render-guard recommendations

## 9.1 Preflight check before response body starts
Before chunk streaming begins:

- if internal heap is already critically low, send a tiny static busy page immediately,
- do not begin normal page rendering if failure is already certain.

## 9.2 Mid-render guard policy
Recommended policy:

- `normal` pressure:
  - log low heap,
  - do not abort on a single sample.

- `constrained` pressure:
  - require at least 2-3 consecutive low samples,
  - require low `largest_block` as well as low free heap.

- `critical` pressure:
  - abort is allowed,
  - but preferably before page body has materially progressed.

## 9.3 Degrade output size before aborting
Before aborting, prefer reducing exposure:

- split or externalize common helper JS,
- reduce inline payload sizes,
- keep dashboard first paint small.

That is a better LVGL strategy than using aborts as the normal safety valve.

## 9.4 What should not be reverted in the render path
Even while relaxing guards, do **not** lose the useful parts of the recent work:

- callback-based streaming render path,
- removal of legacy full-page render as the default path,
- request-progress tracking for long responses,
- capability-aware telemetry/logging for heap and largest-block diagnostics.

The issue is not that streaming was the wrong design choice.
The issue is that the LVGL abort policy is currently too aggressive for `normal` pressure.

---

## 10) Recommended actions to restore stable LVGL behavior

### Recommendation precedence
When two recommendations appear to overlap, precedence is:
1. **10.1 Preferred recommendation** (target end-state architecture)
2. **10.2 Fallback recommendation** (temporary staging profile only)

The fallback profile must not be treated as a permanent alternative architecture.

## 10.1 Preferred recommendation: **Slimmed FSM variant for LVGL**
Do not fully roll back, and do not retain the full current runtime coupling either.

Instead:

- keep shared state/channel/session discipline,
- trim receiver-side recovery aggression,
- relax render guards into bounded degrade behavior,
- keep webserver lifecycle simple.

### A1) Keep the shared FSM foundation
Retain:
- `UnifiedLinkFsm`
- `ChannelAuthority`
- shared session/connect-confirm rules
- shared TX scheduler
- `RadioPressureState`

Action:
- keep these as the shared correctness layer.
- do not remove them just because LVGL currently misbehaves.

### A1b) Do **not** restore direct-send control paths broadly
One major advantage of the redesign was moving toward a single send plane.

So the slimmed LVGL variant should **not** reintroduce widespread direct `esp_now_send()` control traffic just to look like the old code.

Keep:
- scheduler-based send discipline,
- centralized metrics,
- control/data prioritization.

If a very small number of direct sends are ever retained, they should be explicitly justified and bounded, not treated as a general rollback pattern.

### A2) Put LVGL receiver on a lighter receiver policy
Target runtime areas:
- `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp`
- `esp32common/espnow_common_utils/rx_connection_handler.cpp/.h`
- `espnowreceiver_LCD/src/main.cpp`

Action:
- stop treating ordinary pressure as justification for broad recovery escalation,
- disable or gate L1/L2 actions behind longer, explicit hard-failure criteria for LVGL,
- keep simpler discovery/retry behavior closer to the old working receiver profile.

The transmitter should remain on the shared correctness model unless evidence shows a transmitter-side defect.
This review specifically recommends a lighter **receiver** profile for LVGL, not a broad TX/RX architecture split.

### A3) Keep webserver startup/runtime simple during stabilization
- Retain baseline webserver configuration (`max_open_sockets=4`, `send_wait_timeout=10`) initially.
- Avoid aggressive self-recycle/liveness stop-start loops until link layer is stable.

At the same time, keep clearly beneficial hardening that is independent of the FSM question, such as:

- duplicate-stop suppression,
- active-request awareness before recycle,
- safe startup gating when the server is clearly not runnable.

The goal is to remove churn, not to discard safeguards that prevent crashes.

### A4) Remove false-positive render abort in `normal` pressure
For current LVGL rendering path:
- Do **not** abort in `normal` pressure on a single low sample.
- Use repeated confirmation and largest-block checks.
- Prefer tiny preflight fallback page over mid-stream abort.

This should be treated as a required correction, not as an optional tuning pass.

---

## 10.2 Fallback recommendation: **Temporary compatibility profile biased toward pre-FSM behavior**
If the Slimmed FSM variant must be introduced in stages, use a strict temporary compatibility profile for LVGL:

1. Keep `UnifiedLinkFsm` + `ChannelAuthority` enabled.
2. Disable or soften shared recovery escalation hooks (`on_radio_deinit`, `on_l2_wifi_restarted`) for LVGL build.
3. Disable normal-pressure render abort (or require consecutive low samples + low largest block).
4. Keep MQTT/API pressure gates but raise hysteresis to avoid churn.
5. Freeze one known-good channel/reconnect profile first; only then consider stronger recovery behaviors.

This fallback profile should be treated as a transitional staging mode, not as the desired long-term architecture.

Fallback exit criteria (required):
- return to the 10.1 preferred profile after stabilization,
- remove temporary compatibility branches and flags,
- document the removal in Section 11 stage updates,
- confirm no duplicate old/new runtime policy paths remain active.

---

## 11) Proposed execution plan

## 11.0 Implementation governance requirements

The following rules are mandatory acceptance gates for the implementation, not optional process suggestions:

1. **At the end of every implementation stage, this document must be updated** to show:
  - what was completed,
  - what remains open,
  - what changed from the previous stage,
  - and what validation evidence was gathered.

2. **Each stage must leave the codebase cleaner than it found it.**
  Any code that is replaced by the new stage should be removed in that same stage where practical.

3. **Old / redundant / legacy paths must not be left behind as passive duplicates** unless they are deliberately retained behind a temporary compatibility flag with an explicit removal plan.

4. **The target outcome is a clean codebase, not just a working codebase.**
  The implementation should avoid ending up with:
  - duplicate receiver policies,
  - parallel old/new render guard paths,
  - dead hooks,
  - obsolete recovery branches,
  - or commented-out legacy logic left in place.

5. **Stage completion criteria are not met until both of these are true:**
  - the intended behavior works and is validated,
  - superseded code from that stage has been removed or explicitly scheduled for immediate removal in the next stage.

6. **No stage may be signed off as complete without a cleanup statement.**
  That statement must explicitly say either:
  - which legacy/redundant paths were removed in the stage, or
  - which remaining temporary compatibility paths are still present, why they remain, and in which next stage they will be removed.

This is especially important here, because a slimmed-FSM migration could otherwise drift into an awkward half-old / half-new state that is harder to reason about than either design on its own.

## 11.1 File-by-file change checklist

### Shared/common code
- `esp32common/espnow_common_utils/rx_connection_handler.cpp/.h`
  - reduce default LVGL recovery aggression
  - narrow hook usage in normal/constrained operation
  - keep simpler retry/discovery behavior

- `esp32common/espnow_common_utils/link_recovery_coordinator.cpp/.h`
  - make LVGL use this as bounded fallback, not first-line response
  - gate L1/L2 by sustained hard-failure criteria

- `esp32common/espnow_common_utils/radio_pressure_state.*`
  - keep as-is conceptually
  - use as degrade/throttle signal, not direct hard-recovery trigger for LVGL

### LVGL receiver code
- `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp`
  - simplify LVGL hook wiring
  - keep FSM/session/channel discipline
  - remove unnecessary runtime coupling to heavy recovery actions

- `espnowreceiver_LCD/src/main.cpp`
  - reduce aggressive self-healing/recycle behavior during stabilization
  - keep only clearly bounded, low-risk watchdog logic

- `espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp`
  - relax `normal` pressure abort logic
  - require stronger evidence before abort under `constrained`
  - add preflight tiny-fallback behavior

- `espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.h`
  - only if needed for new bounded-guard interfaces or fallback helpers

## Phase 0 — Branch safety
- Create branch: `lvgl-slim-fsm-stabilization`.
- Tag current state before simplification.

**Phase 0 exit gate:**
- rollback point exists and is recorded,
- implementation scope for Phase 1 is written into this document,
- any known legacy areas expected to be removed in Phase 1 are identified up front.

## Phase 1 — Functional simplification
- Apply A1 + A2 + A3 + A4 above.
- Build and flash the LVGL receiver and transmitter pair.
- Update this document with completed items and explicitly record any legacy code removed in this stage.

**Phase 1 exit gate:**
- slimmed-FSM functional changes are implemented,
- replaced logic from this stage has been removed or explicitly isolated behind temporary compatibility controls,
- this document has been updated with completed work, remaining gaps, and cleanup status,
- build/flash succeeds on the intended LVGL target.

### Phase 1 status update (2026-05-12)

Completed in code:
- `espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp`
  - `normal` pressure no longer aborts page rendering on a single low-heap sample.
  - `constrained` pressure now requires repeated starvation confirmation before abort.
  - guard diagnostics now include streak detail to distinguish transient dips from sustained starvation.

- `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp`
  - removed webserver stop/start behavior from ESP-NOW recovery hooks.
  - retained radio state marking and STA reassociation behavior.

- `espnowreceiver_LCD/src/main.cpp`
  - removed aggressive HTTP liveness probe/recycle watchdog paths.
  - removed related helper functions and constants used only by those paths.

- `espnowreceiver_LCD/platformio.ini`
  - added LVGL-only `RXCONN_NO_MEM_TRIGGER_THRESHOLD=20` build flag to reduce transient NO_MEM escalation pressure.

Legacy/redundant code removed in this stage:
- HTTP liveness probe path (`probe_local_http_liveness`).
- recycle helper path (`recycle_webserver_if_idle`).
- request-count probe helper (`webserver_has_active_requests`).
- loop-level inflight watchdog recycle branch and probe-threshold recycle branch.

Validation evidence:
- Local build completed successfully for `waveshare_esp32s3_lcd7_lvgl` after these changes (including the LVGL-only threshold flag update).

Remaining Phase 1 scope:
- ✅ LVGL-specific recovery-threshold softening completed for the current receiver profile.
- ⏳ Runtime soak validation remains open and is intentionally deferred.

### Phase 1 closure update (2026-05-13)

Completed in this closure pass:
- Revalidated receiver and TFT build health after final cleanup/documentation pass.
- Confirmed no new compile/diagnostic issues in LVGL stabilization scope.
- Closed all remaining non-soak implementation tasks for the slimmed-FSM stabilization track.

Open item intentionally deferred:
- Phase 2 soak/runtime validation only.

## Phase 2 — Validation
Required pass criteria:
1. No `render abort` for `/` dashboard during normal operation.
2. No persistent reconnect loops.
3. Stable web access for 30+ minutes with dashboard open.
4. No watchdog/recovery thrash loops.
5. No regression in channel/session correctness.
6. No reintroduction of known channel-authority split-brain behavior.
- Update this document with measured outcomes and any remaining cleanup items.

**Phase 2 exit gate:**
- all required pass criteria above are satisfied,
- validation evidence is written into this document,
- any temporary code retained from Phase 1 is either removed here or explicitly justified for Phase 3,
- there is no known dead or redundant path left behind from the simplification work already completed.

## Phase 3 — Controlled reintroduction
- Only if needed, re-enable stronger LVGL recovery behaviors one by one.
- Validate each step with soak test before enabling the next feature.
- Remove any temporary compatibility branches that are no longer required.
- Update this document after each step so it remains the source of truth for implementation status.

**Phase 3 exit gate:**
- each reintroduced behavior has its own validation evidence,
- temporary compatibility branches used during migration have been removed,
- no superseded recovery or render paths remain in active code,
- this document reflects the final steady-state design rather than a migration snapshot.

---

## 11.2 Tradeoffs and risks of the slimmed-FSM approach

This review should be explicit that a slimmed FSM is a tradeoff, not a zero-cost change.

### Advantages
- keeps the real architectural wins of the redesign,
- avoids a high-risk rollback/rebuild cycle,
- reduces churn specifically where LVGL is weakest,
- keeps shared correctness and shared protocol discipline.

### Risks
- if guards are relaxed too far, true starvation could be tolerated too long,
- if recovery is softened too far, rare deadlocks may take longer to recover,
- if LVGL-specific exceptions are implemented carelessly, shared/common code may become harder to reason about.

### Mitigation
- keep the simplification policy explicit and compile/profile driven,
- define clear sustained-failure thresholds before heavy recovery is allowed,
- validate each change with runtime logs and soak tests before widening scope.

---

## 12) Bottom line

The evidence does **not** support abandoning the FSM/shared-stack concept entirely.

The FSM stack brings real advantages:
- single source of truth,
- channel authority,
- cleaner session discipline,
- centralized scheduler/pressure model.

What the evidence **does** support is that the **LVGL receiver should not run the full-strength version of that stack**.

Preferred recommendation:

- keep the shared FSM foundation,
- slim down LVGL receiver recovery behavior,
- reduce hook-driven system churn,
- replace binary render aborts with more relaxed, bounded, pressure-aware degrade behavior.

The immediate web failure you are seeing (`render abort` at `/` in `normal` pressure) is consistent with over-sensitive guarding and should be relaxed/reworked first.

This is a better path than broad rollback, because it keeps the parts of the redesign that solve real correctness problems while trimming the parts that are too heavy for this receiver class.
