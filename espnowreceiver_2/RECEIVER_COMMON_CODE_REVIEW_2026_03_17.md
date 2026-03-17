# Receiver Common-Code Review (Message Handling, Display, Web/UI)

**Date:** 2026-03-17  
**Project:** `espnowreceiver_2`  
**Primary question:** where should the receiver stop owning bespoke logic and instead reuse or slightly extend `esp32common`?  
**Scope reviewed:**
1. ESP-NOW and MQTT-facing receiver message path
2. Display path and display abstractions
3. HTTP API, SSE, and webpage/UI flow
4. Save vs save+reboot orchestration
5. Small `esp32common` changes that would unlock much larger receiver simplification

---

## Executive Summary

The receiver is already **partly common-first**, but not consistently so.

The strongest architectural foundations are already in place:
- shared ESP-NOW router and standard handlers from `esp32common`
- shared HTTP JSON and SSE primitives from `esp32common`
- a common page shell in `page_generator.cpp`
- a generic OTA coordination pattern in `esp32common/ota/ota_coordinator.h`

The main problem now is not missing abstractions. It is that the receiver still contains a **second layer of app-specific orchestration** above those abstractions, and that second layer has grown independently in three places:
- message send/ACK workflows
- page-level save/reboot flows
- streaming/web status plumbing

### Bottom-line conclusion

The receiver should keep owning:
- transmitter-specific cached data (`TransmitterManager`)
- receiver-only UI/page content
- device-specific renderers (`tft_display.cpp`, LVGL/TFT visual choices)

But it should stop owning repeated implementations of:
- request validation + checksum building + guarded ESP-NOW send patterns
- save / save-and-wait / save-and-reboot orchestration
- SSE session lifecycle loops
- repeated “load current values, retry while loading, update button state, poll operation status” page logic

### Highest-value improvement

The single highest-value improvement is to introduce a **shared save-operation orchestration layer**:
- **simple save**: dispatch request, wait for success/failure, refresh cache/UI
- **save + reboot**: dispatch request, wait for persisted-ready, then reuse the same reboot countdown helper

That would unify:
- network save flow
- component apply flow
- future MQTT/network/system save flows
- any later “save to peer, then maybe reboot” operations

This is the biggest opportunity to make the receiver cleaner without over-generalizing the whole app.

---

## Implementation Progress (Live)

This section is updated as migration work lands so the document reflects current code state (not only target architecture).

### Completed in current branch

1. **Shared save/apply state machine added to common code**
	- Added `SaveApplyCoordinator` in
	  `esp32common/include/esp32common/patterns/save_apply_coordinator.h`.
	- This provides a reusable lifecycle for:
	  - `pending`
	  - `persisted`
	  - `ready_for_reboot`
	  - `failed`
	  - `timed_out`

2. **Receiver tracker converted to thin wrapper over common coordinator**
	- `src/espnow/component_apply_tracker.*` now delegates core state transitions to `SaveApplyCoordinator`.
	- Receiver-specific fields (battery/inverter type/interface echoes) remain local, as intended.

3. **Shared browser-side save orchestration introduced**
	- Added `window.SaveOperation` in `lib/webserver/common/page_generator.cpp`.
	- Consolidated common page behaviors:
	  - button state transitions
	  - delayed restore behavior
	  - sequential save execution
	  - component apply request dispatch + handoff to reboot coordinator

4. **Battery and inverter pages migrated to shared save helper**
	- `lib/webserver/pages/battery_settings_page.cpp`
	- `lib/webserver/pages/inverter_settings_page.cpp`
	- Result: old page-local duplicated save/apply orchestration removed.

5. **Network page migrated to shared save helper pattern**
	- `lib/webserver/pages/network_config_page.cpp` now uses `SaveOperation` for save button state and restore handling.
	- Existing validation + reboot countdown behavior preserved.

