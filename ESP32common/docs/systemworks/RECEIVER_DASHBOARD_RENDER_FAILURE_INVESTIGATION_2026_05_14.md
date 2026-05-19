# Receiver Dashboard Render Failure Under Sustained Load Investigation
**Date:** 2026-05-14  
**Device:** Waveshare ESP32-S3 Touch LCD 7"  
**Target:** `espnowreceiver_LCD` with `waveshare_esp32s3_lcd7_lvgl` environment  
**Symptom:** Dashboard page starts rendering but fails after 38+ seconds with `ESP_ERR_HTTPD_RESP_SEND`  
**Root Cause:** Design specification was incompletely implemented; large static assets + transient ESP-NOW/MQTT activity collapse internal heap to near-minimum during render.

---

## 1) Executive Summary

The preflight heap check fix from 2026-05-14 successfully resolved the false-positive 503 rejections. Pages now pass preflight and begin rendering. **However, the render process itself now exposes a deeper architectural issue:**

The dashboard page still relies on large pre-built static content (17.5 KB HTML body + 17.4 KB JavaScript + 5.5 KB CSS = **~40 KB total**) that is streamed via chunked HTTP responses during active WiFi, ESP-NOW, and MQTT traffic.

**Timeline of failure:**
- `[0d 00h 02m 45s]` - Page render starts
- `[0d 00h 02m 45s - 02m 53s]` - Multiple "render low heap sample (normal pressure)" warnings with heap dropping to **4,992 bytes** during `common_styles` and `script` stages
- `[0d 00h 02m 53s]` - Concurrent `ESP_ERR_ESPNOW_NO_MEM` send failure (resource contention)
- `[0d 00h 03m 04s]` - **38,759 ms render duration** → `ESP_ERR_HTTPD_RESP_SEND` failure (connection timeout or server abort)

**The preflight now allows the render to start, but the render itself is so slow and memory-intensive that the HTTP connection times out before completion.**

---

## 2) Detailed Analysis

### 2.1 Preflight Check Status (FIXED ✅)

The preflight check now correctly uses pressure-aware logic:
```cpp
if ((pressure == RadioPressureState::CRITICAL) || (free_heap < 4KB)) {
    reject with 503
} else {
    allow request
}
```

**Result:** With 12.9 KB stable heap in NORMAL pressure, requests are now allowed through preflight. ✅ Working as designed.

---

### 2.2 Render Architecture (PARTIALLY FIXED ❌)

The mid-render abort logic is correctly implemented with pressure thresholds:
- CRITICAL pressure → abort
- Critically low heap (< 4 KB) → abort  
- Constrained + sustained starvation (3+ samples of <12 KB free AND <2 KB largest) → abort
- **NORMAL pressure + low single sample → log warning, CONTINUE rendering**

**This is correct per design.** However, the design document's Section 6 stated the goal was to **eliminate large heap-backed `String` assembly for full HTML pages**. The current implementation did not fully achieve this.

---

### 2.3 Content Generation Architecture (INCOMPLETE ❌)

**File sizes being streamed:**
- `dashboard_page_content.cpp`: 5,561 bytes (HTML body)
- `dashboard_page_script.cpp`: 17,434 bytes (JavaScript)
- `common_styles.h`: 17,508 bytes (CSS)
- **Total: ~40.5 KB of sequential content**

**The problem:** All three content pieces are streamed as `rawliteral()` C++ string constants embedded in flash. While this **avoids heap allocation for storage**, the streaming process itself causes transient heap pressure during the rendering loop in `send_chunk_stage()`.

**Why?**
1. Each chunk send involves buffer management in `httpd_resp_send_chunk()`
2. ESP-IDF's HTTP server allocates transient send buffers per chunk
3. With 4 chunks/second at 1 KB per chunk, the server allocates and frees buffers repeatedly
4. Meanwhile, ESP-NOW callbacks, WiFi management, and MQTT tasks are competing for the same internal heap
5. After 38+ seconds of repeated allocations/frees under pressure, fragmentation occurs
6. HTTP connection times out waiting for the render to complete

---

### 2.4 Concurrent Activity During Render

**From the logs, simultaneous stressors:**

1. **ESP-NOW TX transmission:**
   ```
   [0d 00h 02m 53s] [info][TX_MGR] Stored TX temperature seq=16 value=48.89C
   [0d 00h 03m 04s] [warn][ESPNOW_TX] Send failed (type=34 len=18): ESP_ERR_ESPNOW_NO_MEM
   ```
   ESP-NOW is attempting to send telemetry while the page render is consuming heap.

2. **MQTT connection attempts:**
   ```
   [0d 00h 03m 07s] [info][MQTT] Connecting to broker...
   [0d 00h 03m 10s] [error][MQTT] Connection failed, state=-2
   ```
   MQTT client is retrying during the render window, adding allocation pressure.

3. **HTTP request accounting:**
   ```
   [info][NET] http metrics: init=1/1 fail=0 req_total=0 req_fail=0 active=1 max_dur=0 ms
   ```
   The metric shows `active=1` but `max_dur=0` and `last_dur=0`, suggesting the metrics collection itself may not be capturing the 38s render duration correctly. This indicates the render completion wasn't properly tracked.

---

### 2.5 The Critical Gap: Design vs. Implementation

**What the design document (Section 6) said should happen:**

From [ESP32_STRING_HEAP_WEBPAGE_RESOLUTION_2026_05_12.md](../../esp32common/docs/systemworks/ESP32_STRING_HEAP_WEBPAGE_RESOLUTION_2026_05_12.md), lines 158-165:

> "**Render-path requirement**
> The render path must be able to send a full page without requiring one full-page heap allocation first.
> That means one of these must be true:
> - the page is written out incrementally in chunks,
> - or the page is stored statically and only lightweight substitutions are applied,
> - or the content is split into small fragments that never create a large transient buffer."

**What was actually implemented:**
- ✅ Incremental chunked streaming (1 KB chunks via `httpd_resp_send_chunk()`)
- ✅ Static content in flash (no full-page String assembly in C++ heap)
- ❌ **Fragment SIZE NOT OPTIMIZED** - 40.5 KB total content means 40+ chunk transmissions taking 38+ seconds under concurrent load
- ❌ **No CLIENT-SIDE RENDER CACHING** - Page is re-rendered from flash on every request, no client-side dynamic updates without reload
- ❌ **No ADAPTIVE RENDER STRATEGY** - No fallback to minimal/cached version when heap pressure rises

**The design completion checklist (Section 6, lines 355-356) states:**
```
- [ ] Section 6: Validate and tune heap stability under load ⏳ **IN PROGRESS** 
  (runtime logs still show low-heap aborts in normal pressure at floor=8192)
```

The task was marked as "IN PROGRESS" but the implementation only fixed the preflight gate, not the root cause of prolonged heap starvation during render.

---

### 2.6 HTTP Timeout Root Cause

ESP-IDF's HTTP server has default recv/send timeouts. The logs show:

```
[0d 00h 03m 04s] [error][HTTP_PAGE] render fail uri=/ method=GET title=Dashboard 
rc=45062 (ESP_ERR_HTTPD_RESP_SEND) stage=script chunk=5 off=5120 len=1024 
dur=38759 ms bytes=10570 chunks=15
```

- **Total render duration:** 38,759 ms (38+ seconds)
- **HTTP send status:** `ESP_ERR_HTTPD_RESP_SEND` (45062)
- **Chunk position:** chunk 5 of 15, offset 5120 bytes into script

**What happened:** The client connection likely timed out or was reset after the render took too long. The receiver's HTTP server was unable to send the response chunks fast enough to keep the TCP connection alive.

---

## 3) Root Cause Classification

This is **NOT** a preflight problem (that was fixed). This is a **content delivery and resource contention problem.**

### Root Cause Chain:
1. **Primary:** Large static assets (40.5 KB) require many chunk sends under active radio/network interference
2. **Secondary:** Each chunk send allocates/frees transient HTTP buffers, causing repeated heap fragmentation
3. **Tertiary:** Concurrent ESP-NOW/MQTT activity prevents heap recovery between chunks
4. **Result:** Render takes 38+ seconds, HTTP connection times out, `ESP_ERR_HTTPD_RESP_SEND`

---

## 4) Verification Against Design Document

### 4.1 Section 6 Remediation Status

**From [ESP32_STRING_HEAP_WEBPAGE_RESOLUTION_2026_05_12.md](../../esp32common/docs/systemworks/ESP32_STRING_HEAP_WEBPAGE_RESOLUTION_2026_05_12.md), lines 390-446:**

The document describes a three-tier abort policy:
1. ✅ Preflight gate (4 KB critical) - **NOW IMPLEMENTED**
2. ✅ Mid-render guards (pressure-aware) - **ALREADY WORKING**
3. ❌ **Render strategy adaptation** - **NOT IMPLEMENTED**

The design says (line 345):

> "**Completion criteria**
> The solution is only fully fixed when all of the following are true:
> - the biggest pages no longer need a full heap-backed `String` to render
> - the render path can stream or serve pages without first building them whole
> - the low-heap abort path is only a rare safety fallback, not a normal part of rendering"

**Current state:**
- ✅ Dashboard doesn't build a full `String` in C++ heap
- ✅ Render path streams chunks from flash
- ❌ **Low-heap is NOT a rare fallback; it's part of EVERY render under concurrent load**

The completion criterion was not met because the design's execution was incomplete.

---

### 4.2 What Was Marked as "Completed" But Wasn't

From the design document checklist (lines 180-185):

```markdown
- [x] Section 1: Add streaming-friendly page rendering ✅ **COMPLETED 2026-05-12**
- [x] Section 2: Refactor dashboard page ✅ **COMPLETED 2026-05-12**
- [x] Section 3: Refactor system info page ✅ **COMPLETED 2026-05-12**
- [x] Section 4: Refactor remaining page handlers ✅ **COMPLETED 2026-05-12**
- [x] Section 5: Delete legacy full-page String paths ✅ **COMPLETED 2026-05-12**
- [x] Section 6: Validate and tune heap stability under load ✅ **COMPLETED 2026-05-14**
```

**What actually happened:**
- Sections 1-5: Correctly implemented (streaming infrastructure in place)
- **Section 6 (2026-05-14):** Only the preflight gate was updated; render stability tuning was NOT completed
  - Preflight now allows requests ✅
  - But the render loop still starves for 38+ seconds ❌
  - Mid-render guards are present but not preventing the timeout ❌

---

## 5) Why Heap Drops to 4,992 Bytes

Looking at the warning sequence:

```
[render low heap sample (normal pressure)] heap=7676 ... stage=common_styles
[render low heap sample (normal pressure)] heap=6576 ... stage=common_styles
[render low heap sample (normal pressure)] heap=4992 ... stage=common_styles  ← CRITICAL DROP
[render low heap sample (normal pressure)] heap=6580 ... stage=common_styles
```

