# Event Logs Full Lifecycle Analysis (2026-04-07)

## Scope
This report analyzes the full event-log lifecycle end-to-end:

1. Event creation on the transmitter
2. Transport to receiver (`ESP-NOW` summary + `MQTT` snapshot rows)
3. Receiver cache and API behavior
4. Web UI behavior on `/` and `/events`
5. Clear flow initiated from `/events`
6. Edge cases (page open during new events, TTL, races, disconnect/reconnect, retained payload behavior)

It also identifies logic gaps and gives concrete implementation actions.

---

## Executive Summary

The architecture is close to the intended model (ESP-NOW summary + MQTT snapshot), but there are **critical lifecycle gaps**:

- Snapshot completion is published by transmitter (`batch_index` / `batch_count`) but not consumed by receiver/UI.
- `/events` unsubscribes immediately after first fetch, not after confirmed snapshot completion.
- Receiver cache lifecycle needs explicit closure/opening contract (`/events` close clears, next `/events` open starts fresh).
- Subscription TTL is implemented as a single global timer/counter, not per-session lease tracking.
- Clear button behavior should be explicitly documented as reboot-style staged confirmation/cancel.

These gaps can explain intermittent stale/empty `/events`, race conditions, and inconsistent clear expectations.

---

## Current End-to-End Lifecycle (As Implemented)

### Operating constraints (authoritative)

- `/events` is configured for a single client/viewer model.
- Receiver cache reset occurs when `/events` is closed.
- Selecting `/events` again starts a fresh snapshot lifecycle.
- Transmitter reboot clears transmitter event logs; event accumulation restarts from zero.

## 1) Event creation on transmitter

Primary event model is in `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.cpp`:

- `set_event(...)` updates event slot fields:
  - `timestamp = millis64()`
  - `event_unix_ms` from `gettimeofday()`
  - `event_utc_offset_min` from local/UTC conversion helper
  - `state`, `data`, and potentially `occurences`
- `MQTTpublished` is set `false` when event transitions from inactive to active.
- `clear_event(...)` changes active state only (historical `occurences` stays).
- `reset_all_events()` zeroes state and historical counters.

### Important behavior detail
If `set_event(...)` is called while event is already active, timestamp still updates but `occurences` does not increment. This means "last event" time may move forward without increasing count.

---

## 2) Transmitter summary path (`ESP-NOW`)

Implemented in `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_routes.cpp`:

- Receiver requests summary via `msg_event_log_summary_request`.
- Transmitter responds with `event_log_summary_t`:
  - `total_historical`
  - `error_historical`
  - `new_since_last_report_total`
  - `new_since_last_report_error`
- Summary also sent on subscribe and clear actions.

### Important behavior detail
`new_since_last_report_*` is delta since last summary build on transmitter (tracked by in-memory snapshot array), not "since user opened page".

---

## 3) Transmitter row path (`MQTT`)

Implemented in `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp`:

- Publishes to retained topic `transmitter/BE/event_logs` only when subscriber count > 0.
- Builds snapshot batches with metadata:
  - `snapshot_id`
  - `batch_index`
  - `batch_count`
  - `snapshot_total`
  - `snapshot_complete`
- Rows include canonical fields:
  - `timestamp_ms` (`uint64`)
  - `event_unix_ms` (`uint64`)
  - `event_utc_offset_min` (`int16`)
  - `level`, `count`, `is_new`, `event`, `message`, `data`
- Marks events as `MQTTpublished=true` only when full snapshot completes.

### Existing TTL fallback
`SUBSCRIPTION_TTL_MS` (60s) cleanup exists, but uses a single global subscriber counter/activity timestamp.

---

## 4) Receiver subscription control (`ESP-NOW`)

Implemented across:

- `espnowreceiver_2/src/espnow/espnow_send.cpp`
- `espnowreceiver_2/src/mqtt/mqtt_client.cpp`
- `espnowreceiver_2/lib/webserver/api/api_modular_handlers.cpp`

Flow:

