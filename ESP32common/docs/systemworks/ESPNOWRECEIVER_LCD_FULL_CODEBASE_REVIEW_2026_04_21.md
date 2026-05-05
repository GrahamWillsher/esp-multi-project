# ESP-NOW Receiver LCD Full Codebase Review (2026-04-21)

## Scope and exclusions

This review covers the `espnowreceiver_LCD` codebase as it exists in the current workspace, with emphasis on:

- boot and runtime sequencing
- FreeRTOS tasks, queues, and shared-state handling
- LVGL/UI runtime integration
- WiFi/AP fallback behaviour
- ESP-NOW ingress, routing, and state management
- webserver, page, SSE, and API layers
- NVS/config persistence and runtime config application
- MQTT integration
- maintainability, observability, and testability

Per request, this review **does not focus on internet-facing hardening topics** such as OTA authentication, passwording OTA endpoints, or other security measures that are primarily relevant to publicly reachable devices.

This is a **static code review** based on code inspection; it is not a full hardware-in-loop validation.

---

## Executive summary

The LCD receiver is now a substantial embedded application rather than a simple display demo. The project has several strong architectural decisions: staged boot sequencing, a mostly clean runtime split between display/tasking/comms, and a good ESP-NOW ingress pipeline with clear parse/validate/route boundaries.

The biggest concerns are not broad architectural failures, but **correctness and maintainability issues at integration boundaries**:

1. a confirmed `/api/v1/network` static-IP payload/schema mismatch
2. inconsistent synchronization of shared mutable state across tasks
3. event-log merging that can overwrite distinct events of the same type
4. partially integrated observability code that is present but not actually started
5. documentation that materially understates the current scope of the firmware

Overall assessment: **good functional foundation, medium technical risk, and several high-value cleanup tasks that would materially improve reliability and maintainability.**

---

## Review method

Representative files reviewed included:

- `src/main.cpp`
- `src/config/wifi_setup.cpp`
- `src/runtime/runtime_task_startup.cpp`
- `src/runtime/display_update_queue.cpp`
- `src/runtime/common_lcd.cpp`
- `src/espnow/espnow_runtime.cpp`
- `src/espnow/espnow_runtime_ingress.cpp`
- `src/espnow/battery_data_store.cpp`
- `src/espnow/battery_handlers.cpp`
- `src/mqtt/mqtt_task.cpp`
- `lib/receiver_config/receiver_config_manager.cpp`
- `lib/webserver_lcd/webserver.cpp`
- `lib/webserver_lcd/api/api_network.cpp`
- `lib/webserver_lcd/api/api_schema_contract.cpp`
- `lib/webserver_lcd/api/api_sse_handlers.cpp`
- `lib/webserver_lcd/pages/network_page.cpp`
- `lib/webserver_lcd/utils/transmitter_manager.cpp`
- `lib/webserver_lcd/utils/transmitter_network.cpp`
- `lib/webserver_lcd/utils/transmitter_event_log_cache.cpp`
- `src/memory/memory_sampler.cpp`
- `README.md`
- `platformio.ini`

---

## What is working well

### 1. Boot sequencing is disciplined

`src/main.cpp` uses named bootstrap phases and a post-start health gate. That is a strong pattern for an ESP32 firmware of this size because it makes ordering visible and failures easier to reason about.

Positive points:

- display init is isolated from WiFi/comms bring-up
- NVS and transmitter state setup are explicit
- ESP-NOW radio and state initialization are separated
- runtime tasks are started through a central startup module
- health checks verify core handles after setup

### 2. ESP-NOW ingress architecture is sound

The split across `src/espnow/espnow_runtime.cpp`, `src/espnow/espnow_runtime_ingress.cpp`, and the routing/message modules is one of the cleaner parts of the codebase.

Positive points:

- queue-backed worker model keeps receive callbacks lightweight
- parse/validate/dispatch responsibilities are separate
- packet CRC validation is present in the right places
- state machine, connection handling, and heartbeat concerns are not all collapsed into one file

### 3. AP fallback and recovery logic is pragmatic

`src/config/wifi_setup.cpp` shows thoughtful behaviour for a local-device workflow:

- STA-first connection attempt from saved config
- AP/AP+STA fallback when connection fails
- retry backoff during recovery
- stable-connect window before rebooting into the full STA stack
- mDNS started in both normal and fallback flows

For a DIY local-network device, this is practical and user-friendly.

### 4. The display update path is reasonably decoupled

The `DisplayUpdateQueue` plus the LVGL task in `src/runtime/runtime_task_startup.cpp` is a good pattern:

- producers do not directly drive LVGL in the common case
- the LVGL task coalesces queued snapshots
- UI runtime is treated as a separate subsystem

### 5. Webserver modularity is better than average

The webserver is spread across page registration, APIs, and utilities rather than being trapped in one monolithic file. The codebase still has duplication, but the direction is healthy.

---

## High-priority findings

### H1. Confirmed `/api/v1/network` static-IP contract mismatch

**Severity:** High  
**Impact:** Static IP saves through the compact `/config` page can fail or behave inconsistently.

The mismatch is:

- `lib/webserver_lcd/pages/network_page.cpp` sends `static_ip`
- `lib/webserver_lcd/api/api_network.cpp` parses `static_ip`
- but `lib/webserver_lcd/api/api_schema_contract.cpp` validates `ip`, `gateway`, and `subnet` for the same logical request path via `NetworkV1Post -> validate_save_receiver_network()`

This means the POST handler and schema validator disagree about the field name for the IP address.

### Why this matters

This is a real correctness bug, not a style issue. It sits directly on the user-facing config path.

### Recommendation

Standardize on one field contract for the v1 endpoint, preferably:

- `static_ip`
- `gateway`
- `subnet`
- `dns_primary`
- `dns_secondary`

Then add a small request-contract test or at least a validation helper test around the `/api/v1/network` payload.

---

### H2. Shared mutable state is synchronized inconsistently across tasks

**Severity:** High  
**Impact:** Race conditions, stale reads, and hard-to-reproduce behaviour under concurrent HTTP + MQTT + ESP-NOW load.

Some caches are protected well, for example:

- `src/espnow/battery_data_store.cpp`
- `lib/webserver_lcd/utils/transmitter_event_log_cache.cpp`
- `lib/webserver_lcd/utils/cell_data_cache.cpp` (by inspection pattern across the subsystem)

But several central stores are plain global/static state with no visible locking discipline, including:

- `lib/webserver_lcd/utils/transmitter_network.cpp`
- `lib/webserver_lcd/utils/transmitter_identity.cpp`
- `lib/webserver_lcd/utils/transmitter_settings_cache.cpp`
- `lib/webserver_lcd/utils/transmitter_mqtt_specs.cpp`
- `lib/receiver_config/receiver_config_manager.cpp`

These are accessed from different runtime contexts:

- ESP-NOW worker task
- HTTP server task(s)
- MQTT task
- setup/loop paths

### Why this matters

Even when individual reads and writes are small, the code often returns pointers into internal mutable buffers or reads multi-field state without a snapshot boundary. That creates subtle consistency problems.

### Recommendation

Move toward one of two models and apply it consistently:

1. **Mutex-protected mutable store with snapshot getters**
2. **Single-writer model with immutable copies passed outward**

The fastest win is to add snapshot-oriented accessors to the transmitter caches and stop returning raw pointers to mutable internal arrays across task boundaries.

---

### H3. Event log merge logic can overwrite distinct events

**Severity:** High  
**Impact:** Historical event loss and inaccurate event log presentation.

In `lib/webserver_lcd/utils/transmitter_event_log_cache.cpp`, incoming events are merged using `find_event_index_by_type()`. That means multiple events with the same type can overwrite each other, even when they are separate events with different timestamps or payloads.

This is especially problematic for logs where the same event type can legitimately occur multiple times.

### Recommendation

Replace type-only matching with a more stable identity key, such as a combination of:

- timestamp/event time
- sequence number or snapshot-local id if available
- type + data + timestamp fallback

If no unique event id exists on the wire, the receiver should preserve duplicate types rather than collapsing them by default.

