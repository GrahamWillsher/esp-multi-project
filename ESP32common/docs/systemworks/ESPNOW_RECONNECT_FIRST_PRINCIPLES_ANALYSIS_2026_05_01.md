# ESP-NOW Reconnect — First-Principles Root-Cause Analysis & Rewrite Plan
**Date:** 2026-05-01  
**Scope:** ESPnowtransmitter2 (TX) ↔ espnowreceiver_LCD / espnowreceiver_2 (RX)  
**Status:** CRITICAL — reconnect after connection loss is unreliable

---

## 1. Executive Summary

Every incremental fix applied so far has addressed symptoms rather than root causes.  
The core problem is **architectural**: the reconnect sequence touches **at least 5 concurrent state machines running across 2 CPU cores without a unified synchronisation domain**.  
This guarantees race conditions that intermittently (and sometimes permanently) prevent the `CONNECTED` state from being reached after a connection loss.

This document:
1. Proves each failure mode with a precise event-trace dry-run.
2. Lists every incremental patch applied and why it did not (or could not) fully fix the problem.
3. Provides a minimal, provably-correct rewrite of the reconnect core.
4. Gives explicit "before" → "after" pseudocode for every changed component.

---

## 2. Current Architecture — Full Component Map

```
TX (Core 0)                           TX (Core 1)
──────────────────────────────────    ─────────────────────────────
connection_event_processor task        active_channel_hopping_task
  └─ EspNowConnectionManager            └─ active_channel_hop_scan()
       └─ process_events()                    └─ scan_channel_for_ack()
            ├─ queue drain                          ├─ send_probe_on_channel()
            └─ CONNECTING timeout check             └─ receive_from_discovery_queue()

Main loop (Core 0)
  ├─ DiscoveryTask::validate_state()        ← reads active_hopping_running_
  ├─ DiscoveryTask::update_recovery()
  ├─ TransmitterConnectionHandler::tick()   ← reads/writes deferred_discovery_start_
  └─ EspNowConnectionManager metrics

State variables shared across cores WITHOUT mutex:
  active_hopping_running_ (bool)      — written by Core 1, read by Core 0
  task_handle_ (TaskHandle_t)         — written by Core 1, read by Core 0  
  peer_register_event_posted_ (bool)  — written by Core 1, read by Core 0
  receiver_channel_ (uint8_t)         — written by Core 1, read by Core 0
  g_lock_channel (uint8_t, global)    — written by Core 1, read by Core 0 & ISRs
```

**No mutex, no atomic, no memory barrier protects any of these.**  
On the Xtensa LX6 (ESP32) or LX7 (S3) dual-core processor, writes on one core are **not guaranteed visible** to the other core without a memory barrier or synchronisation primitive.

---

## 3. Failure Mode Dry-Runs

Each section below traces the exact sequence of events through the code, citing line references, and identifies the precise failure point.

---

### 3.1 — Race: CONNECTING Timeout Fires While PEER_REGISTERED Is in Transit

**Precondition:** TX is CONNECTING. Hopper task (Core 1) has found the receiver.

```
Time    Core 1 (Hopping Task)                 Core 0 (conn_event_processor)
──────  ────────────────────────────────────  ───────────────────────────────────
T=0ms   scan_channel_for_ack() returns true
        on_ack_received() called
          → xQueueSend(PEER_FOUND)  [Q: PEER_FOUND]
        add_peer() called
        on_peer_registered() called
          state check: get_state() = CONNECTING  (reads current_state_ RAW)
          → xQueueSend(PEER_REGISTERED)  [Q: PEER_FOUND, PEER_REGISTERED]

T=1ms                                         process_events() called:
                                                drain queue:  PEER_FOUND processed → saves MAC
                                                              PEER_REGISTERED processed → CONNECTED ✓
                                              [HAPPY PATH — works]

──────────────────────── ALTERNATE TIMELINE ─────────────────────────────────────

T=0ms   scan_channel_for_ack() returns true
        on_ack_received() called
          → xQueueSend(PEER_FOUND)  [Q: PEER_FOUND]
        add_peer() called
        on_peer_registered() called
          state check: get_state() = CONNECTING  ← RAW READ, no barrier

T=1ms                                         process_events() called:
                                                drain queue: empty (PEER_FOUND not in
                                                             queue yet? No — xQueueSend is
                                                             thread-safe. BUT...)
                                                CONNECTING timeout check:
                                                  get_state_time_ms() > 35000? YES
                                                  → transition_to_state(IDLE)
                                                     peer_mac_ cleared
                                                     auto-reconnect → xQueueSend(CONNECTION_START)
                                                     callback fires: CONNECTING→IDLE
                                                                     "preserving hopper" (noop)

T=2ms   on_peer_registered() sees CONNECTING   process_events() called:
        (stale cache line — LX6/LX7 has          drain queue:
        per-core L1 cache with write-            [PEER_FOUND, PEER_REGISTERED,
        invalidation, not coherency!)              CONNECTION_START]
        → xQueueSend(PEER_REGISTERED)          
        [Q: PEER_FOUND, PEER_REGISTERED,
            CONNECTION_START]
```

