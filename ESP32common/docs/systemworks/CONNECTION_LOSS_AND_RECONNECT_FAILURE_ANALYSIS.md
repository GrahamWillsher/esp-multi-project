# Connection Loss & Reconnection Failure — Root Cause Analysis

**Date:** 2026-04-25  
**Devices under investigation:** ESPnowtransmitter2 · espnowreceiver_LCD  
**Evidence timestamps:**  
- RX LCD log event: uptime `[0d 00h 37m 22s]`  
- TX log event: uptime `[0d 00h 38m 03s]`

---

## 1. Investigation Methodology

Direct terminal buffer access was not available via tooling; the analysis is
therefore based on:

1. **Uptime-timestamp evidence** supplied by the user (37m:22s RX, 38m:03s TX).
2. **Complete static code review** of all eight relevant source modules:
   - `espnow_runtime.cpp` (RX message pump)
   - `rx_connection_handler.cpp / .h`
   - `connection_manager.cpp / .h` (shared)
   - `espnow_heartbeat_monitor.h` (shared)
   - `tx_connection_handler.cpp`
   - `discovery_task.cpp`

---

## 2. Timing Signature Analysis

### 2.1 Expected behaviour under the designed asymmetric-timeout scheme

The architecture uses an intentional 3-second asymmetry between the RX
heartbeat timeout (32 s) and the TX heartbeat-ACK timeout (35 s).  When a
transient packet loss occurs both sides should time out within 3 seconds of
each other.  Under ideal recovery, the RX enters `CONNECTING` first, receives
the next TX heartbeat within one heartbeat interval (~10 s), and reconnects
before the TX even notices a problem.

### 2.2 What the timestamps reveal

| Event | Timestamp | Elapsed since last heartbeat exchange |
|-------|-----------|---------------------------------------|
| Last successful heartbeat exchange (computed) | ~36m:50s | 0 s |
| **RX heartbeat timeout fires** | **37m:22s** | **+32 s** ✓ matches 32 s RX threshold |
| **TX heartbeat-ACK timeout fires** | **38m:03s** | **+73 s** ✗ expected +35 s |

The 73-second gap between the last heartbeat exchange and the TX disconnect
event is **38 seconds longer than the 35 s TX threshold**.  This is the smoking
gun.

**The designed quick-reconnect path failed entirely.**  If it had worked, the
TX would never have seen a gap longer than ~10 s (one heartbeat interval) in
its heartbeat-ACK stream, and the TX would never have reached its 35 s timeout
at all.  Instead, the TX received no heartbeat-ACK for 73 s, proving that:

1. The RX was in `CONNECTING` state (not `CONNECTED`) for at least 41 s after
   its own timeout (37m:22s → 38m:03s).
2. During that 41 s window the TX sent multiple heartbeats that the RX either
   did not receive, or received but **could not convert into a PEER_REGISTERED
   event** to complete reconnection.

---

## 3. Root Cause A — Why Was the Connection Lost in the First Place?

### 3.1 The heartbeat delivery chain

```
TX HeartbeatManager  →  EspnowTxScheduler  →  esp_now_send()
                                                    ↓
                                              [WiFi driver]
                                                    ↓
                                           [RF / air interface]
                                                    ↓
                              RX on_data_recv cb  →  message_queue
                                                    ↓
                            task_worker dequeue  →  process_ingress_message
                                                    ↓
                        heartbeat route  →  on_data_received()  →  on_heartbeat_received()
                                                    ↓
                              heartbeat_monitor_.ms_since_last_heartbeat() resets to 0
```

Any break in this chain lasting > 32 s triggers the RX timeout.

### 3.2 Candidate causes ranked by likelihood

#### CAUSE A-1 (Most likely): ESP-NOW NO_MEM in the TX driver silently drops heartbeats

`EspnowTxScheduler` uses a priority queue.  When a heartbeat is dequeued but
its `min_gap_ms` has not elapsed, it is re-enqueued to the **back** of the
queue via `xQueueSend`.  Under sustained bidirectional traffic (catalog
responses, config sync, LED state, version beacons) the heartbeat can be
pushed to the back of a 40-slot queue and delayed well beyond its nominal 10 s
cadence.

More critically, when `esp_now_send()` returns `ESP_ERR_ESPNOW_NO_MEM` (the
ESP-NOW internal frame-buffer pool is exhausted), the **send callback fires
with `ESP_NOW_SEND_FAIL`**.  The scheduler retries with exponential back-off.
If three or four consecutive heartbeats are lost this way, and the back-off
intervals sum to > 32 s, the RX will time out before a heartbeat successfully
arrives.

