# Event Log Count Path Analysis (Transmitter ➜ Receiver)

**Date:** 2026-04-04  
**Repo:** esp-multi-project  
**Branch:** feature/battery-emulator-migration

---

## 1) Executive summary

I traced how the Event Logs “message number” (total/error counts shown in receiver UI) currently moves through the system.

### Main finding
The displayed count is currently **context-dependent and can drift/disappear** because there are two different data paths with different semantics:

1. **Transmitter HTTP endpoint** (`/api/get_event_logs`) reports **active/historical event entries** (`occurences > 0`), limited by query.
2. **Transmitter MQTT event_logs publish** reports only **delta (unpublished) events** when subscribed.

On the receiver side, cached MQTT data is treated as canonical once present, and `event_count` is recomputed from cached array length. That means the UI can show “count of latest delta batch” rather than “true total events in transmitter event table.”

Also, receiver cache replacement currently causes older entries to disappear from the receiver event list when a smaller/newer delta arrives.

---

## 2) Current end-to-end flow (as implemented)

## 2.1 Event generated in transmitter event core

In transmitter event core (`src/battery_emulator/devboard/utils/events.cpp`):

- `set_event(...)` increments `occurences` **only when event transitions from inactive to active**.
- It sets `MQTTpublished = false` for that event.
- Event metadata (`timestamp`, `data`, `state`) is updated.

This is the trigger source for downstream publishing behavior.

Important semantic detail:

- The event backend is **not a FIFO queue of messages**.
- It is a **fixed table** (`EVENT_NOF_EVENTS`) with one slot per event type.
- Each slot tracks state + metadata (`occurences`, `timestamp`, `data`, `MQTTpublished`).
- So “new” currently means: this event type has changed and not yet published (`MQTTpublished == false`).

---

## 2.2 Transmitter MQTT publication logic

In transmitter MQTT manager (`src/network/mqtt_manager.cpp`, `publish_event_logs()`):

- Publishing only runs when `event_log_subscribers_ > 0`.
- It collects events where:
  - `occurences > 0`
  - `MQTTpublished == false`
- This is **delta mode** (changed/unpublished set), not full snapshot mode.
- JSON field `event_count` is set to `ordered.size()` (the unpublished set size).
- After publish success, all sent events are marked via `set_event_MQTTpublished(...)`.

So MQTT `event_count` is “how many changed events in this publish cycle,” not total active events.

---

## 2.3 How receiver asks transmitter to publish event logs

Receiver sends ESP-NOW `msg_event_logs_control` (subscribe/unsubscribe) in `send_event_logs_control()` (`src/espnow/espnow_send.cpp`).

This is triggered by receiver web subscription count changes (`MqttClient::incrementEventLogSubscribers()/decrementEventLogSubscribers()`).

Transmitter receives `msg_event_logs_control` and increments/decrements its internal subscriber count in `src/espnow/message_routes.cpp`.

---

## 2.4 Receiver ingestion and caching

Receiver MQTT handler (`src/mqtt/mqtt_client.cpp`, `handleEventLogs`) stores incoming JSON into cache through `TransmitterManager::storeEventLogs(...)`.

Event log cache (`lib/webserver/utils/transmitter_event_log_cache.cpp`):

- Stores only parsed `events[]` entries.
- Does **not preserve transmitter-provided `event_count` as a separate truth value**.
- API later reports `event_count = logs.size()` from cached vector.
- Cache is **fully replaced** on each incoming MQTT event_logs payload (`event_logs.clear()` first).

This replacement behavior is the main reason events appear to “disappear” on receiver.

---

## 2.5 Receiver API behavior

`api_get_event_logs_handler` (`lib/webserver/api/api_telemetry_handlers.cpp`) does:

1. If cached event logs exist, return cache (`source: mqtt`, `event_count = logs.size()`).
2. Else fallback to direct transmitter HTTP `/api/get_event_logs` proxy.

So after cache becomes non-empty, receiver generally serves MQTT-derived cache semantics.

---

## 2.6 What “Clear Event Logs” actually does

When receiver calls `/api/clear_event_logs`:

1. Receiver forwards to transmitter `/api/clear_event_logs`.
2. Transmitter executes `reset_all_events()`.
3. `reset_all_events()` resets the whole event table:
   - `state = INACTIVE`
   - `occurences = 0`
   - `timestamp = 0`
   - `MQTTpublished = false`
4. Receiver also clears its local cache.