6. **Server-side API response helper introduced and first handlers migrated**
	- Added reusable API JSON response wrapper:
	  - `lib/webserver/api/api_response_utils.h`
	  - `lib/webserver/api/api_response_utils.cpp`
	- Migrated initial handler set to shared response helper + shared HTTP JSON utility:
	  - `lib/webserver/api/api_debug_handlers.cpp`
	  - `lib/webserver/api/api_control_handlers.cpp`
	- This removes repeated direct `httpd_resp_set_type` / `httpd_resp_send*` boilerplate and normalizes response behavior.

7. **Type-selection and selected telemetry handlers migrated to shared response path**
	- `lib/webserver/api/api_type_selection_handlers.cpp`
	  - moved repetitive JSON success/error response paths onto `ApiResponseUtils` / `HttpJsonUtils`
	  - kept existing no-cache status semantics for component-apply polling via shared helper function in-file
	- `lib/webserver/api/api_telemetry_handlers.cpp` (selected high-traffic handlers)
	  - moved repeated direct JSON response boilerplate to `HttpJsonUtils` in:
	    - `api_data_handler`
	    - `api_get_receiver_info_handler`
	    - `api_monitor_handler`
	    - `api_get_data_source_handler`
	    - `api_cell_data_handler`

8. **Telemetry handler response-path migration completed (remaining endpoints)**
	- Continued `lib/webserver/api/api_telemetry_handlers.cpp` migration so all JSON-returning telemetry endpoints now route through `HttpJsonUtils`.
	- Remaining direct response boilerplate in API handlers is now limited to the plain-text 404 fallback in `api_handlers.cpp` (intentional, non-JSON path).

9. **Polling/status no-cache response helper centralized**
	- Added shared no-cache JSON helper to API response utilities:
	  - `ApiResponseUtils::send_json_no_cache(...)`
	- Replaced local no-cache response helper duplication in:
	  - `lib/webserver/api/api_type_selection_handlers.cpp`
	- Result: polling cache headers are now defined in one place for reuse.

10. **API not-found fallback unified with shared response utility**
	- `lib/webserver/api/api_handlers.cpp` now uses `ApiResponseUtils::send_error_with_status(...)` for the 404 catch-all handler.
	- Result: API layer no longer contains bespoke direct response wiring; response behavior is fully centralized.

11. **Transmitter reboot page aligned to shared reboot countdown flow**
	- `lib/webserver/pages/reboot_page.cpp` now uses `window.TransmitterReboot.run(...)` from common page helpers.
	- Behavior now matches shared reboot orchestration style: immediate countdown UX, then reboot command dispatch, then redirect on success.
	- This flow intentionally does not wait for any precondition/ready feedback before starting countdown (per reboot-page use case).

12. **Unused legacy type/interface set endpoints removed**
	- Removed legacy API routes and handlers from `lib/webserver/api/api_type_selection_handlers.cpp`:
	  - `/api/set_battery_type`
	  - `/api/set_inverter_type`
	  - `/api/set_battery_interface`
	  - `/api/set_inverter_interface`
	- Removed now-unused standalone ESP-NOW send helpers:
	  - `send_component_type_selection(...)`
	  - `send_component_interface_selection(...)`
	  from `src/espnow/espnow_send.h` and `src/espnow/espnow_send.cpp`.
	- Active path remains the batched apply flow (`/api/component_apply` + `/api/component_apply_status`).

### Redundant/old code removed during migration

- Removed legacy battery/inverter page-specific save/apply orchestration blocks that duplicated request execution, button-state transitions, and recovery timing.
- Removed redundant network-page JS state (`isAPMode`) and unused `apModeWarning` toggle logic in the runtime script.
- Eliminated leftover direct button-reset code paths where `SaveOperation` now provides the shared implementation.
- Removed duplicated API JSON response scaffolding in debug/control handlers by centralizing success/error/status response formatting in `api_response_utils`.
- Removed repeated direct JSON response wiring in type-selection handlers and selected telemetry handlers by routing through shared response utilities.
- Removed remaining telemetry-side duplicated JSON response boilerplate by switching all telemetry JSON endpoints to shared response utilities.
- Removed local duplicate no-cache JSON status helper by centralizing cache-header response behavior in `api_response_utils`.
- Removed bespoke plain-text API 404 response handling by routing through shared API response utilities.
- Removed page-local reboot fetch/countdown implementation in `/transmitter/reboot` by reusing the shared `TransmitterReboot` common helper.
- Removed dead legacy type/interface `set_*` API endpoints and their unused transport helpers after migration to batched component apply flow.

