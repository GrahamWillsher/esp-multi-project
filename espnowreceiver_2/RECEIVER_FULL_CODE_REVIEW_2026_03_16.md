# Receiver Full Code Review (Performance, Efficiency, and Quality)

Date: 2026-03-16  
Project: `espnowreceiver_2`  
Scope reviewed:
1. Continuous message handling path (ESP-NOW + MQTT ingest)
2. Display output path (TFT/LVGL integration and scheduling)
3. Web output path (HTTP APIs, SSE, pages)
4. Cross-cutting concerns (thread safety, logging, memory behavior, maintainability)

---

## Executive Summary

The receiver architecture is in a good place overall: routing is modular, handlers are separated by domain, and there are clear attempts to avoid unnecessary display updates and to gate expensive cell-data subscriptions when no client is watching.

The largest improvement opportunities are now **runtime behavior under sustained load**:

- **P0 (highest impact):** remove blocking display animation work from the message worker path.
- **P0:** tighten thread-safety for shared caches used by MQTT, ESP-NOW worker, and webserver handlers.
- **P1:** reduce avoidable JSON/String churn in SSE and high-frequency API paths.
- **P1:** reduce INFO-level logging in hot loops.
- **P2:** clean up duplicate/dead compatibility code and improve observability metrics.

If these are addressed, the receiver should gain:
- Better sustained throughput under bursty traffic
- Lower jitter in message processing latency
- Lower heap churn and less fragmentation risk over long uptime
- Better stability when multiple web clients/SSE streams are connected

---

## What is already good

1. **Message routing decomposition is strong** (`setup_message_routes()` is modular and explicit).  
2. **Display updates are value-change gated** (`update_display_if_changed()` avoids blind redraws).  
3. **SSE for monitor stream is event-driven** (waits on event bits instead of fixed busy polling).  
4. **Cell-data MQTT subscription is demand-driven** (subscriber-count model is directionally correct).  
5. **Connection and heartbeat responsibilities are clearer than older monolithic flow**.

---

## Findings and Recommendations

## A) Message Handling (continuous path)

### A1) Blocking display animation occurs inside the ESP-NOW worker path (P0)
**Where observed**
- `src/espnow/espnow_tasks.cpp` (`handle_data_message()` and `update_display_if_changed()`)
- `src/display/tft_impl/tft_display.cpp` (`draw_power()` uses pulse loops + `smart_delay()`)

**Issue**
The ESP-NOW worker acquires display mutex and directly calls display functions. Power-bar rendering can enter pulse animation loops that call `smart_delay()`. This means the hot message-processing path can block on UI animation timing.

**Impact**
- Message queue drain slows under sustained updates
- Higher risk of queue overflow/drops
- Increased end-to-end latency jitter

**Recommendation**
- Make message worker strictly data-ingest/state-update only.
- Move all UI rendering (including pulse animation) to a dedicated display task consuming latest snapshot/state.
- Use “latest-value wins” semantics for display updates rather than per-message rendering.

---

### A2) Queue depth is low for burst scenarios (P1)
**Where observed**
- `src/common.h` (`ESPNow::QUEUE_SIZE = 10`)
- `src/espnow/espnow_callbacks.cpp` (drop on queue full)

**Issue**
Queue length is small relative to possible burst traffic (data + heartbeats + packet fragments + config responses).

**Impact**
- Drops during bursts are likely
- Data freshness and control responsiveness can degrade transiently

**Recommendation**
- Increase queue depth moderately (e.g., 24–48) after stack/heap headroom validation.
- Add counters for enqueue fail count and max queue watermark.

---

### A3) High-volume INFO logs in fast path (P1)
**Where observed**
- `src/espnow/espnow_tasks.cpp` (`LOG_INFO` for frequent data events, display updates, version beacons etc.)
- `src/espnow/rx_heartbeat_manager.cpp` (`LOG_INFO` on every heartbeat)

