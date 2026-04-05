# Azure Maps Timezone API — DST Transition Replacement Report
**Date:** 2026-03-31  
**Scope:** Replace current transmitter-side DST derivation with authoritative Azure Maps timezone transitions, while reusing existing receiver data structures and ESP-NOW transport.

---

## Executive summary

Yes — Azure Maps can provide the transition data needed to build the **next 4 DST/offset changes** accurately.

The correct replacement approach is:
1. Keep geolocation (lat/lon or IANA zone detection) as today.
2. Query **Azure Maps Timezone API** (`byId` preferred, `byCoordinates` optional).
3. Request `options=transitions` (or `all`) with `transitionsFrom=<now>` and `transitionsYears=<2..3>`.
4. Convert returned transition periods into transition instants (`UtcStart`) and map to your existing `TimeTransitionSnapshot`/`time_transitions_snapshot_t` structs.
5. Publish over existing ESP-NOW snapshot path (no wire protocol changes required).

This removes the current fragile local inference path based on hand-maintained POSIX mapping + daily offset scanning.

---

## 1) What Azure Maps provides (relevant to your use case)

Authoritative Microsoft REST docs confirm:

- **Get Timezone By Coordinates**  
  `GET https://atlas.microsoft.com/timezone/byCoordinates/json?...`
- **Get Timezone By ID**  
  `GET https://atlas.microsoft.com/timezone/byId/json?...`

Both support:
- `options=transitions` or `options=all`
- `transitionsFrom` (start timestamp)
- `transitionsYears` (look-ahead window)

Response includes:
- `TimeZones[0].Id` (IANA zone)
- `ReferenceTime` (current offset, daylight flag, tag)
- `TimeTransitions[]` periods with:
  - `UtcStart`
  - `UtcEnd`
  - `StandardOffset`
  - `DaylightSavings`
  - `Tag`

Also useful:
- `Get Timezone IANA Version` for diagnostics/version tracking.

### Source references
- https://learn.microsoft.com/en-us/rest/api/maps/timezone/get-timezone-by-id?view=rest-maps-2026-01-01
- https://learn.microsoft.com/en-us/rest/api/maps/timezone/get-timezone-by-coordinates?view=rest-maps-2026-01-01
- https://learn.microsoft.com/en-us/rest/api/maps/timezone/get-timezone-iana-version?view=rest-maps-2026-01-01

---

## 2) Why current implementation is fragile

Current transmitter logic in `ethernet_utilities.cpp`:
- Geolocation provider returns timezone name.
- Name is mapped via a **small static table** (`map_timezone_name_to_posix`).
- Unknown zones fall back to **fixed UTC offset** (`build_fixed_offset_posix_tz`) → often no DST transitions.
- Transitions are inferred by scanning future days and binary searching offset flips (`refresh_time_transition_snapshot_cache`).

### Main issues
1. **Coverage risk**: zones not in the mapping table cannot produce correct future DST rules.
2. **Rule drift risk**: local POSIX mapping can lag legislative changes.
3. **Inference risk**: transition detection is algorithmic and depends on local TZ state assumptions.
4. **Maintenance burden**: mapping table and edge-case correctness are ongoing liabilities.

---

## 3) Recommended target design

## 3.1 API choice

Use **`byId`** as primary once you have an IANA timezone ID.
- Deterministic behavior for a known zone.
- Avoids coordinate ambiguity near borders.

Use **`byCoordinates`** only for initial zone lookup (or fallback), then switch to byId.

## 3.2 Request pattern

For zone `Europe/London`:

`GET https://atlas.microsoft.com/timezone/byId/json?api-version=1.0&options=transitions&query=Europe/London&transitionsFrom=<UTC_ISO_NOW>&transitionsYears=3&subscription-key=<KEY>`

Notes:
- `transitionsYears=2..3` is enough to reliably extract next 4 transitions globally.
- Keep `timeStamp` optional; `transitionsFrom` drives forward-window extraction.

## 3.3 Parsing to your existing structs

Given `TimeTransitions[]` periods, derive boundary transitions as follows:

1. Sort periods by `UtcStart`.
2. For each adjacent pair `(prev, curr)`:
   - transition instant = `curr.UtcStart`
   - `offset_before_min` = parse(prev.StandardOffset + prev.DaylightSavings)
   - `offset_after_min`  = parse(curr.StandardOffset + curr.DaylightSavings)
3. Keep only transitions where `transition_utc > now`.
4. Take first 4.

Map directly into existing fields:
- `timezone_name` = `TimeZones[0].Id`
- `timezone_abbrev` = `ReferenceTime.Tag`
- `current_utc_offset_min` = parse(`ReferenceTime.StandardOffset + ReferenceTime.DaylightSavings`)
- `transitions[i]` = derived boundaries

No receiver schema changes required.

---

## 4) How this fits current transmitter/receiver setup

Current pipeline already works structurally:
- TX builds `TimeTransitionSnapshot` in ethernet utilities.
- TX publishes snapshot via `VersionBeaconManager` as `msg_time_transitions_snapshot`.
- RX stores snapshot and debug UI renders rows.

Therefore replacement is **inside snapshot generation only**.

### Minimal integration point

Replace internals of `refresh_time_transition_snapshot_cache()` data source:
- **Before:** local TZ-rule inference.
- **After:** Azure Maps transitions response (authoritative source), then same snapshot struct fill.

Transport remains unchanged:
- `time_transitions_snapshot_t`
- checksum
- ESP-NOW route on receiver

---

## 5) Suggested implementation plan (safe rollout)

### Phase A — Add Azure Maps provider module
Create transmitter module (e.g. `timezone_provider_azure_maps.*`) that:
- Calls `byId` / `byCoordinates`
- Parses `ReferenceTime` and `TimeTransitions`
- Returns normalized internal DTO:
  - `iana_id`
  - `abbrev`
  - `current_offset_min`
  - `next_transitions[<=4]`

### Phase B — Dual-run validation mode
For 1–2 firmware iterations:
- Compute snapshots from **old** and **Azure** paths.
- Publish comparison diagnostics via MQTT (`TZ_COMPARE` tag).
- Keep ESP-NOW sending Azure snapshot only when confidence checks pass.

### Phase C — Cutover
- Remove mapping-table dependence for transition generation.
- Keep local POSIX timezone setting only for local clock display (optional).

### Phase D — Hardening
- Cache last successful Azure result in RAM/NVS with timestamp + IANA version.
- Use retry/backoff and health telemetry.

---

## 6) Reliability and security considerations

1. **Auth**: use subscription key initially (simpler), move to SAS/AAD later if needed.
2. **Key handling**: store in secured config/NVS; do not emit key in logs.
3. **Rate limits/cost**: this API is low-frequency in your flow (boot + periodic refresh), so cost/throughput should be small.
4. **Fallback policy**:
   - If Azure unavailable, keep last known valid snapshot.
   - If no historical cache exists, mark `geolocation_valid=false`, send empty transitions with explicit status.
5. **Version observability**:
   - Optionally query IANA version endpoint and include in MQTT diagnostics.

---

## 7) Concrete improvements over current behavior

1. **Correctness**: transitions sourced from maintained timezone data service.
2. **Coverage**: no dependence on local hardcoded mapping table breadth.
3. **Simpler code**: remove day-scan/binary-search inference complexity.
4. **Better debugging**:
   - log provider response summary (`zone`, `offset`, `next4`)
   - log fallback reason when Azure unavailable
5. **Deterministic tests**:
   - unit test parser with captured Azure JSON fixtures
   - verify transition extraction around DST boundaries.

---

## 8) Recommended final architecture (in one line)

**NTP gives UTC time; Azure Maps gives authoritative timezone transitions; existing ESP-NOW snapshot transport distributes that authoritative data unchanged to receiver/UI.**

---

## 9) Practical answer to your core question

Can we get the next 4 DST values correctly using Azure Maps for a specified timezone?  
**Yes.** Use `timezone/byId` with `options=transitions` + `transitionsFrom` + sufficient `transitionsYears`, parse adjacent transition periods, and publish first 4 future boundaries.

This is the clean replacement for the current inferred approach and is fully compatible with your existing receiver/ESP-NOW data path.
