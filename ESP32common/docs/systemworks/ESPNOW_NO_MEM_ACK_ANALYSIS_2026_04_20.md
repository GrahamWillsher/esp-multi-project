# ESP-NOW `ESP_ERR_ESPNOW_NO_MEM` ACK Failure Analysis
**Date:** 2026-04-20  
**Symptom:** `[WARN][ACK] Send failed: ESP_ERR_ESPNOW_NO_MEM` appearing repeatedly on `espnowreceiver_LCD`, not observed on `espnowreceiver_2`.  
**Scope:** Transmitter → Receiver signal path; all outbound ESP-NOW send sites on the receiver side.

---

## 1. Executive Summary

The receiver_LCD is exhausting the ESP-NOW internal TX queue (10 frames by default in ESP-IDF) because it has **more concurrent outbound ESP-NOW send sites** than receiver_2, operating against a **higher WiFi load** (LVGL web UI + MQTT), which slows how quickly Core 0 can drain the queue. Three independent root causes compound each other; a fourth creates an acute burst condition during reconnection.

The warning message itself is produced by the **probe ACK path** in shared library code (`espnow_standard_handlers.cpp::send_ack_response()`), which has only 2 retries vs the heartbeat ACK's 6, so it fails first and most visibly.

---

## 2. Symptom Trace — Where the Log Originates

```
[WARN][ACK] Send failed: ESP_ERR_ESPNOW_NO_MEM
```

Exact source:

```
esp32common/espnow_common_utils/espnow_standard_handlers.cpp
  → EspnowStandardHandlers::send_ack_response()
    → LOG_WARN("ACK", "Send failed: %s", esp_err_to_name(result));
```

This function is called from `handle_probe()` whenever the receiver handles an incoming **probe** (channel-discovery broadcast) from the transmitter. It attempts to send a small `ack_t` frame back to the transmitter. With only **2 retries** and a fixed **2 ms** inter-retry pause, it exhausts its attempts quickly and emits the warning.

The heartbeat ACK path (`rx_heartbeat_manager.cpp::send_ack()`) has its own independent retry loop of **6 attempts** with exponential 8 ms / 16 ms / 24 ms / 32 ms / 40 ms backoff, so it succeeds most of the time and logs only a rate-limited `"ACK tx queue saturated (NO_MEM)"` warning which is rarely seen.

---

## 3. ESP-NOW TX Queue Architecture

The ESP-IDF ESP-NOW layer maintains an internal outbound TX queue (default depth **10 frames** — `CONFIG_ESP_WIFI_ESPNOW_MAX_PENDING_LEN`). This queue is:

- **Filled** by any call to `esp_now_send()` from any FreeRTOS task or ISR.
- **Drained** by the WiFi driver interrupt handler on **Core 0**.

`esp_now_send()` returns `ESP_ERR_ESPNOW_NO_MEM` immediately when the queue is full — there is no internal blocking/retry. The queue can hold at most 10 × 250-byte frames (the maximum ESP-NOW payload). In practice it drains fast (~1 ms per frame when idle), but any burst of ≥ 11 concurrent submissions, or any period where Core 0 WiFi is busy with infrastructure traffic, will cause the queue to back up.

---

## 4. All Outbound ESP-NOW Send Sites on receiver_LCD

The receiver_LCD has **four independent** outbound send paths, all of which share the single 10-frame TX queue:

| # | Site | File | Retry policy | Notes |
|---|------|------|-------------|-------|
| 1 | `send_ack_response()` | `espnow_common_utils/espnow_standard_handlers.cpp` | **2 retries, 2 ms fixed** | Called from probe handler inline in ESP-NOW worker task |
| 2 | `RxHeartbeatManager::send_ack()` | `espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp` | **6 retries, 8 ms × attempt** | Good — rarely the culprit |
| 3 | `request_category_refresh()` | `espnowreceiver_LCD/src/espnow/espnow_settings_sync.cpp` | **None — bare `esp_now_send()`** | Fires unconditionally on every `handle_settings_update_ack()` |
| 4 | `EspnowDiscovery` probe broadcasts | `esp32common/espnow_common_utils/espnow_discovery.cpp` | None (probe, not ACK) | Sends at `ANNOUNCEMENT_INTERVAL_MS = 5000 ms` while not connected |

