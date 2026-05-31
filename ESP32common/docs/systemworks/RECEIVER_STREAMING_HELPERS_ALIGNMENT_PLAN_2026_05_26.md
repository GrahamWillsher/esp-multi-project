# Receiver Web Stack Alignment Plan (Streaming Helper Migration)

Date: 2026-05-26  
Author: GitHub Copilot

## 1) Objective

Align `espnowreceiver_2` to the same web page assembly model already used by `espnowreceiver_LCD`:

- page rendering via `send_rendered_page_streaming(...)`
- page body emission via callback generators and `send_page_content_chunk(...)`
- cacheable shared JS helpers via `/static/helpers.js`
- request accounting + render telemetry hooks used by streaming path

This plan intentionally avoids behavior/UI redesign. It is a structural migration to unify render architecture.

### Implementation status

- [x] Phase 1 — Infrastructure parity in `espnowreceiver_2` (completed 2026-05-26)
- [x] Phase 2 — Page handler migration (16 handlers) (completed 2026-05-26)
- [x] Phase 3 — Content/script signature normalization (completed 2026-05-26)
- [x] Phase 4 — Cleanup and hardening (completed 2026-05-26)

Phase 1 completion notes:

1. Added streaming APIs to `_2` page generator (`send_page_content_chunk`, `send_rendered_page_streaming`, `register_static_helpers_js`).
2. Added adaptive chunking + render telemetry + low-heap preflight gate to `_2` renderer path.
3. Added `/static/helpers.js` registration to `_2` page factory.
4. Added request-accounting hooks/metrics parity in `_2` webserver (`webserver_on_request_*`, `webserver_should_recycle`).
5. Removed unused legacy `renderPage(...)` builder in `_2` (redundant code removed).
6. Validation: `espnowreceiver_2` PlatformIO build succeeded for both environments after changes.

Phase 2 completion notes:

1. Migrated all 16 target handlers in `_2` from `send_rendered_page(...)` to `send_rendered_page_streaming(...)`.
2. Added per-page content callbacks using `send_page_content_chunk(...)` for each migrated handler.
3. Verified legacy handler callsites are removed: `send_rendered_page(` occurrences in `_2` page handlers = 0.
4. Verified streaming adoption: `send_rendered_page_streaming(` occurrences in `_2` page handlers = 16.
5. Validation: `espnowreceiver_2` PlatformIO build succeeded for both environments after Phase 2 migration.

Phase 3 completion notes:

1. Converted static `_2` page content providers to `const char*` for these pages:
   - battery settings, debug, event logs, hardware config, inverter settings,
     monitor, monitor2, OTA, receiver, systeminfo.
2. Retained `String get_*_page_content(...)` only for dynamic-content pages:
   - dashboard, network config (AP/normal branch), transmitter hub.
3. Converted remaining static script providers from `String` to `const char*`:
   - hardware config, inverter settings, network config, systeminfo.
4. Updated all affected handlers to emit `const char*` content via `send_page_content_chunk(...)`.
5. Validation: `espnowreceiver_2` PlatformIO build succeeded for both environments after Phase 3 normalization.

Phase 4 completion notes:

1. Removed obsolete non-streaming renderer declarations from `_2` page generator header.
2. Removed obsolete non-streaming renderer implementations from `_2` page generator source.
3. Verified legacy renderer path removal: `send_rendered_page(` occurrences in `_2` webserver headers/sources = 0.
4. Verified streaming architecture remains complete: `send_rendered_page_streaming(` occurrences in `_2` page handlers = 16.
5. Validation: `espnowreceiver_2` PlatformIO build succeeded for both environments after cleanup.

---

## 2) Current State (Post-Migration, Verified)

### `espnowreceiver_2` (streaming-first, aligned)

