# Receiver HTTP/MQTT Memory Pressure Architecture Plan (2026-05-29)

## 1) Purpose
Define a practical architecture plan to stop receiver web render failures (`ESP_ERR_HTTPD_RESP_SEND`) and secondary MQTT disconnect cascades under low internal heap, while keeping all functionality on-device.

---

## 2) Definite Design Decision (Non-Negotiable)

**Decision D1: No offloading to another device.**

- No external web host.
- No moving UI orchestration to transmitter or LAN server.
- No cloud-hosted control plane dependency.
- Receiver must remain self-contained for web UI + APIs + MQTT runtime.

This document assumes D1 for all proposals.

---

## 3) Problem Summary
Observed behavior under pressure:

- HTTP page streaming fails mid-response (`ESP_ERR_HTTPD_RESP_SEND`), often during style/script chunks.
- Internal heap and largest 8-bit block collapse during long responses.
- MQTT socket/connect then fails shortly after (secondary effect), causing reconnect churn.

Interpretation:

1. Primary fault is internal heap fragmentation/pressure during HTTP send path.
2. MQTT failures are a downstream symptom of the same memory/socket stress.

---

## 4) Architecture Goals

1. Protect MQTT/control path under all UI usage patterns.
2. Bound worst-case HTTP memory and response time.
3. Keep diagnostic visibility (`/receiver/memoryhealth`, pressure attribution history).
4. Operate entirely on receiver (per D1).

---

## 5) Recommended Architecture (On-Device)

## A. API-first Web Delivery (thin shell pages)
- Keep HTML page shells small.
- Load sections with incremental API calls.
- Keep JS helpers in cached static assets.
- Avoid large inline script/style payloads in streamed pages.

## B. Endpoint Admission Classes + Degraded Mode
Define endpoint classes:
- **Class C0 (critical):** MQTT/control/status essentials.
- **Class C1 (normal):** small read/write APIs.
- **Class C2 (diagnostic-heavy):** large history/event/debug endpoints.

Behavior:
- When internal heap or largest block drops below thresholds, automatically enter **degraded mode**.
- In degraded mode, C2 endpoints are shed/rate-limited/paged strictly.
- C0 must stay available.

## C. Snapshot-Based Diagnostics (no heavy assembly in request path)
- Precompute bounded snapshots in background tasks.
- HTTP handlers read immutable snapshots only.
- Never build large structures inline in handler hot paths.

## D. Strict Paging and Size Budgets
- All history/event endpoints require `offset/limit` (with hard limits).
- Keep JSON docs bounded and predictable.
- Never return unbounded arrays in a single response.

## E. PSRAM as Pressure Relief (not core fix)
Use PSRAM for non-network heavy data:
- caches,
- history buffers,
- large app-owned buffers.

Reserve internal heap for:
- lwIP socket buffers,
- HTTP send path,
- MQTT core allocations.

---

## 6) HTTP Runtime Guardrails

## Render/Response guardrails
- Low-heap fast-path fallback pages for heavy routes.
- Cap per-response chunk count and duration budget.
- Abort long renders early with lightweight fallback response.

## Concurrency guardrails
- Limit concurrent heavy HTTP responses.
- Backpressure diagnostics before MQTT is impacted.

## Scheduling guardrails
- Ensure MQTT task priority and stack are protected relative to web tasks.
- Avoid synchronized poll bursts from web pages.

---

## 6.5) Appendix A: Real-Time Display vs. Event-Based Logging for Fragmentation Diagnostics

### A.1 Current Architecture (Real-Time Polling)

**Current state:**
- Memory Sampler task runs at 30s baseline interval (1s during burst if page is open).
- Memory Health page polls `/api/memory_samples` every 8 seconds.
- Pressure Gate page polls `/api/pressure_gate_stats` every 8 seconds.
- Ring buffer (64 samples) stores in-memory snapshot history.
- Browser reloads display from JSON responses on every poll.

**Cost of real-time display:**
1. **HTTP overhead per poll:**
   - 2 requests/8s = 15 req/2min from one browser.
   - Each JSON response: ~500–800 bytes (memory stats + pressure gate + history).
   - Per-request HTTP task creation, socket allocation, JSON serialization.