### Build validation status

- Receiver build validated after each migration slice:
  - `pio run -e receiver_tft` ✅
	- `pio run -e receiver_tft` after API response-helper migration ✅
	- `pio run -e receiver_tft` after type-selection + selected telemetry response-path migration ✅
	- `pio run -e receiver_tft` after completing telemetry response-path migration ✅
	- `pio run -e receiver_tft` after API 404 fallback response unification ✅

---

## What is already reusable and should stay that way

## 1) ESP-NOW routing already uses the right common layer

The receiver already uses the correct shared foundation:
- `esp32common/espnow/message_router.h`
- `esp32common/espnow/standard_handlers.h`
- `esp32common/espnow/connection_manager.h`
- `esp32common/espnow/packet_utils.h`

This is good. The architectural direction is correct.

### Recommendation
Do **not** replace this. Instead, build on it.

### Why
The problem is not the router itself. The problem is that the receiver still packs too much receiver-specific route registration and per-message orchestration into `src/espnow/espnow_tasks.cpp`.

---

## 2) Webserver helper reuse is already happening

The receiver already wraps and uses:
- `esp32common/webserver/http_json_utils.h`
- `esp32common/webserver/http_sse_utils.h`

This is also correct.

### Recommendation
Keep these helpers, but expand them upward one level so pages/handlers stop rebuilding the same patterns above them.

### Why
Today they are still very low-level primitives:
- read request body
- send JSON
- begin/end SSE
- send ping

That is useful, but it still leaves a large amount of repeated receiver logic in handlers.

---

## 3) `page_generator.cpp` is already the right UI commonization seam

The receiver has already started centralizing shared browser behavior in:
- `lib/webserver/common/page_generator.cpp`

The addition of:
- `window.TransmitterReboot`
- `window.ComponentApplyCoordinator`

was a strong move in the right direction.

### Recommendation
Make this file the main home for all shared receiver web orchestration.

### Why
Right now it proves the concept, but only one special flow has been promoted into it. The broader save UI is still page-by-page.

---

## Review Findings

## A) Message Handling Path

## A1) `espnow_tasks.cpp` is still too dense and owns too many responsibilities

### Current state
`src/espnow/espnow_tasks.cpp` currently mixes:
- route registration
- connection/liveness side effects
- cache update rules
- display enqueue decisions
- ACK handling
- config refresh decisions
- version-sync behavior
- packet subtype fallbacks

This file is using common infrastructure, but it still behaves like a local orchestrator for nearly everything.

### Problem
That makes it harder to see which logic is:
- receiver-specific and intentional
- duplicated from other flows
- eligible for promotion into shared code

### Recommendation
Split the receiver message path into **domain registration modules** while keeping the shared router.

Suggested shape:
- `receiver_routes_core.cpp` — probe, ack, heartbeat, version-beacon wiring
- `receiver_routes_runtime.cpp` — data/events/logs/cell info
- `receiver_routes_settings.cpp` — settings ACKs, config refresh, apply ACKs
- `receiver_routes_catalog.cpp` — type catalog fragments and version sync

### Why this is better
- keeps `esp32common` router intact
- makes receiver-only policy obvious
- makes duplication visible enough to promote later
- lets shared/common helpers target a narrower integration point

This is a **receiver refactor**, not necessarily a common-library refactor, but it is the prerequisite for the next steps.

---

## A2) Send-side ESP-NOW patterns are duplicated and should move into common helpers

### Current state
`src/espnow/espnow_send.cpp` repeats the same structure across multiple functions:
- validate current connection state
- validate transmitter MAC presence
- fill packet struct
- calculate checksum manually
- call `esp_now_send`
- log success/failure

This exists for:
- debug control
- component type selection
- component interface selection
- component apply request
- test data control
- event log subscription
- type catalog requests

