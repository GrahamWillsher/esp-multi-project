# ESP-NOW transmitter reconnect and LCD probe cadence review (2026-04-17)

## Scope

This note answers two specific questions:

1. Looking back at the `espnowreceiver_2` behavior, does `espnowreceiver_LCD` need to transmit a discovery probe every 2 seconds while it is connected?
2. Why does reconnection appear to depend on rebooting the transmitter, and what is wrong in the transmitter reconnect path?

The review below is based on the current code in:

- `esp32common/include/esp32common/config/timing_config.h`
- `esp32common/espnow_common_utils/espnow_discovery.cpp`
- `espnowreceiver_2/src/config/runtime_task_startup.cpp`
- `espnowreceiver_2/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_LCD/src/runtime/runtime_task_startup.cpp`
- `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/main.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_connection_handler.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp`
- `esp32common/espnow_common_utils/connection_manager.cpp`

---

## Finding 1: `_lcd` does **not** need to send a probe every 2 seconds while connected

### Short answer

No.

There are two separate cases:

1. **Receiver is connected to WiFi, but not connected to the transmitter**  
   In this case the receiver should keep advertising with the shared discovery task.
2. **Receiver is already connected to the transmitter over ESP-NOW**  
   In this case discovery should be suspended, so the receiver should **not** keep transmitting discovery probes.

### What `_2` actually does

`espnowreceiver_2` starts shared discovery from `src/config/runtime_task_startup.cpp` using `TimingConfig::ANNOUNCEMENT_INTERVAL_MS`.

That interval is defined in `esp32common/include/esp32common/config/timing_config.h` as:

- `announcement_interval_ms = 5000`

So `_2` is using a **5 second** receiver-side announcement interval, not 2 seconds.

### What the shared discovery code actually does

The shared discovery implementation in `esp32common/espnow_common_utils/espnow_discovery.cpp`:

- sends a broadcast `msg_probe`
- waits `interval_ms`
- suspends itself once `is_connected_callback()` reports connected

This means the expected receiver behavior is:

- **not connected to ESP-NOW peer** -> send probe every 5 seconds
- **connected to ESP-NOW peer** -> suspend discovery and stop probe transmission

### What `_lcd` currently does relative to `_2`

`espnowreceiver_LCD` was brought into the same pattern as `_2`:

- discovery is started from runtime task startup
- it uses the same `TimingConfig::ANNOUNCEMENT_INTERVAL_MS`
- `rx_connection_handler.cpp` suspends discovery on `CONNECTED`
- `rx_connection_handler.cpp` resumes discovery when the connection is lost

So the correct expectation for `_lcd` is:

- **while associated to WiFi but not yet ESP-NOW connected**: probe every 5 seconds
- **while ESP-NOW connected**: no continuous probes; heartbeat traffic takes over as the liveness mechanism

### Where the “2 seconds” number actually belongs

The 2-second number is not the receiver discovery interval.

The closest shared timing values are:

- transmitter-side ACK wait timeout: 2000 ms in `espnow_timing_config.h`
- transmitter-side data send interval: 2000 ms in `timing_config.h`

Those are different mechanisms. They are not the receiver discovery cadence.

### Conclusion on item 1

The observed `_lcd` behavior should be judged against this rule:

- **not connected to TX** -> one probe every 5 seconds is correct
- **connected to TX** -> repeated probes are not required and should stop

So `_lcd` does **not** need to transmit a probe every 2 seconds when connected.

---

## Finding 2: transmitter reconnect has a real state-machine hole

### Short answer

Yes, there is a transmitter-side reconnect issue.

The reconnect path is only guaranteed to restart discovery when the common connection manager transitions from:

- `CONNECTED -> IDLE`

It does **not** automatically restart discovery after:

- `CONNECTING -> IDLE`

That is a real gap, and it is enough to explain a “reconnect only after transmitter reboot” symptom.

### How transmitter discovery is supposed to work

The transmitter is the active discovery side.

At boot:

1. `main.cpp` calls `TransmitterConnectionHandler::start_discovery()`
2. that posts `CONNECTION_START`
3. common `EspNowConnectionManager` transitions `IDLE -> CONNECTING`
4. TX state callback starts `DiscoveryTask::start_active_channel_hopping()`
5. active hopping scans channels and broadcasts `msg_probe`
6. when receiver ACKs, the transmitter:
   - posts `PEER_FOUND`
   - registers the receiver as a peer
   - posts `PEER_REGISTERED`
7. common manager transitions `CONNECTING -> CONNECTED`

This boot path is coherent.

### What happens after a normal connected-session loss

If the transmitter was fully connected and later loses the receiver:

1. heartbeat ACKs stop
2. common connection manager posts `CONNECTION_LOST`
3. common manager transitions `CONNECTED -> IDLE`
4. inside `connection_manager.cpp`, auto reconnect posts `CONNECTION_START`
5. event processor later consumes that event and re-enters `CONNECTING`
6. TX callback restarts active discovery hopping

So **`CONNECTED -> IDLE` recovery is implemented**.

### The actual bug

The common connection manager only auto-posts `CONNECTION_START` when:

- `auto_reconnect_enabled_ == true`
- and the transition is `old_state == CONNECTED` to `new_state == IDLE`

That logic is in `esp32common/espnow_common_utils/connection_manager.cpp`.

If the transmitter reaches `CONNECTING`, but discovery/peer registration does not complete before the 30 second connecting timeout, then this happens:

1. common manager logs `CONNECTING timeout ... -> IDLE`
2. state becomes `IDLE`
3. peer MAC is cleared
4. **no auto reconnect is posted**, because the old state was `CONNECTING`, not `CONNECTED`

At that point the transmitter can sit in `IDLE` with no fresh discovery start, unless something external restarts the process.

That is the key reconnect hole.

### Why this matches the field symptom

The reported symptom is that the system reconnected when the transmitter was rebooted.

That matches the code path very well:

- transmitter reboot runs the full boot discovery sequence again
- full boot discovery posts `CONNECTION_START`
- active channel hopping restarts from scratch

But without reboot, if a reconnect attempt already degraded into:

- `CONNECTING -> IDLE`

there is no built-in second auto-start.

So a failed reconnect attempt can leave the transmitter idle until reboot/manual restart.

### Why this is more important than the probe cadence question

The receiver-side probe cadence is behaving by design:

- 5 seconds while not connected
- suspended while connected

The transmitter-side reconnect flaw is more serious because it can strand the whole link after a partial reconnect attempt even when the receiver is behaving correctly.

### Additional transmitter design detail worth noting

The transmitter’s active hopping task exits once a connection is established. That is fine.

However, after that point, reconnect depends entirely on the connection manager / TX handler chain restarting discovery again. Because that chain is incomplete for `CONNECTING -> IDLE`, the transmitter has no guaranteed autonomous recovery after a failed reconnect attempt.

In other words:

- boot discovery is strong
- recovery from fully connected loss is present
- recovery from failed reconnect attempt is incomplete

---

## Root-cause summary

### Item 1: probe cadence

No defect.

The receiver does not need to probe every 2 seconds while connected. The intended `_2` behavior is:

- 5-second probe interval while not connected
- suspended discovery while connected

### Item 2: transmitter reconnect

Real defect.

The transmitter reconnect path has a state-machine hole:

- `CONNECTED -> IDLE` auto-restarts discovery
- `CONNECTING -> IDLE` does **not** auto-restart discovery

That means a failed reconnect attempt can dead-end in `IDLE` until reboot.

---

## Recommended fix

The clean fix is on the transmitter/common state machine side, not on `_lcd` discovery cadence.

### Recommended change

Update the reconnect policy so the transmitter restarts discovery after **any** transition to `IDLE` that represents an interrupted connection workflow, not only after `CONNECTED -> IDLE`.

Practical options:

1. **Preferred**: change `EspNowConnectionManager::transition_to_state()` so auto reconnect posts `CONNECTION_START` for both:
   - `CONNECTED -> IDLE`
   - `CONNECTING -> IDLE`

2. Alternatively: add explicit transmitter-side handling in `TransmitterConnectionHandler` so that when the new state is `IDLE` after a failed reconnect attempt, it reposts discovery start itself.

Option 1 is cleaner because it fixes the state-machine contract at the shared layer rather than relying on TX-local compensation.

### Why this is the right fix

It preserves the intended model:

- receiver advertises when disconnected
- transmitter actively hunts and reconnects
- failed reconnect attempts do not require a reboot to recover

### What should *not* be changed to solve this

The following are not the correct primary fix for the reconnect symptom:

- forcing `_lcd` to probe every 2 seconds while connected
- keeping receiver discovery permanently active during an established link
- relying on manual transmitter reboot to restart discovery

Those either diverge from `_2` behavior or mask the real state-machine issue.

---

## Final conclusion

1. `espnowreceiver_LCD` does **not** need to transmit probes every 2 seconds while connected.  
   The `_2` model is 5-second probes while disconnected, then discovery suspension once connected.

2. The transmitter does have a reconnect defect.  
   The current state machine restarts discovery after `CONNECTED -> IDLE`, but not after `CONNECTING -> IDLE`. A failed reconnect attempt can therefore stall in `IDLE` until the transmitter is rebooted.

That second item is the real bug to fix next.