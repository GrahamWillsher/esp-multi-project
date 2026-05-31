# Receiver Memory Health Assessment
**Date:** 2026-05-27  
**Observed values:** internal free = **20.7 KB**, largest internal block = **7.5 KB**, fragmentation estimate = **63.8%**  
**Scope:** receiver-side `/debug` Memory Health card on the ESP32 receiver device hosting the web UI (`espnowreceiver_2` / `espnowreceiver_LCD`)

---

## Executive summary

These numbers are **not an immediate crash signature**, but they **are a real concern** for this codebase.

The key issue is **not the raw free-heap number alone**. The stronger signal is the combination of:
- only **20.7 KB** free internal heap,
- only **7.5 KB** available as the **largest contiguous block**, and
- **63.8% fragmentation**.

In this project, that means:
- the receiver is **well above** the renderer's hard abort floor,
- but it is already in a state where **medium contiguous allocations can fail**,
- and some web/API paths are already at or past their own safety thresholds.

So the correct interpretation is:

> **This is degraded-but-still-running, not healthy.**

If these figures appear only briefly during a burst (page load, SSE, OTA, MQTT reconnect, LVGL activity), that is manageable. If they are the **steady-state baseline**, I would treat them as **action-needed**.

---

## Which device this applies to

The `/debug` Memory Health panel is receiver-local.

Relevant code:
- `espnowreceiver_LCD/lib/webserver_lcd/pages/debug_page_content.cpp`
- `espnowreceiver_LCD/lib/webserver_lcd/pages/debug_page_script.cpp`
- `espnowreceiver_LCD/src/memory/memory_sampler.cpp`
- `espnowreceiver_2/lib/webserver/pages/debug_page_content.cpp`
- `espnowreceiver_2/lib/webserver/pages/debug_page_script.cpp`
- `espnowreceiver_2/src/memory/memory_sampler.cpp`

The values are sampled from the receiver's own internal heap and PSRAM using `heap_caps_get_free_size(...)` and `heap_caps_get_largest_free_block(...)`, not from the transmitter.

---

## Comparison against current code thresholds

### 1) Memory sampler thresholds

From:
- `espnowreceiver_LCD/src/memory/memory_sampler.h`
- `espnowreceiver_2/src/memory/memory_sampler.h`

Current thresholds:
- internal largest-block warning: **32 KB**
- PSRAM largest-block warning: **128 KB**
- fragmentation warning: **0.70**

Assessment against your figures:
- **largest block = 7.5 KB** → **well below** the internal largest-block warning threshold of **32 KB**
- **fragmentation = 63.8%** → below the sampler's hard warning threshold of **70%**, but still high enough to be operationally significant

Conclusion:
- By the sampler's own design, the **largest-block metric is already in warning territory**.
- The fragmentation estimate is **elevated**, even if it has not crossed the sampler's warn line yet.

---

### 2) Debug UI policy thresholds

From:
- `espnowreceiver_LCD/lib/webserver_lcd/pages/debug_page_script.cpp`
- `espnowreceiver_2/lib/webserver/pages/debug_page_script.cpp`

UI thresholds:
- fragmentation warn: **50%**
- fragmentation critical: **70%**
- internal largest warn: **32 KB**
- internal largest critical styling also begins below **32 KB**

Assessment:
- **63.8% fragmentation** is already **above the UI warn threshold**
- **7.5 KB largest block** is **deep into the UI's warning/critical presentation band**

Conclusion:
- Even the UI policy treats these numbers as **meaningfully degraded**, not nominal.

---

### 3) Web/API admission thresholds

From:
- `espnowreceiver_LCD/lib/webserver_lcd/api/api_middleware.cpp`
- `espnowreceiver_2/lib/webserver/api/api_middleware.cpp`

Heavy read endpoint gate:
- free heap must be at least **60 KB**
- largest 8-bit block must be at least **16 KB**

Assessment:
- **20.7 KB free** → below **60 KB**
- **7.5 KB largest block** → below **16 KB**

Immediate implication:
- heavy endpoints such as `/api/get_event_logs` are expected to be **rejected with 503** under these conditions.

Conclusion:
- For the API layer, these values are already beyond the project's own admission threshold for heavier work.

---

### 4) Streaming renderer exists, but migration is partial

From:
- `espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp`
- `espnowreceiver_2/lib/webserver/common/page_generator.cpp`