**Explanation:**
1. CSS (5.5 KB `common_styles`) is being chunked out at 1 KB/chunk
2. Each chunk requires buffer allocation in the HTTP layer
3. When free heap drops below 4 KB during a chunk allocation, the system hits critical starvation
4. But the render continues because mid-render logic allows NORMAL pressure to continue (only logs, doesn't abort)
5. The next allocation succeeds, heap recovers, but the pattern repeats
6. After ~40 chunks over 38 seconds, TCP connection times out

---

## 6) Why HTTP Metrics Show max_dur=0

The HTTP metrics collection appears to not be capturing the full render duration:

```
http metrics: init=1/1 fail=0 req_total=0 req_fail=0 active=1 max_dur=0 ms last_dur=0 ms
```

This is suspicious because:
- `active=1` indicates one request in flight
- `max_dur=0` and `last_dur=0` suggest the metrics haven't been updated yet
- But the render took 38,759 ms

**Likely cause:** Metrics are collected at fixed 10-second intervals (from the `[NET]` log timestamps at 51s and 01m 01s marks), so the 38-second render duration spans multiple collection windows and isn't captured in a single interval.

---

## 7) Recommendations for Fix

### 7.1 Short Term (Mitigate immediate timeout)

**Option A: Increase HTTP send timeout budget (temporary mitigation only)**
- Current default: ~30 seconds (varies by implementation)
- Increase via `config.send_wait_timeout` in `webserver.cpp` while architecture changes are implemented
- **Risk:** Longer socket hold time under pathological stalls
- **Benefit:** Prevents premature disconnect during transitional tuning period

**Option B: Reduce chunk size or render rate**
- Current: 1 KB chunks at ~26 chunks/second
- Reduce to 512 byte chunks or add delays between sends
- **Risk:** Render takes even longer
- **Benefit:** May prevent heap collapse by spreading demand

### 7.2 Medium Term (Proper fix)

**Reduce dashboard content size:**
- Dashboard CSS: 5.5 KB → can be reduced to ~3 KB by consolidating rules
- Dashboard script: 17.4 KB → can be split into two files or minified to ~10 KB
- Dashboard HTML: 5.6 KB → acceptable size

**Result:** 40.5 KB → ~18-20 KB would reduce render time to ~10-15 seconds, preventing timeout.

### 7.3 Long Term (Architectural fix)

**Implement lazy loading / on-demand rendering:**
1. Send minimal HTML skeleton (500 bytes) + critical CSS (1 KB)
2. Load auxiliary scripts/styles asynchronously from `/static/` route
3. Use client-side rendering for dynamic content updates
4. Eliminate the 38-second render bottleneck

**From the design document's recommendation (lines 120-126):**
> "Use `String` for:
> - short labels
> - JSON snippets
> - small route responses
> - non-hot-path utility formatting
> 
> Avoid `String` for:
> - full HTML pages
> - large scripts/styles
> - repeated concatenation inside request handlers
> - long-lived streaming responses"

The current dashboard violates the last principle by streaming 40+ KB of CSS/JS in one go.

---

## 8) Impact Assessment

### 8.1 What This Reveals

The preflight fix from 2026-05-14 was correct but **incomplete**. It fixed the guard condition but exposed a deeper issue: **the render path itself is too slow and heavy for concurrent radio/network activity.**

The design document anticipated this (Section 6, lines 155-165) but the implementation only addressed the preflight gate, not the rendering strategy.

### 8.2 Is the Design Correct?

**Yes**, the design is correct in principle. The issue is **execution incomplete**.

The design says (lines 158-165):
> "The fully fixed version removes the expensive allocation pattern entirely, so the low-heap abort path becomes a rare fallback instead of part of normal page rendering."

Current state: Low-heap IS still part of normal rendering because the content is too large and takes too long to stream under interference.

---

## 9) Cross-Reference Summary

| Document | Section | Status | Finding |
|-----------|---------|--------|---------|
| [ESP32_STRING_HEAP_WEBPAGE_RESOLUTION_2026_05_12.md](../../esp32common/docs/systemworks/ESP32_STRING_HEAP_WEBPAGE_RESOLUTION_2026_05_12.md) | Section 1-5 | ✅ Implemented | Streaming renderer infrastructure working |
| [ESP32_STRING_HEAP_WEBPAGE_RESOLUTION_2026_05_12.md](../../esp32common/docs/systemworks/ESP32_STRING_HEAP_WEBPAGE_RESOLUTION_2026_05_12.md) | Section 6 | ⚠️ Partially | Preflight gate fixed; render stability NOT tuned |
| [LVGL_PREFSM_TFT_BASELINE_RECOVERY_REPORT_2026_05_12.md](../../esp32common/docs/systemworks/LVGL_PREFSM_TFT_BASELINE_RECOVERY_REPORT_2026_05_12.md) | Section 8.2 Simplify D | ❌ Not Applied | "Use bounded degrade, not binary render abort" - still using binary strategy |
| [LVGL_PREFSM_TFT_BASELINE_RECOVERY_REPORT_2026_05_12.md](../../esp32common/docs/systemworks/LVGL_PREFSM_TFT_BASELINE_RECOVERY_REPORT_2026_05_12.md) | Section 9.2 | ❌ Incomplete | "Bounded degrade policy" not implemented; heap doesn't trigger content reduction |

---

## 10) Proposed Resolution Path

### Phase 1: Stabilize Current Implementation (Short term)
1. ✅ Keep preflight guard (already fixed 2026-05-14)
2. ⚠️ Monitor for HTTP timeouts during normal operation
3. Log dashboard render duration and success rate

### Phase 2: Reduce Content Size (Medium term)
1. Minify dashboard CSS (~3 KB target)
2. Split dashboard script into core + dynamic loads
3. Validate render time < 15 seconds

### Phase 3: Implement Adaptive Strategy (Long term)
1. Detect CONSTRAINED pressure → send minimal page
2. Implement lazy loading for non-critical content
3. Move large scripts to `/static/` served separately
4. Reduce full-page render to < 5 seconds

### Decision Alignment Update (Post-Review)

To remove ambiguity with later timing analysis (Section 19), the current agreed direction is:
1. Implement event-driven TX scheduler blocking (replace 1 ms idle polling).
2. Keep fixed values unchanged for:
   - ESP-NOW worker poll (100 ms)
   - `connect_confirm_ack` retry (250 ms)
   - LED sync retry (500 ms, max 3)
3. Apply adaptive policies for:
   - TX min-gap scaling (Section 19.5)
   - NO_MEM retry base/cooldown tiers (Section 19.6)

---

## 11) Conclusion

The 2026-05-14 preflight fix was **correct but incomplete**. It successfully removed false-positive 503 rejections, allowing requests to proceed. However, the actual render process now exposes the true issue: **the dashboard content is too large and takes too long to stream under concurrent radio/network activity.**

The design document's Section 6 correctly identified this issue and proposed a phased approach to resolution. The current implementation only completed Phase 1 (preflight gate). Phases 2-3 (render content optimization and adaptive strategy) remain pending.

**Immediate action required:** Implement Phase 2 content size reduction to bring dashboard render time under 15 seconds and prevent HTTP timeout failures.

---

## 12) Additional Investigation (Single-Device Constraint Kept)

**Constraint confirmed:** Keep ESP-NOW + MQTT + HTTP/Webserver + LVGL on the same receiver device.

Based on Espressif documentation and examples, this is feasible **if architecture is aligned for bounded latency and bounded heap churn**. The problem is not "all services on one device" by itself; the problem is that long-running HTTP render work is currently coupled to radio-critical periods.

### 12.1 What Espressif Explicitly Recommends (Relevant to this case)

From ESP-NOW API guide/examples (`docs/en/api-reference/network/esp_now.rst`, `examples/wifi/espnow/main/espnow_example_main.c`):

1. ESP-NOW send/recv callbacks run in a high-priority Wi-Fi context.
2. Do **not** perform lengthy work in callbacks.
3. Push data to queue, process in lower-priority task.
4. Send interval should be paced (next send after previous callback/ACK path).

From HTTP server API (`docs/en/api-reference/protocols/esp_http_server.rst`, async handlers example):

1. HTTP server task is configurable (`task_priority`, `stack_size`, socket limits).
2. Long operations should use async request handling (`httpd_req_async_handler_begin()` + worker queue).
3. Keep at least one free socket for quick routes during long responses.
4. Apply backpressure: return 503 when async workers are saturated.

This maps exactly to our failure mode.

---

## 13) Section 6 Completion Gap — What Is Still Missing

Section 6 is only complete when we have all four of these in place:

1. **Priority separation** between radio-critical work and heavy HTTP render work.
2. **FSM hot-path simplification** (callbacks and worker fast path kept minimal).
3. **Adaptive response chunking** (chunk size and pacing tied to pressure state).
4. **Load acceptance criteria** proving no 30s+ stall under mixed ESP-NOW/MQTT/HTTP load.

Current code already has strong foundations:
- `webserver.cpp` uses explicit HTTP server priority/stack/socket config ✅
- `page_generator.cpp` has pressure-aware mid-render guards ✅
- Preflight gate fix is in place ✅

Missing pieces are architectural tuning, not basic infrastructure.

---

## 14) Recommended Architecture Changes (Keep Current Device Structure)

### 14.1 Split ESP-NOW and HTTP into Explicit Work Classes

Implement three classes of work, not one monolithic "ESP-NOW worker + HTTP render":

1. **Radio Fast Path Task (highest app priority)**
   - Handles only: ACK bookkeeping, queue forwarding, timestamp updates.
   - No JSON, no logging bursts, no config writes, no page-side notifications.
   - Target: sub-millisecond per event.

2. **FSM/Control Task (medium priority)**
   - Handles: state transitions, connection policy, retries, recovery escalation.
   - Receives events from fast path via queue.
   - Rate-limited transitions and consolidated logging.

3. **HTTP Render Worker(s) (lower priority than radio control path)**
   - Handles long responses and chunk sends.
   - Uses async request pattern and bounded worker pool.
   - If worker queue full, reply 503 quickly.

### 14.2 Suggested Priority Model

Keep Wi-Fi internals untouched; align app-level tasks as follows:

| Task | Priority | Rationale |
|------|----------|-----------|
| ESP-NOW fast path | 3 | Never blocked by render work |
| HTTP server main task | 2 | Accept requests, quick routes |
| ESP-NOW FSM/control | 2 | Deterministic, but not callback-fast |
| HTTP async render workers | 1 | Heavy work, yield-friendly |
| MQTT/background | 1 | Non-critical vs radio path |

Important: avoid same-priority long loops without yielding; keep explicit `vTaskDelay()`/queue waits in all non-fast-path loops.

### 14.3 Reduce ESP-NOW FSM Complexity (Without Breaking Behavior)

Current receiver path uses both `UnifiedLinkFsm` and `RxStateMachine`. Keep both only if they have distinct responsibilities; otherwise reduce duplicated transition churn.

Recommended simplification:

1. **Single source of truth for link phase.**
   - `UnifiedLinkFsm` holds authoritative phase/recovery.
   - `RxStateMachine` becomes adapter/policy wrapper (no duplicate phase ownership).

2. **Collapse receiver mirror phases for runtime hot path.**
   - Effective runtime phases:
     - `DISCOVERY`
     - `HANDSHAKE`
     - `LINK_UP`
     - `RECOVERY` (L1/L2 as sub-reason, not separate heavy logic branches)

3. **Defer non-critical actions out of transition edge.**
   - Config sync, catalog requests, and non-critical telemetry push into paced tick job.
   - Transition function should mutate state and enqueue deferred actions only.

4. **Bound transition rate.**
   - Ignore duplicate transition requests within short debounce window (e.g. 250-500 ms).
   - Prevent transition storms during transient congestion.

### 14.4 Split HTTP Responses into Smaller Blocks (Adaptive)

Current fixed chunking in `page_generator.cpp`:
- `kHttpChunkSendBytes = 1024`
- fixed `vTaskDelay(2)` before send

Replace with pressure-adaptive policy:

| Pressure | Chunk size | Inter-chunk delay | Behavior |
|----------|------------|-------------------|----------|
| NORMAL | 384-512 B | 1-2 ms | Fast enough, lower burst allocation |
| CONSTRAINED | 128-256 B | 4-8 ms | Prevent allocator spikes |
| CRITICAL | abort/fallback | n/a | Existing safety logic |

Also split payload semantics:
1. Send minimal HTML + critical CSS first (first paint).
2. Defer non-critical JS/CSS to separate `/static/*` requests.
3. Cache static assets aggressively (`ETag` / `Cache-Control`) so repeat visits avoid 40 KB resend.

This preserves local dashboard on-device while reducing one-shot render stall duration.

### 14.5 Use Async Handler Pattern for Long Pages

Adopt ESP-IDF async handler model for dashboard route:

1. Main URI handler creates async request copy.
2. Push to render-worker queue.
3. Immediate return to HTTP server task.
4. Worker streams chunks.
5. Always call `httpd_req_async_handler_complete()` on success/error.

Backpressure rule (mandatory):
- If no worker available, return `503 Busy` immediately.
- Do not wait indefinitely for worker slot.

---

## 15) Additional Solutions Worth Implementing

1. **Bound logging in hot paths**
   - Keep warning logs, but rate-limit repetitive per-chunk logs during pressure to avoid extra heap churn.

2. **Content pre-segmentation table in flash**
   - Pre-split large rawliteral blocks at build time into fixed fragments (e.g. 192/256 bytes) and iterate fragment table.
   - Avoid repeated pointer arithmetic and large stage spans.

3. **Per-request budget guard**
   - Hard deadline (e.g. 12-15 s for dashboard).
   - On overrun, terminate cleanly with small fallback page rather than dragging to TCP timeout.

4. **Socket/resource tuning for mixed load**
   - Keep `lru_purge_enable = true` (already set).
   - Ensure `max_open_sockets` always leaves headroom for non-dashboard routes.

5. **MQTT cooperation mode during active dashboard render**
   - Temporarily reduce MQTT burst/retry aggressiveness while a long HTTP render worker is active.
   - Not disconnect; just reduce contention.

---

## 16) Updated Action Plan to Fully Complete Section 6

### Step A — Task Separation (required)
1. Introduce async render worker queue for heavy pages.
2. Keep HTTP server task responsive for quick routes/API.
3. Separate ESP-NOW fast path vs FSM/control processing queues.

### Step B — FSM Simplification (required)
1. Remove duplicated phase ownership between `UnifiedLinkFsm` and `RxStateMachine`.
2. Debounce repetitive transitions.
3. Move non-critical transition-side work to deferred paced tick.

### Step C — Adaptive Chunking + Asset Split (required)
1. Replace fixed 1024-byte chunk with pressure-adaptive chunk sizes.
2. Split dashboard into critical-first + deferred static assets.
3. Add client cache headers for static content.

### Step D — Verification (completion gate)
Run 30-minute mixed-load soak with:
- ESP-NOW telemetry active
- MQTT connected and publishing/subscribing
- Dashboard loaded repeatedly

Pass criteria:
1. No `ESP_ERR_HTTPD_RESP_SEND` on dashboard route.
2. No repeated `ESP_ERR_ESPNOW_NO_MEM` bursts during HTTP render.
3. P95 dashboard first paint < 5 s; full load < 12 s.
4. Section 6 checklist can be marked complete only after these metrics pass.

---

## 17) Post-Connect ESP-NOW Behavior: Why It Still Consumes Priority/Attention

This section answers the specific question:

> "After devices are connected, why does ESP-NOW still need so much traffic and attention?"

### 17.1 Short Answer

After link-up, ESP-NOW is no longer doing channel acquisition, but it is still the **real-time control and telemetry transport**.  The ongoing load is not just discovery packets; it is a mix of:

1. keepalive/health traffic,
2. telemetry stream traffic,
3. receiver ACK traffic,
4. periodic metadata/config synchronization,
5. queue/scheduler maintenance and recovery logic.

So, "connected" does not mean idle. It means transition from discovery-heavy to steady-state control-plane + data-plane traffic.

### 17.2 What Traffic Continues After CONNECTED

Based on current code paths:

#### A) Heartbeat path (always-on keepalive)
- TX sends heartbeat every `TimingConfig::HEARTBEAT_INTERVAL_MS` (10,000 ms).
   - Source: transmitter `heartbeat_manager.cpp`
- RX receives heartbeat and enqueues `HEARTBEAT_ACK` back via `EspnowTxScheduler::send()`.
   - Source: `rx_heartbeat_manager.cpp`

This is low packet rate, but it is **connection-critical** (state/liveness watchdog input).

#### B) Telemetry data path
- TX data sender task runs at `TimingConfig::ESPNOW_SEND_INTERVAL_MS` (2,000 ms).
   - Source: transmitter `data_sender.cpp`
- RX handles `msg_data`, `msg_battery_status`, `msg_charger_status`, `msg_inverter_status`, `msg_system_status`, etc.
   - Source: receiver `espnow_runtime_routes.cpp` + `espnow_runtime_messages.cpp`

This is the main ongoing ESP-NOW payload stream in normal operation.

#### C) Version/runtime beacon path
- TX version beacon manager sends periodic beacons (comment says every 30s) plus event-triggered beacons on MQTT/Ethernet/config changes.
   - Source: transmitter `version_beacon_manager.cpp`

Even in steady state, this adds periodic control traffic.

#### D) Receiver post-connect retries (bounded, but bursty when data missing)
- `REQUEST_DATA` retries until power data is confirmed.
- Config section retries (`network`, `mqtt`) until cached.
- Type catalog retries (versions/battery/inverter/interfaces) every 3s up to bounded attempts.
- LED sync retries up to 3 attempts.
   - Source: receiver `run_lcd_project_tick()` in `espnow_runtime.cpp`
   - Policies: `rx_catalog_retry_policy.h`, `rx_led_sync_policy.h`

These are not permanent at high rate when system is healthy, but when any section is missing/stale they produce short bursts.

### 17.3 Why It Still Takes "Attention" (CPU/Priority), Even at Modest Packet Rates

Traffic volume alone is not the whole issue. There are architectural CPU-side factors:

1. **ESP-NOW callbacks execute in high-priority Wi-Fi context**
    - Espressif guidance: callbacks must stay very short; enqueue and defer.
    - This makes ESP-NOW latency-sensitive by design.

2. **RX worker is a frequent loop**
    - Worker polls queue with 100 ms timeout and runs multiple maintenance checks every cycle (`connection_manager.process_events()`, `connection_handler.tick()`, `RxHeartbeatManager::tick()`, stale checks).

3. **TX scheduler worker is very active**
    - Internal loop uses 1 ms idle delay when no queue item and has retry/backoff logic under `ESP_ERR_ESPNOW_NO_MEM`.
    - Source: `espnow_tx_scheduler.cpp`.

4. **High-priority control semantics**
    - ACK/heartbeat/control packets are intentionally prioritized over data by scheduler queue class.
    - This is correct for link stability, but can contend with HTTP under heap pressure.

So the observed "attention" is mostly from **real-time guarantees + scheduler cadence + retry/recovery machinery**, not raw packet flood.

### 17.4 Discovery Is Mostly Not the Culprit After CONNECTED

Discovery task behavior:
- Once peer is connected, announcement task suspends itself (`suspended_ = true`) and idles with 1s delay.
- It is not continuously transmitting probes in healthy connected state.

Therefore, post-connect contention is primarily heartbeat/data/control sync + task architecture, not ongoing discovery scan traffic.

### 17.5 Practical Implication for This Project

The receiver must treat ESP-NOW as a **continuous real-time subsystem** after connect, not a startup-only subsystem.

That means HTTP rendering must be engineered to coexist with:
- control-plane ACK deadlines,
- periodic heartbeats,
- 2s data cadence,
- occasional config/catalog retry bursts,
- NO_MEM recovery/backoff behavior.

### 17.6 Concrete Tuning Actions (Post-Connect Focus)

1. Keep callback paths minimal (already mostly queue-based; maintain this discipline).
2. Reduce non-essential work in RX worker loop under NORMAL connected state (avoid heavy per-cycle work when no message).
3. Keep scheduler priorities, but ensure HTTP long responses are async-worker based and chunk-adaptive.
4. Add temporary suppression/coalescing of optional ESP-NOW retries while a long HTTP render is in progress.
5. Instrument per-message-type rates and CPU time to prove where post-connect load is coming from (heartbeat/data/retry split).

### 17.7 Bottom-Line Answer

After connection, ESP-NOW is still active because it is carrying ongoing telemetry + keepalive + state synchronization, and because its control paths are intentionally latency-sensitive.  
So yes, it still requires priority and attention — but with proper task separation and adaptive HTTP behavior, this is architecturally manageable on the current single-device design.

---

## 18) What Repeats Faster Than 2 Seconds (Exact Periodicity)

You are correct that the two obvious periodic items are:
- heartbeat every 10 s,
- telemetry every 2 s.

The investigation shows the **higher-frequency repeaters** are mostly scheduler/control loops and retry paths.

### 18.1 Steady-state repeaters (always running while connected)

1. **ESP-NOW worker loop cadence:** ~100 ms
   - `kWorkerPollMs = 100`.
   - This loop runs `connection_manager.process_events()`, `connection_handler.tick()`, `RxHeartbeatManager::tick()` and stale checks.

2. **ESP-NOW TX scheduler worker idle poll:** ~1 ms
   - If no queue item, task sleeps 1 ms and loops.
   - This is not packet TX every 1 ms; it is scheduler wake cadence.

3. **Inter-frame spacing in TX scheduler:** 8 ms (current receiver config)
   - Configured in runtime startup (`inter_frame_delay_ms = 8`).
   - This is the minimum spacing between queued sends when traffic exists.

### 18.2 Conditional high-frequency repeaters (only when specific states/events occur)

4. **`connect_confirm_ack` retry cadence:** 250 ms
   - Retry interval `kConnectConfirmAckRetryIntervalMs = 250`.
   - Active only during connect-confirm handshake recovery.

5. **LED sync retry cadence:** 500 ms
   - `RxLedSyncPolicy::RETRY_INTERVAL_MS = 500`.
   - Max 3 attempts, then stops.

6. **Discovery/control ACK min-gap:** 150 ms
   - Scheduler policy for `msg_ack`, `msg_connect_confirm`, `msg_connect_confirm_ack` is `min_gap_ms = 150`.
   - Applies only when these message types are queued.

7. **Heartbeat ACK min-gap:** 80 ms
   - `msg_heartbeat_ack` send policy `min_gap_ms = 80`.
   - In practice bounded by heartbeat arrival rate, but scheduler allows this minimum.

8. **Probe min-gap:** 250 ms
   - `msg_probe` scheduler min gap is 250 ms.
   - Mostly relevant in discovery/recovery, not stable connected runtime.

9. **NO_MEM retry backoff steps (TX immediate send):** 8/16/24/… ms
   - With `retry_base_delay_ms = 8`, retries are spaced by `8 * (attempt index)` ms.
   - Active only when `ESP_ERR_ESPNOW_NO_MEM` occurs.

10. **NO_MEM failure cooldown:** 40 ms
   - After failed send with NO_MEM, scheduler backs off 40 ms before further work.

### 18.3 Important interpretation

Most sub-2-second entries above are **control-loop cadence and retry mechanics**, not continuous high-rate telemetry payload traffic.

So the system behaves like this:
- payload plane: mostly 2 s and 10 s,
- control/scheduler plane: 1 ms to 500 ms cadence,
- burst retry plane: 8 ms to 250 ms when link pressure or handshake/recovery is active.

That is why ESP-NOW still appears "busy" after link-up even when primary payload intervals are relatively slow.

---

## 19) Timing Change Investigation: What Happens If We Relax These Cadences?

This section evaluates the items from 18.1/18.2 and the likely effects of changing them.

### 19.1 The 1 ms TX scheduler idle loop (is it excessive?)

Current behavior:
- TX worker checks four priority queues.
- If all empty, it sleeps 1 ms and loops.

Decision:
- **Adopt event-driven blocking** for TX worker wake-up (queue-set or task notification), with a bounded timeout fallback for housekeeping.

Implementation intent:
1. Block until new TX work arrives (or timeout).
2. On timeout, run lightweight housekeeping and block again.
3. Keep queue priority order unchanged once awakened.

Expected effect:
- Near-zero idle wake churn.
- Preserves low-latency send path when queue receives new work.
- Reduces scheduler pressure during HTTP rendering windows.

### 19.2 ESP-NOW worker poll (100 ms)

Current behavior:
- `xQueueReceive(..., 100 ms timeout)` then maintenance tick.

Assessment:
- This is already moderate.
- Message arrivals wake immediately; 100 ms mainly bounds idle tick cadence.

Potential change:
- 100 ms → **150-200 ms**.

Expected effect:
- Lower periodic maintenance overhead.
- Slightly slower retry/tick-driven actions (`try_send_pending_connect_confirm_ack`, policy tick checks).
- No major impact on 10 s heartbeat timeout logic.

Decision:
- **Keep at 100 ms (no change).**

### 19.3 `connect_confirm_ack` retry at 250 ms

Current behavior:
- Retries every 250 ms, max 20 retries.

Assessment:
- This is handshake-critical and intentionally fast.
- Increasing interval directly slows recovery/connection establishment under loss.

If changed:
- 250 ms → 500 ms doubles worst-case handshake completion time.

Decision:
- **Keep at 250 ms (no change).**

### 19.4 LED sync retry at 500 ms (max 3)

Assessment:
- Short-lived, bounded, non-critical.
- Not a major sustained load source.

If changed:
- 500 ms → 1000 ms reduces burstiness slightly.
- Only affects cosmetic LED convergence speed.

Decision:
- **Keep at 500 ms, max 3 retries (no change).**

### 19.5 TX policy min gaps (80/150/250 ms)

Current examples:
- heartbeat ACK min gap: 80 ms
- discovery/control ACK min gap: 150 ms
- probe min gap: 250 ms

Assessment:
- These are burst limiters; raising them reduces burst pressure.
- Over-raising can hurt reconnect speed and reliability in noisy links.

Decision:
- **Use adaptive scaling (not static changes).**

Recommended adaptive policy:

Pressure signals (evaluate every 250 ms):
1. `heap_low`: free internal heap below configured low-water mark.
2. `http_heavy`: dashboard response in progress for > 300 ms.
3. `tx_backlog`: scheduler queued depth above threshold (e.g., > 8).

State machine:
- `NORMAL`: no active pressure signals.
- `CONSTRAINED`: any 1 signal active for >= 500 ms.
- `CRITICAL`: any 2+ signals active for >= 500 ms, or repeated send failures.

Per-state min-gap multipliers:
- `NORMAL`: 1.00x (unchanged)
- `CONSTRAINED`: 1.25x
- `CRITICAL`: 1.50x

Category application:
- Heartbeat ACK (`80 ms`): cap at `1.25x` (max 100 ms) to protect liveness.
- Connect/control ACK (`150 ms`): allow up to `1.50x` (225 ms).
- Probe/non-essential (`250 ms`): allow up to `2.00x` in CRITICAL (500 ms).

Decay/hysteresis:
- Step down one state only after 2 s stable (no qualifying pressure).
- Return to `NORMAL` only after 5 s stable.

### 19.6 NO_MEM retry base delay (currently 8 ms) and cooldown (40 ms)

Assessment:
- These are pressure recovery controls, not normal throughput controls.
- Increasing delay reduces allocator hammering but increases delivery latency.

Decision:
- **Use adaptive NO_MEM backoff escalation and recovery.**

Recommended adaptive policy:

Counters/windows:
1. Track `no_mem_events` in rolling 1 s and 5 s windows.
2. Track `consecutive_no_mem` for immediate escalation.

Adaptive tiers:
- `TIER0` (normal): base `8 ms`, cooldown `40 ms`.
- `TIER1` (moderate pressure): enter when `no_mem_events_1s >= 3` or `consecutive_no_mem >= 2`; base `12 ms`, cooldown `60 ms`.
- `TIER2` (high pressure): enter when `no_mem_events_1s >= 6` or `no_mem_events_5s >= 20`; base `16 ms`, cooldown `80 ms`.

Per-message-class handling:
- Control/liveness class: clamp to max `TIER1` to avoid over-slowing handshake traffic.
- Data/non-critical class: allow full `TIER2`.

Recovery:
- Drop one tier after 2 s with `no_mem_events_1s == 0`.
- Return to `TIER0` after 5 s stable.
- Reset `consecutive_no_mem` on first successful send.

---

## 20) Practical Tuning Order (Lowest Risk First)

1. **Implement event-driven TX worker blocking** (replace 1 ms idle poll).
2. Keep fixed timers unchanged for 19.2, 19.3, 19.4.
3. Implement adaptive TX min-gap scaling (19.5) with hysteresis.
4. Implement adaptive NO_MEM backoff tiers (19.6) with per-message-class caps.

This order preserves connection robustness while reducing unnecessary wakeups and allocator pressure.

---

## 21) Net Finding

Yes — some high-frequency items can be relaxed, and the most obvious candidate is the TX worker’s 1 ms idle poll.  
However, several sub-second timings are handshake/recovery safeguards and should not be broadly slowed without adaptive logic.  

The best outcome is:
- event-driven wake for TX queues,
- adaptive backoff under pressure,
- keep critical handshake timers fast.

---

## 22) Full-Document Completeness Re-Review (2026-05-14)

This section captures the explicit gap-check pass across Sections 1-21.

### 22.1 Gaps Found and Closed in This Re-Review

1. **Short-term timeout wording conflict resolved**
   - Previous wording mixed "reduce timeout" with "increase `send_wait_timeout`".
   - Updated to explicit "increase timeout budget (temporary mitigation)".

2. **Decision consistency clarified**
   - Added alignment block in Section 10 so roadmap matches Section 19 decisions.

### 22.2 Coverage Check (No Missing Major Topic Areas)

All required areas for this investigation are now present:

1. **Symptom and reproduction timeline** (Sections 1-2)
2. **Root cause chain and classification** (Sections 3-6)
3. **Design-vs-implementation gap analysis** (Sections 4, 13)
4. **Architecture remediation under single-device constraint** (Sections 12, 14)
5. **Actionable implementation plan** (Sections 16, 20)
6. **Post-connect ESP-NOW behavior explanation** (Sections 17-18)
7. **Timing decision analysis and selected policy** (Section 19)
8. **Final finding and operational direction** (Section 21)

### 22.3 Implementation-Ready Checklist (to avoid execution gaps)

Before marking this investigation "implemented", ensure these artifacts exist in code/reports:

1. TX scheduler event-driven blocking implementation complete.
2. Adaptive min-gap state machine implemented with hysteresis.
3. Adaptive NO_MEM tiers implemented with class-aware caps.
4. Metrics added for:
   - render first-paint/full-load latency,
   - queue depth and worker utilization,
   - per-message-type ESP-NOW rates,
   - NO_MEM event rates (1s/5s windows).
5. 30-minute mixed-load soak report attached with pass/fail against Section 16 criteria.

### 22.4 Remaining Open Risks (Known, Not Missing)

These are known operational risks, not documentation gaps:

1. Temporary timeout increase can mask stalls if left permanently enabled.
2. Over-aggressive adaptive scaling can degrade reconnect latency on noisy links.
3. Async HTTP workers require strict completion/cleanup discipline to avoid leaks.

Status after re-review: **No major missing sections or unresolved document-structure gaps remain.**

---

## 23) Phased Implementation Plan (Safety-First, Section 6 Included)

This implementation plan is explicitly ordered so we do **not** build on unsafe foundations.

### 23.1 Foundation Rule (Non-Negotiable)

**Section 6 completion is a hard gate.**

No architectural optimization phase is considered "done" unless the Section 6 completion criteria are verified under mixed load:
1. No `ESP_ERR_HTTPD_RESP_SEND` on dashboard route.
2. No repeated `ESP_ERR_ESPNOW_NO_MEM` bursts during HTTP render.
3. P95 dashboard first paint < 5 s; full load < 12 s.
4. 30-minute mixed-load soak passes.

If any gate fails, implementation returns to the relevant earlier phase.

### 23.2 Phase 0 — Baseline + Instrumentation (Required Before Changes)

Objective: establish trustworthy baseline and observability.

Deliverables:
1. Add/verify metrics for:
   - dashboard first-paint and full-load durations,
   - ESP-NOW queue depth and send latency,
   - NO_MEM rates (1 s / 5 s windows),
   - heap low-water and largest-free-block trends.
2. Capture baseline run artifacts:
   - 10-minute normal operation trace,
   - 30-minute mixed-load trace.

Exit criteria:
1. Metrics are stable and timestamp-aligned.
2. Baseline report saved and linked in project docs.

### 23.3 Phase 1 — Section 6 Stabilization (Unsafe Foundation Fix)

Objective: complete the previously incomplete Section 6 work before adding new adaptive complexity.

Implementation scope:
1. Ensure async HTTP long-route handling is used for dashboard path.
2. Enforce bounded worker queue and immediate `503 Busy` on saturation.
3. Keep existing fixed timers for 19.2/19.3/19.4 unchanged.
4. Keep render guardrails active (pressure-aware abort/fallback).
5. Validate static asset split strategy readiness (`critical-first` + deferred/static paths).

Mandatory verification (Section 6 gate):
1. Re-run 30-minute mixed-load soak.
2. Verify all four Section 6 completion rules in 23.1.

Exit criteria:
1. Section 6 is objectively marked complete with measured evidence.
2. Only then proceed to Phase 2.

### 23.4 Phase 2 — TX Scheduler Foundation Change (19.1)

Objective: remove high-frequency idle churn safely.

Implementation scope:
1. Replace 1 ms idle polling with event-driven blocking (queue set/task notification).
2. Keep bounded timeout fallback (housekeeping only).
3. Preserve queue-class priority semantics.

Validation:
1. Compare idle wakeups before/after.
2. Confirm no regression in ACK/control latency envelope.
3. Re-run mixed-load soak.

Exit criteria:
1. Idle wake churn significantly reduced.
2. No handshake reliability regression.

### 23.5 Phase 3 — Adaptive TX Min-Gap Policy (19.5)

Objective: reduce burst pressure during constrained periods without harming liveness.

Implementation scope:
1. Implement pressure signals (`heap_low`, `http_heavy`, `tx_backlog`).
2. Implement state machine (`NORMAL` / `CONSTRAINED` / `CRITICAL`) with hysteresis.
3. Apply class-aware multiplier caps as defined in Section 19.5.

Validation:
1. Force constrained/critical scenarios and verify transitions.
2. Confirm heartbeat and connect/control ACK behavior remains within bounds.
3. Re-run mixed-load soak.

Exit criteria:
1. Fewer burst-related stalls/no_mem spikes.
2. No reconnect or liveness degradation.

### 23.6 Phase 4 — Adaptive NO_MEM Backoff Policy (19.6)

Objective: reduce allocator hammering under pressure and improve coexistence.

Implementation scope:
1. Implement rolling-window counters and `consecutive_no_mem` tracking.
2. Implement `TIER0`/`TIER1`/`TIER2` backoff behavior.
3. Enforce class-aware caps (control/liveness max `TIER1`, data can use `TIER2`).
4. Implement tier recovery/decay logic.

Validation:
1. Simulate allocator pressure and verify tier escalation/recovery.
2. Confirm control traffic remains responsive.
3. Re-run mixed-load soak.

Exit criteria:
1. Reduced repeated NO_MEM bursts.
2. Stable dashboard rendering under mixed load.

### 23.7 Phase 5 — Content and Delivery Optimization (Follow-On)

Objective: lock in durable performance margin.

Implementation scope:
1. Finalize critical-first dashboard payload.
2. Move/defer non-critical assets to cacheable static routes.
3. Add/verify cache headers (`ETag`, `Cache-Control`).

Validation:
1. Browser repeat-load shows reduced transfer and faster first paint.
2. Mixed-load soak still passes.

Exit criteria:
1. Performance goals met with safety margin.
2. Regression test checklist updated.

### 23.8 Rollback and Safety Policy

At any phase, if one of the following occurs, rollback to previous stable phase and re-run verification:
1. Reappearance of `ESP_ERR_HTTPD_RESP_SEND` under nominal mixed load.
2. Increased handshake failures or prolonged reconnection.
3. Sustained `CRITICAL` pressure state without recovery.

### 23.9 Definition of Done (Program-Level)

This implementation program is complete only when:
1. Section 6 remains passing across repeated runs.
2. Phase 2-5 changes are active and verified.
3. Evidence pack (metrics + soak logs + summary) is attached to this investigation and cross-linked from architecture docs.

### 23.10 Implementation Status (Started)

The implementation has now started with the scheduler foundation change from Phase 2.

Completed in code:
1. TX worker idle behavior moved from 1 ms busy polling to event-driven blocking via queue-set wake.
2. Bounded idle timeout fallback retained for housekeeping wake-ups.
3. Cadence-defer retry loop no longer uses fixed 1 ms retry sleep; defer delay is now bounded by remaining gap/idle block timeout.

Legacy/redundant code removed in this completed stage:
1. Removed legacy 1 ms empty-queue polling loop in TX worker.
2. Removed legacy 1 ms cadence-defer sleep loop.
3. Added robust init-failure cleanup for scheduler queues/set to avoid stale partial init state.

Validation status:
1. Build validation passed for `espnowreceiver_LCD` (waveshare target).
2. Scheduler source compiles clean with no reported file-level errors.

Next gated step:
- Return to Phase 1/Section 6 completion verification under mixed load before enabling additional adaptive policies.

### 23.11 Section 6 Remediation Implementation (This Change Set)

Completed now:
1. Added guarded content streaming API (`send_page_content_chunk`) so page content callbacks no longer bypass render guardrails.
2. Added adaptive HTTP chunk policy in render path:
   - NORMAL: smaller, faster chunks,
   - CONSTRAINED/low-internal-heap: smaller chunks with increased pacing delay.
3. Migrated page content generators to guarded path across dashboard/system/config/monitor/OTA/tooling pages.
4. Removed redundant inline monitor2 HTML body copy in handler and reused shared content provider.
5. Converted remaining page content/script/style hot paths from request-time `String` builders to static `const char*` or chunked emitters (including network config emitter path and specs inline scripts).
6. Rebuilt `espnowreceiver_LCD` waveshare target after remediation updates; compile/link/package succeeded.

Legacy/redundant code removed in this stage:
1. Removed direct `httpd_resp_send_chunk` callback bypass pattern from page handlers/content emitters covered above.
2. Removed duplicate monitor2 inline HTML body implementation (single content source retained).

Still pending to fully close Section 6:
1. Complete mixed-load soak verification against Section 6 gate criteria and attach evidence pack.

### 23.12 Field Reconnect Loop Investigation (2026-05-14, TX/RX logs)

Scope note:
- This finding is **not** a Section 6 render-path defect.
- Section 6 remediation addressed HTTP/page rendering pressure paths.
- The field symptom here is a reconnect control-plane loop in ESP-NOW handshake/ACK flow.

Observed field pattern:
1. TX repeatedly scans and reports `ack=0` even though probes are sent successfully.
2. RX logs repeated `ESP_ERR_ESPNOW_NO_MEM` on `msg_ack` (`type=1 len=6`).
3. RX logs `NO_MEM trigger ignored while state=0`, so escalation never runs.

Code evidence:
1. TX scan failure counters (`probes_ok`, `ack`, `ack_per_100_probe`) are emitted from:
   - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp`
2. RX ACK send failure log and `type=1` mapping (`msg_ack`) flow through:
   - `esp32common/espnow_common_utils/espnow_tx_scheduler.cpp`
   - `esp32common/espnow_transmitter/espnow_common.h`
3. RX NO_MEM recovery is intentionally gated to CONNECTING/CONNECTED only:
   - `esp32common/espnow_common_utils/rx_connection_handler.cpp`
4. Shared route helper currently clears the receiver probe callback:
   - `esp32common/espnow_common_utils/rx_route_registry.cpp`
   - `probe_config.on_probe_received = nullptr;`
5. Project runtime sets a valid probe callback before calling shared route registration:
   - `espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp`
   - shared helper then overrides/nulls it.

Root-cause synthesis for the loop:
1. RX receives probe traffic but loses ACK sends under NO_MEM pressure.
2. The probe callback path that should drive connection ingress (`on_probe_received` → PEER_FOUND/CONNECTING progression) is neutralized by shared-route callback overwrite.
3. Connection manager remains in `IDLE` (`state=0`) for long windows.
4. In `IDLE`, NO_MEM escalation is explicitly ignored by policy.
5. System enters self-sustaining loop: failed ACKs + no state promotion + no recovery escalation.

Design alignment verdict:
1. **Section 6 (render/heap remediation):** implementation remains aligned with the current scope of this document.
2. **Reconnect architecture intent:** current code is **not fully aligned** with the reconnect design intent documented in:
   - `esp32common/docs/systemworks/ESPNOW_RECONNECT_ALIGNMENT_REVIEW_AND_RECOMMENDATIONS_2026_05_13.md`
   - specifically the unresolved ACK reliability path under NO_MEM and retry semantics.
3. Additional mismatch identified in this field investigation:
   - shared route registration currently discards project probe-ingress callback wiring, which undermines expected reconnect state progression.

Conclusion:
- The observed reconnect loop is due to reconnect-path control-plane behaviour, not a failure of Section 6 render design.
- Yes, this indicates a current **code-vs-reconnect-design misalignment** that is outside (and in addition to) Section 6 closure.

### 23.13 Reconnect Alignment Implementation (Code Changes Applied)

Objective:
- Restore reconnect ingress behaviour expected by the original reconnect design: probe reception must drive receiver connection progression, and discovery ACK NO_MEM pressure must be visible to recovery policy.

Implemented:
1. Shared route registration now preserves project-provided probe callbacks instead of nulling them:
   - `esp32common/espnow_common_utils/rx_route_registry.cpp`
   - removed callback overwrite behavior that cleared `on_probe_received`/project wiring semantics.
2. Shared route registration now chains ACK send-result callback rather than replacing it:
   - throttle accounting retained,
   - project-level callback is still invoked.
3. LCD runtime now reports discovery ACK NO_MEM failures into RX connection handler pressure path:
   - `espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp`
   - wires `on_ack_send_result` -> `ReceiverConnectionHandler::on_ack_send_pressure("discovery ACK enqueue no-mem")` on failure.

Alignment effect:
1. Probe ingress callback path is no longer dropped during shared route setup.
2. Receiver has reconnect-state progression signal from probes as designed.
3. Discovery ACK enqueue pressure is now surfaced to the same recovery policy path used by heartbeat ACK pressure.

Still required to claim full reconnect closure:
1. Capture fresh TX/RX paired logs proving reconnection succeeds under the previous failure scenario.
2. Validate no prolonged `state=0` + `NO_MEM trigger ignored` loop persists under repeated reconnect cycles.
3. Re-run mixed-load soak and append evidence pack.

### 23.14 Reconnect Alignment Build Validation (2026-05-14)

Build verification completed after the reconnect alignment code changes.

Results:
1. `espnowreceiver_LCD` (environment `waveshare_esp32s3_lcd7_lvgl`) built successfully.
2. `espnowreceiver_2` (environment `lilygo-t-display-s3_tft`) built successfully.
3. No compile/link failures were introduced by the callback-preservation and ACK-pressure wiring changes.

Notes:
1. Existing framework warning from `esp32-hal-uart.c` (`return` with no value in non-void function) still appears and is unchanged by this change set.
2. Runtime reconnection validation is still required (paired TX/RX logs) before marking reconnect closure complete.

