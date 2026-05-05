# ESP-NOW First-Principles Reconnect Investigation (2026-04-26)

## Scope

This document restarts the ESP-NOW reconnect investigation from first principles using the current source tree and the latest observed runtime behaviour:

- transmitter repeatedly performs active channel-hop scans and does not lock back onto the receiver
- receiver remains reported as `CONNECTED` on channel 6 while heartbeat age rises well past 12 s
- receiver MQTT gate closes repeatedly with `ESP-NOW not ready`
- receiver continues to log ACK send failures with `ESP_ERR_ESPNOW_NO_MEM`

The goal here is not to explain one isolated function. It is to explain the full end-to-end reconnect behaviour across:

- transmitter discovery and reconnect flow
- receiver ingress, peer registration, heartbeat handling, ACK handling, and retry traffic
- MQTT coexistence and other receiver WiFi consumers
- the shared ESP-NOW connection manager and TX scheduler


## Source Files Re-Read For This Investigation

### Transmitter

- `ESPnowtransmitter2/espnowtransmitter2/src/main.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_connection_handler.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/heartbeat_manager.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_send_guard.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/queue/espnow_queue_manager.cpp`

### Shared/common

- `esp32common/espnow_common_utils/connection_manager.cpp`
- `esp32common/espnow_common_utils/connection_event_processor.cpp`
- `esp32common/espnow_common_utils/espnow_standard_handlers.cpp`
- `esp32common/espnow_common_utils/espnow_tx_scheduler.cpp`
- `esp32common/espnow_transmitter/espnow_transmitter.cpp`
- `esp32common/include/esp32common/config/timing_config.h`
- `esp32common/espnow_phase0/espnow_heartbeat_monitor.cpp`

### Receiver LCD

- `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp`
- `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp`
- `espnowreceiver_LCD/src/espnow/rx_state_machine.cpp`
- `espnowreceiver_LCD/src/mqtt/mqtt_task.cpp`
- `espnowreceiver_LCD/src/runtime/runtime_task_startup.cpp`

### Receiver 2

- `espnowreceiver_2/src/espnow/espnow_tasks.cpp`
- `espnowreceiver_2/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_2/src/espnow/rx_heartbeat_manager.cpp`
- `espnowreceiver_2/src/espnow/rx_state_machine.cpp`
- `espnowreceiver_2/src/mqtt/mqtt_task.cpp`


## Executive Summary

The current failure is not a single bug. It is the interaction of four real behaviours:

1. **TX reconnect depends on a tiny discovery ACK reaching the transmitter during a 1 s dwell on the receiver channel.**
2. **RX does see those reconnect probes, but reconnect-critical ACK traffic was historically split across multiple send owners (`esp_now_send()` direct paths plus the shared scheduler).** That meant ACKs still competed for the same WiFi driver TX buffers as MQTT/TCP and other receiver-originated ESP-NOW traffic, without one arbiter owning transmit order.
3. **RX does not enter a true “reconnect quiet mode” as soon as TX starts scanning.** Instead it waits for heartbeat age to grow stale, so the most important reconnect window is already contested.
4. **RX reports multiple different notions of “connected”, and those notions drift apart by design.** That is why logs can show `CONNECTED` while `hb_age` rises and MQTT is already shut down.

The symptom set is therefore internally consistent:

- TX scans because it never receives a usable ACK.
- RX still reports `CONNECTED` because its common timeout is 32 s and its UI/data state machine has separate rules.
- `hb_age` rises because actual heartbeats stopped when TX left normal unicast mode and started channel-hopping.
- `rxcb` rises only slowly because RX now mostly hears occasional PROBEs when TX revisits channel 6.
- ACK send attempts fail with `ESP_ERR_ESPNOW_NO_MEM` because reconnect-critical ACKs still share the same constrained WiFi buffer pool, and mixed send ownership made it harder to guarantee that ACKs preempted stale work.

The root-cause fix is not “increase a queue again”. The final root-cause fix is to combine immediate **probe-detected reconnect quiet mode** with a **single scheduler-owned ACK architecture** so reconnect-critical ACKs no longer bypass the shared send arbiter during contention.


## Canonical Timing Contract From Current Code

### Transmitter discovery / reconnect

- PROBE interval during scan: `200 ms`
- dwell per channel: `1000 ms`
- channels scanned: `1..13`
- full scan duration: about `13 s`
- delay between full scan cycles: `5000 ms`

### Heartbeat / connection monitoring

- TX heartbeat send interval: `10000 ms`
- TX common heartbeat timeout: `35000 ms`
- RX common heartbeat timeout: `32000 ms`
- receiver quiet/MQTT gate threshold in current code: `12000 ms`

### Event processing cadence

- common connection-event processor tick: `100 ms`
- LCD receiver worker poll: `100 ms`


## Timing Question: Can We “Lengthen Reply Time” To Reduce Pressure?

Short answer: **yes, selectively**. But changing heartbeat blindly is not the best first lever.

### First correction

Current transmitter heartbeat in this codebase is not every 1 second.

- TX heartbeat interval is `10000 ms` (10 s).

So a change from 1 s → 2 s does not match current behavior.

### Would increasing heartbeat interval reduce queue pressure?

Potentially a little, but it is a weak lever for this failure.

- Fewer heartbeats means fewer heartbeat ACKs, so some reduction in background control traffic.
- However, reconnect failures here are dominated by **discovery ACK starvation during scan windows**, not by steady-state heartbeat load alone.

Also, increasing heartbeat interval has costs:

- slower liveness detection
- larger stale/timeout windows unless thresholds are also retuned
- longer time spent in ambiguous “connected but unhealthy” states

### Better timing levers than heartbeat interval

1. **Increase reconnect dwell window per channel** (or adaptive dwell on last-known channel)
	- more ACK opportunities while TX is on the receiver channel
	- directly targets missed-discovery-ACK timing risk

2. **Enter reconnect quiet mode immediately on first reconnect PROBE at RX**
	- removes competing traffic before ACK window is missed
	- higher impact than reducing normal heartbeat cadence

3. **Pause/defer non-critical retry traffic during reconnect**
	- config/catalog/LED/request retries should not run in ACK-critical windows

4. **Use state-dependent send budgets**
	- in reconnect states, reserve most TX budget for ACK/control traffic

### Practical recommendation for heartbeat timing

- Keep heartbeat at 10 s initially while implementing reconnect quiet mode + state-driven prioritization.
- Re-evaluate heartbeat interval only after instrumentation confirms whether steady-state control traffic remains a measurable contributor.

If you still want a heartbeat timing experiment, do it safely as a bounded A/B test:

- test 10 s (baseline) vs 15 s (candidate)
- keep timeout ratios consistent (for example, timeout about $3\times$ heartbeat interval)
- compare:
  - discovery ACK success rate
  - reconnect completion time
  - NO_MEM counts by message class
  - false/late disconnect behavior

### Direct answer to your queue-pressure question

Yes, longer intervals can reduce queue pressure in principle. But for this incident, the highest-yield fix is to protect reconnect ACK windows (quiet mode + state-aware priorities), not to primarily slow heartbeat cadence.


## Timing Question: Increase TX Channel-Hop Dwell From 1 s To 2 s?

Yes, it can help ACK capture probability on the *correct* channel, but it is a trade-off.

### What improves

At current probe cadence (`200 ms`):

- 1 s dwell gives about 5 probe opportunities per channel
- 2 s dwell gives about 10 probe opportunities per channel

So if RX is on channel 6 and contention is bursty, 2 s dwell can materially improve the chance that at least one discovery ACK gets through during that visit.

### What gets worse

Full scan duration roughly doubles:

- 13 channels × 1 s = about 13 s full pass
- 13 channels × 2 s = about 26 s full pass

If TX starts far from RX’s channel, the first chance to reach the right channel is slower on average.

So this change can improve *per-visit success* but worsen *time-to-next-visit*.

### Best practical approach

Use **adaptive dwell**, not a global 2 s on all channels:

1. Keep baseline dwell at 1 s for all channels.
2. Spend longer dwell (for example 2–3 s) on:
	- last known good channel
	- optionally adjacent channels
3. If any discovery signal is seen (`msg_probe` or partial ACK activity), temporarily extend dwell on that channel.

This gives most of the ACK-window benefit without doubling worst-case scan time everywhere.

### Recommendation

Increasing dwell to 2 s globally is a valid experiment and may help in your current failure mode, but adaptive dwell is the stronger long-term design.


## Transmitter-First Mitigation Ideas (Receiver Is Non-Critical)

Given the receiver is display-only and the transmitter is the critical battery-emulator controller, mitigation should explicitly protect transmitter control behavior first and treat display sync as best-effort.