### Recommendation
Create a shared sender helper in `esp32common`, for example:
- `esp32common/espnow/send_utils.h`
- or `esp32common/espnow/request_dispatcher.h`

It should handle:
- peer/MAC existence checks
- optional connection-state predicate
- checksum population for supported packet shapes
- standardized logging
- standardized result mapping

### Minimal shared version
A small helper is enough:
- `bool is_valid_peer_mac(const uint8_t* mac)`
- `uint16_t checksum16(const void* data, size_t len)`
- `uint8_t checksum_xor8(const void* data, size_t len)`
- `esp_err_t guarded_send(const uint8_t* mac, const uint8_t* data, size_t len, const char* tag)`

### Why this is better
- removes a lot of copy-paste in the receiver and transmitter
- reduces the risk of checksum drift between handlers
- improves consistency of error handling
- makes protocol additions cheaper

This is one of the clearest “slight common modification” wins.

---

## A3) Component-apply orchestration proves a reusable pattern exists

**Decision:** adopt **Option 2**.

The preferred path is now a dedicated save/apply coordinator rather than stretching the OTA coordinator into a broader shape first.

### Chosen implementation direction
- add a shared `SaveApplyCoordinator` in `esp32common`
- use it as the common state machine for receiver-side save/apply workflows
- keep transport-specific request dispatch and ACK parsing outside the coordinator
- keep page-specific UX outside the coordinator

### Why Option 2 is the better fit
- the save/apply lifecycle is richer than OTA and deserves its own vocabulary
- it avoids overloading `OtaCoordinator` with non-OTA semantics
- it gives a reusable backend for component apply, network save+reboot, and later MQTT/system flows
- it lets the receiver migrate incrementally without disturbing the already-working OTA path

### Initial implementation plan
1. extract a common `SaveApplyCoordinator` into `esp32common`
2. refactor `ComponentApplyTracker` into a thin receiver wrapper over that coordinator
3. keep the current HTTP/API and page surfaces stable during the first migration
4. migrate additional save/save+reboot flows onto the same coordinator after the component path proves stable

### Current state
The new component apply flow spans:
- `api_type_selection_handlers.cpp`
- `component_apply_tracker.*`
- `espnow_send.cpp`
- `espnow_tasks.cpp`
- `page_generator.cpp`
- the battery/inverter settings pages

That is a lot of files for what is conceptually one operation:
> dispatch remote save, wait for persisted-ready, then reboot.

### Recommendation
Promote this pattern into a generic common coordinator concept.

Two reasonable options:

#### Option 1: extend the OTA-style coordinator idea
Evolve `esp32common/ota/ota_coordinator.h` into a more general operation coordinator, or create a sibling such as:
- `esp32common/operations/paired_operation_coordinator.h`

#### Option 2: add a new save/apply coordinator (**selected**)
Create a dedicated abstraction for:
- request sent
- in progress
- persisted
- ready for reboot
- failed
- timed out

### Why this is better
The receiver now has a real proof that this pattern is not OTA-specific.

The same lifecycle can cover:
- component apply
- receiver network save with reboot
- transmitter network save with reboot
- future MQTT save/apply flows

This is the biggest candidate for common promotion.

---

## A4) Category-refresh logic should become a small shared policy helper

### Current state
`handle_settings_update_ack()` and `request_category_refresh()` in `espnow_tasks.cpp` encode “ACK means request a targeted refresh for that category”.

### Problem
That policy is useful beyond one specific receiver file, but it is currently embedded in message-handling code.

### Recommendation
Move the mapping logic into a small reusable helper, for example:
- `esp32common/espnow/config_refresh_policy.h`

That helper can answer:
- what request message corresponds to a category
- whether refresh is supported
- how to label/log the refresh

### Why this is better
- isolates protocol policy from message-loop plumbing
- makes unimplemented categories explicit
- helps both receiver and transmitter-side tools reason about refresh behavior consistently

---

## A5) Catalog HTTP handlers are a good candidate for a shared response builder

### Current state
`api_type_selection_handlers.cpp` has similar handlers for:
- battery types
- inverter types
- interfaces
- selected values

