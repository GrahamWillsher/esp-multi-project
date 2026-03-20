# Transient Queue Investigation (Transmitter)

**Date:** 2026-03-17  
**Project:** ESPnowtransmitter2  
**Scope:** Investigate `"Transient queue full"` warning, determine root cause, and propose improvements.

---

## Executive Summary

Your suspicion is correct: in the current implementation, transient entries can effectively remain in the ring buffer forever unless explicitly marked `acked`.

### Root cause in one line
The transmitter marks transient entries as `sent`, but cleanup only removes `acked` entries, and there is no active path that calls `mark_transient_acked()` for transient telemetry.

---

## What I found

## 1) Where the warning comes from

The warning is emitted in:
- `EnhancedCache::add_transient()` in [src/espnow/enhanced_cache.cpp](src/espnow/enhanced_cache.cpp)

When `transient_count_ >= TRANSIENT_QUEUE_SIZE`, it logs:
- `Transient queue full ... oldest entry dropped`

The queue size is currently:
- `TRANSIENT_QUEUE_SIZE = 250` in [src/espnow/enhanced_cache.h](src/espnow/enhanced_cache.h)

---

## 2) Producer/consumer behavior

### Producer
- Telemetry is added in [src/espnow/data_sender.cpp](src/espnow/data_sender.cpp)
- Interval is `timing::ESPNOW_SEND_INTERVAL_MS` (currently 2000 ms)

### Consumer
- Transmission loop runs every 50 ms in [src/espnow/transmission_task.h](src/espnow/transmission_task.h) / [src/espnow/transmission_task.cpp](src/espnow/transmission_task.cpp)
- On send success it calls `mark_transient_sent(seq)`

### Cleanup
- `cleanup_acked_transient()` removes only entries with `acked == true`
- This cleanup runs every transmission loop iteration

---

## 3) Critical gap

`mark_transient_acked(seq)` exists, but I found no call path that uses it for transient telemetry.

So the lifecycle is currently:
1. `add_transient()`
2. `mark_transient_sent()`
3. **never acked**
4. never removed by `cleanup_acked_transient()`

Result: count trends upward until full, then oldest entry is dropped on each new insert.

---

## 4) Why this shows up after some runtime

With current defaults:
- enqueue rate: 1 item / 2 seconds
- queue size: 250

Time to first full queue:
- `250 * 2s = 500s` (~8.3 minutes)

That aligns with seeing the warning after a while rather than immediately.

---

## 5) Related but separate “queue full” message

There is also a different queue-full error in network config handlers:
- [src/espnow/network_config_handlers.cpp](src/espnow/network_config_handlers.cpp)

That is a processing queue for config updates, not this transient telemetry ring buffer.

---

## Impact assessment

- **Correctness:** Old telemetry is dropped once full (by design), but this is avoidable churn.
- **Observability:** Logs become noisy and can mask real issues.
- **Statistics quality:** `transient_acked` and related metrics are misleading if transient ack is never used.
- **Memory:** Bounded (ring buffer), so no leak; but retained stale entries waste capacity.

---

## Improvement options

## Option A — Increase buffer size (quick but weak)

Pros:
- Simple
- Delays warning

Cons:
- Does not fix lifecycle bug
- Just pushes failure later

Verdict: **Not sufficient alone**.

---

## Option B — Add TTL cleanup for sent entries (recommended immediate fix)

Keep current structure but remove stale sent entries automatically after a timeout, even if never acked.

Example policy:
- if `sent == true` and `age > SENT_TTL_MS`, treat as complete and purge from head
- keep `acked` purge behavior too

Pros:
- Fixes unbounded occupancy growth
- No protocol changes
- Fast to implement

Cons:
- Not true end-to-end delivery confirmation

Verdict: **Best short-term fix**.

---

## Option C — Switch transient semantics to “queue-until-send”

If transient queue purpose is smoothing bursts/offline buffering (not guaranteed delivery), remove entry once send succeeds.

