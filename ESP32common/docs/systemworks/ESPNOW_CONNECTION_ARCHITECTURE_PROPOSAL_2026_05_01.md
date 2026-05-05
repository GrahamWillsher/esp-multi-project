# ESP-NOW Connection Architecture Proposal
**Date:** 2026-05-01  
**Author:** Engineering Review  
**Status:** PROPOSAL — requires implementation approval  

---

## 1. Executive Summary

The system has been unable to reliably reconnect ESP-NOW after a link drop for several months of active development effort. The root cause is **architectural**: the current design runs two independent, uncoordinated state machines — one on the transmitter (TX) and one on the receiver (RX). Neither device has authoritative knowledge of the other's state. Both declare themselves "CONNECTED" based on different, asymmetric signals, and there is no handshake that confirms the full bidirectional path works before data traffic begins.

This document identifies all specific failure modes observed in the current code, explains why they occur structurally, reviews what the ESP-IDF documentation and community knowledge say about these patterns, and proposes a concrete replacement architecture that solves the problem definitively.

**The proposed fix is not more band-aids on the current structure. It is a small, focused protocol change at the wire level that makes the TX the single authority for the connection state, adds one new message round-trip to confirm the bidirectional path, and simplifies the RX into a passive responder.**

Additional clarifications in this revision:
- The transmitter stores and prioritises the **last known good channel** on reconnect attempts (retry bias before full-hop scanning).
- ESP-NOW timing has been reviewed with a practical latency budget so control-path communication remains deterministic.
- ESP-NOW operational traffic can be initiated by either side once connected (TX periodic/status pushes and RX MQTT-instigated request/response flows), with explicit behavior on communication loss.

Investigation basis for these points: direct code-path audit across `esp32common`, `ESPnowtransmitter2`, `espnowreceiver_2`, and `espnowreceiver_LCD` current implementations (state handlers, scheduler policies, timing constants, and runtime routes).

---

## 2. Current Architecture — What Actually Exists

### 2.1 The Three State Machines

The system currently has **three overlapping state machines** running simultaneously:

#### 2.1.1 `EspNowConnectionManager` (shared code, runs on both devices)
```
IDLE → CONNECTING → CONNECTED
```
- Uses a FreeRTOS event queue
- Driven by events: `CONNECTION_START`, `PEER_FOUND`, `PEER_REGISTERED`, `CONNECTION_LOST`, `DATA_RECEIVED`
- Has a `CONNECTING` timeout (120 s on TX, same on RX)
- Has heartbeat timeout detection (configurable, enabled on RX)
- Runs on **both** TX and RX as separate instances with no cross-device communication

#### 2.1.2 `TxReconnectManager` (TX only)
```
IDLE → SCANNING → BACKOFF → SCANNING → ...
```
- Queue-based, single task on Core 0
- Launches transient hop-worker tasks on Core 1 that channel-scan and return
- Backoff table: 0 ms → 3000 ms → 5000 ms
- Declares success when the hop-worker gets a PROBE ACK from the receiver
- Transitions TX's `EspNowConnectionManager` to CONNECTED via `PEER_REGISTERED`

#### 2.1.3 `RxRadioArbiterFsm` (RX only)
```
BOOTSTRAP → STEADY_CONNECTED
                ↓ (stale heartbeat or new probe)
         RECONNECT_DETECTED
                ↓ (ACK send OK)
         ACK_RECOVERY_WINDOW
                ↓ (first heartbeat after recovery)
         POST_RECONNECT_SETTLE
                ↓ (settle timer)
         STEADY_CONNECTED
         
RECONNECT_DETECTED → DEGRADED_FALLBACK (timeout)
DEGRADED_FALLBACK → ACK_RECOVERY_WINDOW (ACK send OK)
```
- Controls `mqtt_allowed`, `control_only_mode`, `noncritical_enqueue_allowed`
- This FSM being in any state other than `STEADY_CONNECTED` or `POST_RECONNECT_SETTLE` blocks MQTT

### 2.2 How a Connection Is Currently Established

**TX side:**
1. `TxReconnectManager` starts a hop-worker that scans all 13 channels
2. On each channel it sends broadcast PROBE frames every `PROBE_INTERVAL_MS`
3. If it receives an ACK (via the `espnow_discovery_queue`), it: sets channel, registers peer, calls `on_ack_received()` + `on_peer_registered()`, posts `PEER_REGISTERED` → `EspNowConnectionManager` enters CONNECTED

**RX side (parallel, independent):**
1. Receives broadcast PROBE on whatever channel its radio is on
2. `handle_probe()` removes+re-adds TX peer with `channel=0`, calls `send_ack_response()`
3. `on_probe_received()` posts `PEER_FOUND` → `EspNowConnectionManager` enters CONNECTING
4. `on_peer_registered()` posts `PEER_REGISTERED` → `EspNowConnectionManager` enters CONNECTED

The TX considers itself CONNECTED after: **getting one ACK**  
The RX considers itself CONNECTED after: **receiving one PROBE and sending an ACK**

**Neither device confirms the other is CONNECTED before starting data traffic.**

---

## 3. Failure Mode Analysis — Every Bug That Has Been Observed

### 3.1 Failure Mode A: Stale Peer Channel → Hardware TX Buffer Exhaustion

**Observed:** `ESP_ERR_ESPNOW_NO_MEM` every 0.5–1 s for 20+ minutes on the receiver

**Mechanism:**
1. On a previous session, TX was registered on RX with a non-zero channel (e.g., ch 6)
2. After reboot, WiFi AP assigns the RX to ch 11
3. `esp_now_send()` queues the ACK frame for the peer's stored channel 6, but the 802.11 radio is locked to ch 11
4. There is no valid air path — the frame sits in the hardware LMAC DMA TX ring
5. The send callback **never fires** because the frame is queued but cannot be transmitted
6. The TX descriptor slot is permanently consumed
7. After 4–8 probes: all 8 hardware TX descriptor slots exhausted
8. Every subsequent `esp_now_send()` returns `ESP_ERR_ESPNOW_NO_MEM` immediately
9. The recovery path (`esp_now_deinit()` + `esp_now_init()`) was added but only partially works: it clears the peer table and callbacks but does **not** reliably flush the hardware TX descriptor ring on all ESP-IDF versions