### A. Critical-path isolation on transmitter (highest priority)

1. **Hard isolation between battery-control path and receiver link path**
	- No receiver reconnect logic should block, delay, or backpressure battery-emulator control loops.
	- Enforce CPU budget and queue caps for display/telemetry paths.

2. **Transmitter “display link degraded” mode**
	- If receiver link is unstable, transmitter continues full control operation and reduces display-directed traffic aggressively.
	- Keep only minimal liveness beacons; drop non-essential display payloads until stable again.

3. **One-way telemetry preference during stress**
	- Under reconnect stress, transmitter sends only sparse status and does not wait on any display-side workflow.
	- Receiver catches up later.

### B. Reconnect behavior hardening

1. **Adaptive dwell on last known good channel** (selected)
	- Longer dwell on likely channel(s), short dwell elsewhere.

2. **Two-phase scan policy**
	- Phase 1: fast sweep (short dwell all channels).
	- Phase 2: weighted sweep (long dwell last-known and neighbors).

3. **ACK confidence strategy**
	- Accept reconnect after first valid ACK, but optionally request one immediate confirmation frame before re-enabling heavy telemetry.

4. **Discovery queue overrun visibility + protection**
	- Add explicit counters for ISR enqueue failures into discovery queue.
	- Increase discovery queue depth and/or reserve headroom specifically for `msg_ack`.

### C. Receiver-side concessions (safe because receiver is non-critical)

1. **Aggressive quiet mode trigger**
	- First reconnect PROBE while previously connected immediately disables MQTT and non-essential outbound ESP-NOW.

2. **Display-only traffic freeze during reconnect**
	- Suspend config/catalog/LED sync/retry chatter until heartbeat freshness is restored.

3. **Best-effort receive posture**
	- Receiver prioritizes being discoverable/acknowledging over requesting data.

### D. Protocol-level options

1. **Windowed ACK redundancy for discovery only**
	- Receiver sends up to N ACK attempts within one dwell window with small randomized spacing.

2. **Tiny reconnect token packet**
	- Use smallest possible reconnect-confirm packet to minimize buffer use during lock-on.

3. **Stateful reconnect backoff by TX load**
	- If transmitter control load spikes, slow display reconnect aggressiveness automatically.

### E. Operational and architectural “far-out” options

1. **Optional receiver disable switch**
	- Runtime config to disable receiver link entirely while keeping full transmitter function.

2. **Secondary out-of-band display transport**
	- Keep ESP-NOW for control telemetry, but move heavy display sync to Ethernet/Wi-Fi TCP when available.
	- **Status:** already effectively in use (heavy E2/display traffic via MQTT path), so this is not a primary action item.

3. **Dual-device display architecture**
	- A lightweight ESP-NOW listener front-end relays to a richer display process/device; reconnect noise stays away from transmitter-critical paths.

4. **Physical watchdog policy: transmitter never depends on receiver state**
	- Any receiver absence must be a warning/telemetry event only, never a control-state dependency.

### Recommended implementation order

1. Adaptive dwell + two-phase scan
2. Receiver immediate quiet mode on reconnect PROBE
3. Discovery enqueue-failure instrumentation and queue headroom

### Implementation status (completed)

- ✅ Step 1 complete: adaptive dwell + two-phase scan implemented in transmitter discovery path.
- ✅ Step 2 complete: receiver immediate quiet mode on reconnect PROBE implemented on both receiver variants.
- ✅ Step 3 complete: discovery ISR enqueue drop instrumentation added and discovery queue headroom increased.

### Build validation status (completed)

- ✅ Transmitter build succeeded (`ESPnowtransmitter2` target environment).
- ✅ Receiver LCD build succeeded (`waveshare_esp32s3_lcd7_lvgl`).
- ✅ Receiver 2 builds succeeded (`lilygo-t-display-s3_tft` and `lilygo-t-display-s3`).
- ℹ️ Existing framework/config warnings were observed (HTTPD macro redefinition + framework `uartSetPins` warning), but no new implementation-blocking compile errors were introduced by this reconnect pass.

### Follow-up hardening pass (implemented)

- ✅ PROBE handling now gives receiver reconnect quiet mode a chance to activate before the discovery ACK send path runs.
- ✅ Receiver quiet mode now forces the shared ESP-NOW scheduler into control-only mode and purges non-control backlog on entry.
- ✅ Discovery ACK outcome counters were added so reconnect validation can distinguish queue admission pressure from downstream send failures.

### Deep-dive stability pass (implemented)

- ✅ Transmitter guarded-send mismatch recovery is now state-aware: preflight channel mismatch only triggers forced recovery while `CONNECTED`, avoiding reconnect reset churn while already `CONNECTING`.
- ✅ Non-critical transmitter event-log summary sends are now explicitly gated behind `CONNECTED` + peer-channel coherence checks, so telemetry pushes do not perturb reconnect lock-on windows.
- ✅ Discovery ACK path is now scheduler-owned in shared handler code, removing direct `esp_now_send()` ownership from reconnect ACKs.
- ✅ Receiver heartbeat ACK paths on both receiver variants are now scheduler-only as well, so reconnect-critical ACK classes use one send arbiter.
- ✅ Shared control traffic already enqueues at the front of the control queue, so `msg_ack` and `msg_heartbeat_ack` preempt stale queued work instead of sitting behind it.

### Final architectural conclusion (current)

- ✅ Immediate quiet mode was necessary, but it was not sufficient on its own.
- ✅ The deeper remaining failure mode was mixed ownership of `esp_now_send()` for reconnect-critical ACK traffic.
- ✅ The live shared code now routes discovery ACKs and heartbeat ACKs through `EspnowTxScheduler::send(...)`, so one scheduler task owns transmit ordering during reconnect.
- ✅ This aligns the implementation with the intended architecture: reconnect-critical control frames are prioritized centrally instead of racing a direct-send path against the scheduler.

### Retrospective review of previous fixes (root-cause vs mitigation)

This section reclassifies the earlier fixes so the document does not overstate temporary improvements as final root-cause resolution.

- **Adaptive dwell + two-phase scan:** useful reconnect amplification, but not a root fix. It increases the chance of catching a good ACK window; it does not stop ACK starvation.
- **Immediate quiet mode on reconnect PROBE:** necessary isolation behavior, but still not a complete root fix on its own. It reduces competition, but cannot protect against senders that bypass the shared arbiter elsewhere in the codebase.
- **Discovery queue headroom + enqueue counters:** diagnostic and resilience improvements, not root fixes. They expose and reduce one loss mode, but do not solve shared-buffer contention by themselves.
- **Scheduler-owned discovery ACK + heartbeat ACK:** this is part of the real root fix because it removes split ownership for reconnect-critical ACK classes.
- **State-aware transmitter guarded-send changes / telemetry gating:** correct stabilizers, but still secondary. They prevent avoidable reconnect churn; they do not eliminate the base starvation condition alone.

### Root-solution gap that still remains

The codebase review shows the problem is broader than the original ACK handler path alone.

- Some active runtime paths elsewhere in the tree still call raw `esp_now_send()` directly instead of going through one send owner.
- The transmitter discovery mirror path can still drop a valid over-the-air ACK at queue ingress if the discovery queue is full at the wrong moment.
- Receiver LCD and receiver_2 are still not perfectly aligned in how discovery traffic affects generic activity/liveness paths.

Therefore the true root solution is now understood as:

1. immediate reconnect quiet mode,
2. enforced single-owner ESP-NOW transmit arbitration across active runtime paths,
3. deterministic protection of discovery ACK ingress on the transmitter.

### Codebase cleanup note

As part of this implementation pass, legacy/duplicate behavior in the touched reconnect paths was consolidated so the active code paths are now the single source of behavior for:

- scan dwell strategy (two-phase adaptive path)
- reconnect quiet-mode gating in receiver tick traffic
- discovery enqueue visibility (explicit counters instead of silent drop behavior)

Additional broad repo-wide legacy cleanup can be done in a separate dedicated pass, but the implemented reconnect paths now use the new behavior directly (not feature-flagged legacy fallbacks).


## Document Consistency Review (Post-Update)

This section reconciles all recommendations after the latest decisions.

### Agreed execution plan (authoritative)

The active implementation plan was:

1. Adaptive dwell + two-phase scan
2. Receiver immediate quiet mode on reconnect PROBE
3. Discovery enqueue-failure instrumentation and queue headroom

Status: the original three steps are implemented, and an additional architectural stabilization pass is also implemented. However, those steps should now be understood as a partial path to the root solution, not the full end state.