The handlers repeatedly do:
- copy from cache
- sort entries
- build JSON
- return loading response if empty

### Recommendation
Add a small common helper for “catalog-to-JSON response” and “loading-or-data” semantics.

Possible location:
- `esp32common/webserver/catalog_response_utils.h`

### Why this is better
- reduces heap-heavy copy/sort/string boilerplate
- standardizes loading semantics across pages
- makes battery/inverter/interface catalogs feel like one system instead of separate custom endpoints

---

## B) Display Path

## B1) The display driver boundary is right, but it is only half-common today

### Current state
The receiver has:
- `src/display/display.cpp` as a backend selector
- `src/display/display_manager.*`
- `src/display/display_update_queue.cpp`
- backend-specific implementations like `tft_display.cpp`

There is also a HAL include path pointing into common display definitions.

### Assessment
This is directionally correct:
- the interface should be shared/common
- the renderer implementation should remain receiver/device-specific

### Recommendation
Fully standardize the display HAL boundary in `esp32common`, but keep actual layouts/render logic local.

#### Move/keep in common
- display driver interface
- display manager interface
- lock/backlight abstractions
- generic render queue primitive

#### Keep local to receiver
- SOC/power visual design
- splash screens
- battery-monitor layout choices
- TFT-specific page rendering details

### Why this is better
The common layer should define **how** a display backend is used, not **what** the receiver draws.

That preserves flexibility while preventing each codebase from inventing its own display driver lifecycle.

---

## B2) `DisplayUpdateQueue` is a reusable primitive, even if the payload is receiver-specific

### Current state
The queue model is good:
- latest-value wins behavior
- dedicated rendering task
- decoupling of ingest from draw

### Recommendation
Promote the queue/task pattern into a generic common utility if other projects need similar behavior.

A common version could be a generic “latest snapshot worker” pattern rather than a display-only queue.

### Why this is better
The important reusable idea is not the exact SOC/power payload. It is the scheduling strategy:
- decouple producer from renderer
- coalesce rapid updates
- keep slow visual work away from transport/message tasks

This belongs in common if the same pattern appears elsewhere.

---

## B3) `tft_display.cpp` should stay local

### Current state
`tft_display.cpp` contains receiver-specific rendering, visual states, and device assumptions.

### Recommendation
Do **not** move this into `esp32common`.

### Why
This is product behavior, not shared infrastructure. If moved too early, it will create a common library that is full of application-specific UI code.

The correct split is:
- commonize display plumbing
- keep rendered content local

---

## C) HTTP, SSE, and Webpage Layer

## C1) The current common web helpers are too low-level

### Current state
`HttpJsonUtils` and `HttpSseUtils` only cover primitive operations.

### Problem
Above those primitives, the receiver repeats:
- parse body / validate / send uniform error
- session metrics bookkeeping
- SSE loop lifetime
- periodic ping handling
- JSON event formatting

### Recommendation
Add a second tier of shared helpers.

Examples:
- `HttpJsonUtils::parse_json_body<T>()` or an equivalent callback wrapper
- `HttpJsonUtils::send_success(req, doc)` / `send_error(req, code, message)`
- `HttpSseUtils::send_json_event(req, doc)`
- `HttpSseUtils::run_session(...)` with hooks for connect, send snapshot, wait for change, ping, disconnect

### Why this is better
This gives the receiver reuse at the **workflow** level, not just the socket primitive level.

---

## C2) There are two different SSE styles in the receiver and they should be normalized

### Current state
`api_sse_handlers.cpp` contains:
- a cell-data SSE flow using notifier waiting and explicit JSON assembly
- a monitor SSE flow using a different event-wait loop and a different payload path

Both are valid, but they are structurally similar enough that the lifecycle code should not be repeated.

### Recommendation
Keep payload generation local, but centralize the session loop.

A common SSE runner should own:
- connect bookkeeping
- retry hint
- send initial snapshot
- wait for update or ping timeout
- disconnect cleanup

The handler should only provide:
- `buildInitialPayload()`
- `buildUpdatedPayload()`
- `waitForChange(timeout)`
- optional metrics callbacks