**Why `deinit/init` doesn't fully fix it:**  
The Espressif documentation notes that `esp_now_deinit()` deletes all paired device information and frees internal buffers managed by the ESP-NOW layer. However, the underlying LMAC (Lower MAC) DMA descriptor ring is managed by the WiFi driver layer, not the ESP-NOW layer. If frames are stuck in the LMAC ring awaiting transmission, `esp_now_deinit()` does not drain them. Only `esp_wifi_stop()` + `esp_wifi_start()` (or a full device restart) is guaranteed to flush the LMAC hardware state.

**The channel=0 fix that was applied** correctly prevents new peer registrations from using stale channels. Channel=0 means "use whatever channel the radio is currently on at transmit time," which eliminates the mismatch. This fix is correct and necessary.

**But it doesn't help once the buffer is already stuck** from a prior boot's state.

### 3.2 Failure Mode B: `RxRadioArbiterFsm` Trapped in `RECONNECT_DETECTED` or `DEGRADED_FALLBACK`

**Observed:** MQTT blocked with `state=0 hb_age=1288383 ms` for 21+ minutes

**Mechanism:**
1. When RX receives a PROBE from TX while `had_connected_session_=true` AND heartbeat is stale, it fires `EV_PROBE_WHILE_PREVIOUSLY_CONNECTED`
2. This transitions the arbiter to `RECONNECT_DETECTED`
3. In `RECONNECT_DETECTED`, policy: `mqtt_allowed=false`, `control_only_mode=true`
4. The **only** escape from `RECONNECT_DETECTED` is `EV_DISCOVERY_ACK_SENT_OK`
5. `EV_DISCOVERY_ACK_SENT_OK` fires only when an ACK send succeeds
6. ACK sends are permanently failing with `NO_MEM` (Failure Mode A)
7. Therefore: `EV_DISCOVERY_ACK_SENT_OK` never fires
8. `RECONNECT_DETECTED` has a timeout (`reconnect_timeout_ms` = `reconnect_quiet_window_ms` = 12 s)
9. After 12 s, it transitions to `DEGRADED_FALLBACK`
10. In `DEGRADED_FALLBACK`, the **only** escape is also `EV_DISCOVERY_ACK_SENT_OK`
11. The system is now permanently locked with MQTT disabled

This is a **design deadlock**: the event needed to escape the stuck state is produced by the very operation that is stuck.

### 3.3 Failure Mode C: TX/RX Split-Brain on Channel

**Observed:** TX log shows `Channel mismatch: WiFi=11 locked=7`

**Mechanism:**
1. TX found receiver on ch 7, locked channel to 7, declared CONNECTED
2. TX starts scanning again (120 s CONNECTING timeout expires and reconnect manager re-enters SCANNING)  
3. The hop-worker changes the WiFi channel as it scans (ch 8, 9, 10, 11...)
4. `validate_state()` runs in `loop()` and detects the mismatch
5. `ChannelManager::g_lock_channel` still says 7 but WiFi is on 11
6. This is logged but marked as "reconnect manager will recover" — but the reconnect manager is already running a scan, so nothing changes

**Root cause:** The TX's `ChannelManager::lock_channel()` is advisory, not enforced. The hop-worker calls `esp_wifi_set_channel()` directly during scans, bypassing the lock. The lock prevents `ChannelManager::set_channel()` from being called but not `esp_wifi_set_channel()`.

### 3.4 Failure Mode D: Two Connection Managers Cannot Agree on State

**The fundamental problem that causes all of the above:**

```
TX's EspNowConnectionManager state:  CONNECTING (scanning)
TX's TxReconnectManager state:       SCANNING (attempt #32)
 
RX's EspNowConnectionManager state:  CONNECTING (waiting for PEER_REGISTERED)
RX's RxRadioArbiterFsm state:        DEGRADED_FALLBACK (MQTT locked)
```

The TX's connection manager is in CONNECTING. The RX's connection manager is also in CONNECTING. Both are independently cycling and neither has visibility into the other's state. The TX doesn't know the RX's ACK sends are all failing. The RX doesn't know the TX is scanning.

Even if the TX gets an ACK, the following can happen:
1. TX posts PEER_REGISTERED → TX enters CONNECTED
2. TX starts sending heartbeats on ch N
3. RX is in DEGRADED_FALLBACK → control_only_mode → heartbeats may or may not be processed
4. RX doesn't receive heartbeat in time → RX heartbeat timeout → CONNECTION_LOST → back to IDLE
5. TX sends heartbeat → RX enters CONNECTING (again from PEER_FOUND)
6. TX never gets heartbeat ACK... waits... heartbeat timeout → TX goes back to IDLE too

The CONNECTING timeout (120 s) and heartbeat timeout (32 s on RX) are mismatched timers on two devices with no coordination. They will often timeout at different times and cycle independently.

### 3.5 Failure Mode E: MQTT Permanently Decoupled from Actual State

**Observed:** `FSM gate blocked MQTT (state=0 hb_age=1288383 ms)` — this is `EspNowDeviceState::DISCONNECTED`

The `RxStateMachine` tracks an `EspNowDeviceState` that is set to ACTIVE only when data is actually flowing. The MQTT task gates on this state. Since ESP-NOW data never starts flowing (TX never connects), the `RxStateMachine` never leaves DISCONNECTED, and MQTT is blocked for the entire session.

**This is wrong architectural thinking.** The RX's MQTT connection to the broker should be fully independent of whether the ESP-NOW transmitter is currently connected. The RX can serve a web dashboard, respond to MQTT queries, and report historical data even when the TX is offline. MQTT should only be gated on WiFi+broker availability, not on ESP-NOW connection state.

---

## 4. Industry Practice and Espressif Guidance

### 4.1 What the ESP-NOW Documentation Actually Says

From the ESP-IDF documentation (retrieved 2026-05-01):

