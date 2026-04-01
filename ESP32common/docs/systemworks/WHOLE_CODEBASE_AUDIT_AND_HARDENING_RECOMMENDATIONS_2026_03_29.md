# Whole Codebase Audit and Hardening Recommendations — 2026-03-29

## Scope

Audited codebases:
- `ESPnowtransmitter2/espnowtransmitter2`
- `espnowreceiver_2`
- shared modules under `esp32common`

Audit focus:
- remaining magic numbers / hard-coded policy
- `String` usage and fragmentation risk
- manual memory management / ownership risks
- defensive-coding gaps
- industry-standard improvements
- candidate full rewrites where warranted
- additional hardening opportunities (observability, CI gates, regression controls)

---

## Method

This review used:
- current repository state on `feature/battery-emulator-migration`
- static code inspection and targeted pattern scans across all three codebases
- spot validation of high-risk paths (OTA upload, type catalog serving, shared logging, shared web layout)

Build context observed during review:
- Receiver upload/monitor path succeeded recently.
- **2026-03-30 implementation review clean builds:**
   - Receiver: `pio run -e lilygo-t-display-s3_tft -j 12` ✅
   - Transmitter: `pio run -e olimex_esp32_poe2 -j 12` ✅
- Noted environment warning during builds: obsolete duplicate PlatformIO Core installation message (non-blocking, but should be cleaned up on dev host).

---

## Executive Summary

The hardening work completed since the 2026-03-25 report is substantial and high-value. The codebase is materially safer than before, especially around queue determinism, timing centralization, and web streaming paths.

However, there are still notable runtime-risk areas:

1. **Manual heap and ownership remain in selected high-frequency or critical paths** (notably OTA upload chunk buffer and receiver catalog JSON assembly).
2. **Shared logging still uses allocator-heavy `String` composition and no explicit synchronization**, which is a risk under multi-task logging load.
3. **Receiver type catalog API still uses dynamic allocation (`new[]`) + `String` JSON assembly** in request paths.
4. **Legacy Battery Emulator object factories still allocate with raw `new` without explicit unified ownership model** *(explicitly out of scope for the current hardening sprint per 2026-03-30 direction; tracked but deferred)*.
5. **Some policy constants and encoded values remain inline** (small but avoidable drift risk).

Bottom line:
- Architecture and modularity are good.
- Determinism and defensive robustness are improved.
- Remaining work is now concentrated in **specific runtime hotspots**, not broad structural debt.

---

## Priority Findings (2026-03-29)

## P0 (High) — Shared MQTT logger is allocator-heavy and not explicitly thread-safe

**Files:**
- `esp32common/logging_utilities/mqtt_logger.h`
- `esp32common/logging_utilities/mqtt_logger.cpp`

**Findings:**
- Uses multiple `String` fields in message buffering and publish formatting.
- Repeated `String` concatenation in `log()`, `publish_message()`, and `publish_status()`.
- No explicit mutex/critical section around ring-buffer bookkeeping (`buffer_head_`, `buffer_count_`, entries), despite likely multi-task calls.

**Risk:**
- Heap fragmentation and potential data races in long-running systems under burst logging.

**Recommendation:**
- Replace buffered message fields with fixed char arrays (`tag[32]`, `message[256]` etc.) and `snprintf` formatting.
- Add lock discipline (FreeRTOS mutex or critical section) around enqueue/flush operations.
- Keep publish in single owner task context only.

**Action type:** medium-to-full refactor.

---

## P0 (High) — Receiver type catalog API still allocates per request and builds JSON via `String`

**File:**
- `espnowreceiver_2/lib/webserver/api/api_type_selection_handlers.cpp`

**Findings:**
- `generate_sorted_type_json()` allocates with `new TypeEntry[count]` and builds response using `String` concatenation.
- `serve_cached_type_catalog()` allocates temporary arrays (`new[]`) multiple times.

**Risk:**
- Fragmentation and OOM behavior in repeated page/API load patterns.
- Additional churn at exactly the settings-page workflow where stability is important.

**Recommendation:**
- Replace with fixed-capacity stack or static scratch buffers (bounded by catalog max).
- Build JSON with `StaticJsonDocument` + chunked send, or deterministic writer API.
- Remove all per-request `new[]` from this handler.

**Action type:** targeted refactor.

---

## P1 (High) — OTA upload path still uses raw malloc/free (RAII wrapper present but heap-backed)

**File:**
- `ESPnowtransmitter2/espnowtransmitter2/src/network/ota_upload_handler.cpp`

**Findings:**
- `OtaUploadResources` improves cleanup discipline, but still does `malloc()`/`free()` for upload chunk buffer.

**Risk:**
- Less severe than pre-hardening state, but still a heap dependency in security-critical upload path.

**Recommendation:**
- Move to deterministic preallocated upload scratch (owned by OTA manager/session), or PSRAM pooled allocator with fixed lifetime.
- Keep single lifecycle state machine and explicit ownership boundaries.

**Action type:** medium refactor (hardening follow-up).

---

## P1 (Medium-High) — Shared web layout still exposes String-heavy builder API surface

**Files:**
- `esp32common/webserver_common_utils/src/spec_page_layout.cpp`
- `esp32common/webserver_common_utils/include/webserver_common_utils/spec_page_layout.h`

