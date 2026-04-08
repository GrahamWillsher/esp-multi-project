# Event Logs Transport & Timestamp Decision Record (2026-04-05)

## Scope
Record the agreed architecture decision to move `/events` to **MQTT-only transport**, remove HTTP transport dependency for event-log rendering, and define the timestamp contract required to prevent `N/A`.

---

## Architecture Decision (Approved)

### Decision
`/events` will use **MQTT-derived event logs only**.

### Explicit policy
- Remove HTTP as a data path for event-log rendering.
- Remove HTTP event-log transport references from the implemented codebase as part of the MQTT migration.
- Do not use `source=transmitter|live|direct` for `/events` UI fetches.
- Receiver `/api/get_event_logs` should serve event rows from receiver MQTT event-log cache only.
- Any legacy transmitter event-log HTTP proxy code should be treated as transitional and removed when the MQTT-only implementation is completed.

---

## Target Data Flow (MQTT-only)

1. Browser opens `/events`.
2. Browser posts `/api/event_logs/subscribe` (start MQTT snapshot request).
3. Transmitter publishes the current event-log snapshot on `transmitter/BE/event_logs`.
4. Receiver MQTT client stores/merges into `TransmitterEventLogCache`.
5. Receiver detects snapshot completion.
6. Browser/receiver posts `/api/event_logs/unsubscribe` immediately after completion.
7. Browser requests receiver `/api/get_event_logs?limit=...` (no direct transmitter source override).
8. Receiver responds from MQTT cache.

Result: a single transport for `/events` rows, with receiver-owned snapshot lifecycle.

---

## Timestamp Contract for MQTT-only Path

### Required canonical fields
Every MQTT event row must carry:
- `timestamp_ms` (uint64): monotonic uptime timestamp in milliseconds from transmitter (`millis64` domain)
- `event_unix_ms` (uint64): transmitter UTC epoch milliseconds captured at event creation time
- `event_utc_offset_min` (int16): transmitter UTC offset in minutes captured at event creation time
- `event` or `type`: event identifier
- `level`, `count`, `data`, `message`

Optional but recommended:
- `timezone_name` or `timezone_revision`: useful for diagnostics and debug visibility, but not required for rendering if `event_unix_ms` and `event_utc_offset_min` are present

### Canonical units and width
- `timestamp_ms`: **uint64**, milliseconds
- `event_unix_ms`: **uint64**, Unix epoch milliseconds
- `event_utc_offset_min`: **int16**, signed minutes offset at the instant the event occurred
- No 32-bit truncation for event timestamps in authoritative event payloads.

---

## Impact on `Last Event` Rendering

With MQTT-only rows, `Last Event` should be computed using event-time wall-clock metadata, not current-time transport context.

### Practical answer: can we just use Unix time + offset?
Yes.

If the transmitter publishes both:
- `event_unix_ms`
- `event_utc_offset_min`

then the receiver can derive the displayed wall-clock time directly from those two fields, without implementing DST rules locally.

Practical rule:
- use event UTC epoch as the source of truth
- apply the UTC offset that was active when the event occurred
- format the resulting local wall-clock timestamp for display

This is the preferred practical approach because it keeps DST resolution on the transmitter side and keeps the receiver simple.

### Required rendering method
If `event_unix_ms` and `event_utc_offset_min` exist:
- Treat `event_unix_ms` as the authoritative UTC instant of the event.
- Apply `event_utc_offset_min` captured at the same instant.
- Format the resulting local wall-clock time.

This is the only approach that reliably handles DST for historical events.

### Why current-offset conversion is not sufficient
- Using only current heartbeat `utc_offset_min` is safe only when the event and the current display time are in the same offset regime.
- If an event is older than a DST boundary, applying the current offset can render the wrong local wall-clock time.
- Therefore, current heartbeat offset is useful for runtime status displays, but not sufficient as the sole source for historical event-time reconstruction.

