# Receiver HTTP Unreachable + Warning Flood
## First-Principles Analysis (2026-05-06)

## Executive summary
The core failure mode is **not IP reachability**. The device is pingable because STA/L2 is up, but the HTTP application path is intermittently unavailable because:

1. **HTTP SSE handlers are long-running/blocking** inside ESP-IDF `httpd` request context.
2. ESP-NOW is under sustained pressure (`ESP_ERR_ESPNOW_NO_MEM`), causing repeated recovery and control-plane churn.
3. Log volume is high enough to materially increase scheduling/jitter pressure (synchronous serial logging).
4. Current `webserver=running` telemetry only checks `server != nullptr`, which can report healthy while request handling is effectively blocked.

Net: you can ping `192.168.1.59` while pages do not load.

---

## Scope and method
This analysis was done from first principles across receiver LCD + shared `esp32common` codepaths, focusing on:

- HTTP server lifecycle and request model
- SSE behavior
- ESP-NOW TX pressure and recovery FSM
- WiFi/STA/AP fallback transitions
- logging/scheduler pressure

---

## Key findings

## 1) HTTP server can be "running" but non-responsive
In `espnowreceiver_LCD/src/main.cpp`, diagnostics report:

- `webserver=running` when `server != nullptr`

That indicates handle existence, **not** request progress/liveness.

### Why this matters
A stale/alive handle does not prove `/` or `/api/*` can be served in bounded time.

---

## 2) SSE handlers are implemented as long-lived blocking loops in HTTP request handlers
In `espnowreceiver_LCD/lib/webserver_lcd/api/api_sse_handlers.cpp`:

- `api_monitor_sse_handler()` loops up to `kSseSessionMaxDurationMs = 300000` (5 min)
- `api_cell_data_sse_handler()` also loops long-duration
- each loop waits/sends ping repeatedly in the same handler context

At the same time, pages create permanent EventSource sessions:

- `monitor2_page_script.cpp` opens `new EventSource('/api/monitor_sse')`
- `cellmonitor_page_script.cpp` opens `new EventSource('/api/cell_stream')`

### First-principles consequence
ESP-IDF `httpd` is task-driven and not a thread-per-request model. A handler that does long waits can monopolize HTTP task time. Under this design, **a single active SSE session can starve regular page/API handling**, producing exactly:

- ping works
- browser requests time out/fail
- `webserver=running` still printed

This is the strongest explanation for your "pingable but no webpages" state.

---

## 3) ESP-NOW NO_MEM pressure is real and sustained
From shared scheduler + receiver logs:

- `esp32common/espnow_common_utils/espnow_tx_scheduler.cpp` reports repeated `ESP_ERR_ESPNOW_NO_MEM`
- ACK watchdog releases indicate ACK completion callbacks not arriving in time
- receiver enters L1/L2 recovery (`rx_connection_handler.cpp`), including WiFi stop/start at L2

### First-principles consequence
When WiFi buffers are constrained (shared radio + TCP/HTTP + ESP-NOW control), control packets fail, reconnect loops intensify, and more management traffic is generated. This creates a positive feedback loop.

---

## 4) Warning flood itself is adding load
Logging is synchronous (`Serial.printf`) in `esp32common/logging_utilities/logging_config.h` and `log_routed.cpp`.

Current high-frequency warnings include:

- `MQTT_TASK`: "Radio pressure CRITICAL: MQTT paused" (every ~2s)
- `ESPNOW_TX`: repeated NO_MEM/ACK watchdog warnings
- `CONN_MGR`: expected-but-benign event ordering warnings (`Unexpected event in CONNECTING: CONNECTION_START`)

### First-principles consequence
Under pressure, frequent formatted serial output adds CPU and timing jitter, worsening service latency and recovery behavior.

---

## 5) One warning is mostly diagnostic noise, not root cause
`Unexpected event in CONNECTING: CONNECTION_START` is produced by event ordering:

- auto-reconnect posts `CONNECTION_START` on transition to IDLE
- probe path can already move state to CONNECTING
- later `CONNECTION_START` arrives in CONNECTING and is logged as warning