Current behavior:
- the shared page generator now includes a real streaming path via `send_rendered_page_streaming(...)`
- that renderer emits HTML in chunks with `httpd_resp_send_chunk(...)` and serves shared helpers from `/static/helpers.js`
- the migration is not complete across the page set, so some pages and helper paths can still involve legacy `String` assembly

Assessment:
- **20.7 KB free** is still low enough that any remaining `String`-heavy page or helper work can become fragile under concurrency
- **7.5 KB largest block** means the allocator still has limited room for request-time work, whether the page is streamed or not

Conclusion:
- the shared renderer infrastructure is moving in the right direction
- the remaining risk is the mixed state: streamed pages are safer, but any remaining `String`-heavy or legacy page paths still compete for the same fragmented internal heap
- the practical question is now migration completeness, not whether streaming support exists at all

---

### 5) Setup health gate

From:
- `espnowreceiver_2/src/main.cpp`

Boot-time check:
- `heap_ok` requires `ESP.getFreeHeap() > 32768`

Assessment:
- **20.7 KB** is below that boot-time health target.

Interpretation:
- this does **not** mean the running device is broken,
- but it does show the codebase itself considers **32 KB+** the healthier startup posture.

---

## Why the 7.5 KB largest block matters more than the 20.7 KB total

This codebase already documents that the real bottleneck is **internal heap allocability**, not just total memory volume.

See:
- `../systemworks/ESP32_STRING_HEAP_WEBPAGE_RESOLUTION_2026_05_12.md`
- `../systemworks/RECEIVER_DASHBOARD_RENDER_FAILURE_INVESTIGATION_2026_05_14.md`

The practical meaning of your figures is:
- there may be **20.7 KB total** free,
- but no single allocation bigger than about **7.5 KB** can succeed,
- and repeated transient allocations from HTTP, WiFi, MQTT, JSON, and UI work will keep fighting over a fragmented pool.

For this project, that is exactly the pattern that causes:
- HTTP slowdowns,
- 503 admission rejections on heavier endpoints,
- occasional `NO_MEM` behavior in concurrent subsystems,
- and long-tail instability during mixed web + radio + MQTT activity.

---

## What is actually causing the pressure

The pressure is not coming from MQTT chunking itself. The chunking helps transport, but the receiver still does the work that matters for heap pressure:

1. **Event-log ingest still allocates and parses JSON on the receiver.**
   - `espnowreceiver_2/src/mqtt/mqtt_client.cpp`
   - `espnowreceiver_LCD/src/mqtt/mqtt_client.cpp`
   - `handleEventLogs(...)` uses a `DynamicJsonDocument(6144)` and parses the incoming payload before handing it to the cache.

2. **The event-log cache still merges batches into a full in-memory vector.**
   - `espnowreceiver_2/lib/webserver/utils/transmitter_event_log_cache.cpp`
   - `espnowreceiver_LCD/lib/webserver_lcd/utils/transmitter_event_log_cache.cpp`
   - `store_event_logs(...)` clears `is_new` on the full cache, scans for duplicates, inserts/replaces entries, and then sorts the entire vector after every merge.

3. **The receiver still keeps a full cached history for UI/API consumers.**
   - The paged `/api/event_logs_page` path reduces what is sent to the browser, but the backing cache remains a live in-memory collection.
   - That means the cost is shifted, not removed: the UI is lighter, but the cache maintenance still touches the whole dataset.

4. **There is still a full-snapshot copy path in the cache API.**
   - `get_event_logs_snapshot(...)` copies the entire vector into an output buffer.
   - Even if the browser now prefers paging, that API remains a potential source of extra contiguous allocation pressure when used.

The practical conclusion is:
- **primary pressure source:** event-log cache maintenance and JSON parsing during MQTT ingest
- **secondary pressure source:** any remaining full-snapshot consumers of the cache
- **not the main cause by itself:** MQTT transport chunking

So the fragmentation warning is really saying:
> the receiver is spending heap on event-log ingest/merge work fast enough to keep the internal allocator split up, even when the browser only asks for a page of results.

---

## Is this cause for concern?

## Short answer

**Yes — moderate to high concern.**

## More precise answer

### If these numbers were captured during a short burst
Examples:
- loading `/debug`
- active SSE session
- OTA upload
- MQTT reconnect window
- LVGL/display burst

Then the result is:
- **concerning, but explainable**
- not automatically a bug by itself
- acceptable only if the heap recovers quickly afterward

### If these numbers are the steady idle baseline
Then the result is:
- **definitely cause for concern**
- a sign the receiver is running with too little contiguous internal memory headroom
- likely to surface as intermittent web/API fragility under normal use

