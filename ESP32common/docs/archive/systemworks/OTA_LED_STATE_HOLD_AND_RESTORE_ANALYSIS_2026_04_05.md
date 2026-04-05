# OTA LED State Hold and Restore — Investigation and Analysis
**Date:** 2026-04-05  
**Author:** Copilot investigation  
**Scope:** Both receiver self-OTA (`espnowreceiver_2`) and transmitter OTA (`ESPnowtransmitter2`)

---

## 1. Executive Summary

When an OTA firmware upload is initiated on either device, the physical LED must show a distinct "update in progress" state (BLUE + CLASSIC on the receiver; BLUE + HEARTBEAT on the transmitter). This investigation covers:

- How the pre-OTA LED state is saved and restored on each device.
- The mechanics of the override on each device.
- The full display path from global variable to physical LED.
- What happens when the LED state changes during an OTA in progress.
- Identified risks, gaps, and concrete recommendations.

**Verdict:** Both implementations are functionally correct and safe under normal operating conditions. Four specific risks are identified and ranked; two are high-priority and addressable without major rework.

---

## 2. Wire Format Reference

| Signal   | Wire Value | Meaning |
|----------|-----------|---------|
| **Color** | 0 | RED |
| **Color** | 1 | GREEN |
| **Color** | 2 | ORANGE |
| **Color** | 3 | BLUE |
| **Effect** | 0 | CONTINUOUS (Classic) |
| **Effect** | 1 | FLASH |
| **Effect** | 2 | HEARTBEAT |

Transmitter constants: `LED_WIRE_GREEN`, `LED_WIRE_BLUE`, `LED_WIRE_CONTINUOUS`, `LED_WIRE_FLASH`, `LED_WIRE_HEARTBEAT`.  
Receiver enums: `LEDColor` (RED=0, GREEN=1, ORANGE=2, BLUE=3), `LEDEffect` (LED_EFFECT_CONTINUOUS=0, LED_EFFECT_FLASH=1, LED_EFFECT_HEARTBEAT=2).

---

## 3. LED State Flow — Normal Operation (No OTA)

### 3.1 Transmitter Side
```
datalayer.battery.status          →  get_emulator_status()  →  wire_color_from_status()  ─┐
datalayer.battery.status.led_mode →  wire_effect_from_led_mode()                          ─┼→  led_publish_current_state()
                                                                                            │     │
                                                                               EVENT_OTA_UPDATE  │   (dedup cache: s_last_color / s_last_effect)
                                                                               if active → force HEARTBEAT ┘
                                                                                                 │
                                                                         ESP-NOW flash_led_t packet sent to receiver MAC
```

Key file: `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/led_handler.cpp`

### 3.2 Receiver Display Pipeline
```
ESP-NOW flash_led_t packet
        │
        ▼
handle_flash_led_message()          [espnow_message_handlers.cpp]
        │ (if not blocked by receiver_ota_led_override_active)
        ▼
ESPNow::current_led_color   ─────────────────────────┐
ESPNow::current_led_effect  ─────────────────────────┤
                                                      │
                                                      ▼
                                         task_led_renderer()        [main.cpp]
                                         (FreeRTOS task, runs continuously)
                                                      │
                                           switch(effect):
                                           CONTINUOUS  → set_led(color) [solid on]
                                           FLASH       → toggle at interval
                                           HEARTBEAT   → dual-beat pattern
                                                      │
                                                      ▼
                                              set_led() / clear_led()    [display_led.cpp]
                                              (physically drives the LED via TFT_eSPI)
```

`task_led_renderer` reads `current_led_color`/`current_led_effect` at the top of each iteration. Latency from packet receipt to LED change is at most one render cycle (effectively immediate — sub-100 ms).

---

## 4. Receiver Self-OTA — LED State Management

**Relevant file:** `espnowreceiver_2/lib/webserver/api/api_control_handlers.cpp`

### 4.1 The RAII Guard

`ReceiverOtaLedOverrideGuard` is a RAII struct instantiated at the start of `api_ota_upload_receiver_handler`. It lives for the entire HTTP request handler scope (OTA upload duration):

