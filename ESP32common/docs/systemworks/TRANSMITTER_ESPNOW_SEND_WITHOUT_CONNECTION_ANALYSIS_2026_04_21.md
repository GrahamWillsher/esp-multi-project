# Transmitter: ESP-NOW Send-Without-Connection Analysis
**Original Date:** 2026-04-21  
**Last Reviewed:** 2026-05-13  
**Scope:** ESPnowtransmitter2 — all code paths that issue or attempt to issue an ESP-NOW send, and core affinity of all runtime tasks  
**Original Trigger:** Observed `[ERROR][BATTERY] Battery settings: Invalid checksum - message rejected` at the receiver, caused by a pre-existing checksum algorithm mismatch (fixed separately). During investigation, a broader set of send-without-connection issues and task core-pinning gaps were identified.

> **Status as of 2026-05-13:** Core design is confirmed correct — no wire-send occurs while disconnected; discovery runs at the lowest task priority on Core 1, isolated from the battery emulator. Several residual code-clarity and defensive-guard issues remain open. See Section 6 for the implementation plan.

---

## 1. Send Authority Stack

The transmitter has a three-layer guard stack before any `esp_now_send()` is ever called:

```
DataSender::task_impl()
  → gate: TxStateMachine::is_transmission_active()
           (state == ACTIVE: receiver has sent REQUEST_DATA)
  → writes to EnhancedCache

TransmissionTask::task_impl()
  → gate: EspNowConnectionManager::is_connected()
  → drains from EnhancedCache

TxSendGuard::send_to_receiver_guarded()
  → gate: channel coherence check (home ch == peer ch, authority lock consistent)
  → calls esp_now_send()
```

**All three layers are working correctly.** The binding gate is `TxStateMachine::is_transmission_active()` in `data_sender.cpp` — data only enters the cache when the state machine is in `ACTIVE`, which requires a confirmed connection AND the receiver having explicitly sent `REQUEST_DATA`. The `TransmissionTask`'s `is_connected()` check is a correct but redundant second layer.

`ACTIVE` is a sub-state of `CONNECTED` — so `is_transmission_active()` is the stricter gate:

| Authority | What it checks | Enforced by |
|---|---|---|
| `TxStateMachine::is_transmission_active()` | State == `ACTIVE` (receiver has sent `REQUEST_DATA`) | `DataSender` task |
| `EspNowConnectionManager::instance().is_connected()` | Connection manager state == `CONNECTED` | `TransmissionTask`, `HeartbeatManager`, `VersionBeaconManager` |
| `TxSendGuard::send_to_receiver_guarded()` | Channel coherence + recovery guard | All callers of `esp_now_send()` |

---

## 2. Task Core Affinity and Priority — CURRENT STATE (NEEDS CORRECTION)

**CRITICAL PRINCIPLE:** All tasks **MUST** be explicitly pinned to a specific core using `xTaskCreatePinnedToCore()`. Leaving tasks unpinned (`xTaskCreate`) allows FreeRTOS to schedule them on any available core, violating deterministic core isolation and defeating the battery-emulator safety design.

**ISSUE IDENTIFIED:** The actual task pinning does NOT match the stated design intent. Several high-priority tasks are unpinned or pinned to Core 0, sharing the core with the battery emulator safety loop.

---

### Current vs. Desired State

| Task | Current Priority | Current Core | Current Pin | **→ Desired Core** | **→ Desired Pin** | Impact |
|---|---|---|---|---|---|---|
| Battery emulator / CAN safety | 5 | 0 | ✓ | 0 | ✓ | Core 0 sole owner |
| **RX task** | **5** | **unpinned** | **✗** | **1** | **✓** | **[FIX I-1a]** Equal priority conflict with battery |
| **Network config task** | **3** | **unpinned** | **✗** | **1** | **✓** | **[FIX I-1b]** Preemption risk |
| **Connection event processor** | 3 | 0 | ✓ | 0 | ✓ | No change — low CPU (event-driven) |
| **TxReconnectManager** | 2 | 0 | ✓ | 0 | ✓ | No change — low CPU (queue-blocked) |
| **DataSender** | **2** | **unpinned** | **✗** | **1** | **✓** | **[FIX I-1c]** Unpinned risk |
| **MQTT task** | **1** | **unpinned** | **✗** | **1** | **✓** | **[FIX I-1c]** Unpinned risk |
| TransmissionTask | 1 | 1 | ✓ | 1 | ✓ | Already correct |
| Hop worker | 1 | 1 | ✓ | 1 | ✓ | Already correct |