**Issue**
Frequent INFO logs in continuous paths increase CPU and serial IO overhead.

**Impact**
- Throughput loss and latency jitter
- Harder to read logs due to noise

**Recommendation**
- Keep per-message logs at `DEBUG`/`TRACE` only.
- Keep INFO for state transitions, errors, and summary counters.
- Add periodic rollup logs (e.g., every 10–30s) rather than per-packet logs.

---

### A4) `on_data_recv()` copies full payload to queue each callback (expected, but optimize observability) (P2)
**Where observed**
- `src/espnow/espnow_callbacks.cpp`

**Issue**
Current approach is fine architecturally (minimal callback work), but no detailed telemetry for callback pressure and drop behavior.

**Recommendation**
- Keep architecture as-is.
- Add lightweight stats: callback count, drops, average queue latency, max queue depth.

---

## B) Display Output Path

### B1) Shared TFT mutex contention between LED renderer and data rendering (P1)
**Where observed**
- `src/main.cpp` (`task_led_renderer()` every ~20ms)
- `src/espnow/espnow_tasks.cpp` (`update_display_if_changed()` takes same mutex)

**Issue**
Two high-frequency producers contend for the same mutex; LED updates and SOC/power updates can starve each other.

**Impact**
- Visual jitter
- Occasional skipped display updates
- Message worker blocking longer than necessary

**Recommendation**
- Centralize all display drawing in one display task.
- Expose LED state + metric state into a render model and draw in one pass.
- If keeping separate tasks, use shorter critical sections and avoid any sleep while holding display lock.

---

### B2) Potential backend drift: mixed legacy/new display layers (P2)
**Where observed**
- `src/display/display.cpp` and legacy `src/display/display_core.cpp`
- Build filters exclude/include variants per environment

**Issue**
Multiple display implementations and legacy compatibility files increase risk of divergence and accidental regressions.

**Impact**
- Higher maintenance cost
- Harder to reason about real runtime path

**Recommendation**
- Consolidate onto one active backend pathway per environment.
- Move legacy files to archive or enforce compile-time guard comments and CI checks.

---

### B3) LVGL service cadence risk in generic loop (P2, if LVGL env used)
**Where observed**
- `src/display/display.cpp` provides `display_task_handler()`
- `src/main.cpp` loop currently does not call it directly

**Issue**
For LVGL builds, animations and timers require regular `task_handler()` service. Ensure this is guaranteed in the active runtime path.

**Recommendation**
- Explicitly service LVGL task handler at fixed cadence (or dedicated LVGL task) in LVGL environment.

---

## C) Web Output Path

### C1) Cell SSE stream sends full payload every 500ms even when unchanged (P0/P1 depending traffic)
**Where observed**
- `lib/webserver/api/api_sse_handlers.cpp` (`api_cell_data_sse_handler()`)

**Issue**
Cell stream loops every 500ms and sends JSON each cycle regardless of data change.

**Impact**
- Unnecessary CPU, heap churn, socket traffic
- Avoidable HTTP server task usage

**Recommendation**
- Add change detection (hash/version/sequence) and send only on change.
- Keep heartbeat pings lightweight to detect disconnects.

---

### C2) Extensive dynamic `String` concatenation in frequently hit handlers (P1)
**Where observed**
- `lib/webserver/api/api_telemetry_handlers.cpp`
- `lib/webserver/api/api_sse_handlers.cpp`
- Several page handlers building HTML and JSON via `String`

**Issue**
Large and repeated `String` assembly increases heap churn and fragmentation risk on long uptime.

**Impact**
- Heap fragmentation over time
- Response latency variability

**Recommendation**
- Use preallocated char buffers / streaming serializers where practical.
- Use fixed `StaticJsonDocument` where schema is known.
- For pages: serve static templates from LittleFS or `PROGMEM` with minimal runtime concatenation.

---

### C3) HTTP handlers can block for seconds while proxying transmitter calls (P1)
**Where observed**
- Telemetry/control handlers using `HTTPClient` with 3–60s timeouts