- `/api/event_logs/subscribe` -> increments receiver-local subscriber count.
- On transition 0 -> 1, receiver sends ESP-NOW subscribe control.
- `/api/event_logs/unsubscribe` decrements; on 1 -> 0 sends ESP-NOW unsubscribe.

---

## 5) Receiver MQTT row ingestion/cache

Implemented in:

- `espnowreceiver_2/src/mqtt/mqtt_client.cpp` (`handleEventLogs`)
- `espnowreceiver_2/lib/webserver/utils/transmitter_event_log_cache.cpp`

Behavior:

- Receiver subscribes to topic `transmitter/BE/event_logs` (topic-level subscription remains active after connect).
- Incoming JSON payload is merged into local cache.
- Cache is keyed by event `type` and sorted by `timestamp_ms` desc.
- API `/api/get_event_logs` serves rows from this cache (`source: mqtt`).

### Important behavior detail
Cache merge currently ignores snapshot metadata (`snapshot_id`, `batch_index`, `batch_count`, `snapshot_complete`), so receiver cannot know if a full snapshot has completed.

---

## 6) Receiver dashboard `/` summary display

Implemented in:

- `espnowreceiver_2/lib/webserver/pages/dashboard_page_script.cpp`
- `espnowreceiver_2/lib/webserver/api/api_telemetry_handlers.cpp`

Behavior:

- Dashboard polls `/api/get_event_log_summary` periodically.
- API sends non-blocking ESP-NOW summary request then returns cached summary.
- Script retries once shortly later when summary not ready.

This restores intended summary-first behavior for `/`.

---

## 7) `/events` page behavior

Implemented in `espnowreceiver_2/lib/webserver/pages/event_logs_page_script.cpp`:

- On load:
  1. POST `/api/event_logs/subscribe`
  2. GET `/api/get_event_logs?limit=500`
  3. Immediately POST `/api/event_logs/unsubscribe` in `finally`
- On unload: defensive unsubscribe beacon.
- Timestamp render prefers `event_unix_ms + event_utc_offset_min` (correct policy).

### Important behavior detail
This is snapshot-intent behavior, but it unsubscribes after first fetch rather than after receiver confirms final batch arrival.

---

## 8) Clear flow from `/events`

Implemented in:

- UI: `event_logs_page_script.cpp` (`clearEventLogs()`)
- API: `api_clear_event_logs_handler` in `api_telemetry_handlers.cpp`
- ESP-NOW send: `send_event_logs_clear_request()`
- TX handling: `message_routes.cpp` clear action -> `reset_all_events()`

Current flow:

1. User triggers clear countdown and confirms.
2. Receiver sends ESP-NOW clear request.
3. Receiver immediately clears local cache.
4. Transmitter resets events and sends summary.

No strict ack/confirmation handshake back to UI is enforced before success message.

### UI operation requirement (aligned with `/transmitter/reboot` pattern)

For consistency with reboot operation, clear should remain a staged flow:

1. First click starts a visible countdown (armed state).
2. Second click during countdown cancels.
3. If countdown reaches zero, clear is executed.
4. UI shows explicit completion status and returns to dashboard.

This staged pattern is already implemented on `/events` and should be treated as the required UX baseline.

### Lifecycle policy update requested

Per current direction, event logs on receiver are temporary snapshot data:

- Receiver cache is the authoritative `/events` display source.
- Cache is cleared on `/events` page close and rebuilt when `/events` is selected again.
- Clear operation should clear receiver cache/lifecycle state **and** send ESP-NOW clear to transmitter to clear transmitter-side event storage.
- Republishing explicit empty event payloads from transmitter is not required for this policy.

### Clear command transport requirement (updated)

`/events` clear must remain dual-action:

1. Receiver clears local `/events` cache/state (immediate UI consistency).
2. Receiver sends `EVENT_LOGS_ACTION_CLEAR` over ESP-NOW to transmitter (`msg_event_logs_control`).
3. Transmitter clears event storage (`reset_all_events()`).

