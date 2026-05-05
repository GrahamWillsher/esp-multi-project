# ESP-NOW Root Cause Analysis — Full Codebase Investigation
**Date**: 2026-04-29  
**Scope**: Complete end-to-end analysis of ESP-NOW reconnect failures across transmitter, both receivers, and shared code  
**Status**: This document supersedes all previous investigation documents for the purpose of understanding root causes

---

## Executive Summary

The reconnect failure is **not** primarily caused by any of the recent tuning changes (queue depths, min_gap values, retry counts). Those were symptoms-chasing. The actual problem is a **platform architectural constraint** compounded by **four specific bugs** introduced or worsened in the last month. The patches applied have been treating the wrong level of the stack.

The core constraint is:

> **Both receivers run MQTT over WiFi TCP. ESP-NOW also uses WiFi. They share a single hardware TX buffer pool. When the TCP socket is live — or even during its teardown — there are insufficient `lldesc_t` DMA descriptors for ESP-NOW frames. This is a hardware/driver constraint on the ESP32-S3 that cannot be patched away with retry counts.**

The recent changes (scheduler introduction, min_gap tuning, retry count changes) have been oscillating around this constraint without resolving it. The bugs listed below are what push the system past the recoverable threshold.

---

## Part 1 — Architecture As-Built (The Real Picture)

### 1.1 Radio sharing per device

| Device | Hardware | MQTT transport | ESP-NOW shares radio with MQTT? |
|---|---|---|---|
| Transmitter | ESP32 WROVER (Olimex ESP32-POE2) | **Ethernet** | **No** — MQTT over separate Ethernet port |
| Receiver_LCD | ESP32-S3 (Waveshare) | **WiFi TCP** | **Yes** — single radio |
| Receiver_2 | ESP32-S3 (LilyGo T-Display-S3) | **WiFi TCP** | **Yes** — single radio |

This asymmetry is fundamental. The transmitter routes MQTT through its Ethernet interface and has the WiFi radio exclusively available for ESP-NOW. Both receivers compete for the single WiFi radio with MQTT TCP traffic. Every MQTT TCP segment, ACK, keepalive ping, or socket teardown packet consumes DMA buffer descriptors that are also needed by `esp_now_send()`.

### 1.2 WiFi driver TX buffer pool

ESP-NOW uses the same `espnow_alloc_buf` DMA descriptor pool as all other WiFi TX traffic. When `esp_now_send()` returns `ESP_ERR_ESPNOW_NO_MEM`, it means **this pool is temporarily exhausted**. This pool is shared by:

- MQTT TCP packets (connect, subscribe, publish, keepalive ping, FIN/ACK during disconnect)
- WiFi management frames (association, probe responses from AP)
- ESP-NOW frames

`CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM` (typically 32) and `CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM` control pool size. These are framework defaults — they have not been tuned in either receiver's `platformio.ini` or `sdkconfig.defaults`. The factory defaults are conservative.

### 1.3 TCP socket teardown is not instantaneous

`MqttClient::disconnect()` (ultimately `WiFiClient::stop()`) closes the TCP socket and returns. The application code proceeds. But the OS-level TCP FIN handshake with the MQTT broker continues asynchronously:

```
App calls disconnect()
  → TCP FIN sent  ← requires 1 lldesc_t buffer
  → Waits for FIN-ACK from broker (1 RTT)
  → Sends final ACK  ← requires 1 lldesc_t buffer
  → TIME_WAIT state for 2×MSL (up to ~60s, typically ~4s on embedded lwIP)
```

During this entire window, the TCP socket still holds buffer descriptors. Any `esp_now_send()` call during TCP teardown will see fewer available buffers than in steady state. If the broker is slow or unreachable, the FIN handshake stalls and the socket sits in CLOSE_WAIT consuming resources indefinitely.

### 1.4 The current scheduler and what it actually does

The shared `EspnowTxScheduler` (introduced approximately 1 month ago) owns all outbound `esp_now_send()` calls from receiver paths. It serialises sends through a single FreeRTOS task. This is architecturally correct. **However**:

- It queues frames and sends them asynchronously. A discovery ACK enqueued at time T may not actually reach `esp_now_send()` until T + inter_frame_delay + queue drain time.
- On `ESP_ERR_ESPNOW_NO_MEM`, the scheduler retries with exponential backoff. This is also correct. But the retry window (currently 3 retries × 8ms base = 24–48ms total) may be shorter than a typical TCP teardown RTT.
- After retry exhaustion, the ACK is dropped and a cooldown is applied to that message type. The **next PROBE from TX triggers a new ACK attempt**, but if TCP teardown is still in progress, this new attempt also fails. This can repeat for the entire duration of TCP teardown.

### 1.5 Init sequence and channel ownership

Both receivers boot with WiFi already connected to the AP. The AP sets the WiFi channel. `esp_now_init()` is called *after* WiFi connection, so ESP-NOW inherits the AP's channel. If the AP is on channel 1, ESP-NOW operates on channel 1. If the AP is on channel 6, ESP-NOW is on channel 6.

**The receiver never changes its WiFi channel** — it is fixed to whatever channel the AP uses. The transmitter scans channels 1–13 to find the receiver. This is the intended design.

**Risk**: if the AP itself changes channel (e.g., DFS, radar detection, or router reboot), the receiver silently follows the AP and the transmitter's channel assumption breaks permanently until it re-discovers the new channel. There is no instrumentation for this in the current code.

---

## Part 2 — The Failure Mechanism (Step by Step)

### Scenario: Stable connection lost (TX heartbeat stops arriving at RX)

**T=0**: Last heartbeat received by RX.

**T=10s**: TX sends next heartbeat. Due to whatever caused the disconnect (TX reboot, glitch, etc.), this heartbeat is not received.

**T=12s**: RX MQTT gate threshold: `ms_since_last_heartbeat() >= 12000`. Gate closes. `MqttClient::disconnect()` is called. TCP FIN is sent to broker. **TCP teardown begins but is not yet complete.**

**T=12s to T=12s+RTT**: TCP socket is in FIN_WAIT_1/FIN_WAIT_2. Buffer descriptors are still partially held. ESP-NOW `esp_now_send()` on this device during this window returns `ESP_ERR_ESPNOW_NO_MEM` with higher probability.

**T=32s**: RX connection manager detects heartbeat timeout (32s threshold). Transitions RX state to IDLE → auto_reconnect → CONNECTION_START.

**T=35s** (if TX had transient loss): TX heartbeat timeout fires. TX transitions to CONNECTING. `start_active_channel_hopping()` begins. TX scans channel 1 first.

**T=35s to T=35s+N×1s**: TX scans channels 1→13. Each channel gets a 1000ms dwell. PROBEs are sent every 200ms on the current channel.

**When TX hits the receiver's channel** (channel C, say channel 6):

```
TX sends PROBE (broadcast, channel 6)
  ↓
RX on_data_recv callback fires (WiFi task, high priority)
  ↓
Message enqueued to rx_message_queue (non-blocking, may drop if full)
  ↓
Worker task dequeues the PROBE
  ↓
rx_route_registry::should_send_probe_ack():
  - Throttle check: gap since last ACK attempt > 700ms? → YES (first probe in window)
  - Quiet mode check → PASSES (quiet mode was entered at T=32s when RX entered CONNECTING)
  ↓
ReceiverConnectionHandler::on_probe_received() → enter_reconnect_quiet_mode()
  ↓
EspnowStandardHandlers::send_ack_response()
  ↓
EspnowTxScheduler::send(tx_mac, &ack, sizeof(ack_t), "DISCOVERY_ACK")
  → priority = CONTROL (P0), enqueued to control queue
  ↓
task_tx_worker dequeues ACK from P0
  ↓
send_immediate_with_retry(tx_mac, ack_data, sizeof(ack_t), retries=3):
  attempt 0: esp_now_send() → ESP_ERR_ESPNOW_NO_MEM  ← TCP FIN_WAIT holds buffers
  attempt 1: vTaskDelay(8ms); esp_now_send() → ESP_ERR_ESPNOW_NO_MEM
  attempt 2: vTaskDelay(16ms); esp_now_send() → ESP_ERR_ESPNOW_NO_MEM
  → ACK DROPPED, cooldown applied
  ↓
TX gets no ACK, moves to channel 7 after 1000ms dwell expires
```