**Issue**
Synchronous outbound HTTP calls inside request handlers consume server worker capacity.

**Impact**
- With `max_open_sockets = 4`, a few slow proxied requests can starve other clients.

**Recommendation**
- Prefer cached data for UI paths; refresh cache asynchronously in background task.
- Keep request handlers mostly non-blocking and fast.
- For unavoidable proxy calls, reduce timeout and return partial/degraded response quickly.

---

### C4) Webserver socket/task sizing is conservative for SSE + UI concurrency (P1)
**Where observed**
- `lib/webserver/webserver.cpp` (`max_open_sockets = 4`, server task priority/stack fixed)

**Issue**
With SSE clients plus regular API/page requests, socket pool can saturate quickly.

**Recommendation**
- Re-profile and tune: `max_open_sockets`, task stack, and handler timeouts based on observed concurrent usage.
- Add counters for rejected connections and handler timeouts.

---

## D) Cross-cutting Architecture/Correctness

### D1) Shared cache thread safety is not explicit (P0)
**Where observed**
- `lib/webserver/utils/transmitter_manager.*` static shared mutable data
- Data written from MQTT/ESP-NOW paths, read by web handlers and SSE loops

**Issue**
No explicit mutex/RW-lock around shared arrays/vectors/Strings (cell arrays, event logs, metadata, network config, etc.).

**Impact**
- Potential race conditions and torn reads
- Rare but severe instability (especially around dynamic reallocation in `storeCellData()`)

**Recommendation**
- Introduce a dedicated lock (prefer RW lock or short critical sections + copy-on-read snapshots).
- Never return raw mutable pointers to data that can be reallocated concurrently.
- For SSE handlers, snapshot cell/event structures atomically before formatting JSON.

---

### D2) NVS write-through called from many update paths (P1)
**Where observed**
- `TransmitterManager::saveToNVS()` invoked from multiple store/update APIs

**Issue**
Frequent writes are expensive and increase flash wear.

**Recommendation**
- Add coalesced/debounced persistence (e.g., delayed flush after quiet period).
- Keep immediate writes only for critical state.

---

### D3) Type mismatch and timer-handle hygiene in MQTT subscription grace logic (P1)
**Where observed**
- `src/mqtt/mqtt_client.h/.cpp` (`cell_data_pause_timer_` declared as `TaskHandle_t` but used as FreeRTOS software timer)

**Issue**
Handle type semantics are inconsistent.

**Recommendation**
- Change to `TimerHandle_t` and keep timer ownership/lifecycle explicit.
- Ensure callbacks and stop/delete paths are guarded for race safety.

---

### D4) Duplicate legacy blocks increase review complexity (P2)
**Where observed**
- `lib/webserver/api/api_handlers.cpp` has large disabled legacy block (`#if 0`) and live duplicate registration sections.

**Issue**
Review and maintenance overhead; easier to accidentally modify wrong copy.

**Recommendation**
- Remove dead blocks once migration confidence is high.
- Keep a single source of truth per endpoint map.

---

## Prioritized Action Plan

## Implementation Progress (Updated 2026-03-16)