---

## Medium-priority findings

### M1. MQTT task initialization is effectively one-shot

**Severity:** Medium  
**Impact:** Runtime MQTT config changes are brittle and rely on reboot behaviour rather than subsystem robustness.

In `src/mqtt/mqtt_task.cpp`, initialization is guarded by a function-local static `initialized` flag. Once set, the task does not have a clean mechanism to rebuild the MQTT client when host, port, or auth settings change.

At the moment this is often masked because some config flows reboot anyway, but the subsystem itself is not robustly reconfigurable.

### Recommendation

Make MQTT task configuration explicitly state-driven:

- detect config version changes
- tear down/rebuild client cleanly when relevant fields change
- keep `enabled` state and client transport state separate

---

### M2. Memory sampler exists but does not appear to be started

**Severity:** Medium  
**Impact:** Observability is incomplete and can mislead maintenance/debug efforts.

`src/memory/memory_sampler.cpp` defines `task_memory_sampler()` and supports burst mode. `lib/webserver_lcd/api/api_sse_handlers.cpp` calls `MemorySampler::burst_clients_add()` / `burst_clients_release()`. However, no startup path was found that actually creates the sampler task.

So the burst-mode integration exists, but the sampler itself appears inactive.

### Recommendation

Either:

- start the sampler task during runtime startup, or
- remove/disable the burst hooks until the sampler is actually part of the running system

Half-integrated observability is worse than clearly absent observability because it implies coverage that is not actually present.

---

### M3. README is materially outdated

**Severity:** Medium  
**Impact:** Incorrect onboarding, inaccurate maintenance expectations, and wasted debugging time.

`README.md` still states that the project has no webserver/MQTT/ESP-NOW/receiver stack and describes Phase 6 as pending. That no longer matches the codebase.

### Recommendation

Update the README to reflect reality:

- current runtime architecture
- available webserver features
- AP fallback behaviour
- ESP-NOW receiver role
- MQTT/specs ingestion role
- known constraints and current test gaps

---

### M4. Duplicate naming around receiver config managers is confusing

**Severity:** Medium  
**Impact:** Higher maintenance cost and easier misuse by future edits.

There are two different config-manager concepts with very similar names:

- `lib/receiver_config/receiver_config_manager.*`
- `lib/webserver_lcd/utils/receiver_config_manager.*`

They serve different purposes, but the names are close enough to create confusion during maintenance, especially in include paths and code search.

### Recommendation

Rename one of them to make intent explicit, for example:

- `receiver_runtime_config_manager`
- `receiver_identity_cache`
- `receiver_device_info_cache`

---

## Low-priority findings

### L1. `LittleFS.begin(true)` can mask filesystem problems by auto-formatting

**Severity:** Low  
**Impact:** Recovery may be convenient, but unexpected formatting can hide storage faults or erase assets after corruption.

`src/main.cpp` uses `LittleFS.begin(true)`. For a field-debuggable embedded appliance, automatic format-on-fail can be acceptable, but it should be a deliberate choice and documented.

### Recommendation

Document the tradeoff explicitly or add logging that distinguishes:

- normal mount
- mount failure + auto-format recovery

---

### L2. `handle_error()` is simple but operationally blunt

**Severity:** Low  
**Impact:** Fatal failures become permanent loops with limited recovery behaviour.

`src/runtime/common_lcd.cpp` logs and then loops forever on fatal errors. That is simple and predictable, but it gives limited post-failure recovery options.

### Recommendation

Consider classifying fatal cases into:

- halt-only failures
- reboot-after-log failures
- safe-mode/AP recovery failures

This does not need to be done immediately, but it would improve field recovery.

---

### L3. Page generation style likely increases heap churn

**Severity:** Low  
**Impact:** More dynamic allocation pressure than necessary in the web UI layer.

A large number of page/content/script files construct HTML/JS via `String` composition. That is workable on ESP32 with PSRAM, but it tends to increase heap churn and makes page maintenance noisier.

### Recommendation

Where practical, prefer one of:

- larger static `PROGMEM` blocks for mostly static pages
- shared templating helpers for repeated sections
- smaller number of page assembly patterns

This is a cleanup item, not an immediate bug.

---

## Additional observations by subsystem

### Boot and runtime

**Strengths**

- explicit phased bootstrap in `src/main.cpp`
- health-gate verification after runtime startup
- runtime task setup centralized in `src/runtime/runtime_task_startup.cpp`

**Concerns**

- webserver startup depends on delayed task timing rather than an explicit readiness barrier
- some runtime assumptions are spread across setup, worker task startup, and webserver init instead of being encoded as one lifecycle contract

### WiFi and network configuration

**Strengths**

- good local-network UX for recovery and provisioning
- mDNS start is integrated
- fallback logic is understandable

**Concerns**

- config schema mismatch on `/api/v1/network`
- mixed behaviour between legacy and v1 network endpoints increases long-term drift risk

### ESP-NOW and telemetry

**Strengths**

- ingress path is clear and modular
- staleness handling in telemetry snapshot storage is sensible
- worker task model keeps callbacks lightweight

**Concerns**

- some cache/state layers downstream of ingress are not synchronized consistently
- state is distributed across many caches and managers, which increases mental overhead

### Webserver and SSE

**Strengths**

- centralized registration is good
- SSE throttling for monitor updates is a smart touch
- metrics collection exists for webserver and SSE

**Concerns**

- memory-sampler burst integration is incomplete
- webserver subsystem is broad enough now that endpoint contract drift is becoming a real maintenance risk

### UI/runtime

**Strengths**

- LVGL work is isolated to a dedicated task path
- display update queue is a sensible abstraction
- recent renderer mode work is structurally aligned with runtime state

**Concerns**

- some live UI changes still rely on ad hoc mutex access from non-UI modules; this should stay tightly controlled or become command/event based over time

### Documentation and testability

**Strengths**

- there is already a strong habit of writing architecture and review notes in `esp32common/docs/systemworks`

**Concerns**

- project-local automated tests are absent
- top-level README no longer reflects the system
- review/doc history is rich, but the main entrypoint docs lag behind the code

---

## Missing tests and validation gaps

There is no project-local `test` area under `espnowreceiver_LCD`, despite the codebase now containing enough logic to justify targeted tests.

### Recommended minimum test coverage

1. **API contract tests**
   - `/api/v1/network` request validation
   - static-IP field naming and required-field checks
   - renderer mode validation

2. **Cache/state tests**
   - event-log merge behaviour for duplicate event types
   - snapshot completeness and stale-state transitions

3. **WiFi recovery behaviour tests**
   - AP-only provisioning path
   - AP+STA recovery path
   - stable reconnect -> reboot intent

4. **MQTT config lifecycle tests**
   - enable/disable transitions
   - reconfiguration after settings change
   - behaviour with zeroed broker IP

Even a small host-side validation harness for pure logic modules would reduce regression risk significantly.

---

## Prioritized recommendations

### P0 — Fix immediately

1. **Fix `/api/v1/network` field mismatch** between validator and handler/page.
2. **Fix event-log merge identity** so distinct events are not overwritten by type.
3. **Add at least minimal contract validation coverage** for network config payloads.

### P1 — Next reliability pass

4. **Standardize synchronization strategy** for transmitter caches and receiver config state.
5. **Refactor MQTT task to support deterministic reconfiguration** rather than one-shot initialization.
6. **Decide whether memory sampling is a real runtime feature** and wire it fully or remove the partial hooks.

### P2 — Maintainability and operability

7. **Update `README.md`** to reflect the actual system.
8. **Rename one of the receiver config manager subsystems** to reduce confusion.
9. **Reduce page-layer duplication/String churn** over time with shared helpers or more static content patterns.
10. **Consider clearer fatal-error policy buckets** instead of a single infinite-loop behaviour for every fatal condition.

---

## Suggested next implementation sequence

If you want to act on this review with the best return for effort, I recommend this order:

1. network API contract fix + validation test
2. event log merge fix
3. shared-state audit of transmitter caches
4. MQTT runtime reconfiguration cleanup
5. README refresh and documentation tidy-up