So yes: this is effectively a **full event table reset**, not partial dequeue of a message queue.

---

## 3) Why the message number can be misleading today

The currently displayed total/error numbers can diverge from transmitter reality because:

1. MQTT path is delta-oriented (changed events only).
2. Receiver converts latest cached set into “total” by vector length.
3. Subscription can be temporary (page opened/closed), creating stale cache windows.
4. Dashboard error count is computed from current returned list only, not transmitter-global event table.

Net result: the UI may show the size of the last changed batch rather than actual system-wide totals.

And because the receiver cache replaces on every delta payload, previously seen entries can vanish from the receiver list.

---

## 4) Assessment of your proposed direction

Your proposal: send a **dedicated ESP-NOW summary message** from transmitter event subsystem carrying:

- total events
- error events

This is a strong direction and directly addresses count-semantic mismatch.

### Why it helps

- Decouples **summary counters** from heavy event payload transport.
- Provides deterministic, low-latency count updates independent of MQTT subscription state.
- Keeps event detail transport (MQTT/HTTP) separate from summary transport (ESP-NOW).

---

## 5) Recommended architecture change

## 5.1 Add a dedicated ESP-NOW summary message

Add new message type in shared protocol (esp32common `espnow_common.h`), for example:

- `msg_event_log_summary`

Payload (packed), suggested fields:

- `uint8_t type`
- `uint32_t seq` (monotonic summary sequence)
- `uint32_t total_events`
- `uint32_t error_events`
- `uint32_t warning_events` (optional but useful)
- `uint64_t timestamp_ms` (or uptime ms)

Use 32-bit counters for long uptime safety.

## 5.2 Triggering model (event-driven, but coalesced)

Do not send ESP-NOW directly inside every raw event call site. Instead:

1. Event core sets a `summary_dirty` flag when event state/count changes.
2. A transmitter-side sender context (existing ESP-NOW task or lightweight publisher) emits summary when dirty.
3. Apply a short coalesce/rate-limit window (e.g. 200–500 ms) to avoid bursts during event storms.

This still satisfies “triggered from event log itself” while protecting queue stability.

## 5.3 Receiver handling

Receiver route handler stores summary in a tiny cache struct (latest counters + seq + age).

Dashboard/event card should prefer this summary for:

- total events
- error events

Detailed event page can continue using existing API list transport.

## 5.4 Resynchronization strategy (no periodic heartbeat)

Per your requirement, do **not** resend periodically.

Use on-demand recovery instead:

- Send summary when event table changes (coalesced).
- Send summary when receiver explicitly requests sync (page open / reconnect / manual refresh).
- Include `seq` for out-of-order protection.
- Include cumulative counters so a missed packet can be corrected on next sync request.

This keeps traffic low and still preserves correctness.

---

## 6) System impact analysis

## 6.1 Positive impacts

1. **Correctness of counts**
   - Eliminates ambiguity between delta batch size and total count.

2. **Lower latency for counters**
   - Count updates no longer wait for MQTT publish intervals/subscription state.

3. **Reduced HTTP dependence for dashboard summary**
   - Summary can stay accurate even when detail fetch is deferred.

4. **Cleaner separation of concerns**
   - Summary = ESP-NOW control plane.
   - Detail logs = MQTT/HTTP data plane.

## 6.2 Risks / costs

1. **Missed ESP-NOW summary packet** (no heartbeat design)
   - Mitigate with explicit receiver `request_summary` on connect/page open.
   - Keep cumulative totals in payload so next sync heals any missed delta.

2. **RX queue pressure during event storms**
   - Existing queue depths are modest; avoid per-event immediate send.

3. **Counter consistency with event table semantics**
   - Define exactly what “total” means:
     - total entries with `occurences > 0` (historical)
     - or currently active (`state == ACTIVE|ACTIVE_LATCHED`)
   - Recommendation: publish both if needed (`total_historical`, `total_active`).

4. **Migration complexity (UI/API)**
   - Dashboard should switch to summary counts while preserving legacy fallback behavior.

---

## 7) Suggested implementation plan

## Phase 1 (safe, minimal)

1. Add `msg_event_log_summary` to shared protocol.
2. Add transmitter summary publisher with dirty-flag + rate-limit.
3. Add receiver route + summary cache.
4. Display summary counts on dashboard card (with fallback to current API if summary unavailable).
5. Add receiver-initiated summary sync request (no periodic resend).

## Phase 2 (consistency hardening)

