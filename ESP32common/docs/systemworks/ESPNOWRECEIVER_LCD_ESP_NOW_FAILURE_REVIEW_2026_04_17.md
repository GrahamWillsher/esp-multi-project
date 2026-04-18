# espnowreceiver_LCD — ESP-NOW Failure Review
**Date:** 2026-04-17  
**Author:** GitHub Copilot  
**Goal:** Step back from incremental fixes and review the whole ESP-NOW communications path on `espnowreceiver_LCD`, identify the real failure modes, and recommend a complete fix rather than more local patches.

---

## 1) Executive summary

The LCD receiver's ESP-NOW path is currently unreliable because it is **not a faithful transport-layer port of `espnowreceiver_2`**.

The current LCD implementation has drifted into a **different architecture** with three major classes of failure risk:

1. **Channel ownership is unsafe**
   - the shared `ChannelManager` can restore a saved channel over the live WiFi channel after the receiver has already connected to infrastructure WiFi;
   - that restore is done without validating success and can leave the software's idea of the channel out of sync with the radio's actual channel.

2. **Normal receiver operation is mixing incompatible responsibilities**
   - STA-connected ESP-NOW reception,
   - AP fallback / config portal behavior,
   - disconnected channel sweeping,
   - discovery task announcements,
   - and common peer management that assumes `WIFI_IF_STA`.

3. **The LCD port collapsed `_2`'s staged ESP-NOW lifecycle into one monolithic runtime**
   - this has already produced multiple regressions during the current session (queue lifecycle breakage, dead wrapper code, and misordered initialization);
   - that is a strong sign the architecture itself is too fragile.

### Bottom-line conclusion

This should **not** be fixed with more tactical tweaks inside `espnow_runtime.cpp`.

The correct fix is to **rebase the LCD receiver's ESP-NOW orchestration back onto the `_2` architecture**:
- separate radio init, queue/routes, task start, and state-machine wiring into distinct phases;
- remove AP/ESP-NOW coexistence from the normal connected receiver path;
- make channel ownership single-source-of-truth and STA-first;
- use one connection truth, not three.

---

## 2) Scope and method

I reviewed the current LCD receiver against the working `_2` receiver and the shared `esp32common` ESP-NOW utilities, focusing on:
- WiFi bring-up and channel selection
- ESP-NOW init sequence
- callback / queue / worker handoff
- discovery and channel sweep behavior
- peer registration semantics
- connection state ownership
- where `_lcd` diverged from `_2`

### Primary files reviewed

#### LCD receiver
- `espnowreceiver_LCD/src/main.cpp`
- `espnowreceiver_LCD/src/config/wifi_setup.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp`
- `espnowreceiver_LCD/src/runtime/runtime_task_startup.cpp`
- `espnowreceiver_LCD/src/runtime/common_lcd.cpp`
- `espnowreceiver_LCD/src/espnow/rx_connection_handler.cpp`
- `espnowreceiver_LCD/src/espnow/rx_state_machine.cpp`

#### Working reference receiver
- `espnowreceiver_2/src/main.cpp`
- `espnowreceiver_2/src/config/wifi_setup.cpp`
- `espnowreceiver_2/src/config/runtime_task_startup.cpp`
- `espnowreceiver_2/src/espnow/espnow_callbacks.cpp`
- `espnowreceiver_2/src/espnow/espnow_tasks.cpp`
- `espnowreceiver_2/src/common.h`

#### Shared common transport utilities
- `esp32common/espnow_common_utils/channel_manager.cpp`
- `esp32common/espnow_common_utils/connection_manager.cpp`
- `esp32common/espnow_common_utils/espnow_standard_handlers.cpp`
- `esp32common/espnow_common_utils/espnow_peer_manager.cpp`
- `esp32common/espnow_common_utils/espnow_discovery.cpp`

---

## 3) What is different between `_2` and `_lcd`

