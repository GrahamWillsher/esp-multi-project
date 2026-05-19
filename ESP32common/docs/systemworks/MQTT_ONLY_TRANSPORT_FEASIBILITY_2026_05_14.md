# MQTT-Only Transport Feasibility Study (Remove ESP-NOW from Both Devices)

**Date:** 2026-05-14  
**Scope:** `espnowreceiver_LCD` + `ESPnowtransmitter2`  
**Question:** Is it sensible/practical to remove ESP-NOW entirely (radio init, FSM, queues, routes, handlers) and use MQTT as the single data/control conduit, including receiver→transmitter actions (e.g. static value changes)?

---

## 1) Executive Summary

**Short answer:** Yes, it is practical **if** your MQTT broker is considered a required local infrastructure dependency.  

For the current architecture, a full MQTT-only approach is technically viable and can simplify the receiver runtime by removing ESP-NOW channel-lock/discovery/reconnect pressure. It is **not** a small patch: this is a **medium-to-large refactor** because the existing control plane (settings/network/MQTT config updates, debug control, reboot/OTA start, apply/ack flows) is currently implemented over ESP-NOW.

**Recommendation:** proceed in a staged migration to MQTT command+ack topics, then remove ESP-NOW once parity tests pass.

### Direct answer to the latest question

**Would removing ESP-NOW (especially on receiver) enable the webserver to function correctly?**  
**Most likely yes for stability, but not as a guaranteed single-cause fix.**

Why this is likely to help:
- Receiver currently runs ESP-NOW radio init/FSM/recovery/worker/TX scheduler alongside webserver + LVGL + MQTT.
- Receiver MQTT task is currently gated by ESP-NOW connection/radio-pressure logic.
- Receiver web API control paths currently generate additional ESP-NOW command traffic.

Why this is not an absolute guarantee:
- Webserver performance is also affected by HTTP request pressure, payload size, SSE cadence, and dashboard rendering behavior.
- If those remain heavy, webserver issues can still occur even after ESP-NOW removal.

**Practical conclusion:** removing ESP-NOW should materially improve headroom and reduce contention on receiver, and is a sensible direction for making webserver behavior more reliable, but it must be paired with normal web workload controls (request rate/payload/cache discipline).

---

## 2) What the code currently does (evidence-based)

### Receiver (`espnowreceiver_LCD`)

1. **Boot/runtime still initializes and depends on ESP-NOW path**
   - `src/main.cpp` boots `espnow_radio` and `espnow_state` phases.
   - `src/espnow/espnow_runtime.cpp` initializes radio (`esp_now_init`), callbacks, peer management, connection/recovery hooks, and worker loop.
   - `src/runtime/runtime_task_startup.cpp` creates ESP-NOW worker task and ESP-NOW TX scheduler.

2. **Receiver MQTT is currently gated by ESP-NOW state and radio pressure**
   - `src/mqtt/mqtt_task.cpp` blocks or disconnects MQTT when ESP-NOW connection/radio-pressure gates are not satisfied.

3. **Receiver web/API control actions are sent to transmitter via ESP-NOW**
   - Settings update: `lib/webserver_lcd/api/api_settings_handlers.cpp` (`msg_battery_settings_update` via `EspnowTxScheduler::send`).
   - Network update: `lib/webserver_lcd/api/api_network_handlers.cpp` (`msg_network_config_update`).
   - MQTT config update: `lib/webserver_lcd/api/api_network_handlers.cpp` (`msg_mqtt_config_update`).
   - Debug control: `lib/webserver_lcd/api/api_debug_handlers.cpp` (uses `send_debug_level_control`).
   - Type/apply flows: `lib/webserver_lcd/api/api_type_selection_handlers.cpp` (uses `send_component_apply_request`, catalog requests).
   - Reboot/OTA start control: `lib/webserver_lcd/api/api_control_handlers.cpp` (ESP-NOW reboot/OTA control frames).

4. **Receiver already consumes substantial telemetry via MQTT**
   - `src/mqtt/mqtt_client.cpp` subscribes to `batt-emu/mqtt-v1/tx/*` topics (`spec_data`, `spec_data_2`, `battery_specs`, `battery_type_catalog`, `inverter_type_catalog`, `cell_data`, `event_logs`) and updates caches.

### Transmitter (`ESPnowtransmitter2`)

1. **ESP-NOW is core to current receiver control channel**
   - `src/main.cpp` initializes queues/radio, ESP-NOW stack, discovery/FSM bootstrap.
   - `src/config/runtime_task_startup.cpp` starts ESP-NOW-related runtime tasks.
   - `src/espnow/message_routes.cpp` registers many receiver→transmitter handlers (request/abort, settings updates, component config/apply, network config, MQTT config, control, etc.).

2. **Current MQTT inbound command handling is minimal**
   - `src/network/mqtt_manager.cpp`: subscribes to OTA topic and handles OTA command callback.
   - No equivalent inbound MQTT handlers yet for settings/network/component/debug/reboot control flows.

---

## 3) Practicality Assessment

## ✅ What improves with MQTT-only

1. **Removes ESP-NOW coexistence pressure from receiver**
   - Eliminates channel/discovery/FSM/recovery interactions competing with UI/web workload.
   - Removes ESP-NOW worker/scheduler queues/callback paths from the receiver’s hot runtime.

2. **Unifies transport and observability**
   - One message bus for telemetry + commands + acknowledgements.
   - Easier remote inspection/replay via broker tooling.

3. **Aligns with existing direction**
   - You already publish and consume most telemetry through MQTT `batt-emu/mqtt-v1/tx/*`.

## ⚠️ What gets worse / trade-offs

1. **Hard dependency on broker + LAN path**
   - If broker unavailable, receiver↔transmitter command path is unavailable.
   - This is a design choice: reliability shifts from direct peer radio to broker availability.

2. **More protocol design work required**
   - Need robust command topics, schema versioning, idempotency, correlation IDs, and ACK/error conventions.

3. **Security/ACL discipline becomes mandatory**
   - Must lock write topics so only trusted receiver client can issue control commands.

---

## 4) Effort Estimate

**Overall:** Medium–High (not trivial).  
A realistic implementation is a **multi-phase refactor** rather than a single commit.

- **Phase A (low risk):** add MQTT command+ack parity while keeping ESP-NOW as fallback.
- **Phase B:** switch receiver APIs to MQTT command path by default.
- **Phase C:** remove ESP-NOW runtime from receiver.
- **Phase D:** remove ESP-NOW runtime/task/bootstrap from transmitter.

---

## 5) Required design for receiver→transmitter actions over MQTT

To support “values sent back to transmitter via MQTT could be actioned” (your example: static value changes), implement a request/ack protocol:

### Command topics (receiver publishes)
- `batt-emu/mqtt-v1/rx/cmd/settings/update`
- `batt-emu/mqtt-v1/rx/cmd/network/update`
- `batt-emu/mqtt-v1/rx/cmd/mqtt/update`
- `batt-emu/mqtt-v1/rx/cmd/component/apply`
- `batt-emu/mqtt-v1/rx/cmd/control/reboot`
- `batt-emu/mqtt-v1/rx/cmd/control/debug_level`

### Ack/result topics (transmitter publishes)
- `batt-emu/mqtt-v1/tx/ack/settings/update`
- `batt-emu/mqtt-v1/tx/ack/network/update`
- `batt-emu/mqtt-v1/tx/ack/mqtt/update`
- `batt-emu/mqtt-v1/tx/ack/component/apply`
- `batt-emu/mqtt-v1/tx/ack/control/reboot`
- `batt-emu/mqtt-v1/tx/ack/control/debug_level`

### Payload contract (minimum)
```json
{
  "request_id": "uuid-or-monotonic-id",
  "origin": "receiver_lcd",
  "timestamp_ms": 12345678,
  "schema": 1,
  "payload": { }
}
```
Ack should include:
```json
{
  "request_id": "same-as-request",
  "success": true,
  "code": "OK",
  "message": "human readable",
  "applied_version": 123
}
```

**Important:** make command handlers idempotent by `request_id` (duplicate MQTT delivery safety).

---

## 6) Specific code areas that must change

## Receiver

1. Remove ESP-NOW gates from MQTT task logic (`src/mqtt/mqtt_task.cpp`) so MQTT can connect independently.
2. Replace all ESP-NOW sends in web API handlers with MQTT command publish + await ACK pattern:
   - `api_settings_handlers.cpp`
   - `api_network_handlers.cpp`
   - `api_debug_handlers.cpp`
   - `api_type_selection_handlers.cpp`
   - `api_control_handlers.cpp`