---

### Rationale for Core Assignment

**Core 0 (Battery + Housekeeping):**
- Battery emulator loop (priority 5) — safety-critical, must not be preempted
- Connection event processor (priority 3) — event-driven, minimal CPU, must be responsive to link state changes
- TxReconnectManager (priority 2) — manager task, mostly queue-blocked, drives discovery but non-blocking

**Core 1 (ESP-NOW Services):**
- RX task (priority 5) — must NOT share Core 0 with battery at equal priority
- Network config task (priority 3) — heavy on parsing, should not interfere with battery
- DataSender (priority 2) — reads battery data and caches; better to run on isolated core
- MQTT task (priority 1) — background telemetry, lowest priority, must not steal Core 0 CPU
- TransmissionTask (priority 1) — cache drain, already correctly isolated
- Hop worker (priority 1) — channel scan, already correctly isolated

**Effect:** Core 0 is guaranteed clean for battery control code. Core 1 handles all ESP-NOW housekeeping at deterministic priorities.

---

### Current actual state:

| Task | Priority | Core | Pinned | Notes |
|---|---|---|---|---|
| **Battery emulator / CAN safety** | **PRIORITY_CRITICAL = 5** | **0** | ✓ (implicit loop) | Preempts everything on Core 0 |
| **ESP-NOW RX task** | **PRIORITY_CRITICAL = 5** | **unpinned** | ✗ | Uses `xTaskCreate`; **CAN LAND ON CORE 0** |
| **Network config task** | **PRIORITY_NETWORK_CONFIG = 3** | **unpinned** | ✗ | Uses `xTaskCreate`; **CAN LAND ON CORE 0** |
| **Connection event processor** | **3** | **0** | ✓ | Pinned to Core 0; handles connection state |
| **TxReconnectManager manager** | **PRIORITY_NORMAL = 2** | **0** | ✓ | Pinned to Core 0; blocks on queue; negligible CPU |
| **DataSender** | **PRIORITY_NORMAL = 2** | **unpinned** | ✗ | Uses `xTaskCreate`; **CAN LAND ON CORE 0** |
| **MQTT task** | **PRIORITY_LOW = 1** | **unpinned** | ✗ | Uses `xTaskCreate`; **CAN LAND ON CORE 0** |
| **TransmissionTask** (cache drain) | **PRIORITY_LOW = 1** | **1** | ✓ | Correctly isolated on Core 1 |
| **Hop worker** (channel scan) | **PRIORITY_LOW = 1** | **1** | ✓ | Correctly isolated on Core 1 |

---

### Problem

1. **RX task at PRIORITY_CRITICAL:** Cannot land on Core 0 without preempting the battery emulator, but is not pinned. FreeRTOS *may* schedule it on Core 1, but there's no guarantee. **Equal priority on same core = round-robin time-slicing**, unacceptable for a safety-critical battery loop.
2. **Network config task at PRIORITY_NETWORK_CONFIG (3):** Also unpinned; could land on Core 0, where it would preempt ESP-NOW discovery and other work.
3. **DataSender and MQTT task:** Both unpinned; could land on Core 0.

**Result:** Core 0 may be congested with multiple high-priority ESP-NOW housekeeping tasks, competing with the battery emulator. While the battery emulator is at priority 5 (highest), the RX task at priority 5 (equal priority, round-robin) will share CPU time on Core 0, and tasks 1, 2, 3 can interleave and cause scheduling jitter.

---

## 3. Behaviour While Disconnected

While there is no ESP-NOW connection, the only ESP-NOW radio activity is:

- **PROBE packets** broadcast by the hop worker (`DiscoveryTask::run_hop_scan()`) during active channel scanning. The scan runs with an adaptive backoff schedule — 0 → 3 s → 5 s → 10 s → 15 s → 30 s → 60 s (max) — between scan cycles.
- The hop worker runs at **Priority 1 (PRIORITY_LOW)** on **Core 1**, isolated from the battery emulator.
- `HeartbeatManager::tick()` returns immediately when `EspNowConnectionState != CONNECTED` — no heartbeat packets are sent.
- `DataSender` does not write to the cache when `is_transmission_active()` is false, so no data accumulates for transmission.
- `TransmissionTask` skips drain when `is_connected()` is false.

**No battery data, heartbeat, or config messages are sent via ESP-NOW while disconnected.**