Everything else in this document remains supporting analysis, optional enhancement, or later-phase hardening.

### Conflict checks completed

1. **Heartbeat timing vs reconnect timing**
	- No conflict: heartbeat interval changes are now treated as optional experiments, not primary fixes.

2. **Quiet mode priority vs adaptive dwell priority**
	- No conflict: both are still recommended; execution order is now explicitly the agreed 3-step sequence above.

3. **MQTT/off-band transport suggestion vs current system reality**
	- No conflict: document now states heavy display/E2 traffic is already offloaded via MQTT in practice, so that suggestion is not a primary action.

4. **Far-out options vs core plan**
	- No conflict: far-out options remain explicitly non-primary and not part of the active 3-step implementation plan.

### Items treated as optional (not required for current fix pass)

- global heartbeat interval changes
- protocol-level redundancy refinements
- additional architectural split/device options

### Success criteria for the agreed 3-step pass

- TX reconnect success rate materially improves without impacting transmitter control loops
- discovery ACK capture rate increases on reconnect windows
- measured discovery-queue enqueue drops are visible and trend down after headroom changes
- receiver enters quiet mode immediately on reconnect PROBE and suppresses non-critical traffic during scan windows
- reconnect-critical ACKs no longer use mixed direct-send and scheduler-send ownership

### Implemented files (for traceability)

- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/main.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_send_guard.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_routes.cpp`
- `esp32common/espnow_transmitter/espnow_transmitter.h`
- `esp32common/espnow_transmitter/espnow_transmitter.cpp`
- `esp32common/espnow_common_utils/espnow_standard_handlers.cpp`
- `esp32common/espnow_common_utils/espnow_tx_scheduler.cpp`
- `espnowreceiver_LCD/src/espnow/rx_connection_handler.h`
- `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp`
- `espnowreceiver_2/src/espnow/rx_connection_handler.h`
- `espnowreceiver_2/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_2/src/espnow/rx_heartbeat_manager.cpp`


## Clarification: “Separate Channels” vs What The Code Actually Separates

You are right that we previously discussed separation for message classes. The current implementation does include separation, but mostly as **software queues/priorities**, not as **different RF channels**.

### 1) What is separated today (logical/software path)

#### A. Transmitter ingress queues are split

Transmitter queue manager defines:

- `message_queue` (general app messages)
- `discovery_queue` (PROBE/ACK path for discovery task)
- `rx_queue` (raw ISR-fed receive queue)

This means discovery traffic is logically separated from general RX processing.

#### B. Shared TX scheduler has 4 priority classes

The scheduler classifies outgoing messages into:

- `CONTROL`
- `DISCOVERY`
- `DATA`
- `MONITORING`

This gives queue and cadence isolation between message types at the scheduler level.

### 2) What is NOT separated today (radio/PHY channel path)

There is no persistent “message type A uses RF channel X, message type B uses RF channel Y” design in connected operation.

- During reconnect, TX **hops all RF channels** and sends PROBE on whichever channel it is currently tuned to.
- During normal connected operation, ESP-NOW frames use the current/locked WiFi channel for the peer path.

So message classes do not have dedicated RF channels; they share the same radio buffer pool while tuned to the active channel.

### 3) Why this matters for ACK misses

Because separation is mostly logical (queues/priorities) and not physical (distinct RF resources), ACK frames still contend for the same WiFi driver TX allocation pool.

That is why ACK misses still happen even with separate discovery queues and priority classes.

### 4) Additional current limitation

In the transmitter receive ISR, forwarding to `discovery_queue` is non-blocking (`xQueueSend(..., 0)`) with no return handling. If that queue is full at the wrong moment, a valid on-air ACK can still be dropped before the discovery task sees it.

This does not negate the queue separation work; it explains why queue separation alone does not fully eliminate ACK miss risk.


## Feasibility: Physical RF Separation vs Software Scheduling Separation

This section answers the architecture question directly.

### Can we physically use different radio channels for different message classes?

In this platform, not in the way that would solve this problem.

- ESP32 has one 2.4 GHz radio path for WiFi/ESP-NOW traffic.
- The device is tuned to one active channel at a time per operating context.
- Reconnect already uses channel hopping, but that is time-multiplexing, not simultaneous multi-channel operation.

So:

- **True simultaneous per-message RF channel separation is not available with the current single-radio hardware design.**
- Time-sliced channel switching per message class is technically possible, but it would increase complexity and likely hurt reconnect latency/stability.

If true physical isolation is required, it generally needs different hardware topology (for example, a second radio path/device), not just firmware queue changes.

### If we stay with software priorities, can we split into separate tasks by message class?

Yes, but with an important constraint:

- Multiple producer tasks are fine.
- `esp_now_send()` should still be owned by a **single dispatch/arbiter task**.

Reason: if multiple tasks call `esp_now_send()` concurrently, they still collide on the same driver buffer pool and can create burstier contention.

Recommended model:

1. Class-specific producer tasks/queues:
	- `CONTROL` producer
	- `DISCOVERY` producer
	- `DATA` producer
	- `MONITORING` producer
2. One send arbiter task:
	- the only task that calls `esp_now_send()`
	- enforces per-class budgets and dynamic priority rules
	- applies backpressure/drop policy to lower classes first

This keeps observability and deterministic ordering while preserving isolation benefits.

### Can priorities change dynamically by connection state machine stage?

Yes, and this is strongly recommended.

Use state-dependent scheduling profiles keyed by connection state:

- `CONNECTED_STEADY`: normal weighted fairness (`CONTROL` > `DISCOVERY` > `DATA` > `MONITORING`)
- `RECONNECT_DETECTED` (first PROBE seen while previously connected):
  - hard quiet mode
  - allow only discovery ACK + heartbeat ACK + minimal control
  - pause/drop new `DATA`/`MONITORING`
- `RECONNECT_ACTIVE_SCAN`: keep strict ACK-first policy until heartbeat freshness and peer lock are restored
- `POST_RECONNECT_SETTLE`: controlled ramp-up (small budget for config/data retries, then return to steady profile)

This directly addresses the observed failure mode by shifting scarce TX buffers to reconnect-critical traffic at the exact time it matters.

### Practical conclusion

- Physical per-class RF channels: effectively not viable in the current hardware architecture.
- Software separation with dynamic state-driven priorities: viable, aligns with current code structure, and is the most realistic path to robust reconnect behavior.


## End-to-End Architecture Re-Derivation

## 1. Transmitter reconnect flow

### Normal path

1. TX common connection manager transitions to `CONNECTED`.
2. TX heartbeat manager sends `msg_heartbeat` every 10 s.
3. Receiver sends `msg_heartbeat_ack` back.
4. TX `HeartbeatManager::on_heartbeat_ack()` updates the common heartbeat monitor.

### Loss path

1. TX stops getting valid heartbeat ACKs.
2. Common connection manager or send guard eventually posts `CONNECTION_LOST`.
3. TX transitions back through `IDLE` to `CONNECTING`.
4. `TransmitterConnectionHandler` starts `DiscoveryTask::start_active_channel_hopping()`.
5. TX scans channels in 1 s dwell windows and sends PROBE every 200 ms.
6. TX listens only for reconnect evidence from the discovery queue.
7. On ACK receipt, TX:
	 - records receiver MAC/channel
	 - sets WiFi to the ACK-reported channel
	 - registers the peer
	 - posts `PEER_REGISTERED`
	 - re-enters `CONNECTED`

### Important detail confirmed in live code

The transmitter receive ISR in `esp32common/espnow_transmitter/espnow_transmitter.cpp` mirrors both `msg_probe` and `msg_ack` into the discovery queue. So the discovery queue plumbing itself is present and active.


## 2. Receiver reconnect-related flow

### Receiver common connection establishment

1. RX boots and posts `CONNECTION_START`.
2. On incoming TX PROBE, receiver posts `PEER_FOUND`.
3. On first non-discovery peer registration path, receiver posts `PEER_REGISTERED`.
4. Shared connection manager transitions RX to `CONNECTED`.

### Receiver message classification

Both receivers now intentionally treat discovery traffic specially:

- `msg_probe` / `msg_ack` do **not** promote generic data/activity paths anymore.
- non-discovery traffic can still trigger peer registration and runtime activity.

That earlier fix was correct and should remain.

### Receiver heartbeat ownership

Actual heartbeat freshness is owned by `EspNowConnectionManager::on_heartbeat_received()`.

That freshness is updated by:

- actual heartbeat frames on both receivers
- some keepalive/status frames on `espnowreceiver_2`
- non-discovery data paths that explicitly call `on_data_received()`

It is **not** updated by mere receipt of PROBE / ACK traffic.

This is also correct, because discovery traffic must not masquerade as a healthy data link.


## 3. Receiver MQTT / WiFi coexistence logic

Both receivers currently gate MQTT on:

- RX UI/data state being `CONNECTED` or `ACTIVE`
- `ms_since_last_heartbeat() < 12000`

When that gate closes, MQTT disconnects and stops further broker work.

The LCD receiver also explicitly documents that its ESP-NOW TX scheduler sizing and retry policy were tuned for “MQTT + ESP-NOW coexistence”. That is valuable context, but it also confirms the design assumption: coexistence is currently managed as a tuning problem, not as a hard priority/isolation problem.


## Why The Latest Logs Make Sense

## Observed symptom A: TX keeps scanning and never finds receiver

This means TX is not receiving a successful reconnect ACK while dwelling on the receiver’s real channel.

Current code supports that conclusion directly:

- TX only exits the scan on ACK receipt.
- the active scan revisits channel 6 only for a 1 s dwell inside a 13 s full pass.
- if RX misses even one or two critical ACK windows, reconnect stretches dramatically.


## Observed symptom B: RX still reports `CONNECTED ch=6`

This is **not** proof that the link is healthy.

It happens because the receiver has at least three different notions of “link state”:

1. **Common connection manager state**
	 - remains `CONNECTED` until the full 32 s heartbeat/activity timeout expires.

2. **RxStateMachine UI/data state**
	 - `CONNECTED` means link established but no recent data sample.
	 - `ACTIVE` means real data is flowing.
	 - `STALE` only occurs after active data stops for the stale timeout.

3. **MQTT coexistence gate**
	 - closes much earlier at heartbeat age `>= 12000 ms`.

So the logs showing `CONNECTED` while `hb_age` rises above 12 s are fully consistent with the present design.


## Observed symptom C: `rxcb` still increases slowly

This also fits the code.

When TX is scanning, RX is no longer getting regular unicast heartbeats. But it will still hear occasional reconnect PROBEs when TX revisits the receiver’s home channel.

That produces exactly the pattern seen in the logs:

- callback count is not flat zero
- callback count increases only slowly
- heartbeat age still rises

That means RX is seeing **discovery traffic only**, not a restored connected data path.


## Observed symptom D: ACK send fails with `ESP_ERR_ESPNOW_NO_MEM`

This is the critical failure.

The failing message is `type=1 len=6`, which matches the standard discovery ACK.

The earlier failing code path had two ACK send owners:

1. a direct `esp_now_send()` path in `EspnowStandardHandlers::send_ack_response()`
2. a fallback into the shared `EspnowTxScheduler`

The well-known log line:

`Send failed (type=1 len=6): ESP_ERR_ESPNOW_NO_MEM`

comes from the **shared scheduler worker**, which means the WiFi driver buffer pool was already under pressure even after the ACK path escalated into the queued sender.

That is not just “the queue was full”. That is **driver-level WiFi TX buffer starvation** at the moment the reconnect ACK was needed.

The current shared code has now been corrected so discovery ACKs are scheduler-owned from the start. That does not magically create more WiFi buffers, but it does remove the split-owner race and makes ACK prioritization consistent with the rest of the reconnect strategy.


## Direct Answer: Why TX Stops Receiving Valid ACKs In The First Place

The transmitter does not stop receiving valid ACKs because of one parser bug. It stops because the ACK supply path is broken earlier, at send time on the receiver side, and then amplified by reconnect timing.

### Step 1 (first break): heartbeat ACK starvation starts the failure

Before TX enters channel-hopping, it depends on `msg_heartbeat_ack` to keep its liveness monitor fresh.

Receiver heartbeat ACKs share the same constrained WiFi TX buffer pool.
When receiver WiFi TX buffers are under pressure, those heartbeat ACKs can be delayed or dropped (`ESP_ERR_ESPNOW_NO_MEM`).

Once TX misses enough heartbeat ACK freshness, TX concludes link health is bad and starts reconnect scanning.

So the **first ACK loss** that matters is often heartbeat ACK loss, not discovery ACK loss.

### Step 2: discovery ACKs then fail in the same resource pool

During reconnect scan, RX receives PROBEs and attempts discovery ACKs (`type=1 len=6`).
Those ACKs still share the same driver allocation pool as MQTT/TCP and receiver-originated ESP-NOW chatter.

The important architectural correction is that these ACKs now go through the same scheduler owner as other reconnect-critical control traffic, instead of mixing direct-send and queued-send paths.

If allocation fails at that moment, ACK send fails with `ESP_ERR_ESPNOW_NO_MEM`, so TX never receives a valid reconnect ACK for that dwell window.

### Step 3 (amplifier): narrow dwell windows magnify each miss

TX listens on channel 6 for only about 1 second each full pass.
Missing ACK in that one window usually means waiting another ~13 seconds for the next chance.

So a small burst of NO_MEM at the wrong moment produces prolonged “TX sees no valid ACK” behaviour even if RX is intermittently alive.

### Additional TX-side drop point to acknowledge

TX ISR mirrors incoming PROBE/ACK frames to the discovery queue using non-blocking `xQueueSend(..., 0)` with no return-value handling.
If that queue is temporarily full, an otherwise valid over-the-air ACK can still be dropped before the scan loop sees it.

This is a secondary contributor, but it can further reduce observed ACK reception under bursty conditions.

### Bottom line

TX is “missing valid ACKs” primarily because many ACKs are never successfully transmitted by RX under buffer contention, and secondarily because any successfully received ACK still must survive TX discovery-queue ingestion during a narrow timing window.


## Root Causes

## Root cause 1: No explicit probe-detected reconnect quiet mode on the receiver

This is the biggest architectural gap.

Today, the receiver does **not** treat an incoming PROBE while nominally connected as a first-class reconnect event.

That matters because a reconnect PROBE is the strongest possible evidence that:

- TX is no longer in normal connected unicast mode
- TX needs a discovery ACK immediately
- every non-essential receiver-originated WiFi/ESP-NOW transmission is now competition

Current behaviour is softer than that:

- MQTT is only shut down once heartbeat age reaches 12 s
- tick-driven config/catalog/LED/request traffic is only suppressed once the same stale-heartbeat threshold is crossed
- there is no dedicated “TX is scanning me right now” mode

So the receiver often enters the most important reconnect window still carrying the residue of normal connected-mode behaviour.


## Root cause 2: Discovery ACK still shares the same WiFi driver buffer pool as all other receiver egress

Even after the recent throttling and retry changes, the ACK still ultimately depends on the same underlying WiFi driver allocation path as:

- MQTT TCP traffic
- queued ESP-NOW config/request/catalog traffic
- heartbeat ACKs
- any other receiver-originated WiFi socket activity

That means reconnect-critical discovery traffic is not isolated. It is merely retried.

Retries help with short bursts.
They do not solve sustained buffer starvation.


## Root cause 3: The current 12 s stale-heartbeat coexistence gate is too late for reconnect-critical behaviour

The 12 s gate was introduced to reduce false contention and to avoid disconnecting MQTT on brief heartbeat jitter.

But the reconnect timeline is tighter than that:

- TX heartbeat period is 10 s
- TX can begin scanning before RX reaches its 12 s coexistence cutoff
- each missed ACK on channel 6 costs roughly another 13 s full scan cycle

So even if the receiver eventually quiets down correctly, the first few discovery windows may already have been lost.


## Root cause 4: Receiver state reporting hides the real reconnect phase

The logs look contradictory because the code exports state from different layers with different semantics:

- common connection manager says the session is still connected until 32 s timeout
- RxStateMachine reports UI/data state, not discovery health
- MQTT gate uses heartbeat freshness only

This is not the direct cause of the failed reconnect, but it is a major diagnosis problem and it made the system appear more mysterious than it really is.


## Root cause 5: Queue-size and retry tuning are secondary, not primary

Both receivers already request larger WiFi / ESPNOW defaults in `sdkconfig.defaults`, and the scheduler / ACK retry windows have already been widened.

Yet the live behaviour persists.

That means the remaining issue is not “one more queue depth bump away” from disappearing. The problem is architectural priority and quiescing behaviour at reconnect time.


## What Is Most Likely Happening In The Field

The current code and the latest logs together imply this sequence:

1. TX was previously connected to RX on channel 6.
2. Something causes TX to stop receiving valid ACK-based liveness and enter reconnect scanning.
3. RX remains in a nominal connected state because its full timeout is 32 s.
4. RX heartbeat age begins rising because no true heartbeats are arriving anymore.
5. TX revisits channel 6 during scan and sends reconnect PROBEs.
6. RX hears those PROBEs, which is why callback counters still move.
7. RX attempts to send discovery ACKs back.
8. Some or all of those ACKs fail with `ESP_ERR_ESPNOW_NO_MEM` because the buffer pool is still contested.
9. TX therefore misses the only signal that ends the scan.
10. TX leaves channel 6 and continues the rest of the 13-channel pass.
11. MQTT eventually shuts down after heartbeat freshness crosses 12 s, but by then one or more critical reconnect windows may already have been lost.
12. RX still appears “connected” in some views because the full common timeout has not yet expired.

This sequence explains every reported symptom without contradiction.


## WiFi Users On The Receiver That Matter Here

### 1. MQTT client

This is the most important competing user because it creates outbound TCP socket traffic and holds WiFi driver TX resources during connection attempts and normal operation.

Both receivers already contain explicit logic acknowledging this risk.

### 2. Receiver-originated ESP-NOW control/data traffic

This includes:

- discovery ACKs
- heartbeat ACKs
- `REQUEST_DATA`
- config-section requests
- catalog requests
- LED sync requests
- version announcement

These all share the same ESP-NOW driver allocation path.

### 3. Web server / UI traffic

The receiver projects also maintain web/UI-facing state managers and webserver utilities. Those are not as constantly aggressive as MQTT, but they still represent additional WiFi users when remote clients are attached.

For this failure, MQTT and receiver-originated ESP-NOW traffic are the dominant suspects. Web traffic is a secondary amplifier, not the primary explanation.


## Recommended Fix Plan

## P0 - Add explicit receiver reconnect quiet mode triggered by incoming PROBE while connected

This is the highest-value change.

### New behaviour

If RX receives `msg_probe` while any previous session is still considered connected or heartbeat age is already stale, immediately enter a dedicated reconnect-quiet state.

That mode should:

- disconnect MQTT immediately
- suppress all `ReceiverConnectionHandler::tick()` sends
- suppress config/catalog/LED/request retries
- optionally flush lower-priority scheduler queues
- allow only:
	- discovery ACK
	- heartbeat ACK
	- absolutely essential control traffic

### Why this is the correct trigger

An inbound PROBE during a formerly connected session is direct evidence that TX is currently scanning. That is a better reconnect trigger than waiting for heartbeat age to wander up to 12 s.


## P0 - Make reconnect quiet mode visible in logs and state

Add a receiver-visible state such as:

- `RECONNECT_QUIET`
- `DISCOVERY_RECOVERY`
- or `TX_SCAN_DETECTED`

This state should be what the status logs expose during this window instead of a plain `CONNECTED` label.


## P1 - Tighten ACK priority further by isolating it from lower-priority sender activity

Recommended implementation direction:

- keep reconnect-critical ACK traffic on a single scheduler-owned send path
- in reconnect quiet mode, block new non-critical scheduler enqueues
- optionally purge data/monitoring queues on quiet-mode entry
- leave only CONTROL-class sends active
- remove remaining active runtime direct `esp_now_send()` paths that bypass scheduler ownership

The key point is not “retry harder”; it is “remove competitors before retrying”.


## P1 - Move MQTT shutdown earlier than the current 12 s stale threshold when reconnect is positively detected

Keep the 12 s guard as a passive fallback if needed, but do not rely on it once a reconnect PROBE has been observed.

The first reconnect PROBE should close MQTT immediately.


## P1 - Add instrumentation for the next validation pass

At minimum, add counters/logs for:

- discovery ACK enqueue attempts
- discovery ACK enqueue failures by reason
- heartbeat ACK enqueue failures by reason
- scheduler send failures by message class
- scheduler CONTROL queue depth on first `NO_MEM`
- transmitter discovery-queue mirror drops
- quiet-mode entry / exit timestamps
- first PROBE seen while heartbeat stale

Without these counters the next round of logs will still be more ambiguous than necessary.


## P2 - Unify liveness semantics in receiver status reporting

Do not collapse all of these into one “connected” label:

- peer registered
- heartbeat fresh
- active data flow
- reconnect scan detected

Expose them separately.

That will prevent future confusion where the UI says `CONNECTED` while the reconnect protection logic already knows the link is unhealthy.


## P2 - Validate effective WiFi / ESPNOW runtime config, but treat this as secondary

It is still worth proving the effective runtime values for:

- ESPNOW pending length
- dynamic TX buffer count

But based on the current evidence, even correct enlarged defaults will not be sufficient without the reconnect-quiet architectural change above.


## Concrete Implementation Shape Recommended Next

### Receiver-side state additions

- add `scan_detected_` / `quiet_mode_` flag in receiver connection handler or dedicated coexistence controller
- set it immediately on inbound PROBE when:
	- current session was previously connected, or
	- heartbeat age exceeds one heartbeat period, or
	- connection manager is not cleanly in first-boot CONNECTING

### Receiver-side suppression rules

- on quiet-mode entry:
	- `MqttClient::disconnect()` immediately
	- block `REQUEST_DATA`, config, catalog, LED, metadata/version chatter
	- optionally clear non-control scheduler queues
- on quiet-mode exit:
	- require fresh heartbeat or fully restored `CONNECTED` state
	- then allow paced re-initialisation again

### Receiver-side ACK policy

- discovery ACK remains allowed during quiet mode
- heartbeat ACK remains allowed during quiet mode
- all other ESP-NOW sends require quiet mode to be clear


## Why I Do Not Recommend Another “Just Increase The Queue” Pass

That would treat the symptom, not the system behaviour.

Current evidence already shows:

- MQTT gating exists
- ACK throttling exists
- scheduler-owned ACK routing exists
- scheduler retries exist
- larger desired WiFi defaults exist in `sdkconfig.defaults`

And the wider codebase review shows there are still additional runtime send paths outside the main reconnect ACK handlers.

Yet the reconnect-critical ACK still fails in practice.

That means the problem is not a missing retry knob. The remaining problem is architectural: reconnect-critical traffic still does not own the entire transmit surface early enough and strictly enough.


## Final Conclusion

The present reconnect failure is best understood as an **ESP-NOW discovery ACK starvation problem caused by shared WiFi TX contention, narrow reconnect timing windows, and incomplete send-owner enforcement across the codebase**. Insufficiently early receiver quiescing was a major contributor, but it is no longer sufficient as the sole explanation.

The key facts are:

- TX reconnect succeeds only if RX can return a discovery ACK during a narrow 1 s dwell on the right channel.
- RX does hear those reconnect PROBEs.
- RX still fails some or all discovery ACK sends with `ESP_ERR_ESPNOW_NO_MEM`.
- earlier versions of RX waited too long to enter a fully quiet reconnect posture.
- even after quiet-mode improvements, remaining direct-send paths and discovery-ingress drop points can still break reconnect determinism.
- RX status reporting masks this by continuing to show connected-like states long after heartbeat freshness has already gone stale.

### Most important fix

**Do not treat quiet mode alone as the finish line. The true root fix is: immediate reconnect quiet mode + single-owner transmit arbitration across active runtime send paths + hardened transmitter discovery-queue ingress for ACK capture.**

That combination is the change set most likely to convert the current intermittent/failed reconnect loop into a reliable reconnect path.


## Recommended Next Action

Collect one fresh paired TX/RX log set with this implemented build and evaluate against the success criteria in the consistency review section.

Primary validation points for this run:

1. reconnect completion time and success rate (before vs after)
2. discovery enqueue attempts vs drops (new counters)
3. quiet-mode enter/exit behavior around reconnect PROBEs
4. confirmation that transmitter control loops remain unaffected while receiver link recovers


## Necessary Implementation Plan

This is the concrete execution plan derived from the root-cause review above. It is ordered so architectural fixes land before any further tuning work.

### Phase 0 - Lock the architecture before tuning

Goal: ensure reconnect-critical traffic has one send owner and one protected ingress path.

#### Work package 0.1 - Enforce single send-owner architecture

Replace remaining active runtime direct `esp_now_send()` usage with shared send-owner paths.

Files to update first:

- `espnowreceiver_LCD/lib/webserver_lcd/api/api_network_handlers.cpp`
- `espnowreceiver_LCD/lib/webserver_lcd/api/api_settings_handlers.cpp`
- `espnowreceiver_LCD/lib/webserver_lcd/api/api_sse_handlers.cpp`
- `espnowreceiver_LCD/lib/webserver_lcd/api/api_control_handlers.cpp`
- `espnowreceiver_2/lib/webserver/api/api_network_handlers.cpp`
- `espnowreceiver_2/lib/webserver/api/api_settings_handlers.cpp`
- `espnowreceiver_2/lib/webserver/api/api_sse_handlers.cpp`
- `espnowreceiver_2/lib/webserver/api/api_control_handlers.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_espnow.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/data_cache.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/heartbeat_manager.cpp` (optional/best-effort telemetry path review)

Required outcome:

- reconnect-relevant runtime sends no longer bypass scheduler/guard ownership,
- control-only mode can actually suppress non-critical traffic during reconnect,
- send ordering is no longer dependent on which module happened to call `esp_now_send()` directly.

##### Phase 0.1 progress update - 2026-04-28

- ✅ Receiver API handler sends migrated to scheduler-owned path in both receiver variants:
	- `espnowreceiver_LCD/lib/webserver_lcd/api/api_network_handlers.cpp`
	- `espnowreceiver_LCD/lib/webserver_lcd/api/api_settings_handlers.cpp`
	- `espnowreceiver_LCD/lib/webserver_lcd/api/api_control_handlers.cpp`
	- `espnowreceiver_2/lib/webserver/api/api_network_handlers.cpp`
	- `espnowreceiver_2/lib/webserver/api/api_settings_handlers.cpp`
	- `espnowreceiver_2/lib/webserver/api/api_control_handlers.cpp`
- ✅ Transmitter settings/cache/temperature runtime sends migrated to scheduler-owned path:
	- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_espnow.cpp`
	- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/data_cache.cpp`
	- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/heartbeat_manager.cpp`