```cpp
struct ReceiverOtaLedOverrideGuard {
    uint8_t prev_color_ = 0;
    uint8_t prev_effect_ = 0;

    ReceiverOtaLedOverrideGuard() {
        constexpr uint8_t kLedBlue = 3;
        constexpr uint8_t kEffectContinuous = 0;
        prev_color_ = ESPNow::current_led_color;   // ← Snapshot pre-OTA state
        prev_effect_ = ESPNow::current_led_effect;
        ESPNow::receiver_ota_led_override_active = true;  // ← Block incoming packets
        ESPNow::current_led_color = kLedBlue;             // ← Force BLUE
        ESPNow::current_led_effect = kEffectContinuous;   // ← Force CLASSIC (solid)
    }

    ~ReceiverOtaLedOverrideGuard() {
        ESPNow::current_led_color = prev_color_;          // ← Restore snapshot
        ESPNow::current_led_effect = prev_effect_;
        ESPNow::receiver_ota_led_override_active = false; // ← Re-enable incoming packets
    }
};
```

### 4.2 Incoming Packet Blocking

`handle_flash_led_message()` in `espnow_message_handlers.cpp`:

```cpp
void handle_flash_led_message(const espnow_queue_msg_t* msg) {
    if (ESPNow::receiver_ota_led_override_active) {
        return;   // All incoming LED packets silently discarded during receiver OTA
    }
    // ... normal handling: update current_led_color / current_led_effect ...
}
```

Any `msg_flash_led` ESP-NOW packet from the transmitter during a receiver self-OTA is **completely discarded** at the handler level, before `current_led_color`/`current_led_effect` are touched.

### 4.3 LED Appearance During Receiver Self-OTA

| Phase | Color | Effect | Source |
|-------|-------|--------|--------|
| Before OTA | (whatever transmitter sent last) | (whatever transmitter sent last) | TX ESP-NOW packet |
| Guard constructs | BLUE | CLASSIC (solid) | Guard hard-codes values |
| Upload in progress | BLUE | CLASSIC (solid) | Locked — incoming packets blocked |
| OTA failure path | Pre-OTA snapshot | Pre-OTA snapshot | Guard destructs |
| OTA success path | Receiver reboots | — | Fresh boot re-syncs on next TX packet |

### 4.4 What Happens if Transmitter LED State Changes During Receiver Self-OTA

Scenario: During a receiver self-OTA upload, the transmitter's battery goes to fault, which would normally trigger the LED to change to RED + FLASH.

1. Transmitter detects fault → `led_publish_current_state()` called → sends `msg_flash_led` with RED+FLASH.
2. Receiver receives the packet but `handle_flash_led_message()` returns immediately (guard is active).
3. `current_led_color`/`current_led_effect` are NOT updated — LED stays BLUE+CLASSIC.
4. `prev_color_`/`prev_effect_` are also NOT updated — they still hold the pre-OTA snapshot (e.g., GREEN+HEARTBEAT).
5. OTA fails (no reboot): guard destructor restores GREEN+HEARTBEAT — **the fault state is never shown**.
6. OTA succeeds (reboot): receiver comes up fresh, syncs with next TX packet — fault state is picked up correctly.

**Risk rating: MEDIUM** — On the OTA failure path (no reboot), the receiver shows a stale (pre-OTA) LED state. The actual fault state is not visible until the transmitter sends another LED update (which only happens on the next system state change from the transmitter side).

---

## 5. Transmitter OTA — LED State Management

**Relevant files:**  
- `ESPnowtransmitter2/espnowtransmitter2/src/network/ota_upload_handler.cpp`  
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/led_handler.cpp`

### 5.1 No Explicit State Save — Dynamic Recomputation

The transmitter does **not** save/restore LED state explicitly. Instead, LED state is computed on-demand from live system state:

```
color  = wire_color_from_status(get_emulator_status())
effect = wire_effect_from_led_mode(datalayer.battery.status.led_mode)
         + override: EVENT_OTA_UPDATE active → force HEARTBEAT