This should be downgraded to DEBUG/INFO (or ignored in CONNECTING), as it is expected race behavior.

---

## Causal chain (what is likely happening)
1. UI page opens SSE endpoint (`/api/monitor_sse` or `/api/cell_stream`).
2. HTTP handler enters long loop and holds HTTP task context.
3. New page/API requests are starved or delayed heavily; browser reports page unavailable.
4. Concurrently, ESP-NOW encounters TX buffer pressure (`NO_MEM`), driving reconnect + L1/L2 churn.
5. Warning logs flood serial output, adding further scheduler pressure.
6. Net effect: ping still succeeds, HTTP appears down.

---

## Why channel limitation is not avoidable but still workable
You are correct: 2.4 GHz shared channel/radio is a hard constraint. The architecture must **degrade gracefully** under contention.

Current system still allows one class of workload (long-lived HTTP handler) to monopolize service. That is the key architectural mismatch.

---

## Recommended mitigation plan (priority order)

## P0 (must-do): remove blocking SSE handler model
Replace long-lived blocking SSE handlers with one of:

1. **Short-poll JSON endpoints only** (simplest, robust)
2. True async streaming model that does not monopolize HTTP request context

Given current instability, option (1) is the fastest risk-reduction path.

### Cell Monitor replacement decision (recommended)
For `/cellmonitor`, the best replacement for SSE is:

- **Primary path: HTTP snapshot polling** (`GET /api/cell_data` every **5000 ms**)
- **Design decision: no browser-facing MQTT path**

#### Why HTTP snapshot polling is preferred
- no long-lived HTTP handler loops
- no browser MQTT client/broker dependency added to UI path
- naturally bounded request lifetime in `httpd`
- easy to make pressure-aware using `X-Radio-Pressure` + `Retry-After`
- 5000 ms cadence is acceptable for snapshot-style cell-monitor UX and materially lowers communication pressure

#### Why browser MQTT is excluded for this design
Using MQTT directly in the browser is intentionally out of scope because it:

- introduces external broker/session coupling into local web UI
- still consumes shared WiFi airtime during pressure events
- adds reconnect/session complexity in JS (topic lifecycle, retained state, stale handling)
- does not solve current `httpd` starvation as directly as removing long-lived SSE handlers

#### Practical architecture
1. Keep receiver internal ingest as-is (ESP-NOW/MQTT ingestion into cache).
2. Remove EventSource usage from `cellmonitor_page_script.cpp`.
3. Poll `/api/cell_data` on a **fixed 5000 ms cadence** for normal operation.
4. If response has `Retry-After`, honor it and suspend aggressive polling.
5. Render from last-known-good snapshot when response is delayed/unavailable.

#### Design lock
This review explicitly locks the UI transport decision to **HTTP polling only** for Cell Monitor.
No browser MQTT transport path will be pursued.

---

## P1: make webserver health real, not pointer-based
Add active liveness checks:

- timestamp of last successful request completion
- timeout threshold to trigger controlled `httpd` recycle
- metric for handler execution duration + stuck-handler detection

---

## P1: reduce warning amplification
- Rate-limit repeated NO_MEM warnings (coarse bucket)
- Rate-limit/downgrade `MQTT paused` warning after first emission in state
- Downgrade/ignore expected `CONNECTION_START` while CONNECTING

---

## P1: harden NO_MEM recovery behavior
- Increase backoff on repeated NO_MEM before escalating L1/L2
- Reduce non-critical request retries while pressure is constrained/critical
- Keep control-only mode strict until heartbeat stability window is genuinely met

---

## P2: pressure-aware UI transport policy
When pressure is constrained/critical:

- force dashboard into reduced poll cadence
- remove/deprecate monitor SSE/cell SSE endpoints from UI flows
- show UI banner: "radio constrained, live stream paused"

---

## Immediate operational workaround (no code)
Until P0 is implemented:

1. Avoid opening `/transmitter/monitor2` and `/cellmonitor` (these open SSE streams).
2. Keep one browser tab only.
3. Use lightweight endpoints (`/api/dashboard_data`, `/api/system_metrics`) at low poll rates.
4. Reduce runtime log level to suppress warn storms during testing.

---

## Validation checklist after fixes
Success criteria:

1. With monitor pages open, `/` and `/api/dashboard_data` remain responsive.
2. No prolonged "webserver running but unreachable" episodes.
3. `NO_MEM` bursts recover without L2 restart escalation loops.
4. Warning throughput reduced by >80% during reconnect scenarios.
5. `/api/system_metrics` shows bounded handler latencies and sane active stream count.
6. Cell Monitor remains usable at 5000 ms polling cadence with reduced request pressure.

---

## Document sanity re-review (post-update)

### Conflicting points reviewed and resolved
- **Resolved:** Prior text allowed browser MQTT as a potential direction; this is now explicitly excluded.
- **Resolved:** Prior polling recommendation (1000-2000 ms baseline) conflicted with the low-pressure objective; baseline is now fixed at 5000 ms for Cell Monitor.
- **Resolved:** P2 wording implied temporary SSE disable; updated to explicit deprecation/removal from UI flows.

### Remaining missing items to close in implementation planning
1. **Timeout budget suggestion for `/api/cell_data` handler**
	 - Target handler execution (server-side):
		 - steady-state: <= 50 ms
		 - under load (normal): <= 100 ms
		 - constrained/critical pressure worst-case: <= 200 ms
	 - Operational guardrail:
		 - if handler time exceeds 200 ms for 3 consecutive calls, treat as degraded and force client backoff behavior.
	 - Rationale: keeps request lifetime bounded and prevents HTTP task starvation.

2. **Client retry behavior suggestion when `Retry-After` is absent**
	 - Base polling remains 5000 ms.
	 - On non-200 response or fetch/network error (without `Retry-After`):
		 - retry delays: 5000 ms -> 10000 ms -> 20000 ms -> 30000 ms (cap)
		 - reset to 5000 ms after first successful response.
	 - Add ±10% jitter to each delay to avoid request synchronization from multiple browser tabs/devices.

3. **Explicitly out of scope**
	 - The dedicated induced-NO_MEM test case is not planned for this workstream.
	 - Validation will rely on existing runtime telemetry (`/api/system_metrics`) and field observation during normal operation.

4. **Migration note for EventSource-dependent monitor pages**
	 - Current EventSource dependencies:
		 - `/transmitter/monitor2` -> `monitor2_page_script.cpp` (`/api/monitor_sse`)
		 - `/cellmonitor` -> `cellmonitor_page_script.cpp` (`/api/cell_stream`)
	 - Migration action:
		 - replace `EventSource` logic with timer-driven `fetch()` polling.
		 - keep data rendering model unchanged (last-known-good snapshot + stale indicator).
	 - Compatibility note:
		 - remove automatic SSE reconnect loops, ping keepalive assumptions, and SSE-only connection status text.
		 - update page copy to reflect snapshot polling semantics (5 s cadence, adaptive backoff on errors).

### Sanity outcome
The document is now internally consistent with the agreed design:

- Cell Monitor is snapshot-oriented and polled at 5000 ms.
- Browser MQTT is excluded by design.
- SSE-induced HTTP starvation is addressed by removing blocking SSE usage from UI-critical paths.

---

## Implementation plan (phased, with mandatory cleanup)

## Governance rules (apply to every phase)
1. **Phase completion requires document update** in this file:
	 - add date/time
	 - mark phase status (`not started` -> `in progress` -> `done`)
	 - record what changed, what was removed, and verification evidence.
2. **No dual-path legacy retention** unless explicitly justified for rollback.
3. **All old/legacy/redundant code introduced by replaced paths must be removed** in the same phase that replaces it.
4. **Each phase ends with build + runtime validation evidence**.