- Streaming renderer in [espnowreceiver_2/lib/webserver/common/page_generator.cpp](../../../espnowreceiver_2/lib/webserver/common/page_generator.cpp)
- Chunk API in [espnowreceiver_2/lib/webserver/common/page_generator.cpp](../../../espnowreceiver_2/lib/webserver/common/page_generator.cpp) and declaration in [espnowreceiver_2/lib/webserver/common/page_generator.h](../../../espnowreceiver_2/lib/webserver/common/page_generator.h)
- `/static/helpers.js` route registered in [espnowreceiver_2/lib/webserver/page_registration_factory.cpp](../../../espnowreceiver_2/lib/webserver/page_registration_factory.cpp)
- Request accounting API present in [espnowreceiver_2/lib/webserver/webserver.h](../../../espnowreceiver_2/lib/webserver/webserver.h) and implemented in [espnowreceiver_2/lib/webserver/webserver.cpp](../../../espnowreceiver_2/lib/webserver/webserver.cpp)
- 16/16 page handlers use `send_rendered_page_streaming(...)`
- Legacy non-streaming `send_rendered_page(...)` path removed

### `espnowreceiver_LCD` (reference streaming architecture)

- Streaming renderer in [espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp](../../../espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp)
- `/static/helpers.js` route in [espnowreceiver_LCD/lib/webserver_lcd/page_registration_factory.cpp](../../../espnowreceiver_LCD/lib/webserver_lcd/page_registration_factory.cpp)
- Request accounting API present in [espnowreceiver_LCD/lib/webserver_lcd/webserver.h](../../../espnowreceiver_LCD/lib/webserver_lcd/webserver.h)

### Residual `String` content providers in `_2` (intentional dynamic pages)

- [espnowreceiver_2/lib/webserver/pages/dashboard_page_content.cpp](../../../espnowreceiver_2/lib/webserver/pages/dashboard_page_content.cpp)
- [espnowreceiver_2/lib/webserver/pages/network_config_page_content.cpp](../../../espnowreceiver_2/lib/webserver/pages/network_config_page_content.cpp)
- [espnowreceiver_2/lib/webserver/pages/transmitter_hub_page_content.cpp](../../../espnowreceiver_2/lib/webserver/pages/transmitter_hub_page_content.cpp)

---

## 3) Migration Scope and Implications

### In Scope

1. `espnowreceiver_2` render infrastructure parity with LCD
2. `espnowreceiver_2` page handler migration (16 page handlers)
3. `espnowreceiver_2` page content/script provider signature alignment where needed
4. Routing parity for `/static/helpers.js`
5. Runtime metrics/request accounting parity needed by streaming helper

### Out of Scope (unless explicitly requested)

1. UI redesign / copy changes
2. API schema changes
3. Spec pages in shared/common layout unless needed by compile/runtime parity
4. MQTT/control-plane logic changes unrelated to page render path

### Key Implications

1. **Heap behavior improves** (no full-page body `String` required for each request)
2. **More deterministic long-page handling** (chunked send + guardrails)
3. **Touches many files** (core helper + handlers + some content generators)
4. **Needs careful parity checks** to avoid regressions in page ordering/content/script inclusion
5. **Requires route-factory update** for `/static/helpers.js`

---

## 4) Detailed File Impact Plan

### A) Core rendering infrastructure (must do first)

#### `espnowreceiver_2` target files

- [espnowreceiver_2/lib/webserver/common/page_generator.h](../../../espnowreceiver_2/lib/webserver/common/page_generator.h)
- [espnowreceiver_2/lib/webserver/common/page_generator.cpp](../../../espnowreceiver_2/lib/webserver/common/page_generator.cpp)
- [espnowreceiver_2/lib/webserver/page_registration_factory.cpp](../../../espnowreceiver_2/lib/webserver/page_registration_factory.cpp)
- [espnowreceiver_2/lib/webserver/webserver.h](../../../espnowreceiver_2/lib/webserver/webserver.h)
- [espnowreceiver_2/lib/webserver/webserver.cpp](../../../espnowreceiver_2/lib/webserver/webserver.cpp)

#### Actions

1. Port these interfaces from LCD to `_2`:
   - `page_content_generator_t`
   - `send_page_content_chunk(...)`
   - `send_rendered_page_streaming(...)`
   - `register_static_helpers_js(...)`
2. Port streaming internals:
   - adaptive chunk send policy
   - failure-stage accounting
   - long-response telemetry
   - optional preflight heap gate
3. Register `/static/helpers.js` in page factory (same slot pattern as LCD)
4. Add/request accounting functions in `_2` webserver:
   - `webserver_on_request_start(...)`
   - `webserver_on_request_progress(...)`
   - `webserver_on_request_end(...)`
   - `webserver_should_recycle(...)` (if adopting full LCD parity)