```

This is intrinsically correct: "restoring" state means clearing the event that caused the override, after which the computation naturally yields the correct pre-OTA result.

### 5.2 OTA Entry / Exit LED Call Points

All calls are `led_publish_current_state(true, nullptr)` — `force=true` bypasses the dedup cache:

| Code Location | LED Action | Resulting LED State |
|--------------|-----------|-------------------|
| `set_event(EVENT_OTA_UPDATE, 0)` + publish | BLUE + HEARTBEAT sent to receiver | OTA indicator shown |
| `fail_ota` lambda — all failure paths | `clear_event(EVENT_OTA_UPDATE)` + publish | Real state recomputed + sent |
| Success path after `Update.end()` | `clear_event(EVENT_OTA_UPDATE)` + publish | Real state recomputed + sent |
| Buffer allocation failure | `led_publish_current_state(true, nullptr)` | Note: this path does NOT call `clear_event` first — see Risk #3 |

### 5.3 Dedup Cache Interaction

The transmitter maintains `s_last_color`/`s_last_effect` to avoid resending unchanged states. With `force=true` on all OTA entry/exit calls, the dedup cache is always bypassed, ensuring the state change is always transmitted regardless of what the cache holds.

After clear: cache is updated to the new (post-OTA) values on the next successful send.

### 5.4 What Happens if System State Changes During Transmitter OTA

#### 5.4.1 Battery/system fault fires mid-upload

1. A fault event fires → `events.level` rises → `bms_status` changes in `datalayer`.
2. **No `led_publish_current_state()` call is made** — the transmitter only calls this at OTA entry/exit.
3. Receiver LED remains BLUE+HEARTBEAT for the entire upload duration (could be 30–60 seconds).
4. OTA exits → `clear_event(EVENT_OTA_UPDATE)` → `led_publish_current_state(true)` → computes current status including the fault → sends to receiver.
5. **The fault state is not lost** — it is correctly published at OTA completion.

**Risk rating: LOW** — The receiver is blind to mid-OTA fault changes during the upload, but the state is correctly published on exit. The operator who initiated OTA is the only one watching, and they know an update is in progress. The fault is picked up within one publish cycle after OTA completes.

#### 5.4.2 `led_mode` changes mid-upload

The effect is forced to HEARTBEAT during OTA regardless of `led_mode`. After OTA exits, `led_publish_current_state()` uses the current `led_mode` value — any mid-OTA `led_mode` change is correctly picked up at exit. No state is lost.

---

## 6. Identified Risks and Gaps

### Risk 1 — Receiver OTA Failure: Stale LED Snapshot (MEDIUM)

**What:** On receiver self-OTA failure (upload rejected, checksum error, `Update.end()` returns false), the receiver does not reboot. The guard destructor restores `prev_color_`/`prev_effect_` which were snapshotted at OTA start time. If the transmitter's LED state changed during the upload (any state transition on the transmitter side), the receiver will show the wrong LED state.

**Persistence:** The stale state persists until the transmitter next calls `led_publish_current_state()` — which only happens on system state changes or explicit `force` calls. In a stable system this could persist indefinitely.

**Recommendation:** After the guard destructs on failure, trigger the transmitter to republish its current LED state. Since there is no request-response LED sync message defined, the simplest fix is to add a periodic LED re-publish timer on the transmitter (e.g., every 60 s), or send a "request LED sync" ESP-NOW message type from receiver to transmitter after OTA failure.

Short-term mitigation: add a one-shot timer in `api_ota_upload_receiver_handler` on the failure path to request the transmitter's current status summary (which includes the LED state).

---

### Risk 2 — `receiver_ota_led_override_active` Uses `volatile`, Not `std::atomic` (LOW-MEDIUM)

**What:** `receiver_ota_led_override_active` is declared `volatile bool`. On the ESP32-S3 (which the receiver uses — dual Xtensa LX7 cores), `volatile` does not provide C++ memory ordering guarantees across cores. The variable is written by the HTTP task (core 0, typically) and read by the ESP-NOW receive task (core 1). A missed write or stale read is theoretically possible.

**Current impact:** In practice this is unlikely to cause issues because the OTA upload is a long-running operation and the cores are synchronized through the FreeRTOS scheduler. However, it is not guaranteed by the language standard.

**Recommendation:** Change declaration to `std::atomic<bool>` and access with `std::memory_order_seq_cst` or `std::memory_order_acq_rel`. In `globals.cpp`:

```cpp
std::atomic<bool> receiver_ota_led_override_active{false};
```

In `common.h`:
```cpp
extern std::atomic<bool> receiver_ota_led_override_active;
```

---

### Risk 3 — Buffer Allocation Failure Path Does Not Clear `EVENT_OTA_UPDATE` (MEDIUM)

**What:** In `ota_upload_handler.cpp` at the buffer allocation failure path (~line 174), `led_publish_current_state(true, nullptr)` is called directly — but `EVENT_OTA_UPDATE` was never set at this point (the buffer failure occurs before `set_event(EVENT_OTA_UPDATE, 0)` at line 225). So the call here is redundant but harmless.

However, if the code path ordering ever changes (set_event moved earlier), this publish would send BLUE+HEARTBEAT to the receiver without a corresponding `clear_event()` on failure — leaving `EVENT_OTA_UPDATE` active permanently.

**Recommendation:** Introduce a helper `begin_ota_led()` and `end_ota_led()` that pair `set_event`/`clear_event` with `led_publish_current_state` as an inseparable unit, making it impossible to publish without the event being in the correct state. Alternatively, guard all `led_publish_current_state` calls on the failure path with an explicit check that `EVENT_OTA_UPDATE` has been cleared first.

---

### Risk 4 — Type Mismatch in `ReceiverOtaLedOverrideGuard` Members (LOW)

**What:** `prev_color_` and `prev_effect_` are declared as `uint8_t` in the guard, while `ESPNow::current_led_color` and `ESPNow::current_led_effect` are `LEDColor`/`LEDEffect` enum types in `common.h`. The `api_control_handlers.cpp` file cannot include `common.h` directly (TFT_eSPI include conflict) so re-declares them as `uint8_t` extern.

The assignment `prev_color_ = ESPNow::current_led_color` implicitly converts `LEDColor` to `uint8_t` (valid since the enum is `uint8_t`-compatible), and the restore `ESPNow::current_led_color = prev_color_` assigns `uint8_t` back to `LEDColor` (valid but bypasses the enum type). This works at runtime but silently disables compile-time type checking.

**Recommendation:** Resolve the TFT_eSPI dependency by moving the `LEDColor`/`LEDEffect` enum definitions to a thin header that does not pull in TFT_eSPI, so all translation units can use the typed versions. Or use a `static_assert(sizeof(LEDColor) == sizeof(uint8_t))` guard to catch any future enum base type changes.

---

### Risk 5 — Concurrent OTA: Receiver + Transmitter Simultaneously (LOW)

**What:** If receiver self-OTA and transmitter OTA are initiated at approximately the same time, the receiver's `receiver_ota_led_override_active` flag blocks the transmitter's OTA-start `msg_flash_led` (BLUE+HEARTBEAT). The receiver shows its own BLUE+CLASSIC instead. Both OTAs proceed and complete independently. No data corruption or crash risk.

**Impact:** The receiver's LED during this scenario will show BLUE+CLASSIC (receiver OTA state), not BLUE+HEARTBEAT (transmitter OTA state). There is no way for an observer to distinguish which device is updating by LED alone.

**Recommendation:** This scenario is unlikely in normal operation. No code change is required, but the behavior should be documented as known. A future enhancement could add an OTA-in-progress flag to the ESP-NOW heartbeat packet so the receiver can show a combined state.

---

## 7. Summary Table

| Aspect | Receiver Self-OTA | Transmitter OTA |
|--------|------------------|-----------------|
| State save mechanism | RAII guard saves `prev_color_`/`prev_effect_` on construction | None — state is computed dynamically |
| OTA LED state | BLUE + CLASSIC (forced into global vars by guard) | BLUE + HEARTBEAT (computed from STATUS_UPDATING + EVENT_OTA_UPDATE) |
| Incoming packet protection | `receiver_ota_led_override_active` flag blocks `handle_flash_led_message()` | N/A — transmitter sends, doesn't receive LED packets |
| State restore mechanism | Guard destructor restores `prev_color_`/`prev_effect_` | `clear_event()` makes `led_publish_current_state()` compute correct post-OTA state |
| On OTA success | Receiver reboots — stale state is moot | `clear_event()` + `led_publish_current_state(force=true)` immediately corrects receiver |
| On OTA failure | Guard destructs — stale pre-OTA snapshot restored | `fail_ota` lambda: `clear_event()` + `led_publish_current_state(force=true)` — correct state published |
| Mid-OTA state change impact | TX packets discarded → stale snapshot on failure | System state changes not reflected until OTA exits — no state lost, just delayed |
| Primary risk | Stale LED state on receiver after OTA failure | None significant; mid-OTA events correctly published on exit |

---

## 8. Recommendations — Prioritised

| Priority | Item | Effort |
|----------|------|--------|
| **High** | Risk 1: Add LED re-sync after receiver OTA failure | Low — add a periodic transmitter LED publish or a sync-request message |
| **High** | Risk 2: Replace `volatile bool` with `std::atomic<bool>` | Low — change declaration in `globals.cpp` and `common.h` |
| **Medium** | Risk 3: Refactor OTA LED calls into paired begin/end helpers | Medium — refactor `ota_upload_handler.cpp` |
| **Low** | Risk 4: Resolve `uint8_t`/enum type mismatch in guard | Medium — requires header restructure to break TFT_eSPI dependency |
| **Low** | Risk 5: Document concurrent OTA LED appearance | Trivial — documentation only |

---

## 9. Files Referenced

| File | Role |
|------|------|
| `espnowreceiver_2/lib/webserver/api/api_control_handlers.cpp` | `ReceiverOtaLedOverrideGuard` RAII struct; receiver OTA HTTP handler |
| `espnowreceiver_2/src/espnow/espnow_message_handlers.cpp` | `handle_flash_led_message()` with early-return guard |
| `espnowreceiver_2/src/main.cpp` | `task_led_renderer` FreeRTOS task — consumes `current_led_color`/`current_led_effect` |
| `espnowreceiver_2/src/globals.cpp` | Definitions of `current_led_color`, `current_led_effect`, `receiver_ota_led_override_active` |
| `espnowreceiver_2/src/common.h` | `extern` declarations for `ESPNow` namespace LED globals |
| `ESPnowtransmitter2/espnowtransmitter2/src/network/ota_upload_handler.cpp` | Transmitter OTA upload handler; all LED call sites |
| `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/led_handler.cpp` | `led_publish_current_state()` implementation; OTA override; dedup cache |

---

## 10. `msg_led_state_request` — Investigation and Recommendation

### 10.1 The Proposed Approach

Instead of saving and restoring `prev_color_`/`prev_effect_` snapshots (which can be stale by the time they are restored), the receiver sends a one-shot ESP-NOW request to the transmitter after each OTA session ends. The transmitter responds with its live current LED state using the existing `msg_flash_led` packet. The receiver needs no snapshot at all.

### 10.2 Infrastructure Already in Place

The request/response pattern is already used in four places in this codebase — no architectural work is needed:

| Request (receiver → TX) | Response (TX → receiver) | Registered in |
|------------------------|--------------------------|--------------|
| `msg_event_log_summary_request` | `msg_event_log_summary` | `message_routes.cpp` line 261 |
| `msg_version_request` | `msg_version_response` | `message_routes.cpp` line 324 |
| `msg_request_battery_types` | `msg_battery_types_fragment` | `message_routes.cpp` |
| `msg_request_type_catalog_versions` | `msg_type_catalog_versions` | `message_routes.cpp` |

The transmitter handler for `msg_event_log_summary_request` is a direct template for the new handler — it is a one-liner that calls the existing publish function:

```cpp
// Existing pattern (event log summary):
register_with_context(msg_event_log_summary_request,
    [](const espnow_queue_msg_t* msg, void* ctx) {
        if (msg->len >= (int)sizeof(event_log_summary_request_t)) {
            send_event_log_summary_to_receiver(msg->mac);
        }
    });