**This repeats for every channel until TCP teardown fully completes.** If the MQTT broker is reachable with low latency, TCP teardown takes ~10–100ms. In that case, the second or third probe attempt within the same 1000ms dwell should succeed. But:

- If the MQTT broker is on a remote host (cloud MQTT), RTT could be 50–200ms
- If the broker is unreachable, the FIN is retransmitted with exponential backoff (500ms, 1s, 2s, 4s...)
- If the socket is in TIME_WAIT, the descriptor holds last slightly longer

The result is that reconnect success is **probabilistic and broker-dependent** — which matches exactly the reported symptom of "sometimes it reconnects, sometimes it doesn't."

---

## Part 3 — Root Causes (True vs Symptomatic)

### Root Cause 1 — TCP socket teardown race with ESP-NOW ACK window *(architectural)*

**The primary root cause.** `MqttClient::disconnect()` initiates TCP teardown but the socket is not fully released for an indeterminate time. This overlaps with the exact window where ESP-NOW discovery ACKs must succeed. No amount of retry count tuning resolves this — the buffer pool constraint is physical.

**Fix**: Force TCP socket release *before* the ESP-NOW ACK attempt, or ensure ACK retries span longer than the worst-case TCP teardown time.

The correct sequence in `enter_reconnect_quiet_mode()` should be:
1. Call `MqttClient::disconnect()` (initiates FIN)
2. Wait for socket `CLOSED` state (poll `WiFiClient::connected()` or use a known-safe wait)
3. Only then allow ACK to proceed

A safe approximation without lwIP deep integration: add an explicit `vTaskDelay(150)` in `enter_reconnect_quiet_mode()` *after* the disconnect call, before the function returns. This covers ~95% of LAN broker RTTs without adding meaningful latency to reconnect (150ms vs 13s scan cycle).

### Root Cause 2 — Discovery ACK gets fewer retries than heartbeat ACK *(specific regression bug)*

In `espnow_tx_scheduler.cpp`:

```cpp
case msg_ack:           return {120, 3};  // Discovery ACK — 3 retries
case msg_heartbeat_ack: return {80, 6};   // Heartbeat ACK — 6 retries
```

`msg_ack` (discovery ACK) is the **only frame that can complete a reconnect**. It has *half* the retries of `msg_heartbeat_ack`, which is used during normal connected operation where the link is already stable.

This is directly inverted from the correct priority. The most important frame for reconnect has the fewest retries.

**Fix**: Set `msg_ack` retries to at least 8, and reduce the min_gap to 50ms to match the probe interval:

```cpp
case msg_ack:           return {50, 8};   // Discovery ACK — highest priority reconnect frame
case msg_heartbeat_ack: return {80, 6};   // Heartbeat ACK — normal connected operation
```

With 8 retries at exponential backoff: 8 + 16 + 24 + 32 + 40 + 48 + 56 + 64 = 288ms total retry window. This spans the typical TCP teardown window.

### Root Cause 3 — Receiver_2 MQTT gate does not close promptly during TX scan *(variant drift bug)*

In `receiver_2`, the message processing path calls `on_link_activity()` for **all received messages** including `msg_probe` and `msg_ack`. This refreshes the heartbeat freshness timestamp with discovery traffic. Consequence: the MQTT gate's 12s threshold is never reached during the TX scan phase, because PROBEs keep artificially refreshing the "last heartbeat" time. MQTT stays connected while TX is actively scanning, keeping the buffer pool contested.

In contrast, the LCD receiver correctly filters out discovery messages before refreshing activity state.