> "It is not guaranteed that application layer can receive the data. **If necessary, send back ack data when receiving ESP-NOW data.** If receiving ack data timeouts, retransmit the ESP-NOW data. A sequence number can also be assigned to ESP-NOW data to drop the duplicate data."

This is explicit Espressif guidance: **the application must implement its own ACK/retry for reliable delivery**. The PROBE→ACK exchange is exactly this, but it only proves the RX→TX direction. A true bidirectional confirmation requires the TX to also confirm that its unicast path to the RX works.

> "too short interval between sending two ESP-NOW data may lead to disorder of sending callback function. So, it is recommended that sending the next ESP-NOW data after the sending callback function of the previous sending has returned."

This validates the `EspnowTxScheduler`'s token-based flow control approach, but highlights that the scheduler must not enqueue faster than the callback can drain — which is exactly what happens when callbacks stop firing.

> "If peer_addr is NULL, send data to all of the peers that are added to the peer list"

> "channel: Wi-Fi channel that peer uses to send/receive ESPNOW data. **If the value is 0, use the current channel** which station or softap is on."

This confirms channel=0 is the correct value and explains the stale-channel bug precisely.

### 4.2 The Core Pattern Used in Production ESP-NOW Systems

Reviewing publicly available ESP-NOW implementations and Espressif's own example code (`wifi/espnow` in esp-idf), the universal pattern for reliable point-to-point ESP-NOW is:

1. **One device is the initiator (master), one is the responder (slave)**
2. **The master drives ALL state transitions** — the slave only responds
3. **State transitions require explicit confirmation from the peer**
4. **There is no "assume connected" — connection is proven by round-trip**
5. **The master retries until it gets confirmation or escalates**

The current architecture violates rules 2, 3, and 5. Both devices drive state transitions independently based on local observations only.

### 4.3 Confirmed Community Problem: Stale Peer Channel

This exact `ESP_ERR_ESPNOW_NO_MEM` loop from stale peer channels is a documented community problem. The pattern is:
- System works fine until a reboot on one side
- After reboot, the STA channel changes (DHCP may have moved the AP channel)  
- Stale peer entry uses old channel
- Send callbacks never fire
- Hardware descriptor ring fills
- `NO_MEM` permanently

The **only** reliable fixes are: (a) always use `channel=0` in peer registration (applied), and (b) detect the stuck state and call `esp_wifi_stop()` + `esp_wifi_start()` rather than `esp_now_deinit()` + `esp_now_init()` for the recovery, because only a full WiFi restart flushes the hardware LMAC ring reliably. The alternative is a device restart (`esp_restart()`).

---

## 5. Proposed Architecture: TX-Authoritative with Bidirectional Handshake

### 5.1 Design Principles

1. **The TX is the single authority for the connection state machine.** The RX does not independently decide it is connected.
2. **Connection is confirmed by a full round-trip exchange, not just one-way reception.**
3. **The RX state machine becomes a lightweight responder FSM (3 states).** No `RxRadioArbiterFsm` complexity.
4. **MQTT on the RX runs independently of ESP-NOW state.** The MQTT gate is removed.
5. **Hardware buffer recovery escalates to a full WiFi restart**, not just ESP-NOW deinit.
6. **The RX's WiFi channel is fixed by its AP.** The TX discovers this channel by hopping. Once found, the TX locks its own radio to that channel. The RX registers the TX peer with `channel=0` (meaning: use whatever channel the radio is on at send time) — no channel value needs to be communicated from TX to RX.

### 5.1a How TX-Authority Achieves a Single Source of Truth

This is the core mechanism and it is worth stating precisely.

**In the current design**, both devices fire `PEER_REGISTERED` independently, based on their own local observations:
- TX fires it because `TxReconnectManager` got an ACK
- RX fires it because `ReceiverConnectionHandler` received a PROBE

Both devices decide they are CONNECTED based on different, one-directional signals. Neither waits for the other. This is the split-brain.

**In the proposed design**, the `PEER_REGISTERED` event on the RX is **never fired locally**. It can only be fired by receiving an explicit `connect_confirm` message sent by the TX. The sequence is strictly ordered:

```
1.  TX hop-worker finds RX (gets ACK on channel N)
2.  TX locks its own radio to channel N
3.  TX sends connect_confirm unicast → RX
4.  TX waits — does NOT yet declare itself CONNECTED
                     ↓
5.              RX receives connect_confirm
6.              RX fires PEER_REGISTERED → RX enters CONNECTED
7.              RX sends connect_confirm_ack → TX
                     ↓
8.  TX receives connect_confirm_ack
9.  TX fires PEER_REGISTERED → TX enters CONNECTED
```

The RX's CONNECTED state is a **direct causal consequence** of the TX deciding to issue a `connect_confirm`. If the TX never sends one — because the send callback failed, or it timed out, or it restarted — the RX stays in IDLE indefinitely. The RX cannot promote itself.

The TX is the single source of truth because **it is the only entity that can authorise a state transition on both devices**. The TX transitions first only after the full round-trip is complete (step 8), which also proves that the RX→TX unicast path is working, not just TX→RX.

### 5.2 New Wire-Level Protocol

This proposal uses an explicit confirmation pair:
```
msg_connect_confirm      (TX → RX, unicast, sent after TX receives discovery ACK)
msg_connect_confirm_ack  (RX → TX, unicast, sent after RX accepts connect_confirm)
```

Why explicit messages are selected as the primary path:
- Clear protocol semantics (discovery vs confirmation vs keepalive remain distinct)
- Easier troubleshooting and metrics attribution
- No coupling between periodic heartbeat cadence and connection-establishment logic

Optional compatibility enhancement:
- Add `session_id` to `heartbeat_t` / `heartbeat_ack_t` as additional stale-session protection,
  but **not** as the primary connect handshake mechanism.

### 5.3 New TX Connection Flow

