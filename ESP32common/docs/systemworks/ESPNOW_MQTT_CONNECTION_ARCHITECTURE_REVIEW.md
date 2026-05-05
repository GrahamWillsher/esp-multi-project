# ESP-NOW & MQTT Connection Architecture Review

**Date:** 2026-04-24  
**Scope:** ESPnowtransmitter2, espnowreceiver_LCD, espnowreceiver_2, esp32common shared library  
**Purpose:** Full understanding of connection/reconnection flow, buffer management, queue architecture, race conditions and bottlenecks.  
**Status of codebase reviewed:** Post-fixes (init_pending_ deferral, unconditional MQTT disconnect, TX buffer pool increases)

---

## Table of Contents

1. [System Overview](#1-system-overview)
2. [Common State Machine](#2-common-state-machine-espnowconnectionmanager)
3. [Transmitter Boot and Discovery](#3-transmitter-boot-and-discovery)
4. [Receiver Boot and Discovery](#4-receiver-boot-and-discovery)
5. [Connection Establishment Sequence](#5-connection-establishment-sequence)
6. [Connected-State Operations](#6-connected-state-operations)
7. [Disconnection and Reconnection](#7-disconnection-and-reconnection)
8. [Scenario Walkthroughs](#8-scenario-walkthroughs)
9. [ESP-NOW Queue Architecture](#9-esp-now-queue-architecture)
10. [WiFi Buffer Pool and MQTT Coexistence](#10-wifi-buffer-pool-and-mqtt-coexistence)
11. [MQTT Lifecycle on the Receiver](#11-mqtt-lifecycle-on-the-receiver)
12. [Identified Issues and Shortcomings](#12-identified-issues-and-shortcomings)
13. [Timing Reference Table](#13-timing-reference-table)
14. [Component Dependency Map](#14-component-dependency-map)
15. [Discovery Design — Receiver Probe Broadcasts: Confirmed Dead Code](#15-discovery-design--receiver-probe-broadcasts-confirmed-dead-code)
16. [Issue Register Addendum (Issues 10–11)](#16-issue-register-addendum-issues-1011)

---

## 1. System Overview

The system consists of one **Transmitter** (Battery Emulator, ESPnowtransmitter2) and one or two **Receivers** (espnowreceiver_LCD and/or espnowreceiver_2). They communicate via **ESP-NOW** over 802.11 at a channel negotiated at connection time. Each receiver optionally connects to an **MQTT broker** using its own locally-stored configuration (entered via the receiver's own web UI and persisted in its own NVS — fully independent of the transmitter's MQTT configuration).

> **Design Decision (confirmed 2026-04-24):** The **Transmitter is the sole initiator** of ESP-NOW communication. The receiver is purely reactive — it waits on its current WiFi channel and responds to incoming TX probes with an ACK. Receiver-side probe broadcasting is dead code and must be removed. See Section 15 for full analysis.

Both sides share a common library (`esp32common`) that contains:
- `EspNowConnectionManager` — the 3-state state machine (IDLE / CONNECTING / CONNECTED)
- `EspnowTxScheduler` — 4-priority queued send engine
- `espnow_standard_handlers` — PROBE and ACK message processing
- `EspNowHeartbeatMonitor` and `ReconnectionBackoff` — timeout and retry logic
- `timing_config.h` — single canonical source of all timing constants

---

## 2. Common State Machine (EspNowConnectionManager)

### States

```
    IDLE ──CONNECTION_START──► CONNECTING ──PEER_REGISTERED──► CONNECTED
     ▲                            │                               │
     │                            │ CONNECTION_LOST               │ CONNECTION_LOST
     │                            │ or RESET_CONNECTION           │ or RESET_CONNECTION
     └────────────────────────────┴───────────────────────────────┘
```

| Event | From State | To State |
|---|---|---|
| `CONNECTION_START` | IDLE | CONNECTING |
| `PEER_FOUND` | IDLE or CONNECTING | CONNECTING (stores MAC) |
| `PEER_REGISTERED` | CONNECTING | CONNECTED |
| `CONNECTION_LOST` | CONNECTED or CONNECTING | IDLE |
| `RESET_CONNECTION` | Any | IDLE |
| `DATA_RECEIVED` | CONNECTED | CONNECTED (no transition) |

### Event Queue
- **Capacity:** 10 events (`xQueueCreate(10, sizeof(EspNowStateChange))`)  
- **Post from ISR:** supported (`xQueueSend`, 100ms timeout)  
- **Processed by:** `process_events()` called from main loop / connection task  
- **Heartbeat timeout:** configurable; when enabled, monitors `EspNowHeartbeatMonitor` and auto-posts `CONNECTION_LOST`

### Heartbeat Timeouts
| Side | Threshold | Rationale |
|---|---|---|
| Transmitter | 35 000 ms | TX sends heartbeats every 10 s — 3.5× interval |
| Receiver | 40 000 ms | TX timeout (35 s) + 5 s grace |

### CONNECTING State Timeout
`set_connecting_timeout_ms()` exists but is **not called** on either TX or RX in the current codebase. The constant `ESPNOW_CONNECTING_TIMEOUT_MS = 30 000 ms` is defined but unused. **This means the CONNECTING state has no automatic escape** — it stays CONNECTING indefinitely if peer registration never completes (e.g. `esp_now_add_peer()` failure after ACK arrives).

---

## 3. Transmitter Boot and Discovery

### Bootstrap Sequence (main.cpp phases 1–8)

```
Phase 1 – hardware:     Serial, NVS, hardware GPIO
Phase 2 – persistence:  ChannelManager (loads last channel from NVS)
Phase 3 – battery:      Battery Emulator init (CAN, datalayer)
Phase 4 – connectivity: WiFi STA mode, Ethernet (LAN8720)
Phase 5 – espnow:       esp_now_init(), register recv/send callbacks,
                         EspNowConnectionManager::init(),
                         TransmitterConnectionHandler::init()
Phase 6 – data layer:   DataLayer, cache init
Phase 7 – tasks:        TransmissionTask (Core 1, Prio 2),
                         HeartbeatManager::init(),
                         DataSender::start(),
                         DiscoveryTask::start(),
                         MqttTask (optional, if feature enabled)
Phase 8 – net services: Ethernet DHCP, NTP, OTA
```

After phase 5, `TransmitterConnectionHandler::init()` registers a state-change callback and sets up the heartbeat monitor, then posts `CONNECTION_START` indirectly when discovery begins.

### Discovery Task

**Normal path (after first boot or cold start):**

`start_discovery()` → posts `CONNECTION_START` → state machine enters CONNECTING → `start_discovery_hopping_only()` → `DiscoveryTask::start_active_channel_hopping()` spawns `active_channel_hopping_task` on Core 1 at Priority 2 (LOW).

`active_channel_hop_scan()` scans channels in circular order starting from the last saved channel (`TxStateMachine::last_known_channel()` or NVS via `ChannelManager`):

```
For each channel (1–13, starting from saved channel):
  set_channel(ch)                         ← esp_wifi_set_channel()
  delay(CHANNEL_STABILIZATION_MS=150ms)
  
  For TRANSMIT_DURATION_PER_CHANNEL_MS (1000ms):
    send_probe_on_channel(ch)              ← every PROBE_INTERVAL_MS (200ms) = 5 probes/channel
    check EspnowQueueManager::discovery_queue for ACK
    vTaskDelay(10ms)
  
  If ACK received:
    Extract receiver channel from ACK payload
    esp_wifi_set_channel(ack_channel)
    delay(50ms settle)
    EspnowPeerManager::add_peer(ack_mac, ack_channel)
    TransmitterConnectionHandler::on_peer_registered()  ← posts PEER_REGISTERED
    break
```

**Full scan time:** 13 channels × 1 000 ms = **13 s maximum** (starting from saved channel wraps around, so best case = 1 s if saved channel is correct).

After scan completes (success): `active_hopping_running_ = false`, task self-deletes.

### Probe Frame
```cpp
struct probe_t { uint8_t type; uint32_t seq; };  // 5 bytes
// type = msg_probe, seq = millis() as timestamp
```
Sent via **direct `esp_now_send()`** (not the scheduler), as the scheduler may not be initialised at this point. Peer added with `channel=0` (current WiFi channel).

### Reconnect Path
When `TransmitterConnectionHandler` sees CONNECTED→IDLE transition, it calls `TxStateMachine::on_connection_lost()`. Auto-reconnect fires `CONNECTION_START`. The callback handler checks `should_attempt_reconnect()` (exponential backoff) and then calls `start_discovery_hopping_only()`.

On `DiscoveryTask::restart()`:
- If `g_lock_channel == 0` (no saved channel): falls back to `start_active_channel_hopping()` (full scan)
- Otherwise: `restart_cleanup_peers()` → `force_and_verify_channel(g_lock_channel)` → `EspnowDiscovery::instance().restart()` → waits `RESTART_STABILIZATION_DELAY_MS (100ms)` → verifies channel

---

## 4. Receiver Boot and Discovery

### Boot Sequence (both LCD and _2)

The receiver starts WiFi in STA mode, initialises ESP-NOW, then calls `ReceiverConnectionHandler::init()` which:

1. Sets heartbeat timeout to 40 000 ms on `EspNowConnectionManager`
2. Registers the CONNECTED / CONNECTED→IDLE / CONNECTING state callbacks
3. Posts `CONNECTION_START` → state machine enters CONNECTING
4. `EspnowDiscovery::instance().start()` — **currently** starts a task that broadcasts PROBE announcements every `ANNOUNCEMENT_INTERVAL_MS (5 000 ms)`. **This is dead code (confirmed — see Section 15) and must be removed.** The receiver has no need to broadcast probes; it is purely passive.

**The receiver is passive with respect to channel selection.** It stays on its current channel and does NOT scan channels. It simply waits for the transmitter to send a PROBE on its channel, responds with an ACK, and the connection is established.

### How the Receiver's Discovery Broadcasts Interact (Dead Code — Pending Removal)

The receiver's `EspnowDiscovery` currently broadcasts `probe_t` frames at 5 s intervals via `EspnowTxScheduler`. However, the transmitter's `active_channel_hop_scan()` only inspects the **discovery queue for `msg_ack` frames** — it does not handle `msg_probe`. These broadcasts have **zero effect on connection establishment** and are confirmed dead code:

> RX probes arrive on TX's main queue → TX sends a wasted ACK back to RX → RX's `g_ack_config` has all fields `nullptr` → RX ignores the inbound ACK → neither state machine advances.

**Confirmed design decision:** The call to `EspnowDiscovery::instance().start()` on the receiver, and all associated dead code paths, must be removed when this work is implemented. See Section 15.

### Receiver Discovery Queue
The receiver uses a **separate** `espnow_discovery_queue` that is populated by the ESP-NOW receive ISR for PROBE/ACK frames, independent of the main RX task queue. This prevents the main data processing task from consuming ACK frames meant for the discovery logic.

---

## 5. Connection Establishment Sequence

### Full sequence (TX scans, finds RX):

```
Time →

TX                                      RX
──                                      ──
active_channel_hop_scan() starts
  set_channel(ch=6)
  send PROBE broadcast ──────────────► handle_probe() fires (ISR → queue → task)
                                        EspnowPeerManager::add_peer(tx_mac, ch=6)
                                        send_ack_response() [DIRECT esp_now_send()]
                                          → 3 retries if NO_MEM
                        ◄────────────── ACK { type=1, seq=probe_seq, channel=6 }
                                        post PEER_FOUND
                                        post PEER_REGISTERED
                                        ── state: CONNECTING → CONNECTED ──
                                        CONNECTED callback fires:
                                          RxStateMachine: CONNECTED
                                          RxHeartbeatManager: on_connection_established()
                                          EspnowDiscovery::suspend()
                                          ChannelManager::lock_channel(6)
                                          connected_at_ms_ = millis()
                                          init_pending_ = true   ← DEFERRED
                                        [ACK already sent above, queue empty]

on_ack_received(mac, ch=6)
  post PEER_FOUND
on_peer_registered(mac)
  post PEER_REGISTERED
  ── state: CONNECTING → CONNECTED ──
  CONNECTED callback fires:
    TxSendGuard: connected=true
    ChannelManager::lock_channel(6)
    HeartbeatManager::reset()
    TxStateMachine::on_connected(6)
    led_publish_current_state(true, mac)

                                        tick() fires (2 s after connected_at_ms_):
                                          init_pending_ → send_initialization_requests()
                                          → REQUEST_DATA (power_profile) [queued, P2/DATA]
                                          → version_announce [queued, P3/MONITORING]
                                          → return (yield for scheduler)

  HeartbeatManager::tick()
  → send_heartbeat() every 10 000 ms  ──────────────►
                                        RxHeartbeatManager processes heartbeat
                                        → sends heartbeat_ack [queued, P0/CONTROL]
                        ◄──────────────
  on_heartbeat_ack()
  → TxStateMachine::on_heartbeat_ack()
  → EspNowConnectionManager::on_heartbeat_received()
```

### Key Ordering Guarantee (Post-Fix)

The probe ACK is sent via **direct `esp_now_send()`** in `send_ack_response()` (up to 3+1 attempts before falling back to scheduler) BEFORE the CONNECTED state callback fires. The CONNECTED callback now only sets `init_pending_ = true` instead of calling `send_initialization_requests()` directly. This gives the ACK direct-path at least one attempt with an empty driver queue.

The `tick()` function has an additional 2 000 ms post-connect grace period, further ensuring the ACK is not competing with init-burst frames for WiFi driver buffer slots.

---

## 6. Connected-State Operations

### Transmitter Tasks (when CONNECTED)

| Task | Core | Priority | Period | Send Path |
|---|---|---|---|---|
| HeartbeatManager | 1 (main loop tick) | — | 10 000 ms | `TxSendGuard` → `esp_now_send()` direct |
| TemperatureReport | 1 (inside heartbeat) | — | 10 000 ms | `esp_now_send()` direct (best-effort, no scheduler) |
| TransmissionTask | 1 | LOW (2) | 50 ms | `EspnowTxScheduler::send()` |
| DataSender | 1 | LOW (2) | 2 000 ms | `EspnowTxScheduler::send()` |
| VersionBeaconManager | via loop | — | 15 000 ms | `EspnowTxScheduler::send()` |
| LEDPublish | on CONNECTED, then periodic | — | on change | `esp_now_send()` direct via LED handler |

**Note:** `HeartbeatManager::send_heartbeat()` uses `TxSendGuard::send_to_receiver_guarded()` not the scheduler. This means heartbeats bypass the scheduler queue entirely and go direct to `esp_now_send()` with the send-guard recovery logic. This is intentional — heartbeats must not be delayed by queue depth.

### Receiver Tasks (when CONNECTED)

| Action | Trigger | Send Path |
|---|---|---|
| REQUEST_DATA (power profile) | `send_initialization_requests()` at t+2s | `EspnowTxScheduler` P2/DATA |
| version_announce | Same as above | `EspnowTxScheduler` P3/MONITORING |
| heartbeat_ack | Per incoming heartbeat | `EspnowTxScheduler` P0/CONTROL |
| Config section requests | `tick()` retry loop, `CONFIG_RETRY_INTERVAL_MS` | `EspnowTxScheduler` P2/DATA |
| Catalog requests | `tick()` catalog retry engine | `EspnowTxScheduler` P2/DATA |
| LED state request | `tick()` led_sync bounded retry | `EspnowTxScheduler` P2/DATA |
| REQUEST_DATA retry | `tick()`, every `RETRY_INTERVAL_MS` if no power data | `EspnowTxScheduler` P2/DATA |

### Heartbeat Staleness Gate (Receiver)

Both `rx_connection_handler::tick()` and `mqtt_task` check:
```cpp
if (EspNowConnectionManager::instance().ms_since_last_heartbeat() >= 12000) {
    return;  // suppress all outbound traffic
}
```

This gate fires when the transmitter has been silent for 12 s (heartbeat interval is 10 s, so this requires one full missed heartbeat). It suppresses all ESP-NOW outbound traffic from `tick()` and forces MQTT offline simultaneously, freeing the shared WiFi driver buffer pool so that when TX finds the receiver again, the ACK frame has headroom.

**Headroom:** 12 s gate vs 10 s heartbeat interval → 2 s grace before suppression. One late heartbeat does NOT trigger the gate; two consecutive late heartbeats will.

---

## 7. Disconnection and Reconnection

### Transmitter loses connection (TX-side timeout)

```
HeartbeatManager::on_heartbeat_ack() not called for 35 000 ms
  → heartbeat_monitor_.connection_lost() = true
  → EspNowConnectionManager::process_events() detects timeout
  → post CONNECTION_LOST
  → state: CONNECTED → IDLE
  → IDLE→CONNECTED callback:
      TxSendGuard: connected=false
      HeartbeatManager::reset()
      TxStateMachine::on_connection_lost()
      EspnowPeerManager::remove_peer(receiver_mac)
      ChannelManager::unlock_channel()
  → auto-reconnect fires CONNECTION_START
  → TransmitterConnectionHandler: checks should_attempt_reconnect() (backoff)
  → start_discovery_hopping_only()
```

Reconnect starts with the saved channel (`g_lock_channel`). TX tries that channel first (1 000 ms scan), then falls back to full 13-channel scan.

### Receiver loses connection (RX-side timeout)

```
RxHeartbeatManager: no heartbeat for 40 000 ms
  → EspNowConnectionManager: heartbeat_monitor_.connection_lost()
  → post CONNECTION_LOST
  → state: CONNECTED → IDLE
  → IDLE→CONNECTED callback:
      RxStateMachine::on_connection_lost()
      ReceiverConnectionHandler::on_connection_lost():
        first_data_received_ = false
        power_data_confirmed_ = false
        connected_at_ms_ = 0
        last_retry_ms_ = 0
        last_config_retry_ms_ = 0
        init_pending_ = false
        transmitter_mac_ cleared
        led_sync_.reset(), catalog_retry_.reset()
      EspnowPeerManager::remove_peer(transmitter_mac)
      ChannelManager::unlock_channel()
      EspnowDiscovery::resume()
  → post CONNECTION_START → IDLE → CONNECTING
  → EspnowDiscovery starts broadcasting PROBEs again on unlocked channel
```

RX waits passively (sending periodic PROBEs) until TX scans the right channel.

### MQTT During Reconnection

The MQTT gate (`heartbeat_fresh = ms_since_last_heartbeat() < 12000`) closes **before** the heartbeat monitor fires the full CONNECTION_LOST. At 12 s of silence:
1. MQTT gate closes → `MqttClient::disconnect()` called unconditionally
2. `gate_closed_at_ms` is set
3. All `tick()` outbound ESP-NOW suppressed

After TX reconnects and heartbeats resume:
1. Gate re-opens
2. `POST_RECONNECT_SETTLE_MS = 4 000 ms` hold before MQTT can reconnect
3. During settle: init burst fires (2 s post-connect grace + scheduler)
4. After settle: MQTT reconnects

---

## 8. Scenario Walkthroughs

### 8.1 TX Boots First, RX Boots Later

```
t=0s    TX boots, completes phases 1-8
t=~3s   TransmitterConnectionHandler::init() posts CONNECTION_START
        active_channel_hopping_task starts scanning (ch 1, 2, 3 ...)
        Scan cycles every 13s. TX sends PROBEs endlessly.
        
t=30s   RX boots (example)
        RX completes init, posts CONNECTION_START, starts discovery
        RX is on channel=6 (its default or last-saved)
        
t=~36s  TX scan reaches channel 6
        TX PROBE → RX receives → RX sends ACK → TX receives ACK
        Connection established (see Section 5)
        
t=~38s  RX tick() fires: sends REQUEST_DATA + version_announce
        TX sends first heartbeat at t+10s from connection
```

**Maximum delay:** If TX is on channel 6 and starts scanning from channel 1 (no saved channel), worst case is 6 s before hitting channel 6. With saved channel from NVS, best case is <1 s.

### 8.2 RX Boots First, TX Boots Later

```
t=0s    RX boots, init completes, CONNECTION_START posted
        EspnowDiscovery starts: PROBE broadcast every 5s on current channel
        State: CONNECTING (indefinitely — no CONNECTING timeout)
        
        Note: RX's PROBE broadcasts do NOT help TX discover RX (confirmed dead code).
        TX is the sole active scanner. Confirmed design: remove RX probe broadcasts (Section 15).
        
t=60s   TX boots (example)
        Starts active channel hopping from saved/NVS channel
        
t=~61s  TX hits RX's channel
        TX PROBE → RX handles → RX sends ACK → TX connects
```

**Note:** During the pre-connection wait, RX is in CONNECTING state with no timeout. This is correct behaviour (RX must wait forever for TX), but it means any code that checks `is_connected()` before connection is established will behave accordingly. MQTT gate correctly blocks MQTT in this state.

### 8.3 TX Reboots (RX Stays Running)

```
TX reboots (power loss / firmware update)
    
RX: last heartbeat received at T0
    At T0+12s:  MQTT gate closes (heartbeat_fresh=false)
                tick() suppresses all outbound ESP-NOW
    At T0+40s:  heartbeat timeout → CONNECTION_LOST posted
                on_connection_lost() clears all state
                EspnowDiscovery::resume()
                State: IDLE → CONNECTION_START → CONNECTING
    
TX reboots at T0+Xms (unknown)
    TX boots, scans from g_lock_channel (same channel RX is on)
    TX PROBE → RX ACK → connects
    At TX connect time + 2s: RX init burst fires
    At TX connect time + 4s: MQTT gate settle expires → MQTT reconnects
```

**Critical path:** RX must have unlocked its channel before TX scan. This happens at T0+40s (heartbeat timeout fires). If TX reboots and completes boot in <40 s, TX will reach RX's channel but RX still has it locked. However, RX's channel lock does NOT prevent RX from receiving or responding to PROBEs — the lock only controls `ChannelManager`, not the WiFi driver. So the ACK will still go out. The connection will establish correctly.

### 8.4 RX Reboots (TX Stays Running)

```
RX reboots
    
TX: last heartbeat_ack received at T0
    At T0+35s:  heartbeat timeout → CONNECTION_LOST
                TxSendGuard: connected=false
                HeartbeatManager::reset()
                Peer removed, channel unlocked
                auto-reconnect → CONNECTION_START → CONNECTING
                
RX reboots at T0+Xs (unknown)
    RX comes back on same channel
    RX starts EspnowDiscovery (PROBE every 5s)
    
TX: DiscoveryTask::restart() → tries g_lock_channel (= RX's channel)
    set_channel(g_lock_channel)
    EspnowDiscovery::restart()
    TX PROBE → RX ACK → connects
```

**If RX reboots before TX's 35s timeout:** TX still thinks it's CONNECTED. TX sends heartbeats to RX (which are lost). No reconnect until 35s timeout. This is the expected maximum reconnect latency.

### 8.5 MQTT Broker Goes Offline (ESP-NOW Still Running)

```
MQTT broker offline or unreachable

MqttClient::loop() calls PubSubClient::loop()
  → detect disconnect
  → mqtt_task: connection attempt at MQTT_RECONNECT_INTERVAL_MS (5s)
  
Heartbeats still flowing → heartbeat_fresh=true → gate OPEN
MQTT retries indefinitely at 5s intervals
  
Each retry: TCP SYN → TCP timeout (~10s default TCP timeout)
  → During TCP connect, WiFiClient socket holds driver TX buffer descriptors
  → This is the ORIGINAL root cause of the NO_MEM issue (now mitigated)
  
After fix: disconnect() is called unconditionally on EVERY gate-closed iteration
  → But gate only closes when heartbeat_fresh=false, not when MQTT fails
  → During normal operation with MQTT broker offline:
      Gate is OPEN (heartbeats fresh)
      MQTT retries continuously
      TCP connect attempts hold buffer slots
```

**⚠ Shortcoming identified:** When ESP-NOW is healthy but MQTT broker is unreachable, MQTT retries every 5 s with TCP connect attempts. Each in-progress TCP connect holds a WiFi driver TX buffer descriptor. With the broker unreachable (RST or timeout), there is a window where multiple TCP connects could be in-flight. This is mitigated by PubSubClient's single-socket model but worth monitoring. See Section 12.

### 8.6 WiFi Channel Drift (RX Channel Changes)

This scenario cannot currently occur — both sides lock their WiFi channel (`ChannelManager::lock_channel()`) immediately on connection. Neither side changes channel while connected. The only time channel can change is during disconnected/CONNECTING state.

---

## 9. ESP-NOW Queue Architecture

### Queue Layout

`EspnowTxScheduler` creates **4 independent FreeRTOS queues** with a single worker task that services them in strict priority order (P0 first):

```
Priority 0 — CONTROL    (30% of total depth, min 1)
Priority 1 — DISCOVERY  (20% of total depth, min 1)
Priority 2 — DATA       (40% of total depth, min 1)
Priority 3 — MONITORING (remainder, min 1)
```

For `queue_depth=24` (espnowreceiver_2 default):
- P0 CONTROL:    7 slots
- P1 DISCOVERY:  4 slots
- P2 DATA:       9 slots
- P3 MONITORING: 4 slots

For `queue_depth=16` (espnowreceiver_LCD):
- P0 CONTROL:    4 slots
- P1 DISCOVERY:  3 slots
- P2 DATA:       6 slots
- P3 MONITORING: 3 slots

### Message Type Classification

| Message Type | Priority Queue | min_gap_ms | retry_attempts |
|---|---|---|---|
| `msg_ack` (type=1) | P0 CONTROL | 10 | 4 |
| `msg_heartbeat` (type=2) | P0 CONTROL | 800 | 4 |
| `msg_heartbeat_ack` | P0 CONTROL | 20 | 4 |
| `msg_probe` | P1 DISCOVERY | 250 | 3 |
| `msg_request_data` | P2 DATA | 1000 | 3 |
| `msg_battery_status` | P2 DATA | 1000 | 1 |
| `msg_charger_status` | P2 DATA | 500 | 1 |
| `msg_inverter_status` | P2 DATA | 500 | 1 |
| `msg_system_status` | P2 DATA | 500 | 1 |
| `msg_config_section_request` | P2 DATA | 3000 | 3 |
| `msg_version_beacon` | P3 MONITORING | 60000 | 1 |
| `msg_temperature_report` | P3 MONITORING | 1000 | 1 |
| All others | P2 DATA | 100 | 0 |

### Queue Draining — Worker Task

```
task_tx_worker (EspnowTx, Core 1, Priority 1):
  loop:
    dequeue_next_priority_item()   ← checks P0, P1, P2, P3 in order
    if min_gap_ms not elapsed:
      requeue_deferred(same priority)    ← frame goes back to tail
      if requeue fails: frame dropped + stats.cadence_defer++
      vTaskDelay(1ms)
    else:
      send_immediate_with_retry():
        esp_now_send() up to retry_attempts times
        delay(retry_base_delay_ms × attempt) between retries
      inter_frame_delay_ms = 2ms between frames
```

**Important:** When a frame is re-queued due to min_gap not elapsed, it goes to the **tail** of its queue. For CONTROL queue (no lossy drop), this means a heartbeat_ack that fires too fast (< 20 ms after previous) will cycle through the queue once per ms until the gap elapses. With 4-slot CONTROL queue and heartbeat_acks arriving at ~10 s intervals, this is not a problem.

### Drop Behaviour

| Queue | Drop Policy |
|---|---|
| P0 CONTROL | **Never dropped** — `xQueueSend` with 2ms timeout only; no lossy fallback |
| P1–P3 | Lossy: if full, dequeue oldest then enqueue newest |

**⚠ Issue:** The CONTROL queue uses `xQueueSend` with a 2 ms timeout. If the CONTROL queue is full (4 or 7 slots depending on config) AND the worker task is busy with a long retry cycle, the 2 ms timeout may expire and the ACK is **returned as ESP_ERR_ESPNOW_NO_MEM to the caller** even though CONTROL frames are "never dropped". The caller (`send_ack_response()`) handles this by falling back to the scheduler — but if the scheduler itself returns this, the ACK is silently lost.

### `send_ack_response()` — Special Case

The probe ACK bypasses the scheduler entirely using a direct path:

```cpp
// Try direct esp_now_send() up to 4 attempts (3 retries)
for attempt in 0..3:
    result = esp_now_send(peer_mac, &ack, sizeof(ack))
    if OK: return true
    if NO_MEM and attempt < 3: delay(2ms * (attempt+1))

// Only if direct path fails: try scheduler
if EspnowTxScheduler::is_ready():
    return EspnowTxScheduler::send(peer_mac, &ack, ...)
```

This is the highest-priority send path in the codebase. It does not go through any queue.

### What Bypasses the Scheduler

These sends go **direct to `esp_now_send()`** and compete with scheduler output:

1. **Probe ACK** (`send_ack_response()`) — direct first, scheduler fallback
2. **TX Heartbeat** (`HeartbeatManager::send_heartbeat()` via `TxSendGuard`)
3. **TX Temperature Report** (`send_temperature_report()` — best-effort only)
4. **TX LED state** (`led_publish_current_state()`)
5. **Discovery PROBEs** (TX, `send_probe_on_channel()`)

---

## 10. WiFi Buffer Pool and MQTT Coexistence

### The Shared Buffer Problem

ESP-NOW frames and TCP/IP (MQTT) share the same WiFi driver dynamic TX buffer pool:

```
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM = 64   (LCD, after fix)
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM = 64   (receiver_2, after fix — was missing/defaulted to 32)
CONFIG_ESP_WIFI_ESPNOW_MAX_PENDING_LEN = 20  (both receivers)
```

Every open TCP socket (MQTT connection attempt or established connection) holds a number of buffer descriptors. Every pending ESP-NOW frame also holds buffer descriptors from `espnow_alloc_buf`. When the pool is exhausted, `esp_now_send()` returns `ESP_ERR_ESPNOW_NO_MEM`.

### The Fix (Implemented)

Three-part mitigation:

**1. Unconditional MQTT disconnect**
```cpp
// Previous (buggy):
if (MqttClient::isConnected()) MqttClient::disconnect();

// Fixed:
MqttClient::disconnect();  // always — aborts TCP socket in any state
```
This ensures in-progress TCP SYN sockets (state=-2, `isConnected()=false`) are torn down.

**2. 4-second post-reconnect settle window**
After the MQTT gate re-opens (TX reconnected), MQTT is held off for 4 s to allow the ESP-NOW init burst to complete without TCP competition.

**3. Increased buffer pool**
`CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=64` on both receivers.

### MQTT Gate Logic (Both Receivers, Identical Code)

```
Every MQTT_TASK_POLL_MS (100ms):
  state_ok = (RxStateMachine == CONNECTED or ACTIVE)
  heartbeat_fresh = (ms_since_last_heartbeat < 12000)
  espnow_ready = state_ok AND heartbeat_fresh
  
  if NOT espnow_ready:
    gate_closed_at_ms = millis()
    MqttClient::disconnect()           ← unconditional
    continue (skip MQTT operations)
  
  if (gate_closed_at_ms > 0) AND (millis() - gate_closed_at_ms < 4000):
    continue (post-reconnect settle)
  
  // Gate open and settled: normal MQTT operations
  if mqtt_enabled AND server != 0.0.0.0:
    if config_changed: MqttClient::init(...)
    MqttClient::loop()
```

---

## 11. MQTT Lifecycle on the Receiver

### Two Separate MQTT Contexts (Important)

There are **two completely independent** MQTT configurations on every receiver. Understanding the difference is critical:

| | `ReceiverNetworkConfig` (NVS) | `TransmitterMqttSpecs` (RAM cache) |
|---|---|---|
| **Purpose** | Receiver's own MQTT connection | Mirror of transmitter's MQTT settings |
| **Source** | Receiver's own web UI settings page | Received from TX via `config_section_mqtt` frames |
| **Persisted** | Yes — receiver NVS | Yes — separate NVS namespace |
| **Used by** | `mqtt_task.cpp` to connect to broker | Receiver web UI to display TX broker status |
| **Credentials** | Receiver-specific | Transmitter's credentials |

`mqtt_task.cpp` reads **only** from `ReceiverNetworkConfig`. The `TransmitterMqttSpecs` cache is a read-only mirror for the web UI and does NOT feed the receiver's MQTT connection.

### Should MQTT Credentials Be a Single Point of Truth?

The current design (each device has independent MQTT config) is architecturally correct. Reasons:

**Client ID must be unique per device.** The MQTT specification requires that each client connecting to a broker has a unique client ID. If two devices share a client ID, the broker will disconnect the first client when the second connects. Each device — transmitter, LCD receiver, _2 receiver — must have a different client ID.

**Credentials should be device-specific for security.** Sharing a username/password across all devices means that compromise of any one device exposes the credentials for all. MQTT brokers (e.g. Mosquitto, EMQX) support per-client ACLs: each device authenticates with its own credentials and is restricted to its own topic namespace. This is the recommended practice.

**Broker address/port** could in principle be a single source of truth (pushed from TX to RX), but having each device configured independently gives flexibility — a receiver could connect to a local broker while the transmitter uses a cloud broker, for example. Configuration drift between devices is a practical management concern; this should be addressed by good documentation of the setup, not by coupling device configurations.

**Recommendation:** Keep independent per-device MQTT configuration. Ensure each device uses a unique, device-specific client ID (e.g. based on MAC address suffix). Use broker ACLs to enforce topic-level isolation. The `server == 0.0.0.0` guard in `mqtt_task.cpp` already ensures the receiver won't attempt MQTT until its own config has been populated via its own web UI.

### MQTT Startup Sequence (first boot)

```
t=0       mqtt_task starts → vTaskDelay(2000ms = MQTT_TASK_STARTUP_DELAY_MS)
t=2s      Loop begins
          ESP-NOW not CONNECTED → gate closed → MqttClient::disconnect() → continue
          
t=~4s     TX connects via ESP-NOW
t=~4s     CONNECTED callback → init_pending_ = true
t=~6s     tick() fires init burst: REQUEST_DATA + version_announce
          (config requests go to TX; TX responds with tx-side config sections)
          
t=~4s+4s  Post-reconnect settle expires (~8s after connect)
t=~8s     MQTT gate settles
          If ReceiverNetworkConfig server != 0.0.0.0 → MqttClient::init() → connect
          (ReceiverNetworkConfig was populated by the user via the receiver's web UI,
           NOT by incoming ESP-NOW frames from the transmitter)
```

**First-boot note:** On a brand new receiver, MQTT will NOT start until the user opens the receiver's web UI, enters the broker address/port/credentials, and saves. The `server == 0.0.0.0` guard enforces this correctly.

### MQTT Reconnect After ESP-NOW Link Loss

```
TX reboots (example):
t=0        Last heartbeat
t=12s      Gate closes → MQTT disconnects (unconditional disconnect())
t=40s      Heartbeat timeout → ESP-NOW CONNECTED→IDLE, EspnowDiscovery::resume()
           
t=40s+Xs   TX reconnects via channel hop scan
t+2s       RX init burst: REQUEST_DATA + version_announce
t+4s       Post-reconnect settle expires → MQTT reconnects
           (ReceiverNetworkConfig still holds credentials from NVS — no re-fetch needed)
```

---

## 12. Identified Issues and Shortcomings

### ISSUE 1: CONNECTING State Has No Timeout ⚠ Medium

**Location:** `connection_manager.cpp:set_connecting_timeout_ms()` — never called  
**Symptom:** If CONNECTING state is entered but peer registration never completes (e.g. `esp_now_add_peer()` fails, or ACK arrives but channel mismatch prevents peer add), the state machine stays in CONNECTING forever. Discovery task may be spinning.  
**Constant available:** `TimingConfig::ESPNOW_CONNECTING_TIMEOUT_MS = 30 000 ms`  
**Fix:** Call `set_connecting_timeout_ms(ESPNOW_CONNECTING_TIMEOUT_MS)` in both `TransmitterConnectionHandler::init()` and `ReceiverConnectionHandler::init()`.

---

### ISSUE 2: Connection Event Queue Depth is Only 10 ⚠ Low-Medium

**Location:** `connection_manager.cpp:init()` — `xQueueCreate(10, ...)`  
**Symptom:** During rapid reconnection events (connection loss + multiple probe/ack exchanges), the event queue can overflow and events are silently dropped (xQueueSend fails, post_event returns false but caller ignores return). In normal operation this is fine; under stress it can cause the state machine to miss events.  
**Fix:** Increase to 20 or add monitoring of dropped events.

---

### ISSUE 3: MQTT Retries Hold Buffer Slots When Broker is Unreachable ⚠ Low (mitigated)

**Location:** `mqtt_task.cpp` — broker unavailable but ESP-NOW healthy  
**Symptom:** When MQTT broker is offline but ESP-NOW is running, MQTT reconnects every 5 s. Each in-progress TCP connect attempt holds a WiFi driver buffer. The gate only closes on `heartbeat_fresh=false` (12s), not on MQTT failure. With 64 buffer slots and ESP-NOW at typical load (heartbeat every 10s + battery data every 2s), this is unlikely to cause NO_MEM, but it is a latent risk.  
**Current mitigation:** `MqttClient::disconnect()` is called unconditionally before reconnect. PubSubClient uses a single socket (not multiple). Buffer pool is 64.  
**Recommendation:** Consider setting TCP connect timeout to ≤2 s to bound buffer hold time.

---

### ISSUE 4: Receiver's PROBE Broadcasts Don't Accelerate Discovery ℹ Informational

**Location:** `espnow_discovery.cpp` (common) — receiver broadcasts PROBEs every 5 s  
**Current behaviour:** Receiver sends PROBE every 5 s. Transmitter's scan loop only checks for `msg_ack` responses, not `msg_probe`. The receiver's PROBEs are received by TX (they arrive in the discovery queue) but the inner scan loop only calls `receive_from_discovery_queue()` and only acts on `msg_ack`. The PROBEs are silently discarded.  
**Impact:** None on connection time — the transmitter drives discovery. The receiver's PROBEs waste a small amount of radio air time and scheduler queue slots.  
**Recommendation:** Either suppress receiver PROBEs while in CONNECTING state (or entirely), or handle `msg_probe` on the transmitter side to allow passive-discovery fallback.

---

### ISSUE 5: Heartbeat Gap vs Gate Threshold is Only 2 s ℹ Low

**Location:** `rx_connection_handler::tick()` and `mqtt_task.cpp` — `ms_since_last_heartbeat() >= 12000`  
**Heartbeat interval:** 10 000 ms  
**Gate threshold:** 12 000 ms → only 2 s headroom before gate fires  
**Symptom:** A single delayed heartbeat (e.g. due to scheduler congestion on the transmitter, or a busy CAN/Ethernet event loop) can trigger the gate, immediately disconnecting MQTT. With MQTT_RECONNECT_INTERVAL=5s and POST_RECONNECT_SETTLE=4s, a single false gate close + reopen costs ~9 s of MQTT downtime.  
**Recommendation:** Consider increasing gate threshold to 15 000–20 000 ms (1.5–2 heartbeat intervals). The cost is an extra 3–8 s of buffer contention before MQTT disconnects during a real TX scan. Alternatively, add hysteresis (gate closes at 12s, re-opens only after 2 fresh heartbeats).

---

### ISSUE 6: `version_announce` Has No Retry ℹ Low

**Location:** `rx_connection_handler::send_initialization_requests()` — `version_announce` sent once, no retry  
**Symptom:** If the version_announce frame is dropped (queue full, NO_MEM), TX never learns the receiver's firmware version. This is a metadata/diagnostic loss only — connection still works.  
**Fix:** Add version_announce to the catalog retry engine or retry it periodically (low cadence).

---

### ISSUE 7: TemperatureReport Bypasses Scheduler Entirely ℹ Informational

**Location:** `heartbeat_manager.cpp::send_temperature_report()` — direct `esp_now_send()` with no retry  
**Comment in code:** "Best-effort telemetry: intentionally bypass TxSendGuard recovery/backoff"  
**Impact:** Under buffer pressure, temperature reports silently fail. This is intentional design. Document notes this as confirmed expected behaviour.

---

### ISSUE 8: `g_lock_channel` is a Raw Global uint8_t ℹ Low

**Location:** `espnow_transmitter.h` / transmitter — `uint8_t g_lock_channel`  
**Comment in code:** "uint8_t atomic on ESP32"  
**Risk:** On ESP32, 32-bit aligned accesses are atomic, but 8-bit accesses to unaligned addresses may not be. In practice `uint8_t` on ESP32 Xtensa is single-instruction read/write and is safe. The existing code comments acknowledge this. No action needed, but be cautious if migrating to RISC-V targets (ESP32-C3/S3) which have different atomicity guarantees.

---

### ISSUE 9: RX Discovery Channel is Fixed — No Channel Correction After RX Reboot ℹ Low

**Location:** `ReceiverConnectionHandler::init()` → `EspnowDiscovery::start()` on current channel  
**Scenario:** If RX reboots and its NVS has a stale channel stored, it comes up on a different channel than TX expects. TX will try `g_lock_channel` first (the old channel). This fails, then TX full-scans and finds RX on the new channel. No data loss, but extra 13s scan time.  
**Recommendation:** RX should persist its last connected channel in NVS and restore it on boot. Currently TX persists via `ChannelManager`/NVS; RX depends on WiFi default.

---

## 13. Timing Reference Table

| Constant | Value | Location | Purpose |
|---|---|---|---|
| `HEARTBEAT_INTERVAL_MS` | 10 000 | timing_config | TX sends heartbeat every N ms |
| `TX_HEARTBEAT_TIMEOUT_MS` | 35 000 | timing_config | TX declares RX gone after N ms silence |
| RX heartbeat timeout | 40 000 | rx_connection_handler::init() | RX declares TX gone after N ms |
| Heartbeat gate threshold | 12 000 | mqtt_task + rx_conn tick | MQTT gate + tick suppression trigger |
| `TRANSMIT_DURATION_PER_CHANNEL_MS` | 1 000 | timing_config | TX dwell per channel in active hop scan |
| `PROBE_INTERVAL_MS` | 200 | timing_config | TX sends PROBE every N ms on each channel |
| Full scan time (13ch) | ~13 000 | derived | TX scans all 13 channels |
| `ANNOUNCEMENT_INTERVAL_MS` | 5 000 | timing_config | RX broadcasts PROBE every N ms |
| `POST_RECONNECT_SETTLE_MS` | 4 000 | mqtt_task | MQTT wait after gate re-opens |
| Init burst grace | 2 000 | rx_connection_handler::tick() | Hold tick() sends after CONNECTED |
| `MQTT_TASK_STARTUP_DELAY_MS` | 2 000 | timing_config | Initial MQTT task boot delay |
| `MQTT_RECONNECT_INTERVAL_MS` | 5 000 | timing_config | MQTT broker retry interval |
| `CONFIG_RETRY_INTERVAL_MS` | 3 000 | rx_connection_handler.h | Config section re-request cadence |
| `RETRY_INTERVAL_MS` | 3 000 | rx_connection_handler.h | REQUEST_DATA retry cadence |
| `RETRY_REQUEST_TIMEOUT_MS` | 3 000 | rx_connection_handler.h | Window before first REQUEST_DATA retry |
| `CHANNEL_STABILIZATION_MS` | 150 | timing_config | WiFi channel settle after set_channel() |
| `RESTART_STABILIZATION_DELAY_MS` | 100 | timing_config | Wait after EspnowDiscovery::restart() |
| `RECOVERY_TIMEOUT_MS` | 60 000 | timing_config | Persistent failure → esp_restart() |
| `DISCOVERY_RETRY_INTERVAL_MS` | 5 000 | timing_config | Wait between full scan cycles |
| `ESPNOW_CONNECTING_TIMEOUT_MS` | 30 000 | timing_config | **Defined but not used** |
| Scheduler queue depth (LCD) | 16 | platformio.ini/app_config | P0=4, P1=3, P2=6, P3=3 |
| Scheduler queue depth (_2) | 24 | default InitOptions | P0=7, P1=4, P2=9, P3=4 |
| `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM` | 64 | sdkconfig.defaults (both) | Shared WiFi driver TX pool |
| `CONFIG_ESP_WIFI_ESPNOW_MAX_PENDING_LEN` | 20 | sdkconfig.defaults (both) | Max queued ESP-NOW driver frames |

---

## 14. Component Dependency Map

```
┌─────────────────────────────────────────────────────────┐
│                    TRANSMITTER                          │
│                                                         │
│  main.cpp ──phases──► TransmitterConnectionHandler      │
│                             │                           │
│                    registers│state callbacks            │
│                             ▼                           │
│               EspNowConnectionManager ◄── events ───┐  │
│                (IDLE/CONNECTING/CONNECTED)           │  │
│                             │                        │  │
│                transitions  │                        │  │
│                             ▼                        │  │
│                    DiscoveryTask ──scan──► probe_t   │  │
│                             │               │        │  │
│                    ACK recv ◄───────────────┘        │  │
│                             │                        │  │
│                    HeartbeatManager ──heartbeat_t ───┼──┼──► (to RX)
│                             │                        │  │
│                    TxSendGuard ──── esp_now_send()   │  │
│                    EspnowTxScheduler ─ 4 queues      │  │
└─────────────────────────────────────────────────────────┘

                     ESP-NOW (802.11)
                     ══════════════
┌─────────────────────────────────────────────────────────┐
│                    RECEIVER                             │
│                                                         │
│  main.cpp ──► ReceiverConnectionHandler::init()         │
│                   │                                     │
│          registers│state callbacks                      │
│                   ▼                                     │
│    EspNowConnectionManager ◄── events ──────────────┐  │
│    (IDLE/CONNECTING/CONNECTED)                       │  │
│                   │                                  │  │
│        CONNECTED  │                                  │  │
│               callback:                              │  │
│                 lock channel                         │  │
│                 suspend discovery                    │  │
│                 init_pending_ = true                 │  │
│                   │                                  │  │
│    tick() ──2s──► send_initialization_requests()     │  │
│                   │                                  │  │
│                   ├── EspnowTxScheduler (P2/DATA) ───┤  │
│                   │     REQUEST_DATA                  │  │
│                   │     version_announce              │  │
│                   │     config_section_requests       │  │
│                   │     catalog_requests              │  │
│                   │                                   │  │
│                   └── send_ack_response() ────────────┘  │
│                         DIRECT esp_now_send() (no queue)  │
│                                                           │
│    RxHeartbeatManager ─ heartbeat_ack ─► EspnowTxSched   │
│                                                           │
│    RxStateMachine (IDLE/CONNECTING/CONNECTED/ACTIVE)      │
│         │                                                 │
│         ├─── MQTT gate check ──► mqtt_task                │
│         │                          │                      │
│         │              EspNowConnMgr.ms_since_last_hb()  │
│         │                          │                      │
│         │                 gate closed: MqttClient::disconnect()
│         │                 gate open+4s: MqttClient::loop()
│         │                                                 │
│         └─── tick() heartbeat staleness check (12s)       │
│                   suppress all outbound ESP-NOW           │
└─────────────────────────────────────────────────────────────┘
```

---

## 15. Discovery Design — Receiver Probe Broadcasts: Confirmed Dead Code

### Current Behaviour (Confirmed by Code)

The receiver calls `EspnowDiscovery::instance().start()` during `ReceiverConnectionHandler::init()`. This starts a task that broadcasts `msg_probe` every `ANNOUNCEMENT_INTERVAL_MS (5 000 ms)` on the receiver's current WiFi channel while in CONNECTING state, and suspends on CONNECTED.

Simultaneously, the transmitter runs `active_channel_hop_scan()` which:
1. Sets WiFi channel
2. Sends its own `msg_probe` via `send_probe_on_channel()`
3. Reads from `EspnowQueueManager::discovery_queue` looking only for `msg_ack`

When the receiver's probe arrives at the transmitter:
- It enters the transmitter's **main message queue** (not the discovery queue)
- `EspnowMessageHandler::handle_probe()` processes it: adds RX as peer, sends ACK back to RX
- `probe_config_.connection_flag = nullptr` → the ACK **does not advance TX's state machine**
- TX remains in CONNECTING; only an ACK received on the discovery queue completes TX's connection

When TX's ACK (sent in response to RX's probe) arrives at the receiver:
- RX's `g_ack_config` has all fields `nullptr` (channel lock, ack_received_flag, etc.)
- The ACK is processed but nothing happens — **RX ignores the inbound ACK**

**Net effect of receiver probe broadcasts:**
- RX consumes `EspnowTxScheduler` P1/DISCOVERY slots (5s cadence, 3 retries each)
- TX receives probe, sends ACK — consuming TX's direct `esp_now_send()` path
- Neither side advances its state machine
- **Zero contribution to connection establishment**

### The Two Design Options

#### Option A — Remove Receiver Probe Broadcasts (Recommended)

The system is already TX-driven. The transmitter is the active channel scanner; the receiver's role is to respond to incoming TX probes with an ACK. The receiver's outbound probe broadcast is architecturally redundant.

**Implementation:** Do not call `EspnowDiscovery::instance().start()` on the receiver, or permanently suspend it immediately after start. The receiver stays passive: it simply waits for a TX probe on its current channel, responds with an ACK, and the connection completes.

**Pros:**
- Removes dead code path and false air time
- Eliminates mutual ACK traffic that consumes WiFi buffer slots
- Simplifies the system to a clean unidirectional model: TX scans → TX probes → RX ACKs → connected
- Reduces scheduler load on RX during the pre-connection phase when buffers are most constrained

**Cons:**
- If a second transmitter is added in future, the receiver cannot signal its presence proactively

#### Option B — Bidirectional Discovery (Future Enhancement)

The transmitter's `active_channel_hop_scan()` inner loop could be extended to also recognise incoming `msg_probe` frames from the receiver (in addition to `msg_ack`). When TX is on channel X and receives a probe from RX (which is also on channel X), TX could treat that as equivalent to receiving an ACK — i.e. "the receiver is on this channel, I found it".

This would make both devices' probes useful and could marginally speed up connection (if RX's 5s probe happens to arrive during TX's 1s dwell before TX's own probe elicits an ACK). The improvement is small (at most 200ms — the time between TX probes on the same channel) but would enable future multi-transmitter scenarios.

**Implementation note:** The transmitter's `discovery_queue` routing would need to include `msg_probe` frames (currently only `msg_ack` is routed to the discovery queue from the ISR). The inner scan loop would then check `a->type == msg_ack || a->type == msg_probe`.

### Confirmed Design Decision

**Option A is the confirmed design. The transmitter is the sole initiator of ESP-NOW communication.** The receiver is purely reactive and must not broadcast probes. All receiver-side discovery initiation code must be removed when this work is implemented. This is tracked as **ISSUE 10** below.

---

## 16. Issue Register Addendum (Issues 10–11)

### ISSUE 10: Remove Receiver Probe Broadcasts — ✅ IMPLEMENTED (2026-04-24)

**Design decision:** Transmitter is the sole initiator of ESP-NOW communication. The receiver is purely reactive.  
**Files modified:**
- `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp` — removed `EspnowDiscovery::instance().suspend/resume()` calls; removed `#include <espnow_discovery.h>`; updated log messages
- `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp` — removed fallback `EspnowDiscovery::instance().start()` block and `g_discovery_started` check; removed `#include <espnow_discovery.h>`
- `espnowreceiver_LCD/src/runtime/runtime_task_startup.cpp` — removed `EspnowDiscovery::instance().start()` call and all discovery check/log code; removed `#include <espnow_discovery.h>`
- `espnowreceiver_LCD/src/espnow/espnow_runtime_detail.h` — removed `extern std::atomic<bool> g_discovery_started`
- `espnowreceiver_LCD/src/espnow/espnow_runtime_messages.cpp` — removed `std::atomic<bool> g_discovery_started{false}`
- `espnowreceiver_LCD/src/main.cpp` — removed `discovery_state` variable and its logging; removed `#include <espnow_discovery.h>`
- `espnowreceiver_2/src/espnow/rx_connection_handler.cpp` — removed `EspnowDiscovery::instance().suspend/resume()` calls; removed `#include <espnow_discovery.h>`; updated log messages

**What was retained:** Both receivers' `handle_probe()` / `send_ack_response()` paths are untouched — the reactive model is intact. `EspnowDiscovery` class itself is untouched in `esp32common` (still used by transmitter).

**Post-implementation defect discovered and fixed (2026-04-24):** Removing `EspnowDiscovery::start()` also removed the only call to `EspnowPeerManager::add_broadcast_peer()` on the receiver. Without the broadcast peer (FF:FF:FF:FF:FF:FF) registered in the ESP-NOW peer list, the ESP-IDF WiFi driver silently discards incoming broadcast frames before calling the recv callback — RX cannot receive TX's broadcast PROBE frames during channel-hop scanning. Unicast frames (heartbeats, data) when connected were unaffected. Symptom: `rx_callback_count` froze at the moment TX transitioned from CONNECTED (unicast) to CONNECTING (broadcast probes). Fix: explicitly call `EspnowPeerManager::add_broadcast_peer()` after recv_cb registration in `espnow_runtime.cpp` (LCD) and `main.cpp` (_2). No probes are ever sent by the receiver — this is receive-side registration only.

### ISSUE 11: Receiver MQTT Config Comment is Misleading ℹ Low

**Location:** `mqtt_task.cpp` — file comment says "Uses receiver's own MQTT configuration from ReceiverNetworkConfig" which is correct, but the `rx_connection_handler` code that requests `config_section_mqtt` from TX could imply the receiver uses the transmitter's config.  
**Clarification:** `config_section_mqtt` requests push the *transmitter's* MQTT settings into `TransmitterMqttSpecs` — a read-only display cache for the receiver's web UI. `ReceiverNetworkConfig` (used by `mqtt_task.cpp`) is completely separate and holds the receiver's own MQTT config from NVS.  
**Fix:** Add a comment in `mqtt_task.cpp` and `rx_connection_handler.cpp` at the `config_section_mqtt` request site clarifying that this populates the TX-status display cache, not the receiver's own MQTT connection config.

---

*Document produced by architectural review of source code as-of 2026-04-24.*  
*Files reviewed: discovery_task.cpp, tx_connection_handler.cpp, heartbeat_manager.cpp,*  
*rx_connection_handler.cpp (LCD+_2), mqtt_task.cpp (LCD+_2), connection_manager.cpp,*  
*espnow_tx_scheduler.cpp, espnow_standard_handlers.cpp, timing_config.h*