---

## Most likely operational impact at these values

### High likelihood

1. **Heavy read endpoints may reject**
   - `/api/get_event_logs` is the clearest example because the middleware explicitly gates it.

2. **Medium temporary allocations may fail intermittently**
   - Anything that needs a contiguous internal block larger than ~7.5 KB is already in danger.

3. **Render-time behavior becomes more sensitive to concurrent activity**
   - Page rendering, WiFi, MQTT, and LVGL all compete for the same internal heap.

### Moderate likelihood

4. **Slow page loads or occasional page-send failures under mixed load**
   - especially on the larger LCD receiver routes already discussed in prior investigations.

5. **Increased long-tail instability rather than immediate repeatable failure**
   - i.e. the system may mostly work, but fail in edge timing windows.

### Lower likelihood at the exact numbers shown

6. **Immediate renderer abort purely from low heap**
   - current renderer aborts only below **4 KB** internal heap.
- So these values are not yet at the project’s "drop everything now" floor.

---

## Findings

1. **The free-heap value alone is not catastrophic.**
   - 20.7 KB is low, but above the renderer's hard-abort and warning floors.

2. **The largest-block value is the strongest red flag.**
   - 7.5 KB is below the sampler warning threshold (32 KB) and below the heavy-endpoint admission threshold (16 KB).

3. **The fragmentation estimate confirms the pool is already degraded.**
   - 63.8% is not yet over the sampler's 70% threshold, but it is already above the UI warn threshold and consistent with a badly fragmented allocator state.

4. **The codebase is in a mixed rendering state, not purely string-based.**
   - The shared renderer already streams pages in chunks, but not every receiver page has fully moved onto that path yet.
   - Your sample is still low enough that any remaining `String`-heavy or legacy page path can fail unpredictably.

5. **These figures are especially relevant on the receiver, not the transmitter.**
   - The receiver hosts the web UI, HTTP server, and display/LVGL workload that make internal heap fragmentation matter most.

---

## Recommendations

### 1) Treat 7.5 KB largest block as a real warning signal

Operationally, I would treat:
- **largest internal block < 16 KB** as **action-needed**, and
- **largest internal block < 8 KB** as **high risk** for mixed web/API activity.

That aligns better with actual runtime fragility than relying on free heap alone.

---

### 2) Capture whether this is baseline or burst-only

Use `/api/memory_samples` or the `/debug` history to determine:
- idle free heap
- idle largest block
- burst free heap
- burst largest block
- recovery time back to baseline

What matters most is whether the device recovers after the burst.
If the receiver stays near **7.5 KB largest block** after the request ends, the condition is much more serious.

---

### 3) Finish migrating remaining pages to the streaming renderer

Files:
- `espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp`
- `espnowreceiver_2/lib/webserver/common/page_generator.cpp`
- receiver page handlers and helpers that still build large HTML bodies directly in `String`

Suggested improvement:
- move remaining page handlers onto `send_rendered_page_streaming(...)` where practical
- keep reusable CSS/JS in flash-resident helpers and shared assets
- keep any remaining `String` use limited to small, bounded fragments or streaming callbacks

Why:
- your sample shows the allocator is already fragmented enough that any remaining non-streamed page assembly is materially riskier

---

### 4) Add an explicit UI/operator threshold for largest block < 16 KB

The Memory Health UI already shows the values, but the real operational gate in middleware is **16 KB** for heavy endpoints.

Suggested change:
- make the UI explicitly call out **largest block < 16 KB** as "API-restricted / fragmented"

That would make the page's warning language match the code's real behavior.

---

### 5) Keep reducing internal-heap churn on larger pages

The existing docs already identified the main architectural issue:
- internal heap pressure during large-page delivery under concurrent load

Continue prioritising:
- smaller scripts/styles per page
- more cached/static assets
- less inline dynamic assembly
- avoidance of medium transient internal-heap allocations in hot web paths

This is especially important on `espnowreceiver_LCD`, where display/LVGL work and web rendering coexist.

---

### 6) Avoid treating PSRAM as proof that this is fine

For this codebase, the bottleneck is still **internal** heap.
A receiver can have PSRAM available and still be unhealthy for HTTP/network work if internal heap is fragmented.

---

## Concrete solution plan

Below is the implementation plan I would recommend for this codebase, ordered by value and risk.

### Priority 0 — tighten protection around the current failure band

These are the fastest, lowest-risk changes because they do not redesign the architecture; they make the current runtime behave more honestly when the heap is already degraded.