5. Keep detailed logs in current transport path, but label semantics clearly:
   - `event_count` in detail API should represent detail list size, not global total.
6. Optionally add explicit fields in detail API:
   - `global_total_events`
   - `global_error_events`
   (sourced from latest summary)

## Phase 3 (observability)

7. Add diagnostics endpoint exposing summary age/seq and last source.
8. Add telemetry counters for dropped/out-of-order summary packets.

---

## 8) Final recommendation

Yes — re-evaluating this and adding an ESP-NOW event summary message is the right move.

I recommend implementing a **coalesced event-driven summary path** from transmitter event core to receiver, with sequence numbers and **receiver-initiated sync requests** (not periodic heartbeat). This will make event count/error count accurate while keeping traffic minimal.

---

## 9) Clarification for “since last reported” counts and `*new` marker

Your suggestion is valid and aligns with the current table-style event backend.

Recommended summary payload semantics:

- `total_historical` = number of event slots with `occurences > 0`
- `error_historical` = subset with error level
- `new_since_last_report_total`
- `new_since_last_report_error`
- `seq`

And in receiver UI:

- Keep an event map by `event type` (or event ID) instead of replacing full list.
- Mark entries received in latest delta as `is_new = true` and show `*` next to type.
- Clear `is_new` after user visits page or after acknowledgement action.

This gives:

- Stable historical list (no vanishing items)
- Explicit new-vs-existing visibility (`*`)
- Correct counters on dashboard

---

## 10) Why events currently “disappear” on receiver

Root cause found:

1. Transmitter MQTT event logs are delta-only (`MQTTpublished == false`).
2. Receiver cache function clears all prior cached entries before storing incoming payload.
3. Therefore each incoming delta overwrites prior list.
4. If latest delta has fewer entries, older ones seem to disappear.

This is expected with current implementation and is not currently a pure “new only view toggle”; it is a destructive cache replacement behavior.

### Fix

- Change receiver cache ingest to **merge by event type** instead of replace.
- Preserve existing entries unless explicitly cleared.
- Track `is_new` separately for UI marker.

That will stop apparent disappearance and support your `*new` requirement directly.
---

## 11) Deep investigation: "viewed" semantics and the dashboard button count

**Date investigated:** 2026-04-04  
**Scope:** How events are marked "new"/"viewed" on `/events`, and how this connects to the "Event Logs" button count on the `/` dashboard.

---

### 11.1 The `is_new` flag — what it means and who sets it

**File:** `espnowreceiver_2/lib/webserver/utils/transmitter_event_log_cache.cpp`  
**Function:** `TransmitterEventLogCache::store_event_logs(const JsonObject& logs)`

Every time a MQTT payload arrives on `transmitter/BE/event_logs`, this function runs:

```cpp
// Step 1: Clear is_new for ALL entries already in cache
for (auto& entry : event_logs) {
    entry.is_new = false;
}

// Step 2: For every entry in the incoming batch, set is_new = true
for (JsonObject evt : events) {
    EventLogEntry entry = {};
    ...
    entry.is_new = true;  // All new-batch entries are marked new
    ...
    // Either overwrite existing, or push_back as new entry
}
```

**`is_new` means:** "this event entry appeared in the most recently received MQTT delta batch."  
It is **not** a user-interaction flag. It does **not** persist between MQTT batches.

Since the transmitter's MQTT publisher sends only events where `MQTTpublished == false` (the delta set), the `is_new` flag tracks "which events changed since the last MQTT publish cycle."

---

### 11.2 How `is_new` is displayed on the `/events` page

**File:** `espnowreceiver_2/lib/webserver/pages/event_logs_page_script.cpp`  
**Function:** `loadEvents()` (called on page load)

The JS fetches `/api/get_event_logs?limit=100`, which returns `is_new` per entry from the receiver cache.

```js
const isNew = !!evt.is_new;
const eventTypeDisplay = isNew
    ? (eventType + '<span class="evt-new-marker">*</span>')
    : eventType;
```

Entries with `is_new = true` show a yellow `*` marker next to the event type name. This is purely cosmetic client-side rendering — **nothing server-side is changed by the user viewing the page**.

---

### 11.3 What triggers "viewed" — the critical finding

**There is no explicit "viewed" or "mark as read" mechanism anywhere in the codebase.**

The `is_new` flag is **NOT** cleared when:
- The user opens the `/events` page
- The user reads the events list
- The user navigates away from `/events`

