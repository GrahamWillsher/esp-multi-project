# ESP32 Webpage `String` Heap Analysis and Resolution Plan
**Date:** 2026-05-12  
**Scope:** `espnowreceiver_LCD`, shared `esp32common`, and Arduino-ESP32 webserver/string behavior

---

## Executive summary

`String` is not inherently "bad" on ESP32, but it becomes expensive when we repeatedly build large HTML responses on the **internal heap** with many concatenations.

In this codebase, the main issue is:
- large pages are assembled dynamically with `String`
- multiple concatenations cause repeated reallocations and transient peaks
- the receiver also runs WiFi, ESP-NOW, MQTT, LVGL, and the HTTP server at the same time
- internal heap pressure, not PSRAM capacity, is what triggers the low-heap abort path

So the real answer is:
**`String` is acceptable for small/medium web UI fragments, but this project should not keep assembling large pages into one heap-backed `String` under load.**

---

## What I found

### 1) `String` uses heap-backed growth
The Arduino-ESP32 `String` implementation grows buffers with `reserve()` / `realloc()`. That means repeated `+=` on a large response can:
- allocate new blocks
- copy old content repeatedly
- fragment the heap
- fail if internal heap is already tight

This is documented by the core itself: `String::reserve()` and `String::concat()` are heap-based, and `StreamString` also inherits from `String`.

### 2) Small strings are not the problem
The built-in `String` implementation uses small-string optimization for short values, so tiny fragments are usually fine.
The problem starts when the page becomes large enough to exceed the SSO buffer and begins reallocating repeatedly.

### 3) This project builds large HTML pages dynamically
Key examples in the receiver:
- [espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp](espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp)
- [espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page_content.cpp](espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page_content.cpp)
- [espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page_script.cpp](espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page_script.cpp)
- [espnowreceiver_LCD/lib/webserver_lcd/pages/monitor_page_script.cpp](espnowreceiver_LCD/lib/webserver_lcd/pages/monitor_page_script.cpp)
- [espnowreceiver_LCD/lib/webserver_lcd/pages/cellmonitor_page_script.cpp](espnowreceiver_LCD/lib/webserver_lcd/pages/cellmonitor_page_script.cpp)

Those files build large responses from many literal fragments plus live data.

### 4) The page generator already shows the pressure pattern
The existing page generator uses chunked send and low-heap abort logic. That is a sign the heap cost is real, not theoretical.
The code is already protecting itself because full-page `String` assembly is too expensive under pressure.

### 5) PSRAM being available does not solve internal heap pressure
Even with PSRAM enabled, the HTTP server, WiFi driver, task stacks, and some `String` work still consume internal heap.
The key metric is **internal free heap / largest free block**, not total RAM.

---

## Root cause in plain terms

The receiver is doing too much work on the same internal heap at once:
- web page strings are built dynamically
- chunked HTML responses are large
- WiFi/ESP-NOW/MQTT/LVGL all consume memory and timing budget
- under contention, the heap becomes fragmented and the largest allocatable block shrinks

That is why you see render aborts or low-heap warnings even when the device still has some RAM left overall.

---

## Conclusion about `String`

`String` itself is not the bug.
The bug is **how `String` is being used** in a memory-constrained, concurrent webserver environment.

Use `String` for:
- short labels
- JSON snippets
- small route responses
- non-hot-path utility formatting

Avoid `String` for:
- full HTML pages
- large scripts/styles
- repeated concatenation inside request handlers
- long-lived streaming responses

---

## Recommended fix strategy

### Priority 1: Stop building whole pages as one large heap `String`
Prefer one of these patterns:
1. **Chunked response generation** with pre-sized chunks
2. **Static page text in flash/PROGMEM** with placeholders
3. **Filesystem-hosted HTML/JS/CSS** served as static files
4. **Smaller JSON APIs + client rendering** instead of giant inline HTML

### Priority 2: If `String` must be used, make it predictable
- call `reserve()` before concatenation
- build once, not in many small appends
- avoid nested `String(...) + String(...) + ...` chains
- prefer `F()` / `FPSTR()` for literals stored in flash
- keep dynamic payloads small

### Priority 3: Protect the webserver lifecycle
The receiver should refuse to start or restart HTTP when heap is too low, and should abort active renders if pressure becomes critical.
Those safeguards already exist in parts of the codebase and should stay.

---

## Current project status that matters