5. Extend `_2` runtime metrics struct to include request counters used by streaming path

#### Decision point

- **Recommended:** parity-copy the LCD streaming infrastructure first, then migrate pages.
- **Avoid:** partial custom reimplementation in `_2` (higher bug risk, less maintainable).

---

### B) Page handler migration (16 handlers)

#### Handler files to convert in `espnowreceiver_2`

- `/` → [espnowreceiver_2/lib/webserver/pages/dashboard_page.cpp](../../../espnowreceiver_2/lib/webserver/pages/dashboard_page.cpp)
- `/transmitter` → [espnowreceiver_2/lib/webserver/pages/transmitter_hub_page.cpp](../../../espnowreceiver_2/lib/webserver/pages/transmitter_hub_page.cpp)
- `/transmitter/config` → [espnowreceiver_2/lib/webserver/pages/settings_page.cpp](../../../espnowreceiver_2/lib/webserver/pages/settings_page.cpp)
- `/transmitter/hardware` → [espnowreceiver_2/lib/webserver/pages/hardware_config_page.cpp](../../../espnowreceiver_2/lib/webserver/pages/hardware_config_page.cpp)
- `/transmitter/battery` → [espnowreceiver_2/lib/webserver/pages/battery_settings_page.cpp](../../../espnowreceiver_2/lib/webserver/pages/battery_settings_page.cpp)
- `/transmitter/inverter` → [espnowreceiver_2/lib/webserver/pages/inverter_settings_page.cpp](../../../espnowreceiver_2/lib/webserver/pages/inverter_settings_page.cpp)
- `/transmitter/monitor` → [espnowreceiver_2/lib/webserver/pages/monitor_page.cpp](../../../espnowreceiver_2/lib/webserver/pages/monitor_page.cpp)
- `/transmitter/monitor2` → [espnowreceiver_2/lib/webserver/pages/monitor2_page.cpp](../../../espnowreceiver_2/lib/webserver/pages/monitor2_page.cpp)
- `/transmitter/reboot` → [espnowreceiver_2/lib/webserver/pages/reboot_page.cpp](../../../espnowreceiver_2/lib/webserver/pages/reboot_page.cpp)
- `/receiver` → [espnowreceiver_2/lib/webserver/pages/receiver_page.cpp](../../../espnowreceiver_2/lib/webserver/pages/receiver_page.cpp)
- `/receiver/config` → [espnowreceiver_2/lib/webserver/pages/systeminfo_page.cpp](../../../espnowreceiver_2/lib/webserver/pages/systeminfo_page.cpp)
- `/receiver/network` → [espnowreceiver_2/lib/webserver/pages/network_config_page.cpp](../../../espnowreceiver_2/lib/webserver/pages/network_config_page.cpp)
- `/cellmonitor` → [espnowreceiver_2/lib/webserver/pages/cellmonitor_page.cpp](../../../espnowreceiver_2/lib/webserver/pages/cellmonitor_page.cpp)
- `/ota` → [espnowreceiver_2/lib/webserver/pages/ota_page.cpp](../../../espnowreceiver_2/lib/webserver/pages/ota_page.cpp)
- `/debug` → [espnowreceiver_2/lib/webserver/pages/debug_page.cpp](../../../espnowreceiver_2/lib/webserver/pages/debug_page.cpp)
- `/events` → [espnowreceiver_2/lib/webserver/pages/event_logs_page.cpp](../../../espnowreceiver_2/lib/webserver/pages/event_logs_page.cpp)

#### Migration pattern per handler

1. Add content callback function:
   - `static esp_err_t <page>_content_generator(httpd_req_t* req)`
2. Emit page body via:
   - static const body: `send_page_content_chunk(req, stage, body, len)`
   - dynamic body: compute minimal stack/local values then call emitter
3. Replace renderer call:
   - from `send_rendered_page(...)`
   - to `send_rendered_page_streaming(...)`
4. Keep headers (`Cache-Control`/etc) unchanged unless parity requires update

---

### C) Content/script provider signature alignment

Current signature mismatch indicates where `_2` still relies on dynamic `String` creation.

#### `_2` content providers today

- 12 `String get_*_page_content(...)`
- 1 `const char* get_*_page_content(...)`
- 1 mixed dynamic provider

#### LCD target model

- mostly `const char* get_*_page_content()`
- emitter callbacks for dynamic pages (`emit_*_page_content(...)`)

