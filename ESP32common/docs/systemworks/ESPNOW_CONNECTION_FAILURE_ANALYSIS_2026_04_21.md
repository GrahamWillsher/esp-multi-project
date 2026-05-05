# ESP-NOW Connection Failure Analysis
**Date:** 2026-04-21  
**Codebases:** ESPnowtransmitter2 (Olimex ESP32-POE2) ↔ espnowreceiver_LCD (Waveshare ESP32-S3 7" LCD)  
**Shared library:** esp32common  

---

## Executive Summary

Two distinct failure modes were identified through combined code-path analysis and live serial log evidence from the receiver. The original report ("transmitter channel-hops but never finds receiver") maps to **Failure Mode A** — a structural first-boot/post-firmware-update edge case that can silently abort all reconnection attempts. The live log captured during analysis revealed **Failure Mode B** — a memory-exhaustion cascade on the receiver that makes all outgoing ESP-NOW sends fail, destroying connection stability even when the link physically works.

---

## Evidence: Live Receiver Log (captured during investigation)

```
[WARN][MEM_SAMPLER] Internal largest free block LOW: 6132 bytes (threshold: 32768)
[WARN][ESPNOW_TX] Send failed (type=1 len=6): ESP_ERR_ESPNOW_NO_MEM
[WARN][ESPNOW_TX] Send failed (type=3 len=2): ESP_ERR_ESPNOW_NO_MEM
[WARN][ESPNOW_TX] Send failed (type=31 len=16): ESP_ERR_ESPNOW_NO_MEM
[INFO][MAIN] alive: ms=721795 espnow=connected conn=CONNECTED discovery=suspended ch=6 rxcb=981

[WARN][CONN_MGR] Heartbeat/activity timeout (90055ms since last) -> CONNECTION_LOST
[WARN][CONN_MGR] CONNECTION_LOST -> Back to IDLE
[INFO][CONN_MGR] State transition: CONNECTED -> IDLE (duration: 90156ms)
[INFO][CONN_MGR] Auto-reconnect enabled (CONNECTED -> IDLE) -> posting CONNECTION_START

[INFO][CONN_MGR] PEER_REGISTERED -> Transitioning to CONNECTED
[INFO][CONN_MGR] State transition: CONNECTING -> CONNECTED (duration: 322ms)
[INFO][RX_CONN] Connected - channel locked at 6, discovery suspended
```

**Cycle observed:** CONNECTED (90s) → IDLE → CONNECTING (~400ms) → CONNECTED (90s) → repeat indefinitely.

---

## Failure Mode B: Memory Exhaustion Cascade (Active / Current State)

### Symptom
All outgoing sends from the receiver fail with `ESP_ERR_ESPNOW_NO_MEM`. This includes:
- Probe announcements (type=1)
- Heartbeat messages (type=3)
- Initialization requests (type=31, 50, 52, 11)
- Version announces
- ACK responses to incoming probes

The receiver **can still receive** (ESP-NOW RX path does not require heap allocation), so it can accept probes from the transmitter and maintain the discovery/reconnect cycle. However it cannot ACK, heartbeat, or reply to any request.

### Root Cause Chain

```
Internal heap very low (6132 bytes largest free block)
    ↓
esp_now_send() allocates a frame from internal heap → fails with ESP_ERR_ESPNOW_NO_MEM
    ↓
EspnowTxScheduler::task_tx_worker() retries 6× with linear backoff delays
    → each item blocks the worker for up to ~84ms before being discarded
    ↓
New items from all calling tasks try to enqueue (xQueueSend, 2ms timeout)
    → 24-slot queue fills immediately
    → all callers receive ESP_ERR_ESPNOW_NO_MEM from EspnowTxScheduler::send()
    ↓
Receiver heartbeats fail → transmitter never receives heartbeats from receiver
Receiver ACKs fail → no ACK returned for transmitter-sent probes during discovery
Receiver data-requests fail → transmitter never receives REQUEST_DATA
    ↓
Receiver's own heartbeat_monitor fires after 90s (set_heartbeat_timeout_ms(90000))
    → CONNECTION_LOST → IDLE → auto-reconnect → CONNECTING
    ↓
Transmitter still sending probes → receiver receives probe → PEER_REGISTERED → CONNECTED
    → 90s cycle repeats
```

### Why the Receiver Has So Little Heap

The receiver runs:
- LVGL with 16 MB flash, dual-buffered rendering (large static allocations)
- EspnowQueueManager: 3 queues × 32 items × ~270 bytes each ≈ **25 KB**
- EspnowTxScheduler: queue of 24 × ~260 bytes ≈ **6 KB**
- Multiple FreeRTOS tasks with large stacks
- WiFi + ESP-NOW driver buffers
- MQTT client buffers (also failing: `Connection failed, state=-2`)

The heap alert threshold of 32768 bytes is never met; the actual largest free block is **6132 bytes** — well below what ESP-NOW needs to allocate internal TX frames.

### `ESP_ERR_ESPNOW_NO_MEM` Semantics

`esp_now_send()` returns `ESP_ERR_ESPNOW_NO_MEM` when it cannot allocate a frame in the internal TX buffer from IRAM/internal DRAM. This is distinct from FreeRTOS heap. However, the `[MEM_SAMPLER]` alert confirms internal heap is critical. The ESP-NOW driver uses `ESP_MALLOC_CAP_INTERNAL` and the largest internal free block at 6132 bytes is below the minimum frame allocation size.

### Consequence: "No power-profile data ever arrives"

```
Receiver → EspnowTxScheduler::send(transmitter_mac, &request, ..., "REQUEST_DATA") 
         → queue full → ESP_ERR_ESPNOW_NO_MEM
         → REQUEST_DATA never sent
         → Transmitter never activates power data stream
         → [WARN][RX_CONN] No power-profile data yet - retrying REQUEST_DATA  (every ~1s)
```

The REQUEST_DATA retry loop itself makes the scheduler saturation worse.

---

## Failure Mode A: `restart()` Hard-Abort on `g_lock_channel == 0`

### Location
`ESPnowtransmitter2/src/espnow/discovery_task.cpp`, `DiscoveryTask::restart()`:

```cpp
if (g_lock_channel == 0) {
    LOG_ERROR("DISCOVERY", "Cannot restart - no valid channel (g_lock_channel=0)");
    return;  // SILENT ABORT
}
```

### What This Does

`g_lock_channel` is an atomic `uint8_t` written to `0` at compile time. It is only set to a non-zero value inside `active_channel_hopping_task()` **after** a successful first connection:

```cpp
// In active_channel_hopping_task() — runs only ONCE on initial discovery
g_lock_channel = discovered_channel;
```

After the first successful connection, `g_lock_channel` is permanently set. But if the system **boots and the receiver is not present** (powered off, or on different channel), the initial scan completes without finding the receiver, and `g_lock_channel` remains 0 forever for that boot session.

### Impact

Any subsequent call to `restart()` (e.g., after a heartbeat timeout, connection loss, or recovery state machine trigger) immediately returns without doing anything. The `attempt_restart_once()` path — which would call `EspnowDiscovery::restart()` — is never reached.

The transmitter's `EspnowDiscovery::task_impl()` may still be running (and sending probes), but the `DiscoveryTask`-level recovery state machine cannot restart it when needed.

### Trigger Conditions

This manifests as "transmitter hops channels but never connects" when:
1. Receiver is not powered on at transmitter boot time
2. Firmware update resets the transmitter (receiver still running — old state)
3. Any reboot where the receiver is temporarily offline

### Fix Recommendation

Remove or relax the `g_lock_channel == 0` guard in `restart()`. The only purpose of this guard was to avoid restarting on an unknown channel, but `active_channel_hop_scan()` already handles the no-saved-channel case by falling back to channel 1 and scanning all 13 channels:

```cpp
// Current — BLOCKS reconnection if first boot had no lock
void DiscoveryTask::restart() {
    if (g_lock_channel == 0) {
        LOG_ERROR("DISCOVERY", "Cannot restart - no valid channel (g_lock_channel=0)");
        return;
    }
    ...
}

// Recommended — always allow reconnection
void DiscoveryTask::restart() {
    if (g_lock_channel == 0) {
        LOG_INFO("DISCOVERY", "No saved channel — full scan will be used");
        // Don't abort: attempt_restart_once() can still call start_active_channel_hopping()
    }
    ...
}
```

---

## Additional Code Issues Found During Analysis

### Issue C: Duplicate State Events in `active_channel_hop_scan()`

```cpp
// active_channel_hop_scan() — after receiving ACK:
TransmitterConnectionHandler::instance().on_ack_received(ack_mac, ack_channel);  // Posts PEER_FOUND
// ... channel change, peer add ...
TransmitterConnectionHandler::instance().on_peer_registered(ack_mac);  // Posts PEER_REGISTERED
```

Both calls happen in the same task (the hopping task, Core 1). `on_ack_received()` posts `PEER_FOUND` to the connection event queue. Before `process_events()` runs on the connection manager (which happens in `task_worker` on the receiver side, or a similar loop on the transmitter side), `on_peer_registered()` is also called.

If `process_events()` hasn't consumed `PEER_FOUND` yet (and thus hasn't advanced to `CONNECTING` state), then inside `on_peer_registered()`:

```cpp
const bool is_connecting = (state == EspNowConnectionState::CONNECTING);
const bool should_post = deferred_peer_.on_peer_registered(transmitter_mac_, is_connecting, millis());
```

If `is_connecting` is still `false` (state is still `IDLE` — event not yet processed), the `PEER_REGISTERED` event is deferred with a TTL. If the TTL expires before `process_events()` has run, the connection is silently dropped.

**Risk:** Under high task load (which is present given the scheduler saturation), `process_events()` may lag behind the hopping task, causing this race to fire.

**Mitigation:** Introduce a short `vTaskDelay(pdMS_TO_TICKS(10))` between `on_ack_received()` and `on_peer_registered()` in `active_channel_hop_scan()` to give `process_events()` time to consume the PEER_FOUND event first.

### Issue D: `EspnowDiscovery::task_impl()` vs `DiscoveryTask` Parallel Send Paths

The transmitter has two independent probe-sending mechanisms that can both be active:
1. `EspnowDiscovery::task_impl()` — the common `espnow_announce` task (sends directly via `esp_now_send()`)
2. `DiscoveryTask::active_channel_hopping_task()` — the transmitter-specific hopping task (also calls `esp_now_send()`)

Both call `esp_now_send()` for the broadcast peer. Both delete and re-add the broadcast peer. There is no mutual exclusion between them. If `DiscoveryTask::start()` is called while `EspnowDiscovery` is already running an announce task, both run simultaneously, both manipulating the broadcast peer entry.

