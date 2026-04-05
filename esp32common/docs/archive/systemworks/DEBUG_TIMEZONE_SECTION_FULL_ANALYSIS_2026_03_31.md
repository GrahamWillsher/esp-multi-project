# /debug Timezone Section Full Analysis — 2026-03-31

## Executive Summary

The `/` dashboard and the `/debug` timezone section are **not using the same source of truth**.

- `/` is driven by the transmitter heartbeat (`/api/transmitter_health`), which is arriving and is clearly correct.
- `/debug` is driven by a separate ESP-NOW snapshot message (`/api/transmitter_time_transitions`), and the UI is coded to fail closed if that snapshot is missing.

This explains the exact symptom reported:

- `/` shows:
  - valid time
  - `Source: NTP`
  - `🌍 Geolocation confirmed`
- `/debug` shows:
  - `⚠ Default (UTC) - geolocation pending`
  - `Timezone: — (—)`
  - `Current Offset: —`
  - `Snapshot: —`
  - `Next change: No upcoming time change available`

That output means the debug page is not merely showing stale data. It means the browser received `available: false` from `/api/transmitter_time_transitions`, so the entire section dropped into its placeholder branch.

## What Is Working

### 1. The dashboard path is healthy

The dashboard polls `/api/transmitter_health` and renders:

- `uptime_ms`
- `unix_time`
- `utc_offset_min`
- `time_source`
- `geolocation_valid`

These values come from the receiver heartbeat cache:

- transmitter sends `heartbeat_t`
- receiver `RxHeartbeatManager::on_heartbeat(...)` updates:
  - `TransmitterManager::updateTimeData(...)`
  - `TransmitterManager::updateHeartbeatFlags(...)`
- `/api/transmitter_health` returns those values
- `/` renders them

This path is clearly working because `/` is already showing:

- correct wall-clock time
- NTP source
- geolocation confirmed

### 2. Receiver-side snapshot routing exists

The receiver does have a route for `msg_time_transitions_snapshot`.

In `espnowreceiver_2/src/espnow/espnow_tasks.cpp`:

- it registers a route for `msg_time_transitions_snapshot`
- validates CRC32
- calls `TransmitterManager::updateTimeTransitionSnapshot(...)`

The receiver cache for the snapshot also exists:

- `TransmitterState::update_time_transition_snapshot(...)`
- `TransmitterState::has_time_transition_snapshot()`
- `TransmitterState::get_time_transition_snapshot(...)`

So the receiver is capable of storing and serving the snapshot **if it receives one**.

## What Is Broken

## 1. `/debug` depends on a different data channel than `/`

The debug page fetches `/api/transmitter_time_transitions`.

That API currently behaves like this:

- if `TransmitterManager::hasTimeTransitionSnapshot()` is false:
  - return `available = false`
  - return no timezone name
  - return no current offset
  - return no transitions
- the browser then renders placeholder values and the warning banner

The debug UI code does this explicitly:

- when `!data.success || !data.available`
  - `tz-name = —`
  - `tz-abbrev = —`
  - `tz-offset = —`
  - `tz-generated = status text`
  - `next-change = No upcoming time change available`
  - banner = `⚠ Default (UTC) - geolocation pending`

That means the page is not actually saying “the transmitter is using UTC”.
It is saying “I do not have the snapshot object, so I am showing the fallback placeholder state”.

This is the first major root cause.

## 2. The snapshot path is independent, optional, and more fragile than heartbeat

The heartbeat path is refreshed every 10 seconds and is already proven healthy.
The transition snapshot path is a separate message flow:

1. transmitter builds a `TimeTransitionSnapshot`
2. `VersionBeaconManager` converts it to `time_transitions_snapshot_t`
3. sends it over ESP-NOW
4. receiver route stores it
5. `/api/transmitter_time_transitions` exposes it
6. `/debug` renders it

Any failure in that chain causes `/debug` to show placeholders, even while `/` remains correct.

So the current architecture allows exactly the observed split-brain behaviour.

## 3. Confirmed transmitter bug: snapshot cache is refreshed before geolocation-valid is flipped on

There is a confirmed ordering bug in `ESPnowtransmitter2/espnowtransmitter2/lib/ethernet_utilities/ethernet_utilities.cpp`.

Inside `configure_timezone_from_location_internal(...)`, the order is currently:

1. `setenv("TZ", posix_tz, 1)`
2. `tzset()`
3. set `detected_timezone_name`
4. set `detected_timezone_abbreviation`
5. `refresh_detected_timezone_abbreviation_from_system_time()`
6. `refresh_cached_utc_offset()`
7. `refresh_time_transition_snapshot_cache()`
8. `timezone_configured = true`
9. `timezone_auto_detected = true`

But `refresh_time_transition_snapshot_cache()` builds the snapshot using:

- `next.geolocation_valid = timezone_auto_detected`

At the moment the cache is refreshed, `timezone_auto_detected` is still `false`.

### Result

A successful geolocation lookup can still produce a cached snapshot that says:

- geolocation invalid
- fallback status semantics

until a later refresh happens.

This is a real bug and it is fully consistent with the earlier mismatch where the dashboard showed confirmed geolocation while the debug data path lagged behind.

## 4. Why this bug can persist longer than expected

After timezone lookup succeeds, the task only forces an immediate NTP refresh if `timezone_rule_changed` is true.

That matters because:

- default startup timezone is already a DST-aware default
- if the detected timezone maps to the same POSIX rule as the default, `tz_changed` can be false
- in that case there may be no immediate follow-up refresh of the snapshot after `timezone_auto_detected` becomes true

So the stale snapshot state can survive far longer than intended.

This is the second major root cause.

## 5. Why the current user-facing `/debug` output is worse than it needs to be

Even if the snapshot is genuinely missing, the receiver still knows from heartbeat that:

- time is synchronized
- current UTC offset is valid
- geolocation has succeeded

However, `/api/transmitter_time_transitions` does not return those heartbeat-backed values when no snapshot is available, and the debug UI does not try to use them.

So the page degrades to a misleading all-dashes state instead of a truthful partial state such as:

- geolocation confirmed
- current offset known
- transition schedule unavailable

This is not the root cause of snapshot loss, but it is the root cause of the poor operator experience.

## Most Likely Explanation for the Exact Current Symptom

Given the current code, the most likely sequence is:

1. transmitter heartbeat path is healthy
2. receiver heartbeat cache is healthy
3. `/` shows correct time, NTP, and geolocation confirmed
4. the transition snapshot has not been received by the receiver runtime cache
5. `/api/transmitter_time_transitions` returns `available: false`
6. `/debug` enters the placeholder branch and shows dashes plus the warning banner

This exact outcome does **not** require the dashboard to be wrong, and it does **not** require heartbeat to be wrong.
It only requires the snapshot cache on the receiver to be empty.

## Why the Receiver Snapshot Cache Can Be Empty

The code analysis supports the following candidate causes, ranked by confidence.

### High confidence

#### A. Snapshot/UI architecture fails closed

This is confirmed.
If the snapshot is missing for any reason, `/debug` shows placeholders and misleading wording.

#### B. Transmitter snapshot cache can be built with `geolocation_valid = false` after successful geolocation

This is confirmed.
The ordering bug in `configure_timezone_from_location_internal(...)` is real.

### Medium confidence

#### C. Snapshot message was not received by the receiver at runtime

This is strongly suggested by the actual `/debug` output.

If the receiver had any snapshot at all, the API would return `available: true`, and the page would show at least:

- timezone name
- abbreviation
- generated timestamp
- offset
- transition table or empty transition message

Because none of that is present, the receiver snapshot cache is almost certainly empty at runtime.

#### D. The original one-shot/event-driven design was too easy to miss

This is historically true and explains prior failures.
A periodic resend was added, which improves this substantially, but the page output still proves the receiver cache remained empty in the test state the user observed.

### Lower confidence, but plausible

#### E. Snapshot send/receive is occurring, but runtime evidence is insufficient

There is currently no strong operator-visible telemetry for:

- last snapshot send time
- last snapshot receive time
- last snapshot revision received
- last snapshot CRC failure
- snapshot age on receiver

That makes it hard to distinguish:

- transmitter never sent
- send failed
- receiver dropped
- receiver never routed
- UI used fallback

This is a diagnosability problem.

## Source-of-Truth Comparison

### Dashboard `/`

Source:
- `/api/transmitter_health`
- heartbeat-backed receiver runtime state

Properties:
- high frequency
- proven healthy
- already reflects real transmitter state
- does not include future DST transition schedule

### Debug `/debug`