Pros:
- Simplest lifecycle
- Queue remains healthy
- Matches current reality (no per-telemetry ack)

Cons:
- Loses “post-send retention” unless separately needed

Verdict: **Very good pragmatic fix**, likely cleaner than pretending there is ACK today.

---

## Option D — Implement real telemetry ACK protocol (best long-term reliability)

Add explicit ACK from receiver for each telemetry sequence:
- include sequence on wire payload
- receiver emits data ACK
- transmitter calls `mark_transient_acked(seq)`

Pros:
- True delivery tracking
- Current cache model fully justified

Cons:
- Protocol change (TX + RX)
- More complexity and traffic

Verdict: **Best if guaranteed delivery is required**; otherwise overkill.

---

## Recommended plan

## Phase 1 (now)

1. Add sent-entry TTL cleanup (`SENT_TTL_MS = 60 s`)
2. Keep ack cleanup logic for compatibility
3. Throttle “queue full” warning rate
4. Add explicit stats buckets:
   - removed_by_ack
   - removed_by_sent_ttl
   - dropped_on_overflow

## Phase 2 (next)

Decide semantic intent:
- **A)** transient = queue-until-send (then remove), or
- **B)** transient = queue-until-ack (implement real data ACK)

## Phase 3 (tuning)

After Phase 1/2:
- tune queue size based on measured peak occupancy
- tune send interval vs. queue target occupancy

---

## Acceptance criteria for fix

1. With active connection for 20+ minutes, no repeated transient queue-full warnings.
2. `transient_current` remains bounded well below 386 in steady state.
3. On temporary disconnect/reconnect, queue recovers without long-term saturation.
4. Stats clearly show why entries are removed (ack vs TTL vs overflow).

---

## Final recommendation

Implement **Option B immediately** (TTL cleanup for sent transient entries), and then choose between:
- **Option C** if best-effort telemetry is acceptable (likely enough here), or
- **Option D** if strict delivery semantics are needed.

Increasing ring size alone should be treated as optional tuning, not the primary fix.

---

## Concrete recommendations for Option A and Option B

## Option A — Suggested new ring size

### Selected value: increase from 250 to 386 entries

Why 386:
- Current production rate is 1 telemetry sample every 2 seconds.
- `386 * 2s = 772s` of buffering.
- That is about **12.9 minutes** of retained telemetry before overflow.
- It gives significantly more headroom than 250 while keeping memory growth moderate.
- It remains small enough to avoid affecting battery-emulator critical paths.

### Why not jump straight to 1024?
- `1024` would buffer about **34 minutes** at the current rate, which is probably more than needed.
- It increases retained stale data and memory footprint without fixing lifecycle behavior by itself.
- Since the data is not process-critical, the goal should be **bounded smoothing**, not long-term archival.

### Practical conclusion
- **Selected ring size:** `386`
- Only consider `512` or `1024` later if field data shows sustained disconnect windows that exceed current headroom.

---

## Option B — Proposed TTL cleanup behavior

### Recommendation: clean up expired entries in the background transmission task, not in producer code

This is the most important constraint:
- do **not** add extra cleanup work to `DataSender::send_battery_data()`
- do **not** block or extend the path that reads live battery data

Cleanup should stay in the low-priority background transmission/cache-management path.

### Recommended cleanup rule
Remove entries from the **head of the FIFO only** while either of these is true:

1. `acked == true`
2. `sent == true && (now - timestamp) > TRANSIENT_SENT_TTL_MS`

This preserves FIFO behavior and avoids scanning/removing arbitrary middle entries.

### Why head-only cleanup works
- entries are appended in timestamp order
- once the oldest sent entry is expired, later sent entries will expire soon after
- removing from the front keeps mutex hold time low and logic simple
- it avoids compaction or shifting work

### Recommended TTL value
- **Selected value:** `60 seconds`