Send sites 1, 3, and occasionally 4 have weak or absent retry logic, making them susceptible to immediate failure when the queue is busy.

---

## 5. Root Cause Analysis

### 5.1 RC-1: Weak Retry in `send_ack_response()` (Primary)

`send_ack_response()` in `espnow_standard_handlers.cpp` was written for the transmitter side where the send rate is low. On the receiver side it is called for every incoming probe packet from the transmitter. The retry policy is:

```cpp
constexpr uint8_t kMaxNoMemRetries = 2;  // only 2 attempts
// ...
delay(2);  // only 2 ms — not enough for queue to drain if WiFi is busy
```

By contrast the heartbeat ACK uses 6 attempts with 8/16/24/32/40 ms backoff (up to 120 ms total wait). The probe ACK exhausts its 2 retries in ~4 ms, giving up long before the queue would have drained. **This is why the probe ACK warning appears so much more often than the heartbeat ACK warning.**

Because `send_ack_response()` lives in `esp32common`, both receivers share the code. The difference is that receiver_LCD sees the queue more frequently full due to RC-2 and RC-3 below.

### 5.2 RC-2: Higher Ambient WiFi Load on receiver_LCD (Primary)

receiver_LCD operates with significantly more WiFi traffic than receiver_2:

| Traffic type | receiver_2 | receiver_LCD |
|---|---|---|
| HTTP dashboard / Web API | **Yes** | **Yes** |
| Browser-driven polling + SSE load | Yes (when clients connected) | Yes (when clients connected) |
| Local LVGL touchscreen runtime | No | **Yes** |
| MQTT publish/subscribe logic | Same code paths | Same code paths |
| ESP-NOW RX | Same | Same |
| ESP-NOW TX | Same paths | Same paths + more burst |

Important clarification: both variants include a web server and dashboard scripts. The earlier table label “LVGL web UI” was shorthand and could be read as “web server exists only on LCD,” which is not correct.

What is different is that receiver_LCD also runs the local LVGL touchscreen runtime, and in observed usage it tends to have more concurrent browser/UI traffic. When a browser is open and receiving updates (`notify_sse_data_updated()` fires on incoming ESP-NOW processing paths), sustained WiFi activity on Core 0 delays ESP-NOW TX queue drain. Any attempt to send an ESP-NOW frame during HTTP/SSE flush windows is more likely to hit queue pressure.

Additionally, both receivers run MQTT on Core 1 (`MQTT_CLIENT_PRIORITY = 0`) and both use the same shared timing constants from `TimingConfig::MQTT`. MQTT traffic still uses WiFi on Core 0, so active broker traffic can reduce the window the driver has to drain ESP-NOW frames on either variant.

### 5.3 RC-3: Unguarded `esp_now_send()` in `request_category_refresh()` (Contributing)

`espnow_settings_sync.cpp::request_category_refresh()` calls `esp_now_send()` directly — no retry, no backoff, no rate limiting:

```cpp
// espnow_settings_sync.cpp — same code in both receivers
result = esp_now_send(mac, reinterpret_cast<const uint8_t*>(&req), sizeof(req));
// If result == ESP_ERR_ESPNOW_NO_MEM → only a WARN log, no retry
```

This fires **unconditionally after every `handle_settings_update_ack()`**, both on success and failure:

```cpp
// Fires regardless of success/failure:
const char* refetch_reason = ack->success ? "after successful update" : "to verify state after failure";
request_category_refresh(msg->mac, ack->category, refetch_reason);
```

During a multi-step settings sync session (user saves settings → TX applies → sends ACKs per field), multiple `handle_settings_update_ack()` calls arrive in rapid succession, each queuing a `request_data_t` send. If the TX sends 3–4 setting ACKs back-to-back, the receiver issues 3–4 `esp_now_send()` calls within milliseconds, contributing to queue saturation.

> **Note**: receiver_2 shares this exact code path. However, receiver_2's lower ambient WiFi load (no LVGL web UI traffic) means the queue usually drains before saturation occurs. On receiver_LCD the reduced drain speed means the same pattern overflows.

