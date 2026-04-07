# LED State Sync (Transmitter ↔ Receiver) Lifecycle Analysis — 2026-04-07

## Scope

Requested review:
1. Is LED state sent initially by transmitter at startup?
2. Is LED state requested by receiver on receiver reboot when devices are connected?
3. What happens when LED state is "sent" while link is not connected?
4. Identify gaps, edge cases, and recommended fixes.

---

## Executive Summary

Current behavior is **partially correct** but not fully robust.

- ✅ Receiver has an explicit LED-state request path at connection initialization.
- ✅ Transmitter responds to explicit LED requests.
- ✅ Receiver can also receive LED replay indirectly when it requests battery config section.
- ⚠️ There is **no guaranteed transmitter-side initial LED push on connection-established**.
- ⚠️ LED publish attempts made before connection are dropped (expected), but there is no deferred replay queue.
- ⚠️ Receiver LED request is mostly one-shot during init; no dedicated retry engine for LED sync.
- ⚠️ A single lost request/response can leave stale LED until another state change occurs.

Net: the design works in happy-path, but has reliability gaps for reboot/race/loss windows.

For the requested UX policy, a simple and practical target is:
- Receiver boots with a **temporary initializing LED state** (not "healthy").
- As soon as ESP-NOW connection is confirmed, receiver sends `msg_led_state_request` as an explicit init step.
- Receiver keeps the initializing state only until either:
  - a transmitter `msg_flash_led` arrives (authoritative state), or
  - a short sync deadline expires (then show a neutral "awaiting TX" indication if preferred).

---

## Requested Startup Policy (Initializing state on RX boot)

## Proposed behavior

1. **Receiver startup default**
   - Initialize receiver LED state to an **initializing** color/effect at boot.
   - Mark local state as `provisional_led_state=true`.

2. **On connection established**
   - Receiver sends `send_led_state_request()` immediately as part of connection init checklist.
   - Keep provisional initializing state visible while waiting for authoritative transmitter LED message.

3. **On first transmitter LED message (`msg_flash_led`)**
   - Apply received color/effect.
   - Clear provisional flag (`provisional_led_state=false`).

4. **If no LED reply within a short timeout (e.g. 2–3s)**
   - Keep last state or show a neutral fallback (team choice).
   - Trigger one simple re-request policy (see alternatives below).

This satisfies your requirement with minimal behavior change and no aggressive backoff spam.

## Initializing color/effect decision (unrestricted palette)

If you are free to add new colors (not limited to current wire enums), **orange is no longer the best default**.  
Orange currently carries a warning-like meaning in many systems and can be confused with degraded runtime state.

Selected choice:

1. **CYAN/TEAL + BREATHING/HEARTBEAT** (**chosen**)
   - Meaning: neutral "initializing/syncing" state, clearly different from error/ok/warn/update.
   - High visual separation from current runtime palette (red/green/orange/blue).

Status decision:
- **Adopt CYAN/TEAL as the initialization color**.
- Use HEARTBEAT (or breathing-equivalent render) for the initialization effect.

2. **WHITE + BREATHING**
   - Very clear "system in transition" semantics in many UI systems.
   - Best fallback if cyan cannot be rendered cleanly.

3. **PURPLE/MAGENTA + HEARTBEAT**
   - Distinct from existing semantics.
   - Slightly less universal than cyan/white for "initializing" meaning.

If you must stay with current enum-only colors, then **ORANGE + HEARTBEAT** remains the best constrained option.

### Why cyan/teal is preferred over orange

- **Semantic neutrality**: does not imply "warning" (orange) or "healthy" (green).
- **Low conflict with OTA blue**: cyan is visually distinct from pure blue.
- **Operational clarity**: users can quickly learn "cyan pulse = syncing/initializing".

Suggested semantic mapping after extension:
- RED = fault/error
- GREEN = normal/healthy
- ORANGE/AMBER = warning/degraded
- BLUE = OTA/update
- **CYAN/TEAL = initializing/sync pending**

---

## Code Path Findings (What exists now)

## 1) Transmitter LED publish primitive

`led_publish_current_state()` is the source of truth for transmitter → receiver LED packets.

Key behaviors:
- Checks ESP-NOW connection first; if disconnected returns `ESP_ERR_INVALID_STATE`.
- Uses optional targeted MAC (`receiver_mac`) or active peer MAC from connection manager.
- Sends `msg_flash_led` via `TxSendGuard::send_to_receiver_guarded(...)`.
- Deduplicates non-forced publishes with `(last_color,last_effect)` cache.

Implication:
- If called while not connected, message is dropped (no queue/defer).

Primary reference:
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/led_handler.cpp`

---

## 2) Transmitter startup and early events

During transmitter boot:
- `init_events()` is called.
- Reset reason event is immediately set (`set_event(EVENT_RESET_..., ...)`).
- `set_event(...)` path calls `led_publish_current_state(false, nullptr)`.

Because this happens early in bootstrap, it can occur before ESP-NOW reaches connected state. In that case LED publish returns invalid-state and no replay is queued.

Primary references:
- `ESPnowtransmitter2/espnowtransmitter2/src/main.cpp` (boot phase: init events + reset reason)
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.cpp` (`set_event/clear_event/reset_all_events` calling LED publish)

