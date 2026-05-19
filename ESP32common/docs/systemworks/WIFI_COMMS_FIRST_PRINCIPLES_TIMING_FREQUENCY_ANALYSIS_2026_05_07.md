# Wi-Fi Communications First-Principles Timing & Frequency Analysis (ESP-NOW / MQTT / HTTP)

Date: 2026-05-07  
Scope: `ESPnowtransmitter2`, `espnowreceiver_LCD`, shared `esp32common` communication utilities, and historical comparison with prior `main` behavior.

---

## 1) Executive Summary

The architecture is materially improved versus historical builds (especially removal of long-lived SSE streaming from hot dashboard paths), but radio pressure can still occur when:

1. ESP-NOW control traffic (ACK/heartbeat/discovery) overlaps with bursty HTTP polling and MQTT connect/publish windows.
2. Scheduler retries happen during transient Wi-Fi TX buffer starvation (`ESP_ERR_ESPNOW_NO_MEM`).
3. A few remaining high-frequency HTTP patterns keep socket/radio churn elevated.

The strongest current protections are:
- Priority-based ESP-NOW TX scheduling with per-message pacing and bounded retries.
- Receiver-side radio-pressure gate that throttles read APIs and pauses/defers MQTT during `CONSTRAINED`/`CRITICAL` pressure.
- Three-stage NO_MEM recovery ladder (L1 ESP-NOW reinit, L2 Wi-Fi restart, L3 reboot).

Primary focus after implementation:
- Run soak validation to confirm reduced contention translates into fewer NO_MEM clusters and stable page load behavior under multi-tab use.

---

## 2) ESP-NOW Message Timing and Frequency Map

## 2.1 Global timing contract (shared)
From shared timing config:
- Heartbeat interval: **10,000 ms**
- Heartbeat timeout: **30,000 ms**
- ESP-NOW connecting timeout: **45,000 ms**
- Heartbeat ACK timeout: **1,000 ms**
- Data sender interval: **2,000 ms**
- Generic discovery announcement interval: **5,000 ms**
- Discovery probe interval: **200 ms**
- Per-channel dwell (active scan): **2,000 ms**

## 2.2 Transmitter → Receiver periodic/control traffic

| Message | Trigger | Nominal cadence | Notes |
|---|---|---:|---|
| `msg_heartbeat` | `HeartbeatManager::tick()` while connected | 10 s (0.1 Hz) | Core liveness/time/state payload.
| `msg_temperature_report` | Immediately after heartbeat send success | 10 s (0.1 Hz) | Best-effort monitoring message.
| `msg_data` (`espnow_payload_t`) | `DataSender` when transmission active | 2 s source cadence (0.5 Hz) | Cached first, then sent by background TX task.
| `msg_version_beacon` | Periodic + forced on runtime/config changes | 30 s periodic (0.033 Hz) + event-driven | Includes config versions and runtime states.
| `msg_probe` (discovery) | Active hop scan | every 200 ms during dwell | Up to 10 probes in a 2 s dwell window.

## 2.3 Receiver → Transmitter control traffic

| Message | Trigger | Nominal cadence | Notes |
|---|---|---:|---|
| `msg_ack` (discovery ACK) | On received PROBE | immediate (scheduler-governed) | Reconnect-critical path.
| `msg_heartbeat_ack` | On heartbeat receive | immediate (scheduler-governed) | Also updates connection keepalive.
| `msg_request_data` | Post-connect init and retry loop | event-driven | Retries when power-profile freshness not confirmed.
| `msg_abort_data` | UI/control action | event-driven | Stops selected streams.

## 2.4 Discovery scan envelope (transmitter active hopping)

Phase 1:
- 13 channels × 2 s dwell = **26 s** max.

Phase 2 weighted sweep around last known channel:
- center: 4 s, neighbors: 3 s + 3 s = **10 s** max.

Worst-case full pass (excluding tiny transition overhead):
- **~36 s** before failing a full attempt.