#### A) Make the page renderer react earlier to fragmentation

Files:
- `espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp`
- `espnowreceiver_2/lib/webserver/common/page_generator.cpp`

Current issue:
- adaptive chunk slowdown only starts when free heap `< 16 KB` or largest block `< 4 KB`
- your sample (`20.7 KB free`, `7.5 KB largest`) falls outside that policy even though it is already fragile

Recommended change:
- add an intermediate degraded mode when:
   - largest block `< 12 KB`, or
   - fragmentation estimate / proxy indicates a badly split heap
- in that mode:
   - start conservatively, for example `512 -> 384` or `512 -> 256`, rather than immediately forcing the smallest chunk size
   - only add a small inter-chunk delay if testing shows it improves allocator recovery
   - emit a specific log such as `render degraded_heap mode=fragmented`

Important constraint from current code review:
- this must be validated against render duration, because the current renderer already logs slow responses above `2000 ms`
- making chunks too small or delays too large can **increase total page render time** and may worsen the very `ESP_ERR_HTTPD_RESP_SEND` timeout class already seen in prior investigations
- so this should be implemented as a **measured tuning change**, not a blanket “smaller chunks are always safer” rule

Expected benefit:
- fewer medium transient allocations during page send
- less allocator churn during mixed web + LVGL + MQTT activity
- better alignment between actual risk and render policy

#### B) Align UI warnings with middleware reality

Files:
- `espnowreceiver_LCD/lib/webserver_lcd/pages/debug_page_script.cpp`
- `espnowreceiver_2/lib/webserver/pages/debug_page_script.cpp`

Recommended change:
- add an explicit warning state when largest internal block `< 16 KB`
- label it as something like `API-restricted / fragmented`
- optionally show a small note that heavy read endpoints may be rejected under this condition

Expected benefit:
- operator-facing diagnostics match the actual `api_middleware.cpp` behavior
- less ambiguity when `/debug` says the device looks "ok" but endpoints still return `503`

#### C) Raise pressure visibility in logs

Files:
- `espnowreceiver_LCD/src/memory/memory_sampler.cpp`
- `espnowreceiver_2/src/memory/memory_sampler.cpp`

Recommended change:
- keep the existing `32 KB` warning threshold, but add a second, stronger warning tier for:
   - largest internal block `< 16 KB`
   - largest internal block `< 8 KB`
- keep fragmentation warnings, but log the combined state when both largest-block and fragmentation are poor

Important constraint from current code review:
- the sampler currently logs on **every sample below threshold**, and burst mode runs every `1000 ms`
- adding more warning tiers without hysteresis, edge detection, or rate limiting will create log spam and can itself add noise during the exact stress windows we are trying to diagnose

Safer implementation shape:
- only emit the stronger tier when entering a worse band
- or emit reminder logs at a bounded cadence
- keep the raw samples in `/api/memory_samples`, but avoid turning every burst window into repetitive warning traffic

Expected benefit:
- much better signal when the device has entered the specific risk band that causes intermittent web/runtime failures

---

### Priority 1 — move more transient work off internal heap and into PSRAM where safe

This is the best next step if the receiver board really has stable PSRAM available, which the project documentation and build posture indicate it does.

#### A) Establish an explicit memory-placement policy

Files to document/apply against:
- `espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp`
- `esp32common/webserver_common_utils/src/spec_page_layout.cpp`
- receiver display/LVGL buffer allocation paths

Recommended rule set:
- **must stay internal RAM:**
   - HTTP server control structures
   - WiFi/ESP-IDF/network stack allocations
   - small hot-path control objects
   - task stacks unless a task is explicitly proven safe elsewhere
- **prefer PSRAM:**
   - large web formatting scratch buffers
   - large JSON documents that are not latency-critical
   - display/image/decode buffers
   - catalog/spec-page staging buffers
   - reusable page/script assembly buffers if any still exist

Important constraint from current code review:
- PSRAM is available on the LCD receiver build, but not every allocation path will move there automatically
- `String`, `std::string`, and ordinary heap-backed helpers will not become PSRAM-safe just because the board has PSRAM enabled
- the recommendation therefore applies to **explicitly managed buffers** only, not to a broad search-and-replace of heap usage

Expected benefit:
- internal heap reserved for network/control paths
- fewer avoidable medium allocations in the most fragile memory tier

#### B) Convert medium temporary web buffers to PSRAM-backed scratch

Files:
- `esp32common/webserver_common_utils/src/spec_page_layout.cpp`
- any page/helper still building medium formatting buffers before chunk send

