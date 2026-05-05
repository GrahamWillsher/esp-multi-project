# Transmitter: ESP-NOW Send-Without-Connection Analysis
**Date:** 2026-04-21  
**Scope:** ESPnowtransmitter2 — all code paths that issue or attempt to issue an ESP-NOW send  
**Trigger:** Observed `[ERROR][BATTERY] Battery settings: Invalid checksum - message rejected` at the receiver, caused by a pre-existing checksum algorithm mismatch (fixed separately). During investigation, a broader set of send-without-connection issues was identified.

---

## 1. The Two Send Authorities

The transmitter has two parallel "send authorities" whose semantics are not always aligned:

| Authority | What it checks | Enforced by |
|---|---|---|
| `TxStateMachine::is_transmission_active()` | State == `ACTIVE` (receiver has sent `REQUEST_DATA`) | `DataSender` task |
| `EspNowConnectionManager::instance().is_connected()` | Connection manager state == `CONNECTED` | `TransmissionTask`, `HeartbeatManager`, `VersionBeaconManager` (partially) |

`ACTIVE` is a sub-state of `CONNECTED` — so `is_transmission_active()` is a **stricter** gate. The difference matters:

- A device can be `CONNECTED` (peer is registered, channel is locked) but **not** `ACTIVE` (receiver hasn't yet asked for data via `REQUEST_DATA`).
- A device can be `ACTIVE` but the underlying `EspNowConnectionManager` might not yet reflect `CONNECTED` in edge-cases during state transition.

---

## 2. Issues Found

### 2.1 `VersionBeaconManager::init()` — unconditional send attempt at startup

**File:** `src/espnow/version_beacon_manager.cpp`, `init()`

```cpp
void VersionBeaconManager::init() {
    LOG_INFO("VERSION_BEACON", "Manager initialized");
    send_version_beacon(true);   // ← forced beacon at init
}
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

---

## 3. Root Cause Summary

The transmitter's ESP-NOW send paths have **two disconnected connection guards** (`TxStateMachine::is_transmission_active()` and `EspNowConnectionManager::is_connected()`) and **one side-channel bypass** (direct `esp_now_send()` in temperature report). The guards themselves are correct — no actual wire send reaches `esp_now_send()` while disconnected. However, the `VersionBeaconManager` has a **silent state-loss** problem: when events (MQTT state, Ethernet state) occur while disconnected, the `prev_*` state sync runs without a corresponding send, meaning the pending state change is absorbed and lost. The receiver will never receive a beacon reflecting that transient change.

The checksum bug fixed today (`version_beacon_manager.cpp` using `uint16_t` byte-sum instead of CRC32) is a separate, unrelated issue from the above — but it means that even when `send_config_section` fires correctly, the `battery_settings` section it sends was being silently rejected by the receiver.

---

## 4. Recommendations (Priority Order)

| # | Issue | Risk | Effort | Suggested Action |
|---|---|---|---|---|
| 1 | `VersionBeaconManager` pending-beacon loss on disconnect | Medium — receiver misses MQTT/Eth status changes | Low | Add `beacon_pending_` flag; flush on connection |
| 2 | `VersionBeaconManager::update()` burns rate-limit clock while disconnected | Low — 30s delay on first post-connection beacon | Trivial | Add `if (!is_connected()) return;` guard to `update()` |
| 3 | `VersionBeaconManager::init()` sends forced beacon before connection | Low — clock skew at startup | Trivial | Remove `send_version_beacon(true)` from `init()`; drive first beacon from `on_connected` callback |
| 4 | `send_config_section()` lacks explicit connection guard | Low — safe in practice, risky in future | Trivial | Add `is_connected()` early-return |
| 5 | `TransmissionSelector::transmit_dynamic_data()` called per-tick with no effect | Negligible — TRACE-level noise | Low | Remove from `DataSender` hot path or sample at 5-10s interval |
| 6 | Temperature report bypasses `TxSendGuard` channel coherence check | Very low — best-effort telemetry | Low | Document intent explicitly, or route via `send_to_receiver_guarded()` with result ignored on `INVALID_STATE` |
| 7 | `DataSender` comment contradicts `is_transmission_active()` gate | Code clarity only | Trivial | Fix comment to match actual `ACTIVE`-state gate |

---

## 5. Existing Protections That Are Working Correctly

For completeness, these paths are **correctly guarded** and do **not** send when disconnected:

- **`TransmissionTask`** — explicit `is_connected()` gate, correct
- **`HeartbeatManager::tick()`** — checks `EspNowConnectionState::CONNECTED`, correct
- **`TxSendGuard::send_to_receiver_guarded()`** — channel coherence check + recovery guard, correct
- **`VersionBeaconManager::send_version_beacon()`** — checks `is_connected()` before `esp_now_send`, correct (but has the side-effect/state-loss issue described above)
- **`settings_espnow.cpp` ACK/notification** — called only in response to received messages (connection-implied), correct