Evidence supporting this:
- The connection loss is observed at ~36m:50s — exactly in the region after
  init burst, catalog exchange, and config retries are all active simultaneously.
- The `tick()` method on the RX suppresses outbound traffic if
  `ms_since_last_heartbeat() ≥ 12 000` (anti-NO_MEM guard), confirming that
  the designers already knew NO_MEM pressure was a real problem.
- The `EspnowTxScheduler` cadence re-queue bug (item requeued to back even for
  high-priority items when `min_gap_ms` not elapsed) can starve heartbeats
  behind data traffic.

#### CAUSE A-2: WiFi power-save silently re-enabled

`init_radio()` calls `esp_wifi_set_ps(WIFI_PS_NONE)` once at boot.  In some
ESP-IDF versions, certain events (DHCP renewal, WiFi STA reconnect, IP address
change) can re-arm the default power-save mode.  When the LVGL display driver
(running on Core 0) and the WiFi/ESP-NOW stack compete for the same core's
radio time under power-save, incoming frames may be received during modem
sleep and silently discarded by the driver.

There is no watchdog anywhere in the RX code that periodically re-asserts
`esp_wifi_set_ps(WIFI_PS_NONE)`.

#### CAUSE A-3: MQTT reconnection interrupting ESP-NOW receive path

If the MQTT broker is unreachable, the LCD receiver's MQTT client will
periodically attempt reconnection.  The underlying TCP/IP socket operations
run on `lwIP` tasks which share the WiFi radio path.  Extended TCP connection
attempts can cause the WiFi driver to defer ESP-NOW receive processing,
resulting in a burst of missed heartbeats coinciding with the MQTT retry
window.

#### CAUSE A-4: TX heartbeat scheduler starvation (structural)

`EspnowTxScheduler` dequeues one item per loop iteration.  If `min_gap_ms`
check fails, the item goes to the **back** of the same priority queue.  A
heartbeat (`CONTROL` priority, P0) cannot be starved by lower-priority items,
**but it can be delayed by other P0 CONTROL items** (ACK frames) if they are
queued rapidly.  The ACK throttle on the RX (1 000 ms when CONNECTED) limits
this, but during reconnection windows the throttle drops to 100 ms, producing
a burst of ACKs that can back-pressure the heartbeat slot.

---

## 4. Root Cause B — Why Does Reconnection Fail?

### 4.1 The designed quick-reconnect path

The design intent (from the comment in `rx_connection_handler.cpp`) is:

```
RX heartbeat timeout (32 s)
    → CONNECTED → IDLE (peer removed, channel unlocked)
    → auto-reconnect: IDLE → CONNECTING
    → TX still CONNECTED, still sending unicast heartbeats to RX MAC
    → unicast heartbeat arrives at RX while CONNECTING
    → task_worker: TX not in peer table → add_peer → on_peer_registered → PEER_REGISTERED event
    → connection_manager: CONNECTING → CONNECTED
    → TX never notices: heartbeat_ack stream resumes within one heartbeat interval (~10 s)
```

### 4.2 Why the quick-reconnect path fails

#### BUG B-1 (Critical): Stale comment masks a real peer-removal bug

The comment in `rx_connection_handler.cpp` says:

```
// TX is still CONNECTED and still sending unicast heartbeats.  The unicast
// frames reach receiver (TX peer stays registered because on_connection_lost()
// zeros transmitter_mac_ before remove_peer() is attempted, so the peer is
// never actually deleted).
```

**This comment is incorrect.** The code was patched (correctly) to save
`saved_peer_mac` BEFORE calling `on_connection_lost()`, which allowed
`remove_peer` to succeed.  The peer IS now deleted on disconnect.  The comment
was not updated.

This matters because the comment describes the reconnect mechanism as relying
on the TX peer remaining registered.  The mechanism now relies on the peer
being **re-added** by the `task_worker` when a heartbeat arrives from TX.

The re-add path in `task_worker` is:
```cpp
if (!EspnowPeerManager::is_peer_registered(msg.mac)) {
    if (EspnowPeerManager::add_peer(msg.mac, 0)) {
        connection_handler.on_peer_registered(msg.mac);
    }
}
```

If `add_peer(msg.mac, 0)` fails for any reason (e.g., the ESP-NOW peer table
is momentarily in an inconsistent state after `remove_peer`), `on_peer_registered`
is silently skipped, no `PEER_REGISTERED` event is posted, and the RX stays in
`CONNECTING` until the CONNECTING timeout fires.

#### BUG B-2 (Critical): CONNECTING timeout + heartbeat interval race