This aligns with shared `ESPNOW_CONNECTING_TIMEOUT_MS = 45 s` headroom.

## 2.5 ESP-NOW TX scheduler pacing policy (shared)

| Message type | Min gap | Retry attempts |
|---|---:|---:|
| `msg_ack` | 150 ms | 12 |
| `msg_heartbeat_ack` | 80 ms | 8 |
| `msg_heartbeat` | 800 ms | 4 |
| `msg_probe` | 250 ms | 3 |
| `msg_request_data` | 1000 ms | 3 |
| `msg_battery_status` | 1000 ms | 1 |
| `msg_charger_status` / `msg_inverter_status` / `msg_system_status` | 500 ms | 1 |
| `msg_config_section_request` | 3000 ms | 3 |
| `msg_temperature_report` | 1000 ms | 1 |
| `msg_version_beacon` | 60000 ms | 1 |

Additional anti-pressure controls:
- NO_MEM retry backoff is progressive per attempt.
- Post-NO_MEM worker backoff: 20 ms before next send loop.
- Inter-frame delay configured (receiver LCD runtime currently 8 ms).

## 2.6 Requested deep-dive: `msg_data` 2 s cadence and the selected policy entries

### `msg_data` every 2 s (background stream)

Current behavior is intentional and currently low-risk:
- The source generator (`DataSender`) emits one telemetry sample every 2 s when transmission is active.
- The sample is cached first (`EnhancedCache`) and then drained by a background transmission task.
- Background transmission loop can run at 50 ms, but it only sends unsent cache entries; with 2 s source cadence, steady-state traffic is still ~0.5 Hz.

Conclusion: `msg_data` at 2 s is conservative for UI freshness and does not itself create a high radio-rate path.

### Why these scheduler rows are not all the same despite similar class grouping

The user-highlighted rows:
- `msg_request_data` (1000 ms, 3 retries)
- `msg_battery_status` (1000 ms, 1 retry)
- `msg_charger_status` / `msg_inverter_status` / `msg_system_status` (500 ms, 1 retry)
- `msg_config_section_request` (3000 ms, 3 retries)
- `msg_temperature_report` (1000 ms, 1 retry)

Findings:
1. In current transmitter runtime, these are not equally active.
   - `msg_request_data` and `msg_config_section_request` are actively used control/config requests (receiver → transmitter).
   - `msg_temperature_report` is actively used (heartbeat-cadence best-effort telemetry).
   - `msg_battery_status` / `msg_charger_status` / `msg_inverter_status` / `msg_system_status` are protocol-capable and routed, but the active transmitter stream is primarily `msg_data` in this build path.
2. Semantics differ even within same scheduler priority bands.
   - `msg_config_section_request` is intentionally slower (3 s gap) to avoid repeated config-sync storms.
   - `msg_request_data` is control-plane and may need retries to establish/refresh stream intent.
   - `msg_temperature_report` is monitoring-only and safe as low-retry best-effort.
3. Therefore, equalizing all these rows to one uniform policy is not recommended.

Practical recommendation:
- Keep differentiated policy for now.
- If simplification is desired, first remove or isolate entries not used by current transmitter send path, then unify only genuinely equivalent active flows.

---

## 3) MQTT Timing and Frequency Map

## 3.1 Transmitter MQTT

| Flow | Cadence | Condition |
|---|---:|---|
| Main telemetry publish | every 10 s | while connected |
| Cell-data publish | every 5 s | while connected |
| Event log publish | every 5 s | only if event-log subscribers > 0 |
| MQTT stats log | every 30 s | task always running |
| Task loop poll | every 1 s | state machine update loop |

Observations:
- Cell-data cadence is now aligned to 5 s, reducing periodic MQTT pressure materially.
- Connect/reconnect behavior is already bounded by timing config and manager logic.