This ensures both temporary receiver snapshot state and transmitter source-of-truth event state are cleared.

### Required transmitter behavior after clear command

When transmitter receives clear (`EVENT_LOGS_ACTION_CLEAR`):

1. It clears transmitter event logs.
2. It updates summary counters.
3. It immediately emits ESP-NOW event-log summary so receiver `/` count updates automatically.

---

## Edge Case Analysis

## A) `/events` page open and new event occurs on transmitter

Current behavior (by design intent + current implementation):

- Page usually unsubscribes quickly after initial fetch.
- New event after unsubscribe will not appear until explicit refresh/reload.
- If event occurs during brief subscribe window, appearance depends on timing of next MQTT publish vs fetch timing.

**Result:** user can observe stale page while open. This matches snapshot-page intent, but current race window can also cause accidental empty/stale first load.

---

## B) First load race (subscribe/fetch/unsubscribe)

Because fetch is immediate and snapshot completion is not tracked:

- `/api/get_event_logs` may be read before new snapshot batch arrives.
- Page may show old or empty cache.
- Unsubscribe may happen before complete snapshot is delivered.

**Severity:** High (core UX inconsistency).

---

## C) Receiver-temporary cache policy and clear behavior

Target policy for `/events` is temporary receiver snapshot data:

- Cache is cleared when `/events` closes and rebuilt when `/events` is selected.
- Clear should clear receiver-side cache/state used by `/events`.
- No requirement to publish explicit empty snapshots from transmitter for clear.

Risk to manage under this policy:

- If receiver cache is not reset at snapshot start, stale rows can still appear during early fetch timing windows.

**Severity:** High (if cache reset timing is not deterministic).

---

## D) TTL/session handling

Current TTL logic on transmitter:

- Single global `event_log_subscribers_`
- Single `last_event_log_subscriber_activity_ms_`
- TTL expiry force-clears all subscribers/snapshot state

Limitations:

- Not per-session or per-client
- No lease ID/keepalive model
- One stale session can affect all sessions
- Long-running active session would still expire after inactivity window

**Severity:** Medium now (snapshot model), High if expanded to richer session semantics.

---

## E) Clear acknowledgement and consistency

Under receiver-temporary policy, clear success should be tied to receiver cache clear completion.

- ESP-NOW transmitter clear send attempt is required on `/events` clear.
- API response should clearly state:
  - receiver clear status (authoritative for `/events` UI)
  - transmitter clear request status (best-effort)

### Acknowledgement requirement check

An explicit clear ACK is recommended for deterministic operations:

- Recommended: add ESP-NOW `clear_ack` response with `success/failure` and post-clear summary sequence.
- Minimum acceptable (current): transmitter sends immediate updated `event_log_summary` after clear; receiver treats `total_historical == 0` as practical confirmation.

Given current architecture, summary-based confirmation is workable, but explicit ACK is cleaner and less ambiguous under packet loss/retry scenarios.

**Severity:** Medium.

---

## F) Summary semantics drift risk

`new_since_last_report_*` counters are sampled every dashboard poll (currently frequent), so they represent short-window deltas, not durable "new since user last visited" semantics.

**Severity:** Low-to-medium (depends on UX expectation).

---

## G) Event store persistence model

Event store is RAM-backed (`events.entries[...]`), reset on reboot or clear. Historical is effectively "since boot/clear", not permanent audit history.

**Severity:** Informational (but must be explicit in UX/docs).

---

## Gap List and Required Rectifications

## Gap 1 (High): Receiver does not consume batch completion metadata

### What to implement

- In receiver MQTT event log path, track snapshot session state:
  - `snapshot_id`
  - expected `batch_count`
  - received batch bitmap/set
  - completion flag when final batch processed
- Expose completion state to `/events` page via API (`/api/event_logs/snapshot_status` or include in `/api/get_event_logs`).

### Why
Enables deterministic unsubscribe only after full snapshot arrival.

---

## Gap 2 (High): `/events` unsubscribes before completion

### What to implement