- ✅ Build validation:
	- transmitter build succeeded
	- LCD receiver build succeeded
	- receiver_2 build remains blocked by pre-existing Windows file lock under `.pio/build/.../Preferences.cpp.o` (environmental)

#### Work package 0.2 - Harden transmitter discovery ACK ingress

Protect the path between RF callback receipt and discovery task consumption.

Primary file:

- `esp32common/espnow_transmitter/espnow_transmitter.cpp`

Supporting file:

- `ESPnowtransmitter2/espnowtransmitter2/src/queue/espnow_queue_manager.cpp`

Implementation direction:

- replace blind non-blocking mirror behavior with deterministic discovery handling,
- preserve discovery ACK visibility under burst load,
- add explicit counters for attempts, drops, and recovery behavior.

Required outcome:

- a valid on-air ACK should not be silently lost at discovery-queue ingress under normal reconnect load,
- if loss still occurs, the reason is observable immediately in logs/counters.

##### Phase 0.2 progress update - 2026-04-28

- ✅ `esp32common/espnow_transmitter/espnow_transmitter.cpp` RX callback hardened:
	- clamps copied payload length to queue buffer size before enqueue,
	- adds deterministic ACK-ingress recovery on discovery-queue-full (drop oldest + single retry for `msg_ack`),
	- adds rate-limited queue-full diagnostics including attempts/drops/recovered counters in logs.