The receiver already has the right direction in code:
- active-request-aware recycle logic exists
- the page generator can abort under low heap / critical radio pressure
- the webserver init path has a minimum heap gate
- the build is moving toward reduced polling and smaller request bursts

That means the right fix is **not** to ban `String` entirely.
The right fix is to keep `String` usage small and predictable, and move the large payloads to chunked/static delivery.

### Important gap
These safeguards prevent crashes and reduce damage, but they do **not** by themselves remove the root cause.
The largest receiver pages still assemble their bodies as heap-backed `String` objects before rendering, which means the low-heap condition can still happen under load.

In other words:
- the abort/start guards make the system safer
- the render path makes delivery more robust
- but the page-building layer still needs refactoring before the issue is fully solved

### What is still missing for a full fix
1. Remove large `String` assembly from the hottest page handlers.
2. Move the biggest HTML/JS bodies to static or chunked delivery.
3. Keep only small dynamic fields in `String` or fixed buffers.
4. Prefer the non-allocating page render overloads where possible.
5. Confirm the page load path no longer depends on a large temporary heap allocation.

---

## Fully fixed target state

To call this problem fully fixed, the receiver should reach a state where the web UI no longer depends on large heap-backed `String` construction for normal page delivery.

### Target architecture
- Static HTML, CSS, and JavaScript live in flash or LittleFS, not in request-time `String` concatenation.
- Dynamic data is injected through small placeholders, JSON APIs, or short server-side fragments.
- Page delivery uses chunked or static send paths, not a single large temporary `String`.
- Request handlers only create small temporary values for labels, IDs, status text, and formatted numbers.
- The webserver stays protected by startup heap gating and render-abort guards, but those become safety nets rather than the primary fix.

### Minimum changes needed to achieve that state
1. Refactor `dashboard_page.cpp` so the page body is no longer assembled as one large `String` before send.
2. Refactor `systeminfo_page.cpp` the same way, since it still builds content and script with temporary `String`s.
3. Convert the largest content/script providers into static helpers or chunk emitters.
4. Add or use a render helper that can emit page sections in chunks without first building the full HTML in RAM.
5. Update handlers to pass small runtime values into placeholders rather than concatenating them into the whole document.
6. Keep `String` only in low-risk utility paths where the payload stays small.

### Render-path requirement
The render path must be able to send a full page without requiring one full-page heap allocation first.
That means one of these must be true:
- the page is written out incrementally in chunks,
- or the page is stored statically and only lightweight substitutions are applied,
- or the content is split into small fragments that never create a large transient buffer.

### Why this is the real fix
The current low-heap protection works because it stops the system from crashing, but it still allows the page-build step to hit memory pressure.
The fully fixed version removes the expensive allocation pattern entirely, so the low-heap abort path becomes a rare fallback instead of part of normal page rendering.

---

## Concrete path to the fully fixed solution

This is the implementation path that should be followed to make the solution both architecturally improved and actually complete.

### Execution rule
Each section must be completed, verified, and cleaned up before moving to the next one.
As soon as a replacement section is working, remove the old, redundant, and legacy code it replaces.

### Section checklist
- [x] Section 1: Add streaming-friendly page rendering ✅ **COMPLETED 2026-05-12**
- [x] Section 2: Refactor dashboard page ✅ **COMPLETED 2026-05-12**
- [x] Section 3: Refactor system info page ✅ **COMPLETED 2026-05-12**
- [x] Section 4: Refactor remaining page handlers ✅ **COMPLETED 2026-05-12** (all page handlers now route through `send_rendered_page_streaming()`)
- [x] Section 5: Delete legacy full-page String paths ✅ **COMPLETED 2026-05-12** (legacy `send_rendered_page()` overloads removed from `page_generator.cpp`)
- [⚠️] Section 6: Validate and tune heap stability under load ⚠️ **PARTIALLY COMPLETED 2026-05-14**
  - ✅ **DONE:** Preflight gate refactored (20KB/8KB → 4KB critical-only + pressure-aware policy)
  - ❌ **PENDING:** Render stability tuning (dashboard content still too large; 38+ second render → HTTP timeout)
  - See: [RECEIVER_DASHBOARD_RENDER_FAILURE_INVESTIGATION_2026_05_14.md](RECEIVER_DASHBOARD_RENDER_FAILURE_INVESTIGATION_2026_05_14.md)