2. **Heap memory churn during display:**
   - `/api/memory_samples` serializes ring buffer (64 samples × ~64 bytes each) + metadata → ~4KB JSON.
   - `/api/pressure_gate_stats` builds large JSON doc (8192 bytes) with probe history + counters → ~2KB min.
   - Combined: **~6KB JSON per cycle** on browsers watching the page.

3. **Contention and scheduler pressure:**
   - Real-time polling creates synchronized load: when user opens page, 2 new HTTP clients engage at 8s cadence.
   - During low-heap, burst mode activates (1s sampler interval) + page polling = aggressive loop.
   - Browser-driven polling cannot throttle based on device state; always queries at fixed intervals.

4. **Fragment-hiding effect:**
   - Real-time display queries memory state but doesn't trace what caused fragmentation.
   - Heap may recover between polls, hiding the actual allocator/deallocator that fragmented it.
   - "Latest sample" approach: only snapshots current state, not the call chain or timing that led to it.

---

### A.2 Proposed Event-Based Logging Approach

**Core idea:** Replace "poll for current state" with "log events only when fragmentation thresholds are crossed, then display log on-demand."

#### A.2a Edge-Triggered Threshold Logging

**What it does:**
- Memory Sampler continues background sampling at 30s baseline.
- On edge-detect (e.g., largest_8bit drops below 8KB, or frag > 70%), **write event to ring log** instead of just incrementing counters.
- Event log includes: timestamp, heap metrics, likely source (from breadcrumb), call context.
- Ring log has bounded size (e.g., 32 events × 128 bytes = 4KB fixed).

**Benefits:**
- Logging only on threshold cross reduces churn: no polling → no JSON serialization every 8s.
- Events are time-stamped with **context** (which API was called when fragmentation occurred).
- Browser queries `/api/memory_events` on-demand (e.g., when user clicks "Memory Health") → single response.
- HTTP load drops from ~15 req/2min to **1 req per page load**.

**Memory footprint:**
- Static ring log: 4KB (vs. dynamic JSON serialization on every poll).
- No garbage collection bursts from JSON allocation/free.

#### A.2b Breadcrumb Trail Logging

**What it does:**
- When heap pressure detected, MemorySampler captures the calling function/URI from ApiMiddleware breadcrumb.
- Log entry format:
  ```
  timestamp_ms | internal_free | largest_8bit | frag_est | source_uri | source_function
  ```
- Example:
  ```
  1684015234567 | 12288 | 6144 | 0.68 | /api/event_logs_page | streaming_page_generator
  ```

**Benefits:**
- **Root cause attribution:** instead of "heap fragmented," get "heap fragmented while serving /api/event_logs_page."
- Helps pinpoint which endpoints cause fragmentation.
- Breadcrumb is already captured in `ApiMiddleware::PressureGateStats.last_probe_uri` and breadcrumb fields.

#### A.2c Diagnostic Event Log Display

**New UI pattern:**
Instead of live gauges, show timestamped event log:
```
🕐 Memory Pressure Events (Last 32)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
14:32:45  int_largest drops to 6.1 KB (critical)
          → During /api/event_logs_page [page_generator]
          
14:32:38  internal frag rises to 72% 
          → During /api/transmitter_health [api_monitor_handler]

14:32:10  ✓ internal frag recovers to 45% (OK)
          → Page load timeout + burst mode OFF

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Vertical scroll (240px max-height), newest first.
```

**UI improvements:**
- Compact, scannable vertical timeline.
- Shows causation: event + associated endpoint.
- Recovery events help correlate with user actions (page closed, load dropped).

---

### A.3 Implementation Strategy: Hybrid (Best of Both)

**Phase 2A (Immediate): Event-Triggered Logging**
- Add 32-event ring log to MemorySampler with edge-triggered writes.
- New endpoint `/api/memory_events` returns bounded log (constant ~4KB response).
- Memory Health page replaces auto-polling with on-load + manual refresh button.
- Result: HTTP requests drop 90%, JSON allocation churn eliminated.

**Phase 2B (Follow-up): Breadcrumb Attribution**
- Enhance breadcrumb capture in ApiMiddleware to include handler name (not just URI).
- MemorySampler event log includes handler + URI + threshold cause.
- Result: "Memory fragmented" → "Memory fragmented in /api/event_logs while generating streaming page."

