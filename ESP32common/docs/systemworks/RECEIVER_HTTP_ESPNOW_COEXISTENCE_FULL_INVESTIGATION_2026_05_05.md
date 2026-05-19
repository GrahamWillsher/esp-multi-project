# Receiver HTTP / ESP-NOW Coexistence — Full Investigation and Structured Resolution Plan (2026-05-05)

## 1) Scope and objective

This investigation covers the LCD receiver (`espnowreceiver_LCD`) behavior when the receiver web UI is opened at `http://192.168.1.59`, with focus on:

1. Repeated receiver-side `ESP_ERR_ESPNOW_NO_MEM` (`type=34` and then `type=1`)
2. Transmitter-side heartbeat timeout and reconnect loop
3. Whether HTTP handling should be treated in the ESP-NOW state machine similarly to MQTT
4. Comparison against the pre-shared-stack baseline (commit `820d3fa`) where behavior appeared stable

---

## 2) Evidence summary

### 2.1 Runtime logs (provided)

Receiver timeline when web UI is opened:
- periodic `MQTT Connecting to broker...` followed by `Connection failed, state=-2`
- repeated `Send failed (type=34 len=18): ESP_ERR_ESPNOW_NO_MEM`
- escalation to `Send failed (type=1 len=6): ESP_ERR_ESPNOW_NO_MEM`
- `ACK token watchdog release ... held=500ms` events

Transmitter timeline in same window:
- `Heartbeat/activity timeout` -> `CONNECTED -> IDLE`
- reconnect scan starts
- scan continues while receiver is no longer providing healthy ACK path

### 2.2 Message type mapping

From shared protocol enum (`esp32common/espnow_transmitter/espnow_common.h`):
- `type=34` = `msg_heartbeat_ack` (18-byte payload)
- `type=1`  = `msg_ack` (discovery ACK, 6-byte payload)

So the failures are on **control-plane ACK traffic**, not bulk telemetry.

### 2.3 Current LCD receiver behavior relevant to coexistence

1. Web dashboard polling (`lib/webserver_lcd/pages/dashboard_page_script.cpp`):
   - every 2 seconds: `GET /api/dashboard_data`
   - every 2 seconds: `GET /api/transmitter_health`
   - startup/visibility fetch of event-log summary

2. MQTT client (`src/mqtt/mqtt_task.cpp`, `src/mqtt/mqtt_client.cpp`):
   - gated by `EspNowConnectionManager::is_connected()`
   - when enabled with unreachable broker, repeatedly attempts TCP connect and backs off (`10s`, `20s`, `40s`, ...)
   - each connect attempt still generates WiFi/TCP control traffic (ARP/SYN/retry path)

3. Shared ESP-NOW TX scheduler (`esp32common/espnow_common_utils/espnow_tx_scheduler.cpp`):
   - all ESP-NOW sends serialized through scheduler task
   - NO_MEM retries and pacing are present
   - ACK-token watchdog releases stale discovery ACK ownership after timeout

4. Shared RX connection recovery (`esp32common/espnow_common_utils/rx_connection_handler.cpp`):
   - consecutive ACK NO_MEM threshold can trigger L1/L2 recovery (`esp_now_deinit/init`, then WiFi restart)

### 2.5 Additional code-level findings from follow-up investigation

1. **HTTP is currently not coexistence-gated at middleware level**
   - `lib/webserver_lcd/api/api_middleware.cpp` currently performs auth/content checks only.
   - There is no central HTTP policy hook equivalent to MQTT's connection gate.

2. **Some GET endpoints can still trigger ESP-NOW sends**
   - `api_monitor_sse_handler` sends `REQUEST_DATA` on stream start.
   - `api_get_mqtt_config_handler` opportunistically sends `config_section_request` on cache miss.
   - These are valid behaviors functionally, but they couple HTTP reads to radio TX pressure.

3. **Legacy quiet-mode guard is effectively disabled**
   - `ReceiverConnectionHandler::quiet_mode_active()` currently returns `false` unconditionally in shared header.
   - This means several HTTP-side guards that appear to exist are not providing back-pressure in practice.

4. **Dashboard currently performs two periodic network requests every 2 seconds**
   - `/api/dashboard_data`
   - `/api/transmitter_health`
   - This is a predictable, continuous poll load while the page is open.

### 2.4 Historical comparison (pre-shared-stack baseline)