The `is_new` flag is cleared **only** when:
1. **The next MQTT delta batch arrives** from the transmitter — all entries are set to `false` first, then only the new batch entries get `true`. This happens when a new event occurs on the transmitter after the previous publish cycle.
2. **`clear_event_logs()` is called** — the whole cache is wiped.

So in practice: the `*` markers on `/events` will remain unchanged until a new event fires on the transmitter (causing a new MQTT publish), regardless of whether the user has read the page or not.

---

### 11.4 The subscribe/unsubscribe mechanism on `/events`

**File:** `espnowreceiver_2/lib/webserver/pages/event_logs_page_script.cpp`

```js
window.addEventListener('load', () => {
    fetch('/api/event_logs/subscribe', {method: 'POST'});  // ← on page open
    loadEvents();
});

window.addEventListener('beforeunload', () => {
    navigator.sendBeacon('/api/event_logs/unsubscribe', blob);  // ← on page close
});
```

**`/api/event_logs/subscribe`** → `api_event_logs_subscribe_handler`  
**File:** `espnowreceiver_2/lib/webserver/api/api_modular_handlers.cpp`

```cpp
esp_err_t api_event_logs_subscribe_handler(httpd_req_t *req) {
    MqttClient::incrementEventLogSubscribers();
    send_event_log_summary_request();   // ← triggers ESP-NOW summary request
    return ApiResponseUtils::send_success(req);
}
```

This does two things:
1. Increments the receiver-side subscriber count. If it was 0, sends `send_event_logs_control(true)` via ESP-NOW to notify the transmitter to start MQTT publishing.
2. Sends a `msg_event_log_summary_request` to the transmitter via ESP-NOW — requesting a fresh summary packet.

**On page close (`beforeunload`)** → `api_event_logs_unsubscribe_handler`:

```cpp
esp_err_t api_event_logs_unsubscribe_handler(httpd_req_t *req) {
    MqttClient::decrementEventLogSubscribers();  // ← if reaches 0, sends stop via ESP-NOW
    return ApiResponseUtils::send_success(req);
}
```

**Note:** The subscribe/unsubscribe mechanism controls the MQTT publishing on the transmitter (bandwidth saving) and triggers the summary. It does **not** mark events as viewed or clear `is_new`.

---

### 11.5 The "Event Logs" button on the dashboard (`/`) — how its count works

**File:** `espnowreceiver_2/lib/webserver/pages/dashboard_page_content.cpp`

The button lives in the "System Tools" grid:

```html
<a id='eventLogLink' href='/events' style='text-decoration: none;'>
    <div id='eventLogCard' ...>
        <span style='font-size: 24px;'>📋</span>
        <div style='margin-top: 10px; color: #FF9800; font-weight: bold;'>Event Logs</div>
        <div id='eventLogStatus' style='font-size: 12px; color: #888; margin-top: 5px;'>
            View system events
        </div>
    </div>
</a>
```

The `eventLogStatus` div is updated once on page load by `loadEventLogs()`:

**File:** `espnowreceiver_2/lib/webserver/pages/dashboard_page_script.cpp`

```js
// Load event logs ONCE on page load for card status.
window.addEventListener('load', function() {
    applyFormattedDeviceNames();
    loadEventLogs();
});
```

It is **not polled** — called only once when the dashboard loads.

---

### 11.6 Two-stage count retrieval in `loadEventLogs()`

`loadEventLogs()` uses a **preferred + fallback** strategy:

**Stage 1 — preferred: ESP-NOW summary (fast, accurate counts):**

```js
const summaryResponse = await fetch('/api/get_event_log_summary');
const summary = await summaryResponse.json();

if (summary && summary.success) {
    const total = Number(summary.total_historical || 0);
    const errors = Number(summary.error_historical || 0);
    const newTotal = Number(summary.new_since_last_report_total || 0);

    let statusText = formatCountLabel(total, 'event', 'events');
    if (errors > 0) statusText += ` | ${formatCountLabel(errors, 'error', 'errors')}`;
    if (newTotal > 0) statusText += ` | ${formatCountLabel(newTotal, 'new', 'new')}`;

    statusEl.textContent = statusText;   // e.g. "5 events | 2 errors | 1 new"
    statusEl.style.color = (errors > 0) ? '#ff6b35' : ...;
    return;   // ← stops here if summary was available
}
```

**Stage 2 — fallback: full event log fetch via MQTT cache or HTTP proxy:**