```
State: IDLE
  │
  ▼ CONNECTION_START
State: SCANNING (TxReconnectManager running hop-worker)
  │
  │ [hop-worker receives ACK on channel N]
  ▼ WORKER_FOUND(channel=N, mac=RX_MAC)
State: CONFIRMING  ← NEW STATE
  │
  │ TX sends connect_confirm (unicast to RX_MAC, channel=N)
  │ TX waits up to 2000 ms for connect_confirm_ack
  │
  ├── [connect_confirm_ack received] ──────────►
  │                                             │
  │                                   State: CONNECTED
  │                                   (both sides now confirmed)
  │
  └── [timeout or send fail] → re-enter SCANNING
       (hop-worker tries again, no backoff increment
        unless 3+ consecutive CONFIRMING timeouts)
```

**Why this matters:**
- When TX sends `connect_confirm` via unicast, this proves that TX→RX unicast works at the ESP-NOW hardware layer (the send callback fires with SUCCESS)
- When RX sends `connect_confirm_ack`, this proves RX→TX unicast works
- If either direction fails, the TX has hard evidence and retries

### 5.4 New RX State Machine (Simplified)

The `RxRadioArbiterFsm` is **removed from the connection path**. The RX responder flow becomes:

```
IDLE
  │ [connect_confirm received from TX]
  ▼
CONFIRMING
  │ [send connect_confirm_ack, send callback fires SUCCESS]
  ▼
CONNECTED
  │ [heartbeat timeout / no data / explicit disconnect]
  ▼
IDLE
```

**The RX does NOT do scanning/channel-hopping reconnection logic.** It waits for TX authority confirmation. The TX finds the RX channel via hop scan.

**New RX `on_probe_received()` logic:**
1. Register/refresh TX peer with channel=0 (as currently done)
2. Send ACK (as currently done)
3. **Do NOT change connection state — stay in IDLE/CONNECTING until `connect_confirm` arrives**
4. Log that a probe was received

**New RX `on_connect_confirm_received()` logic:**
1. The RX's WiFi channel is AP-controlled — it must not be forcibly changed by connect-confirm handling
2. Register/refresh the TX peer with `channel=0` (use current radio channel at send time)
3. Clear any stuck send buffer state (clear ACK tokens)
4. Send `connect_confirm_ack`
5. Wait for send callback to confirm the ack was transmitted
6. On callback SUCCESS: post `PEER_REGISTERED` → `EspNowConnectionManager` enters CONNECTED
7. On callback FAIL: log and wait for next `connect_confirm` (do not enter CONNECTED)

### 5.5 Simplified RX MQTT Gate

**Current (broken):** MQTT gated on `EspNowDeviceState` from `RxStateMachine`, which requires active ESP-NOW data flow to leave `DISCONNECTED`.

**Proposed:** MQTT gated only on WiFi association + broker reachability. The RX publishes whatever data it has (cached or stale with staleness annotation). MQTT is never blocked because the transmitter is offline.

```cpp
// Old
if (rx_state_machine.state() == EspNowDeviceState::DISCONNECTED) {
    // block MQTT
}

// New
// MQTT runs whenever WiFi+broker are available.
// Stale/missing data is annotated in the MQTT payload.
// ESP-NOW connection state is reported as a separate MQTT topic.
```

This fixes the 21-minute MQTT blackout entirely. The RX can always report its own status via MQTT even with no transmitter.

### 5.6 Hardware TX Buffer Recovery (Fixed)

The current `esp_now_deinit()` + `esp_now_init()` recovery does not reliably flush the LMAC hardware TX descriptor ring. The correct recovery escalation is:

```
Level 1 (fast, ~100ms): esp_now_deinit() + esp_now_init() + re-register callbacks + re-add peers
         → try 3 times max before escalating

Level 2 (moderate, ~500ms): esp_wifi_stop() + esp_wifi_start() + esp_now_init() + re-register callbacks
         → guaranteed to flush LMAC hardware buffers
         → reconnects to AP (DHCP re-runs, takes ~1-2s)
         → try 2 times max before escalating

Level 3 (final): esp_restart()
         → guaranteed to reset everything
         → full boot takes ~3s
```

The Level 2 recovery (`esp_wifi_stop()` + `esp_wifi_start()`) is what the community has confirmed actually works. The cost is the AP reconnect delay. Given the system has already been stuck for 25+ minutes in the current failure mode, a 2-second reconnect delay is trivially acceptable.

**Implementation for the receiver:**
```cpp
// In rx_connection_handler.cpp tick()
if (consecutive_no_mem >= kNoMemRecoveryThreshold) {
    ++s_reinit_count;
    
    if (s_reinit_count <= 3) {
        // Level 1: ESP-NOW only
        esp_now_deinit();
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_now_init();
        esp_wifi_set_ps(WIFI_PS_NONE);
        // re-register callbacks, re-add broadcast peer
    } else if (s_reinit_count <= 5) {
        // Level 2: Full WiFi restart
        LOG_WARN("RX_CONN", "Escalating to WiFi stack restart (reinit #%lu)", s_reinit_count);
        esp_now_deinit();
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_wifi_start();
        // Wait for AP reconnect (event-driven or poll)
        esp_now_init();
        esp_wifi_set_ps(WIFI_PS_NONE);
        // re-register callbacks, re-add broadcast peer
    } else {
        // Level 3: Device restart
        LOG_ERROR("RX_CONN", "All recovery levels exhausted — restarting");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
}
```

### 5.7 TX Channel Affinity Persistence (Minor but Important)

**Investigation result (current code): partially implemented already.**

Verified behavior in current TX stack:
1. `ChannelManager::lock_channel()` persists the connected channel to NVS (`saved_channel_`) on each successful lock.
2. `ChannelManager::init()` reloads the saved channel at boot (unless STA/AP channel is already authoritative).
3. `TxReconnectManager::start_worker_scan()` passes `ChannelManager::get_channel()` as the scan hint.
4. `DiscoveryTask::active_channel_hop_scan_impl()` starts phase-1 scan from that hint channel.

So the transmitter **does** remember and retry the current/last channel first.

**Gap identified:** the bias is only a single first dwell before continuing full circular scan. There is no dedicated multi-retry "pre-pass" on the preferred channel before expanding to all channels.

