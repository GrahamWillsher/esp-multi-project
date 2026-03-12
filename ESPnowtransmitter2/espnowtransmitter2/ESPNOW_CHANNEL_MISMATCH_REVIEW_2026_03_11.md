# ESP-NOW Channel Mismatch Review (TX-side)

Date: 2026-03-11  
Scope: Why transmitter reports `Peer channel is not equal to the home channel, send fail!` when receiver reboots, and how to make failure handling graceful.

---

## 1) Executive Summary

The error is real and expected when the transmitter's current WiFi channel (home channel) differs from the peer's registered ESP-NOW channel.

Current transmitter behavior is **not graceful** because high-frequency send paths keep calling `esp_now_send()` every cycle, producing repeated immediate failures (`ESP_ERR_ESPNOW_ARG`) and log spam.

### Primary root cause

A channel-coherency gap exists between:
- peer registration channel (fixed in peer table), and
- runtime WiFi/home channel.

When those diverge, all unicast sends fail immediately with `ESP_ERR_ESPNOW_ARG`.

### Why this is noisy/non-graceful today

The main TX send paths use direct `esp_now_send()` with no channel-mismatch guard and no coordinated recovery throttle:
- `transmission_task.cpp` transient/state sending (high-rate)
- `version_beacon_manager.cpp` beacons
- `heartbeat_manager.cpp` heartbeats

---

## 2) Evidence From Current Code

## 2.1 Where the failures are logged

- `transmission_task.cpp` logs repeated transient send failures:
  - `Failed to send transient (seq: ...): ESP_ERR_ESPNOW_ARG`
- `version_beacon_manager.cpp` logs beacon send failures:
  - `Send failed: ESP_ERR_ESPNOW_ARG`

These match your runtime logs exactly.

## 2.2 Peer registration uses explicit channel

Discovery registers the receiver peer with explicit `ack_channel`:
- On discovery success, TX sets WiFi channel to ACK channel.
- Then adds peer with that same explicit channel.

This is correct at registration time, but it means later home-channel drift causes strict mismatch failures.

## 2.3 Send paths bypass graceful backoff utility

A common utility exists:
- `esp32common/espnow_common_utils/espnow_send_utils.cpp`

It has backoff and pause logic, but key TX paths still call `esp_now_send()` directly, so mismatch storms are not damped.

## 2.4 Connection-state gating is necessary but insufficient

`transmission_task.cpp` checks connected state before sending. That prevents sends during clear disconnect states, but it does not protect against this scenario:
- state still CONNECTED (or not yet transitioned), and
- peer channel/home channel already diverged.

Result: immediate `ESP_ERR_ESPNOW_ARG` loops.

---

## 3) Failure Mechanism (What is happening)

A representative sequence during receiver reboot:

1. TX is connected and actively sending.
2. Receiver reboots; link quality/state is transient.
3. TX continues sending (by design until connection manager transitions).
4. At some point, TX home channel and peer registered channel diverge.
5. ESP-IDF rejects unicast sends with:
   - `Peer channel is not equal to the home channel, send fail!`
   - return code `ESP_ERR_ESPNOW_ARG`
6. TX high-rate task retries immediately on next cycle (same seq remains unsent), causing repeated errors.

This exactly matches the observed repeated `seq: 2176` failures.

---

## 4) Why this is not graceful today

## 4.1 No channel-coherency preflight before high-rate sends

There is no centralized pre-send check of:
- current WiFi channel (`esp_wifi_get_channel`)
- peer channel (`esp_now_get_peer`)
- locked/expected channel (`g_lock_channel` / TX state cache)

## 4.2 No one-shot recovery trigger on `ESP_ERR_ESPNOW_ARG`

`ESP_ERR_ESPNOW_ARG` is treated like any generic send error and logged repeatedly, instead of escalating immediately to a controlled recovery path.

## 4.3 No unified send wrapper for all TX packet types

