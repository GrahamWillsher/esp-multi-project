# ESP-NOW Heartbeat (TX↔RX) — Design & Operations

**Status:** Specification / Implementation Guide  
**Scope:** Transmitter + Receiver  
**Goal:** Reliable liveness detection, clean disconnect/reconnect behavior

---

## 1) Audit of Existing Redesign Docs

A review of ESPNOW_REDESIGN_COMPLETE_ARCHITECTURE.md shows **no explicit heartbeat message definition or flow**. There is no documented `msg_heartbeat` message or expected behavior in disconnect/reconnect scenarios. This document fills that gap.

---

## 2) Heartbeat Message Definition

### 2.1 Message Type
- **Type:** `msg_heartbeat`
- **Direction:** TX → RX (primary)
- **Optional:** RX → TX (echo or acknowledge)

### 2.2 Payload (required)
```cpp
struct heartbeat_t {
    uint8_t  type;          // msg_heartbeat
    uint32_t seq;           // Monotonic sequence number (REQUIRED - detects loss/resets)
    uint32_t uptime_ms;     // Sender uptime
    uint8_t  state;         // Connection state enum
    uint8_t  rssi;          // Last RX RSSI (if known)
    uint8_t  flags;         // Bitfield (e.g., low_batt, degraded)
    uint16_t checksum;      // CRC16 checksum (REQUIRED - as per standard message format)
};
```

**Field requirements:**
- **`seq`**: REQUIRED. Monotonic sequence number incremented on every heartbeat. Enables:
  - Packet loss detection (gaps in sequence).
  - Transmitter reset detection (sequence regression).
  - Duplicate detection (same sequence received twice).
- **`checksum`**: REQUIRED. CRC16 checksum computed over the entire message (excluding checksum field itself). Uses the standard CRC implementation shared by all ESP-NOW messages.

**Why include other fields?**
- Enables diagnostics (latency, packet loss, sender resets).
- Prevents false positives by tracking sequence and uptime.

### 2.3 Heartbeat Acknowledgment (REQUIRED)
The receiver MUST acknowledge each heartbeat to confirm bidirectional link health.

```cpp
struct heartbeat_ack_t {
    uint8_t  type;          // msg_heartbeat_ack
    uint32_t ack_seq;       // Sequence number being acknowledged
    uint32_t uptime_ms;     // Receiver uptime
    uint8_t  state;         // Receiver connection state
    uint16_t checksum;      // CRC16 checksum (REQUIRED)
};
```

**ACK behavior:**
- Receiver sends `heartbeat_ack_t` immediately upon receiving valid heartbeat.
- Transmitter tracks ACKs and flags missing ACKs as potential link degradation.
- If N consecutive heartbeats are not ACKed, transmitter posts `CONNECTION_LOST`.

---

## 3) Heartbeat Timing

**Recommended defaults (adjustable):**
- Interval: **10s**
- Warning threshold: **3 missed** (≈ 30s)
- Disconnect threshold: **9 missed** (≈ 90s)
- Jitter: **±500 ms** (avoid sync storms if multiple devices)

---

## 4) Behavior on Each Device

### 4.1 Transmitter (TX)
**Responsibilities:**
1. Send heartbeat periodically when `CONNECTED`.
2. Increment `heartbeat_seq` counter before each send.
3. Track ACKs for last N heartbeats (e.g., 3).
4. Detect receiver failure if no ACK received after multiple heartbeats.
5. Compute CRC16 checksum over message before sending.

**State machine interactions:**
- On successful send: no state change, remains `CONNECTED`.
- On N consecutive unacked heartbeats: post `CONNECTION_LOST`, transition to `IDLE` and restart discovery.

**Recommended integration points:**
- Keep-alive task or periodic timer (10s interval).
- On `CONNECTION_LOST`, post `RESET_CONNECTION` to common manager and restart discovery.

**Skeleton:**
```cpp
static uint32_t heartbeat_seq = 0;
static uint32_t last_ack_seq = 0;

void tx_send_heartbeat() {
    if (connection_manager::get_state() != STATE_CONNECTED) return;

    heartbeat_t hb;
    hb.type = msg_heartbeat;
    hb.seq = ++heartbeat_seq;  // Increment sequence
    hb.uptime_ms = millis();
    hb.state = connection_manager::get_state();
    hb.rssi = get_last_rssi();
    hb.flags = get_status_flags();
    hb.checksum = calculate_crc16(&hb, sizeof(hb) - sizeof(hb.checksum));

    esp_now_send(receiver_mac, (uint8_t*)&hb, sizeof(hb));
}

void tx_on_heartbeat_ack(const heartbeat_ack_t* ack) {
    // Validate CRC
    if (!validate_crc16(ack, sizeof(*ack))) {
        Serial.println("Heartbeat ACK CRC failed");
        return;
    }
    
    if (ack->ack_seq > last_ack_seq) {
        last_ack_seq = ack->ack_seq;
    }
}

void tx_check_ack_timeout() {
    if (heartbeat_seq - last_ack_seq > 3) {  // 3 consecutive unacked
        connection_manager::post_event(CONNECTION_LOST);
    }
}
```