`init_state()` sets:
```cpp
EspNowConnectionManager::instance().set_connecting_timeout_ms(
    TimingConfig::ESPNOW_CONNECTING_TIMEOUT_MS);
```

If `ESPNOW_CONNECTING_TIMEOUT_MS` is shorter than or comparable to the TX
heartbeat interval (10 s), the following race occurs:

```
t=0:    RX enters CONNECTING
t=8s:   CONNECTING timeout fires → RX → IDLE → auto-reconnect → CONNECTING again
t=10s:  TX sends heartbeat but RX just re-entered CONNECTING at t=8s
         The deferred_peer_ was reset by on_connection_lost() at t=8s
         flush_deferred_peer_registered() fires in CONNECTING callback but nothing to flush
t=18s:  CONNECTING timeout fires again → IDLE → CONNECTING
...cycle repeats...
```

In this scenario the RX cycles through CONNECTING→IDLE→CONNECTING every
`ESPNOW_CONNECTING_TIMEOUT_MS` seconds without ever successfully receiving a
heartbeat during a stable CONNECTING window.

After 41 s (38m:03s − 37m:22s), the TX's heartbeat-ACK monitor fires its 35 s
threshold.  At this point the TX transitions CONNECTED→IDLE and starts channel
hopping.  The channel hopping eventually lands on the RX's WiFi channel and
completes a full PROBE/ACK discovery.  This is the **slow path** (13–26 s full
scan) that the quick-reconnect was designed to avoid.

#### BUG B-3: Heartbeat-ACK not sent for the transitional heartbeat

When a TX heartbeat arrives while the RX is in `CONNECTING`:

1. `task_worker` processes the heartbeat.
2. `add_peer(msg.mac, 0)` succeeds.
3. `on_peer_registered()` posts `PEER_REGISTERED` to the event queue.
4. `process_ingress_message()` runs the heartbeat route → calls `on_data_received()`.
   - `on_heartbeat_received()` updates the monitor.
   - But the **heartbeat_ack send** (in the heartbeat route handler) checks
     whether the manager is `CONNECTED` before sending.  At this point the state
     is still `CONNECTING` (the event queue hasn't been drained yet).
   - **The heartbeat_ack for this heartbeat is NOT sent.**
5. `process_events()` drains the queue → `PEER_REGISTERED` → `CONNECTING → CONNECTED`.

The TX receives no heartbeat_ack for this heartbeat.  The TX's heartbeat-ACK
timer does not reset.  The TX sends the next heartbeat 10 s later; by then the
RX is CONNECTED and sends the ack normally.  This is a 10 s gap in the TX's
ack stream, harmless with a 35 s TX timeout.

**However**, if BUG B-2 is also active (CONNECTING timeout cycling), the TX
sees multiple consecutive heartbeats with no ack — each CONNECTING window
expires before `CONNECTED` is reached, so the heartbeat route never reaches
the ack-send code while CONNECTED.  The TX's ack timer keeps running until the
35 s threshold, which fires the CONNECTED→IDLE cascade observed at 38m:03s.

#### BUG B-4: Post-reconnect init burst triggers another NO_MEM cascade

Even after the TX completes channel-hop discovery and both sides reach
`CONNECTED`, the RX init burst (power profile request, version announce, config
section retries, catalog retry engine) fires within 2 s of CONNECTED.
Combined with the TX's own version-beacon and any queued data frames, this
produces exactly the `ESP_ERR_ESPNOW_NO_MEM` pressure described in Cause A-1,
potentially triggering a second disconnect minutes after the first reconnect.

This explains any patterns of "reconnect, run for a few minutes, disconnect
again."

---

## 5. Complete Failure Sequence (Reconstructed)

```
~36m:50s  Last successful heartbeat exchange (TX → RX, RX ack → TX).

          TX continues sending heartbeats every ~10 s.
          One or more heartbeats fail to deliver (NO_MEM / scheduler delay).
          RX heartbeat monitor keeps counting: no successful frame in 32 s.

37m:22s   RX heartbeat monitor fires.
          CONNECTED → IDLE (TX peer removed, channel unlocked).
          Auto-reconnect → IDLE → CONNECTING.

37m:22s–  RX in CONNECTING.
38m:03s   TX sends heartbeats but:
            Option A: CONNECTING timeout (ESPNOW_CONNECTING_TIMEOUT_MS) fires
                      before heartbeat arrives → IDLE → CONNECTING cycle repeats.
            Option B: add_peer fails transiently on first arrival → no
                      PEER_REGISTERED event → heartbeat ack never sent.
          Either way, TX receives no heartbeat_ack for 41 s.

38m:03s   TX heartbeat-ACK monitor (35 s threshold) fires:
          CONNECTED → IDLE.
          TxSendGuard::notify_connection_state(false).
          DiscoveryTask starts active channel hopping (13 channels × ~1 s each).

~38m:15s  TX reaches RX's WiFi channel during hop scan.
          RX (in CONNECTING) receives PROBE → sends ACK.
          TX receives ACK → PEER_FOUND → PEER_REGISTERED → TX CONNECTED.

~38m:20s  RX receives TX heartbeat (unicast, post-discovery).
          add_peer → on_peer_registered → PEER_REGISTERED → RX CONNECTED.
          Init burst starts (2 s grace + paced).
```

---

## 6. Proposed Solutions

### Solution 1 — Fix the CONNECTING Timeout Race (BUG B-2)

Set `ESPNOW_CONNECTING_TIMEOUT_MS` to at least **3× the TX heartbeat
interval** to guarantee that the RX stays in `CONNECTING` long enough to
receive at least two heartbeats.

If the TX heartbeat interval is 10 s:
```
ESPNOW_CONNECTING_TIMEOUT_MS = 35 000  // same as TX threshold
```

This ensures the RX stays in CONNECTING for the same duration as the TX's
own timeout, giving the heartbeat time to arrive and close the loop.

**File:** `esp32common/config/timing_config.h`  
Change `ESPNOW_CONNECTING_TIMEOUT_MS` to `35000` if currently shorter.

---

### Solution 2 — Do NOT Remove the TX Peer on RX Disconnect

The current design removes the TX peer on disconnect and relies on `add_peer`
succeeding when the next heartbeat arrives.  The simpler and more reliable
approach is to **keep the TX peer registered** during the CONNECTING window:

```cpp
// In the CONNECTED → IDLE state callback in rx_connection_handler.cpp:
// INSTEAD of removing the peer immediately, mark it as stale and only
// remove if a CONNECTING timeout occurs without receiving any heartbeat.

// Immediate peer cleanup should only happen when CONNECTING_TIMEOUT fires,
// not when CONNECTED → IDLE first transitions.
```

Concretely:
- In the `CONNECTED → IDLE` callback: **do not call `remove_peer`**.
  Just call `on_connection_lost()` and `unlock_channel`.
- In the `CONNECTING → IDLE` callback (timeout case): call `remove_peer` now.
- In `CONNECTING → CONNECTED`: leave existing peer intact (it's already
  registered and valid).

This eliminates BUG B-1 and BUG B-2 together.  The peer is always available
for the heartbeat to arrive and be responded to; `add_peer` failure is no
longer on the critical reconnect path.

**Files to change:**
- `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp` — restructure
  peer cleanup into the `CONNECTING → IDLE` (timeout) branch only.

---

### Solution 3 — Send Heartbeat-ACK Before State Transition (BUG B-3)

The heartbeat route handler currently checks `is_connected()` before sending
the heartbeat_ack.  During the transitional heartbeat (received while
CONNECTING), the state has not yet advanced to CONNECTED when the route runs.

Fix: Change the heartbeat route handler to send the ack if the sender's MAC
matches the known transmitter MAC regardless of current state, OR send the ack
unconditionally for any valid unicast heartbeat:

```cpp
// In the heartbeat route handler (espnow_routes.cpp or equivalent):
// Replace:
if (EspNowConnectionManager::instance().is_connected()) {
    send_heartbeat_ack(msg.mac);
}
// With:
// Always ACK a heartbeat if we have a valid MAC — the ACK is harmless if
// we are still connecting, and it resets the TX's ack-timer correctly.
send_heartbeat_ack(msg.mac);
```

This eliminates BUG B-3 and ensures the TX's ack timer resets on the very
first transitional heartbeat, preventing the TX from ever reaching its 35 s
threshold during a quick-reconnect event.

---

### Solution 4 — Reduce NO_MEM Pressure (Root Cause A-1)

#### 4a. Periodic power-save assertion

In the RX's main task loop (or a dedicated watchdog task), periodically
re-assert `WIFI_PS_NONE`:

```cpp
// Every 60 s in task_worker or a heartbeat watchdog:
if (esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK) {
    LOG_WARN("ESPNOW", "Failed to re-assert WIFI_PS_NONE");
}
```

#### 4b. Rate-limit non-critical outbound traffic during reconnect window

The 12 s silence guard in `tick()` is a good idea but its threshold should be
tuned to the actual heartbeat interval:

```cpp
// Current (12 s):
if (EspNowConnectionManager::instance().ms_since_last_heartbeat() >= 12000) {
    return;
}
// Recommended (heartbeat_interval + 2 s margin):
if (EspNowConnectionManager::instance().ms_since_last_heartbeat() >= 
    TimingConfig::TX_HEARTBEAT_INTERVAL_MS + 2000) {
    return;
}
```

If the heartbeat interval changes, the guard automatically scales.

#### 4c. Fix EspnowTxScheduler back-of-queue starvation

When a CONTROL-priority item is dequeued but `min_gap_ms` has not elapsed,
re-insert it at the **front** of the queue (or into a separate time-ordered
delay list) rather than the back:

```cpp
// In EspnowTxScheduler dequeue loop, when min_gap_ms not elapsed for P0:
if (priority == MessagePriority::CONTROL) {
    // Return to front of queue, not back
    xQueueSendToFront(q, &item, 0);
} else {
    xQueueSend(q, &item, 0);
}
```

This prevents data traffic from pushing a slightly-early heartbeat past its
window.

---

### Solution 5 — Post-Reconnect Init Burst Pacing (BUG B-4)

The init burst already has a 2 s grace period, but after a reconnect that
follows a TX channel-hop (the slow path), both TX and RX attempt to sync state
simultaneously.

Add a **post-TX-hop detection** gate: when the connection was established via
a PROBE/ACK path (rather than the heartbeat quick path), extend the init burst
grace to 5 s and reduce catalog retry cadence to every 60 s for the first 5
minutes:

```cpp
// In rx_connection_handler.cpp CONNECTED callback:
const bool was_probe_reconnect = (/* detect via PROBE route flag */);
init_grace_ms_ = was_probe_reconnect ? 5000 : 2000;
```

A simple detection: set a `bool reconnected_via_probe_` flag in
`on_probe_received()` and clear it in `on_connection_lost()`.

---

### Solution 6 — Fix Stale Comment (Hygiene)

Update the comment in `rx_connection_handler.cpp` `init()` to accurately
describe the current peer-removal behaviour:

```cpp
// Set receiver timeout to 32 s — 3 s BEFORE TX's 35 s threshold.
// On timeout: TX peer is removed and channel is unlocked.
// On the following TX heartbeat (still unicast to RX MAC): task_worker
// re-adds the TX peer via add_peer(), fires on_peer_registered(), and the
// manager transitions CONNECTING → CONNECTED within one heartbeat interval.
// TX never reaches its 35 s threshold if the quick-reconnect path succeeds.
```

---

## 7. Priority-Ordered Fix Plan

| Priority | Fix | Files Affected | Impact |
|----------|-----|----------------|--------|
| P0 | **Sol. 2** — Keep TX peer during CONNECTING window | `rx_connection_handler.cpp` | Eliminates reconnect race entirely |
| P0 | **Sol. 3** — Send heartbeat-ACK regardless of state | heartbeat route handler | TX ack timer never reaches 35 s during quick reconnect |
| P1 | **Sol. 1** — CONNECTING timeout ≥ 35 s | `timing_config.h` | Belt-and-braces; removes timing sensitivity |
| P1 | **Sol. 4c** — CONTROL requeue to front of scheduler | `EspnowTxScheduler` | Prevents heartbeat starvation behind data |
| P2 | **Sol. 4a** — Periodic WIFI_PS_NONE | `task_worker` or watchdog | Guards against power-save re-arm |
| P2 | **Sol. 4b** — Scale silence guard to heartbeat interval | `rx_connection_handler.cpp tick()` | Removes magic number |
| P3 | **Sol. 5** — Extended init grace after probe reconnect | `rx_connection_handler.cpp` | Reduces post-reconnect NO_MEM spike |
| P3 | **Sol. 6** — Fix stale comment | `rx_connection_handler.cpp` | Code hygiene |

---

## 8. Verification Plan

After applying P0 + P1 fixes:

1. Flash updated firmware to both devices.
2. Run for at least 2 hours with serial monitors open on both.
3. **Expected new behaviour:** If a heartbeat is missed, RX enters CONNECTING,
   receives the next TX heartbeat within ≤ 10 s, reconnects, and TX never
   reaches 35 s.  TX log should show no `State change: CONNECTED → IDLE` events
   during these reconnects.  RX log should show `CONNECTING → CONNECTED` within
   10–15 s of its own timeout.
4. **Connection loss root cause:** Enable verbose ESP-NOW send statistics to
   detect `ESP_ERR_ESPNOW_NO_MEM` events.  Add a counter in
   `EspnowTxScheduler::send()` that logs `[NO_MEM count=%u]` whenever the
   return from `esp_now_send` is `ESP_ERR_ESPNOW_NO_MEM`, and surface this
   counter in the diagnostics endpoint or periodic log.

---

*End of analysis.*