### Why this is better
- lower duplication
- easier to reason about SSE correctness
- easier to add new SSE streams safely

---

## C3) Page-level save logic is still too bespoke

### Current state
The settings pages repeat the same families of logic:
- load current values
- store initial values
- attach change listeners
- count changes
- update button text/colors
- save sequentially or dispatch apply request
- restore button state on failure/timeout

This is especially visible in:
- `battery_settings_page.cpp`
- `inverter_settings_page.cpp`
- `network_config_page.cpp`

### Recommendation
Create a shared browser-side operation helper in `page_generator.cpp` or a new common web JS module.

Suggested objects:
- `window.FormStateTracker`
- `window.SaveOperation`
- `window.SaveAndRebootOperation`

### Suggested responsibilities
#### `FormStateTracker`
- register fields
- snapshot initial values
- count changed fields
- update button labels

#### `SaveOperation`
- run one or more save requests
- show saving/success/failure states
- call page-specific hooks for payload generation and completion

#### `SaveAndRebootOperation`
- extend `SaveOperation`
- wait for ready state
- call `TransmitterReboot.run(...)`

### Why this is better
This directly addresses the user’s example of a unified save/save+reboot flow.

The button should not care whether the underlying operation is:
- local save only
- remote save only
- remote save plus reboot

The orchestration helper should care.

---

## C4) `network_config_page.cpp` is the clearest proof that save flows are not unified yet

### Current state
The network page has its own custom:
- change tracking
- validation
- button-state logic
- reboot countdown/redirect behavior

It does **not** use `TransmitterReboot` or a generalized save coordinator.

### Recommendation
Refactor it onto the same save framework as component apply, but with a different final mode.

Example modes:
- `mode: 'save-only'`
- `mode: 'save-then-local-redirect'`
- `mode: 'save-then-wait-for-remote-ready-then-reboot'`

### Why this is better
It is the strongest remaining example of page-specific orchestration that should become shared.

---

## C5) Legacy/parallel webserver implementation in common should be retired or explicitly archived

### Current state
`esp32common/webserver/receiver_webserver.cpp` still contains a large overlapping receiver webserver implementation.

### Problem
This creates ambiguity:
- which receiver web stack is current
- which HTML/CSS conventions are authoritative
- whether new receiver work should extend common legacy code or the modular receiver pages

### Recommendation
Do one of these:
1. archive it clearly and stop treating it as active,
2. or extract only the still-useful parts and delete the rest.

### Why this is better
A common library should not retain a parallel monolith once the modular receiver stack has taken over. That increases drift and makes future reuse decisions harder.

---

## D) Save vs Save+Reboot: Recommended Unified Model

## Proposed model

Every page-level save action should select one of three operation policies:

### 1) `SaveOnly`
Use when changes take effect immediately and no restart is required.

Flow:
1. validate form
2. dispatch save request(s)
3. wait for success/failure
4. refresh UI/cache

### 2) `SaveThenRefresh`
Use when the transmitter/receiver will asynchronously update caches/specs but no reboot is needed.

Flow:
1. validate form
2. dispatch save request(s)
3. wait for success/failure
4. trigger targeted refresh or poll status until complete
5. refresh UI

### 3) `SaveThenReboot`
Use when persistence must complete before reboot.

Flow:
1. validate form
2. dispatch transaction
3. wait for `ready_for_reboot`
4. run shared reboot countdown
5. redirect/restore UI

### Where this should live
- browser-side JS helper in the receiver web common layer
- optional C++ helper/coordinator in `esp32common` for device-side status tracking semantics

### Why this is better than the current approach
It turns the page code from “handwritten workflow” into “configuration of a shared workflow”.

That is exactly the right level of reuse here.

---

## E) What should move to `esp32common` now

## Move now

### 1) ESP-NOW checksum/send helpers
Low risk, high reuse.

### 2) Higher-level JSON/SSE utilities
Small changes with immediate benefit.

### 3) Save-operation coordination abstractions
The biggest payoff item.

### 4) Generic catalog response helpers
Useful and narrow in scope.

### 5) Display HAL/lifecycle interfaces
Only the infrastructure layer, not the rendering content.