Implementation status:
- Completed: cell-data publish interval moved from 1 s to 5 s via shared timing config.
- MQTT retained topic behavior is unchanged, so latest snapshot delivery semantics are preserved.

## 3.2 Receiver MQTT coexistence behavior

Receiver MQTT task poll cadence: **100 ms**.  
Gates:
- If ESP-NOW not connected: force MQTT disconnect and defer.
- If radio pressure is `CRITICAL`: force MQTT disconnect.
- If pressure is `CONSTRAINED`: do not start reconnect when disconnected.

This is a strong coexistence safeguard and should be retained.

---

## 4) HTTP Timing and Frequency Map

## 4.1 Browser polling cadences (receiver LCD pages)

| Page | Endpoint(s) | Base cadence | Backoff/adaptive |
|---|---|---:|---|
| Dashboard (`/`) | `/api/dashboard_data` | 5 s (12 req/min) | yes (`X-Radio-Pressure`, `Retry-After`) |
| Monitor2 | `/api/monitor` | 5 s (12 req/min) | yes |
| Cell monitor | `/api/cell_data` | 5 s (12 req/min) | yes |
| Monitor | `/api/monitor` | 5 s (12 req/min) baseline | yes (jitter + backoff + `Retry-After`) |
| Event logs page | `/api/event_logs/*`, `/api/get_event_logs` | bursty, state-driven | partial (snapshot polling/retries) |

Status update:
- `/monitor` has now been aligned to the same adaptive polling model as `monitor2` (5 s baseline, backoff, jitter, `Retry-After` support).

## 4.2 API pressure middleware behavior

Read throttles under pressure:
- `CONSTRAINED`: min interval **3 s** on selected telemetry routes.
- `CRITICAL`: min interval **5 s** on selected telemetry routes.

Throttled routes include:
- `/api/dashboard_data`
- `/api/transmitter_health`
- `/api/get_event_log_summary`
- `/api/system_metrics`
- `/api/monitor`

Mutating API behavior:
- Under `CRITICAL`, mutating routes are rejected (`503`) to prioritize control-plane recovery.

## 4.3 Event logs page load pattern (important)

Current flow can create short request bursts:
- `/api/event_logs/subscribe` once.
- `/api/event_logs/snapshot_status` every 500 ms up to 5 s (up to ~10 requests per wait cycle).
- `/api/get_event_logs?limit=500` plus up to 5 retry rounds with 1 s delay.

This is acceptable for occasional page visits but should be rate-softened when troubleshooting radio pressure.

## 4.4 Webpage-loadability review (requested)

Conclusion: the documented cadence/throttle changes are expected to improve page load reliability and responsiveness, not reduce it.

Why:
- They reduce steady-state request pressure (`/monitor` now 5 s adaptive instead of 1 s fixed).
- They preserve all existing routes/endpoints (no URI removals required for these changes).
- They are compatible with existing pressure middleware (`X-Radio-Pressure`, `Retry-After`) and therefore cooperate with reconnect recovery.
- HTTP server provisioning already has headroom (`max_open_sockets=8`, `max_uri_handlers=80`) relative to expected route count.

Important scope note:
- These changes improve the probability of successful page loads under contention.
- They do not override independent startup prerequisites such as Wi-Fi/AP availability and successful HTTP server initialization.

Operational acceptance criteria for “pages load”:
1. Web server reports started and all expected handlers register.
2. First-page load succeeds for `/`, `/monitor`, `/monitor2`, `/cellmonitor`, `/events`.
3. Under multi-tab load, no sustained 429/503 loops; clients recover via backoff and continue rendering snapshots.

---

## 5) Historical GitHub Comparison (Main vs Current)

Key historical finding from prior mainline receiver web stack:
- `monitor2` used EventSource/SSE and server-side long-lived streaming handler loops (`text/event-stream`) with keepalive/ping behavior.

Current status:
- LCD receiver API route table no longer exposes SSE monitor stream paths.
- `monitor2` and `cellmonitor` use controlled polling with backoff + jitter + `Retry-After` support.
- Dashboard merged transmitter health into `/api/dashboard_data`, removing a second periodic endpoint hit.