## 3.1 `_2` keeps ESP-NOW lifecycle in separate phases

The working `_2` receiver has a clear staged boot model:

1. WiFi connect
2. `esp_now_init()` only
3. create runtime primitives (queue, mutex, routes)
4. start worker/discovery/tasks
5. initialize state machines / callbacks / connection manager

That means each layer has a clear contract:
- radio first,
- then queues/routes,
- then tasks,
- then connection-state orchestration.

## 3.2 `_lcd` collapses all of that into `ESPNowRuntime::init()`

The LCD receiver currently does all of the following inside one runtime entry point:
- queue creation,
- `esp_now_init()`,
- recv/send callback registration,
- channel manager init,
- connection manager init,
- RX state machine init,
- connection handler init,
- heartbeat init,
- route registration,
- discovery task start.

This is the key architectural drift.

### Why this matters

When the whole stack is folded into one function, lifecycle assumptions become implicit instead of enforced. That is exactly what happened here: during this session, queue ownership was changed, then moved, then accidentally placed inside an unrelated failure block, and the build still appeared superficially healthy.

That is a symptom of an architecture that is too easy to break accidentally.

---

## 4) High-confidence findings

## 4.0) 2026-04-17 runtime re-check (fresh instrumentation)

After re-running with additional diagnostics, the LCD receiver now provides conclusive transport evidence:

- Discovery task starts successfully (`Announcement task started` / `Periodic announcement started`).
- The LCD receiver is actively transmitting probes every 5 seconds:
   - `TX PROBE #1 ... ch=6`
   - `TX PROBE #2 ... ch=6`
   - `TX PROBE #3 ... ch=6`
   - continuing steadily.
- `alive` logs now include connection/discovery/rx counters and show:
   - `conn=CONNECTING` then timeout to `conn=IDLE`
   - `discovery=running`
   - `rxcb=0` throughout.

### What this proves

1. `_lcd` **is** sending probe messages.
2. `_lcd` is **not receiving any ESP-NOW frames at all** (not even stray callbacks).
3. The stall is therefore **not caused by local probe generation** in `_lcd`.

### Most likely remaining causes (outside local probe sender)

- Transmitter side currently locked/paired elsewhere and not participating in discovery for this receiver instance.
- Transmitter not hopping/listening on channel 6 during this run.
- Transmitter RX queue/callback path not forwarding incoming probe/ack traffic for discovery in current runtime state.

This narrows the fault domain from “LCD discovery broken” to “no inbound RF traffic reaches LCD despite valid local transmit path.”

## 4.1 Channel ownership is currently the biggest structural risk

### Evidence

In shared `channel_manager.cpp`, `ChannelManager::init()` does this:
- reads a saved channel from NVS,
- reads the current live WiFi channel from the radio,
- then, if a saved channel exists, it immediately tries to set the radio to that saved channel,
- and unconditionally updates internal `current_channel_` to the saved value.

### Why this is dangerous on the LCD receiver

The LCD receiver connects to infrastructure WiFi first in `WiFiSetup::setup_from_loaded_config()`.

After that, `ESPNowRuntime::init()` calls `ChannelManager::instance().init()`.

That means the current sequence is effectively:
1. connect STA to real AP channel,
2. then let `ChannelManager` try to restore an old ESP-NOW channel from NVS.

If the saved channel is stale, two bad outcomes are possible:

#### Case A — the driver actually changes channel
The radio is moved away from the infrastructure AP channel and the receiver is now off-channel for normal WiFi operation.

#### Case B — the driver refuses the change
The call fails internally, but `ChannelManager` still updates its own `current_channel_` bookkeeping as if it succeeded.

That creates a **split-brain channel state**:
- the radio is on one channel,
- the manager believes it is on another,
- later logic locks or reports the wrong channel.

### Why this is especially relevant here

The LCD build introduced additional AP fallback and channel sweep behavior, so stale channel state is more likely to matter than on `_2`.

### Conclusion

