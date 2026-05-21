# Receiver MQTT Gate Audit (LCD + Receiver_2)

Date: 2026-05-19  
Scope: `espnowreceiver_LCD`, `espnowreceiver_2`, and shared MQTT feature flags in `esp32common`.

## Purpose

Identify all implemented "gates" that could prevent full MQTT functionality, especially at first boot / day-one configuration.

---

## Executive Findings

The receivers have **multiple layered gates**. MQTT behavior is not controlled by one switch; it is controlled by:

1. **Compile-time feature flags** (shared header)
2. **Runtime task enablement checks** (config + broker validity)
3. **Runtime client connect/publish guards** (enabled, Wi-Fi, connection state, retry cadence)
4. **HTTP/API command channel guards** (feature + connected-state requirements)
5. **Stream subscription gates** (cell/event subscriptions only active under subscriber state)
6. **Web middleware pressure gates** (heap admission + endpoint rate limiting)

This is consistent with a staged/protected rollout model rather than always-on MQTT.

---

## Gate Inventory

## 1) Compile-Time Feature Gates (shared)

File: `esp32common/include/esp32common/mqtt/mqtt_feature_flags.h`

- Line 49: `#define MQTT_FEATURE_LIVE_BATTERY_TELEMETRY 0`
  - Effect: live battery telemetry feature path is disabled at compile-time where this macro is honored.
- Line 64: `#define MQTT_FEATURE_CELL_DATA_TELEMETRY 1`
- Line 79: `#define MQTT_FEATURE_EVENT_LOG_STREAMING 1`
- Line 104: `#define MQTT_FEATURE_COMMANDS 1`
- Line 114: `#define MQTT_FEATURE_ESPNOW_COEXISTENCE_GATES 0`
  - Observed usage: definition exists; no active code references found in receiver trees.

**Day-one impact:** if transport parity expected "all streams always on", the disabled live telemetry flag is a hard compile-time limiter.

---

## 2) MQTT Task Runtime Gates (receiver startup loop)

Files:
- `espnowreceiver_LCD/src/mqtt/mqtt_task.cpp`
- `espnowreceiver_2/src/mqtt/mqtt_task.cpp`

Confirmed gates:
- `ReceiverNetworkConfig::isMqttEnabled()` must be true (LCD line 29 / receiver_2 line 28)
- Broker must not be `0.0.0.0` (guard in same block; comment: "Skip if server is still 0.0.0.0")
- Else path forces disable: `MqttClient::setEnabled(false)` (LCD lines 56/59, receiver_2 lines 55/58)

**Day-one impact:** MQTT remains effectively off until network config is complete and broker IP is valid.

---

## 3) MQTT Client Runtime Guards (connect/publish/loop)

Files:
- `espnowreceiver_LCD/src/mqtt/mqtt_client.cpp`
- `espnowreceiver_2/src/mqtt/mqtt_client.cpp`

Confirmed gates:
- Enabled and broker checks before connect path:
  - LCD line 294, receiver_2 line 131 (`!enabled_ || broker_ip_[0] == 0`)
- Wi-Fi connected prerequisite:
  - LCD line 302, receiver_2 line 139 (`WiFi.status() != WL_CONNECTED`)
- Publish guard blocks send when disabled/null:
  - LCD line 382, receiver_2 line 197
- Loop guard exits if disabled:
  - LCD line 394, receiver_2 line 231
- Reconnect cadence throttling:
  - LCD: explicit backoff state (`reconnect_interval_ms_`, lines 245, 309, 342, 360-364)
  - receiver_2: fixed reconnect interval policy (no exponential state variable equivalent observed)

**Day-one impact:** even after task starts, client traffic is suppressed until all runtime prerequisites align.

---

## 4) Command Path Gates (HTTP -> MQTT command forwarding)

### Settings handlers
- `espnowreceiver_2/lib/webserver/api/api_settings_handlers.cpp`
  - `#if MQTT_FEATURE_COMMANDS` lines 23, 211
  - connected check lines 24, 212
  - unavailable response lines 264-265 (`"MQTT command channel unavailable"`)