3. Decouple UI “link connected” status from ESP-NOW state.
4. After parity, remove ESP-NOW boot/runtime/task creation (`src/main.cpp`, `src/espnow/*`, `src/runtime/runtime_task_startup.cpp` ESP-NOW portions).

## Transmitter

1. Add MQTT command subscriptions in `MqttManager` (beyond OTA).
2. Implement command dispatch to existing managers/handlers (settings/network/component/control).
3. Publish structured ACKs and state updates.
4. Remove ESP-NOW bootstrap/tasks/routes once all commands have MQTT parity (`src/main.cpp`, `src/espnow/*`, runtime task startup).

---

## 7) Risks and mitigations

1. **Broker outage risk**  
   Mitigation: watchdog + clear UI diagnostics + optional offline queueing for non-critical commands.

2. **Command storms / duplicate delivery**  
   Mitigation: rate limits, dedupe cache by `request_id`, bounded retries.

3. **State drift between receiver cache and transmitter truth**  
   Mitigation: transmitter publishes authoritative retained state after successful apply.

4. **Security exposure**  
   Mitigation: broker ACLs per topic, credentials rotation, avoid wildcard write permissions.

---

## 8) Recommendation

**This migration makes sense** for your current problem statement (“too much on receiver”), and is practical.

For the specific webserver reliability objective:
- Treat ESP-NOW removal as a **high-impact enabler**.
- Do **not** treat it as the only lever.
- Keep/introduce lightweight dashboard behavior (bounded polling, efficient SSE usage, reduced payload churn) during and after migration.

Best path:
1. Introduce MQTT command+ack parity first (no ESP-NOW removal yet).  
2. A/B test under your current stress scenario (dashboard + control traffic).  
3. If stable, remove ESP-NOW in receiver, then transmitter.  

This gives maximum risk control while moving toward a cleaner single-conduit architecture.

---

## 9) Suggested next implementation step

Implement one vertical slice end-to-end first:
- `save_setting` (receiver API) → MQTT command publish → transmitter applies via `SettingsManager` → ACK publish → receiver API response.

If that slice proves reliable under load, replicate for network/MQTT config, debug level, and reboot/OTA controls.

---

## 10) Webserver-side implementation details to make this manageable (specific)

This section is the concrete webserver work needed so receiver remains stable after transport simplification.

### A. Response chunking and payload caps

1. **Use HTTP chunked responses for heavy JSON endpoints** (`/api/cell_data`, `/api/event_logs`).
   - Target chunk size: **512–768 bytes** payload per `httpd_resp_send_chunk()` call.
   - Hard upper limit per chunk: **1024 bytes**.
   - Rationale: reduces large contiguous allocations and long blocking send windows.

2. **Cap single JSON response body sizes**
   - Small endpoints (`/api/monitor`, `/api/dashboard_data`): **<= 1024 bytes**.
   - Medium endpoints (`/api/network/*`, `/api/settings/*`): **<= 2048 bytes**.
   - Heavy endpoints should stream/chunk, not build full monolithic strings.

3. **Avoid full-array monoliths for 96-cell payloads**
   - Add paged endpoint: `/api/cell_data_page?offset=<n>&limit=<m>`.
   - Defaults: `offset=0`, `limit=24`.
   - Maximum: `limit=32`.
   - Typical response target: **~700–1200 bytes**.

4. **Event log pagination**
   - Add `/api/event_logs_page?offset=<n>&limit=<m>`.
   - Defaults: `limit=20`.
   - Maximum: `limit=50`.
   - Keep each page under **~2 KB** where possible.

### B. SSE and polling discipline

1. **Monitor SSE cadence**
   - Keep monitor update period at **500 ms minimum** (do not go faster globally).
   - Send only when values change (already mostly done) + ping every timeout cycle.

2. **Cell SSE policy**
   - Keep grace/unsubscribe behavior, but cap outbound SSE event size to **<= 1024 bytes**.
   - If payload exceeds cap, send summary + require paged fetch for detailed cell blocks.

3. **Dashboard fetch consolidation**
   - Poll one consolidated endpoint (`/api/dashboard_data`) at **1000 ms**.
   - Do not run parallel 500 ms polls to multiple endpoints for same card.

4. **SSE client budget**
   - Soft limit: **2 concurrent heavy SSE clients** (`cell_data` stream).
   - Above limit, return `429` with retry hint.

### C. Backpressure + degraded mode

1. **Heap-aware admission control in handlers**
   - If free heap `< 60 KB` or largest 8-bit block `< 16 KB`, reject heavy endpoints with:
     - HTTP `503`
     - `Retry-After: 2`
     - JSON error body (`{"success":false,"retry_ms":2000}`)

2. **Per-endpoint rate limits**
   - `/api/monitor`, `/api/dashboard_data`: **max 5 req/s per client**, burst 10.
   - Heavy endpoints (`/api/cell_data*`, `/api/event_logs*`): **max 1 req/s per client**, burst 3.

3. **Handler execution budget**
   - Target p95 per request:
     - monitor/dashboard: **< 50 ms**
     - heavy paged endpoints: **< 120 ms**
   - Log budget overruns and include counters in diagnostics.

4. **Note: `X-Radio-Pressure` header is deprecated and will not be implemented**
    - `X-Radio-Pressure` was a design artefact from the ESP-NOW coexistence investigations
       (`RECEIVER_HTTP_ESPNOW_COEXISTENCE_FULL_INVESTIGATION_2026_05_05.md`).
    - It depended on `RadioPressureState` / `get_radio_pressure_state()`, which is derived from
       `RxRadioArbiterFsm` and `EspnowTxScheduler` — both of which are removed in the MQTT-only design.
    - In the MQTT-only architecture the radio contention signal no longer exists.
       The heap-admission gate in `ApiMiddleware` (heap < 60 KB → 503) already covers the only
       remaining meaningful pressure signal (memory).
    - **No new implementation is required.** Any document references to `X-Radio-Pressure` as a
       pending action item are superseded by this note.

### D. Front-end/browser-side controls

1. **Cache static assets aggressively**
   - `Cache-Control: public, max-age=86400, immutable` for versioned JS/CSS.
   - Use ETag for HTML/API schema versions.

2. **Debounce setting writes**
   - For slider/rapid controls, debounce to **250–400 ms**.
   - Coalesce multiple field edits into one apply action.

3. **Chunked UI loading**
   - Load summary cards first, then detail panels lazily.
   - Cell table/event log details fetch only on tab open.

### E. Suggested file-level implementation points

- `espnowreceiver_LCD/lib/webserver_lcd/api/api_telemetry_handlers.cpp`
  - Add paged cell/event endpoints and enforce payload caps.
- `espnowreceiver_LCD/lib/webserver_lcd/api/api_sse_handlers.cpp`
  - Add per-event size cap and 429 concurrency guard for heavy streams.
- `espnowreceiver_LCD/lib/webserver_lcd/api/api_middleware.cpp`
   - Add rate limiting + heap-aware admission checks.
   - Note: `X-Radio-Pressure` header propagation is **not** a required implementation item —
      see Section 10.C.4.
- `espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp`
  - Register new endpoints and standardized error/retry headers.
- Dashboard JS assets in `lib/webserver_lcd/pages/*`
  - Consolidate polling to 1-second single endpoint and lazy-load detail calls.

### F. Acceptance criteria (go/no-go)

1. No webserver reset/restart during 30-minute mixed load (dashboard open + settings edits + SSE active).
2. p95 handler latency meets targets in Section C.
3. No single response body >2 KB for regular dashboard paths; heavy data served paged/chunked.
4. Receiver remains responsive with at least one active SSE monitor + one browser dashboard session.

---

## 11) Exact MQTT replacement design (ESP-NOW → MQTT)

This section defines the detailed transport behavior so no component needs request/response polling of the broker for normal operation.

### 11.1 Core principle: event-driven, subscription-based

- **No app-level polling** for config/state changes.
- Both devices keep one MQTT session and use **topic subscriptions**.
- Changes are delivered by broker push to subscribed clients.
- Each device still runs `client.loop()` frequently (socket servicing), but this is not model polling; it is the standard MQTT receive pump.

---

## 12) Topic model (proposed)

Use a stable namespace with separate channels for telemetry, command, ack, and version beacons.