### 4.2 Receiver (RX)
**Responsibilities:**
1. Receive heartbeat and validate CRC.
2. Update `last_rx_time_ms`.
3. Send `heartbeat_ack` with matching sequence number.
4. Compute CRC16 checksum over ACK message before sending.
5. If no heartbeat within timeout, transition to `CONNECTION_LOST` and restart discovery.

**State machine interactions:**
- On heartbeat: Update liveness timestamp (do NOT post `DATA_RECEIVED` for heartbeats—use separate tracking).
- On timeout: `CONNECTION_LOST` event to common manager.

**Skeleton:**
```cpp
static uint32_t last_heartbeat_seq = 0;

void rx_on_heartbeat(const heartbeat_t* hb) {
    // Validate CRC
    if (!validate_crc16(hb, sizeof(*hb))) {
        Serial.println("Heartbeat CRC failed");
        return;
    }

    // Detect sequence regression (TX reboot)
    if (hb->seq < last_heartbeat_seq) {
        Serial.printf("TX reboot detected (seq %u -> %u)\n", last_heartbeat_seq, hb->seq);
    }
    last_heartbeat_seq = hb->seq;

    // Update liveness
    last_rx_time_ms = millis();

    // Send ACK
    heartbeat_ack_t ack;
    ack.type = msg_heartbeat_ack;
    ack.ack_seq = hb->seq;
    ack.uptime_ms = millis();
    ack.state = connection_manager::get_state();
    ack.checksum = calculate_crc16(&ack, sizeof(ack) - sizeof(ack.checksum));

    esp_now_send(transmitter_mac, (uint8_t*)&ack, sizeof(ack));
}

void rx_check_heartbeat_timeout() {
    if (millis() - last_rx_time_ms > 90000) {  // 90s timeout
        connection_manager::post_event(CONNECTION_LOST);
    }
}
```

### 4.3 Current Setup Impact (Data-as-Heartbeat)
Right now, the receiver treats **any incoming message** as liveness. That means:
- `DATA_RECEIVED` is posted on *all* messages (config, data, probe-related packets).
- The common manager remains `CONNECTED` even if heartbeats are not defined.
- You will see logs like:
   - `DATA_RECEIVED (remaining connected)`
   - `Already connected, ignoring PEER_FOUND`

This behavior is **expected** while data traffic is constant, but it is **not a true heartbeat**. If data is paused, the receiver may appear to disconnect even though the link is still healthy.

The heartbeat design below changes this so:
- `DATA_RECEIVED` only represents actual data traffic.
- `HEARTBEAT` (or `DATA_RECEIVED` from heartbeat packets) becomes the authoritative liveness signal.

---

## 5) Disconnection Handling

### 5.1 TX Lost RX
- TX continues heartbeat sends.
- If N consecutive sends fail, post `CONNECTION_LOST` and restart discovery.
- TX resets peer registration if needed (clean slate).

### 5.2 RX Lost TX
- RX watchdog detects timeout based on heartbeat timestamp.
- RX posts `CONNECTION_LOST` and clears `transmitter_connected`.
- RX resumes discovery announcements and waits for next PROBE.

---

## 6) Reconnection Flow

**Typical sequence:**
1. TX restarts discovery (channel hopping).
2. RX receives PROBE, sends ACK.
3. TX registers peer and transitions to CONNECTED.
4. TX resumes heartbeat sending.
5. RX starts receiving heartbeat and clears any reconnect flags.

---

## 7) Implementation Sketch (Both Devices)

### 7.1 TX Sender
```cpp
static uint32_t heartbeat_seq = 0;
static uint32_t last_ack_seq = 0;
static uint32_t last_hb = 0;

void tx_heartbeat_task() {
    if (!conn_mgr.is_connected() || millis() - last_hb < 10000) return;

    heartbeat_t hb;
    hb.type = msg_heartbeat;
    hb.seq = ++heartbeat_seq;
    hb.uptime_ms = millis();
    hb.state = conn_mgr.get_state();
    hb.rssi = get_last_rssi();
    hb.flags = 0;
    hb.checksum = calculate_crc16(&hb, sizeof(hb) - sizeof(hb.checksum));

    esp_now_send(rx_mac, (uint8_t*)&hb, sizeof(hb));
    last_hb = millis();

    // Check ACK timeout (3 consecutive unacked)
    if (heartbeat_seq - last_ack_seq > 3) {
        conn_mgr.post_event(CONNECTION_LOST);
    }
}

void tx_on_heartbeat_ack(const heartbeat_ack_t* ack) {
    if (!validate_crc16(ack, sizeof(*ack))) return;
    if (ack->ack_seq > last_ack_seq) {
        last_ack_seq = ack->ack_seq;
    }
}
```