Source:
- `/api/transmitter_time_transitions`
- cached ESP-NOW snapshot only

Properties:
- lower frequency
- separate message type
- currently all-or-nothing
- carries timezone name, abbreviation, revision, generation time, and transition rows
- currently fails closed and can contradict `/`

## Recommendations

## Priority 1 — Fix the transmitter ordering bug

Change `configure_timezone_from_location_internal(...)` so that `timezone_auto_detected` is true **before** the transition snapshot cache is refreshed.

Recommended order:

1. set `TZ`
2. call `tzset()`
3. update detected timezone strings
4. set `timezone_configured = true`
5. set `timezone_auto_detected = true`
6. refresh abbreviation
7. refresh cached UTC offset
8. refresh transition snapshot cache

At minimum, call `refresh_time_transition_snapshot_cache()` again after setting `timezone_auto_detected = true`.

This removes the confirmed stale-flag bug.

## Priority 2 — Make `/debug` use heartbeat-backed live fields even when snapshot is missing

`/debug` should not show `⚠ Default (UTC) - geolocation pending` just because the transition snapshot is unavailable.

Recommended API behaviour when snapshot is missing:

Return:

- `available: false`
- `geolocation_valid` from heartbeat cache
- `current_utc_offset_min` from heartbeat cache
- `time_source` from heartbeat cache
- `status` explaining that only the transition schedule is unavailable

Recommended UI behaviour:

- show live geolocation banner from heartbeat state
- show live current offset from heartbeat state
- show timezone name/abbrev as unavailable if snapshot missing
- show transition table placeholder only for the missing schedule data

This will make `/debug` operationally truthful.

## Priority 3 — Add snapshot observability

Add the following fields to `/api/transmitter_time_transitions`:

- `snapshot_received`
- `snapshot_revision`
- `snapshot_generated_unix_utc`
- `snapshot_last_received_ms`
- `snapshot_age_ms`
- `snapshot_geolocation_valid`
- `heartbeat_geolocation_valid`
- `heartbeat_utc_offset_min`

Also add logs on both devices:

Transmitter:
- snapshot built
- snapshot send attempted
- snapshot send succeeded/failed
- revision number

Receiver:
- snapshot received
- revision number
- CRC failure count
- last receive timestamp

This will make the failure mode obvious within minutes.

## Priority 4 — Trigger snapshot send at stronger lifecycle points

In addition to periodic resend, force a snapshot send when:

- receiver connection is first established
- geolocation first succeeds
- NTP first becomes valid
- timezone rule changes

This reduces dependence on passive loop timing.

## Priority 5 — Persist the last valid snapshot on the receiver

If the receiver reboots, the current runtime cache starts empty and `/debug` becomes blank until a new snapshot arrives.

Persisting the last valid snapshot would allow `/debug` to show:

- last known timezone
- last known transitions
- staleness indicator

This is optional, but it would materially improve UX.

## Recommended Implementation Strategy

### Short-term fix

1. fix ordering bug in transmitter snapshot generation
2. modify `/api/transmitter_time_transitions` to always include heartbeat-backed current status
3. modify debug UI so missing snapshot does not imply fallback UTC

### Medium-term hardening

4. add snapshot send/receive diagnostics
5. add explicit snapshot timestamps and ages
6. add forced snapshot send on connection and geolocation success

### Long-term cleanup

7. clearly separate:
   - live time status
   - current timezone state
   - future transition schedule
8. avoid a single `available` flag hiding all fields at once

## Final Conclusion

The problem is **not** that the transmitter’s live time is wrong.
The problem is that the `/debug` timezone section is built on a **different, more fragile, fail-closed data path** than `/`.

There are two key findings:

1. **Confirmed design issue:** `/debug` only trusts the transition snapshot and shows misleading placeholders when that snapshot is absent.
2. **Confirmed code bug:** the transmitter refreshes the transition snapshot cache before `timezone_auto_detected` is set to true, allowing the snapshot to lag behind real geolocation state.

Those two issues together fully explain why `/` can be correct while `/debug` is still wrong.

## Recommended Next Action

Implement these in order:

1. fix the transmitter snapshot ordering bug
2. make `/api/transmitter_time_transitions` return heartbeat-backed live fields even without a snapshot
3. change the `/debug` page to show partial truth instead of all-dashes placeholders
4. add snapshot send/receive telemetry so future failures are immediately diagnosable