Current evidence:
- the shared spec layout already supports `use_psram` for larger scratch buffers
- it only allocates PSRAM scratch when explicitly requested or when the buffer exceeds the local `2048` byte stack buffer

Recommended change:
- extend that same pattern to any remaining page helpers that still create medium temporary buffers
- prefer a reusable PSRAM scratch allocator or a bounded PSRAM-backed work buffer instead of ad hoc internal-heap temporaries
- do **not** blindly force small buffers into PSRAM; for small or very hot buffers, the added complexity and slower access may not help

Important constraint from current code review:
- avoid a design that performs `ps_malloc`/`free` on every request for many unrelated small buffers, because that can simply move fragmentation pressure into PSRAM and add latency
- where PSRAM is used, prefer bounded, long-lived, or reusable buffers over high-churn per-request allocation

Expected benefit:
- reduced internal heap fragmentation from medium-lived formatting buffers

#### C) Keep large static page assets out of request-time heap work

Files:
- `espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp`
- large page content/script providers under `lib/webserver_lcd/pages/`
- equivalent `_2` page providers where relevant

Recommended change:
- continue the existing streaming/static approach
- split oversized scripts/styles where useful
- extend the current cacheable-static pattern where practical
- avoid any regression back to request-time `String` assembly for large content

Important constraint from current code review:
- the renderer already serves common helpers from `/static/helpers.js`, so the next step should be to **extend the existing mechanism**, not add a second overlapping delivery strategy
- asset splitting should be driven by measured page size and render duration, not by fragmentation concerns alone

Expected benefit:
- fewer repeated internal-heap transients per page load

Important note:
- PSRAM should be used to relocate **safe large transient buffers**
- it should **not** be used as an excuse to ignore internal-heap fragmentation in the web/network layer

---

### Priority 2 — reduce the sources of fragmentation, not just the symptoms

#### A) Reuse buffers instead of allocating per request

Recommended change:
- introduce reusable bounded work buffers for recurring JSON/page formatting paths
- prefer fixed-capacity buffers or long-lived pools over repeated allocate/free cycles

Important constraint from current code review:
- any shared reusable buffer must be request-safe and task-safe
- do not introduce a single global scratch buffer that can be touched by overlapping page renders, API handlers, or callbacks unless it is explicitly serialized

Why:
- fragmentation is usually driven by allocation churn, not one-time static cost

#### B) Audit remaining `String` growth in hot web paths

Recommended targets:
- any remaining large `String += ...` assembly in request handlers
- medium-sized dynamic JSON formatting on hot endpoints
- helper paths that reserve too late or repeatedly grow buffers

Why:
- even after moving to streaming delivery, `String` growth in helper code can still consume internal heap and split the allocator

#### C) Reduce concurrency during expensive renders

Recommended change:
- during known expensive page renders, consider temporarily soft-throttling non-essential polling or lowering optional UI refresh activity
- ensure burst-mode monitoring remains active during these windows so the effect is visible in `/debug`

Important constraint from current code review:
- this should stay narrowly scoped to non-essential work
- do **not** suppress heartbeat, MQTT liveness, or core control-plane tasks just to make a page load look better, because that would trade memory symptoms for correctness and connection regressions

Why:
- some failures here are caused by allocator contention between web send, WiFi, MQTT, and display work rather than by any one page alone

---

## Recommendation safety review

After comparing the recommendations above against the current codebase, the main guardrails are:

1. **Do not blindly reduce HTTP chunk size**
   - smaller chunks can reduce per-send memory pressure, but they also increase send count and can lengthen total render duration
   - on this codebase that matters because slow renders have already been associated with `ESP_ERR_HTTPD_RESP_SEND`

2. **Do not add more sampler warnings without hysteresis or rate limiting**
   - burst mode samples every `1000 ms`
   - stronger tiers are useful, but only if they avoid repetitive per-sample warning spam

3. **Do not assume PSRAM solves internal-heap problems automatically**
   - the board has PSRAM enabled, but ordinary heap-backed objects do not automatically migrate there
   - PSRAM changes should target explicit, bounded, non-latency-critical buffers

4. **Do not force small or hot buffers into PSRAM by default**
   - the existing shared spec renderer already uses a `2048` byte stack buffer and only moves larger work to PSRAM
   - that is a good pattern to preserve rather than replacing everything with PSRAM allocation

5. **Do not introduce unsafe shared scratch buffers**
   - reusable buffers only help if they are request-safe and task-safe