- ✅ Discovery ingress recovery counters are now surfaced via shared transmitter API:
	- `esp32common/espnow_transmitter/espnow_transmitter.h`
	- `esp32common/espnow_transmitter/espnow_transmitter.cpp`
- ✅ Supporting queue diagnostics now report discovery ingress attempts/drops/recovered rates:
	- `ESPnowtransmitter2/espnowtransmitter2/src/queue/espnow_queue_manager.cpp`

Validation status for this step:

- transmitter build succeeded
- LCD receiver build succeeded
- receiver_2 build remains blocked by pre-existing Windows file lock under `.pio/build/.../Preferences.cpp.o` (environmental)

#### Work package 0.3 - Align receiver discovery semantics

Make both receivers treat discovery traffic identically for link-activity purposes.

Primary file:

- `espnowreceiver_2/src/espnow/espnow_tasks.cpp`

Reference behavior:

- `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp`

Required outcome:

- `msg_probe` / `msg_ack` do not feed generic activity freshness on either receiver,
- reconnect behavior no longer differs by receiver variant.

##### Phase 0.3 progress update - 2026-04-28

- ✅ Receiver discovery semantics are now aligned between variants:
	- `espnowreceiver_2/src/espnow/espnow_tasks.cpp` no longer calls generic link-activity handling for discovery traffic (`msg_probe` / `msg_ack`), matching LCD runtime policy.

Validation status for this step:

- transmitter build succeeded
- LCD receiver build succeeded
- receiver_2 build remains blocked by pre-existing Windows file lock under `.pio/build/.../Preferences.cpp.o` (environmental)

#### Work package 0.4 - Finish quiet-mode enforcement

Ensure quiet mode is not just entered early, but also blocks side-channel senders during reconnect.

Primary files:

- `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_2/src/espnow/rx_connection_handler.cpp`
- receiver API send entry points listed in work package 0.1

Required outcome:

- quiet mode blocks non-critical API/UI-initiated sends,
- only discovery ACK, heartbeat ACK, and explicitly essential control traffic are allowed while reconnect is active.

##### Phase 0.4 progress update - 2026-04-28

- ✅ Quiet-mode API/UI send blocking implemented in both receiver web API stacks:
	- `espnowreceiver_LCD/lib/webserver_lcd/api/api_network_handlers.cpp`
	- `espnowreceiver_LCD/lib/webserver_lcd/api/api_settings_handlers.cpp`
	- `espnowreceiver_LCD/lib/webserver_lcd/api/api_sse_handlers.cpp`
	- `espnowreceiver_LCD/lib/webserver_lcd/api/api_control_handlers.cpp`
	- `espnowreceiver_2/lib/webserver/api/api_network_handlers.cpp`
	- `espnowreceiver_2/lib/webserver/api/api_settings_handlers.cpp`
	- `espnowreceiver_2/lib/webserver/api/api_sse_handlers.cpp`
	- `espnowreceiver_2/lib/webserver/api/api_control_handlers.cpp`
- ✅ SSE monitor-side REQUEST_DATA / ABORT_DATA sends now use scheduler-owned send path and are skipped during quiet mode.
- ✅ Save/update API entry points now reject non-critical ESP-NOW sends while quiet mode is active with explicit operator-facing errors.
- ✅ Reboot endpoint now explicitly blocks command sends during quiet mode.
- ✅ Build validation:
	- LCD receiver build succeeded
	- receiver_2 build still blocked by existing Windows file lock under `.pio/build/.../Preferences.cpp.o` (environmental)

### Phase 1 - Add proof-quality instrumentation

Goal: prove whether the architecture is now correct without relying on ambiguous logs.

#### Work package 1.1 - Per-attempt reconnect diagnostics

Add one concise reconnect report per reconnect attempt containing:

- reconnect start timestamp
- first PROBE seen timestamp
- quiet-mode enter/exit timestamps
- discovery ACK enqueue attempts/failures
- heartbeat ACK enqueue attempts/failures
- scheduler send failures by message class
- transmitter discovery-queue mirror drops
- reconnect duration and final outcome

Primary files:

- `esp32common/espnow_common_utils/espnow_standard_handlers.cpp`
- `esp32common/espnow_common_utils/espnow_tx_scheduler.cpp`
- `esp32common/espnow_transmitter/espnow_transmitter.cpp`
- receiver connection handlers

Required outcome:

- next failure, if any, is diagnosable from one paired TX/RX run without another large archaeology pass.

##### Phase 1.1 progress update - 2026-04-28

- ✅ Shared receiver reconnect-attempt reporting is now implemented in:
	- `esp32common/espnow_common_utils/rx_connection_handler.cpp`
	- Reports one concise `RX_RECONNECT_DIAG` pair per attempt with:
		- reconnect start / first PROBE / quiet enter / quiet exit timestamps
		- attempt duration and final outcome (`recovered` / `connection_lost`)
		- discovery ACK enqueue attempts/failures (from shared standard handler stats)
		- heartbeat ACK enqueue attempts/failures (from shared heartbeat manager stats)
		- scheduler send failures by class (control/discovery/data/monitoring)
- ✅ Heartbeat ACK enqueue instrumentation added in shared heartbeat manager:
	- `esp32common/espnow_common_utils/rx_heartbeat_manager.h`
	- `esp32common/espnow_common_utils/rx_heartbeat_manager.cpp`
- ✅ Shared TX scheduler instrumentation now includes send-failure class breakdown and queue-depth snapshot API:
	- `esp32common/espnow_common_utils/espnow_tx_scheduler.h`
	- `esp32common/espnow_common_utils/espnow_tx_scheduler.cpp`
- ✅ Transmitter reconnect-attempt reporting is now implemented in:
	- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_connection_handler.h`
	- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_connection_handler.cpp`
	- Reports one concise `TX_RECONNECT_DIAG` pair per attempt with:
		- reconnect start / first ACK / end timestamp
		- attempt duration and final outcome (`connected` / `connecting_timeout`)
		- discovery ingress mirror attempts/drops/recovered deltas
		- scheduler send failures by class and queue-depth snapshot