---

## 3) Receiver explicit LED request path (good)

Receiver sends explicit LED-state request on initialization after connection established:
- `ReceiverConnectionHandler::send_initialization_requests(...)` calls `send_led_state_request()`.
- This is intended to converge quickly after TX reboot.

Primary reference:
- `espnowreceiver_2/src/espnow/rx_connection_handler.cpp`

LED request transmit guard:
- `send_led_state_request()` checks receiver message-state `VALID` and known transmitter MAC.
- Sends `msg_led_state_request` to transmitter MAC.

Primary reference:
- `espnowreceiver_2/src/espnow/espnow_send.cpp`

---

## 4) Transmitter handling of LED request (good)

Transmitter route exists and is simple:
- On `msg_led_state_request`, call `led_publish_current_state(true, msg->mac)`.
- `force=true` ensures dedup cache does not suppress replay.

Primary reference:
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_routes.cpp`

---

## 5) Secondary LED replay via battery config section

Receiver init requests config sections including battery section.
Transmitter `VersionBeaconManager::send_config_section(config_section_battery, ...)` sends battery section and then explicitly replays LED state (`led_publish_current_state(true, receiver_mac)`).

This is a second convergence path (in addition to explicit LED request).

Primary reference:
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/version_beacon_manager.cpp`

---

## 6) Receiver OTA override restore path

After receiver self-OTA LED override ends, receiver calls `send_led_state_request()` once to resync from transmitter.

Primary reference:
- `espnowreceiver_2/lib/webserver/api/api_control_handlers.cpp`

---

## Gap Analysis

## Gap A — No guaranteed transmitter-side initial push on connect

Current transmitter connection callbacks log connect events, but do not force-send LED state on `CONNECTED` transition.

Risk:
- If receiver's request path is missed/lost or delayed, LED can remain stale until another state transition/event triggers publish.

---

## Gap B — Boot-time LED publish attempts can occur before link exists

Reset reason and early events can call LED publish before connection is established.
These attempts are dropped by design (`ESP_ERR_INVALID_STATE`) and are not queued.

Risk:
- First meaningful LED state may never be shown until later triggers.

---

## Gap C — Receiver LED request reliability is single-shot in init flow

`send_led_state_request()` is called during init, but there is no dedicated retry schedule for LED sync if request send fails or response is lost. Retry engine currently focuses on `REQUEST_DATA` and type catalogs.

Risk:
- One packet loss during connect window can leave stale LED indefinitely in steady state.

---

## Gap D — No response acknowledgement/correlation for LED sync

Flow is request → async flash_led message, but no explicit ACK or sequence correlation for "LED sync complete".

Risk:
- Receiver cannot prove convergence, only infer it from eventual state update.

Your idea ("send LED as another post-connect init item + acknowledgment") directly addresses this gap and is sound.

---

## Gap E — State-gating mismatch risk

Receiver request function gates on `RxStateMachine::message_state()==VALID` plus MAC known, not directly on connection manager connected-state + recent liveness.

Risk:
- During edge transitions/stale windows, request can be skipped or attempted at suboptimal times.

---

## Edge Cases Reviewed

1. **TX boots first, RX absent**
   - TX early LED publishes likely dropped.
   - LED sync only happens later if RX requests or TX state changes again.

2. **RX reboots while TX stable**
   - Usually recovers: RX reconnect init requests LED and battery section; TX responds.
   - Failure mode remains if init request(s) are lost and no retry.

3. **LED state changes while disconnected**
   - TX computes state but cannot deliver.
   - On reconnect, correctness depends on successful replay request/response.

4. **Transient packet loss right after connect**
   - No ACK/retry dedicated to LED sync, stale display can persist.

5. **Receiver self-OTA end**
   - One explicit LED request is sent; if lost, stale state can persist until next trigger.

---

## Recommendations

## Priority 0 (must-have): simple deterministic sync (low noise)

1. **Implement receiver provisional initializing-on-boot policy**
   - Set receiver LED default to an initializing state: **CYAN/TEAL + HEARTBEAT**.
   - Track `provisional_led_state` until first authoritative TX LED packet.

2. **Keep existing receiver connect-time request as primary pull**
   - On receiver connection-established path, call `send_led_state_request()` (already present) as a formal init checklist item.

3. **Add transmitter forced LED replay on connection-established (recommended companion)**
   - On TX connection transition to `CONNECTED`, call:
     - `led_publish_current_state(true, peer_mac)`
   - This creates deterministic transmitter-initiated initial push and complements receiver request.

4. **Add a lightweight acknowledgment rule for LED init sync**
   - Treat the first received `msg_flash_led` after request as LED-sync completion (`led_sync_ack_received=true`).
   - This is an implicit ACK with no new wire message required.

5. **Use one of the low-noise assurance options below (instead of heavy backoff logs)**