**Findings:**
- Streaming helpers exist and are good.
- Legacy APIs still return/build `String` (`build_spec_page_html_header`, `build_spec_page_html_footer`, `build_spec_page_nav_links`).

**Risk:**
- Continued allocator churn where old API remains in use.
- Slow drift back to heap-heavy call patterns.

**Recommendation:**
- Deprecate `String` return APIs and migrate all callsites to chunked `send_spec_page_response` path.
- Keep static fragments in flash and bounded dynamic sections only.

**Action type:** medium refactor.

---

## P1 (Deferred / Out of Scope for current sprint) — Legacy Battery Emulator polymorphic factories still rely on raw new/delete ownership model

**Files (representative):**
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/battery/BATTERIES.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/inverter/INVERTERS.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/charger/CHARGERS.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/battery/battery_manager.cpp`

**Findings:**
- Extensive use of raw `new` object construction by selected type.
- Ownership appears implicit and spread across modules.

**Risk (still valid, but deferred):**
- Leak/lifetime confusion risk as features evolve.
- Hard to reason about restarts, hot-switch, or partial re-init paths.

**Scope note (2026-03-30):**
- This area is **out of scope for the immediate hardening pass** to keep focus on newly introduced code paths.
- Keep this item in backlog; do not execute in the current sprint.

**Recommendation (deferred backlog):**
- Introduce explicit owner (`std::unique_ptr` where toolchain allows) or a centralized lifecycle manager with documented state transitions.
- Ban raw `new` outside controlled factory boundaries.

**Action type:** phased refactor.

---

## P2 (Medium) — Receiver splash/image rendering still uses manual malloc/free paths

**Files:**
- `espnowreceiver_2/src/display/display_splash.cpp`
- `espnowreceiver_2/src/display/display_splash_lvgl.cpp`

**Findings:**
- Explicit `malloc()`/`free()` for image buffers.

**Risk:**
- Potential fragmentation and error-branch complexity in display lifecycle paths.

**Recommendation:**
- Replace with scoped buffer owner (RAII) and/or dedicated static frame scratch region.
- Ensure all failure branches share one teardown path.

**Action type:** medium refactor.

---

## P2 (Medium) — Remaining hard-coded policy values and encoded status constants

**Files (representative):**
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.cpp`
- `espnowreceiver_2/lib/webserver/pages/*_script.cpp` (JS timeouts/retry constants)
- selected receiver/transmitter handlers with inline limits

**Findings:**
- New event-alignment message decoding uses status nibble values inline.
- Web scripts and some handlers still embed retry/timeout values directly.

**Risk:**
- Policy drift and tuning friction.

**Recommendation:**
- Move status code mappings and policy values into named constants/tables per subsystem.
- For JS assets, keep a small generated policy block (server-supplied) for synchronized tuning.

**Action type:** small-to-medium refactor.

---

## P2 (Medium) — Defensive result checking still inconsistent in some HTTP utility layers

**Scope:** receiver + transmitter web handlers

**Findings:**
- Many paths now correctly check `httpd_resp_send*`, but consistency is not universal.

**Risk:**
- Silent truncation or response failures in adverse network states.

**Recommendation:**
- Standardize through one helper pattern:
  - send chunk
  - verify
  - abort + log consistently
- Add lint/check script for unchecked `httpd_resp_send` calls.

**Action type:** small refactor + tooling.

---

## Focused Deep-Dive Update (2026-03-30 follow-up)

This section captures additional investigation requested after the initial 2026-03-29 audit.

### A) RAII explained fully (and why it matters here)

**RAII** = **Resource Acquisition Is Initialization**.

Core rule:
- A resource is acquired in an object constructor (or equivalent creation boundary),
- and released in that object destructor automatically when the object leaves scope,
- so cleanup happens on **all** exits (success path, error path, early return).

In C++ terms, RAII turns resource lifecycle into deterministic scope lifecycle.

Typical resources:
- heap memory
- file/socket handles
- mutex/lock ownership
- transaction/OTA update session state

Why RAII is safer than manual cleanup:
- prevents leaks from missed `free()`/`delete()` on error branches
- reduces duplicated cleanup code
- makes failure handling simpler and more auditable
- improves exception/early-return safety even when control flow is complex

Applied to this codebase:
- **Good:** transmitter OTA path already introduced a scoped helper (`OtaUploadResources`) that centralizes SHA context teardown and `Update.abort()` guard logic.
- **Remaining gap:** the OTA buffer is still heap-backed via `malloc/free`; RAII is used for cleanup discipline, but allocation remains dynamic.

Practical hardening interpretation:
- RAII is not “no heap”; it is “no leaked/unowned resource.”
- Best outcome in embedded systems is usually **RAII + deterministic bounded storage**.

### B) MQTT block-size and JSON-size investigation (new findings)

#### Confirmed current sizing behavior

1. **MQTT client buffer configured to 6144 bytes on both sides**
   - transmitter `MqttManager::init()` sets `setBufferSize(6144)`
   - receiver `MqttClient::init()` sets `setBufferSize(6144)`

2. **Payload builders and JSON docs are often set to the same nominal limits**
   - TX examples: `kBufferSize`/`DynamicJsonDocument` pairs at 4096, 3072, 6144
   - RX examples: fixed `DynamicJsonDocument` sizes at 4096/3072/6144 for corresponding topics