In commit `820d3fa` (the previously stable period):
- receiver used local `rx_connection_handler.cpp` with direct `esp_now_send` calls
- no shared scheduler-based recovery ladder in common utils
- MQTT task on LCD receiver was pinned to worker core (`Core 1`), not WiFi core

In current branch (`da02232` and later):
- control-plane ACK traffic routed through shared scheduler
- stronger NO_MEM recovery machinery introduced
- MQTT pinned to WiFi core (`Core 0`) for lower IPC overhead

This was a significant architecture change in radio coexistence behavior.

---

## 3) Root-cause analysis

## 3.1 Primary root cause (high confidence)

The failures are consistent with **single-radio contention on ESP32-S3**:
- HTTP (TCP) + MQTT connect attempts + ESP-NOW share the same WiFi/LMAC TX resources
- During contention windows, `esp_now_send()` returns `ESP_ERR_ESPNOW_NO_MEM`
- When control ACKs fail repeatedly (`heartbeat_ack`, then discovery `ack`), transmitter no longer receives timely keepalive/handshake acknowledgements and transitions to `CONNECTION_LOST`

This is a coexistence/resource-arbitration issue, not a protocol enum issue.

## 3.2 Contributing factors

1. **Unreachable MQTT broker while enabled**
   - `state=-2` connect failures are active background radio load
   - this repeats even without user interaction, and becomes worse when HTTP is active

2. **Web UI polling cadence**
   - dashboard uses periodic polling rather than a single consolidated push stream
   - acceptable for normal operation, but adds bursts during a constrained radio budget

3. **Recovery sensitivity under bursty NO_MEM**
   - if thresholds are too low, transient contention can look like persistent stuck TX, causing unnecessary recover/reinit activity

4. **Receiver role requirement mismatch**
   - receiver is display/front-end and does not need low-latency HTTP responses
   - current behavior still treats web traffic as near-real-time polling workload

## 3.3 Not primary root cause

`Channel mismatch: WiFi=9 locked=5` on transmitter appears during reconnect scan state and is downstream symptom of link loss, not the initial trigger.

---

## 4) Why HTTP should be handled “on same lines as MQTT”

Current MQTT path already has a connection gate. HTTP does not: it is passive and accepts browser load whenever requested.

Equivalent policy for HTTP should be:
- **serve stale/cached data preferentially** under radio pressure
- **defer/deny ESP-NOW-triggering HTTP actions** while connection is degraded
- **reduce polling pressure by default** because this device is UI-first, not control-loop critical

In short: HTTP should be coexistence-aware, just as MQTT is coexistence-aware.

### 4.1 Practical model for “HTTP aware of WiFi/radio pressure”

Given the known single-radio resource limit, HTTP should not attempt to compete equally with ESP-NOW control traffic. The architecture should explicitly prioritize:

1. **ESP-NOW control plane first** (`msg_ack`, `msg_heartbeat_ack`, reconnect control)
2. **MQTT second** (only when connection gate permits)
3. **HTTP freshness last** (serve stale snapshot if needed)

Recommended implementation pattern:

- Add a shared `radio_pressure_state` (`NORMAL`, `CONSTRAINED`, `CRITICAL`) derived from scheduler and ACK metrics.
- Enforce policy centrally in `ApiMiddleware::dispatch()`:
   - In `CRITICAL`: read-only telemetry endpoints return cached snapshot + staleness metadata, mutating endpoints return `503`/`429` with retry hint.
   - In `CONSTRAINED`: throttle high-frequency routes and disable opportunistic ESP-NOW fetch-on-read behavior.
- Keep endpoint-specific logic simple; centralize policy so behavior is consistent and testable.

### 4.2 Polling cadence analysis: 2 seconds vs 5 seconds

For the dashboard page alone:

| Configuration | Requests/cycle | Rate | vs current |
|---|---|---|---|
| Current (2s, 2 endpoints) | 2 per 2s | **60 req/min/tab** | baseline |
| **Selected: merge endpoints, keep 2s** | 1 per 2s | **30 req/min/tab** | −50% |
| Merge endpoints + move to 5s | 1 per 5s | 12 req/min/tab | −80% |

**Design decision (2026-05-05):** Merge `transmitter_health` fields into `dashboard_data` response, keeping the 2s polling cadence. This delivers a **50% request reduction** with zero UX impact (freshness unchanged), and eliminates the second TCP request burst per cycle without degrading the display update rate.

**Reassessment point:** If radio pressure remains problematic after this change, moving to a 5s polling interval is the next lever — yielding an additional ~60% reduction (12 req/min/tab total, 80% less than the original baseline).