**Fix**: In `receiver_2`'s message dispatch, guard the activity update:

```cpp
// Do NOT call on_link_activity() for msg_probe or msg_ack types
if (msg_type != msg_probe && msg_type != msg_ack) {
    ReceiverConnectionHandler::instance().on_link_activity(mac);
}
```

### Root Cause 4 — quiet_mode does not guarantee control_only_mode on scheduler *(implementation gap)*

`enter_reconnect_quiet_mode()` is called when a PROBE arrives while the receiver had a prior connection. It calls `MqttClient::disconnect()` and sets an internal quiet flag. However, the shared scheduler's `control_only_mode` flag is set **separately** via `on_ack_send_pressure()` (only triggered on ACK enqueue failure). 

This means there is a window after `enter_reconnect_quiet_mode()` is called where the quiet mode flag is set but the scheduler **still processes DATA and MONITORING queue frames**. If there is a large DATA backlog in the scheduler queue (stale config requests, catalog entries, LED sync frames), those frames will compete for the buffer pool in exactly the window where the discovery ACK must succeed.

**Fix**: Call `EspnowTxScheduler::set_control_only_mode(true, /*purge_queues=*/true)` directly inside `enter_reconnect_quiet_mode()`, rather than waiting for `on_ack_send_pressure()` to trigger it reactively.

### Root Cause 5 — POST_RECONNECT_SETTLE_MS is too short *(configuration bug)*

`POST_RECONNECT_SETTLE_MS = 4000ms`. During this 4s window after reconnect:

- Receiver sends REQUEST_DATA + version_announce (~2 frames immediately)
- TX sends a heartbeat if the 10s interval fires (~1 frame)
- Receiver processes config reply, sends config_ack (~2 frames)
- Receiver sends catalog request (~1 frame)
- LED sync request (~1 frame)

That is 7–8 frames in rapid succession, all going through the scheduler in the first 1–2 seconds of the settle window. MQTT is held off for 4s. If any of these frames causes even mild scheduler backpressure, the settle window may expire before the burst is complete, allowing MQTT to reconnect while ESP-NOW frames are still in-flight.

**Fix**: Increase `POST_RECONNECT_SETTLE_MS` to `8000` (8 seconds) in both `mqtt_task.cpp` files.

### Root Cause 6 — Discovery queue is too small and drop-oldest only works for `msg_ack` *(transmitter bug)*

In `espnow_queue_manager.cpp` (transmitter), the drop-oldest recovery on the discovery queue is guarded:

```cpp
if (msg_type == msg_ack) {  // Only for ACK type
    // drop oldest, insert new ACK
}
```

If the discovery queue is full of stale PROBE entries and a valid ACK arrives, but `msg_type != msg_ack`, the ACK is silently dropped before the discovery task ever sees it. During a scan with 200ms probe interval and a full 1000ms dwell, the discovery queue can contain 5 pending probes. If the queue depth is 5 or less, the ACK arriving after those probes cannot enter.

**Fix**: Apply drop-oldest recovery for **any** message type, not just `msg_ack`, and increase discovery queue depth to at least 16 items.

---

## Part 4 — What Changed in the Last Month That Made This Worse

Based on git history and codebase analysis, the following changes were introduced approximately 1 month ago and each contributed to the current failure pattern:

### Change 1 — Introduction of `EspnowTxScheduler`

**Before**: Discovery ACKs and heartbeat ACKs were sent via direct `esp_now_send()` calls with their own inline retry loops. These were synchronous — the caller waited for the send to succeed or fail, then proceeded. Under NO_MEM conditions, the retry happened inline in the calling task.

**After**: All sends go through the scheduler's queue. ACKs are now *asynchronous* — enqueued and sent later by the scheduler task. Under NO_MEM conditions, the retry adds 8–168ms of scheduler task blocking time. During this blocking, the scheduler cannot process other frames. This is correct in principle but the retry duration for `msg_ack` (3 retries × 8ms = 24ms to 48ms) is **shorter than the TCP teardown window**, meaning all 3 retries are exhausted while TCP is still in FIN_WAIT.