**Second-pass audit note:** the original topic sketch was directionally correct, but it did not explicitly cover all ESP-NOW semantics currently in use. In particular, the MQTT design must also carry:
- retained runtime/version state that currently rides in `msg_version_beacon`
- retained LED state replay used after battery/config refresh
- event-log summary / clear-result semantics (separate from full event-log chunks)
- receiver identity/version presence currently conveyed by `msg_version_announce`
- static `power` / power-profile state currently refreshed via `REQUEST_DATA`

### 12.1 Transmitter → Receiver (state/telemetry)

- `batt-emu/mqtt-v1/tx/state/heartbeat`  
- `batt-emu/mqtt-v1/tx/state/battery_live`  
- `batt-emu/mqtt-v1/tx/state/static/<model>` (retained)  
   - models: `battery`, `power`, `inverter`, `network`, `mqtt`, `led`, `catalog_battery`, `catalog_inverter`
- `batt-emu/mqtt-v1/tx/state/summary/event_logs`  
- `batt-emu/mqtt-v1/tx/state/event_logs/chunk`  
- `batt-emu/mqtt-v1/tx/state/cell_data/chunk`  

### 12.2 Receiver → Transmitter (commands)

- `batt-emu/mqtt-v1/rx/cmd/update/<model>`  
   - models: `battery`, `power`, `inverter`, `can`, `contactor`, `network`, `mqtt`
- `batt-emu/mqtt-v1/rx/cmd/control/<action>`  
   - actions: `debug_level`, `test_data_mode`, `reboot`, `ota_start`, `component_apply`, `event_logs_clear`
- `batt-emu/mqtt-v1/rx/cmd/stream/<stream>`
   - streams: `event_logs`
- `batt-emu/mqtt-v1/rx/cmd/refresh/<model>`
   - models: `battery`, `power`, `network`, `mqtt`, `catalog_battery`, `catalog_inverter`, `led`

### 12.3 Transmitter ACK/Result

- `batt-emu/mqtt-v1/tx/ack/<model_or_action>`

### 12.4 Version/compatibility beacons

- `batt-emu/mqtt-v1/tx/meta/version` (retained)
- `batt-emu/mqtt-v1/tx/meta/schema_versions` (retained)
- `batt-emu/mqtt-v1/tx/meta/runtime` (retained)
   - carries the runtime state currently embedded in ESP-NOW version beacons (`mqtt_connected`, `ethernet_connected`, firmware/build metadata)

### 12.5 Receiver identity / compatibility metadata

- `batt-emu/mqtt-v1/rx/meta/version` (retained or published on connect)
- `batt-emu/mqtt-v1/rx/state/presence` (LWT-backed optional online/offline marker)

This replaces the compatibility/identity role of receiver `msg_version_announce` without recreating an ESP-NOW-style handshake.

---

## 13) Heartbeat without ESP-NOW

### 13.1 Publisher (transmitter)

- Publish interval: **1000 ms** nominal (acceptable 1000–2000 ms).
- Topic: `batt-emu/mqtt-v1/tx/state/heartbeat`.
- QoS: **0** (heartbeat is periodic; dropped frame acceptable).
- Retain: **false**.

### 13.2 Payload

```json
{
   "ts_ms": 123456789,
   "uptime_ms": 987654,
   "seq": 1234,
   "firmware": "9.2.5",
   "proto": 2,
   "status": "ok"
}
```

### 13.3 Receiver liveness rules

- `alive` if heartbeat age < **5000 ms**.
- `degraded` if 5000–15000 ms.
- `offline` if > 15000 ms.

This replaces ESP-NOW link-state/FSM liveness for UI status.

---

## 14) Static data transfer (TX → RX)

### 14.1 Initial sync behavior

- Transmitter publishes each static model to retained topics:
   - `batt-emu/mqtt-v1/tx/state/static/battery`
   - `batt-emu/mqtt-v1/tx/state/static/inverter`
   - `batt-emu/mqtt-v1/tx/state/static/network`
   - `batt-emu/mqtt-v1/tx/state/static/mqtt`
   - plus type catalogs.
- On receiver reconnect, broker immediately replays retained latest state (no explicit fetch needed).

### 14.2 Update behavior

- When transmitter settings change, it republishes only affected model topic.
- Receiver updates corresponding cache and UI immediately.

This removes the ESP-NOW config-section request/retry mechanism for steady-state.

---

## 15) Receiver → Transmitter changed data (no polling)

Your idea is correct and recommended: send a **data-update notification** carrying model identity and correlation info.

Two valid patterns:

### Pattern A (recommended): command carries delta directly

Receiver publishes to `batt-emu/mqtt-v1/rx/cmd/update/<model>` with changed fields.
Transmitter applies directly and emits ACK.

### Pattern B (your suggested fetch pattern): notify + retained model fetch

1. Receiver writes updated model snapshot to `batt-emu/mqtt-v1/rx/state/pending/<model>/<request_id>` (retained=false).  
2. Receiver publishes lightweight notify on `batt-emu/mqtt-v1/rx/cmd/update_notify` with `{model, request_id}`.  
3. Transmitter receives notify and then reads payload from the pending topic (already delivered by subscription).  

Because MQTT is push-based, transmitter does not poll broker; it reacts to subscribed topic arrivals.

### 15.1 Recommended command payload

```json
{
   "request_id": "b14f1f8e-0c4e-4ce6-a9a3-2b4c3f2d6a22",
   "model": "battery",
   "schema": 1,
   "base_version": 1042,
   "patch": {
      "soc_low_limit": 20,
      "soc_high_limit": 95
   },
   "ts_ms": 123456789
}
```

### 15.2 ACK payload

```json
{
   "request_id": "b14f1f8e-0c4e-4ce6-a9a3-2b4c3f2d6a22",
   "model": "battery",
   "success": true,
   "code": "OK",
   "applied_version": 1043,
   "message": "battery settings applied"
}
```

### 15.3 Idempotency and duplicate handling

- Maintain a bounded recent `request_id` cache on transmitter (e.g., last 128 IDs).
- If duplicate `request_id`, return prior ACK without reapplying.

---

## 16) Versioning parity with ESP-NOW

Current ESP-NOW flow already uses config versions and catalog versions. Preserve this behavior in MQTT with explicit per-model version counters.

### 16.1 Per-model version map

Maintain and publish retained map:

```json
{
   "battery": 1043,
   "power": 220,
   "inverter": 335,
   "can": 118,
   "contactor": 91,
   "network": 74,
   "mqtt": 63,
   "catalog_battery": 20012,
   "catalog_inverter": 20013,
   "schema": 1
}
```

Topic: `batt-emu/mqtt-v1/tx/meta/schema_versions` (retained).

### 16.2 Optimistic concurrency

- Receiver includes `base_version` in each update command.
- Transmitter rejects with `VERSION_CONFLICT` if current version differs.
- Receiver then refreshes model from retained topic and retries.

### 16.3 Firmware/protocol compatibility

- Keep `proto` and firmware semantic version in `batt-emu/mqtt-v1/tx/meta/version`.
- Receiver blocks incompatible command schemas and surfaces clear UI errors.

---

## 17) Chunking for large MQTT payloads

Even though current transmitter buffer is configured at 6144 bytes, chunking should be deterministic and bounded.

### 17.1 Chunking thresholds

- Command/ACK messages: target <= **1024 bytes** (no chunking preferred).
- Static model snapshots: target <= **2048 bytes**.
- Large telemetry (`cell_data`, `event_logs`): chunk when payload > **2048 bytes**.

### 17.2 Recommended chunk size

- **1024-byte chunks** payload data field.
- Absolute max chunk body (JSON + metadata): **1400 bytes**.

### 17.3 Chunk envelope

```json
{
   "chunk_id": "evt-20260515-00123",
   "index": 2,
   "total": 5,
   "model": "event_logs",
   "encoding": "json",
   "crc32": 123456789,
   "data": "..."
}
```

### 17.4 Reassembly policy (receiver side)

- Reassembly timeout: **3000 ms** per chunk set.
- Max in-flight chunk sets: **2**.
- Drop and report if missing chunks after timeout.

---

## 18) Impact on transmitter primary role (battery emulator)

### 18.1 Assessment

The battery emulator control path remains dominant and time-sensitive (CAN/state logic), while MQTT control/config traffic is low-rate and non-time-critical.

Given current architecture:
- Emulator/CAN logic continues in primary loop and associated managers.
- MQTT task is low priority and can remain bounded.
- Config updates are infrequent and can be applied asynchronously.

