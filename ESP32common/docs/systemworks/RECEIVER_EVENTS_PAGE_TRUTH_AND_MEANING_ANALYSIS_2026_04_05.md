# Receiver `/events` Behaviour Analysis (What Is Truly Happening)

**Date:** 2026-04-05  
**Scope:** Receiver dashboard event card (`/`) vs receiver event page (`/events`) and transmitter event sources  
**Projects inspected:**
- `espnowreceiver_2`
- `ESPnowtransmitter2/espnowtransmitter2`

---

## Executive Summary

The current system is working as coded, but **the UI semantics are mixed**:

1. The dashboard card on `/` is driven primarily by an **ESP-NOW event summary counter** (`/api/get_event_log_summary`) that includes a **"new since last report"** number.
2. The `/events` page is driven by `/api/get_event_logs`, which on receiver prefers the **receiver MQTT event cache** (delta-fed) and only falls back to **HTTP proxy to transmitter** when cache is unavailable.
3. Because of this, users can see **"new" on dashboard** but then interpret `/events` as "all historical logs", while it is actually a **merged active-event view from recent deltas** (or fallback snapshot), not a guaranteed full historical timeline.

So the issue is mostly **data-contract ambiguity**, not one single broken handler.

---

## What each page is actually using

## 1) Dashboard (`/`) event card

Receiver dashboard script (`dashboard_page_script.cpp`) does:
- Calls `/api/get_event_log_summary`
- Prefers ESP-NOW summary fields:
  - `total_historical`
  - `error_historical`
  - `new_since_last_report_total`

Receiver API (`api_telemetry_handlers.cpp`, `api_get_event_log_summary_handler`) does:
- Sends `send_event_log_summary_request()` (non-blocking ESP-NOW request)
- Returns latest cached summary from `TransmitterManager::getEventLogSummary()`

Transmitter summary build (`message_routes.cpp`, `build_event_log_summary()`):
- Computes deltas against `g_event_occurrences_reported[]`
- **Updates baseline every summary build**

### Important consequence
`new_since_last_report_total` is **ephemeral delta since previous summary report**, not a durable unread counter. It can reset based on polling cadence, even if user never opened `/events`.

---

## 2) Receiver `/events` page data source

Event page script (`event_logs_page_script.cpp`) does:
- On load: POST `/api/event_logs/subscribe`
- Then GET `/api/get_event_logs?limit=100`
- Renders rows with `event/type`, `level`, `timestamp`, `count`, `data`, `message`, and optional `is_new` marker

Receiver API (`api_telemetry_handlers.cpp`, `api_get_event_logs_handler`) uses this order:
1. If `TransmitterManager::hasEventLogs()` is true, return receiver-side cached logs (`source: "mqtt"`)
2. Else, proxy HTTP to transmitter `/api/get_event_logs?limit=...`

So `/events` is **not guaranteed to always come from transmitter live HTTP snapshot**.

---

## 3) What transmitter publishes over MQTT for event logs

Transmitter MQTT manager (`mqtt_manager.cpp`, `publish_event_logs()`):
- Publishes **only events that are `occurences > 0` and `!MQTTpublished`**
- After publish, marks each as published (`set_event_MQTTpublished(...)`)
- Topic is retained (`transmitter/BE/event_logs`)
- Runs only when subscriber count > 0

This is a **delta-style stream**, not full re-broadcast of all event rows each cycle.

---

## 4) How receiver cache merges event deltas

Receiver cache (`transmitter_event_log_cache.cpp`, `store_event_logs()`):
- Requires `events[]`
- Clears `is_new` on existing entries each batch
- For each incoming event:
  - Finds existing row by event `type`
  - Replaces existing row or inserts new row
- Sorts by timestamp desc

So cache is effectively a **latest-state-per-event-type table** with `count` retained from latest payload entry.

---

## Root causes of the confusing user experience

## A) Two different semantics shown as one concept
- Dashboard “new” = **delta occurrences since last summary report**
- `/events` table = **current merged event rows (usually by type) from MQTT delta cache**, not a strict unread queue

## B) Summary requests alter the baseline
Every summary request triggers transmitter summary generation, which advances the internal baseline used for "new".

## C) `/events` is not always authoritative transmitter snapshot
Receiver prefers local MQTT cache when present. Users expecting "all transmitter logs" may assume direct/live pull, but that is not always what happens.

## D) Count field and row count are easy to misread
- Row count = number of cached event types
- `count` column = occurrence count per type
- Dashboard “new” can be larger/smaller than row count depending on repeated occurrences

---

## Is there a data-loss bug?

From the inspected flow, this is primarily a **representation mismatch** rather than hard loss:
- Transmitter HTTP `/api/get_event_logs` gives active-event snapshot by timestamp, bounded by `limit`
- MQTT event stream gives incremental updates for changed/unpublished events
- Receiver cache merges by type and is bounded

However, users can still perceive missing data because the page title/expectation implies "all logs" while implementation is "current merged snapshot from mixed sources".

---

## Recommended solution (to make display meaningful)

## Recommendation 1 (Primary): Split and label semantics explicitly

On dashboard card (`/`):
- Replace current ambiguous text with:
  - `Active event types: X`
  - `Active error types: Y`
  - `New occurrences since last summary: Z`
- Rename “new” wording to **"new occurrences since last check"**

On `/events` page:
- Add explicit source badge from API (`mqtt cache` vs `transmitter snapshot`)
- Add explanatory helper text:
  - "Rows represent current event types; `count` is total occurrences for each type."

This is lowest risk and immediately clarifies behaviour.

## Recommendation 2 (Data contract hardening): Add authoritative snapshot endpoint

Create receiver endpoint that **always proxies transmitter HTTP** (e.g. `/api/get_event_logs_snapshot`) and use it on `/events` initial load.

Then optionally layer MQTT updates on top for live refresh. This gives:
- Deterministic baseline (authoritative snapshot)
- Fast incremental updates (MQTT)

## Recommendation 3 (Optional): Keep unread/new state receiver-local

If true "unread" UX is desired:
- Track `last_seen_summary_seq` or per-event last-seen timestamp on receiver
- Compute unread/new locally
- Do not reuse transmitter’s polling delta directly as UI unread badge

This avoids badge resets caused by background summary polling.

---

## Concrete implementation plan (minimal-to-medium change)

1. **UI wording updates only** (fast):
   - Dashboard status string labels updated for semantics
   - `/events` source badge + explanatory note

2. **Receiver API split** (medium):
   - Keep `/api/get_event_logs` as current merged cache path
   - Add `/api/get_event_logs_snapshot` forced proxy to transmitter HTTP
   - `/events` initial fetch uses snapshot endpoint

3. **Optional unread model** (larger):
   - Add receiver-side unread tracker (summary seq + event timestamps)
   - Dashboard badge uses unread tracker, not raw transmitter delta

---

## Validation checklist after changes

- Dashboard and `/events` no longer appear contradictory during normal use
- Opening `/events` always starts with transmitter-authoritative snapshot
- New badge behaviour is stable across refreshes and background polling
- Source badge accurately reports where list came from
- Counts match their labels (`event types`, `occurrences`, `new occurrences`)

---

## Final conclusion

The current behaviour is internally consistent with the code, but the UX expectation (“/events should show all transmitter logs”) does not match the mixed-source, mixed-semantics pipeline.

The best practical fix is:
1) **clarify labels immediately**, and
2) **use a dedicated authoritative snapshot call for `/events` initial render**,
so the resulting display is technically accurate and user-meaningful.