// Proposed new pattern (LED state request):
register_with_context(msg_led_state_request,
    [](const espnow_queue_msg_t* msg, void* ctx) {
        if (msg->len >= (int)sizeof(led_state_request_t)) {
            led_publish_current_state(true, msg->mac);  // force=true, targeted MAC
        }
    });
```

`led_publish_current_state(bool force, const uint8_t* receiver_mac)` already accepts a targeted MAC parameter — it is already able to respond to a specific requester rather than broadcast. No changes to `led_handler.cpp` are required.

For the receiver side, `espnow_send.cpp` already has the canonical pattern for outbound requests (`send_event_logs_control`, `send_battery_types_request`, etc.) including the transmitter-connected check via `RxStateMachine`. Adding `send_led_state_request()` follows the identical pattern.

### 10.3 Comparison: Save/Restore vs Request/Response

| Property | Current save/restore | `msg_led_state_request` |
|----------|---------------------|------------------------|
| Accuracy | Stale — reflects TX state at OTA *start*, not OTA *end* | Exact — reflects TX state at OTA *end* |
| Code complexity | `prev_color_`, `prev_effect_` fields; snapshot on construct; restore on destruct | No snapshot fields; guard just sets/clears flag; send request on exit |
| Handles TX state change during OTA | ❌ No — change is lost | ✅ Yes — response carries post-change state |
| Latency to correct LED state | Immediate (restore is synchronous) but wrong | ~10–50 ms ESP-NOW round trip, correct |
| Failure mode if TX offline | Stale state persists indefinitely | Request silently fails; LED shows globals default (ORANGE+FLASH) until TX reconnects and sends next LED packet |
| Guard destructor complexity | Must restore two state variables + clear flag | Just clears flag |
| Receiver OTA success path | Stale state (moot — reboots) | No request sent (reboots anyway) — correct |
| Receiver OTA failure path | Stale state may persist | Correct state within one round trip |
| Transmitter OTA | Already pushes state proactively — no change needed | No change needed |

### 10.4 Applicability to Each OTA Path

**Receiver self-OTA — success:** Receiver reboots. The request is never sent and not needed — the fresh boot will receive the next periodic LED packet from the transmitter. No code change required on this path.

**Receiver self-OTA — failure:** OTA is rejected or upload errors out. The guard destructs, clearing `receiver_ota_led_override_active`. `send_led_state_request()` is then called from the HTTP handler's failure path. The transmitter responds within one ESP-NOW round trip with its current LED state. `handle_flash_led_message()` processes it normally (flag is now clear). This eliminates Risk 1 entirely.

**Transmitter OTA:** The transmitter already calls `led_publish_current_state(true, nullptr)` on every exit path (`fail_ota`, success, `Update.end()` failure). This is already the "push current state" equivalent of the new approach — it is functionally identical to the receiver requesting it, except the transmitter drives it proactively. No change is needed on this path.

### 10.5 Edge Case: Transmitter Offline at Receiver OTA Failure Time

If the transmitter is unreachable when `send_led_state_request()` is called (e.g., it was also being updated, or Wi-Fi/ESP-NOW link dropped), the send will fail gracefully. The `RxStateMachine` message-state guard in `espnow_send.cpp` returns `false` without error if the transmitter is not in a `VALID` state.

In this case, `current_led_color`/`current_led_effect` will remain at whatever values were in the globals when the guard cleared (BLUE+CLASSIC — the OTA override values). These values will be overwritten by the next `msg_flash_led` from the transmitter as soon as it reconnects. This is a marginal degradation vs the save/restore approach (which would restore a similarly stale pre-OTA snapshot), and recovers at the same point — next TX LED packet.

### 10.6 Concrete Implementation Plan

All changes are small and additive. Estimated scope: 5 files, ~30 lines net new code, ~10 lines deleted.

---

#### Step 1 — `esp32common/espnow_transmitter/espnow_common.h`

Add `msg_led_state_request` to the `msg_type` enum (after `msg_type_catalog_versions`):

```cpp
    msg_type_catalog_versions,         // Current battery/inverter catalog versions

    // LED state sync (receiver → transmitter, response is msg_flash_led)
    msg_led_state_request              // Request transmitter to republish current LED state
};
```

Add the request struct (1-byte, matches existing request patterns):

```cpp
typedef struct __attribute__((packed)) {
    uint8_t type;  // msg_led_state_request
} led_state_request_t;
```

---

#### Step 2 — `ESPnowtransmitter2/.../espnow/message_routes.cpp`

Add the handler registration in `setup_message_routes()`, alongside the other request handlers. Include `led_handler.h` if not already included:

```cpp
register_with_context(msg_led_state_request,
    [](const espnow_queue_msg_t* msg, void* ctx) {
        if (msg->len >= (int)sizeof(led_state_request_t)) {
            led_publish_current_state(true, msg->mac);
        }
    });