3. **No centralized “effective MQTT payload budget” contract exists**
   - In PubSubClient, effective payload max is below buffer size due to header/topic overhead.
   - Operationally, if buffer is 6144, safe payload is **less than** 6144.

4. **No systematic overflow/truncation guard pattern is consistently applied**
   - No common preflight using `measureJson(...)` before publish.
   - No consistent `doc.overflowed()` gate before serialization/publish.

#### High-risk hotspot identified

**Transmitter event logs publish path** currently attempts up to 100 events with:
- `DynamicJsonDocument(6144)` and
- publish buffer target `6144`.

Given per-event object fields (`timestamp`, `level`, `data`, `message`, `event`), this can exceed realistic capacity well before 100 events in worst cases, causing partial output/overflow behavior depending on allocator/document state.

#### Why this can produce intermittent failures

- Near-limit payloads can fail publish when topic/header overhead is included.
- Fixed capacities copied across producer/consumer paths can drift as schema fields are added.
- Successful compile does not prove runtime JSON fits in budget.

#### Recommended sizing policy (now explicit)

1. Introduce one shared policy block per topic:
   - `doc_capacity`
   - `max_serialized_payload`
   - `mqtt_publish_budget`
   - `max_items` (for arrays like event logs)

2. Enforce publish preflight:
   - build doc
   - fail if `doc.overflowed()`
   - compute `required = measureJson(doc)`
   - publish only if `required <= mqtt_publish_budget`

3. Add runtime diagnostics on refusal:
   - topic
   - measured payload
   - configured budget
   - dropped/truncated item count

4. For event logs specifically:
   - bound `max_events` by measured budget (not fixed 100)
   - optionally chunk/paginate multi-message publishes for large deltas

### C) OTA previous work investigation (including known failure vectors)

#### Confirmed previous hardening already carried out

1. Short-lived signed OTA challenge/session flow (`/api/ota_arm` + `X-OTA-*` headers)
2. Shared OTA auth/session primitives moved to `esp32common/webserver_common_utils`
3. Duplicate control-plane arm protection to avoid rotating an active unconsumed session
4. OTA HTTP stack-stability hardening on transmitter (reduced stack pressure + larger HTTP task stack)
5. Receiver-side OTA forwarding hardened with partial-write handling and early transmitter-error capture
6. Cross-device compatibility checklist added to prevent receiver/transmitter OTA drift

#### Previously observed failure/regression classes

1. **Session mismatch regressions**
   - duplicate arm events could invalidate receiver-held challenge and produce `Invalid OTA session`.

2. **HTTP task stack canary failures during OTA**
   - stack-heavy formatting/logging + OTA path pressure previously caused instability.

3. **Proxy forwarding edge cases**
   - partial writes and early upstream rejection needed explicit handling to avoid generic forwarding failures.

4. **Contract drift risk between receiver and transmitter OTA endpoints**
   - body/header expectations and response-shape compatibility can break cross-device OTA if changed independently.

#### Current remaining OTA hardening gap (post-investigation)

- Transmitter upload path is now much better structured, but upload chunk storage still uses heap allocation (`malloc/free`) inside RAII wrapper.
- This is lower risk than pre-hardening state, but still a deterministic-runtime improvement opportunity.

#### Required guardrail after any OTA change

Run the cross-device smoke checklist end-to-end (arm, stream, transmitter accept, no `httpd` canary, final 200 JSON response) before merge.

---

### D) Embedded heap telemetry mechanics (how this should work here)

For this system, “periodic logging of free heap / largest block / fragmentation estimate” should be implemented as **low-rate sampled metrics**, not per-request heavy logging.

Recommended model:

1. **Sampling cadence**
   - Baseline periodic sample every 10–30 seconds.
   - Burst-mode sample every 1 second only during active web/API stress windows (SSE active, OTA upload, large JSON publish).

2. **Metrics to capture (internal + PSRAM separately)**
   - Internal RAM free bytes
   - Internal RAM largest free block
   - PSRAM free bytes
   - PSRAM largest free block
   - Legacy aggregate heap free/max-alloc (already present in parts of current telemetry)

3. **Fragmentation estimate (practical embedded proxy)**
   - Per memory domain estimate:
     - $frag\_est = 1 - \frac{largest\_free\_block}{free\_bytes}$
   - Interpretation:
     - near $0$ = contiguous free space is healthy
     - closer to $1$ = free space is split into many small holes
   - Only compute when `free_bytes > 0`.

4. **Where to emit**
   - Add to existing system metrics endpoint payloads.
   - Keep optional ring-buffer snapshots for post-failure diagnostics.
   - Avoid verbose serial spam in normal operation.

5. **Alert thresholds (initial values)**
   - Internal largest free block < 32 KB (risk for medium allocations)
   - PSRAM largest free block < 128 KB (risk for display/image bursts)
   - Fragmentation estimate > 0.70 sustained over N samples

Current state observed:
- Receiver metrics already expose aggregate heap (`free`, `min_free`, `max_alloc`) but not largest-block or explicit fragmentation estimate.
- Transmitter health/OTA status reports aggregate heap only.
- This means telemetry direction is good, but not yet sufficient to quantify fragmentation trend under burst load.

#### Receiver implementation work required (concrete)

