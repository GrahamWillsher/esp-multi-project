# ESP-NOW Full Rewrite: Single State Machine + Single Source of Truth
**Date:** 2026-05-08  
**Author:** Systemworks Investigation  
**Status:** DESIGN BASELINE (ground-up rewrite plan)

---

## 1) Executive Decision

This system must stop using overlapping connection/channel controllers.

**Final architecture decision:**
1. **One authoritative link state machine (TX-authoritative).**
2. **Receiver is the definitive channel authority; TX follows receiver-confirmed channel via shared `ChannelAuthority`.**
3. **One send plane (`EspnowTxScheduler`) for all ESP-NOW packets (including handshake/control).**
4. **One session epoch (`session_id`) that gates every CONNECTED transition on both sides.**

This removes split-brain between:
- `EspNowConnectionManager`
- `TxReconnectManager`
- `TxStateMachine`
- `RxRadioArbiterFsm`
- ad-hoc channel signals (`WiFi.channel()`, `ChannelManager`, `g_lock_channel`, ACK payload channel)

---

## 2) What is broken today (root causes)

## 2.1 State ownership is fragmented

Current behavior is driven by multiple independent FSMs and callback paths. This creates contradictory truths:
- TX can be logically reconnecting while RX is in a different recovery phase.
- CONNECTED can be entered from non-identical evidence.
- Recovery actions are initiated from multiple layers with different timers.

## 2.2 Channel ownership is fragmented

At least four channel “truths” exist in code paths:
- live radio channel (`esp_wifi_get_channel` / `WiFi.channel()`)
- `ChannelManager` cached/locked channel
- transmitter `g_lock_channel`
- channel reported by discovery ACK payload

Result: drift and repeated mismatch loops.

## 2.3 Send path is partly centralized, partly direct

Most traffic uses scheduler, but selected control paths still do direct `esp_now_send` at critical moments. Under pressure, this causes:
- callback ordering gaps
- inconsistent retries
- token watchdog fallback behavior
- false escalation into reconnect/reinit loops

## 2.4 Recovery ladder is not single-owner

NO_MEM handling, reconnect timing, and callback reinstall are spread across route, handler, and runtime layers. This creates reinit races and repeated cycles.

---

## 3) Rewrite principles (non-negotiable)

1. **Single write authority for connection state:** TX.
2. **RX never self-elects CONNECTED without TX session confirmation.**
3. **Receiver-origin channel truth:** RX WiFi channel is definitive; TX adopts receiver-confirmed operating channel.
4. **All packet transmission (data/control/handshake/acks) uses scheduler API only.**
5. **Every CONNECTED session is tied to `session_id` epoch.**
6. **All recovery actions are executed by one recovery coordinator per device.**
7. **No background validator may mutate channel/state directly.**
8. **Common code first:** all FSM/channel/recovery policy lives in `esp32common`; project repos keep adapter-only logic.

---

## 4) Target architecture

## 4.1 Unified components

### 4.1.1 `UnifiedLinkFsm` (shared in `esp32common`)

A single FSM definition used by both TX and RX codegen/runtime.

States:
- `BOOTSTRAP`
- `DISCOVERY`
- `HANDSHAKE`
- `LINK_UP`
- `DEGRADED`
- `RECOVERY_L1`
- `RECOVERY_L2`
- `RESTART_REQUIRED`

**Authority rule:**
- TX instance is authoritative and emits session state.
- RX instance is mirror mode and accepts only TX-authorized link-up epochs.

### 4.1.2 `ChannelAuthority` (shared)

Single module that owns:
- current radio channel sample
- intended operating channel
- lock state
- last good channel + confidence

Authority model:
- RX publishes definitive `receiver_home_channel` (derived from live STA channel).
- TX never treats local cache/global variables as final truth; TX locks only after receiver-confirmed channel evidence.
- Persisted channel is a reconnect hint only, never stronger than live receiver-confirmed evidence.

Only this module may call `esp_wifi_set_channel` in steady-state runtime (except controlled discovery worker scope).

### 4.1.3 `LinkRecoveryCoordinator` (shared API, device-specific hooks)

