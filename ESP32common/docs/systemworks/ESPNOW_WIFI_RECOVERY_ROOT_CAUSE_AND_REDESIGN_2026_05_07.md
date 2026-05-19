# ESP-NOW / WiFi Recovery Root Cause Analysis and Redesign Plan
**Date:** 2026-05-07  
**Scope:** `espnowreceiver_LCD` runtime — ESP-NOW lifecycle, WiFi mode transitions, recovery coordination  
**Status:** Analysis verified against actual framework source (Arduino-ESP32 3.x `WiFiSTA.cpp` / `WiFiGeneric.cpp`).  
**Revision:** v2 — Prior version contained an incorrect mechanism for RC-2 and was missing the primary root cause (RC-NEW). Both corrected here.

---

## 1. Executive Summary

The device reboots every 3–5 minutes whenever the ESP-NOW transmitter is absent or the WiFi link is interrupted. This is caused by **three interlocking bugs**, not two as previously stated.

**Bug 1 (primary):** The L2 WiFi recovery handler calls `esp_wifi_stop()` + `esp_wifi_start()` but **never calls `WiFi.begin()`**. STA stays down indefinitely after L2. This is what causes the 15-second AP fallback timer to always fire.

**Bug 2:** `start_ap_fallback()` calls `WiFi.disconnect(true, true)`. Traced through the actual Arduino-ESP32 3.x framework source, this routes through `enableSTA(false)` → `mode(NULL)` → `espWiFiStop()` → `esp_wifi_stop()`. This destroys the ESP-NOW stack that L2 just restored. No `esp_now_init()` follows.

**Bug 3 (secondary):** `g_radio_initialized` is a one-shot flag that is never reset when ESP-NOW is destroyed, blocking all runtime reinit paths.

**The proposed recovery coordination flag from the v1 document would NOT have worked.** It only guards the ~1.7 s L2 execution window. After L2 completes and the flag clears, STA is still down (Bug 1). The AP fallback fires 13+ seconds later regardless.

**Correct minimum fix — two targeted changes:**
- Change `WiFi.disconnect(true, true)` to `WiFi.disconnect(false, false)` in `start_ap_fallback()`. This prevents `esp_wifi_stop()` from being called and preserves ESP-NOW through any mode change.
- Add a hook to L2 recovery that calls `WiFi.begin()` with stored credentials after `esp_wifi_start()`. This makes STA reconnect without waiting for the AP fallback.

---

## 2. Failure Chain — Step by Step (Verified)

Based on the log at `[0d 00h 50m 25s]`, cross-referenced against actual source code and Arduino-ESP32 3.x framework:

| Time | Event | Source |
|---|---|---|
| t+50m25s | `hb_age=107s` — no transmitter heartbeat for 107 s | `mqtt_task` |
| t+50m31s | ACK watchdog fires; NO_MEM on ESP-NOW send | `espnow_tx_scheduler` |
| t+50m36s | 10 consecutive NO_MEM hits → `tick()` triggers L2 | `rx_connection_handler.cpp` L534 |
| t+50m36s | `esp_wifi_stop()` called in L2 → STA drops | `rx_connection_handler.cpp` L540 |
| t+50m36s | `main.cpp` detects STA down, starts 15 000 ms grace timer | `main.cpp loop()` |
| t+50m37s | `esp_wifi_start()` + `vTaskDelay(1500)` + `esp_now_init()` complete in L2 | `rx_connection_handler.cpp` L542–L544 |
| t+50m37s | **STA is STILL DOWN** — no `WiFi.begin()` was called after `esp_wifi_start()` | **← BUG RC-NEW** |
| t+50m42s | NET log: `mode=STA sta=down` — STA down 6 s after L2 completed | `main.cpp` NET diag |
| t+50m51s | 15 000 ms grace expires — AP fallback fires | `main.cpp loop()` L308 |
| t+50m51s | `WiFi.disconnect(true, true)` called | `wifi_setup.cpp` L122 |
| t+50m51s | → `enableSTA(false)` → `mode(NULL)` → `espWiFiStop()` → `esp_wifi_stop()` | `WiFiSTA.cpp:361`, `WiFiGeneric.cpp:733` |
| t+50m51s | **ESP-NOW destroyed** — no `esp_now_init()` follows anywhere | **← BUG RC-1** |
| t+50m51s | `esp now not init!` from peer manager, probe, TX scheduler | IDF ESPNOW layer |
| t+50m52s | `WiFi.mode(WIFI_AP_STA)` + `WiFi.begin()` → STA reconnects instantly | `wifi_setup.cpp` L123, L132 |
| t+50m55s | 3 000 ms stability window passes → `service_recovery()` returns true | `wifi_setup.cpp` L163 |
| t+50m55s | Reboot | `main.cpp` L321 |