### Transitional fallback (acceptable only until migration completes)
If only `timestamp_ms` exists:
- Convert using receiver-cached transmitter time context (`uptime_ms`, `unix_time`, `utc_offset_min`) from heartbeat.
- This may be acceptable for short-lived recent events, but it is not fully DST-safe for historical event rows.
- Reserve `N/A` for truly invalid/absent timestamp input, not for ordinary missing wall-clock enrichment.

Key consequence:
- MQTT-only transport removes transport mismatch for event rows.
- To fully eliminate short-uptime `N/A` and guarantee DST correctness, include both `event_unix_ms` and `event_utc_offset_min` in MQTT payloads.

---

## Structure Unification Requirements

### Must be uint64
- Transmitter event source timestamp storage
- MQTT event payload `timestamp_ms`
- MQTT event payload `event_unix_ms`
- Receiver event-log cache timestamp field
- UI parser/formatter numeric handling

### Must be int16
- MQTT event payload `event_utc_offset_min`
- Heartbeat `utc_offset_min`

### Allowed 32-bit (non-authoritative summary telemetry only)
- Event summary counters / sequence metrics where uptime is informational and not used for wall-clock reconstruction.

Policy:
- Any field used to render `Last Event` must be explicitly documented and must not rely on 32-bit truncation.

---

## Required Code-Path Changes (implementation checklist)

1. Receiver `/events` page
- Remove `source=transmitter` forcing from event fetch URL.
- Subscribe on page entry.
- Track snapshot completion: accumulate batches until `batch_index == batch_count - 1`.
- Unsubscribe immediately after snapshot completion is detected (i.e., after processing the final batch).
- Optionally resubscribe only on explicit user refresh/reload.

2. Receiver API (`/api/get_event_logs`)
- Make MQTT cache path authoritative for `/events`.
- Remove HTTP proxy selection from `/events` flow.
- Remove remaining event-log HTTP transport references once MQTT-only path is validated.

3. Transmitter MQTT publisher
- Standardize payload field to `timestamp_ms` (uint64) for each event.
- Add `event_unix_ms` (uint64) at event creation time.
- Add `event_utc_offset_min` (int16) at event creation time.
- Prefer publishing canonical field names only; retire alternate timestamp field names.
- Publish a bounded current snapshot when subscribed.
- **Add batch completion metadata:**
  - Pre-calculate `batch_count = ceil(total_unpublished_events / max_events_per_message)`
  - Include `batch_index` (0-based) and `batch_count` in JSON root
  - When `batch_index == batch_count - 1`, that is the final batch
  - Once final batch is sent, **stop publishing** for this subscription (do not continue delta publishing new events)
    - Do not force subscriber decrement at publish time; wait for receiver unsubscribe or TTL expiry cleanup

4. Receiver MQTT cache
- Keep merged `timestamp_ms` and `event_unix_ms` as uint64.
- Keep `event_utc_offset_min` as int16.

5. UI formatter
- Prefer `event_unix_ms` + `event_utc_offset_min` for wall-clock rendering.
- Use `timestamp_ms` only for relative-age fallback or diagnostics.

6. Cleanup requirement
- Remove event-log HTTP transport implementation and related selection logic from the codebase when MQTT-only rollout is complete.

7. Stop-message requirement
- Opening `/events` must send the start/subscribe control path.
- After the receiver has received the complete current snapshot, it must send the stop/unsubscribe control path.
- Leaving the page should still send unsubscribe as a defensive cleanup path.
- The transmitter should stop event-log publishing when subscriber count returns to zero.

---

## Subscription Lifecycle Findings

### Current behavior
- Receiver `/events` already sends a subscribe request on load.
- Receiver `/events` already sends an unsubscribe request on unload.
- Transmitter MQTT manager maintains a subscriber count.
- Transmitter publishes event logs periodically only while subscriber count is greater than zero.
- After the current unpublished batch has been sent, the subscription remains open; the transmitter simply has nothing further to publish until a new event occurs.

### Conclusion
- This describes current/legacy behavior before the receiver-owned snapshot lifecycle is fully implemented.
- Final design remains: receiver opens subscription, receives full snapshot, then unsubscribes immediately.
- Leaving `/events` still sends defensive unsubscribe if needed.