Multiple modules independently send packets, so behavior is inconsistent and noisy under fault.

---

## 5) Recommended Solution (Suitable + Practical)

## 5.1 Introduce a single TX send gateway (high priority)

Create a transmitter-side wrapper (e.g. `tx_send_guard`) used by:
- transient/state sending
- heartbeats
- version beacons
- config ACK responses

Wrapper responsibilities:

1. **Preflight channel coherence**
   - Read current home channel.
   - Read peer channel from peer table.
   - If `peer.channel != 0` and `peer.channel != home_channel`, do not send.

2. **Graceful mismatch handling (one-shot, rate-limited)**
   - On first mismatch in a window:
     - log once at error level,
     - increment a mismatch counter,
     - trigger recovery (below).
   - Suppress repeated per-packet logs during same window.

3. **Use controlled backoff**
   - Route failures through existing backoff semantics (`EspnowSendUtils`) or equivalent local gate.

## 5.2 Immediate recovery policy for channel mismatch (high priority)

On detected mismatch (or first `ESP_ERR_ESPNOW_ARG`):

- Post a connection-loss/reset event to move TX out of steady CONNECTED send behavior.
- Stop high-rate packet attempts until reconnect is re-established.
- Restart active discovery/channel lock flow cleanly.

Design target:
- one structured recovery log burst,
- no 20+ repeated send errors per second.

## 5.3 Optional self-heal before full reconnect (medium priority)

Attempt quick self-heal before full restart:

- If expected channel is known and trustworthy, set home channel back to expected.
- If peer entry channel is stale but home is trusted, update peer entry channel.

If quick heal fails once, escalate to full reconnect.

## 5.4 Log hygiene + observability (medium priority)

Add counters and periodic summaries:
- `channel_mismatch_detected`
- `channel_mismatch_recovered_quick`
- `channel_mismatch_reconnect_triggered`
- `espnow_arg_send_failures`

Print concise summary every 30–60s instead of per-packet spam.

---

## 6) Concrete Implementation Plan

## Step A — Add channel-coherency check helper

New helper in transmitter ESP-NOW module:
- `bool is_peer_channel_coherent(const uint8_t* peer_mac, uint8_t* out_home, uint8_t* out_peer)`

Use `esp_wifi_get_channel` + `esp_now_get_peer`.

## Step B — Add unified guarded send API

New function:
- `esp_err_t send_to_receiver_guarded(const uint8_t* mac, const uint8_t* data, size_t len, const char* tag)`

Behavior:
- preflight coherence
- throttled mismatch logging
- one-shot recovery trigger
- backoff gate

## Step C — Replace direct `esp_now_send()` in TX paths

Update these modules to use guarded send:
- `src/espnow/transmission_task.cpp`
- `src/espnow/heartbeat_manager.cpp`
- `src/espnow/version_beacon_manager.cpp`
- `src/espnow/message_handler.cpp` (ACK/config response sends)

## Step D — Recovery trigger wiring

When guarded send detects mismatch:
- set `channel_mismatch_recovery_active` flag,
- post connection reset/lost event once,
- prevent additional triggers until recovery completes.

---

## 7) Expected Behavior After Fix

When receiver reboots or channel mismatch occurs:

- TX detects mismatch once.
- TX stops noisy repeated send attempts.
- TX transitions to controlled recovery.
- Discovery/channel lock re-establishes peer/home alignment.
- TX resumes normal sending.

User-visible result:
- brief, informative recovery logs,
- no flood of repeated `ESP_ERR_ESPNOW_ARG` errors.

---

## 8) Risk / Compatibility Notes

- This is TX-only and does not require immediate receiver protocol changes.
- Existing ESP-NOW packet formats remain unchanged.
- Main behavioral change is failure handling policy and log volume reduction.

---

## 9) Recommendation

Proceed with Step A–D as a single TX hardening patch.

This directly addresses your “not a graceful fail” concern and makes channel mismatch recovery deterministic and reviewable.