1. **Extend receiver metrics payload (`/api/system_metrics`)**
   - File: `espnowreceiver_2/lib/webserver/api/api_telemetry_handlers.cpp`
   - Add nested objects for `heap_internal` and `heap_psram` with:
     - `free`
     - `largest_block`
     - `frag_est`
   - Keep existing aggregate `heap.free/min_free/max_alloc` for backward compatibility.

2. **Add a lightweight memory sampler service (receiver task)**
   - Sample at baseline cadence and temporary burst cadence.
   - Store latest sample + small ring buffer (for diagnostics page and post-failure context).

3. **Expose diagnostics ring buffer via API**
   - Add endpoint (example): `/api/memory_samples`
   - Return last N timestamped samples (bounded, no unbounded growth).

4. **Thresholded warnings only**
   - Do not spam logs every sample.
   - Emit warning only on threshold crossing / sustained degradation windows.

#### Implementation compliance requirements (must-follow)

All receiver work above must follow project standards and current code structure:

1. **Project guideline conformance is mandatory**
   - Follow `esp32common/docs/project guidlines.md` naming, module boundaries, and validation requirements.
   - Prefer shared helpers/utilities over duplicate local implementations.

2. **API message structure must remain consistent**
   - Extend existing JSON payloads additively (no breaking field renames/removals in current consumers).
   - Keep response envelopes and error conventions aligned with existing `ApiResponseUtils` / `HttpJsonUtils` usage.

3. **Logging/message style must match current subsystem patterns**
   - Use existing log macros/tags and severity patterns.
   - No ad-hoc message format drift in diagnostics/status strings.
   - Threshold/state-change logging only for periodic samplers.

4. **Web page integration must match current UI architecture**
   - Add telemetry display through existing page composition flow (`*_page_content.cpp`, `*_page_script.cpp`, page registration).
   - Keep `/debug` as diagnostics-focused page and avoid dashboard clutter.

5. **Build + regression validation required on completion**
   - Receiver and transmitter builds must pass.
   - Verify existing page/API consumers continue to parse status/error payloads.
   - Update architecture/audit docs for any behavior change.

6. **Sequencing rule vs. broader HTTP subsystem work**
   - Implement memory telemetry work **before** the broader HTTP consistency/refactor pass.
   - Implement it directly in the **final, project-compliant format** (no temporary payload schema or throwaway endpoint shape).
   - Keep `/api/system_metrics` changes additive and non-breaking so later HTTP helper consolidation does not require message-format rewrites.

#### Where this should be reported in the receiver web UI

**Primary user-facing page recommendation:**
- Add a **“Memory Health” card on `/debug`** (receiver debug page).

Why `/debug`:
- Already the diagnostics-oriented operator page.
- Avoids crowding `/` dashboard with advanced runtime internals.
- Keeps memory telemetry near other troubleshooting controls.

Suggested presentation on `/debug`:
- Internal RAM: free / largest block / frag estimate
- PSRAM: free / largest block / frag estimate
- Sampling mode indicator: `baseline` vs `burst`
- Optional sparkline of last N samples from `/api/memory_samples`

Secondary/engineering access:
- Keep raw JSON at `/api/system_metrics` for tooling/scripts.
- Optional low-rate MQTT health topic for remote observability (not debug logger stream).

### E) PSRAM utilization assessment (current status)

#### Board/build posture

- Receiver (ESP32-S3) is built with PSRAM-enabled flags (`BOARD_HAS_PSRAM`) and OPI memory type.
- Transmitter (ESP32-POE2 / WROVER) build is PSRAM-aware (`qio_qspi`, cache-issue fix flag).

#### Where PSRAM is already used well

1. **Receiver display paths**
   - Large JPEG/splash and LVGL draw buffers are allocated with `MALLOC_CAP_SPIRAM` first, with internal-RAM fallback.
   - This is appropriate and protects internal heap for networking/control paths.

2. **Shared/receiver web specs rendering**
   - Specs body scratch can move to PSRAM when larger than stack threshold or when explicitly configured.

3. **Transmitter MQTT publish buffer**
   - Reusable publish buffer allocated in PSRAM (`ps_malloc`) and reused, reducing repeated internal-heap churn.

#### Gaps / improvement opportunities

1. **No explicit PSRAM health telemetry in API payloads**
   - Current endpoints mostly expose aggregate heap values only.

2. **No clear policy table for “what must live in PSRAM vs internal RAM”**
   - Display/image buffers already do this implicitly, but policy is not centralized.

3. **Transmitter MQTT PSRAM allocation path has no explicit non-PSRAM fallback path in `ensure_publish_buffer()`**
   - On allocation failure, publish fails immediately; behavior is safe but could be made more resilient if desired.

4. **Some medium transient web buffers still rely on `String`/general heap behavior**
   - This can still consume internal heap even when PSRAM exists.

#### Conclusion on PSRAM usage

- **Receiver:** generally making good practical use of PSRAM in the highest-value areas (display/LVGL/image buffers).
- **Transmitter:** partial but useful PSRAM usage is present (MQTT reusable buffer), but telemetry and allocation policy are not yet mature.
- Net: PSRAM is being used, but not yet managed as a first-class, measured memory tier.

#### PSRAM hardening actions to add to next sprint

