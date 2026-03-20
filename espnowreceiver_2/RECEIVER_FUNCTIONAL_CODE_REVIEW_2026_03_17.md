# Receiver Functional Code Review — 2026-03-17

## Implementation Status — updated 2026-03-18

### ✅ Completed (receiver-only items)
| Item | Files changed | Status |
|------|--------------|--------|
| Remove duplicate `transition_to_state(WAITING_FOR_TRANSMITTER)` | `src/main.cpp` | ✅ Done |
| Delete vestigial `ReceiverConnectionManager` (472 lines) | `src/espnow/receiver_connection_manager.h/.cpp` | ✅ Deleted |
| Delete vestigial `ReceivedDataManager` (293 lines) | `src/state/received_data_manager.h/.cpp` | ✅ Deleted |
| Refactor version beacon: 3× duplicate request blocks → loop over `request_config_section()` | `src/espnow/espnow_tasks.cpp` | ✅ Done |
| Unify display: `display_led.cpp` now uses `tft.fillCircle()` directly instead of `DisplayManager::get_driver()` | `src/display/display_led.cpp` | ✅ Done |
| Remove `DisplayManager::init()` call and `tft_hardware`/`tft_driver` statics from `main.cpp` | `src/main.cpp` | ✅ Done |
| Delete `DisplayManager` and `TftEspiDisplayDriver` dead code | `src/display/display_manager.h/.cpp`, `src/hal/display/tft_espi_display_driver.h/.cpp` | ✅ Deleted |

### ✅ Completed (shared with transmitter / esp32common)
| Item | Scope | Status |
|------|-------|--------|
| Fix ESP-NOW callback ISR API misuse (`xQueueSendFromISR` in Wi-Fi task context) | `src/espnow/espnow_callbacks.cpp` **+** `esp32common/espnow_transmitter/espnow_transmitter.cpp` | ✅ Done |
| Fix common connection event helper context misuse (task callers using ISR queue API) | `esp32common/espnow_common_utils/connection_manager.h` | ✅ Done |
| Remove repeated MQTT payload PSRAM alloc/free churn | `src/mqtt/mqtt_client.cpp` **+** `ESPnowtransmitter2/src/network/mqtt_manager.cpp` | ✅ Done |

### ⏳ Pending (shared with transmitter / esp32common)
| Item | Scope |
|------|-------|
| _None currently_ | — |

### ⏳ Pending (receiver-only, deferred)
| Item | Notes |
|------|-------|
| Standardize logging style across modules | Various |

### ✅ Completed (receiver-only follow-up)
| Item | Files changed | Status |
|------|--------------|--------|
| MQTT topic dispatch: replaced `if/else` chain with table-driven handler routing | `src/mqtt/mqtt_client.cpp` | ✅ Done |
| Battery status handlers: consolidated repetitive checksum/decode boilerplate via shared helper | `src/espnow/handlers/status_handler_common.h`, `src/espnow/handlers/{battery_status,charger_status,inverter_status,system_status}_handler.cpp` | ✅ Done |
| Canonical telemetry snapshot model with atomic update/read API and compatibility sync for legacy globals | `src/espnow/battery_data_store.h/.cpp`, `src/espnow/handlers/{battery_status,battery_info,charger_status,inverter_status,system_status}_handler.cpp` | ✅ Done |
| Web monitor consumers migrated to canonical snapshot telemetry (`api_monitor` + monitor SSE) | `lib/webserver/api/api_telemetry_handlers.cpp`, `lib/webserver/api/api_sse_handlers.cpp` | ✅ Done |
| Legacy/simple telemetry path now publishes into canonical snapshot; `ConnectionStateManager` data access aligned to snapshot reads/writes | `src/espnow/battery_data_store.h/.cpp`, `src/espnow/espnow_tasks.cpp`, `src/state/connection_state_manager.cpp` | ✅ Done |
| Further reduced `ConnectionStateManager` dependency on ESP-NOW caches by composing setter updates from canonical snapshot values; simple telemetry change detection now uses local cache fields in worker path | `src/state/connection_state_manager.cpp`, `src/espnow/espnow_tasks.cpp` | ✅ Done |

---

## Scope
This review focuses on **actual runtime code paths** in `espnowreceiver_2`, with emphasis on:
- boot/setup flow
- ESP-NOW receive path
- connection/state management
- display update path
- MQTT ingest path
- shared data/state ownership