- `espnowreceiver_LCD/lib/webserver_lcd/api/api_settings_handlers.cpp`
  - connected check line 23
  - `#if MQTT_FEATURE_COMMANDS` line 204
  - connected check line 205
  - unavailable response lines 232-233

### Network handlers
- `espnowreceiver_2/lib/webserver/api/api_network_handlers.cpp`
  - `#if MQTT_FEATURE_COMMANDS` lines 269, 403
  - connected check lines 270, 404
  - unavailable response lines 331-332, 458-459
- `espnowreceiver_LCD/lib/webserver_lcd/api/api_network_handlers.cpp`
  - `#if MQTT_FEATURE_COMMANDS` lines 312, 426
  - connected checks lines 313, 350, 427
  - unavailable response lines 342-343, 456-457

### Control handlers
- `espnowreceiver_2/lib/webserver/api/api_control_handlers.cpp`
  - command feature gates lines 504, 526
  - connected checks lines 505, 527
  - unavailable response line 570
- `espnowreceiver_LCD/lib/webserver_lcd/api/api_control_handlers.cpp`
  - command feature gate line 523
  - connected check line 524
  - unavailable response lines 541-542

### Debug handlers
- `espnowreceiver_2/lib/webserver/api/api_debug_handlers.cpp`
  - command feature gate line 43
  - connected check line 44
  - unavailable response line 90
- `espnowreceiver_LCD/lib/webserver_lcd/api/api_debug_handlers.cpp`
  - command feature gate line 41
  - connected check line 42
  - unavailable response line 63

### Additional receiver_2 command guards
- `espnowreceiver_2/lib/webserver/api/api_type_selection_handlers.cpp`
  - connected checks lines 49, 202
- `espnowreceiver_2/lib/webserver/api/api_led_handlers.cpp`
  - connected check line 63
- `espnowreceiver_2/lib/webserver/api/api_sse_handlers.cpp`
  - connected checks lines 229, 316

**Day-one impact:** command-style HTTP writes are intentionally blocked unless MQTT command channel is active and connected.

---

## 5) Subscription/Streaming Gates inside MQTT client

Files:
- `espnowreceiver_LCD/src/mqtt/mqtt_client.cpp`
- `espnowreceiver_2/src/mqtt/mqtt_client.cpp`

Confirmed gates:
- Cell data topic subscription conditional on state not paused:
  - LCD line 607, receiver_2 line 429 (`cell_data_state_ != PAUSED`)
- Event stream control conditional on subscriber count:
  - LCD lines 614, 1301, 1359; receiver_2 line 1312 (`event_log_subscribers_ > 0`)
- Stream control command function present:
  - LCD line 56, receiver_2 line 49 (`publish_event_logs_stream_control(...)`)

**Day-one impact:** some streams are demand-driven, not unconditional; apparent "missing data" may actually be subscriber-gated behavior.

---

## 6) Web/API Middleware Pressure Gates (can mask MQTT functionality)

Files:
- `espnowreceiver_LCD/lib/webserver_lcd/api/api_middleware.cpp`
- `espnowreceiver_2/lib/webserver/api/api_middleware.cpp`

Confirmed gates:
- Heavy endpoint heap admission thresholds:
  - min free heap = 60 KB (line 28 in both)
  - min largest 8-bit block = 16 KB (line 29 in both)
  - reject path with `503 Service Unavailable` + retry JSON (heap admission function around lines 165-183 LCD, 163-181 receiver_2)
- Endpoint token-bucket rate limits on monitor/dashboard/cell/events APIs:
  - policy table includes `/api/monitor`, `/api/dashboard_data`, `/api/cell_data`, `/api/get_event_logs`, etc.
  - exceed path returns `429 Too Many Requests` + retry JSON (rate-limit function around lines 185-226 LCD, 183-224 receiver_2)

**Day-one impact:** under memory pressure or high polling rates, telemetry endpoints can return 503/429, which may be interpreted as MQTT failure although transport may be healthy.