One entry point for:
- transient NO_MEM backoff
- L1 ESP-NOW reinit
- L2 WiFi restart
- reboot decision

No other layer directly performs recovery primitives.

### 4.1.4 `SessionGate`

Connection is valid only when:
$$
\text{session\_id}_{rx} = \text{session\_id}_{tx} \land \text{phase} = \text{LINK\_UP}
$$

Any packet with stale epoch is ignored for state transitions.

---

## 4.2 Canonical handshake (authoritative)

1. TX `DISCOVERY`: probe/hop scan finds receiver evidence.
2. TX seeds discovery using `last_good_channel` first, then enters controlled channel-hop sequence for remaining channels.
3. RX ACK/confirm path provides definitive `receiver_home_channel` (live WiFi channel).
4. TX sets candidate channel via `ChannelAuthority` and starts `HANDSHAKE(session_id++)`.
5. TX sends `CONNECT_CONFIRM(session_boot_nonce, session_id, channel, capabilities)` via scheduler.
6. RX receives confirm, validates tuple `(session_boot_nonce, session_id)`, replies `CONNECT_CONFIRM_ACK(session_boot_nonce, session_id)` via scheduler.
7. TX receives matching ack, enters `LINK_UP` and emits authoritative link-up event.
8. RX enters `LINK_UP` only after confirming same `session_token` and local ack-send success path.

No other path may enter `LINK_UP`.

## 4.2.1 Session identity and timing-safety contract

To prevent historical `session_id` mismatch conflicts, session identity is a 2-part token:

$$
	ext{session\_token} = (\text{session\_boot\_nonce},\ \text{session\_id})
$$

Definitions:
- `session_boot_nonce` (16-bit): random per transmitter boot, regenerated on every restart.
- `session_id` (16-bit): monotonic per boot, incremented only when entering new `HANDSHAKE` after discovery success.

Acceptance rules (TX and RX):
1. ACK/confirm packets are accepted only in `HANDSHAKE`/`CONFIRMING` window.
2. Packet must match expected peer MAC, `session_boot_nonce`, and `session_id`.
3. Packet arrival age must be within handshake validity window (see Section 7.1).
4. Any mismatch is counted, logged, and ignored (no state change).

Why this removes timing conflicts:
- Reboot reuse collision is eliminated because `session_boot_nonce` changes each boot.
- Counter wrap collision is eliminated in practice because equality requires both nonce + id and valid handshake window.
- Delayed/out-of-order packets from old attempts fail token and/or window checks.

## 4.3 Channel hopping and backoff process (required behavior)

Reconnect/discovery algorithm:
1. Build ordered channel plan:
    - `C0 = last_good_channel` (if valid 1..13)
    - then all other channels once each (no duplicates)
2. **Pass 1 (fast sweep):** scan each channel with base dwell.
3. **Pass 2 (weighted local sweep):** if `C0` exists, rescan `C0`, `C0-1`, `C0+1` with extended dwell.
4. If still not found, apply scan-cycle backoff and repeat from step 1.

Backoff policy (shared constant set in `esp32common`):
- cycle 0: 0 ms
- cycle 1: 3000 ms
- cycle 2+: 5000 ms

Guardrails:
- Backoff increments only after a **completed miss cycle** (not on duplicate CONNECTING re-entry).
- Any positive probe/ACK evidence resets cycle counter.
- Channel write/lock is committed only through `ChannelAuthority` after receiver-confirmed channel evidence.

## 4.4 Deterministic reconnection outcomes (state-action table)

For each event, behavior is fixed and single-owned:

| Condition | Mandatory action | Next state |
|---|---|---|
| Probe/ACK found with valid channel | Set candidate channel via `ChannelAuthority`; send `CONNECT_CONFIRM` | `HANDSHAKE` |
| `CONNECT_CONFIRM_ACK` matches current `session_id` | Commit link-up, persist `last_good_channel`, clear backoff | `LINK_UP` |
| `CONNECT_CONFIRM_ACK` missing at timeout | Resend confirm (max retry budget) | `HANDSHAKE` |
| Confirm retry budget exhausted | Restart discovery cycle and apply scan-cycle backoff | `DISCOVERY` |
| Heartbeat freshness timeout in `LINK_UP` | Enter degraded policy, stop data-plane assumptions | `DEGRADED` |
| NO_MEM threshold crossed in `DEGRADED`/`LINK_UP` | Invoke coordinator escalation ladder | `RECOVERY_L1` then `RECOVERY_L2` if needed |
| Recovery succeeds | Re-enter discovery + handshake (new `session_id`) | `DISCOVERY` |
| Recovery exhausted | Controlled restart | `RESTART_REQUIRED` |