### Suggested hardening
- Keep TTL lease/timeout on transmitter side so stale sessions are auto-cleaned if unsubscribe is missed.

### Final lifecycle policy (authoritative)
- `/events` is a snapshot page, not a live tail.
- Receiver owns lifecycle: subscribe -> receive complete snapshot -> unsubscribe immediately.
- Transmitter does not force-close at final batch; it waits for receiver unsubscribe.
- If unsubscribe is missed due to crash/network loss, transmitter TTL cleanup closes the orphaned session.
- New events after snapshot completion are shown on next explicit refresh/resubscribe.

#### Important implementation consequence
For this model to work correctly, MQTT payloads need a clear way for the receiver to know it has received the **complete current snapshot**.

### Snapshot Completion Detection Strategies

#### Option 1: Batch Index + Batch Count (Recommended)
**Payload fields:**
- `batch_index` (uint16): 0-based current message index in this snapshot
- `batch_count` (uint16): total number of batches in this snapshot
- `snapshot_id` (optional uint64): unique session ID for tracing

**Logic:**
- Receiver accumulates batches until `batch_index == batch_count - 1`
- Last message identified when equality holds
- Unsubscribe after last message processed

**Pros:**
- Explicit, unambiguous termination signal
- Handles any batch size, any total event count
- Works with retained payloads naturally
- Receiver logic is simple: compare two integers
- Transmitter logic is straightforward: count total unpublished events upfront, divide into batches

**Cons:**
- Requires transmitter to count events before publishing (minor overhead)
- Adds 4 bytes to payload per message

**Implementation notes:**
- Transmitter calls `publish_event_logs()` which currently collects ~100 events at a time
- Calculate `batch_count = ceil(total_unpublished / max_per_batch)`
- Include `batch_index` and `batch_count` in JSON root or per-event
- Ensure `batch_count >= 1` whenever a snapshot message is published
- After last batch sent, wait for receiver unsubscribe; if unsubscribe is missed, TTL cleanup closes the session

#### Option 2: Event Count in Root
**Payload fields:**
- `event_count` (uint16): number of events in **this message**
- `total_remaining_count` (uint16): number of events still pending after this message

**Logic:**
- Receiver keeps running total of events received
- When `total_remaining_count == 0`, snapshot is complete

**Pros:**
- Similar simplicity to Option 1
- Slightly different cognitive model (remaining vs index)
- 4 bytes per message

**Cons:**
- Requires transmitter to recalculate remaining count per batch (small overhead)
- Less commonly understood pattern

#### Option 3: Snapshot Complete Boolean (Simple but less robust)
**Payload fields:**
- `snapshot_complete` (boolean): true on final batch only

**Logic:**
- Receiver sets flag when `snapshot_complete == true`
- Unsubscribe after next publish cycle or message receipt

**Pros:**
- Minimal payload overhead (1 byte)
- Works for simple cases

**Cons:**
- Ambiguous if a retained payload carries stale `snapshot_complete=true` from a prior snapshot
- Race condition: if subscriber reconnects while snapshot is still broadcasting, may see `snapshot_complete=false`, then later miss the `true` if update happened before reconnect
- Requires explicit state machine to avoid false positives on retained messages

**Not recommended** due to ambiguity with MQTT retained payloads and potential replay issues.

#### Option 4: Single Bounded Snapshot Message (Simplest if payload fits)
**Payload fields:**
- `events`: array of all current events
- `snapshot_id`: session ID
- No per-batch tracking

**Logic:**
- Transmitter collects all events, serializes to single message
- Receiver gets all events in one MQTT message
- Unsubscribe after first receive

**Pros:**
- Simplest receiver logic
- One-shot operation, no accumulation state needed
- No risk of partial snapshots

**Cons:**
- Payload size: current code serializes ~100 events as 6-8 KB; if event count is low (1-50), single message is practical, but at high counts may exceed typical MQTT broker limits or create latency
- Not suitable for very large event logs (>500 events)
- No streaming benefit