Therefore, **MQTT migration should not materially degrade primary emulation role** if bounded correctly.

### 18.2 Required guardrails

1. MQTT command handler budget: **< 10 ms** CPU per message parse/apply step (excluding NVS write).
2. Defer expensive actions (NVS writes, full catalog rebuilds) to background worker queue.
3. Keep MQTT publish/subscribe processing at low priority and avoid unbounded JSON allocations.
4. Enforce message size caps and reject oversized commands with explicit error ACK.

### 18.3 Safety policy

- Never block CAN/emulator update cycle waiting on MQTT.
- If broker down: continue emulation with last known config; mark remote-control unavailable.
- Apply config changes atomically with rollback on validation failure.

---

## 19) Additional considerations (important)

1. **QoS policy**
    - Heartbeat/live telemetry: QoS0.
    - Commands + ACK + version metadata: QoS1.

2. **Retain policy**
    - Retain static models + version maps.
    - Do not retain heartbeat or transient command topics.

3. **Session behavior**
    - Use clean session = false (or MQTT v5 session expiry) to avoid missing command ACKs during brief reconnects.

4. **Security**
    - ACL: receiver can publish only `batt-emu/mqtt-v1/rx/cmd/#`; transmitter can publish only `batt-emu/mqtt-v1/tx/#`.
    - Separate credentials for each device.

5. **Observability**
    - Add counters: command_rx_total, command_apply_ok/fail, version_conflict_total, chunk_reassembly_fail_total.

---

## 20) Recommended implementation sequence (detailed)

1. Add topic contracts + JSON schemas + ACK semantics.
2. Implement transmitter MQTT command subscriptions for one model (`battery`) + ACK.
3. Wire receiver `save_setting` API to MQTT command path.
4. Add version conflict handling and retry flow.
5. Add chunking path for `event_logs` and `cell_data`.
6. Migrate remaining models/actions.
7. Remove ESP-NOW gates from receiver MQTT task.
8. Remove ESP-NOW runtime from receiver.
9. Remove ESP-NOW runtime from transmitter after soak validation.

---

## 21) Final investigation finding: will removing ESP-NOW enable receiver webserver to meet original requirement?

### 21.1 Direct answer

**Finding:** removing ESP-NOW from both devices (especially receiver) is **very likely** to make the receiver capable of running the webserver as originally intended, **provided** Section 10 web workload controls are applied.

**Confidence:** **high for stability improvement**, **medium-high for full requirement closure**.

It is not mathematically guaranteed by transport removal alone, but transport removal + webserver shaping is the strongest architecture match to your stated goal.

### 21.2 Evidence from codebase complexity audit

Receiver (`espnowreceiver_LCD`) currently carries substantial ESP-NOW runtime/control surface:

- ESP-NOW references in receiver source/web stack scan: **~454 matches**.
- MQTT references in same scan: **~647 matches** (shows MQTT is already a first-class path).
- Receiver web API has direct ESP-NOW send usage: **10 direct `EspnowTxScheduler::send` call sites**.
- Dedicated receiver ESP-NOW implementation files: **23 files** under `src/espnow`, about **3102 non-empty lines**.

Transmitter (`ESPnowtransmitter2`) also has heavy ESP-NOW ownership:

- ESP-NOW references in transmitter source scan: **~613 matches**.
- MQTT references in transmitter source scan: **~606 matches**.

Interpretation:
- Receiver is currently doing both web/UI and substantial ESP-NOW infrastructure work.
- Removing ESP-NOW removes a large execution and state-management surface (radio init/callbacks/FSM/recovery/scheduler/queues/routes).

### 21.3 Why this should help webserver behavior specifically

Removing ESP-NOW from receiver eliminates/decouples:

1. ESP-NOW radio lifecycle and callback paths (`esp_now_init`, recv/send callback handling, peer management).
2. ESP-NOW worker and TX scheduler contention with HTTP/SSE and LVGL tasks.
3. ESP-NOW-driven reconnect/quiet-mode gating that currently influences MQTT and API behavior.
4. ESP-NOW command fan-out from web APIs (settings/network/MQTT/debug/control/apply).

Net effect: lower concurrent complexity and fewer contention points in internal RAM, scheduling, and network stack interactions during web requests.

### 21.4 What could still prevent success if not addressed

Even with ESP-NOW removed, webserver can still degrade if:

- dashboard keeps high request fan-out,
- large JSON bodies are built monolithically,
- SSE/poll cadence is too aggressive,
- heavy endpoints are not paged/chunked.

Therefore Section 10 remains mandatory, not optional.

### 21.5 Practical decision statement

For your objective (“receiver runs webserver correctly”), the best technical decision is:

1. Migrate transport/control plane fully to MQTT (Sections 11–20).
2. Remove ESP-NOW from receiver and transmitter.
3. Enforce webserver shaping limits (Section 10).

Under those conditions, this architecture is expected to satisfy the original receiver webserver requirement.

### 21.6 Verification plan to close with evidence

Declare success only when all pass:

1. **30-minute mixed-load soak** (dashboard open + settings saves + SSE active) with no server recycle/reset.
2. **p95 handler latency** within targets from Section 10.
3. **No large-body regressions** (regular paths <= 2 KB; heavy data paged/chunked).
4. **Control update reliability** (MQTT command→ACK success >= 99.9% on LAN, duplicates handled idempotently).
5. **Transmitter primary-role guard**: no CAN/emulator loop starvation while MQTT updates occur.

If these pass, the migration can be considered to have achieved the original requirement with acceptable margin.

---

## 22) Full ESP-NOW communication inventory and migration mapping

This section explicitly maps both codebases from ESP-NOW communication surfaces to MQTT replacements.

### 22.1 Receiver (`espnowreceiver_LCD`) — communication surfaces

### A) ESP-NOW transport infrastructure (remove completely)

**Files (remove):**
- `src/espnow/espnow_runtime.cpp`
- `src/espnow/espnow_runtime_ingress.cpp`
- `src/espnow/espnow_runtime_messages.cpp`
- `src/espnow/espnow_runtime_routes.cpp`
- `src/espnow/espnow_runtime_detail.h`
- `src/espnow/espnow_settings_sync.*`
- `src/runtime/runtime_task_startup.cpp` (ESP-NOW worker/scheduler portions)
- `src/main.cpp` boot phases: `espnow_radio`, `espnow_state`

**Reason:** purely radio/discovery/callback/FSM ownership; redundant in MQTT-only design.

### B) Receiver outbound control message API (replace with MQTT cmd publisher)

**Current ESP-NOW send layer:**
- `src/espnow/espnow_send.cpp`
- `src/espnow/espnow_send.h`

**Current call sites in web/API:**
- `lib/webserver_lcd/api/api_settings_handlers.cpp`
- `lib/webserver_lcd/api/api_network_handlers.cpp`
- `lib/webserver_lcd/api/api_debug_handlers.cpp`
- `lib/webserver_lcd/api/api_led_handlers.cpp`
- `lib/webserver_lcd/api/api_type_selection_handlers.cpp`
- `lib/webserver_lcd/api/api_control_handlers.cpp`
- `lib/webserver_lcd/api/api_sse_handlers.cpp` (REQUEST_DATA use)

**MQTT replacement:**
- New class: `src/mqtt/mqtt_command_client.{h,cpp}`
   - `publish_update(model, payload, request_id)`
   - `publish_control(action, payload, request_id)`
   - `publish_stream_control(stream, action, request_id)`
   - `publish_refresh(model, request_id)`
   - `await_ack(request_id, timeout_ms)`

### C) Receiver ingress/cache updates currently from ESP-NOW (replace with MQTT subscriptions)

**Current ESP-NOW-backed data/cache helpers:**
- `src/espnow/battery_handlers.*`
- `src/espnow/battery_data_store.*`
- `src/espnow/component_config_handler.*`
- `src/espnow/type_catalog_cache.*`

**MQTT replacement:**
- Extend `src/mqtt/mqtt_client.cpp` topic handlers for:
   - `batt-emu/mqtt-v1/tx/state/static/network`
   - `batt-emu/mqtt-v1/tx/state/static/mqtt`
   - `batt-emu/mqtt-v1/tx/state/static/battery`
   - `batt-emu/mqtt-v1/tx/state/static/power`
   - `batt-emu/mqtt-v1/tx/state/static/inverter`
   - `batt-emu/mqtt-v1/tx/state/static/led`
   - `batt-emu/mqtt-v1/tx/state/summary/event_logs`
   - `batt-emu/mqtt-v1/tx/meta/runtime`
   - `batt-emu/mqtt-v1/tx/meta/schema_versions`
   - `batt-emu/mqtt-v1/tx/ack/#`