6. **Do not throttle correctness-critical background work**
   - any concurrency reduction should apply only to optional/non-essential activity, not to heartbeat, connection maintenance, or other core runtime responsibilities

These constraints are why the safest first changes remain:
- clearer operator warnings
- rate-limited stronger sampler signals
- carefully measured renderer-threshold tuning
- explicit PSRAM use only for safe medium/large transient buffers

---

## Implementation phases

If I were turning this into code work, I would treat it as four phases.

### Phase 1 — finish the streaming migration

Goal:
- complete the move from legacy `String`-assembled page bodies to the shared streaming renderer where practical

Work items:
- migrate remaining receiver page handlers onto `send_rendered_page_streaming(...)`
- keep large HTML emission out of request-time `String` assembly
- preserve the measured chunk/send behavior already used by the shared renderer and spec-page helpers

Acceptance criteria:
- no remaining general receiver page handler depends on legacy full-page `String` assembly when a streaming route exists
- obsolete page-rendering helpers removed by the migration have been deleted from the codebase
- the codebase scan shows the remaining page paths match the streaming model described in this document

Why this comes first:
- the codebase already has streaming infrastructure in place, so finishing migration reduces the biggest avoidable heap churn before adding more memory policy changes
- `memoryhealth_page.cpp` already uses the streaming path, and `GenericSpecsPage::send_formatted_page(...)` already feeds chunked responses through `send_chunked_html_response(...)`

Phase close rule:
- at the end of this phase, remove all old/legacy/redundant page-rendering code that is no longer needed
- then re-check the updated codebase against this document
- if the codebase and document do not align, the phase is not complete and the work must continue until they do

### Phase 2 — improve operator visibility

Goal:
- make the `/debug` memory signals match the real allocator risk bands

Work items:
- update both `debug_page_script.cpp` files so largest-block `< 16 KB` is called out clearly
- add stronger sampler bands for `< 16 KB` and `< 8 KB` largest-block conditions
- keep rate limiting / hysteresis so the warning path does not become log spam

Acceptance criteria:
- the `/debug` UI explicitly highlights the `largest block < 16 KB` operator warning band
- the memory sampler emits stronger signals without per-sample spam
- the UI thresholds and sampler thresholds agree with the risk bands described in this document

Why this comes second:
- the streaming migration lowers risk, but the operator still needs clearer signals when the remaining heap is fragile

Phase close rule:
- remove old/legacy/redundant debug-policy code that is superseded by the new thresholds
- re-check the codebase against this document at the end of the phase
- if the implementation and this document disagree, keep iterating until they match

### Phase 3 — move safe transient work to PSRAM

Goal:
- keep internal RAM for networking and latency-sensitive control work while relocating safe medium/large buffers

Work items:
- inspect remaining page/helper scratch buffers and move safe medium/large temporaries to PSRAM-backed storage
- prefer explicit, bounded, reusable buffers rather than high-churn per-request `ps_malloc` usage
- preserve the existing rule that small or hot buffers should stay internal unless a measured benefit is proven

Acceptance criteria:
- internal-heap pressure is reduced by relocating only the safe non-hot buffers identified in this phase
- small or latency-sensitive buffers remain internal unless the codebase proves a safe alternative
- the resulting memory behavior still matches the warning bands and operational thresholds described in this document

Why this comes after streaming:
- once the heaviest page assembly paths are streamed, it becomes easier to identify which remaining scratch buffers are truly worth moving

Phase close rule:
- remove old/legacy/redundant buffer-allocation paths that are replaced by the new PSRAM policy
- verify the resulting memory behavior against this document before closing the phase
- if the implementation drifts from the report, rework the phase until alignment is restored

### Phase 4 — audit the remaining hot paths

Goal:
- remove any residual allocation churn in the busiest routes

Work items:
- remove any remaining repeated `String` growth or per-request medium buffer churn in the largest routes
- verify that page response time, allocator recovery, and `ESP_ERR_HTTPD_RESP_SEND` behavior do not regress

Acceptance criteria:
- the busiest routes no longer contain unnecessary per-request allocation churn
- page response timing remains within the measured envelope for the receiver
- allocator recovery improves or remains stable after the earlier phases are applied

Why this is last:
- it is the broadest cleanup step, and it should be informed by the results of the earlier migration and buffer-placement work

Phase close rule:
- delete any remaining old/legacy/redundant hot-path allocation code that is no longer required
- compare the finished codebase back to this document one final time
- if anything still contradicts the report, continue the cleanup loop until the codebase and document agree