**Recommendation:**
- Use this if `typical_event_count` < 50 and `max_event_count` < 100
- Otherwise, use Option 1 (Batch Index + Batch Count)

#### Option 5: Event Counter with Sequence (Highest robustness)
**Payload fields:**
- `event_count` (uint16): total events in snapshot (sent in every batch)
- `batch_index` (uint16): current batch number
- `batch_count` (uint16): total batches
- `snapshot_id` (uint64): unique session ID

**Logic:**
- Receiver validates `batch_index < batch_count` before processing
- Rejects out-of-order batches
- When `batch_index == batch_count - 1`, process and unsubscribe
- Can cross-check via `event_count == batch_count * events_per_batch` (approximately)

**Pros:**
- Fully robust against packet loss (receiver can request retransmission)
- Can detect corruption or ordering issues
- Highest confidence in completeness

**Cons:**
- Most complex implementation
- Overkill if MQTT QoS is reliable
- Adds 8 bytes per message

---

### Recommended Choice: **Option 1 (Batch Index + Batch Count)**

**Rationale:**
- Current MQTT QoS is reliable, so complex sequence tracking is not needed.
- Explicit batch numbering is standard in streaming protocols.
- Minimal overhead (4 bytes per message).
- Clear, unambiguous termination for receiver.
- Works naturally with current `publish_event_logs()` structure.
- Receiver logic is trivial: store until `batch_index == batch_count - 1`.

**Transmitter-side change:**
```cpp
// In publish_event_logs():
const size_t total_events = ordered.size();  // Already collected
const size_t max_events = 100;  // Already capped
const size_t batch_count = (total_events + max_events - 1) / max_events;  // NEW: ceil divide

// Add to JSON root (or per-batch level):
doc["batch_index"] = batch_index;    // 0-based
doc["batch_count"] = batch_count;    // Total batches
```

**Receiver-side change:**
```cpp
// In receiver MQTT handler:
uint16_t batch_index = root["batch_index"];
uint16_t batch_count = root["batch_count"];
if (batch_count > 0 && batch_index == batch_count - 1) {
    // This is the last batch; safe to unsubscribe after processing
    trigger_unsubscribe_after_this_batch();
}
```

**Important clarification on post-completion behavior:**

The receiver **must unsubscribe after detecting snapshot completion** (when `batch_index == batch_count - 1`).

This is the core recommendation:
- Once snapshot is complete, unsubscribe
- Do NOT keep subscription open hoping to catch live events
- New events that occur after unsubscribe will not appear
- This is intentional: `/events` is a snapshot page, not a live monitor
- If the user wants to see newly occurred events, they explicitly refresh/reload the page

**Why this is the right choice:**
- Keeps transmitter overhead low (no background checks for a dead subscriber)
- Avoids ambiguity about whether the page shows historical or live data
- Aligns with product behavior: `/events` is a **report, not a monitor**

**Technical note on how new events are shown when user refreshes:**

Current behavior of delta-publishing:
- Event logs are **never deleted** from the transmitter event storage; they persist indefinitely
- Event logs are only removed when explicitly requested (e.g., via `/api/clear_event_logs`)
- Each event carries a `MQTTpublished` flag and an optional `new_event_marker` ('*') to distinguish states

When subscriber count drops to zero and a new subscription arrives:
- Transmitter publishes **ALL current events** (the complete event log at that moment)
- Each event that has occurred since the last snapshot will carry the '*' marker
- Events that were present in the previous snapshot will not carry the marker
- Receiver UI can differentiate "new events since last view" (marked) from "events you already saw" (unmarked)

Example flow:
1. First subscribe: transmitter sends Events A, B, C (no markers; these are the current set)
2. User views events A, B, C on page
3. User unsubscribes; new event D occurs
4. Second subscribe: transmitter sends Events A, B, C, D where D carries '*' (newly occurred)
5. Receiver shows all four, but can highlight/style D as "new" using the marker