Interpretation:
- This is a substantial improvement and likely reduced socket residency and server task occupancy.
- `/monitor` has now been aligned to adaptive 5 s baseline polling, removing that prior 1 s hotspot.
- Dashboard and MQTT cell-data are now both at 5 s cadence; event snapshot burst density has also been reduced.

---

## 6) Espressif Official Guidance Alignment

Relevant best practices and alignment:

1. After `ESP_ERR_ESPNOW_NO_MEM`, delay before next send.  
   - **Aligned**: scheduler retries/backoff + post-failure delay + recovery ladder.

2. Avoid very short fixed send intervals; prefer callback-paced sending where possible.  
   - **Partially aligned**: min-gap policy exists; some flows still timer-driven and can overlap with other traffic.

3. Keep send/recv callbacks minimal because they run in Wi-Fi high-priority context.  
   - **Aligned**: architecture pushes work into queues/tasks and central scheduler.

4. Keep channel handling consistent (`channel=0` peer behavior tied to current channel).  
   - **Aligned**: discovery/peer handling uses channel coherence checks and lock/verify logic.

---

## 7) Root-Cause Synthesis (Why pressure still appears)

Primary mechanism is aggregate contention, not single-path failure:

- ESP-NOW control bursts (ACK/heartbeat/reconnect) + MQTT bursts (connect/1 Hz cell topic) + HTTP polling can temporarily saturate shared Wi-Fi TX resources.
- During those windows, scheduler can observe clustered NO_MEM failures.
- Recovery ladder protects from permanent deadlock but L1/L2 recovery events are expensive and user-visible.

So the system is robust but still over-driven in some overlapping windows.

---

## 8) Recommended Actions (Prioritized)

## 8.0 Implementation status snapshot (as of 2026-05-07)

- Completed:
   - `/monitor` migrated to adaptive 5 s polling with backoff + jitter + `Retry-After` behavior.
   - MQTT cell-data publish interval moved to 5 s.
   - Dashboard (`/`) base polling moved to 5 s; constrained/critical extensions retained.
   - Event snapshot-status polling interval increased to 500 ms.
- Pending:
   - Soak validation and metrics collection (Stage D).

## P0 (Immediate)
1. Execute soak validation and capture metrics deltas versus baseline.
2. Confirm no regressions in page usability during AP/AP+STA fallback operation.

## P1 (Short term)
3. If `/events` still bursts in field logs, consider reducing `SNAPSHOT_RETRY_ATTEMPTS` (5 → 3).
4. Ensure all UI pages continue to consume `X-Radio-Pressure` + `Retry-After` consistently.

## P2 (Medium)
5. Consider callback-paced ESP-NOW for selected low-priority telemetry classes (monitoring/state) instead of strict timer cadence.
6. Introduce pressure-adaptive MQTT publish policy (e.g., 5 s nominal, slower under `CONSTRAINED`).

## P3 (Validation/observability)
7. Track and alert on:
   - consecutive NO_MEM streaks,
   - ACK queue fail rates,
   - HTTP throttle counts,
   - reconnect duration percentiles.

---

## 9) Validation Plan

1. Single-client baseline:
- Dashboard open only for 30 minutes; verify NO_MEM spikes and reconnect count near zero.

2. Multi-client HTTP stress:
- Open dashboard + monitor + cellmonitor + events in parallel; compare before/after P0 changes.

3. MQTT stress:
- Force broker reconnect churn while running HTTP clients; verify receiver pressure gate holds ESP-NOW link stable.

4. Regression criteria:
- No entry into L2/L3 recovery during normal operation.
- Significant reduction in `send_fail_no_mem` and read-throttle critical hits.

---

## 10) Implementation Plan (phased)

This plan implements pending items in low-risk increments with measurable checkpoints.