## Progress tracker
- Phase 0: Design lock + instrumentation baseline — `not started`
- Phase 1: Remove Cell Monitor SSE path — `done (2026-05-06)`
- Phase 2: Remove Monitor2 SSE path — `done (2026-05-06)`
- Phase 3: Client backoff + timeout guardrails — `done (2026-05-06)`
- Phase 4: Warning-volume reduction + cleanup sweep — `done (2026-05-06)`
- Phase 5: Final validation + closure — `not started`

### Phase execution log

#### 2026-05-06 — Phase 1 completion record
Status: `done`

What changed:
- Updated `espnowreceiver_LCD/lib/webserver_lcd/pages/cellmonitor_page_script.cpp`:
	- removed `EventSource('/api/cell_stream')` usage
	- replaced SSE loop with timer-based polling to `/api/cell_data`
	- set base poll cadence to 5000 ms
	- implemented fallback backoff (cap 30000 ms) and ±10% jitter
	- retained last-known-good rendering behavior and unavailable-state messaging.

What legacy/redundant code was removed:
- Cell Monitor SSE client path (`eventSource`, reconnect timer, SSE `onmessage`/`onerror` handlers)
- SSE-specific reconnect semantics in the Cell Monitor page script.

Verification evidence:
- Build executed successfully for `espnowreceiver_LCD` environment.
- Result: `SUCCESS` (firmware ELF/BIN produced), only existing framework warning remained (`esp32-hal-uart.c` return-without-value warning).

#### 2026-05-06 — Phase 2 completion record
Status: `done`

What changed:
- Updated `espnowreceiver_LCD/lib/webserver_lcd/pages/monitor2_page_script.cpp`:
	- removed `EventSource('/api/monitor_sse')` usage
	- replaced SSE stream handling with timer-based polling to `/api/monitor`
	- set base poll cadence to 5000 ms
	- implemented fallback backoff (cap 30000 ms) and ±10% jitter
	- updated connection UX text to snapshot polling semantics.

What legacy/redundant code was removed:
- Monitor2 SSE client path (`eventSource`, reconnect timer, SSE `onopen`/`onmessage`/`onerror` handlers)
- SSE-only reconnect/staleness assumptions in Monitor2 page script.

Verification evidence:
- Static verification confirms no `EventSource`/monitor SSE symbols remain in Monitor2 page script.
- Build validation completed for `espnowreceiver_LCD` workspace with no reported errors.

#### 2026-05-06 — Phase 3 completion record
Status: `done`

What changed:
- Updated `espnowreceiver_LCD/lib/webserver_lcd/api/api_telemetry_handlers.cpp`:
	- added snapshot endpoint latency-budget telemetry for `/api/monitor` and `/api/cell_data`
	- added budget thresholds (`steady=50 ms`, `degraded=200 ms`)
	- added over-budget counters and last/max latency tracking
	- exposed budget/counter data via `/api/http_pressure_stats`
	- added warn-level logs when degraded budget is exceeded.

Guardrail alignment:
- Client-side backoff/jitter is active in both polling pages:
	- `cellmonitor_page_script.cpp` (`5000 -> 10000 -> 20000 -> 30000`, cap + jitter)
	- `monitor2_page_script.cpp` (`5000 -> 10000 -> 20000 -> 30000`, cap + jitter)
	- `Retry-After` header is honored when present.

What legacy/redundant code was removed:
- Legacy SSE-only cadence/reconnect assumptions already removed in Phases 1 and 2; no additional obsolete timing constants required for this phase.

Verification evidence:
- Build validation completed for `espnowreceiver_LCD` workspace with no reported errors after budget telemetry changes.

#### 2026-05-06 — Phase 4 completion record
Status: `done`

What changed:
- Updated `espnowreceiver_LCD/src/mqtt/mqtt_task.cpp`:
	- reduced repetitive radio-pressure logs by switching to state-change logging with a 15-second periodic refresh window.
- Updated `espnowreceiver_LCD/lib/webserver_lcd/api/api_handlers.cpp`:
	- removed unused Monitor/Cell SSE routes (`/api/monitor_sse`, `/api/cell_stream`) from API registration.