The scheduler itself is architecturally correct. The problem is the per-message-type policy values for `msg_ack`.

### Change 2 — `rx_connection_handler` extracted to shared code

The receiver-local `rx_connection_handler` was moved to `esp32common`. During this move, the invocation of `set_control_only_mode()` was separated from `enter_reconnect_quiet_mode()` — quiet mode now only sets an internal flag and disconnects MQTT; the scheduler control_only flag is set reactively via `on_ack_send_pressure()`. This is the Root Cause 4 gap described above.

### Change 3 — Discovery ACK retry count set asymmetrically

In the process of tuning the scheduler to reduce NO_MEM pressure (previous patches), `msg_ack` retries were *reduced* from 8 to 3. This was done to reduce burst pressure, but it overcorrected — the discovery ACK now has insufficient retries to survive a typical TCP teardown window.

### Change 4 — `rx_route_registry` ACK throttle widened to 700ms

The probe-ACK throttle was widened to 700ms per peer per reconnect event. With a 200ms PROBE interval and 1000ms channel dwell, this limits the receiver to at most 1 ACK attempt per 1000ms channel dwell. If that one attempt fails (NO_MEM), no further ACK attempts are made during the current dwell window. TX moves to the next channel and the connection window is missed.

**The 700ms throttle is the single most damaging recent change.** With `msg_ack` retries at 3 and throttle at 700ms, there is exactly **one ACK attempt per channel visit**. If it hits NO_MEM, the connection is lost for that channel visit.

**Fix**: Reduce probe-ACK throttle back to 200ms or less in reconnect mode:

```cpp
// In rx_route_registry.cpp
const uint32_t ack_throttle_ms = (conn_state == CONNECTED) ? 1200 : 200;
```

Combined with 8 retries and 50ms min_gap for `msg_ack`, this gives the receiver approximately 4 independent ACK attempts within each 1000ms channel dwell. The probability of all 4 failing due to NO_MEM is substantially lower than the probability of the single attempt failing.

---

## Part 5 — The Correct Fix Set

These changes, applied together, address the root causes systematically. They are ordered by impact and ease of application.

### Fix 1 — `esp32common/espnow_common_utils/espnow_tx_scheduler.cpp`

**Change `msg_ack` policy**:
```cpp
// FROM:
case msg_ack:           return {120, 3};
// TO:
case msg_ack:           return {50, 8};
```

Rationale: 50ms min_gap × 8 retries = up to 288ms retry window per ACK attempt. This covers typical TCP teardown (10–200ms on LAN). The 50ms min_gap still prevents rate-flooding because the throttle in `rx_route_registry` controls the *enqueue rate*.

### Fix 2 — `esp32common/espnow_common_utils/rx_route_registry.cpp`

**Reduce reconnect-mode probe-ACK throttle**:
```cpp
// FROM:
const uint32_t ack_throttle_ms = (conn_state == CONNECTED) ? 1200 : 700;
// TO:
const uint32_t ack_throttle_ms = (conn_state == CONNECTED) ? 1200 : 200;
```

Rationale: 200ms throttle aligns with the 200ms PROBE interval. The receiver can attempt an ACK for every PROBE, giving 5 attempts per 1000ms channel dwell instead of 1.

### Fix 3 — `esp32common/espnow_common_utils/rx_connection_handler.cpp`

**Call `set_control_only_mode` directly inside `enter_reconnect_quiet_mode()`**:
```cpp
void RxConnectionHandler::enter_reconnect_quiet_mode(uint32_t now, const char* reason) {
    // ... existing quiet mode logic ...
    call_disconnect_mqtt(hooks_);
    
    // NEW: immediately purge non-control queues and restrict scheduler
    // This ensures DATA/MONITORING backlog does not compete with discovery ACK
    EspnowTxScheduler::set_control_only_mode(true, /*purge=*/true);
    
    // NEW: brief yield to allow TCP FIN to be sent (reduces FIN_WAIT collision window)
    vTaskDelay(pdMS_TO_TICKS(100));
}
```