---

## 3. Root Causes (Verified Against Framework Source)

### RC-NEW (Primary) — L2 recovery never re-establishes STA association

**File:** `esp32common/espnow_common_utils/rx_connection_handler.cpp`, L534–L562

The L2 block is:
```cpp
esp_wifi_stop();
vTaskDelay(pdMS_TO_TICKS(200));
esp_wifi_start();
vTaskDelay(pdMS_TO_TICKS(1500));   // waiting, but nothing is reconnecting during this time
const esp_err_t l2_err = esp_now_init();
```

`esp_wifi_start()` brings the WiFi driver up but does **not** initiate STA association. This project uses `WiFi.persistent(false)`, so the IDF WiFi layer has no stored credentials to auto-reconnect from. The 1 500 ms delay is labelled "wait for AP re-association" in a comment but no reconnect is actually occurring during that window.

Result: ESP-NOW is correctly restored after L2. STA is not. STA remains down indefinitely — until `start_ap_fallback()` is triggered 15 s later and calls `WiFi.begin()`, which destroys ESP-NOW in the process (RC-1 below).

This is why the AP fallback fires on every L2 cycle. This bug is the root of the reboot loop.

### RC-1 — `start_ap_fallback()` destroys ESP-NOW via `WiFi.disconnect(true, true)`

**File:** `espnowreceiver_LCD/src/config/wifi_setup.cpp`, L122

```cpp
WiFi.disconnect(true, true);   // wifioff=true, eraseAP=true
WiFi.mode(has_credentials ? WIFI_AP_STA : WIFI_AP);
```

**Traced through Arduino-ESP32 3.x source** (`WiFiSTA.cpp`, `WiFiGeneric.cpp`):

```
WiFi.disconnect(wifioff=true, eraseap=true)
  → WiFiSTAClass::disconnect()           [WiFiSTA.cpp:345]
  → esp_wifi_set_config(clear config)    [eraseAP path]
  → WiFi.enableSTA(false)                [WiFiSTA.cpp:361]  ← wifioff=true path
  → WiFiGenericClass::mode(current & ~STA)
  → since current mode is WIFI_STA, (current & ~STA) = WIFI_NULL
  → (cm && !m) branch → espWiFiStop()   [WiFiGeneric.cpp:1258]
  → esp_wifi_stop()                      [WiFiGeneric.cpp:733]
  ← ESP-NOW destroyed here
```

**The subsequent `WiFi.mode(WIFI_AP_STA)` call does NOT stop WiFi.** Starting from `WIFI_MODE_NULL`, the `mode()` function takes the `(!cm && m)` branch and calls `wifiLowLevelInit()` → `esp_wifi_start()`. This is not a stop/start — it is a first-start from null state.

**Correction from v1 of this document:** v1 stated that `WiFi.mode(WIFI_MODE_APSTA)` internally calls `esp_wifi_stop()` + `esp_wifi_start()`. This is incorrect. The stop is caused by the preceding `WiFi.disconnect(true, true)` call, not by the mode change.