---

## 4. Issues Found (Updated 2026-05-13)

### 4.1 `DataSender` and MQTT task not pinned to Core 1  *(NEW — gap vs. design intent)*

**File:** `src/espnow/data_sender.cpp` and `src/config/runtime_task_startup.cpp`

Both `DataSender::start()` and `start_mqtt_task_if_enabled()` use `xTaskCreate`:

```cpp
// data_sender.cpp
xTaskCreate(task_impl, "task_data", STACK_SIZE_DATA_SENDER, NULL, PRIORITY_NORMAL, NULL);

// runtime_task_startup.cpp
xTaskCreate(task_mqtt_loop, "mqtt_task", STACK_SIZE_MQTT, nullptr, PRIORITY_LOW, nullptr);
```

`xTaskCreate` allows FreeRTOS to schedule these tasks on **either core**. The battery emulator runs on Core 0 at `PRIORITY_CRITICAL = 5`. If `DataSender` (PRIORITY_NORMAL = 2) or the MQTT task (PRIORITY_LOW = 1) land on Core 0 they will never preempt the battery emulator but they do add scheduling load and reduce worst-case latency headroom.

**Design intent:** both tasks should run on **Core 1** alongside `TransmissionTask` and the hop worker, which are already correctly pinned there.

**Fix:** Change both to `xTaskCreatePinnedToCore(..., /*core=*/1)`.

---

### 4.2 `VersionBeaconManager::update()` — periodic clause lacks uptime guard  *(pre-existing — open)*

**File:** `src/espnow/version_beacon_manager.cpp`

```cpp
if (now - last_beacon_ms_ >= PERIODIC_INTERVAL_MS) {
    send_version_beacon(true);   // no early-return when disconnected
}
```

`send_version_beacon()` internally checks `can_send_beacon_now()` and returns early when disconnected **without** updating `last_beacon_ms_`. As a result this clause fires on every `update()` call while disconnected rather than just every 30 seconds, creating unnecessary function-call overhead. No wire-send occurs. **Risk: negligible. Fix: trivial.**

**Fix:**
```cpp
void VersionBeaconManager::update() {
    if (!EspNowConnectionManager::instance().is_connected()) return;  // add this
    ...
}
```

---

### 4.3 `VersionBeaconManager::init()` — forced beacon call at startup  *(partially mitigated — cleanup pending)*

**File:** `src/espnow/version_beacon_manager.cpp`

```cpp
void VersionBeaconManager::init() {
    send_version_beacon(true);   // ← still present
}
```

At `init()` time no connection exists. Since the April 2026 review `send_version_beacon()` now correctly sets `pending_beacon_ = true` and returns early when not connected — so the clock-skew and state-loss bugs originally documented are resolved. The call itself is now harmless but is dead code at startup. Leaving it creates confusion: it implies an immediate send attempt is intended.

**Fix:** Remove `send_version_beacon(true)` from `init()`. The `pending_beacon_` flag is already set to `true` by default; the first beacon is correctly driven via the `on_connected` path in `TxReconnectManager::handle_confirm_ack()`.

---

### 4.4 `send_config_section()` — no explicit connection guard  *(pre-existing — open)*

**File:** `src/espnow/version_beacon_manager.cpp`

`send_config_section()` calls `TxSendGuard::send_to_receiver_guarded()` directly without an `is_connected()` guard. It is only reachable via the `msg_config_section_request` message handler, which can only be triggered by a received message (implying a live connection), so no out-of-connection send can occur **in the current call chain**. However this is a brittle assumption — any future caller will silently attempt a send.

**Fix:** Add an explicit early-return:
```cpp
if (!EspNowConnectionManager::instance().is_connected()) {
    LOG_WARN("VERSION_BEACON", "send_config_section: not connected, ignoring");
    return;
}
```

---

### 4.5 `HeartbeatManager::send_temperature_report()` — bypasses `TxSendGuard`  *(pre-existing — open)*

**File:** `src/espnow/heartbeat_manager.cpp`

```cpp
const esp_err_t result = esp_now_send(
    peer_mac,
    reinterpret_cast<const uint8_t*>(&report),
    sizeof(report)
);
```