### Completed
- ✅ **P0-1** Decoupled display rendering from ESP-NOW worker using a snapshot queue + dedicated renderer task.
- ✅ **P0-2** Added explicit synchronization and snapshot read APIs in `TransmitterManager` for cell/event shared data.
- ✅ **P0-3** Made cell SSE change-driven (event-based updates with lightweight keepalive ping).
- ✅ **P1-4** Reduced hot-path INFO logs in ESP-NOW/MQTT data paths.
- ✅ **P1-5** Tuned ESP-NOW RX queue depth (`10` → `24`) and added queue telemetry counters (callbacks/drops/high-watermark) with periodic rollup logging.
- ✅ **P1-6 (expanded)** Reduced String/JSON churn across hot and adjacent API paths: reserved cell-data JSON/SSE paths, removed ad-hoc JSON concatenation in control handlers, and eliminated avoidable `String` copies in network/MQTT config save handlers.
- ✅ **P1-6 (final sweep)** Cleaned remaining shared type-selection JSON generation to use structured serialization rather than repeated concatenation, bringing the primary receiver API surface to a consistent JSON-response style.
- ✅ **P1-7** Implemented debounced/coalesced NVS persistence in `TransmitterManager` (2s debounce timer).
- ✅ **P1-8** Fixed MQTT grace-period timer handle type to `TimerHandle_t`.
- ✅ **P2-9 (api registrar scope)** Removed duplicated legacy API registration blocks in `lib/webserver/api/api_handlers.cpp`; registrar now has a single canonical implementation and rebuild passes.
- ✅ **P2-10 (active display path cleanup)** Switched active code (`main.cpp`, `display_update_queue.cpp`) to the canonical `display.h` public API and marked `display_core.h` as legacy compatibility-only to reduce backend drift/confusion without removing alternate build support.
- ✅ **P2-11 (expanded)** Added `/api/system_metrics` runtime metrics endpoint with heap/wifi/ESP-NOW queue telemetry, SSE subscriber/runtime counters (connects, active clients, ping/send failures, session durations), and event-log proxy latency/timeout/error counters.
- ✅ **P2-11 (latency buckets)** Added per-handler HTTP latency buckets in `/api/system_metrics` (`calls`, `last_ms`, `max_ms`, `avg_ms`) across telemetry endpoints.
- ✅ **P2-11 (webserver capacity/state)** Added webserver runtime config/state metrics (`max_open_sockets`, handler registration coverage, stack/priority, startup success/failure counts) plus a live socket-utilization estimate from active SSE streams.

### Remaining / Next
- ⏳ Optional future polish: continue consistency cleanup in page-rendering utilities and non-critical/debug-only paths.
- ⏳ Continue legacy cleanup sweep in other modules (display/backend and non-active compatibility paths).
- 🔄 Optional deeper legacy cleanup remains in non-active compatibility/alternate-backend files, but active receiver build path now points at the canonical display API.
- 🔄 Expand runtime metrics depth further with deeper ESP-IDF socket/task saturation hooks and cross-module handler instrumentation.

---

## Phase P0 (Do first)
1. Decouple display rendering from ESP-NOW worker hot path (snapshot + render task model).  
2. Add explicit synchronization for `TransmitterManager` shared data (especially cell/event buffers).  
3. Make cell SSE change-driven (not periodic full resend).

## Phase P1 (High value next)
4. Reduce INFO logs in continuous paths; add periodic summaries/counters.  
5. Tune queue depth + add queue/drop telemetry.  
6. Replace high-churn String JSON assembly in hot endpoints.  
7. Debounce/coalesce NVS persistence writes.  
8. Fix MQTT grace timer handle type and lifecycle semantics.

## Phase P2 (Maintainability and future-proofing)
9. Remove stale/duplicated legacy blocks (`#if 0`, duplicate registration sections).  
10. Consolidate display backend code path and add CI guardrails for active files per environment.  
11. Add webserver runtime metrics (active sockets, handler latencies, timeout counts).

---

## Suggested Instrumentation (to validate gains)

Add lightweight metrics and expose via `/api/system_metrics`:
- ESP-NOW RX rate (msg/s), queue depth max, queue drops
- Worker handler time p50/p95
- Display render time p50/p95 and skipped frame count
- SSE active clients and bytes/sec per stream
- HTTP handler latency p50/p95, timeout count
- Free heap / largest block / fragmentation indicator

This will make optimization work measurable and prevent regressions.

---

## Closing

The receiver is already structurally improved and modular. The next gains are mostly in **runtime scheduling discipline**, **shared-state safety**, and **eliminating avoidable work when nothing changed**. Addressing the P0/P1 items should materially improve long-run stability and responsiveness.