`ChannelManager::init()` is currently not safe for a receiver that has already joined WiFi.

This is not just a small bug; it is a broken contract between WiFi association and ESP-NOW channel control.

---

## 4.2 The LCD receiver mixes normal STA receive mode with AP fallback mechanics

### Evidence

In `wifi_setup.cpp`, normal connected operation starts with:
- `WiFi.mode(WIFI_STA)` for configured WiFi.

If WiFi is not connected, the fallback path starts:
- `WiFi.mode(WIFI_AP_STA)`
- soft AP setup on a saved ESP-NOW channel.

Meanwhile, the shared peer/discovery helpers are STA-oriented:
- `EspnowPeerManager::add_peer()` uses `WIFI_IF_STA`
- `EspnowPeerManager::add_broadcast_peer()` uses `WIFI_IF_STA`
- `EspnowStandardHandlers::handle_probe()` registers peers on the current WiFi channel using the standard peer manager

### Why this is a problem

The receiver is currently trying to support these transport assumptions in one runtime:
- **normal connected receiver**: STA on infrastructure WiFi channel
- **AP fallback portal**: AP+STA runtime
- **ESP-NOW discovery**: broadcast probe announcements
- **channel sweep**: changing channels while disconnected or in AP/APSTA mode

Those modes do not share a clean ownership model.

### Practical consequence

There is no single guarantee about which interface/channel is authoritative for ESP-NOW at runtime.

That is exactly the kind of ambiguity that causes "waiting forever" failures even when the packet handlers themselves are correct.

### Conclusion

For a receiver that already has working WiFi credentials, **AP mode should not participate in normal ESP-NOW operation at all**.

Provisioning/config portal behavior should be a separate mode, not something interwoven into the same transport flow.

---

## 4.3 LCD currently has two discovery mechanisms active in the same transport stack

### Evidence

The LCD runtime currently has:
1. the common `EspnowDiscovery::instance().start(...)` periodic announcement task, and
2. additional manual channel sweep logic in `ESPNowRuntime::task_worker()` that changes channel and sends a probe every sweep interval.

### Why this matters

These are two distinct discovery strategies:
- **fixed-channel announcement**
- **disconnected sweep-and-probe**

The working `_2` receiver relies on the common discovery model and stable STA channel assumptions.

The LCD receiver has added a second discovery layer on top, without removing the first one or introducing a single coordinator for them.

### Consequences

This creates non-deterministic behavior:
- duplicate probes,
- unclear ownership of the current channel,
- more difficult connection-state reasoning,
- more difficult diagnosis from logs.

### Conclusion

There should be exactly **one** discovery owner.

If the LCD receiver is meant to behave like `_2` when WiFi is configured, then the correct answer is:
- no channel sweep during normal STA-connected operation,
- just the standard discovery/peer path on the live WiFi channel.

---

## 4.4 The LCD receiver currently has multiple competing definitions of “connected”

### Evidence

At least three separate state sources exist:

1. **`EspNowConnectionManager`**
   - generic transport connection state: IDLE / CONNECTING / CONNECTED

2. **`RxStateMachine`**
   - receiver-side semantic state: DISCONNECTED / CONNECTED / ACTIVE / STALE

3. **LCD-local flags in `espnow_runtime.cpp`**
   - `g_connected`
   - `g_last_rx_ms`
   - `ESPNowRuntime::is_connected()` based on recent RX timeout

`main.cpp` uses `ESPNowRuntime::is_connected()` for the public alive log:
- `espnow=connected`
- `espnow=waiting`

But discovery suspension and connection progression are driven primarily by the common connection manager and the connection handler.

### Why this matters

The system can now be in states such as:
- transport manager says connected,
- RX state machine says active,
- UI/alive log still says waiting because no telemetry has refreshed `g_connected` recently.

That does not itself create radio failure, but it makes the runtime much harder to reason about and hides the real failure point.

### Conclusion