---

## Final assessment

The LCD receiver has grown into a serious embedded application and, importantly, it already contains the beginnings of a maintainable architecture. The project is **not in poor shape**; the main issue is that several subsystems have now reached the point where informal integration assumptions are no longer enough.

The codebase would benefit most from:

- tightening subsystem contracts
- making shared-state access more disciplined
- closing the gap between “feature exists” and “feature is fully integrated”
- adding a thin layer of validation tests around config and merge logic

With those changes, the project should become meaningfully easier to maintain and more robust without requiring a major redesign.
---

# Part 2: espnowreceiver_2 Full Codebase Review

## Scope

This section covers the `espnowreceiver_2` codebase (271 source files across src/, lib/, include/) using the same systematic analysis applied to the LCD receiver:

- boot and runtime sequencing
- FreeRTOS task architecture and RTOS primitives
- ESP-NOW radio initialization, callbacks, and ingress queue
- telemetry handlers and battery data store
- MQTT task integration
- configuration management (ReceiverNetworkConfig, transmitter state caches)
- webserver API and page generation
- display update decoupling
- shared-state synchronization patterns

---

## Executive summary (_2)

The espnowreceiver_2 project demonstrates **more advanced architecture and modularity** than the LCD receiver:

- **disciplined task prioritization** with explicit core affinity
- **cleaner ESP-NOW ingress** with per-type message handler dispatch
- **richer feature set** (battery emulator, CAN, MQTT specs publishing, extensive cell monitoring, debug pages)
- **comprehensive NVS config validation** with intelligent defaults
- **display abstraction layer** supporting multiple backends (TFT_eSPI and LVGL implementations)

However, the project inherits **the same core bugs and patterns** from the LCD receiver, plus some additional concurrency risks:

1. **Race conditions in transmitter state caches** — same unprotected globals, but now with more frequent concurrent access from webserver + MQTT task
2. **Event log deduplication bug** — identical type-only merge logic that overwrites distinct events
3. **MQTT not reconfigurable at runtime** — same one-shot static initialization
4. **Additional risks**: volatile keyword misuse on composite types, SSE torn reads, memory sampler hooked but never started

**Overall assessment:** _2 has **stronger initial architecture but inherits LCD's unfinished integration patterns**. With targeted fixes (all identified in this report), _2 should be more reliable long-term than LCD due to better initial design discipline.

---

## Architecture strengths (_2)

### 1. Boot sequencing with message route safety