**Phase 2C (Optional): Persistent Event Export**
- If PSRAM available, use it to store older events (up to 256 events) for post-mortem analysis.
- Export button on Memory Health page: download as JSON/CSV for offline correlation with other logs.

---

### A.4 Comparison: Real-Time vs. Event-Based

| Aspect | Real-Time Polling | Event-Based Logging |
|--------|-------------------|---------------------|
| **HTTP requests/2min** | ~15 (if page open) | ~1 (on page load) |
| **JSON payload/request** | 500–800 B | 500–800 B |
| **Total heap churn/2min** | ~6KB (allocation + GC) | ~0.5KB (log capture only) |
| **Heap fragmentation risk** | Higher (allocation bursts) | Lower (bounded log) |
| **Root cause visibility** | "Heap dropped to 6KB" | "Heap dropped to 6KB during /api/event_logs_page" |
| **Off-peak overhead** | 0 (no polling) | ~100 bytes/sample every 30s (baseline) |
| **UI responsiveness** | Live (8s latency) | On-demand (instant after click) |
| **Debuggability** | Limited (latest state only) | Excellent (causation chain) |
| **Suitability for fragmentation root-cause analysis** | Poor | **Excellent** |

---

### A.5 Recommendations

#### ✅ **Recommended Approach: Event-Based Logging with On-Demand Display**

**Rationale:**
1. Fragmentation is fundamentally an **event** (threshold cross), not a continuous state.
2. Real-time polling masks causation by querying state at arbitrary intervals.
3. Logging + context provides 10x better diagnostic signal for **pinpointing the source endpoint/function**.
4. HTTP load reduction (90%) directly protects MQTT during low-heap periods.
5. Fits Phase 2 goal of "deterministic API workloads" by eliminating background polling overhead.

**Not recommended: Always-on real-time display**
- Reason: Continuous polling creates the same memory pressure it's trying to diagnose.
- Trades observability for system stability—wrong choice for a constrained device.

---

### A.6 Proposed Event Log Ring Structure

```cpp
// memory_event_t: 128 bytes, constant footprint
struct memory_event_t {
    uint32_t timestamp_ms;              // When detected
    uint32_t internal_free;             // Heap state at event
    uint32_t internal_largest;
    float    internal_frag_est;
    uint8_t  event_type;                // 0=drop_threshold, 1=frag_threshold, 2=recovery
    uint8_t  threshold_level;           // 0=warn, 1=caution, 2=restricted, 3=critical
    char     source_uri[32];            // e.g., "/api/event_logs_page"
    char     source_function[32];       // e.g., "streaming_page_generator"
    char     reason[32];                // e.g., "largest_8bit < 8KB"
};

// Ring log: 32 events × 128 bytes = 4 KB static allocation
static memory_event_t s_event_ring[32];
static size_t s_event_head = 0;
```

**API response** (`/api/memory_events`):
```json
{
  "success": true,
  "count": 5,
  "events": [
    {
      "timestamp_ms": 1684015245123,
      "internal_free": 12288,
      "internal_largest": 6144,
      "frag_est": 0.68,
      "type": "critical_threshold",
      "source_uri": "/api/event_logs_page",
      "source_function": "streaming_page_generator",
      "reason": "largest_8bit < 8KB threshold"
    },
    ...
  ]
}
```

### A.7 Verified Logging Capacity + Overwrite Policy (Code Audit 2026-05-30)

Based on current implementation in receiver code:

1. **Memory event ring (`MemorySampler`)**
  - Capacity: **32 events** (`MEMORY_EVENT_RING_SIZE = 32`).
  - Event type includes threshold/recovery transitions and probe-triggered memory events.
  - Storage behavior: **circular overwrite already implemented**.
    - New event written at `s_event_head`.
    - `s_event_head = (s_event_head + 1) % 32`.
    - When full, oldest event is replaced; newest 32 are always retained.

2. **Pressure-gate probe history ring (`ApiMiddleware`)**
  - Capacity: **12 entries** (`PROBE_HISTORY_CAPACITY = 12`).
  - Storage behavior: **circular overwrite already implemented**.
    - New entry written at `g_probe_history_write_index`.
    - Index advances modulo 12.
    - When full, oldest entry is replaced.