### D) Receiver initial-sync / retry behaviors currently driven by ESP-NOW (replace explicitly)

**Current ESP-NOW behaviors:**
- init burst sends `REQUEST_DATA` for `subtype_power_profile`
- version announce sent on connect (`msg_version_announce`)
- config-section retries for missing `network` / `mqtt`
- LED-state retry loop while cache still pending
- type-catalog version request and follow-up catalog requests
- shared receiver connection handler retries `REQUEST_DATA` when power data is stale/missing

**MQTT replacement:**
- retained replay on subscribe for `battery`, `power`, `network`, `mqtt`, `led`, and catalogs
- retained replay for `batt-emu/mqtt-v1/tx/meta/runtime` + `batt-emu/mqtt-v1/tx/meta/schema_versions`
- receiver publishes `batt-emu/mqtt-v1/rx/meta/version` on connect instead of ESP-NOW `msg_version_announce`
- optional `batt-emu/mqtt-v1/rx/cmd/refresh/<model>` for broker/session recovery if a retained topic is missing or suspected stale
- receiver cache-miss logic should request refresh of the specific model, not rebuild a transport-level state machine

### E) Receiver event-log summary / clear semantics (must not be lost)

**Current ESP-NOW behaviors:**
- subscribe / unsubscribe intent for event-log streaming
- push of compact `event_log_summary`
- explicit `event_logs_clear_ack`

**MQTT replacement:**
- `batt-emu/mqtt-v1/rx/cmd/stream/event_logs` with `subscribe` / `unsubscribe`
- `batt-emu/mqtt-v1/tx/state/summary/event_logs` for compact dashboard counters
- `batt-emu/mqtt-v1/rx/cmd/control/event_logs_clear`
- `batt-emu/mqtt-v1/tx/ack/event_logs_clear`

---

### 22.2 Transmitter (`ESPnowtransmitter2`) — communication surfaces

### A) ESP-NOW connection/discovery/reconnect infrastructure (remove completely)

**Files (remove):**
- `src/espnow/discovery_task.*`
- `src/espnow/tx_reconnect_manager.*`
- `src/espnow/tx_connection_handler.*`
- `src/espnow/tx_state_machine.*`
- `src/espnow/transmission_task.*`
- `src/espnow/enhanced_cache.*`
- `src/espnow/data_cache.*`
- `src/queue/espnow_queue_manager.*`

**Reason:** these exist for ESP-NOW channel ownership/discovery/recovery and queued RF transmission.

### B) ESP-NOW message routing and command ingress (replace with MQTT command router)

**Current ESP-NOW message ingress:**
- `src/espnow/message_handler.*`
- `src/espnow/message_routes.cpp`
- `src/espnow/tx_send_guard.*`

**MQTT replacement:**
- New class: `src/network/mqtt_command_router.{h,cpp}`
   - subscribe to `batt-emu/mqtt-v1/rx/cmd/#`
   - parse schema + validate + dispatch
   - publish `batt-emu/mqtt-v1/tx/ack/#`

### C) ESP-NOW command handlers that should be retained as business logic (transport swapped)

These should **not** be deleted; they should be refactored to transport-agnostic services:

- `src/espnow/network_config_handlers.*` -> service: apply network config + version increment + ack body builder
- `src/espnow/mqtt_config_handlers.*` -> service: apply mqtt config + version increment + ack body builder
- `src/settings/settings_espnow.cpp` -> move logic into generic settings apply service
- `src/espnow/component_catalog_handlers.*` -> retain apply/type logic, replace transport envelopes
- `src/espnow/control_handlers.*` -> reboot/debug/ota-start command application

### D) ESP-NOW request-data flow (replace with retained + periodic MQTT state)

**Current:** `src/espnow/request_data_handlers.*` responds to explicit REQUEST_DATA packets.

**Replacement:**
- Publish retained static/state topics on boot/change.
- Publish live telemetry/heartbeat periodically.
- Receiver consumes by subscription; no request packet roundtrip.

### E) ESP-NOW version-beacon and config-section sync (replace explicitly, not implicitly)

**Current transmitter-owned semantics:**
- `msg_version_beacon` carries config versions + runtime link state + firmware/build metadata
- `msg_config_section_request` allows receiver to request `mqtt`, `network`, `battery`, `power_profile`, or `metadata`
- battery section send also replays LED state

**MQTT replacement:**
- retained `batt-emu/mqtt-v1/tx/meta/runtime`
- retained `batt-emu/mqtt-v1/tx/meta/schema_versions`
- retained `batt-emu/mqtt-v1/tx/state/static/battery`
- retained `batt-emu/mqtt-v1/tx/state/static/power`
- retained `batt-emu/mqtt-v1/tx/state/static/network`
- retained `batt-emu/mqtt-v1/tx/state/static/mqtt`
- retained `batt-emu/mqtt-v1/tx/state/static/led`
- optional `batt-emu/mqtt-v1/rx/cmd/refresh/<model>` if retained replay alone is insufficient operationally

### F) ESP-NOW receiver-discovery / compatibility chatter (remove, but preserve the information)

**Current semantics:**
- `msg_probe`, `msg_ack`, `msg_connect_confirm`, `msg_connect_confirm_ack`
- `msg_version_announce`, `msg_version_request`, `msg_version_response`

**MQTT replacement:**
- remove the discovery/confirm path entirely
- preserve compatibility information via `batt-emu/mqtt-v1/rx/meta/version`, `batt-emu/mqtt-v1/tx/meta/version`, and `batt-emu/mqtt-v1/tx/meta/runtime`
- use broker session presence/LWT rather than radio handshake state

---

## 23) Webserver configuration data migration (ESP-NOW source -> MQTT source)

Some web configuration currently depends on ESP-NOW-fed cache updates and must be switched to MQTT-fed cache updates.

### 23.1 Current cache classes (keep, but change upstream source)

- `lib/webserver_lcd/utils/transmitter_network.cpp`
- `lib/webserver_lcd/utils/transmitter_mqtt_specs.cpp`
- `lib/webserver_lcd/utils/transmitter_manager.cpp`

These caches should remain authoritative for page/API rendering, but population should come from MQTT topics instead of ESP-NOW ACK/config packets.

### 23.2 Required new MQTT topics for web config

- `batt-emu/mqtt-v1/tx/state/static/network` (retained)
- `batt-emu/mqtt-v1/tx/state/static/mqtt` (retained)
- `batt-emu/mqtt-v1/tx/state/static/battery` (retained)
- `batt-emu/mqtt-v1/tx/state/static/power` (retained)
- `batt-emu/mqtt-v1/tx/state/static/led` (retained)
- `batt-emu/mqtt-v1/tx/state/static/settings/<model>` (retained as needed)
- `batt-emu/mqtt-v1/tx/state/summary/event_logs`
- `batt-emu/mqtt-v1/tx/meta/runtime`
- `batt-emu/mqtt-v1/tx/meta/schema_versions`
- `batt-emu/mqtt-v1/tx/ack/network`
- `batt-emu/mqtt-v1/tx/ack/mqtt`
- `batt-emu/mqtt-v1/tx/ack/settings`
- `batt-emu/mqtt-v1/tx/ack/event_logs_clear`

### 23.3 API behavior after migration

- Save API (`/api/save_*`) publishes command and waits for ACK by `request_id`.
- Read API (`/api/get_*`) serves local cache populated by subscribed MQTT state topics.
- No broker polling endpoint is required.
- Cache-miss recovery should use model-specific refresh requests, not generic transport reconnect side effects.

---

## 24) MQTT push architecture: how updates are picked up without polling

MQTT is push once subscribed:

1. Receiver subscribes to `batt-emu/mqtt-v1/tx/state/#` and `batt-emu/mqtt-v1/tx/ack/#`.
2. Transmitter publishes state updates and ACKs.
3. Broker pushes messages immediately to receiver session.
4. Receiver callback updates cache / completes pending request futures.

For receiver -> transmitter changes:

1. Receiver publishes `batt-emu/mqtt-v1/rx/cmd/update/<model>` with `request_id`.
2. Transmitter subscription callback fires; it applies update.
3. Transmitter publishes `batt-emu/mqtt-v1/tx/ack/<model>` with same `request_id`.
4. Receiver matches ACK and resolves API call.

No model polling loop is needed.

---