This naturally handles the refresh/reload case:
- User explicitly refreshes `/events`
- New subscribe request made
- Transmitter publishes complete current event log
- New events since last view are marked with '*'
- No special logic needed; delta-publish mechanism across subscriptions just works

Without one of these completion mechanisms, the receiver cannot safely know when to unsubscribe.

---

## Subscription Resilience & Orphaned Session Handling

### Problem Statement
If a network failure, receiver crash, or browser unload occurs **after** the receiver sends `/api/event_logs/subscribe` but **before** sending `/api/event_logs/unsubscribe`, the subscription can remain open indefinitely on the transmitter.

**Consequences of orphaned subscriptions:**
- Transmitter continues publishing event logs to a dead subscriber
- Wasted bandwidth and MQTT broker resources
- Subscriber counter never decrements
- Eventual accumulation of stale subscriptions if pattern repeats
- Transmitter background load never returns to zero, reducing efficiency

### Industry Standard Solutions

#### Option 1: Subscription Lease Timeout (Recommended for Hardened Code)
**How it works:**
- Transmitter assigns each subscription a TTL (time-to-live) in seconds
- If subscriber does not send a keepalive/refresh message within TTL, subscription auto-closes
- Transmitter deletes the subscription and decrements subscriber count

**Typical values:**
- Short-lived snapshots: 30-60 seconds (page typically loads in <10s)
- Conservative with network variance: 2-5 minutes (allows for network glitches)
- Maximum safety: 10 minutes (detects truly dead subscribers)

**Pros:**
- Simple, deterministic, no state machine complexity
- Works even if receiver/browser disappears completely
- Scales well; no need to track individual keepalives per subscriber
- Industry standard in MQTT brokers and session managers
- Can be implemented with a simple `millis64()` timestamp per subscription

**Cons:**
- False positives: slow network could trigger timeout before batch completes
- Must be tuned for expected snapshot delivery time + network latency

#### Option 2: Keepalive / Heartbeat Mechanism
**How it works:**
- Receiver sends periodic "I'm alive" message while subscribed (e.g., `/api/event_logs/keepalive`)
- Transmitter resets TTL counter on each keepalive
- If no keepalive for TTL seconds, subscription auto-closes

**Typical keepalive interval:**
- Send keepalive every 5-15 seconds while subscribed
- Transmitter timeout 30-60 seconds (allows 2-3 missed keepalives)

**Pros:**
- Graceful: receiver controls whether subscription should stay open
- Fine-grained: can extend TTL for slow networks
- Useful if `/events` needs to support live monitoring (though current design rejects this)

**Cons:**
- More complex: adds extra API calls and state tracking
- More traffic to broker
- Not needed for snapshot-only model (overkill)

#### Option 3: MQTT Connection State Awareness (Intermediate)
**How it works:**
- Transmitter tracks which MQTT client_id sent each subscribe
- On MQTT connection loss/disconnect, transmitter auto-clears subscriptions for that client
- Cleaner than timeout because it's event-driven

**Pros:**
- Immediate cleanup on actual disconnection
- Works alongside timeout as defensive layer

**Cons:**
- MQTT protocol doesn't always notify server of ungraceful disconnects
- Network outages can leave subscriptions open until timeout
- Requires tracking per-subscription which client owns it

#### Option 4: Activity-Based Timeout (Hybrid)
**How it works:**
- Subscription auto-closes if no new batch requests have been made for N seconds
- Different from lease: measures receiver activity, not just existence

**Typical value:**
- Close after 30-60 seconds of inactivity

**Pros:**
- Detects idle subscriptions naturally
- Works well for snapshot model (snapshot completes, subscription goes idle)

**Cons:**
- Requires tracking last-activity timestamp per subscription
- Doesn't handle case where receiver stuck mid-snapshot

---

### Recommended Hardened Approach: Layered Defense

**Primary recommendation: Subscription Lease Timeout (TTL) Model is the preferred solution for this implementation.**