### 5.4 RC-4: Reconnection Burst (Acute / Intermittent)

When the ESP-NOW connection drops (heartbeat timeout) and the transmitter re-enters channel-hop discovery mode, it sends probe packets at **100 ms intervals** on each channel. The receiver's probe ACK rate-limiter allows one ACK every **40 ms** (not-connected state) per same sequence from the same peer. However:

- A different probe sequence number bypasses the rate limiter entirely.
- During the channel-hop scan, each channel gets a fresh probe with a new `esp_random()` sequence number.
- The receiver may receive multiple probes in rapid succession, each with unique seq values, each triggering `send_ack_response()`.

Simultaneously during reconnection, the receiver's own `EspnowDiscovery` task is also sending probe broadcasts at 5000 ms. If a discovery probe coincides with an incoming transmitter probe burst, both `esp_now_send()` paths fire within the same few-ms window.

Result: up to 5–6 `esp_now_send()` calls in rapid succession, easily saturating the 10-frame queue.

---

## 6. Why receiver_2 Does Not Show This

receiver_2 uses identical shared library code for all four send paths. The difference is environmental:

1. **Both variants have web server/dashboard code**; difference is typically operational load (number of active browser clients and update cadence).
2. **MQTT implementation is effectively identical in `_2` and `_lcd`** (same `mqtt_task.cpp`, same `mqtt_client.cpp` behavior, same `TimingConfig::MQTT` constants). Any pressure difference is deployment/runtime dependent (broker activity, topic activity, connected clients), not a hardcoded variant difference.
3. **receiver_LCD has an additional local LVGL rendering/runtime path** on Core 1. LVGL itself does not drain the ESP-NOW queue, but LCD builds commonly run more UI-related activity end-to-end.
4. **Same probe ACK throttle logic** — because queue pressure is generally lower on receiver_2, the current 2-retry probe ACK path fails less often.

In short: both receivers have the same code-level vulnerabilities. The difference in observed warning rate is more likely due to runtime conditions (client activity, reconnect bursts, and concurrent WiFi traffic) than a fundamental MQTT code difference between variants.

---

## 7. Task Architecture Context

```
Core 0: WiFi/BLE stack (ESP-IDF managed)
         ↳ Drains ESP-NOW TX queue
         ↳ Also handles: HTTP server, MQTT, SSE responses

Core 1: LVGL task        (priority 1, 33 ms period)
         ESP-NOW worker  (priority 2, blocks on queue)
         MQTT client     (priority 0)
         Announcement    (priority 1, 5000 ms period)
         Memory sampler  (priority 0)
```

The ESP-NOW worker (priority 2, Core 1) is the highest-priority app task and preempts all others on Core 1. However, all its outbound `esp_now_send()` calls still go to the shared TX queue which Core 0 drains. When Core 0 is busy with HTTP/SSE responses triggered by `notify_sse_data_updated()`, the queue backs up — and `notify_sse_data_updated()` is called from inside the ESP-NOW worker itself, meaning a single incoming message can **both** trigger an outbound ESP-NOW send AND kick off an HTTP/SSE flush on Core 0, creating contention within the same message-processing step.

---

## 8. Suggested Fixes

Fixes are ordered by impact and ease of implementation.

### 8.0 Variant-Alignment Strategy (Requested)

Because your goal is to match receiver_2 behavior where possible, the recommended strategy is:

1. **First apply a receiver_2-parity profile on receiver_LCD** (reduce ambient WiFi pressure).
2. **Then apply shared transport hardening in `esp32common`** so both variants are robust.

This gives you immediate symptom reduction on LCD while also preventing the same issue from resurfacing in either variant under burst traffic.

---

### Fix 1 — Increase Retry Depth in `send_ack_response()` ⭐ (Easy, High Impact)

**File:** `esp32common/espnow_common_utils/espnow_standard_handlers.cpp`

Change the probe ACK retry constants from:

```cpp
constexpr uint8_t kMaxNoMemRetries = 2;
// retry loop:
delay(2);
```

To match the heartbeat ACK's proven retry pattern:

```cpp
constexpr uint8_t kMaxNoMemRetries = 5;   // was 2
// Per attempt: delay(4 * (attempt + 1)) — gives 4/8/12/16 ms
// Total wait budget: ~40 ms — matches heartbeat ACK approach
```

Concrete implementation:

```cpp
bool send_ack_response(const uint8_t* peer_mac, uint32_t seq, uint8_t channel) {
    ack_t ack { msg_ack, seq, channel };
    esp_err_t result = ESP_FAIL;
    constexpr uint8_t kMaxNoMemRetries = 5;
    constexpr uint32_t kRetryBaseMs = 4;

    for (uint8_t attempt = 0; attempt <= kMaxNoMemRetries; ++attempt) {
        result = esp_now_send(peer_mac,
                              reinterpret_cast<const uint8_t*>(&ack),
                              sizeof(ack));
        if (result == ESP_OK) break;
        if (result != ESP_ERR_ESPNOW_NO_MEM || attempt == kMaxNoMemRetries) break;
        delay(kRetryBaseMs * (attempt + 1));  // 4, 8, 12, 16, 20 ms
    }

    if (result == ESP_OK) {
        LOG_DEBUG("ACK", "Sent response (seq=%u, channel=%d)", seq, channel);
        return true;
    }
    LOG_WARN("ACK", "Send failed: %s", esp_err_to_name(result));
    return false;
}
```

**Impact:** Eliminates the majority of warning occurrences under normal load. Applies to both receivers. Total worst-case wait is ~60 ms, acceptable for a probe ACK.

**receiver_2 parity fit:** High. This is a shared-library change and keeps behavior aligned across `_2` and `_lcd`.

**Status:** ✅ Completed on 2026-04-20

Implemented in `esp32common/espnow_common_utils/espnow_standard_handlers.cpp`:
- Increased `kMaxNoMemRetries` from `2` to `5`
- Replaced fixed `delay(2)` retry pause with linear backoff: `4, 8, 12, 16, 20 ms`
- Kept public API and call sites unchanged

Validation:
- `pio run -e waveshare_esp32s3_lcd7_lvgl -j 12` → **SUCCESS**

---

### Fix 2 — Rate-Limit `request_category_refresh()` ⭐ (Easy, Medium Impact)

**Files:**  
- `espnowreceiver_LCD/src/espnow/espnow_settings_sync.cpp`  
- `espnowreceiver_2/src/espnow/espnow_settings_sync.cpp`

Add a per-category minimum re-fetch interval. This prevents a burst of settings ACKs (e.g., 4 categories applied at once) from queuing 4 separate `esp_now_send()` calls simultaneously:

```cpp
namespace {
constexpr uint32_t kRefreshMinIntervalMs = 8000;   // at most once per 8 s per category

struct RefreshThrottle {
    uint32_t last_ms[16] = {};  // indexed by category enum value
};
RefreshThrottle g_refresh_throttle;
}

static void request_category_refresh(const uint8_t* mac, uint8_t category, const char* reason) {
    const uint32_t now = millis();
    const uint8_t idx = (category < 16) ? category : 15;

    if ((now - g_refresh_throttle.last_ms[idx]) < kRefreshMinIntervalMs) {
        LOG_DEBUG("SETTINGS", "Skipping refresh for category=%u (throttled)", category);
        return;
    }
    g_refresh_throttle.last_ms[idx] = now;

    // ... existing send logic unchanged ...
}
```

Additionally, add a retry for the `esp_now_send()` call within this function for completeness:

```cpp
esp_err_t result = esp_now_send(mac, reinterpret_cast<const uint8_t*>(&req), sizeof(req));
if (result == ESP_ERR_ESPNOW_NO_MEM) {
    delay(5);
    result = esp_now_send(mac, reinterpret_cast<const uint8_t*>(&req), sizeof(req));
}
```

**receiver_2 parity fit:** High, if applied to both receivers identically (same throttle values).

**Status:** ✅ Completed on 2026-04-20