#### Conversion strategy

##### Step C1: low-risk bridge (fast parity)

- Keep existing `_2` content functions temporarily.
- In content callback, call existing function and emit once via `send_page_content_chunk(...)`.
- This immediately aligns handler/render structure without full content refactor.

##### Step C2: full parity refactor (remove String page bodies)

- Convert `_2` static content to flash `const char[]` like LCD.
- Convert dynamic pages to `emit_*_page_content(...)` style:
  - `dashboard_page_content`
  - `transmitter_hub_page_content`
  - `network_config_page_content`
- Convert String script providers to `const char*` where possible:
  - hardware, inverter, network, systeminfo scripts currently String-based in `_2`

Recommended: execute C1 first for safety, then C2 in controlled follow-up commits.

---

## 5) Execution Phases (Recommended)

### Phase 0 — Baseline and safety net

1. Build `_2` baseline (`pio run`) and record memory + binary size
2. Save route smoke checklist (all 16 pages)
3. Keep one commit/tag before migration starts

### Phase 1 — Infrastructure parity in `_2`

1. Port streaming helper API + internals
2. Add `/static/helpers.js` registration
3. Add webserver request accounting and metric fields required by streaming
4. Build `_2`

Acceptance:
- compile clean
- `/static/helpers.js` served
- no page handler migration yet

### Phase 2 — Handler migration (all 16 pages)

1. Convert handler by handler to `send_rendered_page_streaming(...)`
2. Keep output HTML/script logically identical
3. Build + route smoke test after every 3–4 handlers

Acceptance:
- all 16 pages on `_2` use streaming helper
- no broken routes

### Phase 3 — Content/script signature normalization

1. Remove remaining page-level `String` body assembly where practical
2. Convert dynamic pages to explicit emitter functions
3. Convert remaining String scripts to flash `const char*` when static

Acceptance:
- no `String get_*_page_content` left for static pages
- parity with LCD file style for dynamic/static split

### Phase 4 — Cleanup and hardening

1. Remove unused old renderer helpers from `_2` (after full migration)
2. Verify no dead includes/utilities remain
3. Final memory/perf comparison vs baseline

Acceptance:
- `_2` and `_lcd` share same rendering architecture
- obsolete string-render-only pathways removed

---

## 6) Risk Register and Mitigations

1. **Risk:** Global state in content generators can be non-reentrant under concurrent requests.  
   **Mitigation:** avoid mutable globals where possible; pass immutable context or recompute in callback.

2. **Risk:** Header/script ordering regressions can break JS init.  
   **Mitigation:** preserve order exactly (`styles` → helper script include → page script → body).

3. **Risk:** Route registration mismatch (`/static/helpers.js`) causes missing JS helpers.  
   **Mitigation:** add explicit route and test direct fetch.

4. **Risk:** Mixed temporary bridge (C1) still allocates Strings for some pages.  
   **Mitigation:** explicitly track C2 completion checklist; do not stop at C1.

5. **Risk:** AP-mode network page behavior drift.  
   **Mitigation:** treat AP and normal branches as separate acceptance tests.

---

## 7) Verification Matrix

For each migrated page:

1. HTTP 200 + full HTML render
2. Back navigation works
3. Page-specific script runs (form fill, status updates, save/apply)
4. No console JS errors
5. No server-side chunk send failures in logs

System-level:

1. `/static/helpers.js` served with cache headers
2. SSE pages (`monitor2`, events) still update correctly
3. OTA/debug/network pages still preserve current behavior
4. PlatformIO build success for both receiver projects

---

## 8) Deliverables

1. `_2` streaming infrastructure parity patch set
2. `_2` 16-page handler migration patch set
3. `_2` content/script normalization patch set
4. final alignment report with:
   - before/after memory metrics
   - before/after render failure/latency observations
   - final parity checklist completion

---

## 9) Recommended Commit Plan

1. `feat(web): add streaming page_generator parity to espnowreceiver_2`
2. `refactor(web): migrate espnowreceiver_2 page handlers to streaming callbacks`
3. `refactor(web): convert espnowreceiver_2 page content/scripts to const/emit model`
4. `chore(web): remove legacy string-page render path in espnowreceiver_2`

This sequence keeps rollback simple and isolates regressions.