**Recommendation update:**
- Keep current persistence path (already correct).
- Add an optional stronger channel-affinity pre-pass: `N` quick retries on preferred channel before full sweep.
- Keep this behind config so existing behavior is preserved by default.

### 5.8 ESP-NOW Timing Investigation (Report Back)

Concern raised: ESP-NOW communication can feel slow in recovery scenarios. Code-path timing review outcome:

**What dominates reconnect time today:**
1. `TRANSMIT_DURATION_PER_CHANNEL_MS = 2000 ms` dwell.
2. `CHANNEL_STABILIZATION_MS = 150 ms` before channel verification.
3. Post-found settle delay in discovery worker (`kPostChannelSettleDelayMs = 50 ms`).

This yields an effective worst-case per-channel budget of roughly $2000 + 150 + 50 = 2200$ ms before loop overhead. A full 13-channel miss sweep is therefore about $13 \times 2.2 \approx 28.6$ s, which is consistent with observed reconnect latency during difficult conditions.

**Control-plane timing is not the main bottleneck:**
- `PROBE_INTERVAL_MS = 200 ms` already allows multiple probe opportunities per dwell.
- Discovery ACK scheduler policy is retry-capable (`msg_ack` retries up to 12 with bounded NO_MEM backoff).
- Heartbeat cadence is 10 s and TX heartbeat-loss timeout is 35 s; this affects loss detection, not scan speed.

**Conclusion:**
- We should **not** slow control messaging globally.
- If faster reconnect is desired, tune scan strategy first (channel-affinity pre-pass and/or lower dwell under clean RF).
- Add explicit reconnect timing metrics before changing global timing constants.

**Telemetry to add (TX + RX):**
- `scan_duration_ms` (already logged per TX scan result; should be aggregated)
- `scan_channels_tried`
- `time_to_first_ack_ms`
- `reconnect_attempts_until_connected`

### 5.9 Bidirectional Initiation and Communication-Loss Handling

Connection authority remains TX-driven, but **application traffic initiation is already bidirectional in current code**.

Verified initiators:
1. **TX-initiated**
   - periodic `msg_heartbeat` and status frames while connected,
   - data-stream push after RX `msg_request_data` activates transmission.
2. **RX-initiated**
   - init burst sends `msg_request_data` + `msg_version_announce` after connect,
   - runtime sends requests/control (catalog requests, LED state request, event-log control, config section requests), including MQTT-instigated event-log subscribe/unsubscribe to TX.

**Current communication-loss behavior (important):**
- TX side: connection manager uses heartbeat-ACK timeout to drive `CONNECTION_LOST` and reconnect.
- RX side: `on_probe_received()` can trigger `PEER_FOUND`/CONNECTING and quiet-mode FSM transitions before full data-path confirmation.
- RX outbound request path currently uses immediate eligibility checks (`RxStateMachine`/connection checks). If not eligible, sends are rejected and logged; they are **not** queued for later replay.

**Implication:** bidirectional initiation is supported when link is up, but outage-time RX request intent is currently best-effort (drop/retry by caller) rather than queue+TTL managed.

**Recommendation update:** if guaranteed RX-initiated request delivery across outages is required, add an explicit bounded pending-request queue in receiver runtime (separate from the control queue) with expiry accounting.

### 5.10 Additional Request-Path Findings: MQTT/Event Logs and Cell Data

Follow-up audit specifically for receiver-originated MQTT/UI request traffic:

1. **Event-log start/stop control is implemented and active over ESP-NOW.**
  - Receiver MQTT subscriber lifecycle sends `msg_event_logs_control` (subscribe/unsubscribe).
  - Event-log clear sends `msg_event_logs_control` with `EVENT_LOGS_ACTION_CLEAR`.
  - TX receives and handles these actions in `message_routes`.

2. **Cell monitor is not currently started via a dedicated `subtype_cell_info` ESP-NOW stream-control path.**
  - Runtime SSE monitor request path sends `msg_request_data` for `/transmitter/monitor2`, which maps to `subtype_power_profile`.
  - TX-side `REQUEST_DATA` handling still marks `subtype_cell_info` and `subtype_events` as not implemented in the request-data handler.
  - Practical result: cell visibility today is driven primarily by MQTT subscription flow, not a dedicated RX→TX ESP-NOW cell-stream start command.

3. **Conclusion for architecture planning:**
  - Event-log request/control path is real and must be modelled in the protocol state machine.
  - Cell-info control path needs explicit implementation if it is intended to be first-class ESPNOW-managed traffic.

### 5.11 Proposed Unified Control Mechanism (State-Machine Based Single Source of Truth)

To make ESP-NOW communication more efficient and resilient, use **three coordinated state machines with one authority**:

#### A) TX Link Authority FSM (Single Source of Truth)

Authoritative owner: **TX only**.

States:
- `BOOTSTRAP`
- `SCANNING`
- `CONFIRMING`
- `CONNECTED_STEADY`
- `DEGRADED`
- `RECOVERY`

Rules:
- Only TX can enter/exit `CONNECTED_STEADY`.
- RX cannot self-promote to CONNECTED from probe/ACK traffic alone.
- All control and telemetry packets carry `session_id` + `epoch`; stale session packets are dropped.
- `CONFIRMING` uses explicit handshake completion (`connect_confirm` / `connect_confirm_ack`) before authority transition.

#### B) RX Intent FSM (Request/Replay FSM)

Owner: RX runtime, but **non-authoritative for link state**.

Purpose: preserve operator/system intent while link is down.

States:
- `IDLE`
- `INTENT_QUEUED`
- `WAIT_LINK`
- `FLUSHING`
- `DONE` / `EXPIRED`

Rules:
- Requests (`request_data`, `abort_data`, `event_logs_control`, config/catalog requests) are queued with TTL and dedupe key.
- Flush only when TX authority state indicates `CONNECTED_STEADY` and session matches.
- Drop expired intents with explicit metrics.

#### C) ESP-NOW Transport Pressure FSM (Anti-loss governor)