Implementation:
- `api_dashboard_data_handler` now includes all `api_transmitter_health_handler` fields under `data.transmitter`
- `StaticJsonDocument` in `api_dashboard_data_handler` increased to `768` bytes
- Dashboard JS updated to read health fields from `data.transmitter`; inner `/api/transmitter_health` fetch removed
- Initial 500ms page-load fetch also converted to `/api/dashboard_data`
- `/api/transmitter_health` endpoint retained for backward compatibility (direct API access, other tools)

---

## 5) Structured resolution plan (no quick fixes)

## Phase A — Instrumentation-first baseline (no behavior change)

Goal: prove causality and quantify radio pressure before policy changes.

A1. Add/extend counters exposed via `/api/system_metrics`:
- scheduler per-priority queue depths over time (min/avg/max)
- per-type send fail counts (`msg_heartbeat_ack`, `msg_ack`, `msg_request_data`)
- NO_MEM burst length histogram (not just total count)
- WiFi reconnect/scan/connect attempt timings
- HTTP active socket count over time and request/sec

A2. Add correlation markers:
- timestamped “HTTP request start/end” markers for heavy endpoints
- timestamped “MQTT connect attempt start/end/result”
- timestamped “ACK enqueue fail” markers

A3. Controlled test matrix:
1) ESP-NOW only (MQTT off, no browser)
2) ESP-NOW + browser dashboard
3) ESP-NOW + MQTT unreachable (no browser)
4) ESP-NOW + MQTT unreachable + browser dashboard

Acceptance:
- quantified NO_MEM onset threshold and exact trigger profile
- reproducible scenario with confidence

## Phase B — HTTP traffic shaping for UI-front-end role

Goal: make HTTP low-impact by design.

B1. **[IMPLEMENTED]** Consolidate dashboard polling to single endpoint:
- Merged `transmitter_health` fields into `dashboard_data` response → **30 req/min/tab** (was 60)
- 2s polling cadence retained; `/api/transmitter_health` preserved for direct/tool access
- Reassessment trigger: if pressure persists after this change, move to 5s → 12 req/min/tab

B1b. (Future) Additional polling reduction:
- Move to 5s baseline (or adaptive 5/10s by link health) if B1 alone is insufficient
- Stop polling when browser tab hidden; resume on visibility change

B2. Prefer SSE or cached snapshot for monitor/dashboard:
- one long-lived stream over repeated short polling requests
- server push only on meaningful value changes

B3. Add response degradation policy under pressure:
- when NO_MEM pressure active, return cached snapshot with staleness metadata
- avoid synchronous/triggering operations from read-only pages

B4. Remove HTTP->ESP-NOW side effects from passive reads under pressure:
- in constrained/critical states, suppress opportunistic ESP-NOW sends from GET handlers
- only allow explicit user actions (POST/mutating) to request fresh remote data

Acceptance:
- web UI remains usable
- ACK fail bursts no longer correlate with normal page open/refresh

## Phase C — Coexistence policy layer (HTTP/MQTT symmetry)

Goal: explicit radio budget arbitration.

### C — Code investigation findings (2026-05-05)

Before designing the implementation, a full code audit of the relevant files was performed. Key findings:

#### C-F1: `RxRadioArbiterFsm` — radio state machine already exists
`esp32common/espnow_common_utils/rx_radio_arbiter_fsm.h`

The FSM already models exactly the states we need:
```
BOOTSTRAP → STEADY_CONNECTED → RECONNECT_DETECTED → ACK_RECOVERY_WINDOW
                                                    → POST_RECONNECT_SETTLE
                                                    → DEGRADED_FALLBACK
```
It also already carries a per-state `Policy` struct:
```cpp
struct Policy {
    bool mqtt_allowed = false;
    bool control_only_mode = true;
    bool purge_non_control_on_entry = false;
    bool noncritical_enqueue_allowed = false;
    AckRetryProfile ack_retry_profile = AckRetryProfile::RECOVERY;
    DiagnosticProfile diagnostic_profile = DiagnosticProfile::NORMAL;
};
```
`mqtt_allowed` per state is already computed; HTTP needs equivalent treatment.

#### C-F2: `EspnowTxScheduler` — real-time pressure metrics already exposed
`esp32common/espnow_common_utils/espnow_tx_scheduler.h`