No undefined branch is allowed. Unknown events are metered and ignored.

---

## 5) Single source of truth data model

Define one struct (shared header):

```cpp
struct LinkTruth {
    uint16_t session_boot_nonce;
    uint16_t session_id;
    LinkPhase phase;                 // BOOTSTRAP..LINK_UP..
    uint8_t operating_channel;       // authoritative runtime channel
    uint8_t last_good_channel;
    uint32_t phase_enter_ms;
    uint32_t last_link_activity_ms;
    bool control_only_mode;
    RecoveryLevel recovery_level;    // NONE/L1/L2/RESTART
    uint32_t no_mem_consecutive;
};
```

Rules:
- Only `UnifiedLinkFsm` mutates `phase`/`session_id`.
- Only `ChannelAuthority` mutates channel fields.
- Other modules subscribe read-only snapshots.

Session safety additions:
- `session_boot_nonce` is set once at boot and never mutated until reboot.
- `session_id` increments strictly on new handshake epochs (not on confirm resend retries).
- `LINK_UP` requires an exact `session_token` match, not `session_id` alone.

`last_good_channel` update rule:
- Update only on successful handshake completion (`HANDSHAKE -> LINK_UP`) with matching `session_id`.
- Persist in common storage (`esp32common`) as reconnect hint.
- On reconnect, TX must attempt this channel first before full sweep.

---

## 6) Failure and recovery matrix (complete coverage)

## 6.1 Boot and ordering cases

1. **TX boots first, RX absent** → TX stays `DISCOVERY`, bounded scan/backoff.
2. **RX boots first, TX absent** → RX stays mirror-waiting, no false CONNECTED.
3. **Both boot simultaneously** → single handshake with monotonic `session_id`.
4. **One side reboots during handshake** → stale `session_id` dropped, restart discovery.

## 6.2 Channel drift cases

5. **AP channel changed while disconnected** → discovery ACK-reported home channel seeds candidate; `ChannelAuthority` confirms live before lock.
6. **AP channel changed while connected** → heartbeat loss + channel mismatch event -> `DEGRADED`, then rediscovery.
7. **Live WiFi channel differs from lock cache** → detected as truth violation; `ChannelAuthority` corrects cache, does not allow dual-write.
8. **NVS saved channel stale/corrupt** → only hint; never authoritative over live STA-connected channel.
9. **Receiver-only WiFi authority constraint** → TX must not force final channel absent receiver-confirmed evidence.

## 6.3 Handshake packet-loss cases

10. **`CONNECT_CONFIRM` lost** → TX retries in `HANDSHAKE` window; then return `DISCOVERY`.
11. **`CONNECT_CONFIRM_ACK` lost** → TX retries confirm; RX handles duplicate idempotently.
12. **Duplicate delayed ACK from old session** → dropped by `session_id` gate.
13. **Out-of-order confirm/ack** → invalid transition; counted + ignored.

## 6.4 NO_MEM and scheduler pressure cases

14. **Transient NO_MEM burst (< threshold)** → scheduler retry/backoff only.
15. **Persistent NO_MEM with callback alive** → coordinator triggers L1 after threshold.
16. **Persistent NO_MEM + callback silence** → coordinator escalates faster to L2.
17. **L1 success, recurring quickly** → bounded retry budget then L2.
18. **L2 success, recurring quickly** → bounded retry budget then reboot-required.
19. **Recovery exhausted** → explicit `RESTART_REQUIRED` and controlled restart.

## 6.5 Peer table / callback integrity cases

