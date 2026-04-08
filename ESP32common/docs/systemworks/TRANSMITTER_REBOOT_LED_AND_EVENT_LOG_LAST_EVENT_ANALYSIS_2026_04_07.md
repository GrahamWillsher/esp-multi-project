# Transmitter Reboot LED + Event Log "Last Event" Analysis (2026-04-07)

## Scope
This review was done to explain two observed regressions:

1. On transmitter reboot, receiver LED is not reliably GREEN.
2. Event Logs page shows missing/`N/A` "Last Event" details even when transmitter time is set.

This revision also verifies three additional requests:

3. Receiver should issue explicit LED state request inside the ESP-NOW lifecycle.
4. Receiver must use the same transmitter timestamp fields (no legacy timestamp ambiguity).
5. Event logs should be MQTT snapshot/on-demand only (no legacy HTTP path).

---

## Executive Summary

### Finding A — LED state on transmitter reboot is not guaranteed to refresh
The current design relies on a **best-effort battery config section request** to trigger LED replay from transmitter. That request is sent once during reconnect and is not retried on failure. If it is dropped during reconnect timing windows, receiver can keep stale LED state.

### Finding B — Event timestamp fields are being dropped in receiver cache/API path
Transmitter now publishes `timestamp_ms`, `event_unix_ms`, `event_utc_offset_min`, but receiver cache still reads legacy `timestamp` and does not persist the new fields. This causes Last Event rendering to fall back to `N/A`.

### Finding C — Additional consistency gaps
Receiver cache currently ignores transmitter `is_new` and sets `is_new=true` for every incoming batch entry. This is not the primary failure, but it weakens snapshot semantics.

### Finding D — "On-demand and MQTT-only" is not fully true yet
Current receiver `/api/get_event_logs` still includes HTTP proxy fallback to transmitter when MQTT cache is unavailable. That legacy path violates strict MQTT-only transport intent.

---

## Detailed Findings

## 1) LED should be GREEN after transmitter reboot

### What code currently does
- Transmitter LED color is derived from `get_emulator_status()` in `led_publish_current_state()`.
- A LED replay is sent when transmitter handles battery config section request (`config_section_battery`) and also on explicit `msg_led_state_request`.
- Receiver reconnect flow sends initialization requests once, including battery section request.

### Why this can fail in practice
- In receiver reconnect init, battery section request is sent once (no resend path for config sections).
- If this one-shot request fails due reconnect race (peer not yet ready), no guaranteed LED refresh follows.
- Result: receiver can continue showing stale LED state from before reboot.

### Why this matches observed behavior
- Reboot timing can temporarily invalidate peer-send readiness.
- Existing retry logic covers `REQUEST_DATA` and catalogs, but not config section requests or dedicated LED refresh.

### Suggested fixes (priority order)
1. **Add explicit LED state sync request on every CONNECTED transition** (receiver -> transmitter), with retry/backoff for a short window.
2. **OR** add a guaranteed transmitter-side `led_publish_current_state(true, peer_mac)` on connection-established callback.
3. Keep existing battery-section replay as supplemental path, not primary guarantee.

### ESP-NOW placement (requested design clarification)
Yes — the LED request should be tied to the receiver state machine reaching connected/ready state.

Recommended exact placement:
- Receiver connection callback at CONNECTED transition (in receiver connection handler), immediately after initialization requests are dispatched.
- Send `msg_led_state_request` once immediately.
- If send fails (peer race), retry in short bounded loop (e.g., same retry scheduler used for startup requests) until first success or short timeout window.

Rationale:
- This aligns with the ESP-NOW connection contract (request only when peer identity and connection state are valid).
- It removes dependency on battery config resend behavior.
- It makes LED convergence deterministic after reboot.

### Acceptance criteria
- After any transmitter reboot, receiver LED converges to transmitter current status within <= 2 seconds.
- Works even if initial config-section request is dropped.

---

## 2) Event Logs "Last Event" missing despite transmitter time being set

### Transmitter payload (current)
`publish_event_logs()` now emits:
- `timestamp_ms`
- `event_unix_ms`
- `event_utc_offset_min`
- plus `event`, `message`, `count`, `is_new`, snapshot metadata.

### Receiver cache/API mismatch
Receiver event log cache currently:
- Reads `entry.timestamp = evt["timestamp"]` (legacy key)
- Does not store `event_unix_ms`
- Does not store `event_utc_offset_min`
- API response sends only `timestamp`, `type`, etc.

So even though transmitter sends canonical event-time fields, receiver drops them before UI sees them.

### Why UI shows `N/A`
UI formatter prefers:
1. `event_unix_ms + event_utc_offset_min`
2. fallback raw uptime-domain timestamp (`timestamp`/`timestamp_ms`) with transmitter uptime conversion