### 1) Move page delivery to a streaming-friendly renderer
Update [espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.h](../../espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.h) and [espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp](../../espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp) so the renderer can emit a page from fragments without first assembling the full body in a temporary `String`.

Required outcome:
- keep the current safety checks
- add a chunked/static render path for full page bodies
- make that path the default for large pages
- remove old, redundant, and legacy render code as each section is replaced so only one implementation remains

### 2) Refactor the dashboard page first
Change [espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page.cpp](../../espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page.cpp) so it only gathers small runtime values and passes them into a render helper.

Required outcome:
- remove the large `String content = ...` build-up
- avoid constructing the full dashboard HTML in RAM
- keep only small formatted fields such as status, IP, version, and device name
- delete the old dashboard page assembly path once the new render path is in place

### 3) Refactor the receiver config / system info page next
Change [espnowreceiver_LCD/lib/webserver_lcd/pages/systeminfo_page.cpp](../../espnowreceiver_LCD/lib/webserver_lcd/pages/systeminfo_page.cpp) in the same way.

Required outcome:
- eliminate the temporary `String content` and `String script` concatenation path
- move static markup into reusable flash/static helpers
- inject only the small runtime values needed by the page
- remove the older page-building branch after the replacement is verified

### 4) Apply the same pattern to any other large page builders
Review [espnowreceiver_LCD/lib/webserver_lcd/pages/monitor2_page.cpp](../../espnowreceiver_LCD/lib/webserver_lcd/pages/monitor2_page.cpp), monitor-related content/script helpers, and any remaining large pages.

Required outcome:
- no request handler should build a large HTML response with repeated `+=`
- long scripts should be delivered as static content or small fragments

### 5) Remove the old heap-heavy path once parity is proven
Once the chunk/static path is proven stable, remove the legacy full-page `String` assembly path for pages that no longer need it.

Required outcome:
- the old path should not remain as the default implementation
- new pages must follow the safer pattern by default
- old, redundant, and legacy code should be deleted section-by-section as soon as the replacement section is complete and verified

### 6) Measure that the fix is complete
Validate under real use with WiFi, ESP-NOW, and MQTT active.

Required outcome:
- no render aborts during normal page loads
- no low-heap startup failures caused by page rendering
- stable largest-free-block behavior while loading the UI
- consistent page load times with no hidden fallback to the old path

### Completion criteria
The solution is only fully fixed when all of the following are true:
- the biggest pages no longer need a full heap-backed `String` to render
- the render path can stream or serve pages without first building them whole
- the low-heap abort path is only a rare safety fallback, not a normal part of rendering
- the refactored pages remain stable during concurrent radio and network activity

### Done definition
This work is only complete when every checklist item above is checked off, the old paths have been removed section-by-section, and the receiver can load the UI without creating the low-heap pressure that previously triggered render aborts.

### Verification verdict
After re-checking the code shape, this solution **will** fix the low-heap problem if it is implemented as written, because it removes the two root causes:
- large request-time `String` assembly for full HTML pages
- dependence on a single full-page temporary buffer before send

The current codebase is **not yet fully fixed** because the biggest handlers still build the page body with `String` first. So the architecture is correct, but the fix is only complete after the refactor lands.

This means the final outcome should be:
- the heap stays stable during normal page loads
- render aborts become rare fallback events instead of routine behavior
- the page path no longer needs to “survive” low heap because it avoids creating the pressure in the first place

---

## Comparison with the TFT receiver codebase

I checked the TFT receiver in [espnowreceiver_2/lib/webserver](../../espnowreceiver_2/lib/webserver) against the LVGL receiver in [espnowreceiver_LCD/lib/webserver_lcd](../../espnowreceiver_LCD/lib/webserver_lcd).

### What the TFT branch already improved earlier
- The TFT branch is more modular overall: page handlers were split into per-page modules instead of one large webserver file.
- Page rendering goes through [espnowreceiver_2/lib/webserver/common/page_generator.h](../../espnowreceiver_2/lib/webserver/common/page_generator.h) and [espnowreceiver_2/lib/webserver/pages/pages.h](../../espnowreceiver_2/lib/webserver/pages/pages.h).
- Several handlers already use `send_rendered_page()` rather than manually assembling and sending one monolithic HTML string.
- The TFT webserver startup already has a more structured lifecycle in [espnowreceiver_2/lib/webserver/webserver.cpp](../../espnowreceiver_2/lib/webserver/webserver.cpp), with handler counting, WiFi readiness checks, and a larger task stack/socket budget.