1. Extend system metrics payloads with PSRAM free/largest-block and fragmentation estimate.
2. Add an explicit memory-placement policy table per subsystem (display, MQTT, OTA, web formatting).
3. Add optional guarded fallback path for transmitter publish buffer allocation strategy (maintaining deterministic bounds).
4. *(Deferred by user request)* Soak-test acceptance criteria for stable internal-largest-block and PSRAM-largest-block under sustained web/API + MQTT load.

---

## Implementation Review Checkpoint (2026-03-30)

This checkpoint validates current code state against the hardening plan and confirms what is complete vs still pending.

### Verified complete in this checkpoint

1. **Receiver memory telemetry shipped (additive, non-breaking)**
   - `/api/system_metrics` now includes additive `heap_internal` and `heap_psram` blocks (`free`, `largest_block`, `frag_est`) while preserving legacy aggregate `heap.*` fields.
   - New memory sampler task added with bounded ring buffer storage and threshold warnings.
   - New `/api/memory_samples` endpoint added and registered.

2. **Receiver debug Memory Health UI shipped**
   - `/debug` page now includes Memory Health card and client-side fetch/render path for `/api/memory_samples`.

3. **Shared MQTT logger rewrite completed (Phase A item)**
   - `mqtt_logger.*` migrated from `String` buffering/composition to fixed-size `char` buffers + `snprintf`.
   - Buffer enqueue/flush bookkeeping protected by FreeRTOS mutex.

4. **Receiver type catalog deterministic rewrite completed (Phase A item)**
   - Removed per-request `new[]`/`delete[]` churn and `String` JSON assembly path in the catalog handler.
   - Replaced with bounded deterministic assembly path and fixed-capacity handling.

5. **Critical-path allocation cleanup completed (Phase B items in scope)**
   - Transmitter OTA upload buffer now prefers PSRAM allocation (`ps_malloc`) with guarded fallback.
   - Receiver splash loading paths updated to scoped RAII buffer ownership for temporary decode buffers.

6. **Build validation (clean compile) passed after implementation**
   - Receiver (`lilygo-t-display-s3_tft`) builds successfully.
   - Transmitter (`olimex_esp32_poe2`) builds successfully.

### Checkpoint addendum (2026-03-30, post-implementation UI fixes)

1. **Memory Health sample age calculation corrected**
   - `/api/memory_samples` now returns `now_ms` (device uptime) so UI age math uses device-uptime deltas instead of browser epoch time.
   - Fix removes erroneous huge “latest ~XXXXXXXXs ago” values.

2. **Memory Health age display made human-readable**
   - `/debug` now formats sample age as `s`, `m s`, `h m s`, or `d h m`.
   - UI is wrap-safe across `millis()` rollover (~49.7 days).

3. **Internal/PSRAM frag display hardened**
   - `/debug` frag rendering now falls back to computed estimate (`1 - largest/free`) if API frag fields are missing/invalid.
   - Prevents intermittent blank frag values in the card.

### Remaining items (next hardening phase)

1. **Phase C policy centralization**
   - Move remaining inline policy constants and encoded values into named policy tables/structs where still outstanding.

2. **HTTP send-result consistency pass**
   - Standardize remaining handler paths to uniform checked-send helper usage.

3. **Phase D tooling/guardrails**
   - Add CI checks for allocation policy and unchecked HTTP send/write patterns.
   - *(Deferred by user request)* Soak-test telemetry assertions for memory stability trends.

### Phase C progress update (2026-03-30, continuation)

1. **Policy centralization started (web diagnostics UI)**
   - Memory Health `/debug` script thresholds/timeouts were moved into a centralized `DEBUG_UI_POLICY` object.
   - Replaced inline literals for frag thresholds, largest-block warning thresholds, burst age cutoff, and status timeout.

2. **HTTP send-result consistency pass started (transmitter OTA status handlers)**
   - Added checked response helpers in `ota_status_handlers.cpp` and converted direct `httpd_resp_send*` calls in:
     - `root_handler`
     - `health_handler`
     - `event_logs_handler` (no-emulator branch)
     - `ota_status_handler`
     - `firmware_info_handler`
     - `test_data_config_get_handler`
   - Handlers now return `ESP_FAIL` when response send fails, with consistent warning logs.

3. **Phase C status**
   - **In progress**: additional policy constant extraction and broader receiver/transmitter send-check normalization still pending.

### Phase C continuation update (2026-03-30, OTA response-path normalization)

1. **Chunked send handling normalized in transmitter OTA event log path**
   - `ota_status_handlers.cpp` now uses a checked chunk-send helper for event log prefix, separators, per-event JSON chunks, suffix, and final terminator.
   - Chunk send failures now emit consistent warning logs before returning `ESP_FAIL`.

2. **Final OTA upload success response now checked**
   - `ota_upload_handler.cpp` now validates the final success `httpd_resp_send(...)` result.
   - Prevents silent success-response loss after a completed upload and aligns behavior with the broader checked-send policy.

3. **Validation update**
   - Transmitter rebuild after this continuation passed successfully (`olimex_esp32_poe2`).

### Phase D kickoff update (2026-03-30, initial guardrails)