`esp_now_init()` is never called anywhere in `start_ap_fallback()` after the stop, so ESP-NOW remains dead until reboot.

### RC-2 (v1 proposed fix was insufficient) — Recovery flag would not have worked

v1 proposed a `set_recovery_in_progress(true/false)` flag set during L2's execution and a gate in `main.cpp`.

**Why it would not have worked:**
- L2 takes ~1.7 s (200 ms + 1500 ms `vTaskDelay`).
- The flag would be set for that 1.7 s window, then cleared.
- After L2, STA is still down (RC-NEW).
- The AP fallback fires 15 s − 1.7 s = 13.3 s after the flag is cleared.
- The gate adds no protection against that.

The flag only prevents AP fallback from overlapping the 1.7 s execution of L2 itself, which is not the failure point. The failure point is STA being down after L2 completes.

### RC-3 — `g_radio_initialized` is a one-shot flag never reset on radio teardown

**File:** `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp`

`g_radio_initialized` is set to `true` at first init and never reset. After ESP-NOW is destroyed by `esp_wifi_stop()`, any code path guarded by `g_radio_initialized.load()` skips reinit and operates silently on a dead stack. This is a secondary consequence — if RC-NEW and RC-1 are fixed, ESP-NOW is never unknowingly destroyed at runtime — but it should still be fixed as a safety net.

### RC-4 — Stability window too short

`kRecoveryStableConnectMs = 3000` ms. After AP+STA is established and `WiFi.begin()` is called, STA reconnects within 1–2 s on a nearby router. This means the device reboots within ~5 s of AP fallback triggering, every time.

---

## 4. Why Previous Fixes Did Not Help

The webserver session-management fixes (removed forced stop/start on STA recovery, added re-entry guard, made mDNS idempotent) were real and correct but orthogonal to this failure mode. They touch only httpd lifecycle, not the WiFi driver or ESP-NOW stack. The reboot loop continued because neither RC-NEW nor RC-1 was addressed.

---

## 5. Correct Implementation Plan

### Fix A — `WiFi.disconnect(false, false)` in `start_ap_fallback()`

**File:** `espnowreceiver_LCD/src/config/wifi_setup.cpp`, L122

**Change:**
```cpp
// BEFORE:
WiFi.disconnect(true, true);
WiFi.mode(has_credentials ? WIFI_AP_STA : WIFI_AP);

// AFTER:
WiFi.disconnect(false, false);   // Drop STA assoc only — do NOT stop WiFi driver
WiFi.mode(has_credentials ? WIFI_AP_STA : WIFI_AP);
```

**Why this works:** `WiFi.disconnect(false, false)` calls only `esp_wifi_disconnect()` which drops the STA association. It does not call `enableSTA(false)` → `mode(NULL)` → `esp_wifi_stop()`. The subsequent `WiFi.mode(WIFI_AP_STA)` from `WIFI_MODE_STA` then calls `esp_wifi_set_mode(WIFI_MODE_APSTA)` directly — no stop/start involved. **ESP-NOW survives entirely.**

**Note on `eraseAP=true` removal:** The second `true` in the original call cleared the IDF WiFi internal credential storage. This was unnecessary because credentials come from `ReceiverNetworkConfig` (own NVS namespace). Removing it is safe.

This is a 1-line change and is the highest-leverage fix.

### Fix B — Add `on_l2_wifi_restarted` hook so L2 can reconnect STA

**Files:** `esp32common/espnow_common_utils/rx_connection_handler.h`, `.cpp`, and `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp`

Add to `ReceiverConnectionHandlerHooks`:
```cpp
/**
 * @brief Called after L2 esp_wifi_start() + esp_now_init() succeed.
 * The project should call WiFi.begin(ssid, password) here to re-establish
 * STA association. If nullptr, STA stays down after L2 and AP fallback fires.
 */
void (*on_l2_wifi_restarted)(void* context) = nullptr;
```