20. **peer stale channel entry** → always remove+re-add channel=0 on handshake boundaries.
21. **send callback lost after reinit** → recovery coordinator re-registers before leaving recovery.
22. **recv callback lost** → same mandatory reinstall gate.
23. **broadcast peer missing** → recovered as part of post-recovery bring-up checklist.

## 6.6 Runtime traffic and service coexistence cases

24. **HTTP/MQTT burst causes temporary radio pressure** → enter `DEGRADED` control-only, avoid immediate L1.
25. **Long quiet after reconnect** → remain `LINK_UP` if heartbeat fresh; no spurious rediscovery.
26. **RX has no fresh power data but link alive** → application retry only; no link-state demotion.
27. **MQTT disconnected while ESP-NOW healthy** → independent service state, no link demotion.

## 6.7 Reset and persistence cases

28. **Cold boot with stale persisted session/channel** → session always regenerated; persisted channel only hint.
29. **Brownout/restart mid-recovery** → startup enters `BOOTSTRAP` then deterministic discovery.
30. **Factory reset/NVS clear** → no assumptions; full discovery handshake path.

---

## 7) State transition contract

Allowed transitions (authoritative graph):

- `BOOTSTRAP -> DISCOVERY`
- `DISCOVERY -> HANDSHAKE`
- `HANDSHAKE -> LINK_UP`
- `HANDSHAKE -> DISCOVERY` (timeout/mismatch)
- `LINK_UP -> DEGRADED` (freshness/channel/send pressure faults)
- `DEGRADED -> LINK_UP` (recovered without re-discovery)
- `DEGRADED -> RECOVERY_L1`
- `RECOVERY_L1 -> DISCOVERY`
- `RECOVERY_L1 -> RECOVERY_L2`
- `RECOVERY_L2 -> DISCOVERY`
- `RECOVERY_L2 -> RESTART_REQUIRED`
- `RESTART_REQUIRED -> BOOTSTRAP` (after reboot)

Any other transition is invalid and must be rejected + metered.

## 7.1 Timeout and cadence contract (explicit values)

All values are defined in shared timing config (`esp32common`) to prevent project drift.

- Event processing cadence: 100 ms
- Discovery per-channel base dwell: 1000 ms
- Discovery weighted dwell: center 2000 ms, neighbors 1500 ms
- Probe send cadence while dwelling: 200 ms
- Post channel-set settle delay: 50 ms
- Signal-extension window on discovery traffic: +500 ms
- Handshake confirm-ack timeout: 2000 ms
- Handshake retry interval (confirm resend): 2000 ms
- Max confirm retries before returning to discovery: 2
- Handshake packet validity window (TX accept): 2500 ms from most recent confirm send
- Handshake stale-drop window on RX pending ACK context: 5000 ms
- Scan-cycle backoff schedule: [0 ms, 3000 ms, 5000 ms, 5000 ms...]
- Connected heartbeat timeout (link freshness loss): 35000 ms
- ACK token watchdog timeout: 3000 ms
- Consecutive NO_MEM trigger threshold: 10
- L1 reinit inter-attempt cool-off gate reset window: 30000 ms
- L1 attempt budget per escalation window: 3
- L2 attempt budget per escalation window: 3

Rules:
1. These values are single-sourced in common code and imported by TX/RX apps.
2. App-local overrides must be explicit and justified per board profile.
3. Documentation and telemetry labels must use the same constant names.
4. If an override changes recovery safety behavior, it must include an updated scenario test record.
5. `session_token` checks must use identical constants on TX and RX; no side-specific drift.

## 7.2 Numeric maximums and wraparound behavior (must be deterministic)

This section defines what happens when counters/timers hit max values.

**Scope (project-wide, mandatory):**
- Applies to all repositories in this workspace (`esp32common`, transmitter, receiver variants, and dependent runtime modules).
- Applies to all numeric state/timer/counter fields, not only ESP-NOW handshake fields.
- New shared implementation baseline: `esp32common/include/esp32common/patterns/numeric_safety.h`.