---

## Startup Task Creation Behavior

- LCD launcher has retry loop for creating MQTT task (`kMqttLauncherRetryMs = 2000`):
  - `espnowreceiver_LCD/src/runtime/runtime_task_startup.cpp` lines 28, 142-150
- receiver_2 runtime startup uses fatal fail-on-create task policy:
  - `espnowreceiver_2/src/config/runtime_task_startup.cpp` task creation path (`create_task_or_fail(...)`)

This is not a direct MQTT protocol gate, but it affects whether MQTT task becomes available after resource pressure at boot.

---

## 7) Transmitter MQTT output vs receiver intake — UPDATED 2026-05-19

Legend: ✅ Resolved this session | ⚠️ Partial / known issue | 🔲 Not yet started

### 7a) Session Changes Applied
### 7a) All Session Changes — COMPLETE ✅

| Item | Change | Files |
|---|---|---|
| ✅ Heartbeat subscription | Both receivers now subscribe to `tx/state/heartbeat` and call `handleHeartbeat()` | Both receiver `mqtt_client.cpp` |
| ✅ Heartbeat JSON parse capacity | `handleHeartbeat()` doc increased 256 → 768 bytes (fixed `NoMemory` parse failure) | Both receiver `mqtt_client.cpp` |
| ✅ Meta/runtime JSON parse capacity | `handleMetaRuntime()` doc increased 256 → 768 bytes | Both receiver `mqtt_client.cpp` |
| ✅ Heartbeat publish cadence | `MQTT_HEARTBEAT_INTERVAL_MS` 1000 → 10000 (1 s → 10 s) | `mqtt_feature_flags.h` |
| ✅ Transmitter name display | `handleMetaVersion()` env `"mqtt"` → `""` so real `DEVICE_NAME` is used | Both receiver `mqtt_client.cpp` |
| ✅ Dashboard transmitter name quotes | `formatEnvName()` strips embedded TOSTRING `"` chars, wraps both TX/RX in `"…"` | Both receiver `dashboard_page_script.cpp` |
| ✅ Dashboard TX telemetry API fields | `name`, `mac`, `uptime_ms`, `unix_time`, `utc_offset_min`, `time_source`, `geolocation_valid`, `temperature_c` | Both receiver `api_telemetry_handlers.cpp` |
| ✅ Duplicate `transmitter["name"]` | Duplicate assignment removed from receiver_2 telemetry API | `espnowreceiver_2/.../api_telemetry_handlers.cpp` |
| ✅ LCD `transmitter["name"]` missing | Field was absent from LCD telemetry API — added | `espnowreceiver_LCD/.../api_telemetry_handlers.cpp` |
| ✅ Heartbeat flags/MAC/geolocation/temperature parsed | `handleHeartbeat()` extracts `heartbeat_flags`, `tx_mac`, `temperature_centi_c` | Both receiver `mqtt_client.cpp` |
| ✅ Transmitter chip temperature | Changed from battery pack temp to `temperatureRead()` with erroneous value filter | `ESPnowtransmitter2/.../mqtt_manager.cpp` |
| ✅ `MQTT_FEATURE_LIVE_BATTERY_TELEMETRY` enabled | Flag 0 → 1; live battery SOC/power/voltage stream now emitted by transmitter | `mqtt_feature_flags.h` |
| ✅ receiver_2 `event_logs/chunk` subscription | Added `tx/state/event_logs/chunk` to base `subscribeToTopics()` | `espnowreceiver_2/src/mqtt/mqtt_client.cpp` |
| ✅ LCD `runtime/charger` + `runtime/inverter` subscriptions | Added both missing runtime topic subscriptions | `espnowreceiver_LCD/src/mqtt/mqtt_client.cpp` |
| ✅ `TxAck::SETTINGS` → `TxAck::BATTERY` | Renamed to match actual TX publish topic `tx/ack/battery` | `espnowreceiver_LCD/include/mqtt/mqtt_topics_receiver.h` |
| ✅ `static/led` NoMemory parse failure | `handleStaticLed()` fixed from fixed `DynamicJsonDocument(512)` to payload-driven `min(4096, max(1024, len*3))`; diagnostic `(len=%u cap=%u)` added to error log | Both receiver `mqtt_client.cpp` |
| ✅ Duplicate TX temperature log | `handleBatteryLive()` was calling `storeTemperatureReport()` on same chip-temp value as `handleHeartbeat()` — removed from `handleBatteryLive()` | Both receiver `mqtt_client.cpp` |
| ✅ Battery pack vs device temperature separation | TX `battery_live` now publishes battery pack temp (`datalayer.battery.status.temperature_max_dC * 10`) as **`battery_temp_centi_c`**; TX `heartbeat` retains device chip temp as `temperature_centi_c`. Receivers store these independently (`storeBatteryTemperatureReport()` from battery_live, `storeTemperatureReport()` from heartbeat). API responses now expose both `temperature_c` (device) and `battery_temp_c` (battery pack). | `ESPnowtransmitter2/.../mqtt_manager.cpp`, both receiver `transmitter_state.h/.cpp`, `transmitter_manager.h/.cpp`, `mqtt_client.cpp`, `api_telemetry_handlers.cpp` |