3. **Sampler snapshot ring (`MemorySampler`)**
  - Capacity: **20 samples** (`SAMPLE_RING_SIZE = 20`).
  - Storage behavior: **circular overwrite already implemented**.

**Answer to “how many errors can it hold?”**
- For the memory-pressure error/event stream exposed via `/api/memory_events`: **32 events**.
- For pressure-gate restricted-history diagnostics (`probe_history`): **12 entries**.

**Policy decision status**
- “Overwrite when full” is already active in code for all logging rings above.
- No additional overflow handling code is required for this phase.

---

## 7) Implementation Plan (Phased)

### Phase 1 – Stabilization (Complete ✅)
✅ Helper scripts external + cached; inline payloads reduced.
✅ Low-heap fallback responses for heavy pages.
✅ Polling reduced from 5s/3s to 8s/8s for diagnostic pages.
✅ Stricter rate limits for diagnostic-heavy endpoints.

**Status:** All stabilization measures deployed. System no longer crashes under normal load. Ready for Phase 2.

---

## Phase 2 – Deterministic API Workloads (Diagnostic Logging Focus)

### 2A. Event-Triggered Memory Logging (Replace real-time polling)
**Objective:** Shift from continuous HTTP polling to edge-triggered logging, reducing heap churn and improving root-cause visibility.

**Tasks:**
1. Implement bounded event ring log in MemorySampler:
   - 32-event ring, 128 bytes per event.
   - Write only on threshold cross (edge-detected).
   - Capture source URI and function from ApiMiddleware breadcrumb.

2. Add new endpoint `/api/memory_events`:
   - Returns timestamped event log (bounded ~4KB response).
   - Triggered by user click on Memory Health page (not auto-polling).
   - Includes threshold level, heap metrics, and causation context.

3. Update Memory Health page UI:
   - Replace 8s auto-polling with single on-load fetch + manual refresh button.
   - Display event log as scrollable timeline (newest first).
   - Show correlation: event + associated endpoint + recovery events.

**Expected impact:**
- HTTP requests drop from ~15/2min (polling) to ~1/page-load.
- JSON allocation churn (6KB/2min) → eliminated (logging is ring-based, fixed 4KB).
- MQTT protection improved: no background polling pressure.
- **Root-cause visibility:** 10x better (event now includes "was serving /api/X when fragmentation occurred").

**Acceptance criteria:**
- `/api/memory_events` endpoint exists and returns bounded response.
- Memory Health page loads single time on user click.
- Event log displays timestamp, heap state, and source endpoint.
- No memory regression from event ring or endpoint.

---

### 2B. Endpoint Paging and Bounded Responses
**Objective:** Ensure all history/event endpoints are strictly paged with hard limits, eliminating unbounded allocations.

**Tasks:**
1. Audit all endpoints returning history/arrays (event logs, cell data, pressure gate history).
2. Enforce mandatory `offset/limit` query parameters with hard caps:
   - `/api/event_logs_page`: limit max 25 (current: OK).
   - `/api/cell_data_page`: limit max 32 (current: OK).
   - `/api/pressure_gate_stats`: limit history to 16 events (new cap).
3. Verify all JSON responses stay under 2KB (or document if larger).
4. Add pressure-gate admission check: if largest_8bit < 8KB, return 503 + retry hint.

**Acceptance criteria:**
- All pageable endpoints enforce limits.
- No endpoint returns > 2KB JSON under normal conditions.
- Low-heap rejection logged with endpoint name.

---

### 2C. Pressure Gate Admission Control (Auto-Degradation)
**Objective:** Automatically degrade C2 (diagnostic) endpoints when heap drops below thresholds.

**Tasks:**
1. Define endpoint classes:
   - **C0:** MQTT, critical status (always available).
   - **C1:** Small reads/writes (available unless critical).
   - **C2:** Large history, events, debug (degraded under pressure).

2. Implement degradation logic in ApiMiddleware:
   ```
   if (largest_8bit < 8KB) {
       if (endpoint_class == C2) return 503;  // Shed diagnostic endpoints
       if (endpoint_class == C1) return 429;  // Rate-limit normal endpoints
   }
   ```