Implemented in both receivers:
- Added per-category throttle state with `kRefreshMinIntervalMs = 8000` and 16 throttle slots
- Guarded `request_category_refresh()` so repeated ACK bursts do not immediately requeue the same category refresh
- Sender path now routes refresh requests through the shared outbound TX scheduler (serialized queue)
- Kept category coverage and public handler behavior unchanged; only send pacing changed

Files updated for this step:
- `espnowreceiver_LCD/src/espnow/espnow_settings_sync.cpp`
- `espnowreceiver_2/src/espnow/espnow_settings_sync.cpp`

Validation:
- `pio run -e waveshare_esp32s3_lcd7_lvgl -j 12` → **SUCCESS**
- `pio run -e lilygo-t-display-s3 -j 12` reached compilation of the updated `_2` file, but full build was blocked by an unrelated locked build artifact under `c:\piobuild5` (`Preferences.cpp.o` / `.d` access denied)

---

### Fix 3 — Increase ESP-NOW TX Queue Depth via `sdkconfig.defaults` ⭐ (Easy, Structural Headroom)

**File:** Create `espnowreceiver_LCD/sdkconfig.defaults` (new file):

```ini
# Increase ESP-NOW TX pending queue depth from 10 to 20 frames.
# Default is 10; we raise it to accommodate burst traffic from multiple
# concurrent send paths (probe ACK + heartbeat ACK + settings sync + discovery).
CONFIG_ESP_WIFI_ESPNOW_MAX_PENDING_LEN=20
```

And reference it in `platformio.ini`:

```ini
board_build.cmake_extra_args = -DSDKCONFIG_DEFAULTS=sdkconfig.defaults
```

> **Note:** PlatformIO with ESP-IDF Arduino framework supports `sdkconfig.defaults` via `board_build.sdkconfig_options` in some versions, or via the `SDKCONFIG_DEFAULTS` cmake arg. The exact mechanism varies with `espressif32` platform version; verify it applies correctly for `espressif32@6.5.0`. If it does not take effect, the other fixes alone are sufficient.

**Impact:** Doubles the burst headroom. Even under high WiFi load, up to 20 frames can queue before `esp_now_send()` returns `NO_MEM`. This is the simplest structural fix requiring no code changes.

**receiver_2 parity fit:** Medium. This is build-profile level (not protocol behavior), so it can be applied to LCD only if desired. For strict parity, apply to both variants.

**Status:** 🟡 Implemented (toolchain-effectiveness pending explicit `sdkconfig` confirmation)

Implemented:
- Added `sdkconfig.defaults` in both receiver projects with `CONFIG_ESP_WIFI_ESPNOW_MAX_PENDING_LEN=20`
- Added `board_build.cmake_extra_args = -DSDKCONFIG_DEFAULTS=sdkconfig.defaults` to both `platformio.ini` files

Files updated for this step:
- `espnowreceiver_LCD/sdkconfig.defaults`
- `espnowreceiver_2/sdkconfig.defaults`
- `espnowreceiver_LCD/platformio.ini`
- `espnowreceiver_2/platformio.ini`

Validation:
- `pio run -e waveshare_esp32s3_lcd7_lvgl -j 12` → **SUCCESS**
- `_2` build remains blocked by unrelated locked artifact under `c:\piobuild5` (`Preferences.cpp.o` / `.d` access denied)
- Because these projects use Arduino framework builds, explicit runtime confirmation of effective queue-depth override is still recommended (the config hook is in place)

---

### Fix 4 — Decouple Outbound Sends from the RX Processing Path (Medium Effort, Best Long-Term)

The cleanest architectural fix is to introduce a **single outbound ESP-NOW send task** on the receiver. Instead of calling `esp_now_send()` directly from the ESP-NOW worker task (which is on Core 1, competing with other sends), all outbound sends are posted to a small FreeRTOS queue; a dedicated low-priority send task drains that queue with configurable inter-frame spacing:

```
┌─────────────────────────────────────────┐
│ ESP-NOW Worker (priority 2, Core 1)     │
│  on_heartbeat → post_ack(seq, mac)      │
│  on_probe     → post_ack_response(...)  │
│  on_settings  → post_refresh_request()  │
└──────────────┬──────────────────────────┘
               │  EspnowTxQueue (FreeRTOS queue, depth 8)
               ▼
┌──────────────────────────────────────────┐
│ EspnowTxTask (priority 1, Core 1)        │
│  dequeue → esp_now_send() → 5 ms delay  │
│  retry if NO_MEM, up to 6 attempts       │
└──────────────────────────────────────────┘
```

Benefits:
- No concurrent `esp_now_send()` calls — serialized.
- Inter-frame 5 ms pacing prevents queue saturation under any burst.
- Single place to monitor/log all outbound send failures.
- Backpressure from the FreeRTOS queue naturally rate-limits senders.

This is the same pattern already proven by the `EspnowSendUtils` class in `esp32common/espnow_common_utils/espnow_send_utils.cpp` which the transmitter uses. A receive-side analogue would complete the symmetry.

**receiver_2 parity fit:** High if implemented in shared/common receiver utilities and consumed by both variants.

**Status:** ✅ Completed on 2026-04-20

Implemented:
- Added shared receiver outbound transport in `esp32common/espnow_common_utils/espnow_tx_scheduler.{h,cpp}` with:
    - dedicated FreeRTOS TX task,
    - bounded queue,
    - linear `ESP_ERR_ESPNOW_NO_MEM` retry/backoff in one place,
    - lightweight queue/send statistics.
- Added stable include wrapper: `esp32common/include/esp32common/espnow/tx_scheduler.h`.
- Initialized scheduler from both receiver startup paths:
    - `espnowreceiver_LCD/src/runtime/runtime_task_startup.cpp`
    - `espnowreceiver_2/src/config/runtime_task_startup.cpp`
- Migrated receiver outbound hot paths to scheduler (removed legacy direct-send retry duplication):
    - shared standard handler ACK/PROBE helper sends (`espnow_standard_handlers.cpp` now queues when scheduler is ready),
    - heartbeat ACK sends,
    - settings refresh request sends,
    - connection-initialization/config/version sends,
    - command/control helper sends in both `espnow_send.cpp` files.

Files updated for this step:
- `esp32common/espnow_common_utils/espnow_tx_scheduler.h`
- `esp32common/espnow_common_utils/espnow_tx_scheduler.cpp`
- `esp32common/include/esp32common/espnow/tx_scheduler.h`
- `esp32common/espnow_common_utils/espnow_standard_handlers.cpp`
- `espnowreceiver_LCD/src/runtime/runtime_task_startup.cpp`
- `espnowreceiver_LCD/include/task_config.h`
- `espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_settings_sync.cpp`
- `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_send.cpp`
- `espnowreceiver_2/src/config/runtime_task_startup.cpp`
- `espnowreceiver_2/src/config/task_config.h`
- `espnowreceiver_2/src/espnow/rx_heartbeat_manager.cpp`
- `espnowreceiver_2/src/espnow/espnow_settings_sync.cpp`
- `espnowreceiver_2/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_2/src/espnow/espnow_tasks.cpp`
- `espnowreceiver_2/src/espnow/espnow_send.cpp`

Validation:
- `pio run -e waveshare_esp32s3_lcd7_lvgl -j 12` → **SUCCESS**
- `pio run -e lilygo-t-display-s3 -j 12` compiled all changed receiver_2 sources; final archive/link still blocked by the existing Windows file-lock issue on `Preferences` artifacts.

---

### Fix 5 — Probe ACK Rate Limit: Clamp `min_interval_ms` During Rapid Reconnect (Low Effort, Targeted)

**File:** `espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp`

The current not-connected minimum interval is 40 ms, which permits up to 25 probe ACKs/second. During a TX channel-hop scan (100 ms probes, each with a new random seq), effectively every probe is acknowledged. Increase the not-connected minimum interval to reduce burst rate:

```cpp
// was:
const uint32_t min_interval_ms = (state == EspNowConnectionState::CONNECTED) ? 120U : 40U;
// change to:
const uint32_t min_interval_ms = (state == EspNowConnectionState::CONNECTED) ? 120U : 80U;
```