1. **Shared hardening guardrail script added**
   - Added `esp32common/scripts/hardening_guardrails.py` as an initial static checker for active hardening hotspots.
   - Current coverage focuses on:
     - unchecked `httpd_resp_send*` usage in receiver webserver and transmitter network code
     - reviewed raw allocation policy patterns in the same hot paths

2. **Guardrail first run surfaced one remaining real OTA send issue**
   - `ota_manager.cpp` still had a direct `httpd_resp_send(...)` success path with no result validation.
   - That path now captures the send result, logs failures, and returns `ESP_FAIL` on send loss.

3. **Guardrail validation status**
   - The new script now passes cleanly against the current ESP workspace roots.
   - Transmitter rebuild after the `ota_manager.cpp` fix also passed successfully (`olimex_esp32_poe2`).

### Phase D continuation update (2026-03-30, CI integration)

1. **Guardrails wired into repository CI**
   - Added `.github/workflows/hardening-guardrails.yml` at repository root.
   - Workflow runs on `push` (`main`, `feature/**`) and `pull_request`.
   - Executes `python esp32common/scripts/hardening_guardrails.py --root "$GITHUB_WORKSPACE"` so hardening policy regressions fail CI early.

2. **Operational impact**
   - Guardrail checks are now both locally runnable and CI-enforced, reducing drift risk for unchecked HTTP send paths and allocation-policy hotspots.

3. **Scope refinement (user-directed)**
   - Soak-test implementation/assertion work is explicitly skipped for this sprint.
   - Phase D execution remains focused on static guardrails and CI enforcement.

### Burst-mode wiring update (2026-03-30, memory sampler activation)

1. **Ref-counted burst-mode API added to `memory_sampler`**
   - `MemorySampler::burst_clients_add()` and `MemorySampler::burst_clients_release()` added to `memory_sampler.h` / `memory_sampler.cpp`.
   - An internal `s_burst_ref_count` (spinlock-protected via `portMUX_TYPE`) drives `s_burst_mode` so that burst sampling is active while **any** stress client (SSE or OTA) holds a reference.
   - `set_burst_mode(bool)` kept for direct one-off use; ref-counted API preferred in handler code.

2. **SSE handlers wired (both streams)**
   - `api_cell_data_sse_handler` and `api_monitor_sse_handler` each call `burst_clients_add()` immediately after their connect-metrics increment.
   - `recordCellSessionEnd()` and `recordMonitorSessionEnd()` each call `burst_clients_release()` after the critical section exits, so all exit paths (early error returns, normal disconnect) release the reference correctly.

3. **OTA upload handlers wired (RAII)**
   - `BurstModeGuard` RAII struct added to the anonymous namespace of `api_control_handlers.cpp`.
   - Instantiated at the top of `api_ota_upload_handler` and `api_ota_upload_receiver_handler`.
   - All return paths (validation failure, forward error, success) automatically release the burst reference via the destructor — no manually matched call pairs required.

4. **Effect on Memory Health UI**
   - The `/debug` Memory Health card's sampling-mode badge now correctly shows `burst` during active SSE sessions and OTA uploads, and `baseline` at all other times.

5. **Build validation**
   - Receiver (`lilygo-t-display-s3_tft`) builds successfully after all wiring changes.

### Updated readiness summary

- Codebase is currently **build-stable** for both receiver and transmitter.
- Phase A and the requested Phase B critical-path cleanup items are now implemented in code.
- Memory Health UI correctness fixes (age source, wrap-safe human formatting, frag fallback) are complete.
- Memory sampler burst mode is now fully wired: both SSE streams and both OTA upload handlers activate 1 s burst sampling for the duration of each stress window.
- Phase C HTTP send-result normalization is now **complete** across all receiver webserver and transmitter OTA handlers.
- Phase D tooling includes executable guardrail script and CI wiring; `httpd_resp_send_err` coverage added and script still passing cleanly.

### Phase C completion update (2026-03-30, receiver HTTP send-result pass)

1. **Full audit of receiver webserver `httpd_resp_send*` usage**
   - Scanned all `espnowreceiver_2/lib/webserver/**/*.cpp` for unchecked `httpd_resp_send*` calls.
   - All SSE chunk sends (`api_sse_handlers.cpp`) were already correctly result-checked inline.
   - All `page_generator.cpp` chunk/finish sends were already returning results to callers.
   - Two real gaps found: both used bare `httpd_resp_send_err` with discarded return values.

2. **`api_type_selection_handlers.cpp` — OOM path normalised**
   - Replaced bare `httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM")` + `return ESP_FAIL`
     with `return ApiResponseUtils::send_error_with_status(...)`.
   - Now consistent with every other error return in the file (JSON body, result returned, no bare send).

3. **`generic_specs_page.cpp` — PSRAM OOM path normalised**
   - Replaced bare `httpd_resp_send_err(...)` with captured `esp_err_t err_rc` + `LOG_WARN` on failure + `return ESP_FAIL`.
   - `httpd_resp_send_err` is appropriate here (page send, not JSON API); the fix ensures the result is no longer silently discarded.

4. **Guardrails script tightened**
   - `httpd_resp_send_err` added to `HTTP_SEND_PATTERNS` in `hardening_guardrails.py`.
   - Existing allowed-pattern `esp_err_t \w+ = httpd_resp_send_err(` already covers the fixed call in `generic_specs_page.cpp`.
   - Script re-run after tightening: `[PASS] unchecked-http-send`, `[PASS] allocation-policy`.