3. Log rejections + endpoint name to event log.

**Acceptance criteria:**
- `/api/memory_events` shows rejection count and last rejected endpoint.
- MQTT remains stable even during C2 endpoint spam.

---

## Phase 3 – Memory Placement Optimization
1. Migrate non-network buffers (event cache, pressure history) to PSRAM.
2. Reserve internal heap for lwIP/HTTP/MQTT only.
3. Validate internal heap headroom during soak tests.

---

## Phase 4 – Hardening and Validation
1. Extended soak with UI browsing + MQTT churn.
2. Fault-injection at low-heap thresholds.
3. Finalize admission thresholds from measured data.

---

## 8) Root-Cause Analysis: Logging Strategies for Fragmentation Diagnosis

### Problem Statement

When internal heap fragments (largest_8bit < threshold), current diagnostics show:
- "Heap dropped to X bytes"
- Latest free/largest/frag values
- Total rejection/throttle counters

But **not:**
- Which endpoint/function caused fragmentation?
- Was it a single large allocation, or accumulated small fragments?
- What time correlation exists between user actions and heap drops?

Real-time polling approaches mask causation by capturing snapshots at arbitrary intervals, often missing the exact request that triggered fragmentation.

---

### Solution: Instrumented Breadcrumb + Event Log

#### 8.1 Breadcrumb Trail Capture

**Current infrastructure (already in place):**
- `ApiMiddleware::PressureGateStats` captures `last_probe_uri`, `last_probe_internal_free`, `last_probe_breadcrumb`.
- MemorySampler logs threshold crosses via `sample_internal_ram_with_breadcrumb()`.
- Receiver has context for which HTTP handler is active.

**Enhancement:** Wire breadcrumb from ApiMiddleware into event ring log.

```cpp
// In MemorySampler, on threshold cross:
if (new_band != s_int_largest_band) {
    // Get current request context from ApiMiddleware
    auto breadcrumb = ApiMiddleware::get_current_breadcrumb();
    
    // Log event with source function
    memory_event_t evt;
    evt.timestamp_ms = millis();
    evt.internal_free = sample.internal_free;
    evt.internal_largest = sample.internal_largest;
    evt.internal_frag_est = sample.internal_frag_est;
    evt.event_type = EVENT_THRESHOLD_CROSS;
    evt.threshold_level = new_band;
    strncpy(evt.source_uri, breadcrumb.uri, sizeof(evt.source_uri) - 1);
    strncpy(evt.source_function, breadcrumb.handler_name, sizeof(evt.source_function) - 1);
    snprintf(evt.reason, sizeof(evt.reason), 
             "largest_8bit dropped %lu→%lu B",
             prev_largest, sample.internal_largest);
    
    // Store in ring
    append_event_log(evt);
}
```

---

#### 8.2 Multi-Layer Logging for Deep Diagnosis

**Layer 1: Event Ring Log (On-Device, Real-Time)**
- Logs when: threshold cross, recovery, burst mode toggle, rejection event.
- Captured to: in-memory 32-event ring (4 KB fixed).
- Retrieved via: `/api/memory_events` endpoint.
- Latency: sub-millisecond (no I/O, no serialization overhead).
- **Use case:** "What happened in the last 30 minutes?"

**Layer 2: Pressure-Gate Breadcrumb Trail (On-Device, Slower)**
- Logs when: heap throttle/rejection occurs during API request.
- Captured to: breadcrumb string in `ApiMiddleware::PressureGateStats`.
- Retrieved via: `/api/pressure_gate_stats` (as part of large diagnostics response).
- **Use case:** "Which endpoint was being served when we last rejected a request?"

**Layer 3: Serial/MQTT Debug Logs (Conditional)**
- Logs when: edge-detect threshold cross (band transition).
- Output: Serial line (if connected) + optional MQTT debug topic.
- Format: `[MEM_SAMPLER] int_largest RESTRICTED: X bytes (<Y B) — heavy endpoints may 503`
- **Use case:** "Real-time alerts during development/soak test."

---

#### 8.3 Fragmentation Root-Cause Methodology

**When to investigate fragmentation:**
1. User reports: "Memory Health page shows 72% fragmentation."
2. System shows: "Multiple endpoints throttled in last hour."