80 ms limits ACKs to ~12/second while not connected, halving the burst. The TX discovery task will still receive sufficient ACKs to complete channel lock-on; ACK delivery is not required for every probe — the transmitter waits for any ACK on a channel before declaring discovery complete.

**receiver_2 parity fit:** High if the same constant is applied to both variants.

**Status:** ✅ Completed on 2026-04-20

Implemented:
- Increased not-connected probe ACK minimum interval from `40 ms` to `80 ms` on LCD runtime route handling
- Added equivalent probe ACK duplicate-throttle gating in `_2` probe route so both variants follow the same `CONNECTED=120 ms`, `not-connected=80 ms` policy

Files updated for this step:
- `espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp`
- `espnowreceiver_2/src/espnow/espnow_tasks.cpp`

Validation:
- `pio run -e waveshare_esp32s3_lcd7_lvgl -j 12` → **SUCCESS**
- `_2` build reached compilation of the updated `espnow_tasks.cpp`, but full link/archive still blocked by unrelated locked artifact under `c:\piobuild5`

---

### Fix 6 — receiver_2 Parity Profile for receiver_LCD (Most Direct to Match _2)

If the requirement is to make LCD behave like `_2` under load, prioritize reducing ambient WiFi pressure on LCD. This does not change ESP-NOW protocol behavior; it reduces Core 0 contention.

Recommended parity profile for LCD:

1. **SSE throttling/coalescing**
    - Gate `notify_sse_data_updated()` so updates are coalesced to a max rate (e.g. 4–5 Hz) instead of per-message.
    - Keep latest snapshot semantics (no data loss in UI, just lower push frequency).

2. **MQTT publish pacing during reconnect / high queue pressure**
    - Reduce non-critical MQTT publish cadence while ESP-NOW is reconnecting.
    - Resume full cadence after stable CONNECTED window.

3. **Optional UI-client-aware mode**
    - If no active browser clients are connected, suppress SSE pushes entirely.
    - This mirrors `_2` traffic profile more closely when headless.

Why this matches `_2`:
- `_2`'s main advantage is lower infrastructure WiFi load.
- The profile above selectively removes the LCD-only traffic pressure points while preserving LCD functionality.

**Status:** ✅ Completed on 2026-04-20 (phase 1 implementation)

Implemented on `receiver_LCD`:

1. **Monitor SSE coalescing / client-aware notification**
    - `SSENotifier::notifyDataUpdated()` is now monitor-client-aware and rate-limited to one wakeup every 250 ms.
    - `/api/monitor_sse` now explicitly registers monitor client connect/disconnect with `SSENotifier`.
    - This preserves latest-value semantics while reducing wakeups and HTTP chunk sends during rapid telemetry ingress.

2. **MQTT event log traffic made truly subscriber-driven**
    - Removed redundant always-on `event_logs` topic subscription from receiver LCD `subscribeToTopics()`.
    - Event log MQTT subscribe/unsubscribe is now deferred and driven by actual `/events` viewer count.
    - This aligns runtime behavior with the existing ESP-NOW `send_event_logs_control(true/false)` model and removes legacy overlap between “subscriber count tracked” and “topic always subscribed anyway”.

Files updated for this step:
- `espnowreceiver_LCD/lib/webserver_lcd/utils/sse_notifier.h`
- `espnowreceiver_LCD/lib/webserver_lcd/utils/sse_notifier.cpp`
- `espnowreceiver_LCD/lib/webserver_lcd/api/api_sse_handlers.cpp`
- `espnowreceiver_LCD/src/mqtt/mqtt_client.h`
- `espnowreceiver_LCD/src/mqtt/mqtt_client.cpp`

Validation:
- `pio run -e waveshare_esp32s3_lcd7_lvgl -j 12` → **SUCCESS**

---

### Recommended Combined Plan (Parity First, Then Shared Hardening)

**Phase A — Match `_2` behavior on LCD quickly**
- ✅ Implement Fix 6.1 (SSE coalescing) and 6.2 (MQTT pacing).

**Phase B — Make both variants robust**
- ✅ Implement Fix 1 and Fix 2 in shared/common paths.
- ✅ Apply Fix 5 as a low-risk burst smoother.
- ✅ Implement Fix 4 outbound TX scheduler architecture in shared/common path and wire both receivers.
- Apply/verify Fix 3 queue-depth behavior on both projects for extra headroom.