```

---

#### Step 3 — `espnowreceiver_2/src/espnow/espnow_send.h` + `espnow_send.cpp`

Add declaration in `espnow_send.h`:

```cpp
/**
 * @brief Request transmitter to republish its current LED state via ESP-NOW.
 * Called after receiver self-OTA failure to avoid showing a stale saved snapshot.
 * Transmitter responds with msg_flash_led containing live state.
 * @return true if request sent successfully, false if transmitter not connected
 */
bool send_led_state_request();
```

Add implementation in `espnow_send.cpp` (follows the identical pattern as `send_battery_types_request()`):

```cpp
bool send_led_state_request() {
    if (RxStateMachine::instance().message_state() != RxStateMachine::MessageState::VALID) {
        LOG_WARN("ESP-NOW", "Transmitter not connected - cannot request LED state");
        return false;
    }

    led_state_request_t req{};
    req.type = msg_led_state_request;

    esp_err_t result = esp_now_send(ESPNow::transmitter_mac,
                                    reinterpret_cast<uint8_t*>(&req), sizeof(req));
    if (result != ESP_OK) {
        LOG_WARN("ESP-NOW", "Failed to send LED state request: %s", esp_err_to_name(result));
        return false;
    }

    LOG_DEBUG("ESP-NOW", "LED state request sent to transmitter");
    return true;
}
```

---

#### Step 4 — `espnowreceiver_2/lib/webserver/api/api_control_handlers.cpp`

**Simplify `ReceiverOtaLedOverrideGuard`** — remove `prev_color_`/`prev_effect_` entirely:

```cpp
struct ReceiverOtaLedOverrideGuard {
    // No saved state — transmitter is asked to republish after OTA ends.