In `rx_connection_handler.cpp` L2 block, after the successful reinit path:
```cpp
if (l2_err == ESP_OK) {
    // ... existing peer restore + no_mem_l1_count_ = 0 ...
    if (hooks_.on_l2_wifi_restarted != nullptr) {
        hooks_.on_l2_wifi_restarted(hooks_.context);
    }
    LOG_INFO("RX_CONN", "L2 WiFi restart OK ...");
}
```

In `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp`, add to the hooks in `init_state()`:
```cpp
handler_hooks.on_l2_wifi_restarted = [](void*) {
    if (ReceiverNetworkConfig::getSSID()[0] != '\0') {
        WiFi.begin(ReceiverNetworkConfig::getSSID(),
                   ReceiverNetworkConfig::getPassword());
        LOG_INFO("ESPNOW", "L2 hook: WiFi.begin() re-issued for STA re-association");
    }
};
```

### Fix C — Reset `g_radio_initialized` when radio is torn down

**Files:** `espnowreceiver_LCD/src/espnow/espnow_runtime.h`, `.cpp`, and `rx_connection_handler.h`, `.cpp`

Add to `ESPNowRuntime`:
```cpp
void mark_radio_deinit();   // sets g_radio_initialized to false
```

Add to `ReceiverConnectionHandlerHooks`:
```cpp
void (*on_radio_deinit)(void* context) = nullptr;   // called before L1/L2 teardown
```

Call `on_radio_deinit` hook in the connection handler L1 block (before `esp_now_deinit()`) and L2 block (before `esp_wifi_stop()`).

Implement in `espnow_runtime.cpp`:
```cpp
void ESPNowRuntime::mark_radio_deinit() {
    Detail::g_radio_initialized.store(false);
    // g_state_initialized not reset — route tables and handlers are still valid.
}
```

### Fix D — Increase stability window

**File:** `espnowreceiver_LCD/src/config/wifi_setup.cpp`

```cpp
constexpr uint32_t kRecoveryStableConnectMs = 10000;   // was 3000
```

---

## 6. Files to Change

| File | Fix | Change |
|---|---|---|
| `espnowreceiver_LCD/src/config/wifi_setup.cpp` | A, D | `disconnect(true,true)` → `disconnect(false,false)`; `kRecoveryStableConnectMs` 3000 → 10000 |
| `esp32common/espnow_common_utils/rx_connection_handler.h` | B, C | Add `on_l2_wifi_restarted` and `on_radio_deinit` hooks |
| `esp32common/espnow_common_utils/rx_connection_handler.cpp` | B, C | Call hooks after L2 reinit and before L1/L2 teardown |
| `espnowreceiver_LCD/src/espnow/espnow_runtime.h` | C | Declare `mark_radio_deinit()` |
| `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp` | B, C | Implement `on_l2_wifi_restarted` hook; implement `mark_radio_deinit()` |

Estimated total: ~50 lines changed/added across 5 files.

---

## 7. What Is NOT Changed

- ESP-NOW TX scheduler, message router, peer manager — correct.
- L1/L2/L3 escalation thresholds — reasonable.
- Connection manager state machine — correct.
- Heartbeat manager — correct.
- Webserver and MQTT systems — not involved in this failure mode.
- Transmitter side — not involved.
- Webserver session management fixes from earlier session — remain in place and valid.

---

## 8. Testing Criteria

1. L2 fires → `esp_wifi_stop()` → STA drops → `WiFi.begin()` hook called → STA reconnects within ~5 s → AP fallback NOT triggered.
2. AP fallback triggers (router genuinely unreachable) → `WiFi.disconnect(false,false)` → `WiFi.mode(APSTA)` → ESP-NOW intact → no `esp now not init!` errors.
3. Reboot does not occur unless L3 is reached (all L1+L2 exhausted) or router genuinely unreachable for 60+ s.
4. Normal operation: no reboots during 30+ minute idle periods.
5. `NET` diag shows `sta=up` within ~5 s of any L2 recovery event.