This calls `esp_now_send()` directly, bypassing `TxSendGuard::send_to_receiver_guarded()`. `send_temperature_report()` is only callable from `send_heartbeat()`, which is itself gated by `EspNowConnectionState::CONNECTED` — so **no wire-send occurs while disconnected**. However channel coherence is not checked: if home and peer channels diverge during a recovery window, this will silently produce `ESP_ERR_ESPNOW_SEND_FAIL` without triggering the recovery path that `TxSendGuard` provides.

**Fix:** Route through `TxSendGuard::send_to_receiver_guarded()` and treat `ESP_ERR_INVALID_STATE` / `ESP_ERR_TIMEOUT` as non-fatal to preserve best-effort semantics while keeping the channel coherence path active. If keeping direct send intentionally, add a comment explicitly documenting why channel coherence is not required here.

---

### 4.6 `DataSender` comment contradicts the actual send gate  *(code clarity — open)*

**File:** `src/espnow/data_sender.cpp`

The comment immediately above `send_battery_data()` reads:
> *"Section 11 Architecture: ALWAYS cache-first (non-blocking) — Data flows through EnhancedCache regardless of connection state"*

This is incorrect. Data only enters the cache when `is_transmission_active()` is true (state == `ACTIVE`). The comment describes an aspirational architecture that is not implemented. Misleading for future maintenance.

**Fix:** Update the comment to accurately describe the `ACTIVE`-state gate.

---

### 4.7 `DataSender` calls `TransmissionSelector::transmit_dynamic_data()` per tick  *(dead weight — open)*

**File:** `src/espnow/data_sender.cpp`

```cpp
auto result = TransmissionSelector::transmit_dynamic_data(tx_data.soc, tx_data.power, timestamp_str);
```

`TransmissionSelector::transmit_dynamic_data()` is an advisory route-planning function that only increments counters — it performs no actual send. It is called on every active data tick at `TRACE` log level, adding dead work to the hot path.

**Fix:** Remove from `DataSender::send_battery_data()`. If route statistics are required for diagnostics, sample at a coarse interval (e.g. every 5–10 s) or remove entirely.

---

## 5. Existing Protections That Are Working Correctly

For completeness, the following paths are **correctly guarded** and do **not** send while disconnected:

| Component | Mechanism | Verified |
|---|---|---|
| `TransmissionTask` | Explicit `is_connected()` gate at top of loop | ✓ |
| `HeartbeatManager::tick()` | Checks `EspNowConnectionState::CONNECTED` | ✓ |
| `TxSendGuard::send_to_receiver_guarded()` | Channel coherence + backoff guard | ✓ |
| `VersionBeaconManager::send_version_beacon()` | `can_send_beacon_now()` = `is_connected() && has_stable_heartbeat()` | ✓ |
| `VersionBeaconManager` event notifications | `pending_beacon_` flag; deferred send until `can_send_beacon_now()` | ✓ |
| `settings_espnow.cpp` ACK/notification | Called only from received-message handler (connection-implied) | ✓ |
| `TxReconnectManager` connect-confirm | `TxSendGuard` used; only sends after WORKER_FOUND | ✓ |
| Hop worker | Sends only PROBE broadcast frames (not battery data); Priority 1, Core 1 | ✓ |

---

## 6. Implementation Plan — Core Isolation and Residual Cleanup

All items below are self-contained and can be applied independently. Ordered by priority: core isolation issues first (I-1a, I-1b, I-1c), then code-quality cleanup.

---

### I-1a — Pin RX task to Core 1  *(PRIORITY: CRITICAL)*

**File:** `src/espnow/message_handler.cpp`  
**Rationale:** RX task runs at `PRIORITY_CRITICAL = 5` (equal to battery emulator) but is unpinned. On an equal-priority system, FreeRTOS uses time-slicing (round-robin) if on the same core. The RX task must not share Core 0 with the battery loop.

**Change:**
```cpp
void EspnowMessageHandler::start_rx_task(QueueHandle_t queue) {
    // Create main RX task — PINNED TO CORE 1
    xTaskCreatePinnedToCore(
        rx_task_impl,
        "espnow_rx",
        task_config::STACK_SIZE_ESPNOW_RX,
        (void*)queue,
        task_config::PRIORITY_CRITICAL,
        nullptr,
        1  // Core 1 — isolated from battery emulator on Core 0
    );
    LOG_DEBUG("MSG_HANDLER", "ESP-NOW RX task started (Core 1)");
```

---

### I-1b — Pin network config task to Core 1  *(PRIORITY: HIGH)*