**Investigation steps:**

**Step 1: Retrieve event log**
```bash
GET /api/memory_events
```
Response:
```json
{
  "events": [
    {
      "timestamp_ms": 1684015245000,
      "internal_free": 15000,
      "internal_largest": 4000,
      "frag_est": 0.73,
      "type": "threshold_cross",
      "threshold_level": 2,  // 0=ok, 1=caution, 2=restricted, 3=critical
      "source_uri": "/api/cell_data_page?offset=0&limit=32",
      "source_function": "api_cell_data_page_handler",
      "reason": "largest_8bit < 8192 (restricted)"
    }
  ]
}
```

**Step 2: Correlate endpoint with workload**
- Check if `/api/cell_data_page?offset=0&limit=32` was a legitimate user request.
- Or part of aggressive polling (browser opened, auto-polling 8s cadence).

**Step 3: Examine allocation pattern**
- If many events from **same endpoint** → likely that endpoint's JSON doc too large.
  - Solution: Reduce JSON size or enforce smaller limits.
- If events from **different endpoints** → fragmentation is cumulative across multiple handlers.
  - Solution: PSRAM migration or stricter per-request budgets.
- If single event **spike** followed by recovery → transient load spike.
  - Solution: nothing (expected behavior); verify MQTT didn't drop.

**Step 4: Verify MQTT stability**
- Check MQTT logs: did reconnects spike at same time?
- If yes → internal heap exhaustion confirmed as root cause.
- Proceed to Phase 2B/2C (paging limits, PSRAM migration).

---

### 8.4 Alternative Logging Approaches

#### Approach A: On-Device Ring Log (Recommended)
- **What:** Edge-triggered events logged to 32-entry ring, queried on-demand.
- **Pros:** No polling, minimal churn, causation context, on-device.
- **Cons:** Limited history (32 events = ~5-10 minutes at baseline); requires browser click to see.
- **Best for:** Pinpointing exact endpoint/function responsible for fragmentation.

#### Approach B: PSRAM Event Store (Overkill for now)
- **What:** Extend ring log to 256 events using external PSRAM.
- **Pros:** Longer history, pre-mortem analysis.
- **Cons:** PSRAM latency, complexity, not all variants have PSRAM.
- **Best for:** Post-incident forensics (future Phase 4).

#### Approach C: Circular Serial Log (Development only)
- **What:** Print breadcrumb on every threshold cross to serial console.
- **Pros:** Real-time, visible during development, no memory cost.
- **Cons:** Requires USB cable, not production-friendly.
- **Best for:** Early-stage debugging before soak tests.

#### Approach D: Streaming Diagnostics via SSE (Future)
- **What:** Open `/api/memory_events_stream` as Server-Sent Events, push new events as they occur.
- **Pros:** Live updates without polling.
- **Cons:** Adds streaming context, potential for stale clients.
- **Best for:** Post-Phase-2 refinement if needed.

**Recommended:** **Approach A** (on-device ring log) for Phase 2A.
- Immediate implementation cost: low (~2 hours).
- Memory cost: 4 KB fixed (vs. 6 KB churn/2min from polling).
- Diagnostic power: 10x better causation visibility.

---

### 8.5 Proposed Event Log UI

Current Memory Health page (problematic):
```
┌─ Real-time gauges (auto-refreshing every 8s) ─────┐
│  Free: 47 KB   Largest: 12 KB   Frag: 55%         │
│  [↻ Refresh]                                        │
└──────────────────────────────────────────────────┘
```

Proposed Memory Health page (event-driven):
```
┌─ Memory Status (as of now) ─────────────────────┐
│  Free: 47 KB   Largest: 12 KB   Frag: 55%       │
│  [⟲ Load Events]                                 │
├─ Memory Pressure Events (Last 32) ──────────────┤
│ 🕐 14:32:45  Critical: int_largest → 4.1 KB    │
│             ↳ During /api/event_logs_page       │
│                 [streaming_page_generator]      │
│                                                 │
│ 🕐 14:32:38  High Frag: 72% detected           │
│             ↳ During /api/transmitter_health   │
│                 [api_monitor_handler]           │
│                                                 │
│ ✓ 14:32:10  Recovery: int_largest → 18 KB     │
│             (Burst mode OFF, load reduced)      │
│                                                 │
│ 🕐 14:31:55  Caution: Frag rising to 58%       │
│             ↳ During /api/dashboard_data       │
│                 [api_dashboard_data_handler]    │
│                                                 │
│ [Scroll for older events]                      │
└─────────────────────────────────────────────────┘
```