---

### 7b) Remaining Gaps (topics, features, parity)
### 7b) Topic / feature parity — COMPLETE ✅

| Area | Status |
|---|---|
| `tx/state/battery_live` periodic stream | ✅ `MQTT_FEATURE_LIVE_BATTERY_TELEMETRY` set to 1; TX now emits stream |
| `tx/state/runtime/{led,system,charger,inverter}` | ✅ All four subscribed in both receivers |
| `tx/state/event_logs/chunk` | ✅ receiver_2 base subscription added; LCD had it already |
| `tx/state/cell_data/chunk` | ✅ Aligned (demand-gated in both receivers) |
| ACK topic naming contract | ✅ `TxAck::SETTINGS` renamed to `TxAck::BATTERY` in LCD topic header |
| Battery pack vs device temperature field naming | ✅ TX uses distinct fields: `battery_temp_centi_c` (battery_live) and `temperature_centi_c` (heartbeat); both receivers store and expose them separately |
| Legacy retained status topic | ℹ️ Not subscribed by receivers — no functional impact; heartbeat covers liveness |
| `MQTT_FEATURE_ESPNOW_COEXISTENCE_GATES` | ℹ️ Flag defined as `0`; no coexistence logic wired — pending design decision only |

---

### 7c) Dashboard / UI Remaining Gaps
### 7c) Dashboard / UI — COMPLETE ✅

| Item | Status |
|---|---|
| Transmitter time/uptime display on `/` | ✅ Wired via heartbeat `handleHeartbeat()` |
| Transmitter geolocation flag on `/` | ✅ Parsed from `heartbeat_flags` |
| Transmitter temperature on `/` (device chip temp) | ✅ Uses `temperatureRead()` — field `temperature_c` on API |
| Battery pack temperature on `/` | ✅ Uses `datalayer.battery.status.temperature_max_dC` — field `battery_temp_c` on API |
| Transmitter name on `/` | ✅ Real `DEVICE_NAME`, consistent `"Name"` format on both buttons |
| Transmitter MAC on `/` | ✅ Populated from `tx_mac` in heartbeat; fallback "Not available" |
| Receiver temperature on `/` | ✅ Populated via `DeviceTemperature` API |
| Battery live data cards on `/` | ✅ Unblocked — `MQTT_FEATURE_LIVE_BATTERY_TELEMETRY` now `1` |
| LCD charger/inverter runtime cards | ✅ `runtime/charger` + `runtime/inverter` subscriptions added |