**File:** `src/espnow/message_handler.cpp`  
**Rationale:** Network config task runs at priority 3 and is unpinned. It should not land on Core 0 where it could preempt lower-priority ESP-NOW housekeeping tasks.

**Change:**
```cpp
    // Create network config processing queue and task
    network_config_queue_ = xQueueCreate(task_config::NETWORK_CONFIG_QUEUE_SIZE, sizeof(espnow_queue_msg_t));
    if (network_config_queue_ == nullptr) {
        LOG_ERROR("MSG_HANDLER", "Failed to create network config queue");
    } else {
        xTaskCreatePinnedToCore(
            network_config_task_impl,
            "net_config",
            task_config::STACK_SIZE_NETWORK_CONFIG,
            nullptr,
            task_config::PRIORITY_NETWORK_CONFIG,
            &network_config_task_handle_,
            1  // Core 1 — isolated from battery emulator on Core 0
        );
        LOG_DEBUG("MSG_HANDLER", "Network config task started (Core 1, priority=%d)", task_config::PRIORITY_NETWORK_CONFIG);
    }
```

---

### I-1c — Pin `DataSender` and MQTT task to Core 1  *(PRIORITY: HIGH)*

**Files:** `src/espnow/data_sender.cpp`, `src/config/runtime_task_startup.cpp`  
**Rationale:** Both tasks use `xTaskCreate` (unpinned) and could land on Core 0. `DataSender` runs at priority 2 and MQTT at priority 1, both below the battery emulator (5) but could still cause scheduling jitter.

**Change 1 — `data_sender.cpp`:**
```cpp
void DataSender::start() {
    xTaskCreatePinnedToCore(
        task_impl,
        "task_data",
        task_config::STACK_SIZE_DATA_SENDER,
        nullptr,
        task_config::PRIORITY_NORMAL,
        nullptr,
        1   // Core 1 — isolated from battery emulator on Core 0
    );
    LOG_DEBUG("DATA_SENDER", "Data transmission task started (Core 1)");
}
```

**Change 2 — `runtime_task_startup.cpp`:**
```cpp
void start_mqtt_task_if_enabled() {
    if (!config::features::MQTT_ENABLED) {
        return;
    }

    const BaseType_t result = xTaskCreatePinnedToCore(
        task_mqtt_loop,
        "mqtt_task",
        task_config::STACK_SIZE_MQTT,
        nullptr,
        task_config::PRIORITY_LOW,
        nullptr,
        1   // Core 1 — background telemetry, isolated from battery emulator
    );

    if (result != pdPASS) {
        LOG_ERROR("TASKS", "Failed to start MQTT task");
    }
}
```

---

### I-2 — Add upfront guard to `VersionBeaconManager::update()`  *(PRIORITY: MEDIUM)*

**File:** `src/espnow/version_beacon_manager.cpp`  
**Rationale:** Eliminates redundant per-tick function calls while disconnected; makes the disconnect-time behaviour explicit.

```cpp
void VersionBeaconManager::update() {
    if (!EspNowConnectionManager::instance().is_connected()) return;  // ADD THIS LINE

    uint32_t now = millis();
    if (pending_beacon_ && can_send_beacon_now() &&
        (now - last_beacon_ms_ >= MIN_BEACON_INTERVAL_MS)) {
        send_version_beacon(true);
        return;
    }
    if (now - last_beacon_ms_ >= PERIODIC_INTERVAL_MS) {
        send_version_beacon(true);
    }
}
```

---

### I-3 — Remove dead `send_version_beacon(true)` call from `init()`  *(PRIORITY: MEDIUM)*

**File:** `src/espnow/version_beacon_manager.cpp`  
**Rationale:** The call is harmless since `pending_beacon_` mitigation was added, but it is misleading dead code at startup — the receiver cannot receive it.

```cpp
void VersionBeaconManager::init() {
    LOG_INFO("VERSION_BEACON", "Manager initialized");
    // First beacon is sent from TxReconnectManager::handle_confirm_ack() on connection.
    // pending_beacon_ defaults true; no send attempt needed here.
}
```

---

### I-4 — Add explicit connection guard to `send_config_section()`  *(PRIORITY: MEDIUM)*

**File:** `src/espnow/version_beacon_manager.cpp`  
**Rationale:** Defensive guard — protects against future call-chain changes silently sending while disconnected.