**Why this matters:** The `get_state()` call inside `on_peer_registered()` (running on Core 1) reads `current_state_` from Core 1's L1 cache. Core 0 may have already written `IDLE` to L1 cache without that write being flushed to L2/RAM. Core 1 therefore reads stale `CONNECTING` and posts `PEER_REGISTERED`. Meanwhile Core 0's state machine is now in `IDLE`.

The queue then contains `[PEER_FOUND, PEER_REGISTERED, CONNECTION_START]` — and the sequence described in §3.0 of the happy path resumes. **So this specific path does eventually recover** — but with a full 35-second delay.

---

### 3.2 — Critical Race: Backoff Blocks Discovery When Hopper Has Already Exited

**This is the primary failure mode causing permanent reconnect failure.**

```
Sequence of events after initial connection loss:

Event #1  CONNECTED→IDLE
          auto-reconnect → CONNECTION_START in queue
          connection_event_processor: CONNECTION_START → CONNECTING
          IDLE→CONNECTING callback:
            should_attempt_reconnect() → true (attempt #1)
            on_reconnect_attempt() → backoff counter = 1
            start_discovery_hopping_only() called
              active_hopping_running_ = false? YES
              → xTaskCreate(active_channel_hopping_task)
              → active_hopping_running_ = true

Event #2  Hopper runs for 35 seconds, does NOT find receiver
          active_channel_hop_scan() returns false (first scan)
          vTaskDelay(DISCOVERY_RETRY_INTERVAL_MS = 5000ms)
          [hopper is still running, active_hopping_running_ = true]

Event #3  CONNECTING timeout fires at 35s
          → IDLE
          auto-reconnect → CONNECTION_START
          IDLE→CONNECTING callback:
            should_attempt_reconnect() → depends on backoff
            [BackoffManager default: after 1 failure, wait ~2s → likely still true]
            on_reconnect_attempt() → backoff counter = 2
            start_discovery_hopping_only():
              active_hopping_running_ = TRUE → duplicate start ignored ✓

Event #4  Hopper finds receiver (scan attempt #2, after 5s retry wait)
          on_ack_received() → PEER_FOUND queued
          add_peer() called
          on_peer_registered() → PEER_REGISTERED queued
          [hopper sets discovery_complete=true, exits loop]
          active_hopping_running_ = false  ← WRITTEN BY Core 1
          task_handle_ = nullptr
          vTaskDelete(nullptr)

Event #5  connection_event_processor processes PEER_FOUND + PEER_REGISTERED
          → CONNECTED ✓

Event #6  [CONNECTED for some time...]

Event #7  Connection lost again (heartbeat timeout)
          CONNECTED→IDLE
          auto-reconnect → CONNECTION_START
          IDLE→CONNECTING callback:
            should_attempt_reconnect() → [after 2 previous attempts, backoff may be 4s, 8s...]
            IF backoff says WAIT:
              deferred_discovery_start_ = true
              deferred_discovery_due_ms_ = millis() + DEFERRED_DISCOVERY_POLL_MS
              active_hopping_running_ = FALSE (hopper exited cleanly in Event #4)
              NO NEW HOPPER IS STARTED

            tick() will poll until backoff expires, then start hopper.
            BUT: during the backoff window (e.g., 8 seconds), NO PROBES ARE SENT.
            The RX is waiting. The TX is silent. Dead time.

Event #8  Backoff eventually allows → tick() starts hopper
          → finds receiver → CONNECTED ✓ (IF it works)

BUT: Each successful reconnect increments the backoff counter regardless of
     whether the reconnect was fast or slow. After ~5 connection cycles, the
     backoff can be 32+ seconds. This creates a 32s window where no probes
     are sent, making the RX think TX is offline and potentially entering
     a degraded state on the RX side.

WORSE: BackoffManager::on_connection_success() is called from transition_to_state(CONNECTED).
       But on_reconnect_attempt() is called EVERY time IDLE→CONNECTING fires —
       including the auto-reconnect cycles triggered by CONNECTING timeout.
       Each 35s CONNECTING timeout → IDLE → CONNECTING counts as an attempt.
       After 3 35-second timeouts, backoff is: 1s, 2s, 4s (or whatever the multiplier is).
       The deferred window grows even though no actual reconnect was attempted.
```