## 25) New MQTT topics and class ownership recommendation

Define topic constants in typed classes (not ad-hoc string literals):

- Receiver:
   - `src/mqtt/mqtt_topics_receiver.h`
   - `src/mqtt/mqtt_command_client.{h,cpp}`
   - `src/mqtt/mqtt_ack_tracker.{h,cpp}`

- Transmitter:
   - `src/network/mqtt_topics_transmitter.h`
   - `src/network/mqtt_command_router.{h,cpp}`
   - `src/network/mqtt_state_publisher.{h,cpp}`

This prevents topic drift and makes ACL/review easier.

---

## 26) Security issues and required controls

### 26.1 Risks

1. Unauthorized command injection (`batt-emu/mqtt-v1/rx/cmd/#`).
2. Replay/duplicate command side effects.
3. Credential leakage (plaintext credentials currently present in some config defaults).
4. Topic wildcard over-permission.
5. Confidential config leakage (MQTT password/config fields).

### 26.2 Controls (required)

1. **Broker ACLs**
    - Receiver identity: publish `batt-emu/mqtt-v1/rx/cmd/#`, subscribe `batt-emu/mqtt-v1/tx/#`.
    - Transmitter identity: publish `batt-emu/mqtt-v1/tx/#`, subscribe `batt-emu/mqtt-v1/rx/cmd/#`.
    - Deny all else.

2. **Auth hardening**
    - Unique per-device credentials.
    - Rotation policy.
    - Remove hardcoded secrets from source defaults in production builds.

3. **Transport security**
    - Prefer TLS-enabled MQTT broker on trusted LAN segments where feasible.
    - If TLS unavailable, isolate VLAN and strict firewall rules.

4. **Message integrity semantics**
    - Mandatory `request_id`, timestamp, schema version.
    - Idempotency cache on transmitter.
    - Optional HMAC signature field for command payloads when broker trust is limited.

5. **Sensitive data minimization**
    - Do not echo plaintext passwords in ACK payloads.
    - Redact credentials in logs and web responses.

---

## 27) Final recommendations from this deep-dive

1. Remove ESP-NOW infra entirely on both devices as planned.
2. Keep business-rule handlers, but refactor them to transport-agnostic service classes.
3. Implement command/ack MQTT classes with `request_id` correlation and idempotency.
4. Move webserver config reads to MQTT-populated caches (`transmitter_network`, `transmitter_mqtt_specs`).
5. Enforce broker ACL + credential hygiene before enabling command topics.
6. Keep Section 10 webserver shaping controls in place to secure receiver stability margin.

---

## 28) Timing implications: ESP-NOW vs MQTT for this project

### 28.1 Direct answer

For the data classes you described (heartbeat, static state sync, infrequent config changes), **moving from ESP-NOW to MQTT does not create a practical timeliness problem**.

This is because these payloads are not hard real-time control loops. They are supervisory/state synchronization traffic where tens to hundreds of milliseconds (and occasionally low seconds during reconnect) are acceptable.

### 28.2 Timing profile comparison (practical)

1. **ESP-NOW (current)**
   - Very low per-frame latency when link is already established.
   - But with current architecture it includes discovery/channel/reconnect behavior that can add larger recovery windows.

2. **MQTT on LAN (target)**
   - Slightly higher per-message latency than direct ESP-NOW in steady state.
   - Predictable push delivery once subscribed.
   - No channel-hopping/discovery cycle on receiver.

In your use-case, the second profile is operationally preferable.

### 28.3 Relevant existing timing constants in codebase

From shared timing config (`esp32common/include/esp32common/config/timing_config.h`):

- ESP-NOW send interval (test/data path): **2000 ms**
- Heartbeat interval: **10000 ms**
- Heartbeat timeout: **30000 ms**
- MQTT loop delay: **1000 ms**
- MQTT reconnect interval baseline: **5000 ms**
- Receiver MQTT task service cadence: **100 ms** (socket/event pump cadence, **not** broker data polling)

Implication:
- Current supervisory cadence is already in seconds for key state/beacon behavior.
- MQTT delivery latency on LAN is well within this envelope for non-real-time state traffic.

### 28.4 Where timing could matter (and how to bound it)

1. **Command/ACK turnaround for web save actions**
   - Target p95 end-to-end (publish -> apply -> ack): **<= 500 ms** on healthy LAN.
   - Accept degraded transient up to **2 s**.

2. **Heartbeat freshness**
   - If publishing every 1–2 s (recommended in Section 13), receiver liveness can be much tighter than current 10 s.
   - Keep offline threshold at 5–15 s bands to avoid flap.

3. **Large payload transfer (cell/event logs)**
   - Chunking avoids large-block stalls; latency increases per object but remains acceptable for non-real-time views.

### 28.5 Conclusion on “does timing have any effect here?”

**Yes, but only in a manageable UX sense, not a control-safety sense.**

- For this migration scope, MQTT timing is acceptable.
- The transmitter’s primary battery-emulator role should not be affected if MQTT work remains low-priority and bounded (Section 18).
- The main risk is not message flight-time; it is queue/backpressure/handler design, which is already addressed by chunking, caps, and ACK correlation.

### 28.6 Recommended timing targets for migration sign-off

Use these objective pass criteria:

1. Command ACK p95 <= **500 ms**, p99 <= **1500 ms**.
2. Heartbeat age p95 <= **2500 ms** (if using 1 s publish), no false-offline flapping in 30 min soak.
3. Static model propagation (TX publish to RX cache visible) p95 <= **1000 ms**.
4. No starvation of emulator/CAN loop while processing MQTT command bursts.

If these are met, timing is confirmed non-blocking for the non-real-time data classes in this design.

### 28.7 Clarification: "MQTT poll" vs MQTT push

Your understanding is correct: MQTT is push after subscription.

The `100 ms` item refers to the **receiver task loop cadence** (how often firmware calls `MqttClient::loop()`), not an application-level "ask broker for data" poll.

What happens in practice:

1. Receiver subscribes to topics.
2. Broker pushes message frames when published.
3. Receiver task calls `loop()` frequently (every ~100 ms) to service socket IO and dispatch callbacks.

So:
- **Data model flow is push**.
- **Task scheduling is periodic** to drain/process pushed bytes.

---

## 29) Transmitter task core-pinning audit

You asked to double-check whether transmitter tasks are hard pinned.

### 29.1 Project task creation findings

Audit of task creation call sites in transmitter source shows project tasks are created with `xTaskCreatePinnedToCore(...)` and explicit core assignment.

Examples:
- `src/config/runtime_task_startup.cpp` -> MQTT task pinned to core **1**.
- `src/espnow/data_sender.cpp` -> data sender pinned to core **1**.
- `src/espnow/message_handler.cpp` -> ESP-NOW RX + network config task pinned to core **1**.
- `src/espnow/transmission_task.cpp` -> background transmission task pinned to caller-selected core (currently passed as **1**).
- `src/espnow/tx_reconnect_manager.cpp` -> reconnect worker pinned to core **1**.

Conclusion for project-owned tasks: **yes, they are hard pinned (primarily to core 1).**

### 29.2 Notable library task behavior

There is a third-party Modbus server task (`eModbus`) that can use `tskNO_AFFINITY` if invoked with `coreID < 0`.

In this codebase, call site uses:
- `MBserver.begin(Serial2, esp32hal->MODBUS_CORE());`

and default HAL defines:
- `MODBUS_CORE() -> 0`

So in current configuration it is also effectively pinned (core 0), not no-affinity.

### 29.3 Recommendation

Keep this invariant explicit in migration docs/code comments:

1. Control/emulator-sensitive work remains on core 0/assigned core as designed.
2. MQTT command/state processing remains pinned to designated low-impact core and low priority.
3. Reject future `xTaskCreate(...)` (un-pinned) additions unless justified and documented.

---

## 30) Second-pass audit: MQTT transmission items previously under-specified

I re-reviewed the document against the current receiver/transmitter code paths to check for any ESP-NOW data/semantics that had not yet been given a clear MQTT replacement. The original document was broadly correct, but the following items needed to be made explicit.

### 30.1 Items that were missing or only partially specified

1. **Runtime/version beacon payload**
   - The transmitter currently uses `msg_version_beacon` to send more than version numbers.
   - It also carries runtime connectivity state (`mqtt_connected`, `ethernet_connected`) and firmware/build metadata.
   - This must become retained MQTT state (`batt-emu/mqtt-v1/tx/meta/runtime` + `batt-emu/mqtt-v1/tx/meta/schema_versions`), otherwise the receiver loses an important lightweight status source.