    ReceiverOtaLedOverrideGuard() {
        constexpr uint8_t kLedBlue = 3;
        constexpr uint8_t kEffectContinuous = 0;
        ESPNow::receiver_ota_led_override_active = true;
        ESPNow::current_led_color = kLedBlue;
        ESPNow::current_led_effect = kEffectContinuous;
        LOG_INFO("OTA_RX", "Receiver self-OTA LED override enabled: BLUE + CLASSIC");
    }

    ~ReceiverOtaLedOverrideGuard() {
        ESPNow::receiver_ota_led_override_active = false;
        LOG_INFO("OTA_RX", "Receiver self-OTA LED override disabled");
    }
    // Non-copyable
    ReceiverOtaLedOverrideGuard(const ReceiverOtaLedOverrideGuard&) = delete;
    ReceiverOtaLedOverrideGuard& operator=(const ReceiverOtaLedOverrideGuard&) = delete;
};
```

**In `api_ota_upload_receiver_handler`**, on the failure path (after the guard destructs), call:

```cpp
// Request transmitter to republish its current LED state.
// The guard above has already cleared receiver_ota_led_override_active,
// so the incoming msg_flash_led response will be processed normally.
send_led_state_request();
```

On the success path no call is needed — the receiver reboots immediately.

The `extern uint8_t current_led_color/current_led_effect` re-declarations in this file can also be removed once the guard no longer writes to them directly — the only remaining write is at construction time (BLUE+CLASSIC), which can be kept.

---

### 10.7 Effect on Identified Risks

| Risk | Before | After |
|------|--------|-------|
| Risk 1 — Stale LED on receiver OTA failure | **Medium** — stale snapshot restored | **Eliminated** — transmitter's live state pulled on demand |
| Risk 2 — `volatile bool` vs `std::atomic` | Medium (unchanged — still worth fixing) | Medium — independent of this change |
| Risk 3 — Buffer path / `clear_event` pairing | Medium (unchanged) | Medium — independent |
| Risk 4 — Type mismatch in guard | Low | **Reduced** — `prev_color_`/`prev_effect_` fields removed; fewer casts needed |
| Risk 5 — Concurrent OTA LED appearance | Low (unchanged — cosmetic) | Low — unchanged |

### 10.8 Updated Recommendations — Prioritised

| Priority | Item | Effort |
|----------|------|--------|
| **High** | Implement `msg_led_state_request` as described in §10.6 — replaces Risk 1 fix and simplifies the guard | Low (~30 lines, 5 files) |
| **High** | Risk 2: Replace `volatile bool` with `std::atomic<bool>` for `receiver_ota_led_override_active` | Low |
| **Medium** | Risk 3: Refactor transmitter OTA LED calls into paired begin/end helpers | Medium |
| **Low** | Risk 5: Document concurrent OTA LED appearance | Trivial |