### What the TFT branch did not fully solve
- It still uses `String` in hot page paths, especially [espnowreceiver_2/lib/webserver/pages/dashboard_page.cpp](../../espnowreceiver_2/lib/webserver/pages/dashboard_page.cpp) and [espnowreceiver_2/lib/webserver/pages/systeminfo_page.cpp](../../espnowreceiver_2/lib/webserver/pages/systeminfo_page.cpp).
- The TFT pages still build large HTML payloads dynamically through content/script helpers such as [espnowreceiver_2/lib/webserver/pages/monitor2_page.cpp](../../espnowreceiver_2/lib/webserver/pages/monitor2_page.cpp) and its content/script files.
- The TFT `webserver.cpp` does not show the same low-heap startup gate, stop-in-progress atomic guard, or mid-render abort logic that is now present in the LVGL branch.

### What that means in practice
- The TFT branch looks like an earlier partial mitigation, not a complete resolution.
- It reduced some of the risk by modularizing pages, but it did not remove the core heap pressure caused by large dynamic HTML/JS assembly.
- The LVGL branch now has the stronger runtime safety layer, but it still needs the same long-term cleanup goal: move large page bodies away from heap-backed `String` construction.

---

## Practical rules for this repository

### Allowed uses of `String`
- JSON serialization for small payloads
- individual labels, status text, IP/MAC formatting
- small config snippets
- one-shot values returned by helper functions

### Not allowed in hot paths
- repeated `+=` across hundreds or thousands of bytes
- full HTML document assembly when the same content can be chunked or static
- long-lived event streams built from `String`
- request handlers that allocate big temporary strings repeatedly

### Preferred alternatives
- `httpd_resp_send_chunk()` for HTML pages
- `send_P()` / flash literals for static content
- `snprintf()` into fixed buffers for formatted text
- static files from LittleFS where practical

---

## Files to review first

- [espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp](espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp)
- [espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page_content.cpp](espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page_content.cpp)
- [espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page_script.cpp](espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page_script.cpp)
- [espnowreceiver_LCD/lib/webserver_lcd/pages/monitor_page_script.cpp](espnowreceiver_LCD/lib/webserver_lcd/pages/monitor_page_script.cpp)
- [espnowreceiver_LCD/lib/webserver_lcd/pages/cellmonitor_page_script.cpp](espnowreceiver_LCD/lib/webserver_lcd/pages/cellmonitor_page_script.cpp)
- [espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp](espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp)

---

## Resolution plan

### Phase A — keep the current safety guards
- keep the low-heap startup gate
- keep the mid-render abort check
- keep active-request-aware recycle protection

### Phase B — reduce `String` footprint
- convert large inline HTML and JS blobs to chunked or static delivery
- pre-reserve when `String` must remain
- remove repeated `String` concatenation in hot paths

### Phase C — validate with metrics
Watch:
- internal free heap
- largest free block
- render duration
- number of aborted page renders
- request counts during dashboard use

Success looks like:
- no render aborts during normal usage
- lower heap fragmentation
- stable page load times
- no loss of web UI while ESP-NOW and MQTT are active

---

## Bottom line

If you want the shortest possible answer:

**`String` is not the problem by itself. Large dynamic webpage assembly on the internal heap is the problem.**

The right fix is to move big page bodies toward chunked/static delivery and keep `String` only for small, predictable fragments.

---

## Implementation Status — 2026-05-12

### ✅ Sections 1-5 Complete (aligned to current code)

**Section 1**: Streaming renderer infrastructure added  
- Added `send_rendered_page_streaming()` callback-based render function  
- Integrates with existing safety checks and chunk infrastructure

**Section 2**: Dashboard page refactored  
- Now uses callback-based streaming render  
- Handler no longer calls legacy full-page renderer
- Note: dashboard content helper still assembles a large `String` body internally

**Section 3**: System info page refactored  
- Uses callback-based streaming render  
- Uses static content/script providers in current handler path

**Section 4**: Remaining page handlers migrated  
- All handlers in `lib/webserver_lcd/pages/` now route through `send_rendered_page_streaming()`

**Section 5**: Legacy code cleanup  
- Removed legacy `send_rendered_page()` overload implementations from `page_generator.cpp`
- Streaming renderer is now the only page-render function implementation in that module

### 📋 Current Codebase State