2. **Receiver identity/version publication**
   - The receiver currently sends `msg_version_announce` during connect.
   - That information still matters for compatibility diagnostics even after discovery/handshake removal.
   - Recommendation: publish `batt-emu/mqtt-v1/rx/meta/version` on connect and optionally maintain `batt-emu/mqtt-v1/rx/state/presence` using broker LWT.

3. **Power-profile / power static refresh path**
   - The receiver init path and shared connection handler both send `REQUEST_DATA(subtype_power_profile)` when power data is missing or stale.
   - The document previously mentioned generic static state, but not this explicit retry behavior.
   - Recommendation: define retained `batt-emu/mqtt-v1/tx/state/static/power` and optional `batt-emu/mqtt-v1/rx/cmd/refresh/power` so cache-miss recovery remains possible without transport coupling.

4. **LED-state replay**
   - The receiver explicitly retries LED-state requests until satisfied.
   - The transmitter also replays LED state when sending the battery config section.
   - Recommendation: publish retained `batt-emu/mqtt-v1/tx/state/static/led`; treat LED state as first-class cached state, not an incidental side-effect of battery updates.

5. **Event-log summary vs full event-log stream**
   - Current ESP-NOW design separates compact `event_log_summary` from the heavier event-log payload path, and also returns a dedicated clear ACK.
   - The earlier document covered event-log chunks but did not explicitly preserve the compact summary and clear-result semantics.
   - Recommendation: add `batt-emu/mqtt-v1/tx/state/summary/event_logs` and `batt-emu/mqtt-v1/tx/ack/event_logs_clear` in addition to chunked log data.

6. **Model-specific refresh of retained state**
   - Under ESP-NOW, missing state is recovered by targeted requests (`config_section_request`, type-catalog version requests, data requests).
   - Under MQTT, retained topics handle the normal case, but the design still benefits from an explicit `batt-emu/mqtt-v1/rx/cmd/refresh/<model>` path for rare broker/session recovery cases.
   - Recommendation: keep refresh optional but designed-in.

### 30.2 Gaps still visible in the codebase today

1. **Power-profile section appears under-defined already**
   - The receiver requests power-profile data today.
   - The transmitter version-beacon manager still logs power-profile section send as "not yet implemented" on the ESP-NOW path.
   - This suggests the migration should not assume a clean existing publisher; the MQTT migration needs a deliberate definition of what `power` means and where its authoritative snapshot comes from.

2. **Current MQTT publisher set does not obviously cover all replacement models**
   - The audited MQTT path clearly publishes the existing `batt-emu/mqtt-v1/tx/*` telemetry/spec/catalog topics.
   - I did **not** find equally obvious current MQTT publishers for retained `network`, `mqtt`, `led`, runtime version state, or compact event-log summary in the same audited path.
   - Recommendation: document these as **new publishers required**, not implied by the current MQTT implementation.

3. **Namespace bridging must be deliberate**
   - Current implementation already uses `batt-emu/mqtt-v1/tx/*` topics.
   - The proposed architecture uses clearer `batt-emu/mqtt-v1/tx/state/*`, `batt-emu/mqtt-v1/tx/meta/*`, and `batt-emu/mqtt-v1/rx/cmd/*` namespaces.
   - Recommendation: either migrate fully to the new namespace or add a temporary bridge/alias layer during rollout so the receiver does not depend on two inconsistent topic models.

### 30.3 Suggested closure list so no data handling is missed

Before any ESP-NOW removal is accepted, verify that MQTT covers **all** of the following payload classes:

1. heartbeat / liveness
2. live battery telemetry
3. battery static settings
4. power-profile static settings
5. network config
6. MQTT config
7. battery type catalog
8. inverter type catalog
9. inverter interface selections / component apply result
10. LED state
11. event-log summary
12. event-log full chunks
13. event-log clear ACK/result
14. cell-data chunks
15. runtime connectivity/meta state (`mqtt_connected`, `ethernet_connected`)
16. firmware / schema / compatibility versions for both devices
17. debug level control
18. test-data mode control
19. reboot / OTA start control and ACKs

If any one of these remains "implicitly handled" rather than having a named topic + owner + cache/ACK behavior, the migration is not yet fully specified.

### 30.4 Final second-pass conclusion

**Conclusion:** the MQTT-only plan remains sound, but the document did need additional explicit coverage.

After this re-review, the main missing items are now accounted for conceptually. The only meaningful unresolved areas are:
- exact authoritative source and schema for `power` / power-profile retained state
- whether to implement `batt-emu/mqtt-v1/rx/cmd/refresh/<model>` immediately or only if retained replay proves insufficient
- whether to bridge old `batt-emu/mqtt-v1/*` topics or perform a single namespace cutover

These are implementation-planning questions, not blockers to the overall architecture decision.

---

## 31) Clarifications requested: MQTT-down behavior, demand fetch, config saves, and FSM sharing

This section answers the latest design questions directly and makes the expected behavior explicit.

### 31.1 What telemetry is kept if MQTT link is down?

### Direct answer

When broker/link is down, telemetry should be treated in **three classes**:

1. **Latest-state snapshots (must be kept):**
   - `battery_live` latest value set
   - latest `cell_data` snapshot
   - latest `event_log_summary` snapshot
   - latest static state (`battery`, `power`, `network`, `mqtt`, `led`, catalogs)

2. **Short-history/transient telemetry (bounded keep):**
   - event-log deltas/new events
   - optional recent heartbeat samples for diagnostics

3. **High-rate ephemeral telemetry (may drop):**
   - repeated live points superseded by newer values

### Recommended retention policy on transmitter

1. Keep **authoritative latest snapshot per model** in memory (already needed for runtime anyway).
2. Keep a **bounded ring buffer** only for transient/delta data that matters after reconnect:
   - event-log deltas: keep last **N** entries (e.g. 200)
   - optional heartbeat diagnostics: keep last **M** samples (e.g. 30)
3. For high-rate repeat telemetry, **replace not queue** (last-value-wins).
4. On reconnect, publish:
   - retained static/meta topics first,
   - then one current live snapshot,
   - then replay bounded backlog where applicable.

### 31.2 Does non-real-time nature change this design?

### Direct answer

Yes. Because this channel is supervisory (not hard real-time control), brief lag or selective loss is acceptable for most telemetry.

Implications:

1. You do **not** need lossless queueing of every live point.
2. You **do** need deterministic recovery of:
   - current state,
   - configuration truth,
   - operator-relevant event history.
3. Therefore: snapshot + bounded backlog is a better fit than unbounded guaranteed delivery.

This is consistent with earlier timing findings (Section 28).

### 31.3 How receiver requests data on demand in MQTT model

Use model-scoped refresh commands; keep this narrow and explicit.

### Request path

1. Receiver cache miss / suspected stale model detected.
2. Receiver publishes `batt-emu/mqtt-v1/rx/cmd/refresh/<model>` with `request_id`.
3. Transmitter validates request + republishes authoritative state for that model.
4. Transmitter publishes `batt-emu/mqtt-v1/tx/ack/refresh/<model>` with same `request_id`.
5. Receiver updates cache and resolves waiting API/UI path.

### Models that should support demand refresh

- `battery`, `power`, `network`, `mqtt`, `led`, `catalog_battery`, `catalog_inverter`, optionally `event_logs_summary`.

### Why this is still push architecture

This is **application-level request/response over push topics**, not broker polling. No periodic “ask broker” loop is introduced.

### 31.4 Saving infrequently changed data (configuration) in MQTT model

For low-frequency settings/config updates, use **command + validate + apply + retained state republish**.

### Save flow

1. Receiver publishes `batt-emu/mqtt-v1/rx/cmd/update/<model>` with:
   - `request_id`,
   - `base_version`,
   - patch/body,
   - schema/version metadata.
2. Transmitter validates payload and concurrency (`base_version`).
3. If valid:
   - apply atomically,
   - persist to NVS,
   - increment model version,
   - publish retained `batt-emu/mqtt-v1/tx/state/static/<model>`,
   - update retained `batt-emu/mqtt-v1/tx/meta/schema_versions`,
   - publish `batt-emu/mqtt-v1/tx/ack/<model>` success.
4. If invalid/conflict:
   - no partial apply,
   - publish negative ACK (`VALIDATION_FAILED`/`VERSION_CONFLICT`) with current version.

### Important reliability point