---

## F) What should stay receiver-specific

### 1) `TransmitterManager`
This is application state, not generic infrastructure.

### 2) Battery/inverter/monitor page layouts
These are product/UI decisions.

### 3) TFT and LVGL content rendering
Keep local.

### 4) Receiver-specific route policy
The receiver should still choose which routes exist and what business behavior follows each message.

Common code should support this, not erase it.

---

## G) Small `esp32common` changes that unlock major simplification

## 1) Expand `HttpJsonUtils`
Add:
- consistent success/error helpers
- JSON body parsing wrapper
- optional query parsing helpers

**Impact:** removes repetitive handler boilerplate quickly.

## 2) Expand `HttpSseUtils`
Add:
- `send_json_event(...)`
- `run_session(...)`
- lightweight session lifecycle helper

**Impact:** unifies receiver SSE handlers.

## 3) Add `esp32common/espnow/checksum_utils.h`
Add:
- `checksum16_sum(...)`
- `checksum8_xor(...)`
- packet checksum fillers where appropriate

**Impact:** eliminates repeated ad hoc checksum loops.

## 4) Add `esp32common/espnow/send_guard_utils.h`
Add:
- common guarded send helpers
- optional peer/MAC validation
- standard log tagging

**Impact:** simplifies both receiver and transmitter send paths.

## 5) Add shared operation coordinator primitives
Either evolve `ota_coordinator.h` or add a sibling operation coordinator.

**Impact:** creates one consistent model for remote save/apply/reboot workflows.

## 6) Add shared web JS operation helpers
Even if they remain compiled into receiver pages rather than a separate asset.

**Impact:** directly removes duplication from settings pages.

---

## Prioritized Recommendations

## P0 — Do next
1. Introduce a unified save/save+reboot browser-side helper built from `page_generator.cpp`.
2. Add shared ESP-NOW checksum/send helpers in `esp32common`.
3. Split `espnow_tasks.cpp` into smaller route-domain files so reuse boundaries are obvious.

## P1 — High value
4. Add higher-level `HttpJsonUtils` and `HttpSseUtils` helpers.
5. Normalize SSE session loops onto one reusable lifecycle.
6. Refactor network save flow onto the same shared operation model used by component apply.

## P2 — Worth doing after that
7. Introduce generic catalog-response helpers.
8. Promote display HAL/lifecycle infrastructure fully into common.
9. Retire or archive the old common `receiver_webserver.cpp` monolith.

## P3 — Optional, but strong cleanup
10. Consider a general paired-operation coordinator in common for OTA/config/apply flows.
11. Consider a generic “latest snapshot worker” primitive for display and other coalescing tasks.

---

## Recommended Implementation Sequence

## Phase 1 — Fast, high-leverage cleanup
- add common checksum/send helpers
- refactor receiver send code to use them
- split `espnow_tasks.cpp` by domain

## Phase 2 — Save orchestration unification
- extend `page_generator.cpp` with form-state/save helpers
- move battery and inverter pages onto it
- move network page onto the same framework

## Phase 3 — Server-side workflow reuse
- expand `HttpJsonUtils` and `HttpSseUtils`
- normalize SSE handlers
- add shared catalog response helpers

## Phase 4 — Broader library cleanup
- promote display infrastructure boundaries to common
- archive/remove legacy overlapping `receiver_webserver.cpp` paths
- evaluate whether the OTA coordinator should become a generic operation coordinator

---

## Final Conclusion

The receiver does **not** need a large rewrite. It needs a more disciplined second pass on commonization.

The best opportunities are not in raw message parsing or renderer internals. They are in the repeated orchestration code that sits **above** the existing common primitives.

### In plain terms
The receiver already has shared bricks. What it still lacks is shared scaffolding.

If you implement only one strategic improvement, make it this:

> **Create one shared save-operation model that can express both simple save and save+reboot, then move the battery, inverter, and network pages onto it.**

That will give the cleanest improvement across:
- webpage behavior
- API predictability
- future maintainability
- common-code reuse across this receiver and any later paired-device workflows