| Field | Type | Max event | Required behavior |
|---|---|---|---|
| `millis()` timestamps | `uint32_t` | wraps every ~49.7 days | All timeout checks use elapsed-delta arithmetic (`now - then`) or signed-delta due checks. Never compare absolute times with `>` alone. |
| `session_id` | `uint16_t` | `0xFFFF -> 0x0000` | Wrap is allowed, but `LINK_UP` admission requires full `session_token` match (`session_boot_nonce + session_id + peer MAC + validity window`). |
| `session_boot_nonce` | `uint16_t` | random collision possibility | Regenerated per boot; collision risk is reduced further by MAC + timing window checks. |
| TX confirm retry counter | `uint8_t` | reaches retry budget | **Saturate at budget** and transition to `DISCOVERY`/backoff. Must never free-run to overflow. |
| RX pending confirm-ack retry counter | `uint8_t` | reaches retry budget | **Saturate at budget** then clear pending context and force rediscovery path. Must not roll over to 0 and continue silently. |
| NO_MEM L1/L2 counters | `uint8_t` | reaches threshold | Saturate at threshold and escalate level (L1->L2->restart-required). |
| Diagnostic/stat counters | `uint32_t` | wrap on long uptime | Allowed to wrap; telemetry consumers treat as modulo counters or periodic reset snapshots. |

Mandatory implementation rules:
1. Counters that drive state decisions must be saturating (not wrapping).
2. Wrapping counters are telemetry-only unless explicitly epoch-scoped.
3. On any wrap-sensitive field, logs must include epoch context (`session_boot_nonce`, `session_id`, state).
4. Value `0` for `session_id` is permitted only as part of full token validation; no logic may assume `0` means "not initialized" in packets.
5. Any repo-local numeric update (`++`, `+=`, timeout compare) must explicitly choose one mode: **saturating**, **wrapping**, or **epoch-reset**.
6. Direct arithmetic on state-driving counters is prohibited unless accompanied by documented mode justification.

---

## 8) Module responsibilities after rewrite

## 8.1 Keep
- `EspnowTxScheduler` (as sole send plane)
- message router abstractions
- queue manager

## 8.2 Replace / retire
- retire TX duplicate lifecycle logic split between `TxReconnectManager` and `TxStateMachine`
- retire RX-only connection arbitration as independent authority (`RxRadioArbiterFsm` as link owner)
- remove direct/global channel truth (`g_lock_channel`) from runtime decisions

## 8.3 Refactor
- `EspNowConnectionManager` becomes thin compatibility shim over `UnifiedLinkFsm` (or fully replaced)
- `ChannelManager` evolves into `ChannelAuthority` with explicit ownership APIs
- receiver/transmitter handlers become route adapters only

## 8.4 Common-code consolidation requirements
- Move reconnect plan generation, channel ordering, backoff progression, and timeout constants into `esp32common`.
- Keep only hardware/board integration hooks in project repositories.
- Ban duplicate FSM or timeout definitions outside shared headers.
- Use shared numeric helpers from `esp32common/patterns/numeric_safety.h` for all state-driving numeric transitions.
- Require repo-level code reviews to validate numeric mode selection on every new/modified counter.

---

## 9) Concrete migration plan

## Phase A: Infrastructure (no behavior change)
1. Add `LinkTruth`, `UnifiedLinkFsm`, `ChannelAuthority`, `LinkRecoveryCoordinator` in `esp32common`.
2. Add telemetry counters for invalid transitions and session mismatches.
3. Route all direct control sends through scheduler wrappers.
4. Add project-wide numeric safety primitives (`numeric_safety.h`) and migrate state-driving counters in common + app layers.

## Phase B: Handshake authority
4. Introduce `CONNECT_CONFIRM`/`CONNECT_CONFIRM_ACK` authoritative gating.
5. Block all non-handshake paths from entering CONNECTED/LINK_UP.
6. Enforce session-id checks on both devices.

## Phase C: Recovery unification
7. Move all L1/L2/reboot decisions into recovery coordinator.
8. Remove ad-hoc reinit calls from handlers/routes.
9. Standardize post-recovery checklist: callbacks, peers, power-save state, queue purge policy.