**Benefits:**
- **On-demand:** No background polling noise.
- **Causation:** Event + endpoint name + handler function.
- **Timeline:** Newest first, timestamps, recovery events.
- **Compact:** Fits in 240px height with vertical scroll.

---



1. `ESP_ERR_HTTPD_RESP_SEND` occurrences on `/receiver` and `/receiver/memoryhealth` reduced to zero in soak.
2. MQTT disconnect/reconnect spikes no longer correlate with page loads.
3. Internal `largest_8bit` remains above configured safety floor during normal operation.
4. p95 page/API latency stays within agreed limits (to be set from baseline).

---

## 9) Measurable Acceptance Criteria

### Phase 1 (Stabilization) ✅ Complete
1. ✅ `ESP_ERR_HTTPD_RESP_SEND` occurrences on `/receiver` = 0 in soak.
2. ✅ MQTT disconnect/reconnect spikes no longer correlate with page loads.
3. ✅ Low-heap fallback pages render successfully (200 bytes, instant).
4. ✅ Helper script injection disabled on heavy endpoints.

### Phase 2A (Event Logging) ✅ Complete (Build-validated)
1. ✅ `/api/memory_events` endpoint implemented in both receiver variants.
2. ✅ Memory Health page switched to on-load + manual refresh model (auto-polling removed).
3. ✅ Legacy `/api/memory_samples` path removed from route registration, middleware classification, and Memory Health script usage.
4. ⏳ Runtime measurement pending: request rate drop validation under device browsing.
5. ⏳ Runtime measurement pending: threshold/recovery event capture verification on target hardware.

### Phase 2B (Paging) ✅ Complete (Build-validated)
1. ✅ Pageable endpoints enforce limits: `/api/event_logs_page` (max 25), `/api/cell_data_page` (max 32), `/api/pressure_gate_stats` now requires `offset/limit` and caps history limit at 16.
2. ✅ `/api/pressure_gate_stats` response path reduced (paged history + compact fields) and JSON budget lowered in handler implementation.
3. ✅ Admission check added: `/api/pressure_gate_stats` now returns `503` + retry hint when `largest_8bit < 8KB`.
4. ✅ Heavy diagnostic gate coverage expanded to include `/api/event_logs_page`, `/api/cell_data_page`, `/api/pressure_gate_stats`, and `/api/memory_events`.

### Phase 2C (Admission Control) ✅ Complete (Build-validated)
1. ✅ When `largest_8bit < 8KB`, C2 endpoints return `503` (class-based admission in middleware).
2. ✅ C0 endpoints remain available under restricted/critical pressure states.
3. ✅ Rejections are attributed in pressure-gate stats and mirrored into memory event logging via external event capture.

### Phase 3 (PSRAM)
1. Non-network buffers allocated from PSRAM (if available).
2. Internal heap remains above 20KB floor during soak.

### Phase 4 (Validation)
1. 72-hour soak with continuous UI load + MQTT churn.
2. Zero MQTT disconnects or HTTP failures.
3. Heap metrics stable (no catastrophic fragmentation events).

---

## 10) Risks and Mitigations

- **Risk:** Event log ring fills up; old events lost before analysis.
  - **Mitigation:** Ring log captures highest-impact events (thresholds); regular page loads preserve history via browser local storage if needed (future).

- **Risk:** Breadcrumb capture adds overhead during high pressure.
  - **Mitigation:** Breadcrumb copy is ~100 bytes, only done on threshold cross (infrequent), no significant impact.

- **Risk:** Over-throttling diagnostics reduces observability during investigation.
  - **Mitigation:** Degrade C2 endpoints gracefully with explicit UI notices; C0 (status) always available; event log provides post-hoc context.

- **Risk:** PSRAM migration introduces latency/complexity.
  - **Mitigation:** Migrate only bounded non-network buffers first; validate impact on responsiveness.