Owner: shared policy module (scheduler + guard signals).

States:
- `NORMAL`
- `CONGESTED`
- `PROTECT_CONTROL`
- `RECOVERING`

Triggers:
- queue-depth thresholds,
- `send_fail_no_mem` rate,
- consecutive NO_MEM,
- callback lag.

Actions:
- `NORMAL`: full priority operation.
- `CONGESTED`: reduce low-priority telemetry emission.
- `PROTECT_CONTROL`: control-only mode + purge non-control queues.
- `RECOVERING`: staged recovery (`esp_now_deinit/init` → WiFi restart → reboot guardrail).

This gives one clear truth for connection state (TX FSM), one bounded replay mechanism for missed RX intent (Intent FSM), and one explicit anti-loss policy under pressure (Transport FSM).

### 5.12 Efficiency and Reliability Improvements (Concrete)

1. **Finish control-plane unification**
  - Move all non-discovery TX sends through one scheduler owner where practical.
  - Keep direct-send paths only for strictly bounded cases and wire them into pressure-state signals.

2. **Implement missing stream-intent semantics**
  - Add explicit TX behavior for `subtype_cell_info` and `subtype_events` request/abort handling.
  - Treat them as managed stream contracts (start, keepalive, stop) instead of ad-hoc best-effort.

3. **Bound and prioritise retry traffic**
  - Keep control-plane retries aggressive but bounded.
  - Apply jittered retries to avoid synchronized re-collision during reconnect storms.

4. **Protect control plane under load**
  - On pressure-FSM transition to `PROTECT_CONTROL`, gate telemetry producers and preserve ACK/heartbeat/control exclusively.
  - Re-enable data progressively after recovery dwell.

5. **Session-aware replay safety**
  - Replay queued RX intents only if `session_id` still valid.
  - Prevent stale post-reconnect requests from previous sessions.

6. **Adaptive scan strategy**
  - Keep existing hint-first behavior.
  - Add configurable preferred-channel pre-pass retries and dynamic dwell reduction after recent successful reconnect history.

7. **Operational observability**
  - Add counters and histograms for: request queue TTL expiry, replay success, pressure-state residency, time in `CONFIRMING`, and reconnect cause taxonomy.

---

## 6. Component-by-Component Changes Required

### 6.1 `common.h` (esp32common)
- Add `msg_connect_confirm` and `msg_connect_confirm_ack` to the message type enum
- Add `connect_confirm_t` and `connect_confirm_ack_t` struct definitions

### 6.2 `TxReconnectManager` (TX)
- Add `CONFIRMING` state between `SCANNING` and `IDLE(connected)`
- After `WORKER_FOUND`: send `connect_confirm` unicast instead of immediately declaring connected
- Add `confirm_timeout_ms` (e.g. 2000 ms) and retry logic
- Add send callback confirmation: only enter CONNECTED after both the send callback fires SUCCESS AND `connect_confirm_ack` is received
- `CONFIRMING` timeout with no response → back to SCANNING (do not increment backoff unless 3+ consecutive confirmation failures)
- Keep existing persisted-channel behavior (`ChannelManager` NVS + hint-based scan start)
- Optional enhancement: add explicit preferred-channel pre-pass retries before full-hop scan

### 6.3 `TransmitterConnectionHandler` (TX)
- Register route for `msg_connect_confirm_ack`
- On receipt: verify `session_id` matches current session, call `TxReconnectManager::notify(WORKER_CONFIRMED)`

### 6.4 `espnow_standard_handlers.cpp` (common)
- Add `handle_connect_confirm()` function
- This is called on the RX when it receives `connect_confirm` from TX
- Logic: re-register peer with `channel=0`, clear token state, enqueue `connect_confirm_ack` (no RX channel lock/set)

### 6.5 `ReceiverConnectionHandler` (common)
- Remove `RxRadioArbiterFsm` dependency from the connection path
- `on_probe_received()`: only update last_rx_time and register peer — do NOT change connection state
- Add `on_connect_confirm_received()`: this is the new trigger for the RX connection
- In `on_connect_confirm_received()`: peer re-registration (`channel=0`), send `connect_confirm_ack` with callback, then (on callback success) post `PEER_REGISTERED`
- Remove the MQTT gate from this class entirely
- Add bounded pending queue handling for RX-initiated ESPNOW requests generated by MQTT while disconnected (currently absent)

### 6.6 `RxRadioArbiterFsm` (common) — DEPRECATION
- The arbiter's role (controlling `mqtt_allowed` and `control_only_mode`) becomes unnecessary if:
  - MQTT is not gated on ESP-NOW state
  - transport-pressure policy handles control prioritisation explicitly
- Replace with a small transport-pressure policy module/FSM (`NORMAL`, `CONGESTED`, `PROTECT_CONTROL`, `RECOVERING`), shared with scheduler/guard telemetry.

### 6.7 `espnow_runtime_routes.cpp` (LCD RX)
- Register route for `msg_connect_confirm` → `ReceiverConnectionHandler::on_connect_confirm_received()`

### 6.8 MQTT task (LCD RX)
- Remove the `EspNowDeviceState` gate
- Add ESP-NOW connection state as an MQTT status field in the periodic publish
- Publish even when ESP-NOW is disconnected (use cached/stale values with staleness flag)
- For MQTT-triggered RX→TX requests during disconnect: add explicit queue+TTL path (current behavior is immediate send-or-fail)

### 6.9 Telemetry/Diagnostics (TX + RX)
- Aggregate existing per-scan timing logs into reconnect metrics (`scan_duration_ms`, `time_to_first_ack_ms`)
- Add channel-affinity counters (`hint_first_channel_hit`, `full_scan_fallback_count`)
- Add RX pending queue counters if queue+TTL feature is implemented (`queued`, `flushed`, `expired`)

### 6.10 RX Request/Intent Queue (new)
- Add a bounded intent queue module (ring-buffer + TTL + dedupe key)
- Route MQTT/UI-generated RX->TX intents through this module instead of immediate send-or-fail
- Flush intents only when TX authority state is `CONNECTED_STEADY`
- Add per-intent-type TTL defaults (`event_logs_control` short TTL, config/cell/info requests medium TTL)