### 7.2 RX Handler
```cpp
static uint32_t last_heartbeat_seq = 0;
static uint32_t last_rx_time_ms = 0;

void rx_on_heartbeat(const heartbeat_t* hb) {
    if (!validate_crc16(hb, sizeof(*hb))) return;

    if (hb->seq < last_heartbeat_seq) {
        Serial.printf("TX reboot detected (seq %u -> %u)\n", last_heartbeat_seq, hb->seq);
    }
    last_heartbeat_seq = hb->seq;
    last_rx_time_ms = millis();

    // Send ACK
    heartbeat_ack_t ack;
    ack.type = msg_heartbeat_ack;
    ack.ack_seq = hb->seq;
    ack.uptime_ms = millis();
    ack.state = conn_mgr.get_state();
    ack.checksum = calculate_crc16(&ack, sizeof(ack) - sizeof(ack.checksum));
    esp_now_send(tx_mac, (uint8_t*)&ack, sizeof(ack));
}
```

### 7.3 RX Timeout Check
```cpp
void rx_check_timeout() {
    if (millis() - last_rx_time_ms > 90000) {  // 90s timeout
        conn_mgr.post_event(CONNECTION_LOST);
    }
}
```

### 7.4 Transition Plan (From Data-as-Heartbeat → Dedicated Heartbeat)

**Goal:** Stop using general data traffic to keep the connection alive, and instead use a dedicated heartbeat packet.

**Changes required:**
1. **Add heartbeat message type** in shared message definitions.
2. **Transmit heartbeat** on TX at fixed interval (e.g., 10s).
3. **Handle heartbeat** on RX and update `last_rx_time_ms`.
4. **Gate `DATA_RECEIVED` events** so they are posted only for real data packets.

### 7.5 Code Areas to Update (Current Projects)

**Transmitter**
- Keep-alive or periodic task should send `msg_heartbeat` when `CONNECTED`.
- Suggested integration point: keep-alive manager task or transmission task loop.

**Receiver**
- In `espnow_tasks.cpp`, route `msg_heartbeat` to a handler that:
   - Updates `last_rx_time_ms`
   - Posts `DATA_RECEIVED` (or a new `HEARTBEAT_RECEIVED`) event
- Ensure `on_data_received()` is called **only** for real data packets, not for PROBE or config packets.

**Connection manager**
- Option A (minimal change): treat heartbeat as `DATA_RECEIVED`.
- Option B (cleaner): add a new `HEARTBEAT_RECEIVED` event to the common manager.

**Observed log example (current):**
```
[TX_MGR] MAC registered: 5C:01:3B:53:2F:18
[CONN_MGR-DEBUG] Processing event: DATA_RECEIVED (state: CONNECTED)
[CONN_MGR-DEBUG] DATA_RECEIVED (remaining connected)
[CONN_MGR-DEBUG] Processing event: PEER_FOUND (state: CONNECTED)
[CONN_MGR-DEBUG] Already connected, ignoring PEER_FOUND event
```
**After heartbeat transition:**
- `DATA_RECEIVED` appears only for data packets.
- Heartbeats update liveness without spamming `DATA_RECEIVED`.

---

## 8) Industry-Ready Improvements

1. **Sequence + Uptime Validation**  
   Detect TX resets by `seq` regression or uptime reset.

2. **Adaptive Timeout**  
   Use $timeout = interval \times N$ with jitter tolerance.

3. **Rate Limiting**  
   Prevent heartbeat flooding on reconnect loops.

4. **Peer Reset Strategy**  
   If reconnect fails N times, clear peer table and rebuild.

5. **Health Telemetry**  
   Include RSSI, voltage, and link quality metrics in heartbeat.

6. **Backoff + Jitter**  
   Exponential backoff for reconnect, add jitter to avoid collisions.

7. **Persistent Failure Mode**  
   After repeated failures, enter degraded mode and log metrics for diagnosis.

8. **ACK Optional**  
   For critical systems, use heartbeat ACK to confirm bidirectional link.

9. **Integration with MQTT/Web UI**  
   Expose heartbeat status to UI (last seen, missed count).

10. **Watchdog-friendly**  
    Keep heartbeat in its own low-priority task; never block.

---

## 9) Summary

- The current redesign doc does **not** define heartbeat behavior.  
- This specification adds a full heartbeat message flow for TX and RX.  
- It covers normal operation, disconnection detection, and reconnection.  
- It is designed to be deterministic, FreeRTOS-friendly, and industry-ready.