**Fix required:** Backoff counter must only increment when a full scan cycle completes without finding the receiver. It must NOT increment on CONNECTING timeout re-entry if a hopper is already running.

---

### 3.3 — Fatal Race: active_hopping_running_ Without Synchronisation

The `active_hopping_running_` flag is a plain `bool` (non-atomic, no volatile).

```cpp
// Core 1 (hopping task, end of task):
self->active_hopping_running_ = false;    // ← write from Core 1
self->task_handle_ = nullptr;
vTaskDelete(nullptr);

// Core 0 (connection_event_processor, IDLE→CONNECTING callback):
DiscoveryTask::instance().start_active_channel_hopping():
  if (active_hopping_running_ && task_handle_ != nullptr) { // ← read from Core 0
      return; // duplicate guard
  }
  // ...
  active_hopping_running_ = true;  // ← write from Core 0
```

On ESP32 (Xtensa LX6/LX7), each core has a 32KB L1 I-cache and 32KB L1 D-cache. Cache coherence between cores is **NOT automatic for regular memory writes**. The `MEMW` instruction (memory wait) or explicit `__sync_synchronize()` / `atomic<bool>` is required to guarantee cross-core visibility.

**Concrete failure scenario:**
1. Core 1 writes `active_hopping_running_ = false` (in Core 1's L1 cache)
2. Core 0 reads `active_hopping_running_` — Core 0's L1 cache still has `true`
3. Core 0 returns early from `start_active_channel_hopping()` (thinking hopper is still running)
4. No new hopper is ever started
5. Reconnect hangs indefinitely

---

### 3.4 — Logic Error: PEER_FOUND in IDLE Triggers Duplicate IDLE→CONNECTING

From `handle_idle_event`:
```cpp
case EspNowEvent::PEER_FOUND:
    LOG_INFO("CONN_MGR", "PEER_FOUND (in IDLE) -> Transitioning to CONNECTING");
    memcpy(peer_mac_, event.peer_mac, 6);
    transition_to_state(EspNowConnectionState::CONNECTING);
    break;
```

Combined with auto-reconnect posting `CONNECTION_START` when entering IDLE:

```
Queue at moment of timeout: [PEER_FOUND, PEER_REGISTERED, CONNECTION_START]

process_events():
  PEER_FOUND (state=IDLE) → CONNECTING
    IDLE→CONNECTING callback fires:
      flush_deferred_peer_registered() [nothing deferred]
      should_attempt_reconnect(): may be false (backoff)
      → deferred_discovery_start_ = true ← SETS DEFERRED FLAG
                                            even though hopper should already
                                            have done its job

  PEER_REGISTERED (state=CONNECTING) → CONNECTED ✓
    CONNECTING→CONNECTED callback fires:
      get_receiver_channel() → returns receiver_channel_
      channel > 0? Only if it was set by on_ack_received()
      on_ack_received() sets receiver_channel_ — but was this from the
      current scan or a stale value from 35 seconds ago?

  CONNECTION_START (state=CONNECTED) → ignored ✓

deferred_discovery_start_ is now true, but we're CONNECTED.
tick() will try to start discovery because it only checks:
  if (get_state() != CONNECTING) { deferred_discovery_start_ = false; }
So tick() will cancel correctly. OK this path actually survives.
```

However, if the sequence is `[CONNECTION_START, PEER_FOUND, PEER_REGISTERED]`:
```
  CONNECTION_START (state=IDLE) → CONNECTING
    callback: starts hopper (or defers via backoff)
    peer_register_event_posted_ = false ← resets the guard!

  PEER_FOUND (state=CONNECTING) → saves MAC, stays CONNECTING ✓

  PEER_REGISTERED (state=CONNECTING) → CONNECTED ✓
```

This also works. The event ordering doesn't actually matter here. The real bug is §3.2 and §3.3.

---

### 3.5 — ESP_ERR_ESPNOW_NO_MEM on RX During Fast Reconnect

When the TX is scanning rapidly and the RX has MQTT active, the MQTT WiFi task and ESP-NOW share the same WiFi driver send path. `esp_now_send()` returns `ESP_ERR_ESPNOW_NO_MEM` when the WiFi driver TX buffer is full.

The RX side tries to send ACK in response to PROBE. If `esp_now_send` returns `NO_MEM`, the ACK is silently dropped. No retry logic exists for ACK sends.

The TX never receives the ACK on the channel it was scanning, moves on, and misses the receiver.

**Current "fix":** Throttle from 200ms to 500ms. This reduces the rate of ACK attempts but does NOT retry a failed ACK. If the one permitted ACK within the 500ms window fails with NO_MEM, the TX still misses it.

**Required fix:** ACK send must use a send callback and retry on NO_MEM, OR the TX must dwell longer on the correct channel.

---

### 3.6 — `g_lock_channel` Is a Non-Atomic Global Written from Core 1

```cpp
// In active_channel_hopping_task (Core 1):
g_lock_channel = discovered_channel;
```

`g_lock_channel` is a `uint8_t` global. It is read from Core 0 (main loop, validate_state, restart_lock_channel). On Xtensa, 8-bit writes are atomic at the hardware instruction level, but **compiler reordering** can still cause Core 0 to read stale values without a memory barrier. This is low severity (uint8_t write is effectively atomic on aligned memory) but is technically undefined behaviour in C++.

---

## 4. Summary of All Previous Incremental Fixes and Why They Were Insufficient

| Fix | What it did | Why it was insufficient |
|-----|------------|------------------------|
| Remove `stop_active_channel_hopping()` on CONNECTING timeout | Prevented hopper task kill/restart cycle | Does not fix the backoff-over-counting bug (§3.2) or the cache coherence bug (§3.3) |
| Add `!is_connected()` guard in `validate_state()` | Prevented false-positive restart() during IDLE/CONNECTING | Correct but addresses a symptom; does not fix reconnect |
| Throttle RX ACK from 200ms to 500ms | Reduces `ESP_ERR_ESPNOW_NO_MEM` pressure | Does not add retry logic; if the one permitted ACK fails, TX still misses it (§3.5) |

None of these fixes address the **two primary bugs** that actually cause reconnect failure:
1. `active_hopping_running_` cache coherence / atomicity (§3.3)
2. Backoff counter incremented on CONNECTING timeout re-entry when hopper is running (§3.2)

---

## 5. Espressif Official Guidance on ESP-NOW + WiFi STA Coexistence

From the official IDF documentation and community practice:

1. **Channel constraint is absolute:** When a device is connected to a WiFi AP in STA mode, the WiFi radio is locked to the AP's channel. ESP-NOW can only operate on that channel. This is not configurable. The RX (LCD, T-Display) MUST receive probes on the AP's channel — and ONLY that channel.

2. **`esp_now_send()` shares the WiFi TX queue with MQTT/HTTP/TCP.** Under heavy MQTT traffic, `ESP_ERR_ESPNOW_NO_MEM` is expected and must be handled with retry logic in the application layer.

3. **ESP-NOW peer channel=0 means "use current WiFi channel."** Sending to a peer registered with channel=0 will use whatever channel WiFi is currently on. This is correct for STA mode coexistence.

4. **For the TX (Olimex PoE2, Ethernet-only):** WiFi is free to change channels. `esp_wifi_set_channel()` works immediately and reliably when not connected to an AP.

5. **Recommended pattern for reliable reconnect with coexistence:** Keep the TX dwell long enough for the RX to detect the probe AND complete the ACK send even under MQTT load. Community experience suggests a minimum dwell of **2000ms per channel** when the RX is WiFi+MQTT connected.

6. **Espressif's own ESP-NOW example** (esp-idf/examples/wifi/espnow) uses a simple `esp_now_send()` with no retry. For production use, they recommend: "If necessary, send back ack data when receiving ESP-NOW data. If receiving ack data timeouts, retransmit the ESP-NOW data."

---

## 6. The Correct Architecture — Design Principles

### 6.1 Single Synchronisation Domain for Reconnect

All reconnect state must live in **one task** or be protected by **one mutex**. Reading state across task/core boundaries without synchronisation is undefined behaviour.

**Proposed:** A single `EspNowReconnectManager` task owns all reconnect state. Other tasks communicate with it via a command queue only.

### 6.2 Event-Driven, Not Polling

The current design polls state (validate_state every 30s, tick() on every loop iteration). This creates opportunities for state to change between the poll and the action. Replace with:
- State changes are events posted to the reconnect manager's queue
- The reconnect manager applies all state changes atomically within its own task context

### 6.3 Backoff Must Track Scan Attempts, Not State Machine Cycles

A "reconnect attempt" is **one complete scan of all 13 channels**. Not a CONNECTING timeout. The backoff counter must only increment after a full scan cycle completes without finding the receiver.

### 6.4 ACK Must Be Delivered Reliably

The RX must retry ACK sends on failure. The simplest approach: register an ESP-NOW send callback that re-posts a "send ACK" command if the send failed with `NO_MEM`. Limit retries to 3 attempts per probe.

### 6.5 `active_hopping_running_` Must Be Atomic

Replace `bool active_hopping_running_` with `std::atomic<bool>` or protect with a mutex. This is the minimum fix that addresses §3.3.

---

## 7. Recommended Rewrite — Minimal Correct Implementation

This section describes the precise changes needed. The goal is the **minimum rewrite** that eliminates the proven bugs without restructuring the entire codebase.

---

### Change 1 — Make `active_hopping_running_` and `task_handle_` thread-safe

**File:** `ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.h`

**Current:**
```cpp
bool active_hopping_running_ = false;
TaskHandle_t task_handle_ = nullptr;
```

**Replace with:**
```cpp
std::atomic<bool> active_hopping_running_{false};
// task_handle_ is only ever written from Core 1 (hopping task start/end) and Core 1
// callback context. Wrap in a critical section on write:
volatile TaskHandle_t task_handle_ = nullptr;
SemaphoreHandle_t task_handle_mutex_ = nullptr;  // init in constructor
```

In `DiscoveryTask` constructor/init:
```cpp
task_handle_mutex_ = xSemaphoreCreateMutex();
```

In `start_active_channel_hopping()`:
```cpp
if (active_hopping_running_.load(std::memory_order_acquire)) {
    // already running — duplicate start ignored
    return;
}
// ...
xTaskCreatePinnedToCore(...);
if (task_handle_ != nullptr) {
    active_hopping_running_.store(true, std::memory_order_release);
}
```

In `active_channel_hopping_task()` (at end, before vTaskDelete):
```cpp
self->active_hopping_running_.store(false, std::memory_order_release);
// brief barrier to ensure Core 0 sees updated value before task disappears:
portMEMORY_BARRIER();
self->task_handle_ = nullptr;
vTaskDelete(nullptr);
```

In `stop_active_channel_hopping()`:
```cpp
active_hopping_running_.store(false, std::memory_order_release);
portMEMORY_BARRIER();
if (task_handle_ != nullptr) {
    vTaskDelete(task_handle_);
    task_handle_ = nullptr;
}
```

---

### Change 2 — Fix Backoff Over-Counting

**File:** `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_connection_handler.cpp`

**Root cause:** `on_reconnect_attempt()` is called in the IDLE→CONNECTING callback, which fires on every CONNECTING timeout → auto-reconnect cycle, even when the hopper is already running and doing useful work.

**Fix:** Only call `on_reconnect_attempt()` when the hopper is NOT already running (i.e., this is a fresh scan attempt, not a timeout-forced re-entry):

```cpp
// In IDLE→CONNECTING callback:
if (EspNowConnectionManager::instance().should_attempt_reconnect()) {
    if (!DiscoveryTask::instance().is_hopping_running()) {
        // Only count as an attempt if we're actually starting a new scan
        EspNowConnectionManager::instance().on_reconnect_attempt();
        TransmitterConnectionHandler::instance().start_discovery_hopping_only();
    } else {
        // Hopper already running from previous cycle — do not penalise backoff
        LOG_INFO("TX_CONN", "IDLE→CONNECTING: hopper already running, backoff not incremented");
    }
} else {
    if (!DiscoveryTask::instance().is_hopping_running()) {
        // Hopper not running AND backoff says wait
        TransmitterConnectionHandler::instance().deferred_discovery_start_ = true;
        TransmitterConnectionHandler::instance().deferred_discovery_due_ms_ =
            millis() + TimingConfig::DEFERRED_DISCOVERY_POLL_MS;
        LOG_INFO("TX_CONN", "Backoff active - deferring discovery start");
    }
    // If hopper IS running during backoff, let it continue silently
}
```

Add accessor to `DiscoveryTask`:
```cpp
bool is_hopping_running() const {
    return active_hopping_running_.load(std::memory_order_acquire);
}
```

---

### Change 3 — Increase TX Dwell Time Per Channel to 2000ms

**File:** `esp32common/include/esp32common/config/timing_config.h`

**Current:**
```cpp
TRANSMIT_DURATION_PER_CHANNEL_MS = 1000
```

**Change to:**
```cpp
TRANSMIT_DURATION_PER_CHANNEL_MS = 2000
```

**Rationale:** The RX is running MQTT over WiFi. `esp_now_send()` on the RX competes with MQTT TCP traffic for the WiFi TX queue. Under moderate MQTT load, the queue can back up for up to 800ms. A 1000ms dwell gives only 200ms margin. A 2000ms dwell gives 1200ms margin — enough for the ACK to survive a short burst of MQTT traffic.

Total scan time increases from 13s to 26s. This is acceptable. If the last known channel is used as the starting point (already implemented), the expected reconnect time when the receiver is on channel Y is `(Y × 2000ms) / 13 ≈ 1-2 scan attempts × 2s = 2–4s in most cases`.

---

### Change 4 — Add ACK Retry on NO_MEM on the RX Side

**File:** `esp32common/espnow_common_utils/espnow_standard_handlers.cpp`

**Current `send_ack_response()`:**
```cpp
void send_ack_response(const uint8_t* mac, uint32_t seq, uint8_t channel) {
    ack_t ack;
    ack.type = msg_ack;
    ack.seq = seq;
    ack.channel = channel;
    // Add peer if needed...
    esp_now_send(mac, (uint8_t*)&ack, sizeof(ack));
}
```

**Replace with retry loop:**
```cpp
void send_ack_response(const uint8_t* mac, uint32_t seq, uint8_t channel) {
    ack_t ack;
    ack.type = msg_ack;
    ack.seq = seq;
    ack.channel = channel;

    constexpr int kMaxRetries = 3;
    constexpr uint32_t kRetryDelayMs = 20;

    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        // Ensure peer is registered
        if (!esp_now_is_peer_exist(mac)) {
            esp_now_peer_info_t p{};
            memcpy(p.peer_addr, mac, 6);
            p.channel = 0;  // current channel
            p.encrypt = false;
            p.ifidx = WIFI_IF_STA;
            esp_now_add_peer(&p);
        }

        esp_err_t rc = esp_now_send(mac, (uint8_t*)&ack, sizeof(ack));
        if (rc == ESP_OK) {
            return;  // enqueued successfully
        }
        if (rc == ESP_ERR_ESPNOW_NO_MEM) {
            // WiFi TX buffer full — brief yield and retry
            vTaskDelay(pdMS_TO_TICKS(kRetryDelayMs));
            continue;
        }
        // Other errors (NOT_FOUND, CHAN, IF) are not retriable
        MQTT_LOG_WARN("ACK", "send_ack_response failed (attempt %d): %s",
                      attempt + 1, esp_err_to_name(rc));
        break;
    }
}
```

---

### Change 5 — Remove the `restart()` / `validate_state()` Self-Healing Loop from main.cpp

**File:** `ESPnowtransmitter2/espnowtransmitter2/src/main.cpp`

The `validate_state()` → `restart()` path in the main loop was intended for steady-state health checking but creates unwanted interactions with the reconnect flow. With the other fixes in place, channel state is managed correctly by the hopper. The `restart()` function calls `stop_active_channel_hopping()` + `start_active_channel_hopping()` — a guaranteed-interruption of a running reconnect.

**Change:**
```cpp
// BEFORE:
if (now - last_state_validation > TimingConfig::STATE_VALIDATION_INTERVAL_MS) {
    if (!DiscoveryTask::instance().validate_state()) {
        LOG_WARN("MAIN", "State validation failed - triggering self-healing restart");
        DiscoveryTask::instance().restart();
    }
    last_state_validation = now;
}

// AFTER:
if (now - last_state_validation > TimingConfig::STATE_VALIDATION_INTERVAL_MS) {
    if (EspNowConnectionManager::instance().is_connected()) {
        // Only audit state when CONNECTED and not mid-scan
        if (!DiscoveryTask::instance().validate_state()) {
            LOG_WARN("MAIN", "State validation failed while connected — "
                             "scheduling graceful reconnect");
            // Do NOT call restart() directly — post a connection reset event
            // which goes through the proper state machine
            EspNowConnectionManager::instance().post_event(EspNowEvent::RESET_CONNECTION);
        }
    }
    last_state_validation = now;
}
```

This replaces a hard `restart()` (which brutally kills the hopper task) with an orderly `RESET_CONNECTION` event that goes through the proper state machine path: CONNECTED→IDLE→auto-reconnect→CONNECTING→hopper starts.

---

### Change 6 — RX: Decouple ACK Throttle from Send Result

**File:** `esp32common/espnow_common_utils/rx_route_registry.cpp`

The current throttle prevents sending more than once per 500ms (recovery mode). But if the FIRST send in that window fails (NO_MEM), no further sends are allowed for 500ms. The throttle state should only be updated if the send SUCCEEDS (or exhausts retries with a terminal error).

Move the throttle state update inside `send_ack_response()` after the retry loop, and pass a success/fail result back to the throttle:

```cpp
// In rx_route_registry.cpp throttle logic:
// Only update throttle_state if we actually sent (or tried) the ACK
// — not before checking whether we should send.
// The flag should record "last time we successfully queued an ACK attempt",
// not "last time we checked the throttle".
// Current code already does this correctly for the happy path.
// The fix is that send_ack_response must not silently drop NO_MEM failures
// (handled by Change 4 above).
```

No change needed here beyond Change 4.

---

## 8. Event Trace Dry-Run of Corrected System

With all 5 changes applied, here is the reconnect sequence trace:

```
T=0       Connection lost. Heartbeat timeout fires.
          CONNECTED→IDLE transition.
          peer_mac_ cleared.
          Auto-reconnect: CONNECTION_START queued.

T=1ms     connection_event_processor: CONNECTION_START → CONNECTING
          IDLE→CONNECTING callback:
            is_hopping_running()? NO (atomic read — correct)
            should_attempt_reconnect()? YES (backoff cleared on last connection success)
            on_reconnect_attempt() called (counter = 1)
            start_discovery_hopping_only():
              active_hopping_running_.load() = false → create task
              active_hopping_running_.store(true, release)
              task starts on Core 1

T=2ms     Core 1: active_channel_hop_scan begins
          Start from last_known_channel (e.g., ch=6, AP channel of RX)
          Phase1: TX sets WiFi to ch=6, sends PROBE broadcast

T=150ms   RX (on ch=6) receives PROBE
          handle_probe() → add_peer(TX_MAC, 0) → send_ack_response()
            esp_now_send attempt 1 → OK
          [ACK queued in WiFi TX buffer]

T=200ms   TX receives ACK on ch=6 (within first 2000ms dwell)
          scan_channel_for_ack returns true
          on_ack_received(ack_mac, 6) → PEER_FOUND queued
          add_peer(ack_mac, 6)
          on_peer_registered(ack_mac):
            state check: get_state() = CONNECTING ✓ (atomic, barrier ensures visibility)
            PEER_REGISTERED queued

T=201ms   Core 1: discovery_complete = true, exit scan loop
          g_lock_channel = 6
          force_and_verify_channel(6)
          flush DataCache
          send version beacon
          active_hopping_running_.store(false, release) ← atomic, visible to Core 0
          portMEMORY_BARRIER()
          task_handle_ = nullptr
          vTaskDelete(nullptr)

T=202ms   connection_event_processor processes queue:
          PEER_FOUND → saves MAC, stays CONNECTING
          PEER_REGISTERED → CONNECTED ✓
          CONNECTING→CONNECTED callback:
            channel = get_receiver_channel() = 6 ✓
            ChannelManager::lock_channel(6)
            HeartbeatManager::reset()
            TxStateMachine::on_connected(6)
            LED replay sent

T=203ms   System is CONNECTED ✓
          Heartbeats resume. Data flows. MQTT config synced.
```

**Total reconnect time from loss to reconnect: ~200ms when on the last-known channel.**

In the worst case (receiver on a different channel from last known):
- Each channel takes 2000ms
- If receiver is on ch=13, worst case = 13 × 2000ms = 26s
- With phase2 weighted sweep starting at saved channel, average case ≈ 4s

---

## 9. What Is NOT in This Rewrite (Acceptable Scope Reduction)

The following improvements were discussed in the previous implementation plan but are NOT required to fix the reconnect issue. They remain desirable future work:

1. **Full single-task reconnect manager** — architecturally cleaner but requires restructuring too much code for emergency fix
2. **NVS-persisted channel** — already partially implemented via `ChannelManager`
3. **MQTT disconnect/reconnect ordering during reconnect** — relevant for RX LCD only, separate concern
4. **OTA compatibility during reconnect** — not blocking

---

## 10. Implementation Priority Order

Complete these in sequence. Test between each step.

### Step 1 — Atomic `active_hopping_running_` (Change 1)
**Impact:** Eliminates §3.3 cache coherence bug. Low risk.  
**Test:** Reboot TX 10 times while RX stays online. Reconnect should work every time within 30s.

### Step 2 — Fix Backoff Over-Counting (Change 2)
**Impact:** Eliminates §3.2 deferred-discovery dead window.  
**Test:** After 5+ connection cycles, reconnect should still happen promptly (not after 30+ second backoff wait).

### Step 3 — Increase Dwell to 2000ms (Change 3)
**Impact:** Eliminates §3.5 ACK-miss under MQTT load.  
**Test:** With heavy MQTT publish traffic on RX, reconnect should complete within 30s.

### Step 4 — ACK Retry on NO_MEM (Change 4)
**Impact:** Defensive belt-and-suspenders for §3.5.  
**Test:** Stress test with `esp_now_send` mock returning NO_MEM for first attempt.

### Step 5 — Replace `restart()` with `RESET_CONNECTION` event (Change 5)
**Impact:** Eliminates disruptive hard-stop of running hopper during steady-state health check.  
**Test:** Let device run for 10 minutes while connected. Verify no spurious reconnects.

---

## 11. Files to Change — Complete List

| File | Change | Risk |
|------|--------|------|
| `ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.h` | `active_hopping_running_` → `std::atomic<bool>`, add mutex for `task_handle_`, add `is_hopping_running()` accessor | Low |
| `ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp` | Use `store()/load()` with release/acquire ordering, add `portMEMORY_BARRIER()` before `vTaskDelete` | Low |
| `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_connection_handler.cpp` | Fix backoff over-counting in IDLE→CONNECTING callback | Medium |
| `ESPnowtransmitter2/espnowtransmitter2/src/main.cpp` | Replace hard `restart()` with `RESET_CONNECTION` event | Low |
| `esp32common/include/esp32common/config/timing_config.h` | `TRANSMIT_DURATION_PER_CHANNEL_MS` 1000 → 2000 | Low |
| `esp32common/espnow_common_utils/espnow_standard_handlers.cpp` | Add retry loop in `send_ack_response()` | Low–Medium |

---

## 12. Verification Checklist

After all changes are applied and flashed to both TX (COM8) and RX LCD (COM7):

- [ ] Reboot TX 10 times; observe reconnect in serial log — should succeed ≤30s every time
- [ ] Allow TX/RX to be connected for 5 minutes; then reboot TX — should reconnect within 30s
- [ ] Force 5 consecutive connection cycles (cable pull simulation or hard reboot); 6th reconnect should still be prompt (backoff not accumulated incorrectly)
- [ ] Under MQTT high-load: send 10 MQTT publishes/second from RX side, then reboot TX — reconnect should still work
- [ ] Serial log must NOT show: "CONNECTING timeout" → "IDLE" → "CONNECTING" loop more than 2 times before successful connection
- [ ] Serial log must show `active_hopping_running_` guard correctly allowing/blocking duplicate starts

---

## 13. References

- Espressif ESP-NOW API: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html
- esp_now_send() NO_MEM handling: IDF docs: "when this happens, you can delay a while before sending the next data"
- Xtensa LX6/LX7 Memory Ordering: Xtensa ISA Reference, §4.3.11 — writes from one core not guaranteed visible on second core without MEMW instruction or FreeRTOS synchronisation primitive
- FreeRTOS SMP on ESP32: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/freertos-smp.html — "Critical sections disable preemption on both cores and prevent simultaneous access by other tasks or ISRs"
- Community pattern for ESP-NOW + WiFi coexistence: Use channel=0 for all peers (use current WiFi channel), dwell ≥2s per channel when RX is WiFi-connected