Primary files reviewed include:
- `src/main.cpp`
- `src/state_machine.cpp`
- `src/espnow/espnow_callbacks.cpp`
- `src/espnow/espnow_tasks.cpp`
- `src/espnow/rx_connection_handler.cpp`
- `src/mqtt/mqtt_client.cpp`
- `src/display/display.cpp`
- `src/display/display_manager.cpp`
- `src/display/display_update_queue.cpp`
- `src/espnow/battery_data_store.cpp`
- `src/state/received_data_manager.cpp`
- `src/espnow/receiver_connection_manager.cpp`
- `src/espnow/handlers/*.cpp`

This is a **functional / maintainability / efficiency** review, not just formatting/style feedback.

---

## Executive Summary
The receiver has improved significantly in structure, but the current codebase still carries **multiple overlapping architectural layers** from earlier phases. The main pattern I found is:

> newer abstractions were added, but older pathways were often left in place rather than fully retired.

That creates three concrete problems:
1. **too many sources of truth** for the same runtime state,
2. **duplicate orchestration logic** around display and connection setup,
3. **large switchboard-style functions** that are doing too much and are harder to test.

The code does work, but it is now at the point where the biggest gains will come from **consolidation**, not from more incremental feature additions.

---

## What is already good
Several parts of the receiver are materially better than older iterations and should be preserved:

### 1) Display snapshot queue is a good direction
`DisplayUpdateQueue` is a strong design choice.
- It decouples message ingest from TFT rendering.
- It limits queue depth.
- It uses a sensible drop-oldest policy when saturated.
- It reduces mutex hold time in the hot receive path.

This is one of the cleaner pieces of the current runtime architecture.

### 2) `TftDisplay` backend separation is the right long-term shape
The newer `display.cpp` + `IDisplay` backend abstraction is much cleaner than the old direct-global-TFT approach.
That direction should continue.

### 3) `ComponentApplyTracker` is relatively well-contained
Compared with older global-state patterns in the project, `ComponentApplyTracker` is a good example of a bounded subsystem with explicit state transitions and snapshot retrieval.

### 4) Connection logic is moving in the right direction
`RxStateMachine`, `RxHeartbeatManager`, and `ReceiverConnectionHandler` show a move toward explicit state coordination instead of implicit flags.
That is the correct direction.

---

## High-priority findings

## 1) The display stack currently has overlapping ownership layers
### What I found
`src/main.cpp` does both of these during setup:
- `Display::DisplayManager::init(&tft_driver);`
- `init_display();`

At the same time:
- `src/display/display_manager.cpp` creates its **own mutex** and manages backlight/power.
- `src/main.cpp` separately creates `RTOS::tft_mutex`.
- `src/display/display.cpp` owns the active backend through `Display::g_display`.

So the display system currently has **at least three overlapping layers**:
1. `DisplayManager`
2. legacy/global wrapper functions (`init_display`, `display_soc`, `display_power`, etc.)
3. raw RTOS TFT mutex ownership in `main.cpp`

### Why this is a problem
This is the clearest example of architectural duplication in the receiver.
It increases the chance of:
- mismatched initialization order,
- duplicated backlight logic,
- mutex confusion,
- future bugs where one path is updated and the other is forgotten.

### Recommendation
Pick **one display owner** and remove the rest.

#### Best option
Keep:
- `display.cpp` / `IDisplay` / `TftDisplay`
- `DisplayUpdateQueue`

Retire or fold in:
- `DisplayManager`
- direct display hardware init duplication in `main.cpp`
- any remaining legacy direct-TFT wrapper assumptions

### Impact
High maintainability gain.
Moderate bug-risk reduction.
Low runtime improvement, but big structural improvement.

---

## 2) ESP-NOW receive callback is written as if it were ISR context
### What I found
In `src/espnow/espnow_callbacks.cpp`, `on_data_recv()` uses:
- `xQueueSendFromISR(...)`
- `uxQueueMessagesWaitingFromISR(...)`
- `portYIELD_FROM_ISR(...)`

However, Espressif documents that ESP-NOW send/receive callbacks run from the **high-priority Wi-Fi task**, not from an ISR.

### Why this matters
This is not just stylistic.
It means the callback is using the wrong API family for its execution context.
Even if it appears to work, it is semantically misleading and increases maintenance risk.
It also makes future developers think this path has ISR restrictions that it does not actually have.