The LCD receiver should publish **one canonical connection truth** for both runtime logic and UI/logging.

---

## 4.5 The current LCD runtime has already shown lifecycle fragility during this session

### Evidence from the current investigation cycle

During this session alone, the LCD ESP-NOW stack accumulated multiple regressions while trying to repair communication:
- queue creation was moved out of one place without being safely re-homed;
- queue creation logic then ended up inside the wrong failure block;
- a dead AP/interface wrapper was introduced and left unused;
- AP fallback mode had to be revisited because it conflicted with STA assumptions;
- channel sweep was added separately from the existing discovery task.

### Why this matters

This is not evidence that the code is hopeless.
It is evidence that the **current shape of the LCD transport code makes correctness too easy to lose**.

If a system keeps breaking in different ways while trying to make it work, the right response is to correct the structure, not keep patching behavior locally.

---

## 5) What does *not* look like the primary problem

It is important to separate the likely root causes from code that is probably not the main blocker.

## 5.1 The message handlers themselves are not the first thing I would blame

The LCD route table in `espnow_runtime.cpp` is large, but structurally it is reasonable:
- packets are queued in callback,
- routed in worker task,
- known message types update battery/cache/UI state.

If the receiver were reliably receiving and draining messages on the correct channel, most of the handler layer appears capable of processing them.

## 5.2 The local display/LVGL path is not the primary transport failure

The LCD runtime has display-task differences from `_2`, but they do not explain the absence of ESP-NOW communication.

The core problem is lower in the stack:
- radio/channel/interface ownership,
- discovery strategy,
- lifecycle ordering.

---

## 6) Most likely root-cause set

I do **not** think there is a single-line root cause anymore.

The most likely root-cause set is:

### Root cause A — channel restoration is overriding or lying about the live WiFi channel
This is the strongest first-principles failure in the current code.

### Root cause B — transport mode boundaries are blurred
Normal receiver mode and config-portal/AP mode are sharing the same ESP-NOW runtime assumptions.

### Root cause C — discovery/channel logic has two owners
The common discovery task and the LCD sweep logic can both drive probe transmission and channel changes.

### Root cause D — the LCD port is no longer structurally equivalent to `_2`
That makes it easy for fixes in one layer to break another layer unintentionally.

---

## 7) Recommended complete fix

## 7.1 Do not keep patching `espnow_runtime.cpp`

The correct next step is **not**:
- more wrappers,
- more AP/STA branching,
- more special cases inside the monolithic LCD runtime.

The correct next step is to **replace the LCD ESP-NOW orchestration with the `_2` orchestration model** and keep only the LCD-specific UI adapters.

---

## 7.2 Target architecture

### Phase A — WiFi bring-up
- If receiver config exists and WiFi connects successfully:
  - run in `WIFI_STA` only
  - no AP
  - no AP+STA transport mode
  - no channel sweep while associated

- If receiver config does not exist or user explicitly requests provisioning:
  - enter a dedicated config-portal mode
  - do not pretend this is the same runtime as normal ESP-NOW receive mode

### Phase B — radio init only
Mirror `_2`:
- initialize ESP-NOW radio (`esp_now_init()`)
- disable power save if required
- do not start discovery/tasks/state callbacks yet

### Phase C — runtime primitives
Mirror `_2`:
- create ESP-NOW queue
- create LCD/UI mutexes/queues
- register message routes before worker start

### Phase D — start tasks
Mirror `_2`:
- start worker task
- start UI/display tasks
- start discovery task only after scheduler is running

### Phase E — state-machine and callback wiring
Mirror `_2`:
- init `ChannelManager`
- init `EspNowConnectionManager`
- init `ReceiverConnectionHandler`
- init `RxHeartbeatManager`
- init `RxStateMachine`
- register recv/send callbacks

### Phase F — single connection truth
Use one canonical state source for UI/logging.
Recommended:
- use `RxStateMachine` or `EspNowConnectionManager` as the canonical source;
- remove `g_connected` as a separate public truth.