Validation status for this step:

- transmitter build succeeded
- LCD receiver build succeeded
- receiver_2 build remains blocked by pre-existing Windows file lock under `.pio/build/.../Preferences.cpp.o` (environmental)

### Phase 2 - Only then tune reconnect timing

Goal: optimize reacquire speed after the architecture is correct.

#### Work package 2.1 - Re-evaluate scan dwell after P0/P1 only

Primary file:

- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp`

Rules:

- keep two-phase scan,
- keep adaptive/weighted dwell,
- do not apply broad dwell inflation until architectural fixes are verified.

Required outcome:

- timing changes are measured refinements, not substitutes for contention isolation.

### Phase 3 - Remove recurrence risk

Goal: stop the codebase from drifting back into split ownership.

#### Work package 3.1 - Guardrails and cleanup

Implementation direction:

- establish approved send-owner modules,
- remove or quarantine legacy utility paths that encourage raw `esp_now_send()` usage,
- document that new runtime send paths must go through the shared arbiter.

Candidate files:

- `esp32common/espnow_common_utils/espnow_send_utils.cpp`
- `esp32common/espnow_common_utils/espnow_discovery.cpp`
- related headers/docs that still imply direct-send usage is normal

Required outcome:

- future reconnect regressions are less likely because the allowed architecture is explicit.

### Phase 4 - Receiver ESP-NOW codebase unification

Goal: make `espnowreceiver_2` and `espnowreceiver_LCD` use the same ESP-NOW communications implementation, with only HAL/device integration differences left outside the shared layer.

#### Work package 4.1 - Extract shared receiver ESP-NOW transport/runtime

Move the common receiver-side ESP-NOW communications logic into shared code under `esp32common`.

Primary candidates for consolidation:

- `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_runtime_messages.cpp`
- `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp`
- `espnowreceiver_LCD/src/espnow/rx_state_machine.cpp`
- `espnowreceiver_2/src/espnow/espnow_tasks.cpp`
- `espnowreceiver_2/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_2/src/espnow/rx_heartbeat_manager.cpp`
- `espnowreceiver_2/src/espnow/rx_state_machine.cpp`

Expected shared responsibilities:

- message ingress and routing
- reconnect quiet-mode behavior
- heartbeat ACK handling
- peer registration and discovery semantics
- MQTT coexistence gating hooks
- reconnect diagnostics and counters
- scheduler/send-owner policy enforcement

Allowed receiver-specific responsibilities after extraction:

- board/HAL integration
- display/update plumbing
- project-specific web/UI hook wiring
- project-specific queue/task bootstrap glue only where hardware or HAL requires it

Required outcome:

- both receivers consume the same shared ESP-NOW communications core,
- bug fixes in reconnect logic land once,
- receiver behavior drift is no longer possible except through explicit HAL-layer differences.

#### Work package 4.2 - Define allowed deviation boundary

Document and enforce the rule that only HAL-facing modules may differ between the two receiver projects.

Allowed deviations:

- board pin mappings
- display hardware integration
- device-specific web presentation glue
- hardware/service bootstrap that depends on the board implementation

Not allowed to diverge:

- reconnect logic
- ACK policy
- send-owner policy
- discovery message handling
- heartbeat/liveness semantics
- MQTT coexistence gating policy

Required outcome:

- future maintenance keeps communications logic unified by default,
- project differences are isolated to hardware adaptation rather than protocol behavior.

#### Work package 4.3 - Concrete shared-module extraction map

The receiver ESP-NOW communications layer should be consolidated into `esp32common` using the existing shared include/source layout instead of creating new receiver-local duplicates.

##### Phase 4 progress update - 2026-04-28

Completed first migration step: `rx_connection_handler` is now shared.

- shared implementation now lives at `esp32common/espnow_common_utils/rx_connection_handler.cpp`
- stable public include is `esp32common/include/esp32common/espnow/rx_connection_handler.h`
- `espnowreceiver_LCD/src/espnow/rx_connection_handler.h` and `espnowreceiver_2/src/espnow/rx_connection_handler.h` now forward to the shared public header
- receiver-specific behavior is isolated to hook wiring in:
	- `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp`
	- `espnowreceiver_2/src/main.cpp`
- removed legacy duplicate implementations:
	- `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp`
	- `espnowreceiver_2/src/espnow/rx_connection_handler.cpp`
- removed the abandoned scaffold attempt:
	- `esp32common/src/espnow/rx_connection_handler.cpp`
	- `esp32common/src/espnow/rx_connection_handler.h`

Validation status for this step:

- `espnowreceiver_LCD` migrated cleanly after hook wiring and targeted include fixes
- `espnowreceiver_2` source-level migration is in place and static checks passed
- full PlatformIO validation for `espnowreceiver_2` is currently blocked by a workspace filesystem lock / access-denied condition under `.pio/build/.../Preferences.cpp.o`, which is environmental rather than a reported source error in the migrated handler code

Important implementation note:

- the original plan targeted `esp32common/src/espnow/rx_connection_handler.cpp`
- that path was scaffolded but did not enter the active PlatformIO common-library build as expected
- the real shared implementation therefore moved to `esp32common/espnow_common_utils/rx_connection_handler.cpp`, which is on the active shared-library build path today

Completed second migration step: `rx_heartbeat_manager` is now shared.

- shared implementation now lives at `esp32common/espnow_common_utils/rx_heartbeat_manager.cpp`
- stable public include is `esp32common/include/esp32common/espnow/rx_heartbeat_manager.h`
- `espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.h` and `espnowreceiver_2/src/espnow/rx_heartbeat_manager.h` now forward to the shared public header
- receiver-specific behavior is isolated to hook wiring in:
	- `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp`
	- `espnowreceiver_2/src/main.cpp`
- removed legacy duplicate implementations:
	- `espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp`
	- `espnowreceiver_2/src/espnow/rx_heartbeat_manager.cpp`
- removed the abandoned scaffold attempt:
	- `esp32common/src/espnow/rx_heartbeat_manager.cpp`
	- `esp32common/src/espnow/rx_heartbeat_manager.h`

Validation status for this step:

- `espnowreceiver_LCD` builds cleanly with shared heartbeat manager wiring
- `espnowreceiver_2` source-level migration is in place and static checks passed
- no new compile diagnostics were reported from the touched shared/common and receiver bootstrap files

Completed third migration step: `rx_state_machine` is now shared.

- shared implementation now lives at `esp32common/espnow_common_utils/rx_state_machine.cpp`
- stable public include is `esp32common/include/esp32common/espnow/rx_state_machine.h`
- `espnowreceiver_LCD/src/espnow/rx_state_machine.h` and `espnowreceiver_2/src/espnow/rx_state_machine.h` now forward to the shared public header
- removed legacy duplicate implementations:
	- `espnowreceiver_LCD/src/espnow/rx_state_machine.cpp`
	- `espnowreceiver_2/src/espnow/rx_state_machine.cpp`
- removed the abandoned scaffold attempt:
	- `esp32common/src/espnow/rx_state_machine.cpp`
	- `esp32common/src/espnow/rx_state_machine.h`

Validation status for this step:

- `espnowreceiver_LCD` builds cleanly with shared state-machine wiring
- `espnowreceiver_2` source-level migration is in place and static checks passed
- no new compile diagnostics were reported from the touched shared/common and receiver bootstrap files

Completed fourth migration step (core route path): shared probe/ACK route registration is now unified.

- new shared route helper module:
	- `esp32common/espnow_common_utils/rx_route_registry.h`
	- `esp32common/espnow_common_utils/rx_route_registry.cpp`
- shared helper now owns:
	- discovery message classification (`msg_probe` / `msg_ack`)
	- standard probe/ACK route registration and throttling policy
	- shared keepalive/activity message classification used by receiver worker policy gates
	- shared registration helpers for type-catalog fragment routes
	- shared registration helpers for packet subtype routes (`events` / `logs` / `cell_info`)
- receiver integrations now use shared route helper:
	- `espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp`
	- `espnowreceiver_2/src/espnow/espnow_tasks.cpp`
- shared discovery classification is now consumed in receiver workers:
	- `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp`
	- `espnowreceiver_2/src/espnow/espnow_tasks.cpp`
- shared keepalive/activity classification is now consumed in receiver worker policy logic:
	- `espnowreceiver_2/src/espnow/espnow_tasks.cpp`
	- `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp` (alive-log discovery filtering)
- shared packet/type route registration helpers are now consumed by both receiver route setups:
	- `espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp`
	- `espnowreceiver_2/src/espnow/espnow_tasks.cpp`
- shared route helpers now provide explicit fragment-type / packet-subtype callback parameters, so receiver handlers no longer need label-string matching to dispatch route-specific logic:
	- `esp32common/espnow_common_utils/rx_route_registry.h`
	- `esp32common/espnow_common_utils/rx_route_registry.cpp`
	- `espnowreceiver_2/src/espnow/espnow_tasks.cpp`

Validation status for this step:

- `espnowreceiver_LCD` builds cleanly with the shared route helper wired (`waveshare_esp32s3_lcd7_lvgl`)
- `espnowreceiver_2` route-path changes compile through normal source stages, but full PlatformIO build remains blocked by a workspace filesystem lock / access-denied condition under `.pio/build/.../Preferences.cpp.o` (environmental)
- no new static diagnostics were reported for the touched shared/common and receiver route/runtime files

##### Proposed shared modules under `esp32common`

1. **Receiver communications runtime shell**
	- header: `include/esp32common/espnow/rx_runtime.h`
	- source: `src/espnow/rx_runtime.cpp`
	- responsibility:
	  - shared receiver ESP-NOW init/worker orchestration
	  - receive-queue processing loop contract
	  - connection-manager / heartbeat-manager / state-machine tick sequencing

2. **Receiver ingress and route registration**
	- header: `include/esp32common/espnow/rx_runtime_routes.h`
	- source: `src/espnow/rx_runtime_routes.cpp`
	- responsibility:
	  - route registration for shared message classes
	  - common `msg_probe` / `msg_ack` handling policy
	  - discovery throttling, shared ACK gating, and route bootstrap

3. **Receiver message handlers**
	- header: `include/esp32common/espnow/rx_runtime_messages.h`
	- source: `src/espnow/rx_runtime_messages.cpp`
	- responsibility:
	  - shared handlers for heartbeat, version, config ACK, event-log summary, type-catalog fragments, and other communications-layer messages
	  - shared protocol activity hooks

4. **Receiver connection handler**
	- header: `include/esp32common/espnow/rx_connection_handler.h`
	- source: `espnow_common_utils/rx_connection_handler.cpp`
	- responsibility:
	  - reconnect quiet mode
	  - deferred peer registration semantics
	  - init-burst sequencing
	  - suppression policy for reconnect windows

5. **Receiver heartbeat manager**
	- header: `include/esp32common/espnow/rx_heartbeat_manager.h`
	- source: `espnow_common_utils/rx_heartbeat_manager.cpp`
	- responsibility:
	  - heartbeat ownership
	  - heartbeat ACK sending
	  - reboot detection
	  - liveness timing hooks shared by both receivers

6. **Receiver state machine**
	- header: `include/esp32common/espnow/rx_state_machine.h`
	- source: `espnow_common_utils/rx_state_machine.cpp`
	- responsibility:
	  - shared device-state transitions for `CONNECTED` / `ACTIVE` / `STALE`
	  - message validity and freshness semantics

7. **Receiver transport policy hooks**
	- header: `include/esp32common/espnow/rx_runtime_hooks.h`
	- responsibility:
	  - callback interface for project-specific actions that must remain outside shared communications code
	  - examples: UI snapshot enqueue, transmitter cache updates, web refresh notifications, board-specific status publication

##### Receiver-local code that should remain after extraction

Each receiver project should keep only thin adapter code such as:

- runtime bootstrap that wires queues/tasks into the shared receiver runtime
- HAL/board includes
- UI/display update adapters
- project-specific `TransmitterManager` / webserver bridge hooks

Those adapters should call into the shared `esp32common` receiver communications modules rather than re-implement protocol logic.

##### File-by-file migration target

Move or merge the following responsibilities:

- `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp`
	→ `src/espnow/rx_runtime.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_runtime_routes.cpp`
	→ `src/espnow/rx_runtime_routes.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_runtime_messages.cpp`
	→ `src/espnow/rx_runtime_messages.cpp`
- `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp`
	→ `espnow_common_utils/rx_connection_handler.cpp` (completed 2026-04-28)
- `espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp`
	→ `espnow_common_utils/rx_heartbeat_manager.cpp` (completed 2026-04-28)
- `espnowreceiver_LCD/src/espnow/rx_state_machine.cpp`
	→ `espnow_common_utils/rx_state_machine.cpp` (completed 2026-04-28)
- `espnowreceiver_2/src/espnow/espnow_tasks.cpp`
	→ split between `src/espnow/rx_runtime.cpp`, `src/espnow/rx_runtime_routes.cpp`, and receiver-local adapter glue
- `espnowreceiver_2/src/espnow/rx_connection_handler.cpp`
	→ converge into `espnow_common_utils/rx_connection_handler.cpp` (completed 2026-04-28)
- `espnowreceiver_2/src/espnow/rx_heartbeat_manager.cpp`
	→ converge into `espnow_common_utils/rx_heartbeat_manager.cpp` (completed 2026-04-28)
- `espnowreceiver_2/src/espnow/rx_state_machine.cpp`
	→ converge into `espnow_common_utils/rx_state_machine.cpp` (completed 2026-04-28)

##### Recommended migration sequence inside Phase 4

1. unify `rx_connection_handler`
	- completed 2026-04-28 via shared `espnow_common_utils/rx_connection_handler.cpp` plus per-project hook wiring
2. unify `rx_heartbeat_manager`
	- completed 2026-04-28 via shared `espnow_common_utils/rx_heartbeat_manager.cpp` plus per-project hook wiring
3. unify `rx_state_machine`
	- completed 2026-04-28 via shared `espnow_common_utils/rx_state_machine.cpp`
4. unify shared route registration / message handlers
	- core probe/ACK route registration + discovery classification completed 2026-04-28 via shared `espnow_common_utils/rx_route_registry.{h,cpp}` and receiver integration rewiring
	- broader message-handler convergence remains in progress under this step
5. unify worker/runtime shell
6. reduce each receiver project to adapter + HAL glue only

This order minimizes risk because it converges policy first, then routing, then task/bootstrap structure.

##### Definition of success for Phase 4

Phase 4 is complete only when:

- both receivers compile against the same shared communications modules in `esp32common`,
- reconnect bug fixes require editing one shared receiver communications implementation instead of two project copies,
- remaining receiver-local code is demonstrably hardware/UI adaptation rather than communications logic.

## Execution Order

Implement in this order:

1. Phase 0.1 single send-owner enforcement
2. Phase 0.2 discovery ACK ingress hardening
3. Phase 0.3 receiver semantic alignment
4. Phase 0.4 quiet-mode enforcement of side-channel senders
5. Phase 1 instrumentation
6. Phase 2 timing refinement
7. Phase 3 guardrails / debt retirement
8. Phase 4 receiver codebase unification

Do not skip ahead to timing experiments before Phase 0 is complete.

## Validation Gate After Each Phase

### Gate A - Build and static verification

- transmitter builds cleanly
- LCD receiver builds cleanly
- receiver_2 builds cleanly
- no new active runtime reconnect paths call raw `esp_now_send()` outside approved owner modules

### Gate B - Reconnect stress validation

Run repeated forced reconnect cycles and confirm:

- reconnect completes reliably
- `ESP_ERR_ESPNOW_NO_MEM` on ACK-class traffic materially drops
- discovery-queue mirror drops are zero or clearly bounded and explained
- receiver quiet mode activates immediately and exits cleanly
- transmitter control behavior remains unaffected during receiver reconnect

### Gate C - Closure criteria

Treat the root problem as solved only when all of the following are true:

- reconnect-critical runtime traffic is owned by one send arbiter,
- discovery ACK ingress no longer has silent-loss behavior,
- both receivers behave equivalently under reconnect,
- logs/counters are sufficient to explain any remaining edge-case failure,
- both receivers use the same ESP-NOW communications core,
- remaining differences between `espnowreceiver_2` and `espnowreceiver_LCD` are HAL/device integration differences only.

## Immediate Coding Step

The next coding step should be **Phase 0.1: eliminate remaining active runtime direct `esp_now_send()` paths in the receiver API handlers and transmitter settings/cache paths**. That is the highest-leverage remaining root-cause work.

## Target End State

The target architecture is now explicitly:

- one shared transmitter ESP-NOW communications implementation,
- one shared receiver ESP-NOW communications implementation,
- per-project deviations limited to HAL and hardware-integration layers.

If a future change requires different reconnect or ACK behavior between the two receivers, that should be treated as an architectural exception that must be justified explicitly, not as the default development model.