### Recommendation
Refactor `on_data_recv()` to use task-context APIs and treat it as a very short Wi-Fi-task callback:
- keep the callback minimal,
- copy packet data,
- enqueue with normal queue APIs,
- remove ISR-specific yield logic.

### Impact
High correctness/readability improvement.
Potential stability improvement.

---

## 3) There are too many overlapping state stores for the same data
### What I found
The receiver currently carries multiple overlapping state containers:
- `ESPNow::received_soc`, `ESPNow::received_power`, dirty flags, etc.
- `DisplayUpdateQueue::Snapshot`
- `BatteryData::*`
- `ReceivedDataManager`
- `TransmitterManager` cached runtime/config metadata

In particular:
- `ReceivedDataManager` appears to be effectively unused in the current live path.
- `BatteryData` is used for battery-emulator style telemetry.
- `ESPNow::*` globals are still used for immediate display/web/runtime state.

### Why this is a problem
This makes it hard to answer basic questions like:
- What is the authoritative SOC?
- Which state is safe to read from web pages?
- Which state should drive the display?
- Which fields are updated atomically vs independently?

It also increases the chance of stale, partially updated, or contradictory values.

### Recommendation
Create one explicit runtime snapshot model, for example:
- `ReceiverLiveSnapshot` for live telemetry,
- `ReceiverStaticSnapshot` for static/configuration data.

Then make all other layers consume those snapshots instead of owning their own duplicate globals.

#### Immediate cleanup candidates
- Remove `ReceivedDataManager` if it is no longer used.
- Reduce `ESPNow::*` globals to only queue/task counters if needed.
- Keep `BatteryData` only if it becomes the canonical telemetry snapshot.

### Impact
High maintainability improvement.
High reduction in cognitive load.
Medium correctness improvement.

---

## 4) `setup_message_routes()` is too large and too mixed in responsibility
### What I found
`src/espnow/espnow_tasks.cpp` contains a very large `setup_message_routes()` function registering many routes with inline lambdas.
It mixes:
- probe/ack setup,
- live telemetry handlers,
- settings/config handlers,
- type catalog handlers,
- metadata/version handling,
- side effects into caches/managers.

### Why this is a problem
It is hard to:
- reason about route coverage,
- test handlers in isolation,
- see which routes are “core connection” vs “battery domain” vs “web/config cache updates”.

The function is acting as a router registry, a domain coordinator, and a dependency wiring layer all at once.

### Recommendation
Split route setup by domain, for example:
- `register_connection_routes(router)`
- `register_live_data_routes(router)`
- `register_catalog_routes(router)`
- `register_config_routes(router)`
- `register_metadata_routes(router)`

A further improvement would be a small table-driven registration structure rather than repeated `router.register_route(...)` boilerplate.

### Impact
High maintainability improvement.
Moderate testability improvement.

---

## 5) Version-beacon handling duplicates request construction repeatedly
### What I found
Inside the version-beacon route in `src/espnow/espnow_tasks.cpp`, the code manually builds and sends nearly identical `config_section_request_t` messages multiple times:
- MQTT config request
- network config request
- battery config request

A helper `request_config_section(...)` already exists earlier in the file, but the version-beacon handler still duplicates the request assembly/send/logging logic.

### Why this is a problem
This is a classic consolidation opportunity.
Repeated code here makes it easier for one request path to drift from the others.

### Recommendation
Make section refresh fully table-driven.
For example:
- define a small array of `SectionRefreshRule`
- evaluate each rule in one loop
- send through one helper

This would replace several ad-hoc blocks with one compact flow.

### Impact
Moderate maintainability gain.
Low runtime impact.

---

## Medium-priority findings

## 6) MQTT payload handling does unnecessary allocation/copying per message
### What I found
In `src/mqtt/mqtt_client.cpp`, `messageCallback()`:
- allocates PSRAM for every incoming message,
- copies the payload,
- null-terminates it,
- then dispatches by topic with a long `if / else if` chain.

But most handlers already call `deserializeJson(doc, json_payload, length)`, which means the code conceptually already works with a buffer + length.

### Why this is a problem
Per-message allocation/copy is unnecessary overhead in a callback-driven path.
It is not catastrophic, but it is avoidable churn.

### Recommendation
Refactor to:
1. use a topic-dispatch table,
2. parse directly from the incoming byte buffer and length,
3. centralize the repeated `deserializeJson + error handling + store` pattern.