This can produce:
- `Failed to remove old broadcast peer` log spam
- Lost ACKs (whichever task's `esp_now_recv_cb` consumes the ACK first "wins")
- Channel verification failures if one task switches channel while the other has the peer registered on the old channel

### Issue E: `on_peer_registered()` TTL Drop in `ReceiverConnectionHandler`

```cpp
// rx_connection_handler.cpp
void ReceiverConnectionHandler::on_peer_registered(const uint8_t* transmitter_mac) {
    ...
    const bool should_post = deferred_peer_.on_peer_registered(
        transmitter_mac_, is_connecting, millis());
    if (should_post) {
        (void)post_connection_event(EspNowEvent::PEER_REGISTERED, transmitter_mac_);
        deferred_peer_.on_event_posted();
    }
}
```

If called while not in `CONNECTING` state, the event is deferred. The deferred TTL check is in `tick()`, which is called from `task_worker`. If `task_worker` is blocked (e.g., blocked on queue receive waiting for an inbound message that never comes because the connection is down), `tick()` doesn't run, the TTL expires, and the event is permanently lost.

The receiver's `task_worker` uses `xQueueReceive(..., pdMS_TO_TICKS(kWorkerPollMs))` with `kWorkerPollMs = 100ms`. If messages arrive slowly, `tick()` fires at ~10 Hz. If the deferred TTL is short relative to this polling rate, events can be dropped.

### Issue F: Receiver Discovery Not Suspended During Active CONNECTED State (Race)

In `rx_connection_handler.cpp`, when `CONNECTED → IDLE` fires:

```cpp
EspnowDiscovery::instance().resume();
LOG_INFO("RX_CONN", "Connection lost - peer cleaned up, channel unlocked, discovery resumed");
```

Then `CONNECTION_START → CONNECTING` fires (auto-reconnect), which also calls `resume()`. The `EspnowDiscovery` task now sends probe broadcasts. But the transmitter's active hopping task may still be in the middle of `active_channel_hop_scan()`, waiting for an ACK on a different channel. The receiver's resumed probe on the current channel goes to the transmitter's main `message_queue` (if the hopping task's `EspnowQueueManager::receive_from_discovery_queue` is the only path that processes ACKs during discovery), which means the receiver's probe triggers `on_probe_received()` and posts `PEER_FOUND/PEER_REGISTERED` on the receiver side, but the transmitter's hopping loop never sees an ACK for its own probes.

