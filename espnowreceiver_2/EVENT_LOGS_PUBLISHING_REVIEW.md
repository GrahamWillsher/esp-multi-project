# Event Logs Publishing Review (Transmitter)

## Purpose
Review how event logs are stored and updated in the current emulator code, compare to intended behavior, and propose a publishing strategy that avoids repeatedly sending unchanged data and avoids sending data when the receiver hasn’t requested it.

## Findings (Current Code)

### 1) Event storage is a fixed per‑event table (not a rolling log)
- The event system stores one entry per event type in a fixed array sized by `EVENT_NOF_EVENTS`.
- Each entry tracks `timestamp`, `data`, `occurences`, `level`, `state`, and an `MQTTpublished` flag.
- This means the “log” is a snapshot of the latest state for each event type, not a continuously growing history buffer.
- When an event is set again, its `timestamp` and `occurences` change; otherwise entries remain unchanged.

Source: [ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.h](ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.h)

### 2) Timestamps are available and updated on event changes
- When an event is set, its `timestamp` is updated using `millis64()`.
- This gives a reliable “last updated” value per event for change detection and sorting.

Source: [ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.cpp](ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.cpp)

### 3) There is an unused “publish once” mechanism baked in
- `MQTTpublished` is stored per event and is reset to `false` when an event is set.
- A helper `set_event_MQTTpublished()` exists but is not used elsewhere.
- This strongly suggests the original design intended to publish new/changed events only, then mark them as published.

Source: [ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.h](ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.h)

### 4) Current transmitter publishing always sends full snapshots
- The transmitter’s MQTT publisher builds a full JSON snapshot of event logs and sends it periodically.
- This results in repeated transmission of identical data when nothing has changed.

Source: [ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp](ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp)

### 5) The receiver already uses “subscribe on demand” for /cellmonitor
- The receiver includes a subscription gating mechanism to avoid continuous cell data when no clients are connected.
- This pattern is a good match for event logs as well (publish only when requested).

Source: [espnowreceiver_2/src/mqtt/mqtt_client.cpp](espnowreceiver_2/src/mqtt/mqtt_client.cpp)

## Implications
- Because the event store is a fixed per‑event table, sending full snapshots continuously provides no new information unless an event changes.
- Timestamps already exist to detect changes per event and to sort events by recency.
- The presence of `MQTTpublished` indicates the original intent was to publish only once per event change.

## Recommended Publishing Strategy (Suggested Design)

### Goal
1) Avoid publishing unchanged data.
2) Avoid publishing unless the receiver requests data (like /cellmonitor behavior).

### Option A (Recommended): On‑Demand Snapshot with Change Detection
**Summary:** Publish full snapshots only when explicitly requested or when the snapshot has changed since the last request.

**How it works:**
1. Add a transmitter MQTT request topic, e.g. `transmitter/BE/event_logs/request`.
2. When the receiver opens `/events`, it publishes a request message (optionally retained for re‑connect).
3. Transmitter responds by publishing a snapshot to `transmitter/BE/event_logs`.
4. The transmitter caches a lightweight hash/CRC of the last published snapshot (or a “dirty” flag). It only re‑publishes if:
   - A request is received **and** the snapshot has changed since last send, or
   - A forced request arrives (e.g. with a `force:true` flag).

**Change detection options:**
- **Dirty flag:** set `events_dirty = true` in `set_event`, `clear_event`, and `reset_all_events`, then clear after publish.
- **Snapshot hash:** compute a hash of `(timestamp, state, occurences, data)` for each event entry and compare to last hash.

**Why this fits the codebase:**
- Timestamps already exist, so change detection is reliable.
- The receiver already uses on‑demand subscription gating for cell data.
- No need for continuous periodic publishing once logs are stable.

### Option B: Incremental (“delta”) Publishing
**Summary:** Publish only the events that changed since last publish.

**How it works:**
- Use the `MQTTpublished` flag per event (reset in `set_event`), and publish only those with `MQTTpublished == false`.
- After publishing, call `set_event_MQTTpublished()` for each emitted event.

**Pros:** Very small payloads when only a few events change.

**Cons:** The receiver must already have the full snapshot. New receivers will miss old events unless a separate snapshot request is performed. This approach is best as a supplement to Option A (snapshot on request + deltas thereafter).

### Option C: Periodic Snapshot Only When Changed
**Summary:** Keep the current periodic timer (every 5s), but only publish if data changed (dirty flag or hash).

**Pros:** Minimal changes and no new request topic.

**Cons:** Still publishes even when no receiver is listening; does not satisfy the “no need to send if not required” requirement.

## Recommended Implementation Plan (Minimal Changes)

1) **Add request/response topic**
- Receiver publishes to `transmitter/BE/event_logs/request` when `/events` is opened and optionally every N seconds while the page remains open.
- Transmitter subscribes to the request topic and triggers a response.