Rationale for TTL preference:
- Snapshot-based model (not live monitoring) requires deterministic, time-bounded cleanup
- TTL is simplest to implement and requires minimal state tracking
- Industry standard used by MQTT brokers, session managers, and IoT platforms
- Scales well and requires no per-subscriber keepalive traffic
- Can be augmented with connection awareness and activity tracking for defense-in-depth
- Aligns with receiver-owned lifecycle model: receiver responsible for unsubscribe, transmitter responsible for TTL safety net

Combine **Subscription Lease Timeout (primary)** + **Connection Awareness (secondary)** + **Activity Tracking (tertiary)**:

**Layer 1: Subscription Lease Timeout (Required - PREFERRED)**
- Every subscription gets a TTL of **60 seconds**
- Rationale: Snapshot delivery should complete in <10s under normal conditions; 60s allows for network variance and slow connections
- On every transmitter loop iteration, check: `if (time_now_ms - subscription_created_ms > TTL_MS) { close_subscription(); }`
- Transmitter decrements subscriber count and stops publishing for that subscription

**Layer 2: Connection State Awareness (Recommended)**
- When MQTT client disconnects (if broker notifies), mark subscriptions from that client for cleanup
- This catches ungraceful exits faster than timeout
- Does not replace timeout (timeout is safety net for broker notification failures)

**Layer 3: Activity Tracking (Optional, for Observability)**
- Track last-batch-received timestamp per subscription
- Log warning if subscription is idle >30s (suggests receiver may be hung)
- Can be used for metrics/alerts but not for automatic cleanup (too aggressive)

---

### Implementation Checklist for Hardened Subscription Management

**General Implementation Standards:**
- All code must follow industry best practices and standards
- All implementations must prioritize efficiency and minimize resource overhead
- **All timeout and timing configuration values MUST be stored in a dedicated shared configuration header: `event_log_config.h` (in common code used by both transmitter and receiver)**
- **NO magic numbers are permitted in source code** - all timing constants must be named, documented, and centrally managed
- `event_log_config.h` is the preferred and required location for event-log timeout constants

**Transmitter-side (mqtt_manager.cpp):**

1. **Add subscription metadata structure:**
   ```cpp
   struct EventLogSubscription {
       uint64_t created_time_ms;      // When subscription started
       uint64_t last_batch_sent_ms;   // When last batch was published
       uint32_t batch_count_sent;     // Number of batches sent (for diagnostics)
       bool is_complete;              // Snapshot delivery finished
   };
   
   std::map<uint32_t, EventLogSubscription> active_subscriptions_;
   uint32_t next_subscription_id_ = 1;  // Monotonic ID (never decremented)
   ```

2. **Set TTL on subscription creation:**
   ```cpp
   void increment_event_log_subscribers() {
       event_log_subscribers_++;
       // NEW: Track creation time for lease timeout
       EventLogSubscription sub;
       sub.created_time_ms = millis64();
       sub.last_batch_sent_ms = millis64();
       sub.batch_count_sent = 0;
       sub.is_complete = false;
       const uint32_t subscription_id = next_subscription_id_++;
       active_subscriptions_[subscription_id] = sub;
       LOG_INFO("MQTT", "Event log subscription %lu created, TTL: %llu ms", 
                static_cast<unsigned long>(subscription_id), config::EVENT_LOG_SUBSCRIPTION_TTL_MS);
   }
   ```

3. **Reap expired subscriptions in main loop:**
   ```cpp
   void reap_expired_subscriptions() {
       // Get TTL from config header (NOT a magic number)
       uint64_t now = millis64();
       
         std::vector<uint32_t> to_remove;
       for (auto& [sub_id, sub] : active_subscriptions_) {
           if (now - sub.created_time_ms > config::EVENT_LOG_SUBSCRIPTION_TTL_MS) {
               LOG_WARN("MQTT", "Subscription %lu expired (TTL exceeded after %llu ms)", 
                        static_cast<unsigned long>(sub_id), config::EVENT_LOG_SUBSCRIPTION_TTL_MS);
               to_remove.push_back(sub_id);
           }
       }
       
       for (auto sub_id : to_remove) {
           active_subscriptions_.erase(sub_id);
           decrement_event_log_subscribers();
       }
   }
   ```