```cpp
void VersionBeaconManager::send_config_section(config_section_t section, const uint8_t* receiver_mac) {
    if (!EspNowConnectionManager::instance().is_connected()) {  // ADD THIS
        LOG_WARN("VERSION_BEACON", "send_config_section: not connected, ignoring");
        return;
    }
    LOG_INFO("VERSION_BEACON", "Sending config section: %d", (int)section);
    ...
}
```

---

### I-5 — Route temperature report through `TxSendGuard`  *(PRIORITY: LOW)*

**File:** `src/espnow/heartbeat_manager.cpp`  
**Rationale:** Ensures channel-mismatch events during recovery are detected and counted even for best-effort telemetry payloads.

Replace the direct `esp_now_send()` call in `send_temperature_report()` with:
```cpp
const esp_err_t result = TxSendGuard::send_to_receiver_guarded(
    peer_mac,
    reinterpret_cast<const uint8_t*>(&report),
    sizeof(report),
    "temperature_report"
);
// Treat INVALID_STATE and TIMEOUT as non-fatal (best-effort telemetry)
if (result != ESP_OK && result != ESP_ERR_INVALID_STATE && result != ESP_ERR_TIMEOUT) {
    LOG_WARN("HEARTBEAT", "Temperature report send failed: %s", esp_err_to_name(result));
}
```

---

### I-6 — Fix misleading comment in `DataSender`  *(PRIORITY: LOW)*

**File:** `src/espnow/data_sender.cpp`  
**Rationale:** Comment contradicts the actual `ACTIVE`-state gate.

Replace:
```cpp
// Section 11 Architecture: ALWAYS cache-first (non-blocking)
// - Data flows through EnhancedCache regardless of connection state
```
With:
```cpp
// Architecture: cache-first, connection-gated
// - Data is written to EnhancedCache only when is_transmission_active() == true
//   (state ACTIVE: receiver has sent REQUEST_DATA).
// - Background TransmissionTask drains cache and sends via ESP-NOW.
// - Non-blocking: cache write is < 100µs and does not block the battery emulator.
```

---

### I-7 — Remove `TransmissionSelector::transmit_dynamic_data()` from hot path  *(PRIORITY: LOW)*

**File:** `src/espnow/data_sender.cpp`  
**Rationale:** Removes per-tick dead work. `TransmissionSelector::transmit_dynamic_data()` performs no actual send; it only increments advisory counters.

Delete the following block from `DataSender::send_battery_data()`:
```cpp
// Phase 2: Record route selection for dynamic data (selector is advisory/planning)
char timestamp_str[32];
snprintf(timestamp_str, sizeof(timestamp_str), "%lu", millis());
auto result = TransmissionSelector::transmit_dynamic_data(tx_data.soc, tx_data.power, timestamp_str);
if (result.espnow_sent) {
    LOG_TRACE("DATA_SENDER", "Dynamic data route selected: %s", result.method);
}
```

---

## 7. Issue Status Summary

| ID | Description | Severity | Status |
|---|---|---|---|
| **I-1a** | **RX task not pinned to Core 1** | **CRITICAL — Equal priority with battery on Core 0** | **OPEN** |
| **I-1b** | **Network config task not pinned to Core 1** | **HIGH — Could land on Core 0** | **OPEN** |
| **I-1c** | **`DataSender` + MQTT task not pinned to Core 1** | **HIGH — Core isolation gap** | **OPEN** |
| I-2 | `VersionBeaconManager::update()` lacks disconnect guard | Low — unnecessary CPU work | **OPEN** |
| I-3 | Dead `send_version_beacon(true)` call in `init()` | Low — misleading dead code | **OPEN** |
| I-4 | `send_config_section()` lacks explicit connection guard | Low — brittle but safe today | **OPEN** |
| I-5 | Temperature report bypasses `TxSendGuard` | Low — channel events not counted | **OPEN** |
| I-6 | `DataSender` comment contradicts `ACTIVE`-state gate | Clarity | **OPEN** |
| I-7 | `TransmissionSelector` call in data hot path | Negligible — dead per-tick work | **OPEN** |
| — | `VersionBeaconManager` pending-beacon state loss | **FIXED** (2026-04-21) | CLOSED |
| — | `VersionBeaconManager` event notifications fire unguarded | **FIXED** (2026-04-21) | CLOSED |
| — | `battery_settings` checksum algorithm mismatch | **FIXED** (2026-04-21) | CLOSED |

---

## 8. Summary

**Core Design:** The three-layer send authority stack (is_transmission_active → is_connected → TxSendGuard) is correct. No battery data is sent while disconnected.

