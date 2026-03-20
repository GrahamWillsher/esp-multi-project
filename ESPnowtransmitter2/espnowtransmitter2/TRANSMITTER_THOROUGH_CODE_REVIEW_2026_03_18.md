# ESP-NOW Transmitter Thorough Code Review (March 18, 2026)

## Scope
This review focused on the **new transmitter architecture areas** you highlighted:
- ESP-NOW connection/discovery/state flow
- Ethernet state machine and service gating
- migrated web/OTA APIs
- transmission/cache/messaging path
- maintainability, readability, and long-term performance

Primary files reviewed include:
- [src/main.cpp](src/main.cpp)
- [src/espnow/discovery_task.cpp](src/espnow/discovery_task.cpp)
- [src/espnow/tx_connection_handler.cpp](src/espnow/tx_connection_handler.cpp)
- [src/espnow/tx_send_guard.cpp](src/espnow/tx_send_guard.cpp)
- [src/espnow/enhanced_cache.cpp](src/espnow/enhanced_cache.cpp)
- [src/espnow/message_handler.cpp](src/espnow/message_handler.cpp)
- [src/espnow/version_beacon_manager.cpp](src/espnow/version_beacon_manager.cpp)
- [src/network/ethernet_manager.cpp](src/network/ethernet_manager.cpp)
- [src/network/mqtt_manager.cpp](src/network/mqtt_manager.cpp)
- [src/network/mqtt_task.cpp](src/network/mqtt_task.cpp)
- [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- [src/network/transmission_selector.cpp](src/network/transmission_selector.cpp)

---

## Executive Summary
Overall, the codebase shows strong architectural intent (state machines, queue isolation, background tasks, cache-first TX pipeline). However, several implementation choices are currently undermining reliability and maintainability:

1. **Lifecycle ownership is fragmented** (duplicate initialization and split service startup paths).
2. **Some state machine logic has correctness risks** (recovery gating, recursive restart, blocking `delay()`).
3. **Web/OTA endpoints are operationally useful but need hardening** (auth, input handling, heap-safe JSON construction).
4. **Several modules are semantically misleading** (functions that report sends but do not actually send).
5. **There are quick wins available immediately** with low risk and high maintainability impact.

---

## Top Priority Findings (Action First)

### P0-1: Duplicate heartbeat initialization
- Evidence: [src/main.cpp](src/main.cpp#L344) and [src/main.cpp](src/main.cpp#L428)
- Risk: double init side effects, duplicate timers/state resets, non-deterministic behavior if init becomes non-idempotent.
- Recommendation: keep one initialization path only; enforce idempotent `init()` with guard and explicit return status.

### P0-2: Recursive restart with blocking backoff in discovery recovery
- Evidence: [src/espnow/discovery_task.cpp](src/espnow/discovery_task.cpp#L77), [src/espnow/discovery_task.cpp](src/espnow/discovery_task.cpp#L79)
- Risk: stack growth risk under repeated failures, long blocking sections, reduced responsiveness.
- Recommendation (rewrite): convert `restart()` into a non-recursive state-driven retry loop using scheduled deadlines (`next_retry_ms`) and `tick()` processing.

### P0-3: Ethernet recovery transition gate uses cumulative metric as one-shot flag
- Evidence: increment in handler [src/network/ethernet_manager.cpp](src/network/ethernet_manager.cpp#L153), transition guard [src/network/ethernet_manager.cpp](src/network/ethernet_manager.cpp#L227), increment again [src/network/ethernet_manager.cpp](src/network/ethernet_manager.cpp#L230)
- Risk: after first recovery, later LINK_LOST events may not transition to RECOVERING as intended.
- Recommendation (rewrite): separate `recovery_attempted_this_cycle` from lifetime metric counter.

### P0-4: OTA upload endpoint appears unauthenticated
- Evidence: upload route registration [src/network/ota_manager.cpp](src/network/ota_manager.cpp#L345) with POST [src/network/ota_manager.cpp](src/network/ota_manager.cpp#L346)
- Risk: anyone on reachable network may attempt firmware upload.
- Recommendation (rewrite): require auth token/HMAC + optional allowlist + one-time nonce + rate limit. Reject unauthorized requests before reading body.

#### P0-4 Implementation Design (works with current 2-device setup)
For your current architecture (receiver control/display + transmitter control device), the most practical model is **receiver-authorized short-lived OTA session tokens**:

1. **Shared secret in both devices (PSK)**
  - Store one provisioned secret in receiver and transmitter NVS (or compile-time + rotation support).
  - Do not expose secret over ESP-NOW or HTTP.

2. **OTA arm step from receiver over ESP-NOW**
  - Receiver sends `OTA_ARM_REQUEST` to transmitter (already trusted control plane).
  - Transmitter validates receiver identity (`EspNowConnectionManager` peer MAC + connected state).
  - Transmitter generates:
    - `nonce` (128-bit random),
    - `session_id`,
    - `expires_at` (e.g., now + 120s),
    - `attempt_limit` (e.g., 3).

3. **Transmitter derives session token**
  - Token formula example:
  - $token = HMAC\_SHA256(PSK, session\_id \| nonce \| expires\_at \| transmitter\_mac)$
  - Return only `session_id`, `nonce`, `expires_at` to receiver (not PSK).

4. **HTTP upload requires OTA auth headers**
  - Client (receiver UI or operator tool) sends:
    - `X-OTA-Session: <session_id>`
    - `X-OTA-Nonce: <nonce>`
    - `X-OTA-Expires: <expires_at>`
    - `X-OTA-Signature: <hmac>`
  - Transmitter recomputes and verifies signature **before reading body**.

5. **Single-use + expiry + lockout**
  - Mark session consumed on first successful validation.
  - Reject reused/expired session.
  - Decrement attempts on failed signature and lock session when attempts exhausted.

6. **Optional hardening**
  - Source-IP allowlist for upload endpoint.
  - Rate limiting per source.
  - Require explicit `OTA_ARM_REQUEST` each update cycle.

Why this fits your current system:
- no cloud PKI requirement,
- uses existing trusted ESP-NOW control relationship,
- keeps HTTP OTA possible without adding full TLS stack complexity,
- minimizes permanent credentials exposure.

Suggested shared/common placement:
- move HMAC/session primitives into `esp32common` (e.g., `webserver_common_utils`),
- transmitter and receiver use the same verifier/signer helpers.

---

## High-Impact Structural Findings

### H1: `setup()` is overloaded and mixes orchestration with business logic
- Evidence: [src/main.cpp](src/main.cpp) — approximately 400 lines, lines 86–430.
- Confirmed symptoms:
  - Very long monolithic function covering hardware init, WiFi, Ethernet, CAN/battery, ESP-NOW queues, connection manager, state machines, message handler, settings, MQTT config, OTA, tasks, heartbeat, test data, data sender, discovery, NTP, version beacon — all in sequence with no phase structure.
  - **Boot-time Ethernet gate is a functional bug** (see H2): MQTT, OTA HTTP server, and `StaticData::update_inverter_specs()` are inside a single `if (EthernetManager::instance().is_connected())` block at setup time. If Ethernet is not up when `setup()` runs these services are **permanently disabled until reboot** — there is no deferred retry or post-boot activation.
  - **Battery emulator ordering is actually correct** but fragile and undocumented:
    - `init_stored_settings()` → `CANDriver::init()` → `BatteryManager::init_primary_battery()` → `setup_battery()` (sets global `battery*` pointer) → `StaticData::init()` → `TestDataGenerator::update()` → `DataSender::start()`.
    - `DataSender` reads `datalayer.battery.status.*` which is populated via `BatteryManager::update_transmitters(millis())` in `loop()` — this is correct but the ordering dependency is implicit and nowhere documented. Any reordering risks the test data generator initialising before `number_of_cells` is set (there is already a manual patch at line ~306 to work around this).
  - Error handling is log-and-continue throughout: failed CAN init, failed queue manager, failed connection manager — none abort, degrade gracefully, or set a boot-failed flag.
  - FreeRTOS tasks (DataSender, DiscoveryTask, TransmissionTask, MQTT) are spawned once with `xTaskCreate` and never monitored. A silent task crash results in permanent data-flow loss with no recovery.
- **Recommendation (structural rewrite):** introduce a `BootstrapOrchestrator` with explicit typed phases:
  1. hardware (serial, HAL, GPIO)
  2. persistence/config (NVS load, settings, MQTT config)
  3. battery subsystem (CAN driver, BatteryManager, datalayer pre-populate)
  4. connectivity primitives (WiFi, Ethernet, ESP-NOW queues)
  5. services (OTA, MQTT, NTP, TimeManager, VersionBeacon)
  6. tasks (DataSender, Discovery, Transmission, MQTT task)
  7. post-start health checks

  Each phase returns typed status; prerequisites block later phases. Services in phase 5 must be restartable via an event from the Ethernet state machine (fixes the H2 boot-gate bug).

### H2: Ethernet service callbacks are empty shells — active functional bug
- Evidence:
  - `on_connected` lambda registered at [src/main.cpp](src/main.cpp#L140) — **body is empty** (logs only, no code).
  - `on_disconnected` lambda registered at [src/main.cpp](src/main.cpp#L146) — **body is empty** (logs only, no code).
  - MQTT and OTA are started at [src/main.cpp](src/main.cpp#L319) inside `if (EthernetManager::instance().is_connected())` at boot time only.
- **Active bug confirmed:**
  - If Ethernet is not up when `setup()` reaches line ~319, neither MQTT nor OTA HTTP server are ever started. The empty `on_connected` callback never starts them later.
  - If Ethernet drops after boot and reconnects, `on_disconnected` never shuts MQTT down cleanly; `on_connected` never re-arms the OTA server.
  - The `MqttManager` MQTT task does have an internal reconnect loop to the broker — so MQTT reconnects to the broker OK — but only if the task was created at boot. If it was never created (Ethernet absent at boot), no reconnect ever happens.
  - `StaticData::update_inverter_specs()` is also only called in the `is_connected()` block, so inverter spec data is absent from the data layer on Ethernet-absent boots.
- **Battery emulator impact:** The CAN → datalayer → DataSender → ESP-NOW data path is fully independent of Ethernet and is unaffected. Battery data flows regardless. However MQTT telemetry (which publishes battery state to the broker) is lost entirely if Ethernet is absent at boot.
- **Recommendation:** Fill the `on_connected` / `on_disconnected` callbacks with real logic:
  - `on_connected`: start (or restart) OTA HTTP server, start (or reconnect) MqttManager.
  - `on_disconnected`: stop OTA HTTP server, disconnect MqttManager cleanly.
  - Remove the inline `is_connected()` service-start block from `setup()` — delegate entirely to the callback.
  - Long-term: a centralized `ServiceSupervisor` owns all service lifecycle transitions driven by the Ethernet state machine events.

### H3: Global/shared mutable state — lower risk than originally assessed
- Evidence:
  - **Queue globals** in [src/main.cpp](src/main.cpp#L82): `espnow_message_queue`, `espnow_discovery_queue`, `espnow_rx_queue`.
    - These are **ISR-mandated**: the `esp32common/espnow_transmitter` library's send-callback ISR needs them as `extern` globals. `EspnowQueueManager` is the real owner; the `main.cpp` globals are just re-exported ISR-accessible aliases populated after queue creation. This is a library-level hardware constraint.
  - **`g_lock_channel`, `g_ack_seq`, `g_ack_received`** are declared in [esp32common/espnow_transmitter/espnow_transmitter.h](../../esp32common/espnow_transmitter/espnow_transmitter.h#L14) as `volatile` externs and defined in the library `.cpp`. They are written by the ESP-NOW ISR callback and read from tasks.
    - `g_lock_channel` (uint8_t) is read directly in `discovery_task.cpp` (4 sites) and `tx_send_guard.cpp` (2 sites). Being uint8_t it is atomically readable on ESP32.
    - Changing these requires modifying the shared library, not just the transmitter.
- **Revised risk:** The coupling is real — `discovery_task` and `tx_send_guard` directly dereference library-internal volatile globals. But the race risk is lower than originally stated because the values are ISR-written/task-read (unidirectional for lock_channel and ack_received), and the types are uint8_t/bool (atomic on this target).
- **Actual risk is readability / future portability**, not a live data race today.
- **Recommendation (lower priority):** If the common library is ever refactored, expose `g_lock_channel` and related flags through a `ChannelManager` accessor rather than raw externs. For now, add a comment on each usage site explaining the ISR-sourced nature of the value. No immediate action required.

### H4: Dynamic singleton allocation in Ethernet manager
- Evidence: [src/network/ethernet_manager.cpp](src/network/ethernet_manager.cpp#L14) `static EthernetManager* g_ethernet_manager_instance = nullptr`, [src/network/ethernet_manager.cpp](src/network/ethernet_manager.cpp#L18) `g_ethernet_manager_instance = new EthernetManager()`.
- **Confirmed.** Unnecessary heap allocation for a permanent singleton. The `EthernetManager` has a destructor but the heap pointer is never freed. If heap is fragmented at the call site, the `new` could fail silently (returns null on OOM) and the next `instance()` access would dereference null.
- **Battery emulator impact: none.** The CAN → datalayer → DataSender pipeline is fully independent of EthernetManager. The risk is only to MQTT/OTA/NTP connectivity.
- **Fix is trivial and low risk:** replace the raw pointer + null-check pattern with a function-local static:
  ```cpp
  EthernetManager& EthernetManager::instance() {
      static EthernetManager instance;
      return instance;
  }
  ```
  Remove `g_ethernet_manager_instance`, the null-check, and the `new` call. The constructor's `LOG_DEBUG` call is safe at static init time.
- **Execution priority:** Highest of H1–H4 because it is the simplest change (3 lines) with the lowest risk and a small OOM-safety improvement.

---

## Code Quality & Correctness Findings

### C1: Unsafe API shape in `EnhancedCache::peek_next_transient()` pointer variant
- Evidence: declaration/implementation [src/espnow/enhanced_cache.cpp](src/espnow/enhanced_cache.cpp#L92), return [src/espnow/enhanced_cache.cpp](src/espnow/enhanced_cache.cpp#L109)
- Issue: returns internal pointer after mutex released.
- Risk: stale pointer / concurrent mutation risk.
- Recommendation: remove pointer-return API; keep only copy-out version (already present) and make it the single interface.

### C2: Dead code path for IP push helper
- Evidence: only definition found [src/espnow/message_handler.cpp](src/espnow/message_handler.cpp#L123)
- Risk: maintenance noise and confusion.
- Recommendation: either wire it to a clear trigger or delete.

### C3: Duplicate state-machine init ownership
- Evidence: [src/espnow/message_handler.cpp](src/espnow/message_handler.cpp#L38), [src/main.cpp](src/main.cpp#L225)
- Risk: hidden side effects during singleton construction and init order fragility.
- Recommendation: avoid side effects in constructors; perform init only in one explicit lifecycle point.

### C4: Mixed logging channels and direct serial debug bypass
- Evidence: direct `Serial.println` in MQTT task [src/network/mqtt_task.cpp](src/network/mqtt_task.cpp#L142), [src/network/mqtt_task.cpp](src/network/mqtt_task.cpp#L148), [src/network/mqtt_task.cpp](src/network/mqtt_task.cpp#L150)
- Risk: inconsistent log policy, noisy runtime, harder filtering.
- Recommendation: route all logs through one logger macro system with compile/runtime levels.

### C5: Time-based logging using modulo on elapsed time
- Evidence: [src/network/ethernet_manager.cpp](src/network/ethernet_manager.cpp#L265)
- Risk: brittle periodic behavior dependent on update cadence/timing jitter.
- Recommendation: use `last_log_ms` delta comparison.

### C6: Comment/behavior mismatch in version beacon cadence
- Evidence: comment says 15s [src/espnow/version_beacon_manager.cpp](src/espnow/version_beacon_manager.cpp#L55), interval constant is 30s in header [src/espnow/version_beacon_manager.h](src/espnow/version_beacon_manager.h#L84)
- Risk: operator confusion and wrong expectations during diagnostics.
- Recommendation: keep comments and constants synchronized; prefer one source of truth docstring in header.

### C7: MQTT connection logic duplicated across two APIs
- Evidence: state-machine path [src/network/mqtt_manager.cpp](src/network/mqtt_manager.cpp#L130) and legacy direct connect [src/network/mqtt_manager.cpp](src/network/mqtt_manager.cpp#L251)
- Risk: behavior drift, duplicated bug fixes.
- Recommendation (rewrite): keep only one connection path (`attempt_connection()`), make `connect()` a thin wrapper or remove it.

### C8: Transmission selector semantics are misleading
- Evidence: comments/flags imply send while often not sending actual payload, e.g. "Assuming handler will send it" [src/network/transmission_selector.cpp](src/network/transmission_selector.cpp#L177)
- Risk: false-positive telemetry statistics and difficult operational debugging.
- Recommendation (rewrite): either perform real send in selector or rename methods to `plan_route_*` and separate execution from planning.

### C9: Event log and status HTTP responses built with repeated `String` concatenation
- Evidence: [src/network/ota_manager.cpp](src/network/ota_manager.cpp#L139), [src/network/ota_manager.cpp](src/network/ota_manager.cpp#L216)
- Risk: heap fragmentation on long-running embedded systems.
- Recommendation: use `StaticJsonDocument` + stream response or bounded char buffers with measured writes.

---

## Security and Robustness Improvements

### S1: Add OTA auth + replay protection (mandatory)
- Add token-based auth header (or HMAC over body metadata).
- Require nonce + timestamp window.
- Log source IP and throttle failed attempts.

### S2: Validate HTTP payload lengths uniformly
- For config endpoints, reject oversized payloads before read/parse.
- Standardize error response schema (`code`, `message`, `details`).

### S3: Add explicit service health model
- Expose health states for ETH, MQTT, ESP-NOW, OTA via one `/api/health` endpoint.
- Include state age and last error reason to support field troubleshooting.

---

## Proposed Rewrite Targets (Concrete)

## 1) Rewrite `DiscoveryTask::restart()` as non-recursive state flow
Current issue: recursion + `delay()` blocking.  
Target: stateful retry with no recursion:
- `restart_requested`
- `restart_in_progress`
- `next_retry_ms`
- `retry_count`

Benefits: deterministic, testable, no stack growth risk, better watchdog friendliness.

## 2) Rewrite Ethernet recovery as explicit per-cycle recovery context
Current issue: lifetime metrics mixed with control flags.  
Target context:
- `recovery_active`
- `recovery_attempts_this_outage`
- `outage_started_ms`

Benefits: correctness across repeated unplug/replug cycles.

## 3) Rewrite `OtaManager` response generation to fixed-allocation JSON
Current issue: `String` accumulation and many ad-hoc responses.  
Target:
- helper `send_json_ok()` / `send_json_error()`
- `ArduinoJson` with fixed documents
- centralized auth check helper for all protected routes

Benefits: lower fragmentation, consistent API, easier security hardening.

## 4) Rewrite startup into phase-based bootstrap class
Current issue: long monolithic `setup()`.  
Target:
- `BootstrapPhase` enum + ordered execution table
- dependency checks + fail-fast / degrade rules
- startup telemetry summary

Benefits: easier debugging, cleaner ownership, safer changes.

---

## Suggested Structural Refactor Plan (Phased)

### Execution Policy (applies to all phases)
- As each step is completed, **remove old/redundant code immediately** (no parallel legacy path left behind unless explicitly marked temporary).
- Keep a strict “single active implementation” rule to prevent regression drift.
- Update this document at completion of each step with:
  - status (`done/in-progress/not-started`),
  - files touched,
  - residual debt intentionally deferred.

Recommended tracking table to add/maintain in this file during implementation:

| Item | Status | Legacy Code Removed | Notes |
|---|---|---|---|
| P0-1 heartbeat init dedupe | done | yes | Removed duplicate init in setup path |
| P0-2 discovery restart rewrite | done | yes | Replaced recursive retry with scheduled state-driven retries |
| P0-3 ethernet recovery rewrite | done | yes | Added `recovery_active_this_outage_` bool; removed lifetime-counter gate and spurious increment in ETH_CONNECTED |
| P0-4 OTA auth/session flow | done | yes | Added short-lived signed OTA sessions with one-time consumption, expiry, and attempt lockout prior to body read |
| C2 dead helper removal (`send_ip_to_receiver`) | done | yes | Unused function removed |
| C3 duplicate state-machine init ownership | done | yes | Removed stale no-op `EspnowMessageHandler::init()` lifecycle hook; `TransmitterConnectionHandler` remains the single owner of Tx state transitions. |
| C4 serial debug bypass cleanup | done | yes | `Serial.println` debug lines removed from MQTT task |
| C1 unsafe cache pointer API removal | done | yes | Pointer-return `peek_next_transient()` removed |
| C5 modulo log → delta timing | done | yes | `age % interval == 0` replaced with `millis() - last_ip_wait_log_ms_` in IP_ACQUIRING |
| C6 version beacon comment fix | done | yes | Comment updated from "15 seconds" to "30 seconds" to match constant |
| C7 MQTT connect path deduplication | done | yes | Legacy `connect()` now forwards to `attempt_connection()`; duplicated connect/subscribe path removed |
| C8 TransmissionSelector semantics cleanup | done | yes | Selector now explicitly reports route planning outcomes (not payload send success); misleading "sent" logs removed |
| C9 OTA/status String concat cleanup | done | yes | Replaced String-heavy JSON responses with bounded snprintf/chunked JSON emission in OTA HTTP handlers |
| CC3 Ethernet snake/camel API cleanup | done | yes | Removed camelCase wrappers, standardized on snake_case API, migrated all transmitter callsites |
| CC1/CC2 common preflight+peer helper | done | yes | Shared helper added and both config handlers migrated |
| CC6 router registration helper cleanup | done | yes | Added a shared route-registration helper in `setup_message_routes()` and removed repeated `register_route(..., 0xFF, this)` boilerplate without changing handler behavior. |
| CC5 bootstrap phase-runner extraction | done | yes | Extracted bootstrap phase runner and then moved the canonical implementation to shared `esp32common/runtime_common_utils` for transmitter/receiver reuse while preserving exact phase execution order/behavior. |
| CC4 shared OTA auth/session primitives | done | yes | Extracted reusable OTA auth and session lifecycle primitives into `esp32common/webserver_common_utils` (`ota_auth_utils` + `ota_session_utils`) and migrated transmitter `OtaManager` to the shared canonical implementation. |
| Normalize HTTP error payloads | done | yes | `send_json_error()` helper added; all `httpd_resp_send_err` calls in OTA/test-data handlers replaced |
| **H4** EthernetManager heap alloc | done | yes | Replaced `new`-backed singleton with function-local static in `EthernetManager::instance()`; removed global heap pointer. |
| **H2** Ethernet empty callbacks / service boot-gate bug | done | yes | Filled callback bodies with real OTA/MQTT lifecycle actions; removed boot-time `is_connected()` service gate and switched to callback-driven ownership. |
| **H1** `setup()` bootstrap orchestrator | **done** | yes | Extracted 8 static `bootstrap_*()` phase functions from monolithic `setup()`. Battery ordering contract made explicit in comments. `setup()` body reduced to 12 lines. |
| **H3** ISR-global coupling comments | **done** | yes | Added explanatory comments at each `g_lock_channel` usage site documenting ISR-written nature and atomic readability. |
| Phase D runtime context object (foundation) | **done** | yes | Added `RuntimeContext` module for ESP-NOW queue handles; moved queue globals out of `main.cpp`; kept ISR compatibility exports synchronized. |
| OTA cross-device session compatibility hardening (Mar 20) | done | yes | Prevented duplicate control-plane OTA arm from rotating an active unconsumed session, avoiding receiver/transmitter challenge mismatch mid-flow. |
| OTA HTTP task stack-stability hardening (Mar 20) | done | yes | Fixed transmitter `httpd` stack canary panic during OTA by reducing OTA-path stack pressure (heap upload buffer + lighter auth-success logging) and increasing HTTP server task stack size. |
| Receiver OTA proxy forwarder robustness (Mar 20) | done | yes | Added partial-write aware forwarding loop and early-transmitter-error capture so receiver returns exact upstream rejection instead of generic chunk-forward failure. |

### Phase A (1–2 days, low risk)
- Remove duplicate init calls and dead helpers.
- Remove unsafe pointer-return cache API.
- Remove direct serial debug prints in tasks.
- Align comments/constants.

### Phase B (3–5 days, medium risk)
- Refactor discovery restart logic to iterative state machine.
- Refactor Ethernet recovery gating variables.
- Consolidate MQTT connect path.

### Phase C (4–7 days, medium/high value)
- OTA auth + unified JSON response helpers.
- `String`-heavy HTTP handlers migrated to fixed allocation.
- Add `/api/health` and runtime diagnostics endpoint.

### Phase D (1–2 weeks, highest maintainability payoff)
- Introduce bootstrap orchestrator.
- Introduce runtime context object replacing global cross-module state.
- Move all service lifecycle transitions to one supervisor owner.

---

## Common Code Consolidation Investigation (Transmitter + Shared Library)

This review also checked where code can be shifted to shared/common modules (including minor signature adjustments for flexibility).

### CC1: Config ACK pipeline is duplicated and can be generalized
- Evidence:
  - network ACK path [src/espnow/network_config_handlers.cpp](src/espnow/network_config_handlers.cpp#L85)
  - MQTT ACK path [src/espnow/mqtt_config_handlers.cpp](src/espnow/mqtt_config_handlers.cpp#L106)
- Observation: both handlers duplicate peer-check, packet fill, message copy, send, and logging patterns.
- Improvement:
  - create common helper in `esp32common` (e.g., `config_ack_sender.h`) with generic API:
    - `build_and_send_ack<T>(receiver_mac, fill_fn, tag)`
  - allow module-specific payload fill lambda (`fill_fn`) while sharing send/peer/error plumbing.

### CC2: Request/update validation flow is duplicated across config handlers
- Evidence:
  - state/size checks and CONNECTED guard in [src/espnow/network_config_handlers.cpp](src/espnow/network_config_handlers.cpp#L13) and [src/espnow/mqtt_config_handlers.cpp](src/espnow/mqtt_config_handlers.cpp#L15)
- Improvement:
  - add common preflight utility in `esp32common/espnow`:
    - validate packet size,
    - validate connection state,
    - copy sender MAC safely,
    - standardize early-failure logging.

### CC3: Transitional API duplication in Ethernet manager should be collapsed
- Evidence:
  - camelCase compatibility wrappers [src/network/ethernet_manager.h](src/network/ethernet_manager.h#L192)
  - duplicated snake/camel implementation bridging [src/network/ethernet_manager.cpp](src/network/ethernet_manager.cpp#L356), [src/network/ethernet_manager.cpp](src/network/ethernet_manager.cpp#L525)
- Improvement:
  - keep one canonical API style (snake_case),
  - keep adapters only temporarily,
  - mark adapters deprecated and remove after call sites are migrated.

### CC4: OTA/auth/session logic belongs in shared common utilities
- Evidence: web handlers and OTA flow in [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Status: implemented in this review stream (shared `ota_auth_utils` + `ota_session_utils` in `esp32common/webserver_common_utils`; transmitter `OtaManager` migrated).
- Improvement:
  - create shared auth/session module in `esp32common/webserver_common_utils`:
    - `create_session()`,
    - `verify_session_signature()`,
    - `consume_session_once()`.
  - receiver and transmitter both use same canonical implementation.

### CC5: Startup orchestration pattern should be shareable across transmitter/receiver
- Evidence: transmitter monolith [src/main.cpp](src/main.cpp)
- Status: implemented in this review stream (phase runner extracted and canonical implementation moved to shared `esp32common/runtime_common_utils`).
- Improvement:
  - extract reusable startup framework (phase runner + dependency checks) into shared component,
  - pass board-specific hooks/lambdas for device differences.

### CC6: Keep existing common router approach and extend it further
- Evidence:
  - shared router/handlers already used in message handler [src/espnow/message_handler.cpp](src/espnow/message_handler.cpp#L23)
- Status: implemented in this review stream for transmitter route registration boilerplate reduction.
- Improvement:
  - continue migrating device-specific repeated route boilerplate into common helper registration functions.
  - allow optional callbacks for role-specific behavior (TX vs RX).

### Commonization principle to apply during refactor
When a function is “almost reusable”, prefer a **small signature adjustment** over cloning:
- add strategy callback/lambda,
- pass context struct,
- keep role-specific policy outside shared transport/plumbing.

This keeps common code cohesive while preserving transmitter/receiver specialization where required.

---

## Quick Wins Checklist

- [x] Remove duplicate `HeartbeatManager::init()` call.
- [x] Remove `send_ip_to_receiver()` or wire to explicit event.
- [x] Replace recursive `restart()` with iterative retry scheduling.
- [x] Replace modulo-based periodic log condition with delta timing.
- [x] Remove/replace `TransientEntry* peek_next_transient()`.
- [x] Remove direct `Serial.println` debug lines from MQTT task.
- [x] Add auth guard to `/ota_upload` before request body handling.
- [x] Normalize all HTTP error payloads.
- [x] Remove legacy wrappers after API callsite migration (snake/camel duplicate cleanup).
- [x] Move duplicated config ACK and preflight plumbing into shared common helpers.
- [x] Update this document after every completed refactor step and mark legacy removal status.

### Implementation Update (March 18, 2026)
Completed in this step:
- Removed duplicate heartbeat init in [src/main.cpp](src/main.cpp#L424).
- Removed dead `send_ip_to_receiver()` helper from [src/espnow/message_handler.cpp](src/espnow/message_handler.cpp).
- Removed direct serial debug prints from [src/network/mqtt_task.cpp](src/network/mqtt_task.cpp#L142).
- Removed unsafe pointer-return cache API from [src/espnow/enhanced_cache.h](src/espnow/enhanced_cache.h) and [src/espnow/enhanced_cache.cpp](src/espnow/enhanced_cache.cpp).
- Added shared config preflight/peer helper:
  - [src/espnow/config_handler_common.h](src/espnow/config_handler_common.h)
  - [src/espnow/config_handler_common.cpp](src/espnow/config_handler_common.cpp)
- Migrated both config handlers to the shared helper:
  - [src/espnow/network_config_handlers.cpp](src/espnow/network_config_handlers.cpp)
  - [src/espnow/mqtt_config_handlers.cpp](src/espnow/mqtt_config_handlers.cpp)

Completed in subsequent step:
- Rewrote discovery restart flow to non-recursive, state-driven retry scheduling:
  - [src/espnow/discovery_task.h](src/espnow/discovery_task.h)
  - [src/espnow/discovery_task.cpp](src/espnow/discovery_task.cpp)
- Removed blocking recursive `restart()` self-calls and replaced with:
  - explicit restart request state,
  - scheduled retry deadlines,
  - bounded attempt escalation to `PERSISTENT_FAILURE`.
- Build validation passed after refactor.

Completed in subsequent step:
- Consolidated MQTT connection flow to a single state-machine path:
  - [src/network/mqtt_manager.h](src/network/mqtt_manager.h)
  - [src/network/mqtt_manager.cpp](src/network/mqtt_manager.cpp)
- Legacy `connect()` compatibility API now forwards to `attempt_connection()`.
- Removed duplicated direct `client_.connect()`/publish/subscribe block from legacy path.
- Added Ethernet readiness guard to `attempt_connection()` for safer external wrapper calls.
- Build validation passed after refactor.

Completed in subsequent step:
- Clarified TransmissionSelector semantics to route planning/advisory behavior:
  - [src/network/transmission_selector.h](src/network/transmission_selector.h)
  - [src/network/transmission_selector.cpp](src/network/transmission_selector.cpp)
- Updated route-result method labels/logs to explicit route outcomes (`ESP-NOW_ROUTE`, `MQTT_ROUTE`, `BOTH_ROUTE`).
- Removed misleading cell publish log that referenced selector state as if it were executed transmission:
  - [src/network/mqtt_task.cpp](src/network/mqtt_task.cpp)
- Updated dynamic-data log wording to "route selected":
  - [src/espnow/data_sender.cpp](src/espnow/data_sender.cpp)
- Build validation passed after refactor.

Completed in subsequent step:
- Reduced heap-fragmentation risk in OTA HTTP JSON responses by removing String concatenation hot paths:
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- `event_logs_handler` now streams JSON via chunked response and per-event `StaticJsonDocument` serialization.
- `ota_status_handler` now uses bounded `snprintf` JSON construction with quote sanitization.
- test-data config/apply/reset handlers now use fixed buffers or constant response strings.
- Build validation passed after refactor.

Completed in subsequent step:
- Implemented authenticated OTA session flow with pre-body verification for `/ota_upload`:
  - [src/network/ota_manager.h](src/network/ota_manager.h)
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Added short-lived one-time OTA session model:
  - random `session_id` + `nonce`,
  - TTL-based expiry,
  - signature attempt limit/lockout,
  - one-time consumption on successful auth validation.
- Added HMAC-SHA256 signature verification using `X-OTA-*` headers before reading upload body.
- Added explicit OTA session arm endpoint (`/api/ota_arm`) and exposed session status/challenge fields in `/api/ota_status`.
- Wired ESP-NOW `OTA_START` control message to arm OTA session automatically:
  - [src/espnow/control_handlers.cpp](src/espnow/control_handlers.cpp)
- Build validation passed after refactor.

Completed in subsequent step:
- Collapsed transitional Ethernet API duplication (snake/camel) to canonical snake_case:
  - [src/network/ethernet_manager.h](src/network/ethernet_manager.h)
  - [src/network/ethernet_manager.cpp](src/network/ethernet_manager.cpp)
- Removed camelCase compatibility wrappers and migrated remaining transmitter callsites to snake_case.
- Build validation passed after refactor.

Completed in subsequent step:
- Normalized transmitter HTTP error payloads to JSON across OTA/test-data handlers:
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Added a bounded helper that returns consistent `{"success":false,"error":"..."}` responses with the correct HTTP status.
- Replaced raw `httpd_resp_send_err(...)` calls in OTA auth, OTA upload, OTA status, and test-data error paths.
- Build validation passed after refactor.
- Migrated transmitter callsites to snake_case Ethernet API:
  - [src/espnow/network_config_handlers.cpp](src/espnow/network_config_handlers.cpp)
  - [src/espnow/version_beacon_manager.cpp](src/espnow/version_beacon_manager.cpp)
  - [src/battery_emulator/communication/nvm/comm_nvm.cpp](src/battery_emulator/communication/nvm/comm_nvm.cpp)
- Removed legacy camelCase wrapper declarations/implementations and eliminated duplicate bridge layer.
- Build validation passed after refactor.

Completed in subsequent step:
- Implemented H4 singleton hardening (no heap-backed singleton allocation):
  - [src/network/ethernet_manager.cpp](src/network/ethernet_manager.cpp)
- Replaced `new` + global pointer singleton with function-local static in `EthernetManager::instance()`.
- Build validation passed after refactor.

Completed in subsequent step:
- Implemented H2 callback-owned Ethernet service lifecycle:
  - [src/main.cpp](src/main.cpp)
  - [src/network/ota_manager.h](src/network/ota_manager.h)
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Added idempotent OTA HTTP server lifecycle methods (`init_http_server()` guard + `stop_http_server()`).
- Replaced empty Ethernet callbacks with real actions:
  - `on_connected` starts OTA HTTP server and initializes MQTT client.
  - `on_disconnected` stops OTA HTTP server and gracefully disconnects MQTT.
- Removed boot-only `if (EthernetManager::instance().is_connected())` gate that previously blocked late-Ethernet service startup.
- Added immediate callback replay on boot when Ethernet is already connected to preserve existing behavior.
- Build validation passed after refactor.


Completed in subsequent step:
- Implemented H1 bootstrap orchestrator (setup() decomposition):
  - [src/main.cpp](src/main.cpp)
- Extracted monolithic `setup()` (~340 lines) into 8 ordered static phase functions:
  1. `bootstrap_hardware()` — serial, HAL, firmware metadata
  2. `bootstrap_persistence()` — NVS settings, WiFi radio init
  3. `bootstrap_battery()` — CAN driver, BMS init, datalayer pre-populate (CONTRACT: must precede Phase 6)
  4. `bootstrap_connectivity()` — Ethernet + H2 service callbacks, ESP-NOW queue init
  5. `bootstrap_espnow()` — RX task, ChannelManager, ConnectionManager, state machines, discovery start
  6. `bootstrap_data_layer()` — StaticData, test data init, cell count sync (CONTRACT: must follow Phase 3)
  7. `bootstrap_tasks()` — TransmissionTask, HeartbeatManager, DataSender, DiscoveryTask, MQTT task
  8. `bootstrap_network_services()` — NTP, TimeManager, VersionBeacon
- `setup()` body is now 12 lines: 8 phase calls + 2 LOG_INFO + closing brace.
- Battery ordering fragility made explicit via inline ORDERING CONTRACT comments in `bootstrap_battery()` and `bootstrap_data_layer()`.
- Section 11 architecture comment block preserved in `bootstrap_espnow()`.
- No logic changes — pure structural extraction; identical execution order preserved.
- Build validation passed after refactor (`[SUCCESS] Took 120.98 seconds`).

Completed in subsequent step:
- Implemented H3 ISR-global coupling comments (documentation only):
  - [src/espnow/discovery_task.cpp](src/espnow/discovery_task.cpp)
  - [src/espnow/tx_send_guard.cpp](src/espnow/tx_send_guard.cpp)
- Added inline comments at 5 usage sites in discovery_task.cpp explaining `g_lock_channel` is ISR-written and uint8_t-atomic on ESP32
- Added inline comments at 2 usage sites in tx_send_guard.cpp for same purpose
- Comments clarify that reads are safe (unidirectional ISR-write, task-read), preventing future data-race confusion on refactoring
- Build validation passed after refactor (`[SUCCESS] Took 99.07 seconds`).

Completed in subsequent step:
- Extracted Ethernet-dependent service ownership out of `main.cpp` into a dedicated supervisor:
  - [src/network/service_supervisor.h](src/network/service_supervisor.h)
  - [src/network/service_supervisor.cpp](src/network/service_supervisor.cpp)
  - [src/main.cpp](src/main.cpp)
- `ServiceSupervisor` now owns OTA HTTP server and MQTT lifecycle transitions triggered by Ethernet callbacks.
- `main.cpp` no longer contains inline Ethernet connect/disconnect lambdas for service start/stop.
- Callback registration is centralized and idempotent; current Ethernet state is replayed once on attach.

Completed in subsequent step:
- Introduced Phase D runtime context foundation for shared runtime handles:
  - [src/runtime/runtime_context.h](src/runtime/runtime_context.h)
  - [src/runtime/runtime_context.cpp](src/runtime/runtime_context.cpp)
  - [src/main.cpp](src/main.cpp)
- Moved `espnow_message_queue`, `espnow_discovery_queue`, and `espnow_rx_queue` definitions out of `main.cpp` into `runtime_context.cpp`.
- Added `RuntimeContext::bind_espnow_queues(...)` to own queue handle binding.
- Preserved ISR/library compatibility by synchronizing legacy global exports inside the context binder.
- Updated `main.cpp` to initialize/start ESP-NOW using `RuntimeContext` getters instead of raw globals.

Completed in subsequent step:
- Reduced mutable-global state in TX send guard by encapsulating file-scope `g_*` variables into a private struct:
  - [src/espnow/tx_send_guard.cpp](src/espnow/tx_send_guard.cpp)
- Replaced 10 file-scope guard globals (`g_recovery_*`, backoff counters/timers, mismatch metrics) with one private `GuardRuntimeState` instance in anonymous namespace.
- No API changes and no behavior changes; this is a pure state-encapsulation refactor.

Completed in subsequent step:
- Implemented consolidated runtime diagnostics endpoint:
  - [src/network/ota_manager.h](src/network/ota_manager.h)
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Added `GET /api/health` with bounded JSON payload containing:
  - `eth_connected`, `eth_ready`
  - `mqtt_connected`
  - `espnow_connected`
  - `ota_in_progress`, `ota_ready_for_reboot`
  - `uptime_ms`, `heap_free`, `heap_max_alloc`
- Registered `/api/health` in the OTA HTTP server URI table.

Completed in subsequent step:
- Extended runtime-context ownership to wrap TX payload initialization path:
  - [src/runtime/runtime_context.h](src/runtime/runtime_context.h)
  - [src/runtime/runtime_context.cpp](src/runtime/runtime_context.cpp)
  - [src/main.cpp](src/main.cpp)
  - [../../esp32common/espnow_transmitter/espnow_transmitter.cpp](../../esp32common/espnow_transmitter/espnow_transmitter.cpp)
- Added `RuntimeContext::set_tx_soc(...)` / `tx_payload()` helpers so startup no longer writes `tx_data` directly.
- Updated stale shared-library comment to reflect queue symbols are provided by runtime context, not `main.cpp`.

Completed in subsequent step:
- Removed misplaced `volatile` qualifiers from task-local ACK state in `active_channel_hop_scan`:
  - [src/espnow/discovery_task.cpp](src/espnow/discovery_task.cpp)
- `ack_received` and `ack_channel` are set after dequeuing from `espnow_discovery_queue` — entirely within the discovery task's stack frame. No ISR writes them directly; all ISR concurrency is handled by the FreeRTOS queue.
- Replaced `volatile` qualifiers with an inline comment explaining the actual ISR → queue → task concurrency model.
- Consistent with H3 ISR-coupling documentation work.
- Build validation passed (`[SUCCESS] Took 101.69 seconds`).

Completed in subsequent step:
- Implemented C3 duplicate state-machine init ownership cleanup:
  - [src/espnow/message_handler.h](src/espnow/message_handler.h)
  - [src/espnow/message_handler.cpp](src/espnow/message_handler.cpp)
  - [src/main.cpp](src/main.cpp)
- Removed stale no-op `EspnowMessageHandler::init()` API after Tx connection/device state ownership had already moved to `TransmitterConnectionHandler`.
- Updated Phase 5 bootstrap comments to reflect actual ownership: message routes are registered during `EspnowMessageHandler` singleton construction, while connection state transitions are owned only by `TransmitterConnectionHandler`.
- Legacy duplicate-init path removed entirely; no behavior change intended.
- Build validation passed (`[SUCCESS] Took 102.66 seconds`).

Completed in subsequent step:
- Implemented CC6 route-registration boilerplate reduction:
  - [src/espnow/message_routes.cpp](src/espnow/message_routes.cpp)
- Added a local shared `register_with_context(...)` helper in `setup_message_routes()` to centralize the repeated `router.register_route(..., 0xFF, this)` plumbing.
- Migrated all route registrations in that function to the helper with no handler logic changes.
- Kept behavior identical; this is maintainability cleanup only.
- Build validation passed (`[SUCCESS] Took 91.35 seconds`).

Completed in subsequent step:
- Implemented CC5 startup phase-runner extraction:
  - [src/runtime/bootstrap_phase_runner.h](src/runtime/bootstrap_phase_runner.h)
  - [src/runtime/bootstrap_phase_runner.cpp](src/runtime/bootstrap_phase_runner.cpp)
  - [src/main.cpp](src/main.cpp)
- Added reusable `BootstrapPhaseRunner` module with a generic `Phase` table + `run_phases(...)` helper.
- Migrated `setup()` to execute the same 8 bootstrap phases through a static phase table, preserving exact ordering and behavior.
- This creates a sharable orchestration primitive for future transmitter/receiver convergence without changing startup contracts.
- Build validation passed (`[SUCCESS] Took 86.28 seconds`).

Completed in subsequent step:
- Finalized CC5 commonization by moving canonical phase-runner implementation into shared common library:
  - [../../esp32common/runtime_common_utils/include/runtime_common_utils/bootstrap_phase_runner.h](../../esp32common/runtime_common_utils/include/runtime_common_utils/bootstrap_phase_runner.h)
  - [../../esp32common/runtime_common_utils/src/bootstrap_phase_runner.cpp](../../esp32common/runtime_common_utils/src/bootstrap_phase_runner.cpp)
  - [../../esp32common/runtime_common_utils/library.json](../../esp32common/runtime_common_utils/library.json)
  - [src/main.cpp](src/main.cpp)
  - [src/runtime/bootstrap_phase_runner.h](src/runtime/bootstrap_phase_runner.h)
- Updated transmitter to include the shared header directly from `runtime_common_utils`.
- Kept transmitter-local runtime header only as a non-owning compatibility forwarder.
- Kept any remaining transmitter-local phase-runner files implementation-free (no local phase-runner symbols), so shared common code remains the only active implementation.
- Build validation passed (`[SUCCESS] Took 95.35 seconds`).

Completed in subsequent step:
- Finalized CC5 cleanup follow-through:
  - [platformio.ini](platformio.ini)
- Removed the temporary build exclusion for `src/runtime/bootstrap_phase_runner.cpp`; the file is implementation-empty, so compiling it does not introduce a second implementation path.
- Shared `runtime_common_utils` remains the canonical and only functional phase-runner implementation.
- Build validation passed (`[SUCCESS] Took 95.35 seconds`).

Completed in subsequent step:
- Implemented CC4 shared OTA/auth primitive extraction (foundation):
  - [src/network/ota_manager.h](src/network/ota_manager.h)
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
  - [../../esp32common/webserver_common_utils/include/webserver_common_utils/ota_auth_utils.h](../../esp32common/webserver_common_utils/include/webserver_common_utils/ota_auth_utils.h)
  - [../../esp32common/webserver_common_utils/src/ota_auth_utils.cpp](../../esp32common/webserver_common_utils/src/ota_auth_utils.cpp)
- Moved neutral OTA/auth helpers into shared common code:
  - HTTP header extraction,
  - random lowercase hex generation,
  - constant-time string comparison,
  - generic HMAC-SHA256-to-hex helper.
- Removed duplicated transmitter-local implementations of those helpers from `OtaManager` and rewired transmitter OTA auth validation/signature generation to the shared utility.
- Left transmitter-specific session lifecycle/orchestration in place for now; this is a foundation step toward the fuller CC4 shared session module.
- Build validation passed (`[SUCCESS] Took 102.61 seconds`).

Completed in subsequent step:
- Completed CC4 shared OTA session lifecycle extraction and transmitter migration:
  - [../../esp32common/webserver_common_utils/include/webserver_common_utils/ota_session_utils.h](../../esp32common/webserver_common_utils/include/webserver_common_utils/ota_session_utils.h)
  - [../../esp32common/webserver_common_utils/src/ota_session_utils.cpp](../../esp32common/webserver_common_utils/src/ota_session_utils.cpp)
  - [src/network/ota_manager.h](src/network/ota_manager.h)
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Added shared `OtaSessionUtils::Session` with canonical arm/validate/consume/deactivate behavior and explicit validation outcomes.
- Migrated transmitter `OtaManager` to the shared session model (state storage, signature validation path, one-time consume, OTA status/session reporting).
- Kept HTTP error/response behavior stable by mapping shared validation results to existing status codes/messages.
- Build validation passed (`[SUCCESS] Took 101.63 seconds`).

Completed in subsequent step:
- Hardened ESP-NOW ACK/message construction in active transmitter config/settings reply paths:
  - [src/espnow/mqtt_config_handlers.cpp](src/espnow/mqtt_config_handlers.cpp)
  - [src/espnow/version_beacon_manager.cpp](src/espnow/version_beacon_manager.cpp)
  - [src/settings/settings_manager.cpp](src/settings/settings_manager.cpp)
- Replaced placeholder zero checksums in `mqtt_config_ack_t` and `settings_update_ack_msg_t` responses with `EspnowPacketUtils::calculate_message_checksum(...)` from shared common utilities.
- Switched these transmitted stack message structs to `{}` zero-initialization so checksum calculation and on-wire payload contents remain deterministic.
- Kept `network_config_ack_t` on its existing path because the shared message definition has no checksum field.
- Build validation passed (`[SUCCESS] Took 90.75 seconds`).

Completed in subsequent step:
- Hardened test-data configuration POST payload handling to enforce bounded full-body reads:
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Added strict request-body validation for `/api/test_data_config` POST:
  - reject missing/empty body with `400`,
  - reject oversized body with `413 Payload Too Large`,
  - require full-body read before JSON parse (no truncated partial parse path).
- Added shared local helper for bounded body reads so payload-length checks are centralized within OTA/test-data handler plumbing.
- Build validation passed (`[SUCCESS] Took 122.31 seconds`).

Completed in subsequent step:
- Hardened OTA upload request guards for payload robustness:
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Added pre-stream upload validation for `/ota_upload`:
  - reject missing/empty upload body (`400`),
  - reject payload larger than OTA target partition (`413 Payload Too Large`).
- Added bounded timeout handling in OTA upload receive loop to prevent indefinite timeout-only loops under stalled clients; timed-out uploads now abort cleanly and return `408 Request Timeout`.
- Build validation passed (`[SUCCESS] Took 104.89 seconds`).

Completed in subsequent step:
- Hardened OTA arm endpoint request shape enforcement:
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Added strict body policy for `/api/ota_arm` POST: requests carrying a body are rejected with `400` to enforce the intended header-only/session-arm API contract.
- Build validation passed (`[SUCCESS] Took 119.44 seconds`).

Completed in subsequent step:
- Added `Content-Type` validation across HTTP endpoints (415 Unsupported Media Type on mismatch):
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Added `HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE = 415` constant and `415 Unsupported Media Type` mapping to `http_status_text()`.
- Added `check_request_content_type()` anonymous-namespace helper: validates `Content-Type` header prefix, allows `; charset=...` parameters, returns `415` if absent or mismatched.
- Applied `check_request_content_type(req, "application/json")` guard to `/api/test_data_config` POST handler — rejects non-JSON bodies before read/parse.
- Applied `check_request_content_type(req, "application/octet-stream")` guard to `/ota_upload` handler — rejects non-binary uploads before `Update.begin()`.
- Added explicit `Transfer-Encoding: chunked` detection in `/ota_upload` handler: chunked uploads are rejected with `400` with a clear message ("use Content-Length"), because the OTA receive loop requires a known `content_len` for partition size pre-flight and bounded loop termination.
- Added missing `httpd_resp_set_type(req, "text/plain")` to `root_handler` — all endpoints now declare their response Content-Type explicitly.
- Build validation passed (`[SUCCESS] Took 97.79 seconds`).

Completed in subsequent step:
- Standardized OTA/test-data HTTP error envelope fields and expanded body-shape enforcement:
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Upgraded `send_json_error(...)` payloads to include standardized fields:
  - `success=false`
  - `code` (HTTP status)
  - `message` (canonical text)
  - `details` (optional)
  - retained `error` for backward compatibility with existing clients.
- Added shared `reject_unexpected_request_body(...)` helper and applied it to bodyless POST endpoints:
  - `/api/ota_arm`
  - `/api/test_data_apply`
  - `/api/test_data_reset`
- Added structured `details` context to `Content-Type` validation errors:
  - missing header now reports expected media type
  - mismatched header now reports expected vs received media type.
- Build validation passed (`[SUCCESS] Took 102.00 seconds`).

Completed in subsequent step:
- Added standardized JSON success helper and migrated simple test-data success responses:
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Added shared `send_json_ok(...)` helper with uniform fields (`success`, `code`, `message`, optional `details`) for 200 responses.
- Added explicit `200 OK` mapping in `http_status_text()` for consistent status-text generation across helper-based responses.
- Migrated body-only success responses from hardcoded string literals to `send_json_ok(...)` in:
  - `/api/test_data_config` POST
  - `/api/test_data_apply` POST
  - `/api/test_data_reset` POST
- Build validation passed (`[SUCCESS] Took 94.92 seconds`).

Completed in subsequent step:
- Expanded request-shape hardening to reject unexpected request bodies on GET/read endpoints:
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Reused shared `reject_unexpected_request_body(...)` helper for:
  - `/api/health`
  - `/api/get_event_logs`
  - `/api/ota_status`
  - `/api/firmware_info`
  - `/api/test_data_config` (GET)
- This enforces a strict API contract for read-only endpoints and prevents silent acceptance of malformed client calls.
- Build validation passed (`[SUCCESS] Took 97.27 seconds`).

Completed in subsequent step:
- Added OTA source-client IP observability for security troubleshooting:
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Added best-effort `get_request_client_ip(...)` helper using `httpd_req_to_sockfd(...)` + `getpeername(...)` (IPv4).
- Added source-IP context to OTA auth and upload logs:
  - missing `X-OTA-*` header rejection logs now include client IP,
  - invalid session/signature logs now include client IP,
  - successful auth logs include client IP,
  - upload-start logs include client IP,
  - auth-rejected upload attempts log client IP.
- Build validation passed (`[SUCCESS] Took 97.28 seconds`).

Completed in subsequent step:
- Added lightweight per-IP OTA auth throttling (rate-limit lockout) for repeated failed attempts:
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Added `429 Too Many Requests` support to HTTP status mapping and JSON error responses for throttled clients.
- Implemented fixed-allocation in-memory limiter state (no heap allocation):
  - tracks up to 8 source IP entries,
  - 60s failure window,
  - lockout after 5 failed OTA auth attempts,
  - 120s temporary block period.
- Integrated limiter into `validate_ota_auth_headers(...)`:
  - pre-check rejects blocked clients with `429` and retry-after details,
  - failed auth outcomes record failure counters,
  - successful auth clears prior failure state for that source IP.
- Build validation passed (`[SUCCESS] Took 97.27 seconds`).

Completed in subsequent step:
- Improved OTA auth throttle response semantics and diagnostics:
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Added explicit `Retry-After` response header for `429 Too Many Requests` lockout responses (seconds until retry).
- Added richer `details` payload on `401` auth failures for invalid session/signature outcomes with remaining attempt count.
- Preserved existing status codes and backward-compatible error envelope fields while increasing operator/client observability.
- Build validation passed (`[SUCCESS] Took 105.29 seconds`).

Completed in subsequent step:
- Refined OTA auth throttling semantics for unresolved client-IP cases and expanded root endpoint request-shape enforcement:
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Updated `validate_ota_auth_headers(...)` so per-IP throttle accounting/rate-limit checks execute only when client IP resolution succeeds.
  - Prevents unintentionally collapsing all unresolved-IP clients into a shared limiter bucket.
  - Maintains existing auth checks and response behavior for all callers.
- Added strict unexpected-body rejection to root `GET /` endpoint using the shared `reject_unexpected_request_body(...)` helper.
- Build validation passed (`[SUCCESS] Took 92.11 seconds`).

Completed in subsequent step:
- Hardened `Content-Type` validation to exact media-type token matching (case-insensitive):
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Replaced prefix-only matching with exact token validation that accepts only:
  - exact media type (e.g. `application/json`), or
  - media type followed by valid parameter delimiters (`;` / whitespace).
- Prevented false-positive accepts such as `application/jsonx` and similar malformed types.
- Added explicit `<cctype>` include for clear/portable character classification usage.
- Build validation passed (`[SUCCESS] Took 99.29 seconds`).

Completed in subsequent step:
- Hardened OTA auth-header expiry parsing and validation time consistency:
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Added strict unsigned-integer parsing for `X-OTA-Expires` via a dedicated helper (`parse_uint32_strict(...)`) so non-numeric/malformed values are rejected deterministically.
- Updated auth-header validation to fail fast on invalid `X-OTA-Expires` format with a structured `401` response detail.
- Reused a single `now_ms` snapshot through expiry checks and `validate_and_consume(...)` to avoid mixed-time validation edge cases.
- Build validation passed (`[SUCCESS] Took 89.46 seconds`).

Completed in subsequent step:
- Tightened `X-OTA-Expires` parser semantics to digit-only (no whitespace/sign/extra chars):
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Reworked `parse_uint32_strict(...)` to manual bounded digit parsing (`0`-`9` only) with explicit `uint32_t` overflow rejection, eliminating permissive `strtoul(...)` behaviors.
- Removed a duplicate `<cctype>` include during cleanup.
- Build validation passed (`[SUCCESS] Took 99.93 seconds`).

Completed in subsequent step:
- Hardened `Transfer-Encoding` chunked rejection to token-aware header parsing:
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Added `header_has_token_ci(...)` helper for case-insensitive, comma-separated token matching with whitespace handling.
- Updated `/ota_upload` guard to reject any `Transfer-Encoding` header that contains `chunked` as a token (not just prefix-form values).
- This closes bypass-shaped header forms such as `gzip, chunked` while preserving existing `Content-Length` requirement behavior.
- Build validation passed (`[SUCCESS] Took 122.49 seconds`).


Completed in subsequent step:
- Migrated remaining raw-`snprintf` JSON responses to ArduinoJson, replaced `atoi` with strict parse in event-log limit, and fixed chunked-TE block indentation:
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- `ota_arm_handler`:
  - Added best-effort source-IP extraction via `get_request_client_ip(...)` at handler entry.
  - Added `LOG_INFO` statement logging the arming client IP and `session_id` on successful arm.
  - Replaced raw `snprintf` response construction with `StaticJsonDocument<320>` + `serializeJson(...)`: eliminates manual field escaping, uses a length-checked serialized path, and is consistent with all other handler response patterns.
- `ota_status_handler`:
  - Removed crude `"` → `'` character-substitution loop used to sanitize `ota_last_error_` — loop did not escape `\`, control characters, or other JSON-unsafe bytes.
  - Replaced the entire response construction block with `StaticJsonDocument<512>` + `serializeJson(...)`: ArduinoJson now handles all field escaping correctly, including embedded quotes, backslashes, and control characters in the last-error string.
- `firmware_info_handler`:
  - Removed unchecked `snprintf(json, sizeof(json), ...)` + `strlen(json)` response path (return value of `snprintf` was discarded, leaving truncated/corrupted JSON undetected).
  - Replaced with bounded string fields built via `snprintf` into local char arrays, then assigned into `StaticJsonDocument<384>` fields; `serializeJson(...)` result length is explicitly checked before `httpd_resp_send(...)`.
- `event_logs_handler` `?limit=` parameter:
  - Replaced `atoi(limit_str)` (accepts leading spaces, signs, non-numeric tails, silently truncates) with `parse_uint32_strict(...)` + explicit range guard `[1, 500]`.
  - Values that fail strict parse or fall outside the valid range are silently ignored (handler falls through to default limit), matching the intended graceful-degradation behaviour.
- Chunked-TE rejection block indentation: corrected misaligned `send_json_error(...)` and `return ESP_FAIL` inside the `if (is_chunked)` guard to consistent 4-space indentation.
- Build validation passed (`[SUCCESS] Took 94.05 seconds`).

Completed in subsequent step:
- Completed full ArduinoJson migration across all remaining `snprintf`-based handlers and removed last hardcoded JSON string literal:
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- `health_handler`:
  - Replaced `snprintf(...)` with 9 manual bool-as-string ternary arguments with `StaticJsonDocument<384>` that assigns boolean fields directly as C++ `bool` values.
  - ArduinoJson serializes these as proper JSON `true`/`false` tokens without any manual conversion; `uptime_ms`, `heap_free`, `heap_max_alloc` typed as unsigned integers.
  - Removed associated manual signed-vs-unsigned length check on `json_len`; replaced with consistent `json_len == 0 || json_len >= sizeof(json)` guard matching every other handler.
- `ota_upload_handler` success path:
  - Replaced hardcoded literal JSON string (`httpd_resp_sendstr(req, "{...}")`) with `StaticJsonDocument<192>` + `serializeJson(...)` send.
  - Preserved all existing response fields including typed `ready_for_reboot` boolean.
  - Added `code = 200` field for consistency with `send_json_ok`-based success responses.
  - All OTA/HTTP handler JSON responses in the file are now constructed exclusively via ArduinoJson; no raw string literals or `snprintf`-based JSON remain.
- Build validation passed (`[SUCCESS] Took 92.22 seconds`).

Completed in subsequent step:
- Added `Cache-Control: no-store` response header to every HTTP endpoint to prevent proxies and browsers from caching session tokens, OTA status, or system state:
  - [src/network/ota_manager.cpp](src/network/ota_manager.cpp)
- Applied to all response paths in the two shared helpers:
  - `send_json_error` — both the normal path and the rare format-failure fallback path.
  - `send_json_ok` — normal path.
- Applied individually to every custom success-response send path (handlers that build their own JSON directly):
  - `ota_arm_handler` success (most sensitive — returns `session_id`, `nonce`, `signature`).
  - `ota_upload_handler` success.
  - `ota_status_handler` success (returns session state and OTA progress).
  - `firmware_info_handler` success.
  - `health_handler` success.
  - `event_logs_handler` — set after `httpd_resp_set_type` to cover both the `#ifdef` chunked streaming path and the `#else` no-emulator direct-send path.
  - `test_data_config_get_handler` success.
- Every response from the OTA HTTP server now carries `Cache-Control: no-store`; the `root_handler` `text/plain` response was intentionally left without this header as it contains no sensitive or dynamic state.
- Build validation passed (`[SUCCESS] Took 88.30 seconds`).

---

## Final Assessment
The transmitter codebase is on a solid architectural trajectory, and the major review items plus OTA cross-device stabilization fixes are now implemented. The remaining work is primarily **operational guardrails** (release gating and regression prevention), not core architecture repair.

### Next Step (proceeding item)
Create and enforce a cross-device OTA release gate for every OTA-related change:

1. Build gate (transmitter + receiver must both pass).
2. Runtime smoke gate (arm, upload, completion, reboot path).
3. Panic gate (no `httpd` stack canary or Guru Meditation during OTA).
4. Contract gate (required `/api/ota_arm` and `/ota_upload` headers/fields unchanged or explicitly versioned).

Reference checklist added for this gate:
- [../esp32common/OTA_CROSS_DEVICE_COMPATIBILITY_CHECKLIST.md](../esp32common/OTA_CROSS_DEVICE_COMPATIBILITY_CHECKLIST.md)