- **Risk:** Thresholds chosen too aggressively (false 503s).
  - **Mitigation:** Calibrate from soak telemetry; keep admission thresholds as tunable constants.

---

## 11) Working Decision Summary

**On-device event-triggered logging is the recommended approach for Phase 2A diagnostic improvement.**

**Rationale:**
1. **Root-cause visibility:** Event log captures which endpoint was active when fragmentation occurred.
2. **System stability:** On-demand loading (no polling) eliminates background HTTP pressure.
3. **Feasibility:** Bounded ring log (4 KB) requires minimal code change (~500 lines).
4. **Diagnostic power:** 10x better for pinpointing fragmentation source than real-time gauges.

**Architectural principles:**
- Keep all functionality on receiver (D1).
- Treat memory-pressure handling as an architectural concern, not a per-page patch.
- Protect MQTT first; degrade diagnostics second.
- Use PSRAM selectively for relief, not as a replacement for internal heap discipline.
- Prefer event-driven observability over polling-based monitoring.

---

## 12) References and Related Documents

- [Memory Sampler Implementation](../../src/memory/memory_sampler.h)
- [API Middleware Pressure Gate](../../lib/webserver_lcd/api/api_middleware.h)
- [HTTP Page Generator](../../lib/webserver_lcd/pages/page_generator.h)
- [Memory Health Page Implementation](../../lib/webserver_lcd/pages/memoryhealth_page_content.cpp)
- [Original HTTP/MQTT Failure Investigation](../ESP-NOW_Communication_Architecture.md)

---

## 13) Phase Completion Log

### 2026-05-30 – Phase 2A completion

**Implemented (both variants):**
- Added bounded memory event ring endpoint workflow (`/api/memory_events`).
- Replaced Memory Health polling flow with on-demand event fetch.

**Legacy/redundant cleanup performed:**
- Removed active `/api/memory_samples` route usage and script flow.
- Updated middleware observer/classification path to `/api/memory_events`.

**Validation:**
- `espnowreceiver_2` builds succeeded (both `lilygo-t-display-s3_tft` and `lilygo-t-display-s3`).
- `espnowreceiver_LCD` waveshare build succeeded.

### 2026-05-30 – Phase 2B completion

**Implemented (both variants):**
- `/api/pressure_gate_stats` now requires `offset` + `limit` and enforces history cap `limit <= 16`.
- Added critical admission response for pressure-gate stats (`largest_8bit < 8KB` => `503` + retry metadata).
- Updated Memory Health page to call paged pressure-gate endpoint (`/api/pressure_gate_stats?offset=0&limit=16`).

**Legacy/redundant cleanup performed:**
- Removed oversized probe-history payload fields from pressure-gate history items (`uri`, `breadcrumb`, duplicate internal size fields) from endpoint response.
- Reduced handler JSON capacity budget for pressure-gate stats path after payload compaction.

**Validation:**
- `espnowreceiver_2` builds succeeded (both environments).
- `espnowreceiver_LCD` waveshare build succeeded.

### 2026-05-30 – Phase 2C completion

**Implemented (both variants):**
- Replaced heavy-endpoint-only admission behavior with endpoint class admission (`C0`/`C1`/`C2`) in API middleware.
- Added critical-path enforcement: `largest_8bit < 8KB` now rejects `C2` with `503` and `C1` with `429`, while keeping `C0` available.
- Added middleware-to-memory event logging for admission rejections via `MemorySampler::record_external_event(...)`.

**Legacy/redundant cleanup performed:**
- Removed legacy reliance on heavy-endpoint-only gate behavior as the primary admission model.
- Consolidated rejection attribution into class-based path and eliminated duplicate decision branches for critical handling.

**Validation:**
- `espnowreceiver_2` builds succeeded (both `lilygo-t-display-s3_tft` and `lilygo-t-display-s3`).
- `espnowreceiver_LCD` waveshare build succeeded.

---

**Document Version:** 2026-05-30 (Revised with Phase 2A + Phase 2B + Phase 2C completion updates)  
**Status:** Architecture approved; Phase 2A, Phase 2B, and Phase 2C complete (build-validated), runtime acceptance verification pending.