**Core Isolation Problem:** The actual task mapping violates the stated design intent. RX, network config, DataSender and MQTT tasks are not pinned to isolated cores; they share Core 0 with the battery emulator (and equal-priority RX task), creating potential scheduling jitter and violating the "battery emulator isolation" design.

**Recommended Action:** Apply fixes I-1a, I-1b, I-1c immediately to restore core isolation. Then apply I-2 through I-7 for code quality cleanup.
```

At `init()` time, no ESP-NOW connection has been established (discovery hasn't started). The `send_version_beacon()` call does check `is_connected()` before the actual `esp_now_send`, so the wire-send is suppressed — **but the side-effect is not clean**: `last_beacon_ms_` is updated to `now`, `prev_mqtt_connected_` and `prev_ethernet_connected_` are stamped with current values, and the PERIODIC_INTERVAL_MS 30-second clock starts ticking from this useless point.

**Impact:** The first genuine periodic beacon after connection is delayed by up to 30 seconds relative to when the connection was established. Any state changes that occurred while disconnected (e.g. MQTT connects before ESP-NOW) will appear as "no change" on the next beacon, because `prev_*` was already synced at init.

**Suggestion:** Remove the `send_version_beacon(true)` call from `init()`. Instead, when the connection manager transitions to `CONNECTED`, call `send_version_beacon(true)` from the connection-state callback in `tx_connection_handler.cpp` (which is the correct trigger point — the receiver has a peer to receive it).

---

### 2.2 `VersionBeaconManager` event notifications fire regardless of connection state

**File:** `src/espnow/version_beacon_manager.cpp`

```cpp
void VersionBeaconManager::notify_mqtt_connected(bool connected) {
    ...
    send_version_beacon(true);  // ← no is_connected() guard before entering send_version_beacon
}

void VersionBeaconManager::notify_ethernet_changed(bool connected) {
    ...
    send_version_beacon(true);  // ← same
}
```

These are called when Ethernet link changes or MQTT connects/disconnects. Both events can (and do) occur while ESP-NOW is not yet connected (e.g., Ethernet comes up before the ESP-NOW discovery completes). The `send_version_beacon()` body guards on `is_connected()` so no wire-send occurs, but again `prev_*` state and `last_beacon_ms_` are silently updated, losing the pending change.

**Impact:** If MQTT connects while ESP-NOW is still in `DISCOVERING`, the receiver will never receive a beacon reflecting `mqtt_connected=true` until the **next** config-version mismatch or the next 30-second periodic — not on the connection that triggers the first beacon.

**Suggestion:** Add a **pending beacon flag**:

```cpp
// In VersionBeaconManager:
bool beacon_pending_ = false;

void VersionBeaconManager::notify_mqtt_connected(bool connected) {
    if (mqtt_connected_ != connected) {
        mqtt_connected_ = connected;
        beacon_pending_ = true;           // mark for delivery when connected
        if (EspNowConnectionManager::instance().is_connected()) {
            send_version_beacon(true);
        }
    }
}