```js
const response = await fetch('/api/get_event_logs?limit=100');
const data = await response.json();
// Manually counts levels from data.events[]
// Shows "X events | Y errors | Z warnings"
```

The button text format is:
- `"5 events | 2 errors | 1 new"` — when ESP-NOW summary is available
- `"5 events | 2 errors | 1 warning"` — when fallback to cache/HTTP proxy

---

### 11.7 The ESP-NOW summary path — how `new_since_last_report_total` is computed

**File:** `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_routes.cpp`  
**Function:** `build_event_log_summary()`

```cpp
event_log_summary_t build_event_log_summary() {
    event_log_summary_t summary{};
    summary.type = msg_event_log_summary;
    summary.seq = ++g_event_log_summary_seq;
    summary.uptime_ms = millis();

    initialize_event_occurrence_snapshot_if_needed();  // First call only

    for (int i = 0; i < EVENT_NOF_EVENTS; ++i) {
        const EVENTS_STRUCT_TYPE* evt = get_event_pointer(...);

        if (evt->occurences > 0) {
            summary.total_historical++;
            if (is_error) summary.error_historical++;
        }

        const uint32_t previous = g_event_occurrences_reported[i];
        if (evt->occurences > previous) {
            const uint32_t delta = evt->occurences - previous;
            summary.new_since_last_report_total += delta;
            if (is_error) summary.new_since_last_report_error += delta;
        }

        // ← UPDATES baseline immediately
        g_event_occurrences_reported[i] = evt->occurences;
    }

    return summary;
}
```

**Critical semantics of `g_event_occurrences_reported[]`:**

- Initialised to current occurrence counts on **first call**.
- **Updated to current counts every time `build_event_log_summary()` is called.**
- Therefore `new_since_last_report_total` = occurrences that incremented **since the last time a summary was built**, not since the user last viewed the page.

---

### 11.8 What triggers `build_event_log_summary()` to run

`send_event_log_summary_to_receiver()` calls `build_event_log_summary()`. This is invoked when:

1. **Receiver sends `msg_event_logs_control` (subscribe=true)** → transmitter's `message_routes.cpp` handler calls `send_event_log_summary_to_receiver()` immediately.
2. **Receiver sends `msg_event_log_summary_request`** → transmitter sends fresh summary back.

On the receiver side, summary requests are triggered:
- By `api_event_logs_subscribe_handler` → `send_event_log_summary_request()` → called when user opens `/events` page.
- By `api_get_event_log_summary_handler` → `send_event_log_summary_request()` → called whenever the dashboard JS calls `/api/get_event_log_summary`.

The dashboard calls `/api/get_event_log_summary` **once on page load** (inside `loadEventLogs()`). This causes the transmitter to build a summary with the updated baseline — effectively **resetting `new_since_last_report_total` to 0** for the next caller.

---

### 11.9 Lifecycle summary — end-to-end

```
User loads / dashboard
  └─ JS calls loadEventLogs()
      └─ fetch('/api/get_event_log_summary')
          └─ api_get_event_log_summary_handler()
              └─ send_event_log_summary_request() [ESP-NOW, non-blocking]
                  └─ Transmitter: build_event_log_summary()
                      - Computes new_since_last_report from g_event_occurrences_reported delta
                      - Updates g_event_occurrences_reported[] to current values ← RESETS BASELINE
                      - Sends msg_event_log_summary to receiver via ESP-NOW
                  └─ Receiver: storeEventLogSummary()
                      - Caches total_historical, error_historical, new_since_last_report_total
  └─ Button shows: "5 events | 2 errors | 1 new"
  └─ (Next page load will show "5 events | 2 errors | 0 new" if no new events fired)

User navigates to /events
  └─ JS calls fetch('/api/event_logs/subscribe')
      └─ api_event_logs_subscribe_handler()
          └─ MqttClient::incrementEventLogSubscribers()
              └─ If first subscriber: send_event_logs_control(true) [ESP-NOW] → TX starts MQTT publishing
          └─ send_event_log_summary_request() [ESP-NOW] → same as above, resets baseline
  └─ JS calls loadEvents()
      └─ fetch('/api/get_event_logs?limit=100')
          └─ Returns cached entries with is_new per entry
          └─ JS renders * marker next to new entries

  [User reads the page — NOTHING changes is_new]

User leaves /events
  └─ JS calls fetch('/api/event_logs/unsubscribe')
      └─ api_event_logs_unsubscribe_handler()
          └─ MqttClient::decrementEventLogSubscribers()
              └─ If reaches 0: send_event_logs_control(false) [ESP-NOW] → TX stops MQTT publishing

Next MQTT delta arrives (some event fired on TX)
  └─ handleEventLogs() → TransmitterManager::storeEventLogs()
      └─ store_event_logs():
          - All existing entries: is_new = false   ← CLEARS * markers
          - Incoming batch entries: is_new = true   ← Sets * on changed events
```