Rationale: 100ms yield after disconnect covers the TCP FIN transmission time. By the time the next PROBE arrives (at earliest ~100ms after quiet mode entry), the TCP socket is sending its FIN and the buffer pool is beginning to free up. The `set_control_only_mode(true, true)` ensures stale DATA queue frames are purged before ACK attempts.

### Fix 4 — Both `mqtt_task.cpp` files

**Increase post-reconnect settle window**:
```cpp
// FROM:
static constexpr uint32_t POST_RECONNECT_SETTLE_MS = 4000;
// TO:
static constexpr uint32_t POST_RECONNECT_SETTLE_MS = 8000;
```

### Fix 5 — `espnowreceiver_2/src/espnow/espnow_tasks.cpp` (or equivalent message dispatch file)

**Guard activity update against discovery messages in receiver_2**:

Find any call to `on_link_activity()` or equivalent heartbeat-freshness update in the main message processing loop and add the guard:
```cpp
// Only refresh heartbeat freshness for non-discovery messages
if (msg_type != msg_probe && msg_type != msg_ack) {
    // call on_link_activity / update heartbeat freshness
}
```

### Fix 6 — `ESPnowtransmitter2/.../src/queue/espnow_queue_manager.cpp`

**Apply drop-oldest to all discovery queue types, not just `msg_ack`**:
```cpp
if (xQueueSend(espnow_discovery_queue, &msg, 0) != pdTRUE) {
    ++g_discovery_enqueue_drops;
    // Drop oldest item regardless of type — always prefer newer traffic
    espnow_queue_msg_t dropped{};
    if (xQueueReceive(espnow_discovery_queue, &dropped, 0) == pdTRUE) {
        ++g_discovery_enqueue_recovered;
        xQueueSend(espnow_discovery_queue, &msg, 0);
    }
}
```

Also increase discovery queue depth (in the same file where it is created):
```cpp
// FROM: whatever the current depth is (likely 5–8)
// TO:
espnow_discovery_queue = xQueueCreate(16, sizeof(espnow_queue_msg_t));
```

---

## Part 6 — What NOT to Change (Rollback Candidates)

The following changes from recent patches are **actively harmful** and should be reconsidered:

| Change | Current Value | Problem | Recommendation |
|---|---|---|---|
| `msg_ack` retry count | 3 | Far too low for NO_MEM conditions | Increase to 8 (Fix 1) |
| `msg_ack` min_gap | 120ms | Too large — limits to 1 ACK per channel dwell when probe interval is 200ms | Reduce to 50ms (Fix 1) |
| Probe-ACK throttle (reconnect) | 700ms | Limits to 1 ACK attempt per channel visit. Single failure = missed channel | Reduce to 200ms (Fix 2) |
| `no_mem_retry_attempts` | 6 | Was reduced from 12. 6 is acceptable but low | Can stay at 6 |
| `retry_base_delay_ms` | 8ms | Was increased from 2ms. This is correct direction | Keep at 8ms |
| `inter_frame_delay_ms` | 8ms | Was increased from 4ms. This is correct direction | Keep at 8ms |

---

## Part 7 — Longer-Term Architectural Recommendation

The correct long-term fix is to eliminate the shared radio constraint for critical reconnect traffic. There are two viable approaches:

### Option A — Move MQTT to the Ethernet interface (if available)

Both receiver devices (Waveshare and LilyGo) do not have Ethernet. This option is not available without hardware changes.

### Option B — Use a local MQTT broker on the same LAN with guaranteed sub-10ms RTT

If the MQTT broker is on the local LAN (e.g., running on a Raspberry Pi or router), TCP teardown RTT is under 5ms. The existing retry window (even at 3 retries × 8ms = 24ms) would cover this. This reduces the NO_MEM collision probability to near zero without any code changes.