### 6.11 Stream-Control Completion for Cell/Event Data
- Implement TX request-data handler support for `subtype_cell_info` and `subtype_events` with explicit start/stop semantics
- Keep `msg_event_logs_control` as canonical event-log lifecycle control (already active)
- Ensure abort paths (`msg_abort_data`) map cleanly to the same stream IDs used by start requests

---

## 7. State Machine Diagrams

### 7.1 TX — New Full State Machine

```
                     ┌─────────────────────────────────────────┐
                     │           TxReconnectManager             │
                     │                                           │
    IDLE ──CONNECT──► SCANNING ──WORKER_FOUND──► CONFIRMING     │
      ▲                 │                            │           │
      │                 │ WORKER_MISS                │ CONFIRM_OK│
      │                 ▼                            ▼           │
      │              BACKOFF ◄──────── SCANNING ◄───────────────┘
      │                 │              (retry if CONFIRM timeout)
      └─────────────────┘
           STOP (on connection established or explicit shutdown)
```

### 7.2 RX — New Simplified State Machine

```
         IDLE
          │
          │ [connect_confirm received]
          │ [peer registered, ack enqueued]
          ▼
       CONFIRMING (wait for send callback SUCCESS for confirm_ack)
          │
          │ [send_cb fires SUCCESS]
          ▼
       CONNECTED
          │
          │ [heartbeat timeout 32s / explicit disconnect / TX reboot detected]
          ▼
         IDLE
```

Note: PROBE receipts do NOT change state. The RX accepts probes and sends ACKs in any state. Probes are how the TX finds the channel; `connect_confirm` is how the link is established.

### 7.3 Message Sequence — Successful Reconnect

```
TX (channel hop)                    RX (fixed channel, AP connected)
    │                                     │
    │ ──── PROBE (broadcast, ch N) ──────►│
    │                                     │ register TX peer (ch=0)
    │                                     │ enqueue ACK
    │                                     │ [send cb fires SUCCESS]
    │◄─── ACK (unicast, ch N) ────────────│
    │                                     │
    │ WORKER_FOUND(ch=N, mac=RX)          │
    │ ──── connect_confirm (unicast) ────►│
    │      [TX: wait for send_cb]         │ lock channel to N
    │      [TX: send_cb SUCCESS]          │ re-register TX peer (ch=0)
    │      [TX: wait for confirm_ack,     │ enqueue connect_confirm_ack
    │           timeout 2000 ms]          │ [send_cb fires SUCCESS]
    │◄─── connect_confirm_ack (unicast) ──│
    │                                     │ post PEER_REGISTERED
    │ WORKER_CONFIRMED                    │ EspNowConnectionManager: CONNECTED
    │ post PEER_REGISTERED                │
    │ EspNowConnectionManager: CONNECTED  │
    │                                     │
    │ ──── HEARTBEAT ─────────────────────►│
    │◄─── HEARTBEAT_ACK ──────────────────│
    │                                     │
    │  <<< BOTH DEVICES NOW CONNECTED >>> │
```

### 7.4 Message Sequence — Failed Confirmation (Channel Noise / Buffer Stuck)

```
TX                                  RX
    │                                     │
    │◄─── ACK ──────────────────── ───────│
    │ WORKER_FOUND(ch=N)                  │
    │ ──── connect_confirm ──────────────►│
    │      [TX: send_cb: FAIL]            │ (ACK send still failing from stuck buffer)
    │                                     │
    │ CONFIRMING timeout (2000 ms)        │
    │ → back to SCANNING                  │
    │   (no backoff increment)            │
    │                                     │
    │ [RX: 5th consecutive NO_MEM]        │
    │                                     │ recovery: esp_wifi_stop/start
    │                                     │ (clears LMAC hardware buffers)
    │                                     │ reconnects to AP (~1-2s)
    │                                     │
    │ ──── PROBE (next scan cycle) ──────►│ (RX now healthy)
    │◄─── ACK ────────────────────────────│ [send_cb SUCCESS]
    │ WORKER_FOUND                        │
    │ ──── connect_confirm ──────────────►│
    │      [TX: send_cb SUCCESS]          │
    │◄─── connect_confirm_ack ────────────│
    │ CONNECTED ──────────────────────────► CONNECTED
```

---

## 8. Why This Resolves Each Failure Mode

| Failure Mode | Current Cause | How Proposal Fixes It |
|---|---|---|
| A: NO_MEM loop | Stale peer channel; deinit/init doesn't flush LMAC | channel=0 prevents new stale entries; Level 2 recovery (WiFi stop/start) forces LMAC flush |
| B: Arbiter deadlock | `EV_DISCOVERY_ACK_SENT_OK` never fires when buffer stuck | Arbiter removed; CONNECTED requires `connect_confirm_ack` send callback SUCCESS, not just "an ACK was queued" |
| C: Channel mismatch | TX hop-worker bypasses channel lock | TX enters a non-hop `CONFIRMING` phase immediately after discovery ACK and does not permit scan retunes until confirm success/fail is resolved; channel commitment happens atomically with confirm completion |
| D: Split-brain state | Two independent FSMs with no shared state | TX-authoritative: RX only enters CONNECTED on receipt of `connect_confirm` from TX; TX only enters CONNECTED on receipt of `connect_confirm_ack` |
| E: MQTT permanent block | MQTT gated on ESP-NOW data reception | MQTT gate removed; RX publishes independently of ESP-NOW state |

---

## 9. Implementation Phasing

### Phase 1 — Immediate (Fix the Stuck Hardware Buffer)
**Without the new handshake, just fix the recovery:**
- Change recovery from `esp_now_deinit/init` to `esp_wifi_stop/start` (Level 2) after 3 Level 1 failures
- This alone will unblock the current stuck-in-NO_MEM scenario

**Files:** `rx_connection_handler.cpp`  
**Risk:** Low (adds a WiFi restart, but the system is already non-functional in this state)  
**Expected result:** Stuck buffer cleared in ~2–5 seconds instead of never  