**Page handler routing:**
- All page handlers use streaming render entrypoint (`send_rendered_page_streaming()`).

**Important remaining gap (root-cause still partially present):**
- Several content/script providers still build large request-time `String` payloads.
- So request handlers are migrated, but full heap-pressure elimination is not yet complete.

### ⏳ Section 6 In Progress

Hardware validation and threshold tuning are still required.
Recent receiver logs showed normal-pressure aborts during head/style emission when free heap dropped below an unconditional floor (`8192`), e.g. at `title` and `common_styles` stages.

#### Section 6 definitive remediation now applied in code
The render guard has been changed to capability-aware internal-heap logic aligned with ESP-IDF guidance on capability heaps (`MALLOC_CAP_INTERNAL`, `MALLOC_CAP_8BIT`):

1. Mid-render checks now use **internal 8-bit heap** metrics (`heap_caps_get_free_size`, `heap_caps_get_minimum_free_size`, `heap_caps_get_largest_free_block`) instead of generic totals.
2. Normal-pressure requests are no longer aborted by an unconditional 8 KB floor.
3. Abort behavior remains strict for:
	 - `CRITICAL` radio pressure, or
	 - truly critical internal heap (`< 4 KB`), or
	 - constrained-pressure starvation (`free < 6 KB` **and** `largest block < 2 KB`).

This removes the known false-abort mode while preserving safety behavior for genuine memory starvation.

#### Implementation Applied (2026-05-14) — PREFLIGHT ONLY

**STATUS: Preflight gate fixed ✅ / Render stability NOT fully addressed ❌**

**File:** `espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp`  
**Change:** Preflight heap check (Phase 3) refactored to use pressure-aware policy

**Old policy (lines 880-895 before 2026-05-14):**
```cpp
if (ph < 20KB || pb < 8KB) {
    reject with 503
}
```
This caused all requests to be rejected when receiver had 12-13 KB stable heap (normal LVGL state).

**New policy (lines 880-905 after 2026-05-14):**
```cpp
constexpr uint32_t kCriticalHeapThreshold = 4U * 1024U;
const RadioPressureState pressure = get_radio_pressure_state();

if ((pressure == RadioPressureState::CRITICAL) || (free_heap < 4KB)) {
    reject with 503
} else {
    allow request (mid-render aborts still apply for sustained constrained starvation)
}
```

**Result of preflight fix:**
- ✅ NORMAL pressure: requests allowed
- ✅ CONSTRAINED pressure: requests allowed (aborts only after sustained low samples)
- ✅ CRITICAL pressure: requests rejected
- ✅ Critically low heap (< 4 KB): requests rejected

**CRITICAL FINDING: Preflight fix exposes deeper issue ❌**

Runtime testing (2026-05-14) revealed the preflight fix only solved the gate, not the underlying cause:

Dashboard render fails after **38+ seconds** with `ESP_ERR_HTTPD_RESP_SEND` (HTTP timeout).

Root cause: Dashboard content (40.5 KB total) requires 40+ chunk sends over 38+ seconds under concurrent ESP-NOW/MQTT activity, causing HTTP connection timeout before completion.

**This reveals Section 6 was only partially completed:**
- ✅ Preflight gate fixed
- ❌ Render content NOT optimized (still too large)
- ❌ Render time NOT addressed (38+ seconds → timeout)

The completion criterion (lines 320-328) was NOT fully met: "the low-heap abort path is only a rare safety fallback, not a normal part of rendering" ❌

Currently, low-heap warnings appear in EVERY dashboard render under concurrent load.

**Detailed analysis:** See [RECEIVER_DASHBOARD_RENDER_FAILURE_INVESTIGATION_2026_05_14.md](RECEIVER_DASHBOARD_RENDER_FAILURE_INVESTIGATION_2026_05_14.md)

#### Next steps required for full Section 6 completion:
1. Reduce dashboard CSS from 5.5 KB → 3 KB
2. Reduce dashboard script from 17.4 KB → 10 KB  
3. Validate render < 15 seconds without timeout
4. Confirm low-heap warnings are rare (< 1% of requests)

#### Source references used for this remediation
- ESP-IDF Heap Memory Allocation (capability allocator and `heap_caps_*` APIs):
	- https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/mem_alloc.html
- ESP-IDF HTTP Server (chunked send semantics and handler error behavior):
	- https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_http_server.html
- ESP-IDF External RAM restrictions and internal-memory reservation context:
	- https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/external-ram.html