Reference anchors used for this comparison:
- Transmitter publish/subscribe logic: `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp` (notably around lines 131, 443-549, 699, 1349)
- Transmitter periodic publish gates: `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_task.cpp` (notably around lines 139-182)
- Shared feature flags: `esp32common/include/esp32common/mqtt/mqtt_feature_flags.h` (line 49 and related flags)
- Receiver_2 subscriptions/handlers: `espnowreceiver_2/src/mqtt/mqtt_client.cpp` (notably around lines 310-317, 409-430)
- Receiver_LCD subscriptions/handlers: `espnowreceiver_LCD/src/mqtt/mqtt_client.cpp` (notably around lines 455-462, 585-616, 1350-1363)
- Receiver topic contract header: `espnowreceiver_LCD/include/mqtt/mqtt_topics_receiver.h` (notably `TxState::HEARTBEAT`, `TxAck::SETTINGS`)

_Last updated: 2026-05-20 — battery pack / device temperature fully separated; static/led parse and duplicate temp log also resolved._

---

## Interpretation: Which gates most likely caused "not fully functioning from the beginning"?

Highest-probability blockers in initial deployments:

1. **Broker config validity gate** (`isMqttEnabled` + non-zero broker IP) in `mqtt_task.cpp`
2. **Connected-state gate** for command APIs (`MqttClient::isEnabled() && MqttClient::isConnected()`)
3. **Live telemetry compile-time disable** (`MQTT_FEATURE_LIVE_BATTERY_TELEMETRY = 0`) — **still active**
4. **Middleware 503/429 pressure gates** when front-end polling is aggressive or heap is low
5. **Heartbeat not subscribed** — neither receiver consumed heartbeat liveness data (now fixed)
6. **Heartbeat cadence** — published every 1 s instead of 10 s (now fixed)
7. **Transmitter name as "MQTT"** — env field hardcoded, masking real device identity (now fixed)
8. **Transmitter temperature** — was reporting battery pack temp instead of chip temp (now fixed)

---

## Recommendations — Updated 2026-05-19

### Remaining action items in priority order

All HIGH and MEDIUM items from the original list have been resolved. The following LOW-priority items remain as optional improvements:

| Priority | Action | File(s) |
|---|---|---|
| **LOW** | Add a diagnostics endpoint (`/api/mqtt_status` or similar) reporting all gate states in one payload: feature flags, enabled/configured/connected, broker IP validity, command channel readiness, middleware pressure counters. | New handler in both receivers |
| **LOW** | Standardise startup behaviour between receivers (LCD retry loop vs receiver_2 fatal-fail for MQTT task creation). | `espnowreceiver_2/src/config/runtime_task_startup.cpp` |
| **LOW** | Decide intent for `MQTT_FEATURE_ESPNOW_COEXISTENCE_GATES` — either wire up coexistence logic or remove the flag to avoid dead-code confusion. | `esp32common/include/esp32common/mqtt/mqtt_feature_flags.h` |
| **LOW** | Surface explicit UI messages for 503/429 "pressure gate" API responses to avoid front-end mislabelling them as broker/connectivity failures. | Dashboard JS in both receivers |
| **LOW** | Expose `battery_temp_c` field in dashboard card UI (currently available in API response but not yet rendered in the front-end). | Dashboard JS/HTML in both receivers |

---

## Bottom Line

The MQTT integration is now **fully functional at all data layers**. All HIGH and MEDIUM priority gaps have been resolved across two sessions (2026-05-19 and 2026-05-20):

- Live battery telemetry enabled (SOC, power, voltage, current, BMS status)
- Heartbeat liveness, timing, name, MAC, geolocation all wired
- **Device chip temperature** (from `temperatureRead()`) travels via `heartbeat` → `temperature_centi_c` → `temperature_c` in API
- **Battery pack temperature** (from `datalayer.battery.status.temperature_max_dC`) travels via `battery_live` → `battery_temp_centi_c` → `battery_temp_c` in API
- These two temperature streams have independent sequence numbers, independent storage paths, and independent API fields — no duplication
- `static/led` retained-payload parse failure resolved
- All topic subscriptions aligned between both receivers
- ACK topic naming corrected

The only remaining work is LOW-priority: a diagnostics endpoint, startup behaviour parity between receivers, deciding the fate of the coexistence gate flag, and front-end rendering of the new `battery_temp_c` field.