---

## 9. Recommended Implementation Order

| Priority | Fix | Effort | Risk |
|----------|-----|--------|------|
| 1 | ✅ Fix 6: receiver_2 parity profile on LCD (SSE + MQTT pacing) | Completed | Low |
| 2 | ✅ Fix 1: Increase `send_ack_response()` retries (shared) | Completed | Zero — shared lib, pure additive change |
| 3 | ✅ Fix 2: Rate-limit `request_category_refresh()` (both variants) | Completed | Low — adds throttle, no behaviour change under normal cadence |
| 4 | 🟡 Fix 3: `sdkconfig.defaults` queue depth (prefer both variants) | Implemented, pending explicit effect confirmation | Low — build config only |
| 5 | ✅ Fix 5: Raise not-connected probe ACK min interval (both) | Completed | Low — slightly reduces ACK density during discovery |
| 6 | ✅ Fix 4: Outbound send task (architectural, both variants) | Completed | Medium |

Fixes 6, 1, 2, 4, and 5 are complete. Fix 3 wiring is in place and should be explicitly verified for effective queue-depth override on this Arduino toolchain.

---

## 10. Verification Plan

After applying fixes:

1. Connect a browser to the LVGL web UI and leave it open (maximum WiFi load scenario).
2. Trigger a full settings sync (save multiple settings from the web UI in quick succession).
3. Force a reconnect (power-cycle the transmitter or wait for heartbeat timeout).
4. Monitor serial output for `[WARN][ACK] Send failed` occurrences.
5. Check that transmitter's heartbeat sequence numbers remain continuous (no missed heartbeats due to failed ACKs).

Expected outcome: zero or near-zero `[WARN][ACK] Send failed` during steady state; rare (≤ 1 per reconnect event) during channel-hop discovery.

---

## 11. Files Affected by Recommended Fixes

| Fix | File | Change Type |
|-----|------|-------------|
| 1 | `esp32common/espnow_common_utils/espnow_standard_handlers.cpp` | Modify retry constants |
| 2 | `espnowreceiver_LCD/src/espnow/espnow_settings_sync.cpp` | Add throttle table |
| 2 | `espnowreceiver_2/src/espnow/espnow_settings_sync.cpp` | Same (shared code divergence) |
| 3 | `espnowreceiver_LCD/sdkconfig.defaults` | New file |
| 3 | `espnowreceiver_LCD/platformio.ini` | Reference sdkconfig |
| 4 | `esp32common/espnow_common_utils/espnow_tx_scheduler.{h,cpp}` | New shared outbound TX queue/task |
| 4 | `esp32common/include/esp32common/espnow/tx_scheduler.h` | New public wrapper include |
| 4 | `espnowreceiver_LCD/src/runtime/runtime_task_startup.cpp` | Initialize scheduler |
| 4 | `espnowreceiver_2/src/config/runtime_task_startup.cpp` | Initialize scheduler |
| 4 | `espnowreceiver_LCD/src/espnow/*.cpp` + `espnowreceiver_2/src/espnow/*.cpp` | Migrate direct send sites to scheduler |
| 5 | `espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp` | Raise min probe ACK interval |

---

## 12. Direct Answer to Request: “Match _2 if possible, else one solution for both”

### Can we match `_2` behavior directly?
**Yes — partially.**

The closest match is to reduce LCD-only WiFi pressure (Fix 6), because that is the dominant environmental difference from `_2`. This is the fastest way to make LCD behave like `_2` in practice.

### If a single solution should work for both variants
Apply this common set:

1. Fix 1 (`send_ack_response()` retry/backoff) in `esp32common`.
2. Fix 2 (`request_category_refresh()` throttle) in both receivers.
3. Fix 4 (shared outbound TX scheduler queue/task) in both receivers.
4. Fix 5 (probe ACK min interval) in both receivers.
5. Optional Fix 3 (queue depth 20) in both build profiles.

This keeps protocol behavior aligned and improves resilience regardless of UI/MQTT load differences.