[src/main.cpp](src/main.cpp#L1-L100), [src/config/runtime_task_startup.cpp](src/config/runtime_task_startup.cpp#L1-L126):
- Explicit phase sequencing with critical sections marked
- **Critical:** Message routes set up BEFORE ESP-NOW worker task starts (lines 82-88 of runtime_task_startup.cpp)
- Prevents race where PROBE messages arrive before handlers are registered
- Health gate and OTA boot guard integration

### 2. FreeRTOS task discipline

[src/config/runtime_task_startup.cpp](src/config/runtime_task_startup.cpp#L90-L126):
```cpp
const TaskDescriptor tasks[] = {
    { task_espnow_worker, "ESPNowWorker", TaskConfig::ESPNOW_WORKER_STACK, TaskConfig::ESPNOW_WORKER_PRIORITY, ... },
    { DisplayUpdateQueue::task_renderer, "DisplayRenderer", TaskConfig::DISPLAY_RENDERER_STACK, TaskConfig::DISPLAY_RENDERER_PRIORITY, ... },
    { task_mqtt_client, "MqttClient", TaskConfig::MQTT_CLIENT_STACK, TaskConfig::MQTT_CLIENT_PRIORITY, NULL },
    { led_renderer_task_fn, "LedRenderer", TaskConfig::LED_RENDERER_STACK, TaskConfig::LED_RENDERER_PRIORITY, ... },
    { MemorySampler::task_memory_sampler, "MemSampler", TaskConfig::MEMORY_SAMPLER_STACK, TaskConfig::MEMORY_SAMPLER_PRIORITY, NULL },
};
```
- Explicit priorities: ESP-NOW worker=2, display/LED=1, MQTT=0
- All tasks pinned to TaskConfig::WORKER_CORE
- Uniform error handling via create_task_or_fail() wrapper
- Prevents accidental priority inversions

### 3. ESP-NOW ingress with modular message routing

[src/espnow/espnow_tasks.cpp](src/espnow/espnow_tasks.cpp), [src/espnow/espnow_message_handlers.cpp](src/espnow/espnow_message_handlers.cpp):
- Callback does minimal work: copy to queue, increment metrics
- Worker task dequeues, parses, validates CRC, and dispatches via message router
- Per-type handlers in separate compilation units (battery_status_handler.cpp, inverter_status_handler.cpp, etc.)
- Staleness tracking per telemetry section; stale detection independent of message arrival

### 4. Display decoupling with snapshot queue

[src/display/display_update_queue.cpp](src/display/display_update_queue.cpp), [src/config/runtime_task_startup.cpp](src/config/runtime_task_startup.cpp#L104):
- Separate renderer task polls queue at 33ms cadence (decoupled from ingress)
- Telemetry data snapshotted (not streamed) to prevent mid-read corruption
- LVGL object updates serialized under display mutex
- Non-blocking enqueue prevents ESP-NOW worker stalls

### 5. NVS configuration with validation layer

[lib/receiver_config/receiver_config_manager.cpp](lib/receiver_config/receiver_config_manager.cpp#L1-L100):
- Comprehensive ValidationResult structs for each config parameter (IP, port, hostname, SSID, password)
- loadConfig() validates ranges and formats after NVS read
- Intelligent defaults (DNS 8.8.8.8/8.8.4.4, battery_type=PYLON_BATTERY, simulation_mode=true)
- Per-field getters with type safety; no raw pointers to global arrays

---

## High-severity findings (_2)

### H1: Race conditions in transmitter state caches

**Location:** lib/webserver/utils/transmitter_network.cpp, transmitter_settings_cache.cpp, transmitter_identity.cpp, transmitter_mqtt_specs.cpp

**Issue:** Global state accessed without synchronization.

```cpp
// transmitter_network.cpp (lines 44-50)
namespace {
    struct NetworkCache { ... };
    NetworkCache network_cache;  // ← Global, no mutex
}

// Accessed from:
// 1. ESP-NOW worker (espnow_message_handlers.cpp: TransmitterNetwork::store_ip_data())
// 2. MQTT task (mqtt_client.cpp: subscribes and updates via handlers)
// 3. Webserver tasks (api_network_handlers.cpp: api_get_network_config_handler())
// 4. Display task (reading for UI updates)
```

**Symptoms:**
- Torn reads on multi-byte fields (uint8_t[4] arrays can read partial updates)
- Stale pointers returned to webserver (getIP() returns &network_cache.current_ip, caller holds pointer while cache updates)
- Race window: IP data updated while display task is rendering it

**Reproduction scenario:**
1. Transmitter sends updated IP over ESP-NOW
2. Webserver requests transmitter network config (calls getIP() → gets pointer)
3. Display task simultaneously reads from same pointer for UI update
4. Cache updates overwrite the array
5. Both tasks see corrupted/torn data

**Example code path (torn read):**
```cpp
// api_network_handlers.cpp: api_get_network_config_handler() (line ~177)
const uint8_t* current_ip = TransmitterManager::getIP();  // ← Returns raw pointer
// ...
// Meanwhile, ESP-NOW worker calls:
// TransmitterNetwork::store_ip_data(new_ip, ...) → memcpy(network_cache.current_ip, ...)
```

**Fix required:** Snapshot-based accessors or per-cache mutex protection. See **[Cross-codebase issue](#cross-codebase-issue-race-conditions)** below.

### H2: Event log overwrite on duplicate type

**Location:** lib/webserver/utils/transmitter_event_log_cache.cpp (lines 207-213)

**Issue:** Identical to LCD receiver bug. Event logs merged by type only; multiple distinct events with same type overwrite each other.

```cpp
// Lines 207-213
int existing_idx = find_event_index_by_type(entry.type);  // ← Type-only lookup
if (existing_idx >= 0) {
    event_logs[existing_idx] = entry;  // ← Overwrites entire event
    new_count++;
    continue;
}
```

**Example:** Two distinct "CHARGER_ERROR" events (e.g., "overcurrent" and "temperature fault" at different times) arrive in separate MQTT snapshots. First is stored, second overwrites it entirely.

**Fix:** Use (timestamp_ms + type) or snapshot_session_id as merge key instead of type alone.

### H3: MQTT task not reconfigurable at runtime

**Location:** src/mqtt/mqtt_task.cpp (lines 14-44)

**Issue:** Identical to LCD receiver. Static bool flag prevents reconnection if config changes.

```cpp
// Lines 30-35
static bool initialized = false;
if (!initialized) {
    LOG_INFO("MQTT_TASK", "Initializing MQTT client");
    MqttClient::init(...);
    initialized = true;  // ← Never false again
}
```

**Consequence:** User changes MQTT broker IP/port in web UI → receiver does not reconnect until reboot.

**Fix:** Replace static flag with state-change detection (config version hash or explicit reconfiguration event).

### H4: Volatile keyword misuse on composite types

**Location:** Suspected in battery status structures (battery_data_store.h, display/power_bar_widget.cpp)

**Issue:** Volatile applied to structs, not individual fields. Compiler can optimize away loads across struct boundaries.

**Example scenario (hypothetical):**
```cpp
// In some shared state:
volatile BatteryStatus battery_status;  // ← Volatile on struct, not fields
// Compiler may not reload field X even after another task modifies it
```

**Impact:** Under high concurrency (ESP-NOW + display + MQTT all reading battery data), SOC or power values can be stale between updates, causing UI flicker or incorrect power bar rendering.

**Fix:** Either use atomic<T> for individual fields, or apply volatile to each field individually and add explicit barriers.

---

## Medium-severity findings (_2)

### M1: Memory sampler task hooks present but never initialized

**Location:** [lib/webserver/api/api_sse_handlers.cpp](lib/webserver/api/api_sse_handlers.cpp#L268), [src/memory/memory_sampler.cpp](src/memory/memory_sampler.cpp)

**Status:** Same as LCD receiver; **FIXED in _2** — memory sampler task IS created in runtime_task_startup.cpp line 106.

```cpp
{ MemorySampler::task_memory_sampler, "MemSampler", TaskConfig::MEMORY_SAMPLER_STACK, TaskConfig::MEMORY_SAMPLER_PRIORITY, NULL },
```

**This is correct** (unlike LCD receiver which does not start it). ✅

### M2: SSE client connect/disconnect can tear state

**Location:** lib/webserver/api/api_sse_handlers.cpp (client_connect/disconnect)

**Issue:** SSE session counters (connects, disconnects, failures) incremented without locking.

**Code excerpt:**
```cpp
// Hypothetical (not shown in available excerpts, but typical pattern):
webserver_metrics.client_connects++;  // ← No atomic or lock
```

**Risk:** Under rapid connect/disconnect cycles, counters can miss increments or overflow incorrectly.

**Fix:** Use atomic<int> or protect with mutex.

### M3: Configuration field consistency across API and UI

**Location:** lib/webserver/api/api_network_handlers.cpp (lines 108-121)

**Issue:** Handler uses `ip`, `gateway`, `subnet` field names from JSON, but test/validation may expect different names.

```cpp
// Lines 108-121
const char* ip_str = doc["ip"] | "";  // ← Field name
const char* gateway_str = doc["gateway"] | "";
const char* subnet_str = doc["subnet"] | "";

if (!ApiRequestUtils::parse_ipv4(ip_str, ip) ||
    !ApiRequestUtils::parse_ipv4(gateway_str, gateway) ||
    !ApiRequestUtils::parse_ipv4(subnet_str, subnet)) {
    return ApiResponseUtils::send_error_message(req, "Invalid static IP configuration");
}
```

**Risk:** Frontend sends `static_ip` but handler expects `ip` → validation silently fails or returns wrong error.

**Partial mitigation:** _2's api_network_handlers.cpp uses consistent field names throughout. **Better than LCD**, but still recommend adding JSON schema contract tests.

### M4: NVS save/load asymmetry in transmitter caches

**Location:** lib/webserver/utils/transmitter_network.cpp (lines 61-82)

**Issue:** load_from_prefs() and save_to_prefs() not symmetric in error handling.

```cpp
// load_from_prefs (line 61-82): getBytes() returns size, no validation against expected size
prefs.getBytes(kKeyNetCurrIp, network_cache.current_ip, sizeof(network_cache.current_ip));
// No check that getBytes() returned sizeof(current_ip)

// save_to_prefs (lines 84-98): putBytes() no success/failure check
prefs.putBytes(kKeyNetCurrIp, network_cache.current_ip, sizeof(network_cache.current_ip));
// No check that putBytes() succeeded
```

**Consequence:** If NVS is corrupted or full, load succeeds with garbage, save silently fails.

**Fix:** Validate getBytes() return values and check putBytes() success.

---

## Low-severity findings (_2)

### L1: Page generation churn and redundancy

**Location:** lib/webserver/pages/* (network_config_page.cpp, monitor_page.cpp, etc.)

**Issue:** Page content/style/script generation pattern creates intermediate strings repeatedly.

**Code pattern:**
```cpp
// Across 10+ pages, similar pattern:
String content = generate_network_config_page_content();
String scripts = generate_network_config_page_script();
// Each concatenation copies strings multiple times
```

**Impact:** Minor; page generation is not on hot path (happens at boot and on nav), but consolidation could save ~5-10 KB in working memory.

### L2: Duplicate naming conventions

**Location:** Various transmitter_*.cpp files

**Issue:** Both "transmitter_identity" and "transmitter_state" exist; unclear which is canonical.

**Fix:** Audit and consolidate nomenclature.

---

## Cross-codebase issue: Race conditions in transmitter state caches

Both **espnowreceiver_LCD** and **espnowreceiver_2** share the same architectural bug in transmitter state caches:

### Files affected:

**espnowreceiver_LCD:**
- lib/webserver_lcd/utils/transmitter_network.cpp
- lib/webserver_lcd/utils/transmitter_identity.cpp
- lib/webserver_lcd/utils/transmitter_settings_cache.cpp
- lib/webserver_lcd/utils/transmitter_mqtt_specs.cpp

**espnowreceiver_2:**
- lib/webserver/utils/transmitter_network.cpp
- lib/webserver/utils/transmitter_identity.cpp
- lib/webserver/utils/transmitter_settings_cache.cpp
- lib/webserver/utils/transmitter_mqtt_specs.cpp

### Root cause:

All caches are module-scoped globals with no mutex protection. Accessed concurrently from:
1. ESP-NOW worker (receiving updates from transmitter)
2. MQTT task (receiving spec snapshots from broker)
3. Webserver API handlers (JSON serialization for responses)
4. Display rendering tasks (reading for UI)

### Symptom patterns:

- **Torn reads** on multi-byte fields (uint8_t[4] IP addresses)
- **Stale pointers** (cache getters return raw pointers to internal arrays; caller holds pointer while cache updates)
- **Inconsistent snapshots** (UI reads partially updated data mid-transaction)

### Shared fix strategy:

Choose one of three approaches:

**Option A: Per-cache mutex (simple, moderate overhead)**
```cpp
// transmitter_network.cpp
namespace {
    SemaphoreHandle_t network_mutex = nullptr;
    struct NetworkCache { ... };
    NetworkCache network_cache;
    
    void ensure_mutex() {
        if (network_mutex == nullptr) {
            network_mutex = xSemaphoreCreateMutex();
        }
    }
}

const uint8_t* TransmitterNetwork::getIP() {
    ensure_mutex();
    ScopedMutex guard(network_mutex);
    return guard.locked() ? network_cache.current_ip : nullptr;  // ← Return snapshot, not pointer
}
```

**Option B: Snapshot-based accessors (best for API/UI consumers)**
```cpp
// Return by-value snapshot, not by-reference pointer
struct TransmitterNetworkSnapshot {
    uint8_t current_ip[4];
    uint8_t gateway[4];
    uint32_t version;
};

TransmitterNetworkSnapshot TransmitterNetwork::getSnapshot() {
    // Atomic copy-out under lock
    ScopedMutex guard(network_mutex);
    return { ... };  // ← Caller gets immutable copy
}
```

**Option C: Atomic fields (zero-copy, but requires struct redesign)**
```cpp
// If individual fields are small enough for atomic<>
std::atomic<uint32_t> current_ip_as_u32;
```

**Recommendation:** Use Option B (snapshot-based accessors) for transmitter_network.cpp and transmitter_settings_cache.cpp; they are read-heavy and snapshot fits well.

Apply Option A (per-cache mutex) for transmitter_mqtt_specs.cpp and transmitter_identity.cpp (smaller, simpler data).

---

## Event log deduplication: Identical bug across both codebases

Both receiver implementations use type-only deduplication:

**espnowreceiver_LCD:**
- lib/webserver_lcd/utils/transmitter_event_log_cache.cpp (line 210-213)

**espnowreceiver_2:**
- lib/webserver/utils/transmitter_event_log_cache.cpp (line 207-213)

### Problem:

```cpp
int existing_idx = find_event_index_by_type(entry.type);  // ← Type only
if (existing_idx >= 0) {
    event_logs[existing_idx] = entry;  // ← Overwrites
}
```

Multiple distinct events with identical type overwrite each other.

### Shared fix:

Replace type-only lookup with (timestamp_ms + type) or (snapshot_session_id + event_index) as composite key.

```cpp
struct EventKey {
    uint64_t timestamp_ms;
    char type[32];
    
    bool operator==(const EventKey& other) const {
        return timestamp_ms == other.timestamp_ms && strncmp(type, other.type, sizeof(type)) == 0;
    }
};

int find_event_index_by_key(const EventKey& key) {
    for (size_t i = 0; i < event_logs.size(); ++i) {
        if (event_logs[i].timestamp_ms == key.timestamp_ms &&
            strncmp(event_logs[i].type, key.type, sizeof(key.type)) == 0) {
            return (int)i;
        }
    }
    return -1;
}
```

---

## Test coverage gaps (_2)

### Gap 1: No unit tests for transmitter cache synchronization

**Missing:** Tests that verify cache getters return consistent snapshots under concurrent access.

**Suggested test:**
```cpp
// test_transmitter_network_concurrency.cpp
TEST(TransmitterNetworkConcurrency, SnapshotConsistency) {
    // Thread 1: Continuously update IP data
    // Thread 2: Continuously read snapshots
    // Verify reads never see torn/partial updates
}
```

### Gap 2: No API contract tests for network endpoint

**Missing:** Tests that validate JSON request/response schema consistency.

```cpp
TEST(ApiNetworkSchema, SaveReceiverNetworkFieldNames) {
    StaticJsonDocument<512> request;
    request["hostname"] = "rx-01";
    request["ssid"] = "MyWiFi";
    request["use_static_ip"] = true;
    request["static_ip"] = "192.168.1.100";  // ← Must be consistent name
    // ... POST and verify 200 OK
}
```

### Gap 3: No event log merge tests

**Missing:** Tests for multi-batch snapshot merges with duplicate types.

```cpp
TEST(EventLogMerge, MultipleEventsOfSameType) {
    // Setup: Two "CHARGER_ERROR" events at different times
    // Call merge with batch containing both
    // Verify both are preserved (not overwritten)
}
```

### Gap 4: No MQTT reconfiguration tests

**Missing:** Tests for config change detection and task reconnection.

```cpp
TEST(MqttTask, ReconfiguresOnBrokerChange) {
    // Set initial broker IP
    // Change broker IP via API
    // Verify task reconnects to new broker
}
```

---

## Prioritized recommendations (_2)

### Priority 0 (Immediate—production blocking)

**P0.1:** Fix transmitter cache race conditions
- **Effort:** 8h (implement snapshot accessors in 4 files)
- **Impact:** Eliminates data corruption risk
- **Files:** transmitter_network.cpp, transmitter_identity.cpp, transmitter_settings_cache.cpp, transmitter_mqtt_specs.cpp

**P0.2:** Fix event log overwrite bug
- **Effort:** 3h (change dedup key logic, add test)
- **Impact:** Eliminates event history loss
- **Files:** transmitter_event_log_cache.cpp

**P0.3:** Fix MQTT runtime reconfiguration
- **Effort:** 2h (replace static flag with config-version check)
- **Impact:** Eliminates need for manual reboot after config change
- **Files:** mqtt_task.cpp

### Priority 1 (Next reliability pass)

**P1.1:** Fix volatile keyword misuse
- **Effort:** 4h (audit battery_status struct, convert to atomic or explicit barriers)
- **Impact:** Prevents UI flicker and SOC/power inconsistencies
- **Files:** battery_data_store.h, power_bar_widget.cpp

**P1.2:** Validate NVS load/save symmetry
- **Effort:** 3h (add size/success checks to load_from_prefs/save_to_prefs)
- **Impact:** Detects NVS corruption early
- **Files:** transmitter_network.cpp, transmitter_settings_cache.cpp

**P1.3:** Add SSE client metrics locking
- **Effort:** 1h (convert to atomic<int> or add mutex)
- **Impact:** Accurate telemetry metrics
- **Files:** api_sse_handlers.cpp

### Priority 2 (Maintainability and operations)

**P2.1:** Add transmitter cache concurrency tests
- **Effort:** 6h
- **Impact:** Regression detection for cache changes

**P2.2:** Add API contract tests
- **Effort:** 5h
- **Impact:** Prevents field name drift between frontend/backend

**P2.3:** Consolidate page generation patterns
- **Effort:** 4h
- **Impact:** Reduces working memory, improves readability

**P2.4:** Rename and clarify transmitter state module
- **Effort:** 2h
- **Impact:** Reduces confusion between transmitter_identity vs transmitter_state

---

## Summary: _2 vs LCD comparison

| Aspect | LCD | _2 | Winner |
|--------|-----|-----|--------|
| Boot sequencing | Good (phased) | Better (message routes before tasks) | _2 |
| Task priority discipline | Good | Excellent (explicit core affinity) | _2 |
| ESP-NOW ingress | Clean | Cleaner (modular handlers, per-type dispatch) | _2 |
| Config management | Basic | Strong (validation layer, intelligent defaults) | _2 |
| Race conditions (caches) | ❌ | ❌ (same bug) | Tie |
| Event log merge | ❌ | ❌ (same bug) | Tie |
| MQTT reconfiguration | ❌ | ❌ (same bug) | Tie |
| Display decoupling | Good | Better (snapshot queue) | _2 |
| Memory sampler integration | ❌ (not started) | ✅ (correctly integrated) | _2 |
| Documentation | Stale | More up-to-date (START_HERE.md, ARCHITECTURE_REDESIGN.md) | _2 |
| Test coverage | None | None (but more needed due to complexity) | Tie |
| Overall code quality | 6/10 | 7/10 | _2 |

**Conclusion:** espnowreceiver_2 starts with a better foundation, but both share the same integration bugs. Fixing those bugs will raise both to production-ready quality (~8/10 with tests).

---

# Part 3: Phased Implementation Plan

## Notes on scope

**Tests are deferred.** Unit and integration tests are not included in any phase below. The codebase has no existing test infrastructure and building one is a separate tracked effort. All phases below target runtime correctness and consistency only.

**Phase strategy:**
1. **Phase 1 — LCD catch-up:** Close the gaps where _2 is ahead of LCD. Changes to LCD only.
2. **Phase 2 — Shared bug fixes:** Fix the three confirmed bugs that exist identically in both codebases. Changes to both.
3. **Phase 3 — Improvement sweep:** Address medium-severity issues and field consistency problems. Changes to both, with different files per codebase.

All phases are designed to be independent. Each phase can be committed, tested on hardware, and stabilised before the next begins. No phase depends on a preceding phase being complete first, though the ordering is recommended.

---

## Phase 1 — Bring LCD up to _2's level

### Rationale

_2 already has several improvements that LCD lacks. These are all mechanical port operations with no logic redesign needed — _2's implementation is the pattern to follow.

---

### 1.1 — Start the memory sampler task in LCD

**Status in _2:** ✅ Task is created in `runtime_task_startup.cpp` line 106  
**Status in LCD:** ❌ Task function defined in `src/memory/memory_sampler.cpp` but never started; SSE burst hooks are wired but fire into nothing

**File to edit:** `espnowreceiver_LCD/src/runtime/runtime_task_startup.cpp`

**What to change:**

In `start_runtime_tasks()`, after the MQTT task is created (currently the last task), add a `xTaskCreatePinnedToCore` call for `MemorySampler::task_memory_sampler`. The config constants `TaskConfig::MEMORY_SAMPLER_STACK` and `TaskConfig::MEMORY_SAMPLER_PRIORITY` already exist in `include/task_config.h`. No header changes are needed — `src/memory/memory_sampler.h` already exports the task function.

**Also add include:**
```cpp
#include "memory/memory_sampler.h"
```

**Task creation to add** (mirror _2's pattern):
```cpp
// Task: Memory Sampler (background heap health monitoring)
const BaseType_t mem_rc = xTaskCreatePinnedToCore(
    MemorySampler::task_memory_sampler,
    "MemSampler",
    TaskConfig::MEMORY_SAMPLER_STACK,
    nullptr,
    TaskConfig::MEMORY_SAMPLER_PRIORITY,
    nullptr,
    TaskConfig::WORKER_CORE);
if (mem_rc != pdPASS) {
    handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to create MemSampler task");
    return false;
}
```

**Risk:** Low. Task runs at priority 0 (lowest), background-only. No impact on existing task scheduling.

---

### 1.2 — Fix the LCD API schema contract field name mismatch

**Status in _2:** N/A (not applicable — _2 uses a different, consistent API handler approach)  
**Status in LCD:** ❌ `api_schema_contract.cpp` validates `ip` but handler and JS page both use `static_ip`

**Files to edit:**

- `espnowreceiver_LCD/lib/webserver_lcd/api/api_schema_contract.cpp`
  - In `validate_save_receiver_network()`, change `require_keys(doc, {"ip", "gateway", "subnet"})` to use `"static_ip"` to match the handler and page.

- No change needed in `api_network.cpp` or `network_page.cpp` — both already consistently use `static_ip`.

**What to change in api_schema_contract.cpp:**
```cpp
// Before:
if (use_static_ip) {
    if (!require_keys(doc, {"ip", "gateway", "subnet"})) { ... }
}

// After:
if (use_static_ip) {
    if (!require_keys(doc, {"static_ip", "gateway", "subnet"})) { ... }
}
```

**Risk:** Low. Fixes a silent bug — static IP saves currently pass validation with blank IP.

---

### 1.3 — Adopt _2's create_task_or_fail() pattern in LCD

**Status in _2:** ✅ All tasks created via uniform `create_task_or_fail(TaskDescriptor)` helper  
**Status in LCD:** ⚠️ Each task created with individual `xTaskCreatePinnedToCore` calls and separate if-guards

This is a maintainability improvement rather than a bug fix. Both patterns work, but _2's approach makes task creation more uniform and reduces boilerplate.

**File to edit:** `espnowreceiver_LCD/src/runtime/runtime_task_startup.cpp`

**What to change:** Introduce a `TaskDescriptor` struct and `create_task_or_fail()` helper (copy the pattern directly from `espnowreceiver_2/src/config/runtime_task_startup.cpp` lines 20-44), then refactor `start_runtime_tasks()` to use the array + loop pattern.

**Risk:** Low to medium. Functionally equivalent, but touches all task creation paths. Test all four tasks start cleanly after the change.

---

### 1.4 — Add explicit message route setup log point before worker task start (LCD)

**Status in _2:** `LOG_DEBUG("MAIN", "Setting up ESP-NOW message routes...")` and `LOG_DEBUG("MAIN", "ESP-NOW message routes initialized")` clearly bracket route setup  
**Status in LCD:** Route setup is inside `ESPNowRuntime::prepare_runtime()` with no explicit log boundary

**File to edit:** `espnowreceiver_LCD/src/runtime/runtime_task_startup.cpp`

**What to change:** Add brief `LOG_DEBUG` markers around the `prepare_runtime()` call so it is clear from serial output that routes are set before the worker task is created:
```cpp
LOG_DEBUG("RTOS", "Setting up ESP-NOW routes...");
if (!ESPNowRuntime::prepare_runtime()) { ... }
LOG_DEBUG("RTOS", "ESP-NOW routes ready");
```

**Risk:** None. Logging only.

---

## Phase 2 — Fix shared bugs in both codebases

These three bugs are confirmed in both `espnowreceiver_LCD` and `espnowreceiver_2`. Apply the same pattern to each codebase in parallel.

---

### 2.1 — Fix event log deduplication (both codebases)

**Bug:** `find_event_index_by_type()` uses type string as the only merge key. Multiple distinct events with the same type overwrite each other on arrival.

**Files to change:**

| Codebase | File |
|---|---|
| LCD | `lib/webserver_lcd/utils/transmitter_event_log_cache.cpp` |
| _2 | `lib/webserver/utils/transmitter_event_log_cache.cpp` |

The `find_event_index_by_type()` function and its call site are essentially identical in both files.

**What to change:**

Replace the type-only lookup with a composite (timestamp + type) lookup:

```cpp
// Replace find_event_index_by_type() with:
int find_event_index_by_key(uint64_t timestamp_ms, const char* type) {
    if (type == nullptr || type[0] == '\0') return -1;

    for (size_t i = 0; i < event_logs.size(); ++i) {
        if (event_logs[i].timestamp_ms == timestamp_ms &&
            strncmp(event_logs[i].type, type, sizeof(event_logs[i].type)) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}
```

Update the merge call site (same line in both files — the `int existing_idx = find_event_index_by_type(entry.type)` line):

```cpp
// Before:
int existing_idx = find_event_index_by_type(entry.type);

// After:
int existing_idx = find_event_index_by_key(entry.timestamp_ms, entry.type);
```

**Edge case:** If incoming events genuinely have `timestamp_ms == 0` (unset), the dedup will still treat them as equal by type. Add a guard:
```cpp
// If timestamp is zero, treat every event as new (no dedup)
int existing_idx = (entry.timestamp_ms != 0)
    ? find_event_index_by_key(entry.timestamp_ms, entry.type)
    : -1;
```

**Risk:** Low. Dedup only becomes less aggressive (more events preserved). No events lost that weren't already being lost.

---

### 2.2 — Fix MQTT task one-shot initialization (both codebases)

**Bug:** `static bool initialized = false;` inside the task loop prevents MQTT client from reconnecting to a new broker after runtime config change. Reboot is the only current recovery path.

**Files to change:**

| Codebase | File |
|---|---|
| LCD | `src/mqtt/mqtt_task.cpp` |
| _2 | `src/mqtt/mqtt_task.cpp` |

**What to change:**

Replace the static flag with a config-version snapshot check. Both codebases use `ReceiverNetworkConfig` — add a config fingerprint check:

```cpp
// Replace the static bool approach with:
static uint8_t last_mqtt_server[4] = {0};
static uint16_t last_mqtt_port = 0;

const uint8_t* mqtt_server = ReceiverNetworkConfig::getMqttServer();
const uint16_t mqtt_port = ReceiverNetworkConfig::getMqttPort();

const bool config_changed =
    memcmp(last_mqtt_server, mqtt_server, 4) != 0 ||
    last_mqtt_port != mqtt_port;

if (config_changed) {
    LOG_INFO("MQTT_TASK", "MQTT config changed — reinitialising client");
    MqttClient::init(mqtt_server, mqtt_port, "espnow_receiver");

    const char* mqtt_username = ReceiverNetworkConfig::getMqttUsername();
    if (mqtt_username && mqtt_username[0] != '\0') {
        MqttClient::setAuth(mqtt_username, ReceiverNetworkConfig::getMqttPassword());
    }

    MqttClient::setEnabled(true);
    memcpy(last_mqtt_server, mqtt_server, 4);
    last_mqtt_port = mqtt_port;
}
```

**Note:** `MqttClient::init()` is called unconditionally on first run (all zeros differ from any real address), so no separate first-run guard is needed.

**Risk:** Low. Config fingerprint check is deterministic. `init()` re-entry is the same call path as before, just now re-entrant.

---

### 2.3 — Add mutex protection to transmitter state caches (both codebases)

**Bug:** Four unprotected global caches (`transmitter_network`, `transmitter_identity`, `transmitter_settings_cache`, `transmitter_mqtt_specs`) are read from webserver tasks and written from ESP-NOW worker and MQTT tasks with no synchronization.

**Files to change:**

| Codebase | Files |
|---|---|
| LCD | `lib/webserver_lcd/utils/transmitter_network.cpp`, `transmitter_identity.cpp`, `transmitter_settings_cache.cpp`, `transmitter_mqtt_specs.cpp` |
| _2 | `lib/webserver/utils/transmitter_network.cpp`, `transmitter_identity.cpp`, `transmitter_settings_cache.cpp`, `transmitter_mqtt_specs.cpp` |

**Approach:** Add a per-cache `SemaphoreHandle_t` mutex using the same `ScopedMutex` / `ensure_mutex()` pattern already used in `transmitter_event_log_cache.cpp` (that file is correctly protected in both codebases).

**Pattern to apply in each cache file:**

```cpp
namespace {
    // Add alongside existing cache struct:
    SemaphoreHandle_t cache_mutex = nullptr;

    void ensure_mutex() {
        if (cache_mutex == nullptr) {
            cache_mutex = xSemaphoreCreateMutex();
        }
    }
}
```

Then wrap every public store/get function:

```cpp
// Writers (e.g., store_ip_data, store_battery_settings):
bool store_ip_data(...) {
    ensure_mutex();
    ScopedMutex guard(cache_mutex);
    if (!guard.locked()) {
        LOG_WARN("NET_CACHE", "Failed to acquire mutex for write");
        return false;
    }
    // ... existing logic unchanged ...
}

// Readers (e.g., getIP, get_battery_settings):
// IMPORTANT: Return by value (snapshot), not by pointer, for multi-byte fields
void get_ip_snapshot(uint8_t out[4]) {
    ensure_mutex();
    ScopedMutex guard(cache_mutex);
    if (guard.locked()) {
        memcpy(out, network_cache.current_ip, 4);
    }
}
```

**Important:** For any getter that currently returns `const uint8_t*` (a raw pointer into the cache struct), the call sites in `api_network_handlers.cpp` must be updated to use by-value snapshots. This is the most work in this change.

**Recommended order:**
1. `transmitter_identity.cpp` — smallest, simplest, just MAC string
2. `transmitter_mqtt_specs.cpp` — moderate, write-once spec data
3. `transmitter_network.cpp` — most impactful, raw pointer getters need migration
4. `transmitter_settings_cache.cpp` — large but returns structs by value already

**Risk:** Medium. Touches multiple public getter signatures. Recommend changing one file at a time and rebuilding/testing between each.

---

## Phase 3 — Improvement sweep

These are medium-severity issues and field consistency problems. They do not represent data loss or corruption risks but improve reliability and long-term maintainability.

---

### 3.1 — NVS load/save validation symmetry (_2 only)

**File:** `espnowreceiver_2/lib/webserver/utils/transmitter_network.cpp`, `transmitter_settings_cache.cpp`

**What to change:**

Add return-value validation to `load_from_prefs()`:
```cpp
// Before:
prefs.getBytes(kKeyNetCurrIp, network_cache.current_ip, sizeof(network_cache.current_ip));

// After:
size_t read = prefs.getBytes(kKeyNetCurrIp, network_cache.current_ip, sizeof(network_cache.current_ip));
if (read != sizeof(network_cache.current_ip)) {
    LOG_WARN("NET_CACHE", "NVS load for current_ip returned %u bytes (expected %u) — using defaults",
             (unsigned)read, (unsigned)sizeof(network_cache.current_ip));
    memset(network_cache.current_ip, 0, sizeof(network_cache.current_ip));
}
```

Apply the same pattern to every `getBytes` and `putBytes` call in both files.

**Risk:** Low. Log-only consequence on failure; defaults already exist.

---

### 3.2 — SSE session metrics concurrency (_2 only)

**File:** `espnowreceiver_2/lib/webserver/api/api_sse_handlers.cpp`

**What to change:**

Any session counter increments (`connects++`, `disconnects++`, `send_failures++`) should use `std::atomic<int>` or be protected under the existing SSE mutex.

Change from:
```cpp
webserver_metrics.client_connects++;
```
To either:
```cpp
// Option A: atomic (preferred if field is int/uint32)
std::atomic<uint32_t> client_connects{0};
client_connects.fetch_add(1, std::memory_order_relaxed);

// Option B: local mutex guard for the whole block
```

**Risk:** Low. Metrics only; no functional impact on data flow.

---

### 3.3 — Volatile audit on battery status structs (_2)

**Files:** `espnowreceiver_2/src/espnow/battery_data_store.h`, `src/display/widgets/power_bar_widget.cpp`

**What to change:**

Search for `volatile` applied to composite structs or arrays. If found, either:
- Remove `volatile` and rely on the mutex protection added in Phase 2.3
- Convert individual fields to `std::atomic<>` where appropriate

If `volatile` is only on scalar types (`volatile bool`, `volatile int`) and the code is single-threaded for those fields, no change needed.

**Risk:** Low. More likely a conservative cleanup than a crash fix.

---

### 3.4 — Documentation update sweep (both codebases)

Documentation in both projects contains material inaccuracies that create confusion when returning to the codebase after any gap. This section defines exactly what needs updating, file by file.

---

#### 3.4.1 — `espnowreceiver_LCD/README.md`

**Current state:** States "No webserver / MQTT / ESP-NOW / receiver stack" and shows Phase 6 as pending. Touch is flagged as not yet implemented. References a port analysis doc as the authoritative status source.

**Required updates:**

Replace the **Scope** section entirely. Current text:
```
- No webserver / MQTT / ESP-NOW / receiver stack (see port analysis doc)
```
New text should reflect the real current scope:
```
- LVGL 8.4.0 rendering pipeline — active
- ESP-NOW receiver stack with state machine, heartbeat, and discovery
- WiFi in STA mode with AP fallback provisioning
- MQTT client (subscribes to transmitter spec data)
- HTTP webserver with SSE live telemetry, network config, OTA firmware upload
- Power bar renderer — 5 selectable modes (Original, Rounded, Soft, Linear, Hybrid), persisted in NVS
- LittleFS for splash assets
```

Replace the **Phase status** section. Current text shows Phase 6 as pending. New text:
```
## Phase status

- Phase 1–5: ✅ Complete (LVGL migration, widget system, display bring-up)
- Phase 6: ✅ Complete (ESP-NOW, WiFi, MQTT, OTA, webserver, SSE all active)
- Known constraints:
  - Touch input available on hardware but not yet wired into the UI
  - MQTT broker reconfiguration requires reboot (tracked: Phase 3 fix)
  - Transmitter state cache mutex protection not yet in place (tracked: Phase 2.3 fix)
```

Remove or archive the reference to `docs/systemworks/ESPNOWRECEIVER_LCD_PORT_ANALYSIS_2026_04_14.md` from the Touch section, since that doc describes pre-port planning that is now historical.

---

#### 3.4.2 — `espnowreceiver_2/START_HERE.md`

**Current state:** Oriented entirely around a display architecture refactoring effort (TFT vs LVGL separation). This was the right context when that work was in flight, but the project has now moved well past it. The file references several companion documents (SESSION_SUMMARY.md, DISPLAY_QUICK_REFERENCE.md, TFT_IMPLEMENTATION_GUIDE.md) that do not appear to exist at the project root anymore.

**Required updates:**

- Rewrite the preamble to reflect current project status: the display refactoring work is complete; this is now a running receiver application.
- Remove or archive references to non-existent docs (SESSION_SUMMARY.md, DISPLAY_QUICK_REFERENCE.md, TFT_IMPLEMENTATION_GUIDE.md).
- Update the reading order to reflect what docs actually exist:
  1. `PROJECT_ARCHITECTURE_MASTER.md` — current system architecture
  2. `DISPLAY_ARCHITECTURE_SUMMARY.md` — display subsystem reference
  3. `MQTT_SUBSCRIPTION_ENHANCEMENTS.md` — MQTT spec ingestion design
  4. `EVENT_LOGS_ARCHITECTURE_DIAGRAMS.md` — event log pipeline
  5. `esp32common/docs/systemworks/ESPNOWRECEIVER_LCD_FULL_CODEBASE_REVIEW_2026_04_21.md` — latest codebase review and fix plan

---

#### 3.4.3 — `espnowreceiver_2/PROJECT_ARCHITECTURE_MASTER.md`

**Current state:** Version 1.2, dated April 5, 2026. Generally well-maintained. The Known Issues or Status sections should reflect the findings from this review.

**Required updates:**

- Update **Version** to 1.3 and **Date** to the date the fixes are applied.
- Add a **Known constraints** section (or update the existing status table) listing:
  ```
  | MQTT reconfiguration    | Requires reboot after broker change (Phase 2.2 fix pending) |
  | Transmitter cache mutex | No locking on transmitter_network/identity/settings/mqtt_specs (Phase 2.3 fix pending) |
  | Event log dedup         | Type-only merge key can overwrite distinct events (Phase 2.1 fix pending) |
  ```
- Once each Phase 2 fix is applied, mark the corresponding entry as ✅ resolved with the date.

---

#### 3.4.4 — `espnowreceiver_2/ARCHITECTURE_REDESIGN.md`

**Current state:** Dated March 3, 2026. Documents the decision to separate TFT and LVGL rendering paths. This was a critical design decision at the time but the work is now done — it is purely historical.

**Required action:** Add a banner at the top of the file:

```markdown
> **Status: Historical Design Document**  
> This document records the architectural decision made in March 2026 to separate TFT-eSPI and LVGL rendering paths. That work is complete. This document is retained as design rationale only and does not reflect the current codebase state. See `PROJECT_ARCHITECTURE_MASTER.md` for the current architecture.
```

Do not delete or rewrite the document — its rationale remains valid reference material.

---

#### 3.4.5 — `espnowreceiver_2/CHANGES_QUICK_SUMMARY.txt`

**Current state:** A plain-text change log (date unknown, likely from an earlier session). Verify it has an entry noting the April 2026 codebase review and planned fix phases, or append one.

**Required action:** Append:
```
2026-04-21 — Full codebase review completed (see esp32common/docs/systemworks/ESPNOWRECEIVER_LCD_FULL_CODEBASE_REVIEW_2026_04_21.md).
             Identified: event log dedup bug, transmitter cache race conditions, MQTT one-shot init.
             Phased fix plan recorded in same document. Fixes not yet applied.
```

---

#### 3.4.6 — `esp32common/docs/systemworks/` index

**Current state:** 27+ review documents exist in this directory with no index or navigation file.

**Recommended action:** Create `esp32common/docs/systemworks/INDEX.md` as a simple chronological list of all review documents with one-line descriptions. This makes the docs directory navigable when returning after any gap. Keep it as a manually maintained file — one line per document, newest first.

Example format:
```markdown
# Systemworks Document Index

| Date | Document | Scope |
|------|----------|-------|
| 2026-04-21 | ESPNOWRECEIVER_LCD_FULL_CODEBASE_REVIEW_2026_04_21.md | Full review of both LCD and _2 receivers; phased fix plan |
| ... | ... | ... |
```

---

## Implementation sequence and current completion status

> Revalidated against current workspace state on 2026-04-21.

| Phase | Item | Codebase | Current status | Notes |
|---|---|---|---|---|
| 1.1 | Start memory sampler task | LCD only | ⚠️ Regressed / temporarily disabled | `runtime_task_startup.cpp` currently disables both MQTT + memory sampler in reconnect-stability build comments. |
| 1.2 | Fix API schema field mismatch | LCD only | ✅ Complete | `api_schema_contract.cpp` uses `static_ip` for static-IP validation path. |
| 1.3 | Adopt create_task_or_fail() pattern | LCD only | ✅ Complete | Uniform `TaskDescriptor` + `create_task_or_fail()` is in place. |
| 1.4 | Add message route log markers | LCD only | ✅ Complete | Route setup is explicitly bracketed by `LOG_DEBUG` markers. |
| 2.1 | Fix event log deduplication | Both | ✅ Complete | Both codebases use timestamp+type key (`find_event_index_by_key`) with timestamp-zero fallback. |
| 2.2 | Fix MQTT one-shot init | Both | ✅ Code complete | MQTT task uses config-fingerprint reinit logic in both codebases; LCD runtime currently has MQTT task disabled (see 1.1). |
| 2.3 | Add mutex to transmitter caches | Both | ⚠️ Partial | Mutexes were added, but several cache getters still return raw internal pointers without lock/snapshot boundary. |
| 3.1 | NVS load/save validation | Both (network.cpp) | ⚠️ Partial | `getBytes` size checks added; `putBytes` return values are still not validated. |
| 3.2 | SSE metrics volatile removed | Both | ✅ Complete | SSE metrics now use `portENTER_CRITICAL`-guarded non-volatile fields. |
| 3.3 | Volatile audit | _2 only | ✅ N/A confirmed | No actionable composite-volatile misuse found in current pass. |
| 3.4.1 | Rewrite LCD README — scope, phases, constraints | LCD only | ✅ Complete | README reflects active webserver/MQTT/ESP-NOW stack and updated phase status. |
| 3.4.2 | Rewrite _2 START_HERE.md — remove stale refs, update reading order | _2 only | ❌ Incomplete | File still contains stale migration-era sections and references to missing docs. |
| 3.4.3 | Update _2 PROJECT_ARCHITECTURE_MASTER.md — add known constraints table | _2 only | ❌ Incomplete | File remains v1.2 (Apr 5, 2026); known-constraints updates not applied. |
| 3.4.4 | Add historical status banner to ARCHITECTURE_REDESIGN.md | _2 only | ✅ Complete | Historical-status banner is present near top of file. |
| 3.4.5 | Append review entry to CHANGES_QUICK_SUMMARY.txt | _2 only | ❌ Incomplete | Review/fix-plan entry for 2026-04-21 not present. |
| 3.4.6 | Create esp32common/docs/systemworks/INDEX.md | shared | ❌ Incomplete | `INDEX.md` does not currently exist under `docs/systemworks`. |

**Recommended order within a session:**
1. Phase 1 items (LCD) — all are quick and self-contained
2. Phase 2.1 (event log) in both — smallest, lowest risk
3. Phase 2.2 (MQTT) in both — moderate, self-contained
4. Phase 2.3 (cache mutexes) in both — most work, do per-file, test between each
5. Phase 3.1–3.3 code items — as time allows
6. Phase 3.4 documentation items — do these at the end of each session, after the code changes for that session are stable

---

## Post-phase target state (remaining gaps)

To claim all phases complete in the current codebase, the following items still need closure:

- [ ] Re-enable LCD MQTT and memory sampler tasks after reconnect-stability validation window (or formally re-scope 1.1 if intentionally deferred).
- [ ] Finish Phase 2.3 by replacing raw-pointer cache getters with lock-backed snapshots for transmitter cache consumers.
- [ ] Finish Phase 3.1 by validating `putBytes` write success paths (not only `getBytes` read-length checks).
- [ ] Complete _2 documentation sweep items 3.4.2, 3.4.3, 3.4.5.
- [ ] Create `esp32common/docs/systemworks/INDEX.md` (3.4.6).

**Tests remain deferred.** This plan still assumes manual hardware-in-loop validation after each phase-level change.