2) **Add `events_dirty` or snapshot hash**
- Set dirty in `set_event`, `clear_event`, `reset_all_events`.
- Publish snapshot only if dirty OR request includes `force:true`.
- Clear dirty after successful publish.

3) **Keep sorting by timestamp**
- Continue sorting events by `timestamp` so that the newest events appear first.

4) **Payload additions (optional but useful)**
- Include `state` and `occurences` for each event in the JSON so UI can show “active vs inactive” and “count”.
- Include `snapshot_ts` or `snapshot_hash` in the root of the JSON for easy client‑side change detection.

## Timestamps in the Original Event Logs
- Each event stores a `timestamp` updated when the event is set, using `millis64()`.
- This is ideal for:
  - Sorting events by recency.
  - Detecting changes (if timestamp or occurences changes, the entry changed).

Source: [ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.cpp](ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.cpp)

## Comparison with Original Battery-Emulator Implementation

### 1) Event Storage Structure – IDENTICAL
- **Transmitter:** Fixed array of ~170 event types, each with `timestamp`, `data`, `occurences`, `level`, `state`, and **`MQTTpublished` flag**.
- **Battery-Emulator:** Fixed array of ~170 event types, each with identical structure and **`MQTTpublished` flag**.
- **Verdict:** Structurally identical.