5. **Build validation**
   - Receiver (`lilygo-t-display-s3_tft`) builds successfully after all Phase C changes.

### Phase C continuation update (2026-03-30, receiver policy-constant extraction)

1. **Receiver SSE timing policy constants centralized**
   - `api_sse_handlers.cpp` now defines named constants for session max duration, cell update wait, monitor update wait, and SSE event reserve overhead.
   - Replaced inline literals (`300000`, `15000`, `500`, `12`) with policy names to reduce drift risk.

2. **Receiver OTA timing/buffer policy constants centralized**
   - `api_control_handlers.cpp` now defines named constants for HTTP poll delays, forward retry delay, response wait polling, stream stall timeout, transmitter socket timeout, early-response timeout, final parse timeout, OTA start settle delay, reboot delay, and upload chunk size.
   - Replaced inline literals (`5`, `10`, `500`, `250`, `60000`, `70000`, `1200`, `3000`, `1024`) with policy names.

3. **Validation**
   - Receiver rebuild passed after extraction (`lilygo-t-display-s3_tft`).
   - Guardrails re-run passed (`unchecked-http-send`, `allocation-policy`).

### Phase C continuation update (2026-03-30, transmitter policy-constant extraction)

1. **Transmitter OTA status handler sizing/limit constants centralized**
   - `ESPnowtransmitter2/espnowtransmitter2/src/network/ota_status_handlers.cpp` now defines named constants for:
     - event-log default/min/max limits
     - JSON document capacities
     - query/body/response buffer sizes used by status and test-data endpoints
   - Replaced inline literals (`50`, `1`, `500`, `128`, `16`, `96`, `384`, `512`, `896`, `1024`, `1152`) with policy names.

2. **Operational impact**
   - Behavior is unchanged; this is a pure policy-centralization step to reduce tuning drift and make size/limit review explicit in one location.
   - Complements earlier receiver-side extraction so both sides now follow the same “named policy constant” rule in active OTA/SSE HTTP paths.

3. **Validation**
   - Transmitter rebuild passed after extraction (`olimex_esp32_poe2`).
   - Guardrails re-run passed (`unchecked-http-send`, `allocation-policy`).

### Phase C continuation update (2026-03-30, legacy spec-layout API deprecation + guardrail)

1. **Shared legacy String builder APIs explicitly deprecated**
   - In `ESP32common/webserver_common_utils/include/webserver_common_utils/spec_page_layout.h`, the following APIs are now marked `[[deprecated]]`:
     - `build_spec_page_html_header(...)`
     - `build_spec_page_html_footer(...)`
     - `build_spec_page_nav_links(...)`
   - Guidance in deprecation text points all new usage to `send_spec_page_response(...)`.

2. **Receiver compatibility shim cleaned up**
   - `espnowreceiver_2/lib/webserver/common/spec_page_layout.h` removed the inline wrapper functions that re-exposed the deprecated String builders.
   - The shim now keeps only type aliases (`SpecPageNavLink`, `SpecPageParams`) to the shared common types.

3. **Stale legacy nav-link helpers removed**
   - Removed unused `get_*_nav_links_html()` helpers from:
     - `battery_specs_display_page_script.{h,cpp}`
     - `inverter_specs_display_page_script.{h,cpp}`
   - These were the only remaining receiver source callsites that invoked legacy String nav-link builders.

4. **Guardrail extended to prevent reintroduction**
   - Added `legacy-spec-layout-string-api` rule in `esp32common/scripts/hardening_guardrails.py`.
   - The rule fails if legacy String builder APIs are called in receiver/transmitter/shared code (except declaration/definition lines in the shared layout implementation/header).

5. **Validation**
   - Receiver rebuild passed (`lilygo-t-display-s3_tft`).
   - Transmitter rebuild passed (`olimex_esp32_poe2`).
   - Guardrails passed with new rule enabled:
     - `[PASS] unchecked-http-send`
     - `[PASS] allocation-policy`
     - `[PASS] legacy-spec-layout-string-api`

### Phase D continuation update (2026-03-30, raw new/delete gate)

1. **New CI/static guardrail added for raw C++ heap ownership operators in active web/network hotspots**
   - Added `raw-new-delete-policy` rule to `esp32common/scripts/hardening_guardrails.py`.
   - Scope is intentionally focused to active hardened paths:
     - `ESPnowtransmitter2/espnowtransmitter2/src/network/**`
     - `espnowreceiver_2/lib/webserver/api/**`
     - `espnowreceiver_2/lib/webserver/common/**`

2. **Detection policy**
   - Flags actionable operator usage patterns (`= new ...`, `delete ptr...`) rather than free-form keyword text, avoiding false positives from embedded JavaScript/page strings.
   - Explicitly allows C++ deleted-function declarations (`= delete;`).

3. **Operational impact**
   - New introduction of raw `new`/`delete` in these hardened paths now fails the guardrail run/CI early.
   - Existing guarded RAII and non-heap paths remain unaffected.

4. **Validation**
   - Guardrails pass with the new rule active:
     - `[PASS] unchecked-http-send`
     - `[PASS] allocation-policy`
     - `[PASS] legacy-spec-layout-string-api`
     - `[PASS] raw-new-delete-policy`
   - Receiver rebuild passed (`lilygo-t-display-s3_tft`).
   - Transmitter rebuild passed (`olimex_esp32_poe2`).

