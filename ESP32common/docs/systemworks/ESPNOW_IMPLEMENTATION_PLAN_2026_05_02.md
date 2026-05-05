# ESP-NOW Robust Communications — Comprehensive Implementation Plan
**Date:** 2026-05-02  
**Status:** ACTIVE IMPLEMENTATION GUIDE  
**Supersedes:** Phase sections 9–12 of `ESPNOW_CONNECTION_ARCHITECTURE_PROPOSAL_2026_05_01.md`  
**Devices in scope:** ESPnowtransmitter2 (Olimex ESP32-PoE), espnowreceiver_2 (LilyGo T-Display S3), espnowreceiver_LCD (Waveshare ESP32-S3 LCD 7")  
**Shared library:** esp32common

---

## 0. Reading Guide

This document is structured as four sequential phases. Each phase is independently deployable — it improves robustness before the next phase starts. Phases must be executed in order because later phases assume earlier ones are in place.

Each phase section contains:
- **Goal** — one sentence on what the phase achieves
- **Files to change** — exact paths, not generic descriptions
- **Step-by-step work items** — numbered, atomic, testable
- **Acceptance criteria** — what must pass before moving on
- **Risk notes** — what can go wrong and how to recover

The shared library (`esp32common`) is consumed by both receivers via `lib_deps`. Changes there affect both receiver builds simultaneously. Test on espnowreceiver_2 first (it has the richer display/log output).

---

## 1. Phase 1 — Hardware Buffer Recovery Escalation  
**Status: ✅ COMPLETE (2026-05-02)**  
**Goal:** Prevent the device from staying stuck in `ESP_ERR_ESPNOW_NO_MEM` indefinitely by escalating from `esp_now_deinit/init` (Level 1) to `esp_wifi_stop/start` (Level 2) before giving up and restarting.

**Why this is Phase 1:** It is a one-file change, zero protocol change, and directly unblocks the current field failure (stuck TX buffer → 25+ minute MQTT blackout).

### 1.1 Files to Change

| File | Project |
|---|---|
| `esp32common/espnow_common_utils/rx_connection_handler.cpp` | esp32common (shared) |

### 1.2 Step-by-Step

**Step 1.2.1 — Understand the current escalation path**

In `RxConnectionHandler::tick()` locate the `ESP_ERR_ESPNOW_NO_MEM` handler block. The current logic is:
```
consecutive_no_mem_count_++
if count >= 5:
    esp_now_deinit()
    esp_now_init()         // Level 1 only
    if reinit_count_ >= 5:
        esp_restart()      // hard restart without Level 2 attempt
```

**Step 1.2.2 — Add a Level 2 counter and the WiFi stop/start recovery**

Modify the escalation block to follow three levels:

```
Level 1 (tries 1–3):
    esp_now_deinit()
    esp_now_init()
    re-add peer (channel=0)
    re-register all callbacks
    
Level 2 (tries 4–6, i.e. three Level 1 failures):
    esp_wifi_stop()
    vTaskDelay(pdMS_TO_TICKS(200))   // allow LMAC DMA to drain
    esp_wifi_start()
    vTaskDelay(pdMS_TO_TICKS(1500))  // wait for AP re-association
    esp_now_init()
    re-add peer (channel=0)
    re-register all callbacks
    post CONNECTION_LOST to EspNowConnectionManager
    (let normal reconnect cycle restart PROBE scanning)

Level 3 (after 3 Level 2 failures, i.e. 9 total Level 1 failures):
    ESP_LOGE(TAG, "All recovery levels exhausted — rebooting")
    esp_restart()
```

**Step 1.2.3 — Add member variables for level tracking**

In `rx_connection_handler.h` (or the `.cpp` anonymous namespace / class body):
```cpp
uint8_t no_mem_l1_count_ = 0;   // Level 1 reinit attempts since last clean connection
uint8_t no_mem_l2_count_ = 0;   // Level 2 WiFi restart attempts since last clean connection
```

Reset both counters when `EspNowConnectionManager` transitions to `CONNECTED`.

**Step 1.2.4 — Ensure peer table is rebuilt after WiFi restart**

After `esp_wifi_start()` the peer table is empty (WiFi restart clears all ESP-NOW state). Call the existing peer-registration helper (currently used in the Level 1 path) and re-register TX's MAC with `channel=0`.

**Step 1.2.5 — Emit structured log lines for each recovery level**

```
[RX_CONN][WARN] NO_MEM Level-1 recovery #1 (esp_now deinit/init)
[RX_CONN][WARN] NO_MEM Level-2 recovery #1 (esp_wifi stop/start, AP rejoin ~1.5s)
[RX_CONN][ERROR] NO_MEM Level-3 — all recovery exhausted, rebooting
```
These are critical for post-mortem. Use `ESP_LOGW` / `ESP_LOGE`.

**Step 1.2.6 — Add a Kconfig / compile-time guard**

In `platformio.ini` for each receiver add:
```ini
build_flags =
    -DRXCONN_NO_MEM_L1_THRESHOLD=3
    -DRXCONN_NO_MEM_L2_THRESHOLD=3
```
Use these symbols in the C++ code so the thresholds are tunable without code edits.

### 1.3 Acceptance Criteria

- [x] Three-level escalation implemented in `rx_connection_handler.cpp`
- [x] `no_mem_l1_count_`, `no_mem_l2_count_`, `no_mem_last_reinit_ms_` member variables added to header (not statics)
- [x] Counters reset to zero on `CONNECTED` transition and in `on_connection_lost()`
- [x] L1: `esp_now_deinit/init` + peer re-registration (up to `RXCONN_NO_MEM_L1_THRESHOLD`, default 3)
- [x] L2: `esp_wifi_stop/start` + 200 ms drain + 1500 ms AP rejoin wait + `esp_now_init` + peer re-registration (up to `RXCONN_NO_MEM_L2_THRESHOLD`, default 3); resets L1 count so L1 is retried before next L2
- [x] L3: `esp_restart` only after all L2 attempts exhausted
- [x] Structured `[STATE_CHANGE]` log lines at each level for post-mortem analysis
- [x] Build-flag overrides (`RXCONN_NO_MEM_L1_THRESHOLD` / `RXCONN_NO_MEM_L2_THRESHOLD`) via `#ifndef` defaults in `.cpp`
- [x] Both `espnowreceiver_2` (2 environments) and `espnowreceiver_LCD` build **SUCCESS** with no new errors

### 1.4 Risk Notes

- **WiFi restart disconnects MQTT:** expected. The MQTT client must have auto-reconnect enabled (verify `mqtt_reconnect_timeout_ms` is set in both receivers). MQTT reconnects in ~2 s — this is the acceptable recovery cost vs. the current 25+ minute blackout.
- **Level 2 takes ~1.5–2 s:** the UI will briefly show "disconnected." Display an explicit "recovering…" state rather than silent stale data.
- **If WiFi AP association fails repeatedly:** the Level 3 restart is the correct response. Do not suppress it.

---

## 2. Phase 2 — Bidirectional Connection Confirmation Handshake  
**Status: ✅ COMPLETE (2026-05-02)**  
**Goal:** Replace the single-ACK "connection" with a confirmed bidirectional handshake (`connect_confirm` / `connect_confirm_ack`) so that neither device can declare CONNECTED until a full TX→RX→TX round-trip has completed.

This eliminates split-brain (TX connected, RX stuck) and stale-session data forwarding.

### 2.1 Files to Change

| File | Project | Change Type |
|---|---|---|
| `esp32common/include/espnow_common.h` | esp32common | Add message types + structs |
| `esp32common/espnow_common_utils/espnow_message_types.h` (or equivalent message enum file) | esp32common | Add enum values |
| `esp32common/espnow_common_utils/rx_connection_handler.h` | esp32common | New method + state |
| `esp32common/espnow_common_utils/rx_connection_handler.cpp` | esp32common | Implement `on_connect_confirm_received()`, new `CONFIRMING` sub-state |
| `esp32common/espnow_common_utils/espnow_standard_handlers.h` | esp32common | Declare new route handler |
| `esp32common/espnow_common_utils/espnow_standard_handlers.cpp` | esp32common | Implement `handle_connect_confirm()` |
| `ESPnowtransmitter2/.../src/espnow/tx_reconnect_manager.h` | ESPnowtransmitter2 | Add `CONFIRMING` state, new timer, counter |
| `ESPnowtransmitter2/.../src/espnow/tx_reconnect_manager.cpp` | ESPnowtransmitter2 | Implement confirm wait + timeout |
| `ESPnowtransmitter2/.../src/espnow/tx_connection_handler.cpp` | ESPnowtransmitter2 | Register `handle_connect_confirm_ack()` route |
| `espnowreceiver_2/.../src/espnow/espnow_tasks.cpp` | espnowreceiver_2 | Register `handle_connect_confirm` route |
| `espnowreceiver_LCD/src/app/espnow_tasks.cpp` (or equivalent) | espnowreceiver_LCD | Register same route |

### 2.2 Step-by-Step

#### 2.2.1 Wire Protocol — New Message Types

In `espnow_common.h` add two new message type constants and their payload structs.

**`msg_connect_confirm` (TX → RX):**
```c
#define MSG_TYPE_CONNECT_CONFIRM   0x10  // choose next available value

typedef struct {
    uint8_t  msg_type;           // MSG_TYPE_CONNECT_CONFIRM
    uint8_t  protocol_version;   // must match receiver's expected version
    uint16_t session_id;         // monotonically increasing per TX boot; wraps OK
    uint8_t  channel;            // channel TX believes the link is on (informational)
    uint8_t  reserved[3];
} __attribute__((packed)) espnow_connect_confirm_t;
```

**`msg_connect_confirm_ack` (RX → TX):**
```c
#define MSG_TYPE_CONNECT_CONFIRM_ACK  0x11

typedef struct {
    uint8_t  msg_type;           // MSG_TYPE_CONNECT_CONFIRM_ACK
    uint8_t  protocol_version;
    uint16_t session_id;         // echo TX's session_id
    uint8_t  rx_status;          // 0=OK, 1=protocol_version_mismatch, 2=not_ready
    uint8_t  reserved[3];
} __attribute__((packed)) espnow_connect_confirm_ack_t;
```

The `session_id` is the key to drop stale retransmissions. The TX increments it on each reboot (persisted in RTC fast memory or NVS). The RX echoes it in the ACK so the TX can match confirm → ACK pairs.

#### 2.2.2 TX Side — `TxReconnectManager` Changes

**Current SCANNING → success path (simplify and extend):**
```
SCANNING
    hop-worker gets PROBE ACK
    → post WORKER_FOUND(ch, mac)
    → currently: register peer, post PEER_REGISTERED → CONNECTED   ← REMOVE THIS
    → NEW: register peer, send connect_confirm, enter CONFIRMING
    
CONFIRMING
    start 2000 ms confirm_timeout timer
    if connect_confirm_ack received (matching session_id):
        cancel timer
        post PEER_REGISTERED → EspNowConnectionManager: CONNECTED
        → enter CONNECTED_STEADY
    if timer fires:
        log "confirm timeout, retry scan"
        return to SCANNING (no backoff increment on first timeout)
        (second consecutive timeout: increment backoff as normal)
```

Add to `TxReconnectManager`:
```cpp
static constexpr uint32_t CONFIRM_TIMEOUT_MS = 2000;
uint16_t session_id_ = 0;        // incremented each time CONNECTED is reached
uint8_t  confirm_retry_count_ = 0;
```

Send `connect_confirm` using the existing `EspnowTxScheduler` with:
- Retry policy: `{retry_interval_ms=400, max_retries=4}` (total window ~1.6 s, within the 2 s timeout)
- Priority: HIGH

#### 2.2.3 TX Side — Route Registration for `connect_confirm_ack`

In `tx_connection_handler.cpp` (or wherever TX routes are registered):
```cpp
route_registry.register_route(MSG_TYPE_CONNECT_CONFIRM_ACK,
    [](const uint8_t* mac, const uint8_t* data, int len) {
        const auto* pkt = reinterpret_cast<const espnow_connect_confirm_ack_t*>(data);
        TxReconnectManager::instance().on_confirm_ack_received(pkt->session_id, pkt->rx_status);
    });
```

#### 2.2.4 RX Side — `ReceiverConnectionHandler` / `handle_connect_confirm()`

In `espnow_standard_handlers.cpp` add:
```cpp
void handle_connect_confirm(const uint8_t* mac, const uint8_t* data, int len) {
    const auto* pkt = reinterpret_cast<const espnow_connect_confirm_t*>(data);
    
    // Version check
    if (pkt->protocol_version != ESPNOW_PROTOCOL_VERSION) {
        send_connect_confirm_ack(mac, pkt->session_id, RX_STATUS_VERSION_MISMATCH);
        ESP_LOGW(TAG, "connect_confirm version mismatch (got %d, expected %d)",
                 pkt->protocol_version, ESPNOW_PROTOCOL_VERSION);
        return;
    }
    
    // Re-register TX peer with channel=0 (follow current radio channel)
    remove_espnow_peer(mac);
    add_espnow_peer(mac, /*channel=*/0, /*encrypt=*/false);
    
    // Clear any stale TX token / session state
    ReceiverConnectionHandler::instance().on_connect_confirm_received(pkt->session_id);
    
    // Send ACK — on send callback SUCCESS, post PEER_REGISTERED
    send_connect_confirm_ack_with_callback(mac, pkt->session_id, RX_STATUS_OK,
        []() {
            ReceiverConnectionHandler::instance().on_confirm_ack_sent();
        });
}
```

In `ReceiverConnectionHandler`:
```cpp
void on_connect_confirm_received(uint16_t session_id);
    // stores session_id, enters CONFIRMING sub-state

void on_confirm_ack_sent();
    // called from send callback on SUCCESS
    // posts PEER_REGISTERED → EspNowConnectionManager: CONNECTED
    // fires send_receiver_initialization_burst() (request_data for power_profile etc.)
    // triggers flush of any queued RX intents (Phase 4)
```

#### 2.2.5 RX Route Registration

In each receiver's `espnow_tasks.cpp`:
```cpp
route_registry.register_route(MSG_TYPE_CONNECT_CONFIRM, handle_connect_confirm);
```

#### 2.2.6 Backward Compatibility

Receivers that have not yet received the Phase 2 update will not have the `MSG_TYPE_CONNECT_CONFIRM` route. Their router will log an "unknown message type" warning and discard it. TX will time out in `CONFIRMING` and return to `SCANNING`. This will cycle until the receiver is updated. **It is not a hard failure** — it degrades to the current behaviour.

Deploy order: update espnowreceiver_2 first, then ESPnowtransmitter2 TX, then espnowreceiver_LCD.

#### 2.2.7 Adjust Connection Timeouts

Update in `esp32common/include/espnow_common.h` or `timing_config.h`:

| Constant | Old | New | Reason |
|---|---|---|---|
| `ESPNOW_CONNECTING_TIMEOUT_MS` | 120,000 ms | 45,000 ms | Full scan + 3 confirm retries ≪ 45 s |
| `confirm_timeout_ms` (new) | — | 2,000 ms | One confirm+ack round-trip |
| `confirm_max_retries` (new) | — | 2 | Before returning to SCANNING |

### 2.3 Acceptance Criteria

- [x] `msg_connect_confirm` / `msg_connect_confirm_ack` message types + packed structs added to `espnow_common.h`
- [x] `ESPNOW_PROTOCOL_VERSION=2`, `CONNECT_CONFIRM_STATUS_OK/VERSION_MISMATCH` constants added
- [x] `TxReconnectManager` gains `CONFIRMING` ManagerState, `CONFIRM_ACK` ReconnectEvent, `session_id_`, `confirm_timeout_until_ms_`, `confirm_retry_count_`, `confirm_peer_mac_`, `confirm_channel_`
- [x] `handle_worker_found()` enters `CONFIRMING` (sends `connect_confirm`, stores context) instead of immediately posting `PEER_REGISTERED`
- [x] `handle_confirm_ack()` re-registers peer with `channel=0`, posts `PEER_REGISTERED` (→ CONNECTED), flushes cache, sends beacon
- [x] `on_confirm_ack_received()` thread-safe queue post from ESP-NOW receive task
- [x] `send_connect_confirm()` direct `esp_now_send` helper (no scheduler, connection-phase only)
- [x] Confirm timeout polled every 50 ms alongside BACKOFF; up to 2 retries before returning to SCANNING
- [x] `msg_connect_confirm_ack` route registered on TX via `register_connect_confirm_ack_route()` (called from constructor)
- [x] `register_standard_connect_confirm_route()` added to shared `rx_route_registry` (single implementation covers both receivers)
- [x] Route registered in `espnowreceiver_2/espnow_tasks.cpp` and `espnowreceiver_LCD/espnow_runtime_routes.cpp`
- [x] `ESPNOW_CONNECTING_TIMEOUT_MS` reduced from 120,000 ms to 45,000 ms
- [x] Backward compatible: old TX ignores new RX (probe → PEER_REGISTERED path still active); old RX silently discards unknown `connect_confirm` message type
- [x] All three projects build **SUCCESS** with no new errors

### 2.4 Risk Notes

- **Session ID wrap-around:** `uint16_t` wraps at 65535. This is safe; the probability of a wrap colliding with an in-flight confirm is negligible.
- **If RX receives `connect_confirm` while already `CONNECTED`:** treat it as an implicit reconnect (re-enter `CONFIRMING` sub-state and re-send ACK). This handles TX-reboot-without-heartbeat-timeout gracefully.
- **`send_connect_confirm_ack_with_callback` callback race:** the callback fires on the ESP-NOW internal task. Ensure `on_confirm_ack_sent()` is thread-safe (post an event to the connection manager's queue rather than calling state-machine methods directly).

---

## 3. Phase 3 — RX State Machine Simplification and MQTT Decoupling  
**Goal:** Remove `RxRadioArbiterFsm` from the connection path. Replace its MQTT gate with a simple connection-state gate. MQTT becomes available whenever WiFi is connected, not just when ESP-NOW is settled.

### 3.1 Context

`RxRadioArbiterFsm` has these states:
```
BOOTSTRAP → STEADY_CONNECTED ↔ RECONNECT_DETECTED ↔ DEGRADED_FALLBACK
                                       ↓
                              ACK_RECOVERY_WINDOW
                                       ↓
                              POST_RECONNECT_SETTLE
                                       ↓
                               STEADY_CONNECTED
```

Its purpose was to block MQTT during reconnect to avoid publishing stale data. With Phase 2 in place, the `CONNECTED` flag is now reliable (confirmed by handshake). The arbiter's states `RECONNECT_DETECTED`, `DEGRADED_FALLBACK`, and `ACK_RECOVERY_WINDOW` are no longer needed — the new `CONFIRMING` phase replaces them.

### 3.2 Files to Change

| File | Project | Change |
|---|---|---|
| `esp32common/espnow_common_utils/rx_radio_arbiter_fsm.h/.cpp` | esp32common | Mark deprecated; strip MQTT gate logic |
| `esp32common/espnow_common_utils/rx_connection_handler.cpp` | esp32common | Drive MQTT permission directly from `EspNowConnectionManager` state |
| `espnowreceiver_2/.../src/mqtt/mqtt_client.cpp` | espnowreceiver_2 | Change MQTT gate to query `EspNowConnectionManager::is_connected()` |
| `espnowreceiver_LCD/src/app/` (MQTT task equivalent) | espnowreceiver_LCD | Same |

### 3.3 Step-by-Step

**Step 3.3.1 — Define the new MQTT permission rule**

MQTT publishing is permitted when: `wifi_connected == true`  
ESP-NOW data is forwarded via MQTT only when: `espnow_connected == true`

These are independent. MQTT can be connected without ESP-NOW (the UI should show "ESP-NOW offline" rather than being entirely dark).

**Step 3.3.2 — Add `EspNowConnectionManager::is_connected()` accessor**

This is likely already present; verify and expose it if not.

**Step 3.3.3 — Replace arbiter gate in MQTT publish path**

Current gating (schematic):
```cpp
if (!arbiter_.mqtt_allowed() || !arbiter_.noncritical_enqueue_allowed()) return;
```

New gating:
```cpp
if (!EspNowConnectionManager::instance().is_connected()) {
    // Queue or discard depending on message priority
    return;
}
```

**Step 3.3.4 — Remove arbiter state transitions triggered by PROBE receipts**

In `handle_probe()` (and anywhere `EV_PROBE_WHILE_PREVIOUSLY_CONNECTED` was fired), remove the arbiter event post. PROBE receipts should not change MQTT state.

**Step 3.3.5 — Retain arbiter for radio pressure feedback only (optional)**

If `RxRadioArbiterFsm` is being used for radio congestion signalling (e.g., throttling non-critical MQTT), retain only that aspect and rename it `TransportPressureFsm` (aligns with proposal §5.11.C). Remove all connection-state semantics from it.

**Step 3.3.6 — Add a visible "ESP-NOW offline" MQTT status topic**

Publish `espnow/status = offline` when `EspNowConnectionManager` leaves `CONNECTED`, and `espnow/status = online` when it enters. This gives the consumer (Home Assistant or similar) a reliable signal without requiring the receiver to go entirely silent.

### 3.4 Acceptance Criteria

+ [x] MQTT gate replaced with `EspNowConnectionManager::instance().is_connected()` in both `mqtt_task.cpp` files — `RxRadioArbiterFsm` completely removed from MQTT task gate path
+ [x] `RxRadioArbiterFsm` no longer drives `RECONNECT_DETECTED`, `DEGRADED_FALLBACK`, `ACK_RECOVERY_WINDOW`, `POST_RECONNECT_SETTLE` transitions from `rx_connection_handler` — all FSM event posting removed
+ [x] `quiet_mode_active()` now returns `!EspNowConnectionManager::instance().is_connected()` — no FSM dependency
+ [x] `control_only_mode` driven directly from connection state: `false` on CONNECTED entry, `true` on CONNECTED→IDLE
+ [x] `reconnect_quiet_window_ms` removed from `ReceiverConnectionHandlerConfig`
+ [x] Reconnect diagnostics repositioned: `begin` on CONNECTED→IDLE, `end` on next CONNECTED entry
+ [x] All three projects (espnowreceiver_2, espnowreceiver_LCD, ESPnowtransmitter2) build SUCCESS
+
+> **Phase 3 completed 2026-05-02.** `RxRadioArbiterFsm` is now decoupled from the connection/MQTT path. The FSM class file is retained but no longer called from `rx_connection_handler`. The MQTT task gates directly on `EspNowConnectionManager::is_connected()`. The `quiet_mode_active()` API is preserved for webserver API handlers but resolves from the connection manager.

### 3.5 Risk Notes

- **First-boot MQTT publishes with no real data:** guard with a `has_received_first_data_packet` flag. Only permit sensor MQTT publish after at least one valid data frame has been received in the current session.
- **If MQTT reconnects faster than ESP-NOW:** this is the correct behaviour. The consumer sees `espnow/status=offline` and can handle it. It is better than the current "no MQTT at all."

---

## 4. Phase 4 — Intent Replay Queue and Stream-Control Completion  
**Goal:** Ensure that operator actions taken during an outage (e.g., enabling event log viewing, requesting cell data) are not silently lost, and complete the TX-side request handling for `subtype_cell_info` and `subtype_events`.

### 4.1 Context

Current gaps (identified in proposal §5.10–5.11):
- Event-log subscribe/unsubscribe messages are sent on demand but not replayed after reconnect. If reconnect happens while an MQTT subscriber is active, the TX never receives the subscribe signal.
- Cell-info streaming start/stop has no ESP-NOW path at all — it relies entirely on MQTT subscription.
- TX `request_data_handlers.cpp` has `subtype_cell_info` and `subtype_events` marked "not implemented yet."

### 4.2 Files to Change

| File | Project | Change |
|---|---|---|
| `esp32common/espnow_common_utils/` (new file) | esp32common | `rx_intent_queue.h/.cpp` — new module |
| `espnowreceiver_2/.../src/mqtt/mqtt_client.cpp` | espnowreceiver_2 | On connected: flush intent queue |
| `espnowreceiver_2/.../src/espnow/espnow_send.cpp` | espnowreceiver_2 | Route outgoing requests through intent queue |
| `ESPnowtransmitter2/.../src/espnow/request_data_handlers.cpp` | ESPnowtransmitter2 | Implement `subtype_cell_info`, `subtype_events` |
| `ESPnowtransmitter2/.../src/espnow/` (new or existing) | ESPnowtransmitter2 | Cell-info and event-log stream managers |

### 4.3 Step-by-Step

#### 4.3.1 RX Intent Queue Module (`rx_intent_queue.h/.cpp`)

This module holds bounded, deduplicated, TTL-limited outgoing requests that should survive a link outage.

```cpp
struct RxIntent {
    uint8_t  msg_subtype;        // e.g., subtype_cell_info, subtype_events
    bool     subscribe;          // true=start/subscribe, false=stop/unsubscribe
    uint32_t enqueued_at_ms;
    uint32_t ttl_ms;             // default 10,000 ms
};

class RxIntentQueue {
public:
    // Add or replace an intent (deduplicated by subtype)
    void upsert(uint8_t subtype, bool subscribe, uint32_t ttl_ms = 10000);
    
    // Called when link becomes CONNECTED — sends all non-expired intents
    void flush(send_fn_t send_fn);
    
    // Called from tick() — expire entries past TTL
    void expire_stale(uint32_t now_ms);
    
    static constexpr size_t MAX_INTENTS = 8;
private:
    RxIntent entries_[MAX_INTENTS];
    size_t   count_ = 0;
};
```

**Key design rules:**
- **Deduplicate by subtype:** a second `upsert(subtype_cell_info, true)` before the first is sent just refreshes the TTL.
- **Last-write wins:** `upsert(subtype_cell_info, true)` followed by `upsert(subtype_cell_info, false)` leaves only the unsubscribe intent.
- **TTL expiry:** if the link is down for longer than `ttl_ms`, the intent is discarded (the operator's UI session has likely ended anyway).
- **Flush on CONNECTED:** call `flush()` from `ReceiverConnectionHandler::on_confirm_ack_sent()` (after Phase 2 CONNECTED is confirmed).

#### 4.3.2 Connect-Time Intent Replay in `mqtt_client.cpp`

On `EspNowConnectionManager::CONNECTED` (via event or callback):
```cpp
// Re-send event log subscription if currently subscribed
if (event_log_subscriber_count_ > 0) {
    intent_queue_.upsert(subtype_events, /*subscribe=*/true, /*ttl_ms=*/0); // 0 = no expiry at connect
}
if (cell_data_subscriber_count_ > 0) {
    intent_queue_.upsert(subtype_cell_info, /*subscribe=*/true, /*ttl_ms=*/0);
}
intent_queue_.flush(espnow_send_fn);
```

This ensures the TX always has the correct subscription state after any reconnect.

#### 4.3.3 TX Side — Implement `subtype_cell_info` in `request_data_handlers.cpp`

```cpp
case subtype_cell_info:
    ESP_LOGI(TAG, "Cell info stream start requested by RX");
    CellInfoStreamManager::instance().start_stream(sender_mac);
    break;
```

Create `CellInfoStreamManager` (or extend `TxStateMachine`) to:
- Periodically send `msg_cell_data` frames containing individual cell voltages and temperatures
- Stop streaming on `msg_abort_data(subtype_cell_info)` or link loss
- Rate-limit to 1 Hz (cell data does not need sub-second resolution)

#### 4.3.4 TX Side — Implement `subtype_events` in `request_data_handlers.cpp`

```cpp
case subtype_events:
    ESP_LOGI(TAG, "Event log stream start requested by RX");
    EventLogManager::instance().set_remote_subscriber(sender_mac, /*active=*/true);
    break;
```

Note: `msg_event_logs_control` handling already exists in `message_routes.cpp`. The `subtype_events` request-data path is an alternative entry point — they should converge to the same `EventLogManager` state.

#### 4.3.5 Abort Data Handling Verification

Confirm that `msg_abort_data(subtype_cell_info)` and `msg_abort_data(subtype_events)` are routed to the new stream managers on the TX side. These are already sent on RX SSE-client disconnect (for power_profile) — extend the pattern to the new subtypes.

### 4.4 Acceptance Criteria

- [ ] After an ESP-NOW reconnect, if a MQTT subscriber to event logs was active before disconnect, event logs resume within 3 s of reconnect without operator action
- [ ] After an ESP-NOW reconnect, if a cell monitor SSE session was open before disconnect, cell data resumes within 3 s of reconnect
- [ ] If the link remains down for > 10 s and no browser session is active, queued intents expire and are not replayed (verify via log)
- [ ] TX `request_data_handlers.cpp` no longer has "not implemented" for `subtype_cell_info` and `subtype_events`
- [ ] A new browser session connecting to `/cellmonitor` triggers the cell-info stream within 500 ms

### 4.5 Risk Notes

- **Cell data volume:** if TX sends all cell voltages + temps at 1 Hz over ESP-NOW, this is ~60–80 bytes/frame. Acceptable. Do not increase rate beyond 2 Hz.
- **Event log flood on reconnect:** if many events accumulated during outage, the TX may send a burst. The RX scheduler should rate-limit this with the existing inter-frame delay.
- **Intent TTL too short:** if the outage lasts longer than `ttl_ms`, operator intent is lost. This is by design — the session has ended. Set default TTL to 15 s (longer than the MQTT reconnect+resubscribe cycle).

---

## 5. Cross-Cutting Changes (All Phases)

These changes are not phase-specific but should be completed alongside the phase work.

### 5.1 Structured Logging Standards

All ESP-NOW state transitions must emit a log line in this format:
```
[COMPONENT][STATE_CHANGE] old_state -> new_state  reason=<reason>  session=<session_id>
```
Example:
```
[TX_RECONNECT][STATE_CHANGE] CONFIRMING -> CONNECTED  reason=confirm_ack_received  session=42
[RX_CONN][STATE_CHANGE] CONFIRMING -> CONNECTED  reason=confirm_ack_sent  session=42
```
This makes post-mortem analysis tractable without a logic analyser.

### 5.2 Firmware Version Coordination

Both the TX and RX already exchange `msg_version_announce` / `msg_version_beacon`. Add a `protocol_version` field (uint8) to these frames. If the protocol version mismatches, the connection proceeds but logs a prominent `WARN`. Do not hard-reject — allow degraded operation with old firmware until both devices are updated.

Bump `protocol_version` to `2` when Phase 2 is deployed.

### 5.3 Configuration Constants to Update

Locate these in `esp32common/include/` or `timing_config.h` and update:

| Constant | Old Value | New Value | Phase |
|---|---|---|---|
| `ESPNOW_CONNECTING_TIMEOUT_MS` | 120,000 ms | 45,000 ms | Phase 2 |
| `RXCONN_NO_MEM_L1_THRESHOLD` | hardcoded 5 | build-flag 3 | Phase 1 |
| `RXCONN_NO_MEM_L2_THRESHOLD` | N/A | build-flag 3 | Phase 1 |
| `confirm_timeout_ms` | N/A | 2,000 ms | Phase 2 |
| `rx_intent_ttl_ms` | N/A | 15,000 ms | Phase 4 |
| `reconnect_quiet_window_ms` | active | removed | Phase 3 |
| `post_reconnect_settle_ms` | active | removed | Phase 3 |

### 5.4 Unit Test Targets

For each phase, the following should be unit-testable without hardware:

| Phase | Test Target | Method |
|---|---|---|
| 1 | `RxConnectionHandler` recovery escalation | Mock `esp_now_deinit`, `esp_wifi_stop`, count calls |
| 2 | `TxReconnectManager` CONFIRMING state and timeout | Mock route registry, inject confirm_ack or let timer fire |
| 2 | `ReceiverConnectionHandler` confirm receipt + ACK send | Mock send function, verify PEER_REGISTERED posted |
| 3 | MQTT gate logic | Mock `EspNowConnectionManager::is_connected()`, verify publish allow/block |
| 4 | `RxIntentQueue` deduplicate, TTL expiry, flush | Pure logic test, no hardware |

The `esp32common/tests/` directory already exists. Add test files there.

### 5.5 OTA Deployment Order

Each phase must be OTA-deployable. The recommended order per phase is:

**Phase 1:** RX devices only (espnowreceiver_2 first, then espnowreceiver_LCD). TX is unchanged.

**Phase 2:** 
1. Deploy updated espnowreceiver_2 (adds route for `connect_confirm`, sends `connect_confirm_ack`)
2. Deploy updated ESPnowtransmitter2 (adds `CONFIRMING` state, sends `connect_confirm`)
3. Deploy updated espnowreceiver_LCD

Between steps 1 and 2: espnowreceiver_2 has the new route but will never receive a `connect_confirm` from the old TX — no regression.

**Phase 3:** RX devices only. TX is unchanged.

**Phase 4:** TX first (add stream handlers), then RX (add intent queue). RX will only queue intents it can actually send; the TX handlers being present first means the first RX-initiated request after reconnect will succeed.

---

## 6. Test Scenario Matrix

These test scenarios must all pass before a phase is considered complete.

| # | Scenario | Expected Result | Phase |
|---|---|---|---|
| T01 | TX power-cycle while RX connected | RX detects heartbeat timeout, TX completes new handshake within 20 s | 2 |
| T02 | RX power-cycle while TX connected | TX detects lost peer, returns to SCANNING, completes handshake on RX boot | 2 |
| T03 | Both devices power-cycled simultaneously | Devices connect within 30 s of both being up | 2 |
| T04 | WiFi AP power-cycled | RX rejoins AP, ESP-NOW reconnects, MQTT reconnects | 1+2 |
| T05 | RF interference (microwave oven / 2.4 GHz congestion) | NO_MEM burst triggers Level 2 recovery, clears within 5 s | 1 |
| T06 | TX sends `connect_confirm`, RX ACK is dropped once | TX retries (within 1.6 s), RX receives retry, connection established | 2 |
| T07 | RX browser opens `/cellmonitor` while ESP-NOW connected | Cell data appears within 500 ms | 4 |
| T08 | RX browser opens `/cellmonitor`, ESP-NOW drops, reconnects | Cell data resumes within 3 s of reconnect | 4 |
| T09 | MQTT event log subscriber active, ESP-NOW drops and reconnects | Event logs resume within 3 s, no operator action required | 4 |
| T10 | ESP-NOW link stays down for 30 s with active browser session | Queued intents expire; on reconnect no phantom subscriptions sent | 4 |
| T11 | Protocol version mismatch (old TX, new RX) | RX logs WARN, degrades to current behaviour, does not crash | 2 |
| T12 | MQTT publishes `espnow/status=offline` then `online` | HA or consumer correctly shows offline/online transitions | 3 |

---

## 7. Definition of Done

The implementation is complete when:

1. **All four phases are deployed** to ESPnowtransmitter2 + espnowreceiver_2 + espnowreceiver_LCD
2. **All T01–T12 test scenarios pass** without manual intervention
3. **No `ESP_ERR_ESPNOW_NO_MEM` burst lasts longer than 5 seconds** in any 24-hour soak test
4. **`RxRadioArbiterFsm` RECONNECT_DETECTED / DEGRADED_FALLBACK states are never entered** in normal operation
5. **MQTT reconnects automatically** after any single-device power cycle within 30 seconds
6. **Cell data and event logs resume automatically** after any reconnect where a subscriber was active
7. **The proposal document** `ESPNOW_CONNECTION_ARCHITECTURE_PROPOSAL_2026_05_01.md` status is updated from `PROPOSAL` to `IMPLEMENTED`

---

## 8. File Change Summary (Quick Reference)

| File | Phase | Change |
|---|---|---|
| `esp32common/espnow_common_utils/rx_connection_handler.cpp` | 1, 2, 3 | Level 2 recovery; `on_connect_confirm_received`; MQTT gate removal |
| `esp32common/espnow_common_utils/rx_connection_handler.h` | 1, 2 | New member vars, new methods |
| `esp32common/espnow_common_utils/espnow_standard_handlers.cpp` | 2 | `handle_connect_confirm()` |
| `esp32common/espnow_common_utils/espnow_standard_handlers.h` | 2 | Declare new handler |
| `esp32common/espnow_common_utils/rx_radio_arbiter_fsm.cpp` | 3 | Strip connection-state logic |
| `esp32common/espnow_common_utils/rx_intent_queue.h` | 4 | New module |
| `esp32common/espnow_common_utils/rx_intent_queue.cpp` | 4 | New module |
| `esp32common/include/espnow_common.h` | 2 | New message types + structs |
| `ESPnowtransmitter2/.../tx_reconnect_manager.h` | 2 | CONFIRMING state, session_id |
| `ESPnowtransmitter2/.../tx_reconnect_manager.cpp` | 2 | Confirm send + timeout handling |
| `ESPnowtransmitter2/.../tx_connection_handler.cpp` | 2 | Register confirm_ack route |
| `ESPnowtransmitter2/.../request_data_handlers.cpp` | 4 | cell_info + events implementation |
| `espnowreceiver_2/.../espnow_tasks.cpp` | 2 | Register connect_confirm route |
| `espnowreceiver_2/.../mqtt_client.cpp` | 3, 4 | MQTT gate; connect-time intent flush |
| `espnowreceiver_2/.../espnow_send.cpp` | 4 | Route requests via intent queue |
| `espnowreceiver_LCD/.../espnow_tasks.cpp` | 2 | Register connect_confirm route |
| `espnowreceiver_LCD/.../` (MQTT task) | 3, 4 | Same as receiver_2 equivalents |
| `esp32common/tests/` (new test files) | 1–4 | Unit tests per §5.4 |

---

*End of implementation plan. Start with Phase 1 (`rx_connection_handler.cpp` Level 2 recovery) — it is the only change needed to unblock the current field failure and can be deployed today.*