Source comparisons:
- Transmitter: [ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.h](ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.h#L160-L167)
- Battery-Emulator: [Battery-Emulator-9.2.4/Software/src/devboard/utils/events.h](Battery-Emulator-9.2.4/Battery-Emulator-9.2.4/Software/src/devboard/utils/events.h#L154-L161)

### 2) HTTP Web Delivery (HTML Page)
- **Battery-Emulator:**
  - Renders `/events` endpoint as a full **HTML page** (no auto-refresh, no SSE).
  - Uses `events_processor()` function to generate HTML row-by-row from the fixed table.
  - Each HTTP GET request is stateless—always returns full snapshot.
  - No polling, no real-time updates, no client awareness.

Source: [Battery-Emulator-9.2.4/Software/src/devboard/webserver/events_html.cpp](Battery-Emulator-9.2.4/Battery-Emulator-9.2.4/Software/src/devboard/webserver/events_html.cpp)

- **Transmitter:**
  - Not applicable (no webserver in transmitter; only MQTT publishing).

### 3) MQTT Publishing – **KEY DIFFERENCE**
- **Battery-Emulator (What it SHOULD be doing):**
  - Iterates over all events and checks `if (event_pointer->occurences > 0 && !event_pointer->MQTTpublished)`.
  - **Only publishes events NOT yet published** (delta mode).
  - After publishing each event, calls `set_event_MQTTpublished(event_handle)` to mark it as published.
  - **Result:** Each new or changed event is published exactly once.

Source: [Battery-Emulator-9.2.4/Software/src/devboard/mqtt/mqtt.cpp (lines 495–530)](Battery-Emulator-9.2.4/Battery-Emulator-9.2.4/Software/src/devboard/mqtt/mqtt.cpp#L495)

```cpp
// Battery-Emulator's publish_events() function
for (int i = 0; i < EVENT_NOF_EVENTS; i++) {
    event_pointer = get_event_pointer((EVENTS_ENUM_TYPE)i);
    if (event_pointer->occurences > 0 && !event_pointer->MQTTpublished) {  // <-- ONLY unpublished
        order_events.push_back({...});
    }
}
// Sort and iterate
for (const auto& event : order_events) {
    // ... build JSON ...
    mqtt_publish(...);
    set_event_MQTTpublished(event_handle);  // <-- Mark as published
}
```

- **Transmitter (Current Implementation):**
  - Builds a full JSON snapshot of **all active events** (no filtering).
  - Publishes the entire snapshot on a periodic 5-second timer.
  - **Does NOT use the `MQTTpublished` flag** even though it's available.
  - **Result:** Duplicate data; the same events are re-published every 5 seconds even if nothing changed.

Source: [ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp (lines 263–290)](ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp#L263)

### 4) How Amended Data is Handled with Active Connections

**Battery-Emulator:**
- No explicit "client subscription" mechanism. Each HTTP request or MQTT pub-sub is independent.
- When an event changes, the `MQTTpublished` flag is reset to `false` automatically in `set_event()`.
- Next MQTT publish cycle (~5s), only the modified event is sent (thanks to the flag check).
- **No request/response topic.** Just periodic publishing of deltas.

Source: [Battery-Emulator-9.2.4/Software/src/devboard/utils/events.cpp (line 213)](Battery-Emulator-9.2.4/Battery-Emulator-9.2.4/Software/src/devboard/utils/events.cpp#L213)

**Transmitter:**
- No subscription tracking; always publishes the full snapshot regardless of whether anyone is listening.
- When an event changes, the transmitter's `publish_event_logs()` still publishes the full snapshot on the next 5-second cycle.
- **Problem:** Receiver doesn't request data, so no on-demand behavior like `/cellmonitor` uses.

### 5) Design Philosophy Comparison

| Aspect                       | Battery-Emulator (Original)           | Transmitter (Current)                    |
|------------------------------|---------------------------------------|------------------------------------------|
| **Event table structure**    | Fixed array per event type ✓          | Fixed array per event type ✓            |
| **Timestamps available?**    | Yes ✓                                 | Yes ✓                                   |
| **MQTTpublished flag used?** | **YES** (publish-on-change) ✓         | **NO** (always full snapshot) ✗          |
| **HTTP delivery**            | Stateless full page                   | N/A                                     |
| **MQTT delivery**            | Delta publishing (only new events) ✓  | Full snapshots (repeated) ✗             |
| **On-demand behavior**       | No explicit request topic             | No subscription/request topic ✗         |
| **Bandwidth efficiency**     | High (deltas only) ✓                  | Low (full snapshots repeated) ✗         |
| **Client awareness**         | No                                    | No ✗                                    |

## Summary
- The transmitter uses the **same event structure and timestamps** as the battery-emulator.
- However, the transmitter **does not implement** the battery-emulator's delta-publishing mechanism.
- The battery-emulator **does use `MQTTpublished`** to publish only changed/new events, but the transmitter **ignores this flag entirely** and publishes full snapshots every 5 seconds.
- The transmitter lacks the original battery-emulator's efficient "publish on change" behavior and should adopt it to reduce bandwidth waste.

## Recommended Implementation Plan (On-Demand + Change Detection)

**Final Design (Aligns with /cellmonitor behavior):**

Since event logs are **only displayed when the user opens `/events` on the receiver**, we can adopt an on-demand subscription model **combined with change detection**, just like `/cellmonitor` does with cell data.

### Architecture:
1. **Receiver behavior:**
   - When user opens `/events` page, receiver sends a **subscription request** to transmitter on topic `transmitter/BE/event_logs/request`.
   - Receiver subscribes to `transmitter/BE/event_logs` and caches incoming events.
   - When user **leaves `/events` page**, receiver **unsubscribes** (via `/cellmonitor`-style gating).

2. **Transmitter behavior:**
   - Tracks if any clients are listening (subscription count).
   - **Only publishes** when:
     - At least one client is subscribed, **AND**
     - An event has changed (detected via `MQTTpublished` flag).
   - Uses the delta-publishing approach (check `!event_pointer->MQTTpublished` before including).
   - Stops publishing (or publishes very infrequently) when no clients are subscribed.

### Code Changes (Minimal):

**Transmitter side:**
```cpp
// Add subscription tracking (similar to cell_data model)
static int event_log_subscribers = 0;

// Modify publish_event_logs() to check flag:
if (event_ptr && event_ptr->occurences > 0 && !event_ptr->MQTTpublished) {  // <-- ADD FLAG CHECK
    // ... add to JSON ...
    set_event_MQTTpublished(item.event_handle);  // <-- MARK PUBLISHED
}

// Only call publish_event_logs() if subscribers > 0
```

**Receiver side:**
- Leverage existing `cell_data_subscribers_` pattern from [espnowreceiver_2/src/mqtt/mqtt_client.cpp](espnowreceiver_2/src/mqtt/mqtt_client.cpp).
- When `/events` page is opened: send request message, increment subscriber count, start SSE stream.
- When `/events` page is closed: decrement subscriber count, unsubscribe from MQTT if count = 0.

### Benefits:

- ✓ **No redundant transmission:** Data only sent while page is open and only when changed.
- ✓ **Bandwidth efficient:** Matches the proven `/cellmonitor` + cell_data pattern.
- ✓ **Minimal code:** Reuse existing subscription tracking framework.
- ✓ **Handles amended data:** Flag automatically resets when event changes; next cycle publishes only the changed event.
- ✓ **Scalable:** Works with multiple simultaneous clients via subscription count.
- ✓ **Consistent UX:** Event logs vanish from transmitter MQTT when no one is watching, just like cell data.

### How Amended Data is Handled:

1. User views `/events` → receiver subscribes → transmitter starts publishing (only changed events).
2. Event changes on transmitter → `MQTTpublished` flag resets to `false`.
3. Next 5-second cycle → transmitter publishes the changed event (because flag is `false`).
4. Transmitter marks it published → flag set to `true`.
5. User leaves `/events` → receiver unsubscribes → transmitter stops publishing.
6. If event changes while user is away → flag remains `false`, ready for next time user opens page.

### Summary:
This design treats event logs exactly like the receiver already treats cell data—**on-demand subscription with change detection**. The transmitter only publishes when someone is listening, and only publishes what has changed. When the page is closed, transmission stops, conserving bandwidth.