---

## Current Operating State Summary

| Aspect | State | Evidence |
|---|---|---|
| Initial connection | ✅ Working | `CONNECTING → CONNECTED (duration: 322ms)` in log |
| Channel lock | ✅ Working | `Channel locked at 6` persists across cycles |
| Receive path (RX) | ✅ Working | `rxcb` counter increments; TX_MGR MAC registered each cycle |
| Transmit path (TX) | ❌ Broken | `Send failed: ESP_ERR_ESPNOW_NO_MEM` on ALL outgoing sends |
| Heartbeat (receiver→transmitter) | ❌ Broken | Fails in TxScheduler |
| Data requests | ❌ Broken | `No power-profile data yet - retrying REQUEST_DATA` indefinitely |
| ACK responses | ❌ Broken | ACK sends fail in TxScheduler |
| MQTT | ❌ Broken | `Connection failed, state=-2` (independent but same memory pressure) |
| Connection cycle | ⚠️ 90s cycle | Reconnect works, but re-broken immediately due to TX failure |
| Internal heap | 🔴 CRITICAL | `Internal largest free block LOW: 6132 bytes` |

---

## Priority Fix List

### P0 — Memory Exhaustion (must fix first; all other issues are secondary)

**Option 1 — Reduce static allocations:**

| Allocation | Current | Reduction Possible |
|---|---|---|
| `EspnowQueueManager` message queue | 32 items × ~270B ≈ 8.6KB | Reduce to 10 items → 2.7KB |
| `EspnowQueueManager` discovery queue | 20 items × ~270B ≈ 5.4KB | Reduce to 8 items → 2.2KB |
| `EspnowQueueManager` rx queue | 30 items × ~270B ≈ 8.1KB | Reduce to 10 items → 2.7KB |
| `EspnowTxScheduler` queue | 24 items × ~260B ≈ 6.2KB | Reduce to 8 items → 2.1KB |
| **Total recoverable** | **~28KB** | **~19KB freed** |

These changes in `runtime_task_startup.cpp` and the scheduler init call would recover ~19KB of internal heap.

**Option 2 — Move LVGL frame buffers to PSRAM:**
If not already done, allocating LVGL draw buffers with `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` keeps large display buffers out of internal heap.