## Execution rule

A phase is only complete when all four conditions are true:
- the code changes for that phase are implemented
- obsolete code removed by the phase has been deleted from the codebase
- the updated codebase has been checked back against this document
- the document and implementation match with no unresolved outliers

Do not advance to the next phase until the current phase satisfies this rule.

---

## Recommended target architecture

The stable end-state should be:

- internal RAM primarily reserved for:
   - HTTP/network stack activity
   - small control-plane objects
   - latency-sensitive runtime work
- PSRAM used intentionally for:
   - display/image/LVGL bulk buffers
   - large or medium web formatting scratch buffers
   - reusable non-latency-critical JSON/page work buffers
- page rendering becomes more conservative when largest contiguous internal heap shrinks, even if total free heap still looks acceptable
- `/debug` reports a state that matches what middleware and render code will really do

---

## Bottom-line solution recommendation

The best concrete solution is **not** just "use more PSRAM" and it is **not** just "lower thresholds".

The correct fix is a combination of:
- **earlier degraded-mode behavior in the page renderer**
- **clearer operator warnings around largest-block health**
- **moving safe medium/large transient buffers to PSRAM**
- **reducing allocation churn in hot web paths**

If only one **safe** change is done first, start with the **debug-page warning update** so the UI reflects the real `largest block < 16 KB` risk band.

If the first **behavioral** change is being made, the highest-value next step is completing the **streaming renderer migration** for the remaining pages, but it should still be treated as a measured change rather than a blind rewrite to more dynamic allocation.

At the end of every completed phase:
- remove all old/legacy/redundant code that the phase made obsolete
- re-run the codebase review against this document
- do not mark the phase complete until the implementation matches the document exactly

---

## Suggested practical target bands

These are not all current enforced thresholds, but they better reflect the observed risk profile of this codebase.

### Healthy
- free internal heap: **> 32 KB**
- largest internal block: **> 24 KB**
- fragmentation estimate: **< 50%**

### Caution
- free internal heap: **16–32 KB**
- largest internal block: **8–24 KB**
- fragmentation estimate: **50–70%**

### High risk
- free internal heap: **< 16 KB**
- largest internal block: **< 8–16 KB**
- fragmentation estimate: **> 70%**

Your sample fits roughly here:
- free heap = **caution**
- largest block = **high risk**
- fragmentation = **caution / near-high-risk**

Overall combined state: **cause for concern**.

---

## Sanity check / outliers

The report now matches the current codebase better, but there are still a few important outliers to keep in mind:

1. **`espnowreceiver_2` appears to be the main migration gap.**
   - In the current scan, I found the streaming renderer implementation in the shared page generator, but no direct `send_rendered_page_streaming(...)` consumers inside `espnowreceiver_2/lib/webserver/**`.
   - That means the shared streaming infrastructure exists, but the `_2` receiver page set still looks more migration-complete than actually fully migrated.

2. **`espnowreceiver_LCD` is partially ahead of `_2`.**
   - I found `memoryhealth_page.cpp` using `send_page_content_chunk(...)` and `send_rendered_page_streaming(...)`.
   - This is a real streaming consumer, but it is only one page, so it should not be read as proof that the whole LCD page set is migrated.

3. **The shared spec-page path is already chunked, but it is a special-case path.**
   - `GenericSpecsPage::send_formatted_page(...)` feeds `WebserverCommonSpecLayout::send_chunked_html_response(...)`.
   - That is a genuine streamed path, but it applies to spec-style pages rather than proving all general web pages are on the same model.

4. **The memory report is still conservative about legacy `String` paths.**
   - Even with streaming in place, any remaining page helpers that build large `String` content can still become the weak spot under fragmentation.
   - So the document is safe to treat as a migration plan, not a claim that the full UI stack has already finished the transition.

## Final conclusion

The observed receiver memory-health figures are **not yet a hard-failure state**, but they are **absolutely low enough to matter** in this codebase.

The biggest concern is the **7.5 KB largest internal block**, because it means the receiver's internal heap is already fragmented enough to interfere with heavier web/API and mixed-load behavior even though total free heap is still above the renderer's critical floor.

If this was a brief burst sample, monitor recovery. If it is the normal resting level, I recommend treating it as a real issue and tightening both runtime thresholds and web-path memory pressure accordingly.

---

## Breadcrumb Diagnostics Enhancement (2026-05-28)

### Implementation Summary