- Change `/events` lifecycle to:
  1. subscribe
  2. wait until snapshot complete (or timeout)
  3. fetch cache
  4. unsubscribe
- Add bounded timeout and explicit status text when timeout occurs.

### Why
Eliminates subscribe/fetch race and partial snapshot issues.

---

## Gap 3 (High): Receiver clear lifecycle is not yet explicit per `/events` request

### What to implement

- On `/events` page close, clear receiver event-log cache.
- On next `/events` page open, begin fresh snapshot lifecycle.
- Keep clear button semantics receiver-first:
  - clear receiver cache/state immediately
  - send transmitter clear as required ESP-NOW side action
- Document API response fields to distinguish receiver clear vs transmitter clear request outcome.

### Why
Matches temporary receiver-cache policy and avoids stale rows from previous page sessions.

---

## Gap 4 (Medium): Subscription lease model is global count, not per-session

### What to implement

- Replace single count/timestamp with session map:
  - `session_id`
  - create time
  - last activity
  - completion flag
- Support lease refresh or deterministic short-lived session close.

### Why
Improves correctness and observability, enables reliable TTL cleanup.

---

## Gap 5 (Medium): Clear result contract is ambiguous

### What to implement

- Return structured clear result from receiver API:
  - `receiver_cache_cleared` (authoritative)
  - `transmitter_clear_requested` (required send attempt)
  - `transmitter_clear_confirmed` (summary/ack confirmed)
- Keep reboot-style countdown/cancel UX as mandatory pattern for clear trigger.

### Why
Makes clear semantics explicit and aligned with intended UI behavior.

---

## Gap 6 (Medium): Config centralization for event-log timing constants

### What to implement

- Move/align event-log timing constants into shared common config header (as previously recommended), then consume from both TX and RX.
- Avoid independent hardcoded timeouts and diverging behavior.

### Why
Keeps lifecycle policy coherent across codebases.

---

## Recommended Implementation Plan (Priority Order)

## P0 (must do first)

1. Receiver snapshot completion tracking (`batch_index`/`batch_count`/`snapshot_id`).
2. `/events` wait-for-completion unsubscribe flow.
3. Receiver cache reset at snapshot start + receiver-authoritative clear contract.

## P1

4. Clear ack handshake to UI.
5. Per-session lease tracking + TTL cleanup metrics.

## P2

6. Shared event-log timing config unification.
<!-- Deferred for now per project direction: lifecycle diagnostics endpoints/log counters. -->

---

## Suggested Observability Additions

Add counters/logs on both sides:

- TX: subscriptions created, unsubscribed, TTL reaped, clear commands received
- TX: snapshot_id, batch_count, batch_index logs
- RX: snapshot started/completed/timed-out, cache-cleared-at-start, batches received/missed, unsubscribe reason
- RX API: latest snapshot state fields returned for `/events` diagnostics panel

---

## Validation Checklist After Fixes

1. Open `/events` with populated transmitter events -> full table loads reliably first attempt.
2. Simulate high latency -> still unsubscribes only after completion or explicit timeout.
3. Clear from `/events` -> reboot-style flow (countdown/cancel/execute), receiver cache empties authoritatively.
4. Re-enter `/events` after clear -> fresh snapshot lifecycle rebuilds rows from current (cleared) transmitter state.
5. Keep `/events` open; trigger new transmitter event -> page remains snapshot-stale by design until explicit refresh.
6. Dashboard `/` count updates via summary polling and reflects new events quickly.

7. TTL behavior confirmation: each new `/api/event_logs/subscribe` starts a fresh subscription window and refreshes transmitter TTL baseline.

8. After clear, dashboard `/` event card converges to `0 events` when next ESP-NOW summary is received (`total_historical=0`, `error_historical=0`).

---

## Additional Shortfalls / Missing Items Identified in QA Review

## H) Multi-client semantics

Not applicable for current design scope.

- System is explicitly single-client for `/events` lifecycle.
- Multi-tab/multi-client behavior is out of scope.

---