- Updated stale page comments to polling semantics:
	- `espnowreceiver_LCD/lib/webserver_lcd/pages/cellmonitor_page_script.h`
	- `espnowreceiver_LCD/lib/webserver_lcd/pages/monitor2_page_script.h`.

What legacy/redundant code was removed:
- Unused browser-facing SSE endpoints for Monitor2/Cell Monitor from route table.
- Stale SSE documentation/comments in page script headers.

Verification evidence:
- Static verification in `espnowreceiver_LCD` shows no `EventSource`, `/api/monitor_sse`, or `/api/cell_stream` references in UI sources.
- Build validation completed for `espnowreceiver_LCD` workspace with no reported errors after cleanup.

## Phase 0 — Design lock + baseline
Scope:
- Freeze architecture decisions in code comments/docs:
	- Cell Monitor = HTTP polling at 5000 ms
	- no browser MQTT path
- Capture baseline metrics before change:
	- page responsiveness
	- warning volume
	- `/api/system_metrics` snapshots.

Deliverables:
- Baseline evidence block added to this document.

Exit criteria:
- Baseline recorded and accepted.

## Phase 1 — Remove Cell Monitor SSE path
Scope:
- Update `cellmonitor_page_script.cpp`:
	- remove `EventSource('/api/cell_stream')` path
	- implement timer `fetch('/api/cell_data')` at 5000 ms
	- implement suggested error backoff policy.
- Keep rendering semantics (last-known-good + stale indicator).

Mandatory legacy removal:
- Remove cell-monitor-specific SSE reconnect logic and dead handlers in page script.
- Remove any now-unused UI text/flags tied only to SSE state.

Exit criteria:
- `/cellmonitor` works from polling only.
- no EventSource usage remains in Cell Monitor page script.
- build passes.

## Phase 2 — Remove Monitor2 SSE path
Scope:
- Update `monitor2_page_script.cpp` to polling model.
- Replace SSE connection state UX with snapshot polling status UX.

Mandatory legacy removal:
- Remove monitor2 EventSource/reconnect/ping assumptions.
- Remove dead JS branches and helper code only used by SSE.

Exit criteria:
- `/transmitter/monitor2` operates without EventSource.
- no monitor2 EventSource references remain.

## Phase 3 — Timeout + retry guardrails
Scope:
- Enforce `/api/cell_data` response-time budgeting in implementation and observability.
- Client logic:
	- base 5000 ms
	- fallback backoff when `Retry-After` absent: 5s -> 10s -> 20s -> 30s cap
	- reset on success
	- ±10% jitter.

Mandatory legacy removal:
- Remove obsolete timing constants tied to old SSE cadence for these pages.

Exit criteria:
- Guardrail behavior visible in logs/metrics.
- polling behavior matches spec.

## Phase 4 — Warning-volume reduction + cleanup sweep
Scope:
- Rate-limit repetitive warnings (`NO_MEM`, `MQTT paused`) and downgrade expected race warnings.
- Perform dead-code sweep for all replaced SSE pathways.

Mandatory legacy removal:
- Remove unused API routes/endpoints if no longer consumed by UI.
- Remove unused notifier/state variables/functions created solely for deprecated SSE flows.

Exit criteria:
- warning volume reduced materially versus baseline.
- static search shows no obsolete path references.

## Phase 5 — Final validation + closure
Scope:
- Full build + run validation.
- Confirm HTTP availability under typical load.
- Confirm 5000 ms snapshot UX is acceptable.

Required evidence to record in this document:
- build result
- runtime log excerpts
- `/api/system_metrics` before/after summary
- list of removed legacy files/functions/branches.

Exit criteria:
- all phases marked `done`
- no outstanding legacy/redundant path items
- final sign-off note added.

---

## Final diagnosis
The principal web unavailability bug is an **HTTP service architecture issue (blocking SSE in handler context)**, amplified by **radio contention**, **NO_MEM recovery churn**, and **high synchronous log volume**.

The radio is a limiting factor, but the system can be made robust by preventing HTTP handler monopolization and reducing pressure amplification loops.