### Phase 2 — Short Term (Add Connection Confirmation Handshake)
**Add `connect_confirm` / `connect_confirm_ack` wire messages:**
- New message types in `common.h`
- New `CONFIRMING` state in `TxReconnectManager`
- New `on_connect_confirm_received()` on RX
- Route registration in `espnow_runtime_routes.cpp`
- TX waits for full round-trip before declaring CONNECTED

**Files:** `common.h`, `tx_reconnect_manager.h/.cpp`, `tx_connection_handler.cpp`, `espnow_standard_handlers.h/.cpp`, `rx_connection_handler.h/.cpp`, `espnow_runtime_routes.cpp`, relevant message structs  
**Risk:** Medium — this changes the connection protocol  
**Expected result:** No more split-brain. No more TX saying CONNECTED while RX is stuck  

### Phase 3 — Medium Term (Simplify RX State Machine)
**Remove `RxRadioArbiterFsm` dependency from the connection path:**
- Simplify RX to pure 3-state machine driven by TX's `connect_confirm`
- Decouple MQTT from ESP-NOW state
- Remove the `RxRadioArbiterFsm`'s mqtt_allowed gate

**Files:** `rx_connection_handler.h/.cpp`, `rx_radio_arbiter_fsm.h/.cpp` (may be deprecated), LCD MQTT task  
**Risk:** Medium — changes the MQTT publish gating (verify MQTT doesn't publish invalid data on first boot)  
**Expected result:** MQTT always available when WiFi is up; clean separation of concerns  

### Phase 4 — Medium Term (Intent Replay + Stream-Control Completion)
- Implement RX bounded intent queue (TTL + dedupe + flush-on-connected)
- Integrate transport-pressure FSM with scheduler and direct-send guard
- Complete TX request-data semantics for `subtype_cell_info` and `subtype_events`

**Files:** receiver runtime intent queue module, `espnow_send` call sites, tx request-data handlers, scheduler/guard policy integration  
**Risk:** Medium — behavior change for deferred operator actions and stream start/stop semantics  
**Expected result:** fewer dropped user/system intents during outages, cleaner reconnect recovery, reduced NO_MEM recurrence pressure

---

## 10. Configuration Constants That Need to Change

| Constant | Current | Proposed | Reason |
|---|---|---|---|
| `ESPNOW_CONNECTING_TIMEOUT_MS` | 120,000 ms | 45,000 ms | The confirmation round-trip proves connection faster; long timeouts waste time |
| `TRANSMIT_DURATION_PER_CHANNEL_MS` (dwell) | 2,000 ms | 2,000 ms | Keep — allows for probe→ACK→confirm_ack sequence within one dwell |
| `confirm_timeout_ms` (new) | N/A | 2,000 ms | Time to wait for `connect_confirm_ack` before retrying |
| `channel_affinity_probe_retries` (optional new) | N/A | 2–3 | Strengthens existing hint-first behavior with explicit pre-pass retries |
| `channel_affinity_dwell_ms` (optional new) | N/A | 300–500 ms | Short preferred-channel retry dwell before full scan |
| `rx_pending_request_ttl_ms` (new, if queue implemented) | N/A | 10,000 ms | Bounds queued RX-initiated requests during outage |
| `reconnect_quiet_window_ms` (RX) | 12,000 ms | Remove | No longer needed in simplified RX FSM |
| `post_reconnect_settle_ms` (RX) | 8,000 ms | Remove | No longer needed |
| RX heartbeat timeout | 32,000 ms | 32,000 ms | Keep — connection drop detection remains unchanged |

---

## 11. Risks and Mitigations

| Risk | Mitigation |
|---|---|
| `connect_confirm` is dropped by noise | TX retries SCANNING if confirmation times out in 2 s (same cost as current false-start scenario) |
| RX WiFi restart (Level 2 recovery) disconnects MQTT | MQTT reconnects automatically (~2 s); this is acceptable and far better than the current 25+ minute blackout |
| Adding new message types breaks protocol with `espnowreceiver_2` | The old receiver ignores unknown message types (checked in router — unknown types log a warning and are discarded). Phase 2 can be deployed to LCD RX first. |
| RX MQTT publishes stale data when TX offline | Add `data_age_s` field to MQTT payload so the consumer knows data is stale |

---

## 12. Summary

The reconnect failure is the direct result of running two independent, uncoordinated state machines. The receiver cannot reliably send ACKs (stuck hardware buffer), which means the `RxRadioArbiterFsm` never escapes its stuck state, which means MQTT is permanently blocked, and the user sees no activity.

Every reactive fix that has been applied — channel=0 in peer registration, NO_MEM counter, deinit/init recovery, purge queues, WiFi power save — addresses symptoms. The root cause is that the connection protocol has no mechanism to confirm that both sides have a working bidirectional link before declaring CONNECTED.

The proposed architecture adds exactly that: a 2-message round-trip (`connect_confirm` / `connect_confirm_ack`) that proves the full TX→RX→TX path before either device declares CONNECTED. Combined with the Level 2 recovery (WiFi stop/start when deinit/init repeatedly fails), this eliminates all five identified failure modes.

This revision also includes implementation-level findings and corrections: (1) TX channel persistence and hint-first scan are already present, with a clearly identified enhancement path for stronger preferred-channel retry bias; (2) reconnect slowness is primarily scan-dwell bounded, not control-packet bounded; (3) event-log lifecycle control from RX is already active via `msg_event_logs_control`; and (4) cell-info request semantics are not yet fully implemented on the TX request-data handler path.

The recommended long-term shape is a unified control mechanism: TX-authoritative link FSM (single source of truth), RX intent/replay FSM (bounded queue+TTL), and a transport pressure FSM (explicit anti-loss policy). Together these provide deterministic reconnect, lower packet-loss risk under congestion, and clearer operator-visible behavior during outages.

The Phase 1 change (Level 2 recovery escalation) can be implemented and deployed today and should unblock the current hardware. Phase 2 (the handshake) is the correct permanent fix.