A feature-aware breadcrumb system has been added to improve memory health diagnostics when heap pressure is detected. This addresses the need to identify **which feature and request phase** was active during heap pressure, rather than just knowing that pressure occurred.

### New Diagnostic Fields

The `/api/pressure_gate_stats` endpoint now returns:
- `last_probe_item` — Feature category (e.g., "event_logs", "cell_monitor", "dashboard", "memory_health", "pressure_gate", "other")
- `last_probe_phase` — Request phase (e.g., "dispatch_pre", "heap_admission", "dispatch_post", "stats_snapshot")
- `last_probe_uri` — The API endpoint being accessed (e.g., "/api/event_logs_page")
- `last_probe_breadcrumb` — Complete context string (e.g., "api:event_logs:dispatch_post:/api/event_logs_page")
- `last_probe_internal_free` — Internal heap bytes free at probe time
- `last_probe_internal_largest` — Largest contiguous internal block at probe time
- `last_probe_internal_frag_est` — Heap fragmentation estimate at probe time

### UI Display Updates

The `/debug` Memory Health page now shows:
- **Likely source:** Changed from vague section name to "Feature: event_logs" or "Feature: cell_monitor"
- **Attribution (reason):** Changed to "Phase: dispatch_post" or "Phase: heap_admission"
- **Endpoint:** Shows actual probe URI instead of rejection URI
- **Cause:** Displays the full breadcrumb for root-cause analysis
- **Code gate:** Shows probe phase instead of generic "ApiMiddleware::enforce_heap_admission"
- **Heap @ trigger:** Shows probe heap metrics with fragmentation estimate

**Example display transformation:**
```
OLD (Before):
   Likely source: mqtt_client.cpp::handleEventLogs
   Attribution: restricted now without a rejected heavy endpoint yet
   Cause: (empty)

NEW (After):
   Likely source: Feature: event_logs
   Attribution: Phase: dispatch_post
   Cause: api:event_logs:dispatch_post:/api/event_logs_page
   Heap: 20.7 KB free, 7.5 KB largest (63.8% frag) [probe]
```

### Files Updated

**Frontend (UI display logic):**
- [espnowreceiver_2/lib/webserver/pages/memoryhealth_page_script.cpp](espnowreceiver_2/lib/webserver/pages/memoryhealth_page_script.cpp#L204-L256)
- [espnowreceiver_LCD/lib/webserver_lcd/pages/memoryhealth_page_script.cpp](espnowreceiver_LCD/lib/webserver_lcd/pages/memoryhealth_page_script.cpp#L204-L256)

**Backend (API breadcrumb generation):**
- [espnowreceiver_2/lib/webserver/api/api_middleware.cpp](espnowreceiver_2/lib/webserver/api/api_middleware.cpp) — Routes mapped to features, multi-phase probing integrated
- [espnowreceiver_2/lib/webserver/api/api_middleware.h](espnowreceiver_2/lib/webserver/api/api_middleware.h) — New `PressureGateStats` fields
- [espnowreceiver_2/lib/webserver/api/api_modular_handlers.cpp](espnowreceiver_2/lib/webserver/api/api_modular_handlers.cpp#L150) — Breadcrumb fields exposed in JSON
- (Same files in espnowreceiver_LCD variant)

### Build Validation

✅ **espnowreceiver_2 (TFT variant)**: Successfully compiled in 101s
✅ **espnowreceiver_LCD (Waveshare LCD7)**: Successfully compiled in 140s

Both builds show no errors; warnings are pre-existing framework-level config redefinitions.

### Diagnostic Capabilities Enabled

Operators and developers can now:
1. **Identify the culprit feature** — "Was it event_logs, cell_monitor, or dashboard causing pressure?"
2. **Pinpoint the request phase** — "Did it fail during pre-dispatch, at admission gate, or post-dispatch?"
3. **See the exact endpoint** — "/api/event_logs_page" or "/api/cell_data"
4. **Correlate patterns** — "Event logs always trigger pressure during dispatch_post phase"
5. **Root-cause efficiently** — "7.5 KB fragmentation occurred when event_logs was in dispatch_post phase"

### Impact

This enhancement transforms the memory health diagnostics from:
- ❌ Generic "restricted now" signals
- ❌ Source code file paths that are hard to correlate
- ❌ No context about what the request was doing

Into:
- ✅ Feature + phase + URI breadcrumb trail
- ✅ Heap metrics captured at diagnostic point
- ✅ Actionable root-cause information for each pressure event
- ✅ UI-friendly display that operators can understand immediately

---