// In update() or on_connected callback:
if (beacon_pending_ && EspNowConnectionManager::instance().is_connected()) {
    send_version_beacon(true);
    beacon_pending_ = false;
}
```

---

### 2.3 `VersionBeaconManager::update()` — periodic beacon burns the rate-limit clock while disconnected

**File:** `src/espnow/version_beacon_manager.cpp`, `update()`

```cpp
void VersionBeaconManager::update() {
    uint32_t now = millis();
    if (now - last_beacon_ms_ >= PERIODIC_INTERVAL_MS) {
        send_version_beacon(true);   // forced, every 30s
    }
}
```

`update()` is called from the main heartbeat/message loop. While disconnected, every 30-second tick calls `send_version_beacon(true)`, which:
1. Updates `last_beacon_ms_` to `now` (resets the 30-second clock)
2. Silently absorbs any state diffs into `prev_*`
3. Does **not** send anything

The result: after connection is established, the next periodic beacon won't fire for up to 30 seconds, even though the receiver needs it immediately.

**Suggestion:** Either skip the `update()` tick entirely when not connected, or move the `last_beacon_ms_` reset to only happen on successful send:

```cpp
void VersionBeaconManager::update() {
    if (!EspNowConnectionManager::instance().is_connected()) return;  // add this
    uint32_t now = millis();
    if (now - last_beacon_ms_ >= PERIODIC_INTERVAL_MS) {
        send_version_beacon(true);
    }
}
```

---

### 2.4 `DataSender` uses `TxStateMachine::is_transmission_active()` — stricter than needed, and misaligned with cache architecture

**File:** `src/espnow/data_sender.cpp`

```cpp
if (state_machine.is_transmission_active()) {
    send_battery_data();   // writes to EnhancedCache
} else {
    LOG_WARN("DATA_SENDER", "Transmission inactive - no ESP-NOW data being sent");
}
```

`is_transmission_active()` returns true only in state `ACTIVE` — meaning the receiver has sent `REQUEST_DATA`. This is more restrictive than `is_connected()`. The consequence is:

- When the connection first becomes `CONNECTED` (before `REQUEST_DATA` is received), data is **not** written to the cache.
- The `TransmissionTask` (which gates on `is_connected()`) will have nothing to send during the `CONNECTED → ACTIVE` window.
- This is probably the intended behavior — but the code comment says "Section 11: ALWAYS cache-first... Data flows through EnhancedCache regardless of connection state", which contradicts the actual `is_transmission_active()` guard.

The `TransmissionTask` **independently** checks `is_connected()` before draining the cache. So the effective gate stack for wire-send is:

```
DataSender (is_transmission_active = ACTIVE)
  └─ EnhancedCache
       └─ TransmissionTask (is_connected = CONNECTED or ACTIVE)
            └─ TxSendGuard (channel coherence check)
                 └─ esp_now_send()
```

The `DataSender` → `EnhancedCache` gate at `is_transmission_active()` is **the binding one** — the `TransmissionTask`'s `is_connected()` guard is effectively redundant since data only reaches the cache when `ACTIVE`. The comment should be corrected, or the architecture clarified.

**Additionally:** `send_battery_data()` also calls `TransmissionSelector::transmit_dynamic_data()` after writing to the cache. This function is documented as "route planning only" but is called for every data tick while `ACTIVE`. It performs no actual send — it only increments stats counters. It adds dead weight per tick at `TRACE` log level and should either be removed or consolidated into a slower diagnostic sample.

---

### 2.5 `HeartbeatManager::send_temperature_report()` — bypasses `TxSendGuard` entirely

**File:** `src/espnow/heartbeat_manager.cpp`

```cpp
const esp_err_t result = esp_now_send(
    peer_mac,
    reinterpret_cast<const uint8_t*>(&report),
    sizeof(report)
);
```

This calls `esp_now_send()` directly, skipping `TxSendGuard::send_to_receiver_guarded()`. The code comments acknowledge this as intentional ("best-effort telemetry"). `send_temperature_report()` is only called from `send_heartbeat()`, which is itself correctly gated by `is_connected()` — so a wire-send while disconnected is **not** possible here.

**However**, the channel coherence check in `TxSendGuard` is also bypassed. If the home channel and peer channel diverge during a recovery window, this bare `esp_now_send()` will silently fail with `ESP_ERR_ESPNOW_SEND_FAIL` without triggering recovery — exactly the failure mode `TxSendGuard` exists to detect. This is low-risk (temperature is optional telemetry) but the behavior should be explicitly documented.

**Suggestion:** Either call `TxSendGuard::send_to_receiver_guarded()` and ignore `ESP_TIMEOUT`/`ESP_ERR_INVALID_STATE` returns (which would preserve the best-effort semantics while keeping the channel coherence path active), or add a comment explicitly noting why channel coherence is intentionally not required here.

---

### 2.6 `send_config_section()` — no explicit connection guard, relies on implicit context

**File:** `src/espnow/version_beacon_manager.cpp`, `send_config_section()`

```cpp
void VersionBeaconManager::send_config_section(config_section_t section, const uint8_t* receiver_mac) {
    LOG_INFO("VERSION_BEACON", "Sending config section: %d", (int)section);
    // ... builds message ...
    TxSendGuard::send_to_receiver_guarded(receiver_mac, ...);
}
```

No `is_connected()` guard. This is safe **in practice** because `send_config_section()` is only called from the `msg_config_section_request` message handler in `message_handler.cpp` — which can only be reached if a message was received (implying a connection exists). But it is not defensively guarded, which is a maintainability risk if the call chain is ever extended.

**Suggestion:** Add an explicit early return:
```cpp
if (!EspNowConnectionManager::instance().is_connected()) {
    LOG_WARN("VERSION_BEACON", "send_config_section: not connected, ignoring");
    return;
}
```