```cpp
uint32_t get_consecutive_no_mem_count();   // persistent pressure indicator
bool read_stats(Stats& out_stats);         // Stats::send_fail_no_mem, per-priority fail counts
bool read_queue_depths(QueueDepths& out);  // control/discovery/data/monitoring depth
```
All the signals needed to derive a `RadioPressureState` are already queryable.

#### C-F3: `quiet_mode_active()` — 14 dead call sites already in HTTP handlers
`esp32common/espnow_common_utils/rx_connection_handler.h`:
```cpp
// Legacy compatibility API used by webserver handlers.
// Phase 3 removed reconnect quiet-mode state-machine gating, so this now
// intentionally reports false (no transport-level quiet mode).
bool quiet_mode_active() const { return false; }
```
14 call sites across `api_control_handlers.cpp`, `api_network_handlers.cpp`, `api_settings_handlers.cpp`, `api_sse_handlers.cpp` already check `quiet_mode_active()` — but the signal was removed in Phase 3. **The call-site infrastructure is already wired; only the signal source needs restoring.**

#### C-F4: `ApiMiddleware::dispatch()` — single clean injection point
`espnowreceiver_LCD/lib/webserver_lcd/api/api_middleware.cpp`

All HTTP routes funnel through `dispatch()`. Currently performs: auth gate → content-type gate → handler call. Pressure policy can be injected here centrally, consistent with how MQTT gates on `EspNowConnectionManager::is_connected()`.

The `RoutePolicy` enum (`ReadOnly`, `MutatingNoBody`, `MutatingJson`) already differentiates route types — pressure policy can use this to vary behavior (block mutations under `CRITICAL`, degrade reads under `CONSTRAINED`).

---

### C — Concrete implementation plan

#### C1. Add `RadioPressureState` query function

#### C1. **[IMPLEMENTED]** Add `RadioPressureState` query function

New files created:
- `esp32common/espnow_common_utils/radio_pressure_state.h` — enum declaration + `get_radio_pressure_state()` + `radio_pressure_state_to_string()`
- `esp32common/espnow_common_utils/radio_pressure_state.cpp` — implementation
- `esp32common/include/esp32common/espnow/radio_pressure_state.h` — stable public forwarding header

```cpp
enum class RadioPressureState : uint8_t {
    NORMAL      = 0,   // STEADY_CONNECTED, no elevated NO_MEM count
    CONSTRAINED = 1,   // RECONNECT_DETECTED / ACK_RECOVERY_WINDOW / POST_RECONNECT_SETTLE
   CRITICAL    = 2,   // DEGRADED_FALLBACK or consecutive_no_mem_count >= threshold
};
RadioPressureState get_radio_pressure_state();
const char* radio_pressure_state_to_string(RadioPressureState state);
```

Derivation logic sources `RxRadioArbiterFsm::instance().state()` and `EspnowTxScheduler::get_consecutive_no_mem_count()`.
Threshold tuneable via build flag `-D RADIO_PRESSURE_CRITICAL_NO_MEM_THRESHOLD=<n>` (default: `5`).

#### C2. **[IMPLEMENTED]** Restore `quiet_mode_active()` to delegate to `RadioPressureState`

Files modified:
- `esp32common/espnow_common_utils/rx_connection_handler.h` — dead `return false` inline removed; replaced with proper declaration
- `esp32common/espnow_common_utils/rx_connection_handler.cpp` — `#include "radio_pressure_state.h"` added; implementation added at end of file

```cpp
// rx_connection_handler.cpp
bool ReceiverConnectionHandler::quiet_mode_active() const {
    return get_radio_pressure_state() != RadioPressureState::NORMAL;
}
```

All 14 existing `quiet_mode_active()` call sites across `api_control_handlers.cpp`, `api_network_handlers.cpp`, `api_settings_handlers.cpp`, `api_sse_handlers.cpp` now receive a live signal with no per-handler code changes required.

#### C3. **[IMPLEMENTED]** Add central pressure enforcement to `ApiMiddleware::dispatch()`

File modified: `espnowreceiver_LCD/lib/webserver_lcd/api/api_middleware.cpp`
- `#include <esp32common/espnow/radio_pressure_state.h>` added
- Pressure gate injected after auth/content checks in `dispatch()`

Behavior:
- **CRITICAL**: mutating routes (`MutatingNoBody`, `MutatingJson`) return `503 Service Unavailable` with a structured error message; read-only routes pass through with `X-Radio-Pressure: critical` header
- **CONSTRAINED**: all routes pass through; `X-Radio-Pressure: constrained` header added for observability
- **NORMAL**: no header; full behavior unchanged