4. **Call reap in publish loop:**
   ```cpp
   bool publish_event_logs() {
       reap_expired_subscriptions();  // NEW: cleanup expired subscriptions first
       
       if (event_log_subscribers_ <= 0) return true;
       // ... rest of publish logic ...
   }
   ```

5. **Update batch tracking on send:**
   ```cpp
   // In publish_event_logs(), after publishing:
   for (auto& [sub_id, sub] : active_subscriptions_) {
       sub.last_batch_sent_ms = millis64();
       sub.batch_count_sent++;
       if (batch_index == batch_count - 1) {
           sub.is_complete = true;
       }
   }
   ```

**Receiver-side (event-log API handlers):**

1. **Subscribe creates a session with timeout expectation:**
   ```cpp
   // In /api/event_logs/subscribe:
   receiver.start_event_log_snapshot();  // Subscribe to MQTT
   receiver.set_snapshot_timeout(config::EVENT_LOG_SNAPSHOT_TIMEOUT_MS);  // From config
   return response({ "status": "subscribed", 
                     "max_wait_ms": config::EVENT_LOG_SNAPSHOT_TIMEOUT_MS });
   ```

2. **Unsubscribe on snapshot completion (primary path):**
   ```cpp
   // When batch_index == batch_count - 1:
   receiver.queue_event_log_unsubscribe();
   ```

3. **Optional: Defensive timeout handler (fallback):**
   ```cpp
   // If snapshot takes too long, receiver gives up and unsubscribes defensively
   // Timeout value from config (NOT a magic number)
   if (time_since_snapshot_start_ms > config::EVENT_LOG_SNAPSHOT_TIMEOUT_MS) {
       LOG_WARN("Event logs", "Snapshot timeout (>%llu ms), forcing unsubscribe", 
                config::EVENT_LOG_SNAPSHOT_TIMEOUT_MS);
       receiver.force_event_log_unsubscribe();
   }
   ```

**Configuration File Requirements (`event_log_config.h` in shared/common code):**
   ```cpp
   // All timing constants must be centrally defined and NOT hardcoded
   namespace config {
       // Event log subscription TTL for orphaned session cleanup (primary defense)
       constexpr uint64_t EVENT_LOG_SUBSCRIPTION_TTL_MS = 60000;      // 60 seconds
       
       // Receiver-side snapshot completion timeout (defensive cleanup)
       constexpr uint64_t EVENT_LOG_SNAPSHOT_TIMEOUT_MS = 60000;      // 60 seconds
       
       // Activity monitoring threshold (optional, for observability)
       constexpr uint64_t EVENT_LOG_IDLE_WARNING_THRESHOLD_MS = 30000; // 30 seconds
       
       // Maximum batch size for event publishing
       constexpr size_t EVENT_LOG_MAX_BATCH_SIZE = 100;  // Events per MQTT message
       
       // Maximum events in persistent storage (before cleanup requested)
       constexpr size_t EVENT_LOG_MAX_STORED_EVENTS = 10000;
   }
   ```

---

### Configuration Recommendations

**For typical LAN with low latency (<50ms):**
- Subscription TTL: **30 seconds**
- Rationale: Snapshot should complete in <5s; 30s allows 5x margin for network hiccups

**For remote/high-latency networks:**
- Subscription TTL: **120 seconds**
- Rationale: Snapshot could stall due to bandwidth constraints; 2-minute buffer is safe

**For hardened production systems:**
- Subscription TTL: **60 seconds** (balanced)
- Connection awareness: **enabled**
- Activity logging: **enabled**
- Metrics: Monitor `subscriptions_reaped_due_to_timeout` counter

---

### Operational Observability