But receiver cache/API path strips both canonical fields and effectively sets timestamp to 0 when only `timestamp_ms` exists, so formatter has no valid input and returns `N/A`.

### Suggested fixes (required)
1. Update receiver event log model to include:
   - `timestamp_ms`
   - `event_unix_ms`
   - `event_utc_offset_min`
2. In cache merge logic, map robustly:
   - `timestamp_ms = evt["timestamp_ms"] | evt["timestamp"] | 0`
   - preserve canonical fields when present.
3. In `/api/get_event_logs` response, return these same field names.
4. Remove legacy-only response shaping once all producers/consumers are migrated.

### "Use same timestamp as transmitter" policy (final)
Receiver should treat transmitter fields as canonical and avoid recomputation when canonical fields exist:

1. `event_unix_ms` (UTC epoch at event creation) + `event_utc_offset_min` (captured at event creation)
2. `timestamp_ms` (uptime domain) only as secondary fallback

This means receiver should render from transmitter event-time metadata first, not from local `Date.now()` approximations.

### Acceptance criteria
- Last Event column always renders for events with valid `timestamp_ms`.
- DST-correct wall-clock rendering works when `event_unix_ms` + `event_utc_offset_min` present.
- No regressions for older payload producers.

---

## 3) Secondary correctness gap: `is_new` semantics

### Current behavior
Receiver cache currently forces `entry.is_new = true` for all items in the latest incoming batch and clears previous `is_new` globally.

### Risk
This can misrepresent transmitter-defined "new since snapshot start" semantics, especially across multi-batch snapshots.

### Suggested fix
- Trust transmitter `is_new` when present.
- Use local fallback only for legacy payloads that do not include `is_new`.

---

## Root Cause Classification

1. **Contract drift** between transmitter payload schema and receiver cache schema.
2. **One-shot reconnect initialization** for LED sync path without robust retry.
3. **Backward-compatibility assumptions** not updated after MQTT snapshot/timestamp redesign.
4. **Transport-policy drift**: API fallback path still allows HTTP fetch, conflicting with MQTT-only requirement.

---

## 4) Verification: are event logs "on demand" and MQTT-only today?

### What is true today
- Transmitter MQTT publish path is subscriber-gated (`event_log_subscribers_ > 0`), so snapshot publishing is effectively on-demand.
- Receiver page subscribe/unsubscribe uses ESP-NOW control message to start/stop transmitter event log publishing.

### What is not true yet
- Receiver `/api/get_event_logs` still has HTTP fallback proxy to transmitter when cache is unavailable.
- Receiver cache still accepts legacy fields and reshapes payload in legacy form.

Conclusion:
- **On-demand publishing:** mostly correct.
- **MQTT-only transport contract:** **not yet fully enforced** until HTTP fallback is removed.

---

## 5) Legacy removal checklist (requested)

To satisfy "remove all legacy code" for event logs, apply this cleanup plan:

1. Remove HTTP fallback branch from receiver `/api/get_event_logs`; return MQTT-cache-only status/error.
2. Remove legacy timestamp aliases in event log cache (`timestamp` as primary key) and move to canonical `timestamp_ms`.
3. Extend receiver event log entry model with canonical fields:
   - `timestamp_ms`
   - `event_unix_ms`
   - `event_utc_offset_min`
4. Remove UI fallback parsing paths that infer time from free-form strings for this page.
5. Keep a short migration window only if mixed firmware fleets are expected; otherwise remove all legacy branches in one cut.
6. Preserve strict pass-through of transmitter `is_new` and `count`.

---

## Recommended Implementation Plan

### Phase 1 (hotfix)
- Receiver cache/API schema alignment for event timestamps.
- LED explicit resync request on connect with short retry window.
- Remove `/api/get_event_logs` HTTP fallback to enforce MQTT-only behavior.

### Phase 2 (hardening)
- Preserve transmitter `is_new` semantics.
- Add telemetry counters/logs for:
  - LED sync request attempts/success
  - event timestamp field coverage (`timestamp_ms` present, `event_unix_ms` present)

### Phase 3 (tests)
- Reboot transmitter while receiver stays up; verify LED settles GREEN when healthy.
- Events page: verify Last Event non-`N/A` for mixed old/new events and after time sync.
- Multi-batch snapshot: verify stable ordering and new markers.

---

## Final Recommendation
Proceed in this order:

1. **Canonical timestamp pass-through** in receiver cache/API (direct blocker for Last Event display).
2. **Explicit ESP-NOW LED state request at CONNECTED** with bounded retry.
3. **Remove receiver HTTP event-log fallback** to enforce MQTT-only transport.
4. **Delete remaining legacy parsing/alias logic** after migration validation.