Expected outcome:
- Either side can drive convergence; single packet loss no longer leaves stale UI.

---

## Simple Communication Assurance Options (without noisy backoff)

## Option A — One-shot + single delayed retry (simplest)
- Send request on connect.
- If no `msg_flash_led` received after `T=1500ms`, send exactly one more request.
- Stop after second attempt.
- Logging: only one warning when both attempts fail.

Pros: tiny change, low log noise, high success improvement.
Cons: still possible (rare) miss under repeated packet loss.

## Option B — Bounded retry window with silent retries (recommended)
- Retry every 500ms for max 3 attempts.
- No per-attempt warning logs; only debug trace/counter increments.
- Emit one summary warning only if window expires unsatisfied.

Pros: robust and still quiet.
Cons: slightly more state logic than Option A.

## Option C — Dual-trigger without retry loop
- Keep receiver request-on-connect.
- Add transmitter forced LED push on TX `CONNECTED`.
- No retry loop.

Pros: very simple, no timers.
Cons: if both packets are lost in same reconnection window, stale state may persist.

## Option D — Piggyback LED into existing periodic beacon
- Add LED color/effect fields to periodic message already sent frequently (e.g. version beacon).
- Receiver converges naturally without explicit retry loops.

Pros: eventually consistent with minimal control traffic.
Cons: protocol structure change + slightly slower convergence depending on beacon cadence.

## Option E — Init checklist + implicit ACK (best fit for your suggestion)
- Add `LED_SYNC` as an explicit post-connect init step on receiver.
- Receiver sends `msg_led_state_request`.
- Receiver waits for first `msg_flash_led` as ACK of sync completion.
- If timeout, do 1–2 silent retries, then one summary warning.

Pros: simple mental model, aligns with your idea, no new protocol type required.
Cons: still timeout-based; not cryptographic/strict delivery guarantee.

## Optional explicit-ACK variant
- Add new message type `msg_led_state_ack` with seq.
- Receiver sends request with seq; transmitter echoes seq in LED response and/or ACK.

Pros: strict correlation.
Cons: extra protocol complexity and versioning cost for limited practical gain.

---

## Recommended Practical Choice

For your preference (simple, minimal backoff chatter), use:

- **Boot UX**: provisional initializing state on receiver startup (**CYAN/TEAL + HEARTBEAT**).
- **Sync trigger**: request LED on connect as explicit init checklist step.
- **Acknowledgment model**: **Option E** implicit ACK (`msg_flash_led` receipt completes LED init step).
- **Reliability**: 2–3 silent retries max, one final warning only.
- **Optional hardening**: transmitter forced push on TX connected.

This gives predictable behavior without log noise or complex backoff frameworks.

### Migration note for CYAN/TEAL rollout

To adopt CYAN/TEAL, extend shared wire enums and rendering path in both TX/RX with backward compatibility:
- add new `LedColorWire` values in shared protocol
- keep existing color codes unchanged
- default unknown/new color on older receiver firmware to current initializing fallback (e.g. ORANGE)

---

## Priority 1 (strongly recommended): add explicit LED sync completion semantics

3. **Add lightweight LED sync ACK/correlation**
   - Option A: Extend `flash_led_t` with seq (preferred if wire-compat handled).
   - Option B: Keep message unchanged, but set local "LED update received after request" latch tied to request timestamp window.

4. **Unify request gating to connection+liveness criteria**
   - Require both connected-state and known MAC.
   - Optionally avoid strict dependence on `message_state()==VALID` for control requests.

---

## Priority 2 (nice-to-have): improve observability

5. **Add structured counters/metrics**
   - TX: `led_publish_attempted`, `led_publish_sent`, `led_publish_dropped_disconnected`.
   - RX: `led_sync_requested`, `led_sync_retried`, `led_sync_satisfied`, `led_sync_timeout`.

6. **Add throttled warning logs for unsatisfied sync windows**
   - Helps quickly diagnose field cases where UI appears stale.

---

## Suggested Acceptance Criteria (post-fix)

1. TX reboot with RX online:
   - Receiver LED becomes correct within bounded SLA (e.g. ≤ 3 s).
2. RX reboot with TX online:
   - Receiver starts in initializing state immediately, then converges to TX LED state even with one dropped request packet.
3. Disconnect/reconnect with no intervening status changes:
   - Receiver still converges (no dependence on future event mutation).
4. Receiver OTA override exit:
   - LED returns to true transmitter state within bounded retry window.
5. Logging behavior:
   - No per-attempt warning spam for LED sync retries; only one summary warning on failed sync window.
6. Init checklist behavior:
   - `LED_SYNC` init step is marked complete only when `msg_flash_led` is received after request.

---

## Bottom Line

Your intended behavior is valid:
- **Transmitter should push initial state**, and
- **Receiver should request state after its reboot once connected**.

Current code already supports parts of this, but it is not fully deterministic under race/loss/disconnect conditions. Implementing the P0 recommendations will close the key reliability gaps and remove stale-LED edge cases.