## Phase D: Channel authority unification
10. Replace `g_lock_channel` and scattered channel writes with `ChannelAuthority`.
11. Ensure discovery worker reports candidate only; locking performed by authority module.
12. Add periodic invariant assertion + telemetry (read-only monitor).

## Phase E: Cleanup
13. Remove obsolete FSMs/flags.
14. Keep compatibility event API only as façade during transition.
15. Update docs/tests/diagnostic pages to report `LinkTruth` only.

---

## 10) Verification strategy (must-pass)

## 10.1 Deterministic test matrix
- 30+ scripted scenarios matching Section 6 cases.
- Pass criterion: no split-brain (`session_id` mismatch), bounded recovery, no infinite CONNECTING loop.

## 10.2 Runtime invariants (assert/telemetry)
- At all times in `LINK_UP`:
$$
\text{operating\_channel} = \text{live\_wifi\_channel}
$$
- At all times:
$$
\text{CONNECTED} \Rightarrow \text{session\_id\_validated}
$$
- Recovery escalation only monotonic within window.

## 10.3 Long soak
- 24 h coexistence run with induced HTTP/MQTT bursts and AP channel changes.
- Expected: recover without manual reboot in all non-terminal faults.

## 10.4 Reconnection certainty checks (must pass)

The following checks guarantee that reconnection behavior is fully determined:

1. **Single next-state check:** for every `(state,event)` pair there is exactly one valid next-state.
2. **No silent transition check:** every state change emits one telemetry record with cause and `session_id`.
3. **Epoch safety check:** stale/foreign `session_id` packets cannot produce `LINK_UP`.
4. **Channel authority check:** in `LINK_UP`, TX operating channel equals receiver-confirmed channel snapshot.
5. **Backoff determinism check:** scan-cycle counter increments only on completed miss cycles.
6. **Recovery monotonicity check:** escalation ladder cannot skip backward within an active escalation window.
7. **Post-recovery checklist check:** callbacks/peers/channel policy are re-established before resuming discovery.
8. **Reboot collision check:** after forced TX reboot, old pre-reboot ACK packets cannot satisfy new handshake.
9. **Counter-wrap check:** synthetic wrap of `session_id` (0xFFFF -> 0x0000) cannot produce false `LINK_UP` without matching nonce and window.
10. **Retry saturation check:** TX/RX confirm retry counters hit budget and transition deterministically (no rollover-to-zero loop).
11. **Millis wrap check:** 49-day timer wrap simulation preserves timeout behavior and does not stall reconnect FSM.

Acceptance criterion: all eleven checks green across boot-first permutations, AP channel changes, packet loss, reorder/delay injection, forced reboot, long-uptime wrap simulation, and NO_MEM injection tests.

---

## 11) Implementation guardrails

1. No module outside `ChannelAuthority` may call `esp_wifi_set_channel` in steady-state code.
2. No module outside `LinkRecoveryCoordinator` may call `esp_now_deinit/init` or `esp_wifi_stop/start`.
3. No module outside `UnifiedLinkFsm` may mutate connection phase/session.
4. Handshake packets must be idempotent and epoch-gated.
5. Scheduler metrics must be the only source for NO_MEM escalation decisions.
6. Receiver channel evidence is final; TX channel caches/legacy globals are hints only.
7. Any reconnect flow must attempt `last_good_channel` first before full channel sweep.
8. `LINK_UP` admission must validate full `session_token` (`session_boot_nonce` + `session_id`) and peer MAC.
9. Project-wide numeric mode must be explicit on every counter/timer field: saturating, wrapping, or epoch-reset.
10. State-driving numeric paths must use shared common helpers; ad-hoc arithmetic is non-compliant.

---

## 12) Decision summary

This rewrite is approved as the only path to eliminate current reconnection instability.

- Incremental fixes can reduce symptoms but cannot remove structural split ownership.
- The system will move to a single authoritative FSM, a single channel authority, and a single send/recovery plane.
- MQTT/HTTP work should proceed only after this foundation is in place and verified.

---

## 13) Immediate next engineering step

Create implementation issue set from this document with one PR stream per phase (A-E), and block feature work touching ESP-NOW connection semantics until Phase C is merged.