The `X-Radio-Pressure` response header is visible in browser DevTools and enables future client-side adaptive polling.

#### C4. Extend `RoutePolicy` to include `ReadOnlyCanDegrade` (future Phase C+)

For endpoints that have a cached snapshot path:
```cpp
enum class RoutePolicy {
    ReadOnly,
    ReadOnlyCanDegrade,  // serve stale snapshot under CONSTRAINED/CRITICAL
    MutatingNoBody,
    MutatingJson,
};
```
Handlers tagged `ReadOnlyCanDegrade` can check `get_radio_pressure_state()` and return their cached last-known value with a `"stale": true` field instead of triggering a live fetch.

#### C5. Exclude safety-critical routes from pressure throttling
#### C5. Exclude safety-critical routes from pressure throttling (future Phase C+)

OTA and reboot routes must remain accessible regardless of radio state. These should be tagged with a `SafetyCritical` policy that bypasses pressure checks (or explicitly whitelisted in `dispatch()`).

---

### C — Acceptance criteria
- No transmitter heartbeat timeouts during normal browser use
- Deterministic degrade/recover behavior instead of collapse/reconnect loops
- All 14 existing `quiet_mode_active()` call sites automatically gain live pressure signal ✅
- HTTP response headers expose pressure state for observability ✅
- Mutating routes return structured `503` under `CRITICAL` with retry hint ✅

## Phase D — Validation and rollout

D1. 24-hour soak tests (with scripted browser load + unreachable MQTT profile)
D2. Success criteria:
- zero `CONNECTED -> IDLE` transitions caused by ACK starvation
- NO_MEM events may occur transiently, but no prolonged ACK watchdog churn
- web UI stays responsive enough for display/front-end use

D3. Rollout:
- first on `espnowreceiver_LCD`
- then mirror to `espnowreceiver_2`

---

## 6) Recommended immediate operating posture (while Phases A–D are implemented)

1. If MQTT broker is not reachable, disable receiver-side MQTT temporarily to preserve radio headroom.
2. Temporarily increase dashboard polling to 5 seconds during diagnostics.
3. Use one browser tab only; avoid rapid refresh during diagnostics.
4. Use `/api/system_metrics` sampling to capture pressure signatures when reproducing.

These are temporary operational mitigations, not final fixes.

---

## 7) Implementation recommendation order

1. **Phase B1 — [IMPLEMENTED]** Endpoint consolidation (60 → 30 req/min/tab, no UX change).
2. **Phase C1+C2+C3 — [IMPLEMENTED]** `RadioPressureState` module created; `quiet_mode_active()` restored; middleware pressure gate active.
   - Build verified: `[SUCCESS]` waveshare_esp32s3_lcd7_lvgl, Flash 50.6%, RAM 75.1%
3. **Phase A** — Instrument counters if field observation shows pressure events that the current gates are not resolving.
4. **Phase B1b** — Move to 5s polling if Phase C + B1 together still leave pressure issues.
5. **Phase D** — 24-hour soak validation and rollout to `espnowreceiver_2`.

---

## 8) Conclusion

Yes — HTTP activity is a practical trigger in the current coexistence envelope, but the deeper issue is shared radio TX resource contention across ESP-NOW + MQTT + HTTP. The correct fix is a structured coexistence architecture with measurement, traffic shaping, and explicit pressure policy, not isolated one-off tweaks.

Follow-up conclusions:
Follow-up conclusions:
- **[IMPLEMENTED — Phase B1]** Merging `transmitter_health` fields into `dashboard_data` delivers a 50% request reduction (60 → 30 req/min/tab) at 2s polling cadence with no UX degradation.
- **[IMPLEMENTED — Phase C1+C2+C3]** Structured coexistence architecture is in production code:
   1. `radio_pressure_state.h/.cpp` in `esp32common/espnow_common_utils/` — `RadioPressureState` derived live from `RxRadioArbiterFsm` + `EspnowTxScheduler`
   2. `ReceiverConnectionHandler::quiet_mode_active()` restored as a one-liner delegating to `get_radio_pressure_state()` — 14 handler call sites reactivated with zero per-handler changes
   3. `ApiMiddleware::dispatch()` gates mutating routes under `CRITICAL` with `503`; adds `X-Radio-Pressure` header for observability at all non-NORMAL states
- Moving to 5s polling cadence remains available as a second-order reduction (→ 12 req/min/tab, 80% below baseline) if radio pressure persists after Phase C lands.