This is a strong candidate for replacing many similar handler functions with one reusable helper template.

### Example shape
- `dispatch_topic(topic, payload, length)`
- `parse_json_and_apply<TDocSize>(payload, length, handler)`

### Impact
Moderate efficiency gain.
High maintainability gain.

---

## 7) Battery status handlers are repetitive and can be collapsed into a common primitive
### What I found
The handlers in:
- `handlers/battery_status_handler.cpp`
- `handlers/battery_info_handler.cpp`
- `handlers/charger_status_handler.cpp`
- `handlers/inverter_status_handler.cpp`
- `handlers/system_status_handler.cpp`

all follow the same pattern:
1. cast message,
2. validate checksum,
3. copy/convert fields into `BatteryData`,
4. set a `*_received` flag,
5. log.

### Why this matters
This is exactly the kind of area where “numerous functions could be replaced by a single less complex function” is true.

### Recommendation
Introduce a reusable helper, for example:
- validate fixed-size payload,
- verify checksum,
- apply domain-specific field copy through a lambda.

That leaves each handler as only the data mapping, e.g.
- `decode_status_message<T>(msg, "Battery status", [](const T& data){ ... })`

This reduces boilerplate and makes checksum/size policy consistent.

### Impact
Moderate maintainability gain.
Low runtime gain.
Good simplification opportunity.

---

## 8) `BatteryData` updates are not grouped into an atomic snapshot ✅ Addressed (2026-03-18)
### What I found
`BatteryData` uses many separate global `volatile` fields.
Handlers update them field-by-field.
Readers can observe partially updated state across a message boundary.

### Why this is a problem
`volatile` does not make multi-field updates atomic.
That means a web page or another task could read:
- new SOC,
- old power,
- old status flag,
- mixed timestamps.

### Implemented
- Added canonical `TelemetrySnapshot` + per-section `SectionState` metadata in `battery_data_store`.
- Added atomic update APIs (`update_battery_status/info/charger/inverter/system`) protected by a shared mutex.
- Added atomic snapshot read API (`read_snapshot`) and sequence tracking (`snapshot_sequence`).
- Added staleness refresh (`refresh_staleness`) with per-section thresholds.
- Kept legacy `BatteryData::*` globals for compatibility and synchronized them from snapshot updates.

### Impact
Moderate correctness improvement.
Moderate maintainability gain.

---

## 9) `main.cpp` still contains orchestration duplication and residual setup debt
### What I found
`src/main.cpp` is cleaner than older versions but still does too much directly:
- display setup,
- filesystem setup,
- Wi-Fi setup,
- web server setup,
- ESP-NOW setup,
- queue creation,
- route setup,
- task creation,
- discovery startup,
- connection manager init,
- heartbeat init,
- state machine transitions.

Also, `transition_to_state(SystemState::WAITING_FOR_TRANSMITTER);` is called twice.

### Why this is a problem
This makes setup hard to evolve safely.
A failure or reordering change can have side effects across many unrelated subsystems.

### Recommendation
Create a single boot coordinator with explicit phases such as:
- `init_storage()`
- `init_display_stack()`
- `init_network_stack()`
- `init_runtime_services()`
- `start_tasks()`
- `start_protocols()`

This would make startup dependencies far more obvious.

### Impact
Moderate maintainability gain.
Low runtime impact.

---

## 10) `ReceiverConnectionManager` appears to be vestigial next to the newer connection path
### What I found
There is a substantial `ReceiverConnectionManager` implementation, but the current runtime path is already using:
- `EspNowConnectionManager`
- `ReceiverConnectionHandler`
- `RxHeartbeatManager`
- `RxStateMachine`

`ReceiverConnectionManager` appears not to be part of the primary active path anymore.

### Why this is a problem
Large unused subsystems are expensive:
- they confuse review,
- they suggest alternative flows that do not really exist,
- they discourage confident refactoring.

### Recommendation
Confirm whether it is dead.
If yes:
- remove it,
- or move it to `archive/` with a clear note.

If not dead, document exactly where it participates.

### Impact
Moderate maintainability gain.
Large reduction in architectural ambiguity.

---

## Lower-priority findings

## 11) `ComponentConfigHandler` is data-heavy and manual
The component configuration handler works, but it is very table-like code written manually:
- manual NVS key constants,
- manual getters/setters,
- manual type-name arrays,
- manual field assignment.