Why 60s is a good fit here:
- it is long compared to the 2s data interval
- it allows several resend opportunities if you later add resend logic
- it is still short enough to stop stale telemetry sitting around indefinitely
- it is completely acceptable for non-process-critical ESP-NOW telemetry

### When cleanup should run
- run TTL cleanup in the existing background transmission task
- **not every 50ms loop**
- instead run it every **10 seconds** (or every **200 task iterations** at a 50ms task loop)

This keeps overhead tiny and avoids unnecessary mutex churn.

### Extra diagnostic counters to add
- `transient_removed_acked`
- `transient_removed_expired`
- `transient_dropped_overflow`

That will let you tell whether the system is healthy or simply masking a deeper problem.

---

## At least 3 practical low-impact solutions

## Solution 1 — Moderate resize + simple sent TTL cleanup (**recommended**)

### Changes
- increase ring size from `250` to `386`
- add `TRANSIENT_SENT_TTL_MS = 60000`
- in background task, once every 10 seconds, remove head entries while:
   - `acked`, or
   - `sent && expired`

### Pros
- minimal code change
- very low runtime cost
- no protocol changes
- does not interfere with battery emulator critical code
- enough buffering for ~12.9 minutes at the current 2s telemetry rate

### Cons
- still not true end-to-end delivery confirmation

### Best use
- best overall fit for your current design goals

---

## Solution 2 — Keep best-effort semantics explicit: remove on send success + larger queue

### Changes
- increase ring size from `250` to `386`
- on successful ESP-NOW send, mark the entry removable immediately
- optionally still keep a short post-send TTL (for example 5s) if you want brief observability

### Pros
- simplest lifecycle
- queue stays naturally drained
- matches reality if telemetry is best-effort and not reliability-critical
- lowest long-term queue occupancy

### Cons
- you lose the idea of retaining entries pending a later ACK
- less future-ready if you later want strict delivery accounting

### Best use
- ideal if you want the transient queue to be a **burst/offline smoothing buffer only**, not a reliability mechanism

---

## Solution 3 — Moderate resize + occupancy-aware TTL cleanup

### Changes
- increase ring size from `250` to `386`
- apply normal sent TTL cleanup at 60s
- if occupancy exceeds a threshold, e.g. `75%`, use more aggressive cleanup:
   - drop sent entries older than `20–30s`
   - continue preserving newest data

### Pros
- keeps freshest telemetry prioritized
- reduces risk of overflow during long reconnect windows
- still low impact because logic stays in background task

### Cons
- a bit more policy complexity
- behavior becomes slightly less predictable than fixed TTL

### Best use
- useful if you expect sporadic link issues and want automatic self-protection before the queue reaches full

---

## Solution 4 — Larger buffer + batched cleanup budget

### Changes
- increase ring size to `386` (selected), or `512` if later needed
- cleanup only in batches, e.g. max `8` removals per cleanup pass
- run cleanup once every 10 seconds
- expire sent entries after `60s`

### Pros
- strictly bounds mutex hold time per cleanup cycle
- good if you want deterministic low interference
- scales better if queue size later grows

### Cons
- stale entries may take a few seconds to fully drain under backlog
- slightly more implementation detail than Solution 1

### Best use
- good if you want very explicit protection against long cleanup loops while still using TTL expiry

---

## My recommendation

For your stated constraints:
- ESP-NOW telemetry is **not process critical**
- it must **not interfere with the main battery emulator code**

I recommend:

### Primary recommendation
- **Solution 1**
- increase ring size to **386**
- add **60-second sent TTL cleanup**
- run cleanup in the background transmission task **once every 10 seconds**

### Alternative if you want even simpler semantics
- **Solution 2**
- treat transient queue as best-effort buffering only and remove entries after send success

### What I would avoid as a first step
- increasing to `1024` without fixing lifecycle cleanup
- any cleanup work in the producer path
- any middle-of-buffer compaction or expensive scans on every telemetry update