For infrequent config writes, correctness matters more than latency. Use QoS1 + idempotency cache by `request_id`.

### 31.5 FSM architecture: shared framework vs per-device FSMs

### Direct answer

Use a **shared FSM framework/common library**, but run **separate FSM instances per device role**.

### Recommended structure

1. **Shared code in `esp32common`**
   - common FSM primitives (`state`, `event`, transition table, timers, backoff policy, metrics hooks)
   - common MQTT transport-state enums (e.g. `DISABLED`, `NET_DOWN`, `BROKER_CONNECTING`, `BROKER_UP`, `DEGRADED`)
   - shared helper for retry/backoff and health counters

2. **Device-specific FSM implementations**
   - transmitter FSM: publish gating, backlog replay sequencing, command-apply admission
   - receiver FSM: cache freshness, request/refresh orchestration, web/API degraded-mode behavior

3. **Why not one global FSM shared across both?**
   - roles differ materially:
     - transmitter is source-of-truth publisher + command executor,
     - receiver is cache/UI consumer + command originator.
   - transition triggers and side effects are not symmetric.

### Practical decision

- **Common framework:** yes, shared.
- **Common state machine instance:** no.
- **Per-device FSM instance with shared contracts:** yes.

### 31.6 MQTT send gating decision (finalized)

Yes, MQTT transmission should be FSM-gated.

Minimum policy:

1. Send live telemetry only in `BROKER_UP`.
2. While disconnected/backoff:
   - keep/update latest snapshots locally,
   - enqueue only bounded transient backlog,
   - do not block emulator/CAN loops.
3. On reconnect:
   - publish retained static/meta first,
   - publish current live snapshots,
   - replay bounded transient backlog,
   - resume normal periodic cadence.

This preserves receiver/web correctness while preventing unbounded memory growth or control-path stalls.

---

## 32) Full implementation program (execution plan + mandatory rules)

This section converts the design into an implementation program with explicit phase gates and cleanup requirements.

## 32.1 Non-negotiable implementation rules (must be enforced)

1. **Section 10 HTTP/webserver work is mandatory** and must be completed as part of this migration program (not deferred indefinitely).
2. **After every phase, perform a formal code review against this document**.
   - If implementation diverges from this document, code must be corrected/re-written until aligned.
3. **At the end of each phase, remove old/legacy/redundant code introduced by superseded paths**.
   - Target outcome after every phase: cleaner codebase than before phase started.
4. **Receiver changes must cover both receiver variants (TFT + LCD)**.
   - Any transport/web/API changes must be validated on both receiver build targets before phase sign-off.

These rules are part of the implementation definition, not optional process notes.

## 32.2 Scope mapping for receiver variants (TFT + LCD)

Receiver work must be applied consistently to both receiver product variants.

Minimum required treatment:

1. Shared receiver transport/cache/API logic follows one common design contract.
2. Variant-specific UI/rendering layers (TFT and LCD) consume the same normalized cache/update interfaces.
3. Build + runtime validation must be executed for both receiver targets each phase.
4. No variant may retain obsolete ESP-NOW behavior once a phase declares MQTT parity for that behavior.

## 33) Namespace recommendation + revised "minimal MQTT viability first" path

This section addresses the latest request to choose a concrete namespace and to de-risk implementation by proving minimal MQTT operation first.

### 33.1 Finalized namespace: `batt-emu/mqtt-v1/{tx,rx}`

**Approved namespace** (concrete, non-negotiable):

- **Transmitter → Receiver:** `batt-emu/mqtt-v1/tx/state/*`, `batt-emu/mqtt-v1/tx/meta/*`, `batt-emu/mqtt-v1/tx/ack/*`
- **Receiver → Transmitter:** `batt-emu/mqtt-v1/rx/cmd/*`, `batt-emu/mqtt-v1/rx/meta/*`

Rationale: role-scoped (`tx`/`rx`), versioned (`v1`), explicit (state/meta/cmd/ack separation).

### 33.2 Single implementation plan: "Minimal MQTT viability first" (de-risk-first approach)

Execution follows a checkpoint-gated de-risk strategy: prove minimal MQTT operation before full build-out. If checkpoint fails, pause and re-evaluate; if it passes, progressively re-enable remaining features.

### 33.3 Implementation stages (M-prefix nomenclature — definitive execution plan)

Adopt this as an explicit checkpoint program.

#### Stage M0 — Preparation

1. Implement namespace constants and FSM gates.
2. Keep full webserver feature surface present, but add feature flags so non-critical telemetry publishers can be disabled.
3. Ensure both receiver variants (TFT + LCD) compile with the same transport contract.

#### Stage M1 — Remove ESP-NOW transport path and enable minimal MQTT only

1. Disable/remove ESP-NOW runtime path for receiver + transmitter transport.
2. Enable only minimum MQTT feeds required to prove system viability:
   - heartbeat/liveness,
   - minimal dashboard status,
   - page-access-critical config snapshots (retained),
   - enough data for web UI to confirm end-to-end functionality.
3. Keep all other web pages/endpoints present, but disable non-essential MQTT data transmission for now.

#### Stage M2 — HTTP/webserver stabilization under minimal MQTT mode

1. Complete Section 10 controls (chunking/paging/rate limiting/backpressure/SSE discipline).
2. Run soak tests with dashboard + minimal telemetry active.

#### Stage M3 — Stop/Go architecture checkpoint

**Go criteria** (all required):

1. web pages consistently accessible,
2. heartbeat + minimal telemetry reliable,
3. no resets/recycles under mixed load,
4. acceptable latency/responsiveness metrics,
5. both receiver variants pass.

If any fail materially, **pause further migration and re-evaluate architecture** before adding more MQTT payload classes.

#### Stage M4 — Progressive re-enable of remaining MQTT-transmitted web data

1. Re-enable one model/action group at a time (feature-flag controlled).
2. After each addition:
   - remove superseded temporary code,
   - run the Section 10 acceptance checks,
   - verify both receiver variants (TFT + LCD),
   - run formal code review against this document before advancing.
3. Recommended re-enable order:
   - static config state (`network`, `mqtt`, `battery`, `power`, `led`),
   - catalog/state summaries,
   - event log summary/clear semantics,
   - large payload streams (`event_logs`, `cell_data`) with chunking guards,
   - remaining control actions (`debug_level`, `test_data_mode`, `reboot`, `ota_start`).
4. If any regression appears, disable only the most recently enabled group, fix, and re-validate.

**Current Stage M4 status (2026-05-19):**
- Step 1 enabled: chunked large-payload MQTT streams for `cell_data` and `event_logs`
   via `MQTT_FEATURE_CELL_DATA_TELEMETRY=1` and `MQTT_FEATURE_EVENT_LOG_STREAMING=1`.
- `MQTT_FEATURE_DEMAND_REFRESH=1` is also enabled to match the M2/M3 readiness requirement.
- `MQTT_FEATURE_LIVE_BATTERY_TELEMETRY` intentionally remains disabled because the LCD receiver
   already consumes `batt-emu/mqtt-v1/tx/state/battery_live`, but the TFT receiver does not yet
   have equivalent `battery_live` topic handling. That parity work should be completed before the
   live-battery stream is re-enabled as a later M4 step.

#### Stage M5 — Final cleanup and sign-off

1. Remove all superseded ESP-NOW transport/runtime files listed in Section 22.
2. Confirm command/ack idempotency, schema/version checks, and retained-state replay behavior.
3. Confirm security controls in Section 26 (ACLs, credentials, redaction) are active.
4. Complete final 30-minute mixed-load soak and timing/latency checks.
5. Publish a final implementation conformance matrix against Sections 10–33.

### 33.4 Explicit exit criteria for migration completion

Migration is complete only when all are true:

1. Section 10 webserver controls are implemented and validated.
2. Both receiver variants pass build + runtime acceptance checks.
3. All payload classes in Section 30.3 have named topic ownership and verified handler paths.
4. ESP-NOW transport runtime paths are removed from both receiver and transmitter.
5. Security controls in Section 26 are operational in deployment.

---

## 34) Immediate next actions from the current checkpoint

1. Run build validation for transmitter + both receiver variants with M4 step-1 flags enabled.
2. Run Stage M3/M4 checkpoint evidence collection for the newly enabled `cell_data` / `event_logs` MQTT streams.
3. Complete TFT `battery_live` consumer parity before enabling `MQTT_FEATURE_LIVE_BATTERY_TELEMETRY`.
4. Keep a living conformance matrix so each step is explicitly traced back to this document.