## I) Snapshot integrity checks (duplicates, missing, out-of-order)

Document currently requires completion detection but does not define integrity handling.

Missing detail:

- Duplicate `batch_index` handling
- Out-of-order batch handling
- Missing final batch timeout behavior and retry strategy

Required addition:

- Define receiver-side acceptance rules per `snapshot_id` + `batch_index`.
- Define discard/restart behavior on invalid sequence.

---

## J) Snapshot start-state reset contract

Document states cache should be refreshed per `/events` request, but does not specify exact reset boundary.

Missing detail:

- Whether reset occurs on subscribe API entry, first batch receipt, or completion timeout.

Required addition:

- Specify deterministic rule: clear receiver snapshot accumulator/cache at snapshot session start before ingesting new batches.

---

## K) Transmitter reboot behavior and summary baseline

`new_since_last_report_*` depends on in-memory reported-occurrence snapshot on transmitter.

Missing detail:

- After transmitter reboot, baseline resets and counters can appear discontinuous.

Required addition:

- Document reboot semantics for summary deltas and define receiver UI hinting for post-reboot counter discontinuity.

---

## L) Clear-confirm timing contract for dashboard `/`

Dashboard `/` is summary-driven and eventually consistent.

Missing detail:

- No explicit maximum convergence time target after clear.

Required addition:

- Define expected convergence SLA (for example within next summary poll + transport latency window) and timeout warning behavior.

---

## M) API error contract completeness

Document proposes structured clear response but does not define standard error fields across related endpoints.

Missing detail:

- Uniform status/error codes for subscribe, unsubscribe, clear, and snapshot-timeout conditions.

Required addition:

- Define shared API response schema (success, code, message, retryable, telemetry fields).

---

## N) Security / safety guardrails for clear

Not required for current scope (single-client controlled workflow).

---

## O) Observability gaps for clear outcome

Current observability list is broad but not clear-specific enough.

Missing detail:

- No dedicated counters for clear send/confirm/fail/timeout.

Required addition:

- Add metrics: `clear_requests_total`, `clear_tx_send_fail_total`, `clear_confirm_timeout_total`, `clear_confirm_success_total`.

---

## TTL Confirmation (Requested)

Current transmitter TTL behavior is subscription-triggered:

- On each subscribe control (`EVENT_LOGS_ACTION_SUBSCRIBE`), transmitter executes `increment_event_log_subscribers()`.
- This resets `last_event_log_subscriber_activity_ms_`, which is the TTL baseline used by `SUBSCRIPTION_TTL_MS` checks.
- Therefore TTL is effectively refreshed per new `/events` subscription trigger.

Important limitation (still valid):

- TTL state is global-count/global-timestamp, not per-session lease.
- This is acceptable for the current single-client/single-snapshot pattern.

---

## Impact of Clear on Dashboard `/` Event Count (ESP-NOW)

Dashboard `/` card uses receiver API `/api/get_event_log_summary`, which is backed by ESP-NOW `event_log_summary_t`.

When `/events` clear is pressed:

1. Receiver sends ESP-NOW clear to transmitter.
2. Transmitter clears events and emits an immediate fresh summary.
3. Receiver stores updated summary (`TransmitterManager::storeEventLogSummary(...)`).
4. Dashboard polling reflects updated counters:
  - `total_historical` -> expected `0`
  - `error_historical` -> expected `0`
  - `new_since_last_report_*` -> may be `0` or transient depending on summary sequence timing

Operational note:

- Dashboard count on `/` is summary-driven, not row-cache-driven.
- Therefore, clear correctness on `/` depends on receiving post-clear ESP-NOW summary (or ACK+refresh trigger).

---

## Final Assessment

The current system has the right transport split and mostly correct timestamp contract, but lifecycle closure is incomplete. The highest-risk defect is snapshot completion race on `/events`, followed by lack of explicit receiver-temporary cache lifecycle controls.

Addressing the P0 items will make event logs deterministic, clear-safe, and aligned with the intended snapshot lifecycle policy.