**Option 3 — Task stack sizes:**
Audit `TaskConfig::*_STACK` values. LVGL renderer, webserver, and ESPNOW tasks should use minimum necessary stacks, with overflow detection enabled.

### P1 — `DiscoveryTask::restart()` Hard-Abort

See Issue A fix description above. One-line change: replace `return` with a log + fallthrough.

### P2 — Race Condition in `active_channel_hop_scan()`

Add a brief yield after `on_ack_received()` before calling `on_peer_registered()`:

```cpp
// In active_channel_hop_scan() after receiving ACK
TransmitterConnectionHandler::instance().on_ack_received(ack_mac, ack_channel);

// Give connection manager time to process PEER_FOUND → advance to CONNECTING state
// before on_peer_registered() checks for CONNECTING state
vTaskDelay(pdMS_TO_TICKS(20));

if (!EspnowPeerManager::add_peer(ack_mac, ack_channel)) { ... }
TransmitterConnectionHandler::instance().on_peer_registered(ack_mac);
```

### P3 — TxScheduler Saturation During Failure

The retry loop in `send_immediate_with_retry()` blocks the worker for up to 84ms when `esp_now_send()` repeatedly fails. During this time the queue fills and all callers see failures. Consider capping retries to 2–3 during known low-memory conditions, or adding a `is_memory_ok()` gate before attempting sends.

### P4 — Parallel Probe Senders (Issue D)

On the transmitter, ensure `EspnowDiscovery::stop()` is called before `DiscoveryTask::start_active_channel_hopping()` is invoked, so only one entity owns the broadcast peer at any time.

---

## Reconnection Flow (Verified Working)

The following flow works correctly in the live system:

```
Receiver: CONNECTED(90s) → heartbeat timeout → CONNECTION_LOST
    → IDLE → auto-reconnect → CONNECTION_START
    → CONNECTING → EspnowDiscovery resumed

Transmitter: still sending PROBEs (EspnowDiscovery running)

Receiver: PROBE received from transmitter
    → on_probe_received() → PEER_FOUND posted
    → peer_registered (transmitter already in peer table) → PEER_REGISTERED posted
    → CONNECTING → CONNECTED (within ~400ms)

Receiver: channel locked, discovery suspended
    → send_initialization_requests() called
         → BUT: all sends fail with ESP_ERR_ESPNOW_NO_MEM
         → 90s clock starts again with no data flowing
```

The reconnection infrastructure is sound. The bottleneck is entirely the TX memory exhaustion.

---

## Files Analysed

| File | Lines Read | Key Finding |
|---|---|---|
| `ESPnowtransmitter2/src/espnow/discovery_task.cpp` | 1–726 (complete) | `restart()` abort on `g_lock_channel==0`; active hopping flow |
| `ESPnowtransmitter2/src/espnow/tx_connection_handler.cpp` | 1–262 (complete) | State callbacks; deferred discovery; TTL-based peer registration |
| `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp` | Complete | Init sequence; task_worker loop |
| `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp` | 1–200 | State callbacks; init; on_peer_registered TTL |
| `espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp` | 1–150 | `setup_message_routes()`; probe ACK config |
| `esp32common/espnow_common_utils/espnow_discovery.cpp` | Complete | Both senders use `EspnowDiscovery` (race potential) |
| `esp32common/espnow_common_utils/espnow_standard_handlers.cpp` | Complete | `handle_probe()` → `send_ack_response()` |
| `esp32common/espnow_common_utils/connection_manager.cpp` | Complete | `PEER_FOUND/PEER_REGISTERED/CONNECTED` state machine |
| `esp32common/espnow_common_utils/espnow_tx_scheduler.cpp` | Complete | Queue saturation mechanism; retry loop timing |
| `ESPnowtransmitter2/src/queue/espnow_queue_manager.cpp` | Complete | Three-queue architecture; discovery queue routing |

---

*Document generated: 2026-04-21*