---

## 7.3 Channel rules that must be enforced

This is the most important concrete design rule.

### Rule 1 — when STA is connected, live WiFi channel wins
If the receiver is associated to an infrastructure AP:
- `ChannelManager` must initialize from the live radio channel,
- not from saved NVS state.

### Rule 2 — saved channel is advisory, not authoritative
Saved channel may be used only when:
- there is no active STA association, or
- the device is in an explicit disconnected discovery mode.

### Rule 3 — never update internal channel state unless the radio call succeeded
Any call to set channel must check the return code before changing internal bookkeeping.

### Rule 4 — no disconnected sweep while WiFi-connected
If STA is connected, the receiver should remain on that channel and use standard discovery behavior.

### Rule 5 — provisioning mode must not redefine transport assumptions
AP/config-portal mode should be its own mode with explicit behavior, not something mixed into normal receive mode.

---

## 7.4 Discovery rules that must be enforced

Pick one of these models and stick to it.

### Recommended model for LCD receiver
Use the same effective model as `_2` during normal operation:
- fixed-channel operation on the infrastructure WiFi channel,
- standard discovery task only,
- no additional sweep task when WiFi is connected.

### Optional disconnected fallback model
If you still want a disconnected recovery mode, it must be explicit and bounded:
- only enabled when not associated to WiFi,
- only one owner may change channel,
- discovery task and sweep logic must not both drive probing independently.

---

## 7.5 Concrete implementation recommendation

### Recommended implementation strategy

Do a **transport-layer port reset** for LCD:

1. Keep the LCD-specific files only for:
   - LVGL/UI update enqueueing,
   - LCD logging tags,
   - display/network status presentation.

2. Recreate `_2`'s separation on LCD:
   - callback module,
   - worker/task module,
   - runtime task startup module,
   - state bootstrap phase.

3. Remove or sharply reduce the monolithic responsibilities of `espnow_runtime.cpp`.

4. Move any truly shared receiver transport orchestration into `esp32common` where possible.

### Why this is preferable

Because it fixes the real problem:
- architectural drift,
not just the latest symptom.

---

## 8) Verification plan for the complete fix

After the refactor, verification should be done in this order.

## 8.1 Boot-time logs
Confirm these truths explicitly:
- connected WiFi channel,
- current radio channel after channel manager init,
- whether saved channel was ignored or used,
- whether AP mode is active,
- whether discovery is running,
- whether sweep mode is disabled/enabled.

## 8.2 Callback path
Confirm that a probe from the transmitter produces:
- recv callback hit,
- queue depth increment,
- worker dequeue,
- `PEER_FOUND`,
- peer registration,
- `PEER_REGISTERED`,
- connection manager transition to CONNECTED.

## 8.3 Data path
Confirm that the first telemetry frame produces:
- `DATA_RECEIVED`,
- battery/UI update,
- canonical connection state becoming active,
- alive log changing away from `waiting`.

## 8.4 Negative-path checks
Confirm that:
- a stale saved channel does not override a valid WiFi association,
- AP fallback cannot silently alter normal STA-connected runtime behavior,
- discovery suspension/resume follows one canonical connection source.

---

## 9) Final recommendation

My recommendation is:

**Do not continue fixing the current LCD ESP-NOW path by adding local exceptions.**

Instead:
- treat the current `espnowreceiver_LCD` ESP-NOW transport as an architectural dead-end,
- port the working `_2` orchestration model across cleanly,
- keep LCD-only code at the UI boundary,
- and make channel ownership STA-first and explicit.

If you want the shortest correct description of the problem, it is this:

> The LCD receiver is currently trying to be a normal STA receiver, a config AP, a channel sweeper, and a custom ESP-NOW runtime all at once. The working `_2` receiver is not. The fix is to stop mixing those roles and restore a single, staged transport design.