This is not the worst part of the codebase, but it would benefit from a small data-driven mapping layer if you revisit it.

---

## 12) Logging style is inconsistent across modules
Some paths use:
- `LOG_INFO/LOG_WARN/LOG_ERROR`
while others still use:
- `ESP_LOG*`
- `Serial.printf`
- `Serial.println`

This makes operational logs harder to reason about and harder to filter uniformly.

Recommendation: standardize on one logging interface for app code.

---

## Consolidation opportunities with the best return
If the goal is “replace many functions with fewer, simpler, more reliable primitives”, these are the best targets:

### A) Unify display ownership
Replace:
- `DisplayManager`
- duplicated backlight init
- duplicate mutex concepts
with one display service.

### B) Replace repeated config-request blocks with one section-refresh loop
Strong win, low risk.

### C) Replace repeated message handlers with generic decode/apply helpers
Good win, especially for battery-related handlers.

### D) Replace scattered globals with one runtime snapshot model
This is the most important structural improvement after display unification.

### E) Replace MQTT `if/else` topic routing with a table-driven dispatcher
Simple, worthwhile cleanup.

---

## Recommended implementation order

### Phase 1 — Safe consolidation
1. ~~Fix ESP-NOW callback context handling (`FromISR` removal in task-context callback).~~ ✅ **Done 2026-03-17** (receiver + esp32common)
2. ~~Remove duplicate state transition in `main.cpp`.~~ ✅ **Done 2026-03-17**
3. ~~Refactor version-beacon section requests through one helper/loop.~~ ✅ **Done 2026-03-17**
4. ~~Refactor MQTT topic dispatch into a handler table.~~ ✅ **Done 2026-03-17**

### Phase 2 — Architecture cleanup
5. ~~Choose one display ownership model and retire the others.~~ ✅ **Done 2026-03-17** (DisplayManager + TftEspiDisplayDriver removed; display_led now uses global `tft` directly)
6. ~~Remove or archive vestigial modules (`ReceivedDataManager`, `ReceiverConnectionManager`).~~ ✅ **Done 2026-03-17**
7. Standardize logging style. ⏳ **pending**

### Phase 3 — Data model simplification
8. ~~Introduce a canonical live telemetry snapshot.~~ ✅ **Done 2026-03-18**
9. ~~Convert battery handlers to decode/apply helpers.~~ ✅ **Done 2026-03-17** (shared status-handler helper)
10. ~~Make web/display consumers read from canonical snapshots rather than parallel globals.~~ ✅ **Done 2026-03-18** (all consumers migrated; ESPNow::received_soc/power/voltage_mv globals deleted; 453-line #if 0 dead block removed from ConnectionStateManager; canonical snapshot is sole source of truth for live telemetry)

---

## Suggested target architecture
A cleaner receiver structure would look like this:

### Boot layer
- startup coordinator
- explicit init phases

### Protocol ingest layer
- ESP-NOW callback copies packets to queue
- worker task decodes/routs messages
- MQTT callback dispatches through a topic table

### State layer
- one canonical runtime snapshot
- one canonical static/config snapshot
- clear ownership of connection state

### Presentation layer
- display queue consumes snapshots
- web APIs consume snapshots
- no direct dependency on low-level ESP-NOW globals

This would remove most of the ambiguity now present in the tree.

---

## Final judgement
The receiver is **not badly written**, but it is carrying the weight of its own evolution.
The main issue is no longer algorithm quality — it is **layer overlap**.

The best improvements from here are not micro-optimizations.
They are:
- deleting redundant paths,
- centralizing state ownership,
- replacing repeated procedural blocks with small reusable primitives.

If you want the single highest-value next step, I would choose this:

> **Fully unify the display stack and remove the leftover parallel display ownership model.**

If you want the safest first code change, I would choose this:

> **Fix `espnow_callbacks.cpp` to use task-context queue handling rather than ISR APIs.**

If you want the biggest simplification-by-refactor target, I would choose this:

> **Introduce a canonical receiver snapshot model and retire duplicate live-state containers.**

---

## Recommended follow-up work I can do next
If you want, I can now take this review and turn it into a concrete execution plan, for example:
1. implement the ESP-NOW callback fix,
2. remove dead/vestigial modules safely,
3. unify the display stack,
4. refactor the battery handlers into a generic decode/apply framework.