**Metrics to track (for debugging and alerts):**
- `event_log_subscribers_current`: Current active subscription count
- `subscriptions_reaped_total`: Lifetime count of timeout-based cleanups
- `subscriptions_reaped_by_disconnect`: Count of connection-aware cleanups
- `snapshot_completion_time_ms`: Distribution of snapshot delivery times (identify slow clients)
- `snapshot_timeout_count`: Count of subscriptions that hit TTL before completion

**Log messages to emit:**
- INFO: "Event log subscription created, TTL: 60s"
- INFO: "Event log snapshot complete, batch X/Y"
- WARN: "Subscription expired (TTL exceeded), forcing cleanup"
- WARN: "Receiver snapshot timeout, unsubscribing defensively"
- ERROR: (if subscriber count never returns to zero after timeout—indicates bug)

### Current findings
- MQTT task is configured as low priority.
- Runtime startup launches MQTT with low/background priority.
- Event publishing is periodic and subscriber-gated rather than continuous.

### Required policy
- Event-log MQTT transport must remain lower priority than the battery emulator, safety, ESP-NOW control, and core transmission functions.
- Event-log transport must never interfere with the transmitter's primary job, which is battery emulation and core telemetry/control.

### Required safeguards
- Keep MQTT task at low priority.
- Keep publish cadence bounded.
- Publish only when subscribed.
- Keep payloads bounded and per-cycle event count capped.
- If MQTT broker is slow or unavailable, skip/defer event-log publication rather than blocking primary functions.

### Recommended additional safeguard
- If a snapshot-on-subscribe path is added, it must be rate-limited and capped so it cannot starve the main transmitter work.

---

## Risk Notes

- MQTT is delta-oriented; cold-start clients need retained payload strategy or a bootstrap snapshot to avoid empty table after page load.
- If only uptime timestamps are provided, heartbeat time context availability still matters for wall-clock formatting.
- Browser unload is not guaranteed in all failure cases, so stale subscriptions are possible without timeout/lease handling.
- Large or frequent snapshot bursts could interfere with background bandwidth or heap if not bounded.

Mitigation:
- Keep retained MQTT payload for latest event set or publish periodic snapshot events.
- Add `event_unix_ms` and `event_utc_offset_min` to make rendering independent from heartbeat timing races and DST boundary ambiguity.
- Add transmitter-side subscription lease expiry.
- Keep MQTT event transport low priority and bounded.

---

## DST / Wall-Clock Conversion Findings

### What the transmitter already provides
- Heartbeat already carries `unix_time`, `utc_offset_min`, and `time_source`.
- `unix_time` is UTC epoch time.
- `utc_offset_min` represents the transmitter's currently active local offset.

### What is still missing for historical event correctness
- Historical event rows need the offset that applied **when the event occurred**, not merely the current offset.
- Without per-event offset capture, an event before a DST change can be displayed using the post-change offset, which is wrong.

### Recommended event-time capture rule
When an event is created on the transmitter, capture all of the following together:
- `timestamp_ms = millis64()`
- `event_unix_ms = gettimeofday()/epoch at event time`
- `event_utc_offset_min = currently active UTC offset at event time`

This creates a self-contained event-time record that can always be rendered into wall-clock time on the receiver.

### Practical recommendation
- Do not make the receiver calculate DST rules.
- Let the transmitter resolve local-time offset and publish the offset active at event creation time.
- The receiver should simply combine event Unix epoch + event offset to render wall-clock time.

---

## Final Statement
This project is now aligned to a single `/events` transport: **MQTT only**.

Timestamp policy is also aligned:
- authoritative event timestamps are **uint64**
- canonical field is `timestamp_ms`
- authoritative wall-clock conversion fields are `event_unix_ms` and `event_utc_offset_min`
- HTTP event-log transport references should be removed from the codebase as part of implementation completion.

Operational policy is also aligned:
- entering `/events` starts a receiver-owned MQTT snapshot request
- once the complete current snapshot has been received, the receiver stops it
- leaving `/events` still sends defensive cleanup unsubscribe if needed
- MQTT event-log transport must remain lower priority than the transmitter's primary battery-emulator function and must never interfere with it.