### Operational fix update (2026-03-30, NTP/DST timezone correction)

1. **Root cause identified**
   - Transmitter NTP bootstrap in `lib/ethernet_utilities/ethernet_utilities.cpp` was initializing the process timezone to `UTC0` on first use.
   - A later geolocation-based timezone lookup could override that, but until it succeeded the device remained on UTC.
   - Unknown timezone names from providers also fell back straight back to `UTC0`, discarding correct regional offsets.
   - This produces an exact 1-hour wall-clock error at DST boundaries for affected regions.

2. **Fix applied**
   - Shared defaults in `esp32common/ethernet_config.h` are now neutral again:
     - `EthernetConfig::NTP::DEFAULT_POSIX_TZ = "UTC0"`
     - `EthernetConfig::NTP::DEFAULT_TIMEZONE_NAME = "UTC"`
   - `ethernet_utilities.cpp` now performs timezone lookup as soon as the network is up, before the first regular sync loop settles.
   - Known timezone names still use explicit POSIX DST rules.
   - Unknown timezone names now fall back to the provider's reported current UTC offset instead of reverting to `UTC0`.
   - Timezone lookup now refreshes periodically so later summer/winter transitions can self-correct without requiring hard-coded UK-only rules.

3. **Display/logging cleanup**
   - Timezone abbreviation is now refreshed from the active system `localtime` rule (`%Z`) so display/log output reflects the real active zone (`GMT`/`BST`) instead of a stale static label.
   - `TimeManager::time_sync_notification_cb()` log wording corrected from `UTC` to `local` because it logs `localtime_r(...)`, not UTC.

4. **Validation**
   - Transmitter rebuild passed after the change (`olimex_esp32_poe2`).
   - Edited files report no IDE errors.

---

## Additional Hardening Opportunities

1. **Concurrency contracts as code comments + assertions**
   - For shared singletons and queue/logging managers, document allowed thread contexts and add runtime guard assertions in debug builds.

2. **Heap health telemetry**
   - Periodic logging of free heap, largest free block, fragmentation estimate during web/API bursts.

3. **CI hardening gates**
   - Add static checks for:
     - raw `new`/`delete` introduction outside approved modules
     - unchecked `httpd_resp_send*`
     - added magic numeric literals in policy-sensitive directories

4. **Fuzz/negative tests for API payloads**
   - Particularly settings and component apply endpoints.

5. **Documented ownership matrix**
   - One markdown table mapping each long-lived pointer/object to owner + lifecycle events.

---

## Candidate Full Rewrites (Updated)

1. **Shared MQTT logger rewrite (new full-rewrite candidate)**
   - Replace `String` buffering + unsynchronized mutable state with fixed-buffer lock-protected ring queue.

2. **Receiver type catalog handler rewrite**
   - Remove dynamic array churn and `String`-based JSON assembly from settings catalog APIs.

3. **Battery/Invertor object factory ownership normalization** *(deferred/out of scope for current sprint)*
   - Keep in backlog; revisit after new-path hardening items are complete.

4. **(Optional) OTA buffer determinization**
   - Move remaining dynamic upload scratch allocation to preallocated or bounded owned region.

---

## Recommended Implementation Order (Next Sprint)

### Phase A — Highest risk runtime determinism
1. Rewrite shared MQTT logger (`mqtt_logger.*`) to fixed buffers + lock discipline.
2. Refactor receiver type catalog API to zero-`new[]` + non-`String` JSON assembly.

### Phase B — Critical-path cleanup (new code focus)
3. Convert receiver splash loaders to scoped/static buffer ownership.
4. OTA upload buffer determinization (remove remaining dynamic chunk allocation in hot path).

### Phase C — Policy and consistency
5. Remove remaining inline policy constants into named tables/structs.
6. Enforce uniform HTTP send result-check helper usage.

### Deferred backlog (explicitly out of current scope)
- Normalize legacy battery/inverter/charger factory ownership model.

### Phase D — Tooling/guardrails
7. Add CI checks for raw allocation and unchecked HTTP sends.
8. *(Deferred by user request)* Soak-test telemetry/assertions for internal/PSRAM memory stability.

---

## Definition of Done (Hardening Pass)

The hardening pass can be considered complete when:
- no allocator-heavy `String` assembly remains in long-lived/hot paths
- no per-request `new[]` in web/API handlers
- critical shared mutable queues/loggers are lock-safe and deterministic
- explicit ownership model is documented and enforced for newly introduced/actively changing runtime objects (legacy factory normalization tracked separately in deferred backlog)
- policy constants are centralized and named
- unchecked network/storage send/write calls are eliminated or lint-gated
- *(Deferred by user request)* soak-test stability criterion tracked for a later sprint

---

## Final Assessment

The codebase is now in a strong transition state: major architectural cleanup is done, and many hardening items are already complete. The remaining risk is concentrated and tractable.

The next highest-value work is **deterministic runtime behavior in shared logging and receiver catalog APIs**, followed by OTA buffer determinization and remaining new-path defensive consistency work.

This should be handled as a focused hardening sprint, not broad refactoring. Legacy Battery Emulator ownership normalization remains explicitly deferred for now.