### Phase A — MQTT cell publish cadence (1 s → 5 s) [COMPLETED]

Goal:
- Reduce sustained MQTT pressure by ~80% for cell telemetry topic.

Implemented:
1. Updated shared timing constant `MQTT_CELL_PUBLISH_INTERVAL_MS` from 1000 to 5000.
2. Rebuilt transmitter successfully to validate integration.

Primary file targets:
- `esp32common/include/esp32common/config/timing_config.h`
- `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_task.cpp` (validation only; cadence is constant-driven)

Acceptance criteria:
- `publish_cell_data()` emits at ~5 s intervals while connected.
- No increase in reconnect rate or MQTT publish failures.
- Receiver dashboards still show current cell snapshots (retained topic behavior unchanged).

Rollback:
- Revert `MQTT_CELL_PUBLISH_INTERVAL_MS` to 1000.

### Phase B — Dashboard polling cadence (`/` 2 s → 5 s) [COMPLETED]

Goal:
- Lower baseline HTTP read load and reduce overlap with ESP-NOW control traffic.

Implemented:
1. Changed dashboard base interval from 2000 ms to 5000 ms.
2. Kept pressure-aware behavior and set constrained/critical extension intervals above baseline.

Primary file target:
- `espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page_script.cpp`

Acceptance criteria:
- `/` performs ~12 req/min baseline (down from ~30 req/min).
- UI remains responsive and continues pressure-aware behavior via `X-Radio-Pressure` and `Retry-After`.

Rollback:
- Restore `DASHBOARD_POLL_BASE_MS` to 2000.

### Phase C — Event logs snapshot burst softening [COMPLETED]

Goal:
- Reduce short startup bursts when opening `/events`.

Implemented:
1. Increased `SNAPSHOT_WAIT_POLL_MS` from 200 ms to 500 ms.
2. Kept retry count unchanged pending soak data.
3. Subscription semantics unchanged.

Primary file target:
- `espnowreceiver_LCD/lib/webserver_lcd/pages/event_logs_page_script.cpp`

Acceptance criteria:
- Lower request burst density on `/api/event_logs/snapshot_status`.
- Page still converges to complete snapshot with acceptable UX delay.

Rollback:
- Restore previous polling/retry constants.

### Phase D — Verification and soak testing

Goal:
- Confirm pages remain loadable and reconnect robustness improves.

Validation matrix:
1. Single-client baseline (30 min): open `/` only.
2. Multi-page stress (30 min): open `/`, `/monitor`, `/monitor2`, `/cellmonitor`, `/events`.
3. Broker churn test: restart/unavailable MQTT broker while pages remain open.
4. AP fallback test: force STA drop and verify AP/AP+STA page access remains functional.

Metrics to record:
- ESP-NOW scheduler: `send_fail_no_mem`, control/discovery/data fail deltas.
- API middleware: throttle counts, 429/503 rates.
- Webserver runtime: startup success, registered handler count, restart count.
- Reconnect diagnostics: duration and escalation level (L1/L2/L3 occurrence).

Exit criteria:
- No L2/L3 recovery during normal soak.
- Significant reduction in NO_MEM clusters vs pre-change baseline.
- All core pages consistently load in STA and AP/AP+STA fallback modes.

Build validation status:
- Receiver LCD build: SUCCESS.
- Transmitter build: SUCCESS.

### Phase E — Optional policy simplification (deferred)

Do not unify scheduler message policies yet. First collect real send-path usage over soak logs. If a message type remains inactive, remove or isolate it before any policy consolidation.

---

## 11) Final Assessment

The communication architecture is now fundamentally sound and much closer to Espressif-recommended coexistence behavior than earlier mainline implementations. Major periodic pressure contributors (dashboard cadence, MQTT cell cadence, event snapshot poll burst rate, legacy `/monitor`) have been reduced. Remaining work is validation-focused: proving in soak that NO_MEM cluster frequency and page load interruptions are materially lower.