---

### 11.10 Key findings

| Question | Answer |
|---|---|
| What makes an event "new" on `/events`? | It appeared in the most recent MQTT delta batch from the transmitter (`is_new = true` in receiver cache). |
| What clears "new" / removes the `*`? | The **next MQTT delta batch arriving** — all entries get `is_new = false` first, then only new batch gets `true`. NOT user viewing the page. |
| Is there a "mark as viewed" mechanism? | **No.** There is no user-interaction-driven clear of `is_new`. |
| What does the "Event Logs" button count show? | `total_historical` events + `error_historical` errors + `new_since_last_report_total` new — from ESP-NOW summary. Falls back to MQTT cache counts if no summary. |
| What updates the button count? | Loaded once on dashboard page load. **Not polled.** |
| What does "new" on the button mean? | Events whose occurrence count increased since the last time `build_event_log_summary()` was called on the transmitter. |
| What resets the "new" count on the button? | Every call to `build_event_log_summary()` on the transmitter (triggered by dashboard load or `/events` page open via `send_event_log_summary_request()`). After the first summary response, the baseline is updated, so next load shows 0 new unless another event fired. |
| Does visiting `/events` clear the button's "new" count? | **Indirectly yes.** Opening `/events` triggers `api_event_logs/subscribe` → `send_event_log_summary_request()` → transmitter builds new summary → updates `g_event_occurrences_reported[]` baseline. Next time dashboard loads, `new_since_last_report` will be 0 unless new events fired in between. |
| Does loading the dashboard clear the "new" count for next load? | **Yes.** The `/api/get_event_log_summary` call triggers `send_event_log_summary_request()` which resets the transmitter baseline. |

---

### 11.11 Implications and gaps

1. **`is_new` is MQTT-batch-driven, not user-driven.** The `*` marker on `/events` does not indicate "unread by this user" — it indicates "arrived in the last MQTT publish cycle." If the user opens the page between MQTT publishes, all `is_new` flags will be `false` (from the previous reset) and no `*` markers will show, even if the user has never seen the events.

2. **The button count "new" field resets too eagerly.** Because `build_event_log_summary()` updates `g_event_occurrences_reported[]` every time it runs, and it runs on every dashboard load and every `/events` subscribe, the "new" count effectively tracks "events that occurred since the last page load by any user," not "events unseen by this user." If two users load the dashboard within seconds, the second user will see `0 new` even though they never opened the event details.

3. **No persistence across reboots.** If the transmitter reboots, `g_event_occurrences_reported[]` resets to zero on first summary call. But `occurences` in events also resets. So both sides reset together and no false "new" counts result.

4. **The "new" count on the dashboard button is a snapshot, not a live feed.** The button is populated once on page load and then not updated unless the user refreshes the dashboard.

5. **Potential for stale "new" count on dashboard.** If events fire after the page loads, the button count won't update until the user refreshes. The count is therefore always "at least as old as the most recent page load."

---

### 11.12 If you wanted a true "user-viewed" mechanism

To genuinely track whether the user has seen the events, the following changes would be needed:

1. **Server-side "viewed" state** — add a `events_viewed_seq` or `events_last_viewed_ms` to the receiver's state (in NVS or RAM). Update it when the user visits `/events`.
2. **API endpoint to mark as viewed** — e.g. `POST /api/event_logs/mark_viewed`. Called on `/events` page load.
3. **Button shows "unseen" count** — compare `events_last_viewed_ms` against event `timestamp` to count only events that arrived after the last view.
4. **`is_new` flag driven by viewed state** — set to `true` if `event.timestamp > events_last_viewed_ms`.

This would be a significant behaviour change and would require careful design for multi-user scenarios (two browser tabs, two different users on same device IP, etc.).

**Currently, the system does not implement any of this.** The `is_new` and "new" badge are coarse, MQTT-batch-oriented indicators of recent event activity, not true per-user read tracking.