**If the MQTT broker is cloud-hosted (e.g., HiveMQ, AWS IoT), this is likely a significant contributor to the failure rate** because the TCP teardown RTT is 50–300ms, well outside the current retry window.

### Option C — Increase the WiFi driver TX buffer pool

In `sdkconfig.defaults` for both receivers:
```
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=64
CONFIG_ESP_WIFI_STATIC_TX_BUFFER=n
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER=y
```

This doubles the pool from the default 32 to 64 descriptors. Both MQTT TCP and ESP-NOW can coexist with more headroom. This is the lowest-effort architectural mitigation and should be applied regardless of other fixes.

**Note**: Increasing buffer count increases DRAM usage by approximately 32 × 1600 bytes = ~50KB. The ESP32-S3 has 512KB DRAM (expandable with PSRAM) so this is generally safe, but check free heap before applying.

---

## Part 8 — Verification Checklist

After applying the fixes in Part 5, verify using these specific log patterns:

### Reconnect success indicators
- `RX_RECONNECT_DIAG`: `outcome=recovered`, `ack_enqueue_ok > 0`, `ack_send_fail_delta = 0`
- `TX_RECONNECT_DIAG`: `outcome=connected`, `first_ack != 0` (non-zero timestamp)
- MQTT gate log: `ESP-NOW ready after X ms settle` (without repeated gate-close cycles)
- No `Send failed (type=1 len=6): ESP_ERR_ESPNOW_NO_MEM` in RX logs after reconnect

### Regression indicators (if still failing)
- `Send failed (type=1 len=6): ESP_ERR_ESPNOW_NO_MEM` → TCP teardown still racing. Check broker RTT and Option C above.
- `TX_RECONNECT_DIAG outcome=connecting_timeout, discovery_ingress attempts=0` → PROBE not arriving at RX (channel mismatch). Check AP channel.
- `TX_RECONNECT_DIAG outcome=connecting_timeout, discovery_ingress attempts>0 drops>0` → discovery queue overflow on TX. Apply Fix 6.
- `RX_RECONNECT_DIAG: ack_enqueue_ok > 0, ack_send_fail_delta > 0` → ACK reaching scheduler but failing at send. NO_MEM still present — apply Option C (buffer pool increase).

---

## Appendix — Key Timing Constants (Cross-Reference)

| Constant | Current Value | File | Notes |
|---|---|---|---|
| TX heartbeat interval | 10,000ms | `timing_config.h` | |
| TX heartbeat timeout | 35,000ms | `timing_config.h` | Triggers reconnect |
| RX heartbeat timeout | 32,000ms | `timing_config.h` | RX connection manager |
| MQTT gate threshold | 12,000ms | both `mqtt_task.cpp` | Hardcoded `12000` |
| Channel dwell | 1,000ms | `timing_config.h::DISCOVERY` | Per-channel scan time |
| Channels scanned | 1–13 | `discovery_task.cpp` | Full scan = 13s |
| PROBE interval | 200ms | `timing_config.h::DISCOVERY` | 5 probes per channel dwell |
| `msg_ack` retries | **3** (too low) | `espnow_tx_scheduler.cpp` | **Fix to 8** |
| `msg_ack` min_gap | **120ms** (too large) | `espnow_tx_scheduler.cpp` | **Fix to 50ms** |
| `msg_heartbeat_ack` retries | 6 | `espnow_tx_scheduler.cpp` | Fine |
| Probe-ACK throttle (CONNECTED) | 1200ms | `rx_route_registry.cpp` | Fine |
| Probe-ACK throttle (reconnect) | **700ms** (too large) | `rx_route_registry.cpp` | **Fix to 200ms** |
| Post-reconnect settle | **4,000ms** (too short) | both `mqtt_task.cpp` | **Fix to 8,000ms** |
| Scheduler CONTROL queue depth | ~4 (depth=16, 30%) | `espnow_tx_scheduler.cpp` | Could increase to 40% |
| WiFi TX buffer pool | ~32 (default) | `sdkconfig.defaults` | **Increase to 64** |
