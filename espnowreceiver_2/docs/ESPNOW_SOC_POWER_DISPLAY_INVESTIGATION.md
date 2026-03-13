# ESP-NOW SOC/Power Display Investigation

Date: 2026-02-26

## Executive Summary
The transmission and reception pipeline for SOC/power is present end-to-end. The transmitter builds an `espnow_payload_t`, sets `type = msg_data`, caches it, and the transmission task sends it via ESP-NOW. The receiver enqueues raw frames, routes `msg_data` to `handle_data_message()`, validates checksum, updates globals, and calls `display_soc()`/`display_power()` under the TFT mutex. A likely reason for “nothing displayed” is that the first received values can equal the receiver’s defaults (SOC=50, power=0), which previously caused the display updates to be skipped. I already added a “first_data” path so the first valid packet forces a display update.

If the screen is still blank, the next likely causes are: (1) display task never called due to routing mismatch (message type mismatch), (2) no data actually sent because transmission is inactive or connection not established, or (3) TFT not initialized/blocked by mutex. The investigation steps below target these explicitly.

## End-to-End Data Path (What Happens)

### 1) Transmitter: generate SOC/power payload
- File: [ESPnowtransmitter2/espnowtransmitter2/src/espnow/data_sender.cpp](ESPnowtransmitter2/espnowtransmitter2/src/espnow/data_sender.cpp)
- Data source: `datalayer.battery.status.reported_soc` and `active_power_W`.
- Creates `tx_data` and sets:
  - `tx_data.type = msg_data`
  - `tx_data.soc = soc_percent`
  - `tx_data.power = power_w`
  - `tx_data.checksum = calculate_checksum(&tx_data)`
- Caches data and calls `TransmissionSelector::transmit_dynamic_data()`.

### 2) Transmitter: send payload via ESP-NOW
- File: [ESPnowtransmitter2/espnowtransmitter2/src/espnow/transmission_task.cpp](ESPnowtransmitter2/espnowtransmitter2/src/espnow/transmission_task.cpp)
- `TransmissionTask::transmit_next_transient()` uses `esp_now_send(peer_mac, &entry.data, sizeof(espnow_payload_t))`.
- Only sends if `EspNowConnectionManager::instance().is_connected()` is true.

### 3) Receiver: ISR callback enqueues messages
- File: [espnowreceiver_2/src/espnow/espnow_callbacks.cpp](espnowreceiver_2/src/espnow/espnow_callbacks.cpp)
- `on_data_recv()` validates length and pushes raw message into `ESPNow::queue`.

### 4) Receiver: message routing
- File: [espnowreceiver_2/src/espnow/espnow_tasks.cpp](espnowreceiver_2/src/espnow/espnow_tasks.cpp)
- `router.route_message(queue_msg)` uses `msg->data[0]` as message type.
- `msg_data` handler calls `handle_data_message()`.

### 5) Receiver: payload parsing + display
- File: [espnowreceiver_2/src/espnow/espnow_tasks.cpp](espnowreceiver_2/src/espnow/espnow_tasks.cpp)
- `handle_data_message()`:
  - Casts to `espnow_payload_t`.
  - Validates checksum (`soc + power`).
  - Updates `ESPNow::received_soc`, `received_power`, `data_received`, and dirty flags.
  - Calls `display_soc()` and `display_power()` under `RTOS::tft_mutex`.
  - **Fix applied:** on first valid packet, dirty flags are forced even if values match defaults.

### 6) Display functions
- File: [espnowreceiver_2/src/display/display_core.cpp](espnowreceiver_2/src/display/display_core.cpp)
- `display_soc()` and `display_power()` early-return if value unchanged.
- This is why “first data equals defaults” previously resulted in no draw.

## Findings (Current State)

### Confirmed working logic
- Transmitter sets `tx_data.type = msg_data` and checksum in data sender.
- Transmission task sends the cached `espnow_payload_t` directly via `esp_now_send()`.
- Receiver routes `msg_data` and parses `espnow_payload_t` correctly.
- Display functions exist and are called in the data handler.

### Known fix already applied
- Receiver now forces display update on first valid packet (even if SOC=50 and Power=0).
- File: [espnowreceiver_2/src/espnow/espnow_tasks.cpp](espnowreceiver_2/src/espnow/espnow_tasks.cpp)

## What Could Still Be Failing (Ranked)

### A) No ESP-NOW data is actually being sent
Likely causes:
- Transmitter doesn’t think the receiver is connected, so `TransmissionTask` skips sending.
- Transmission is inactive (`EspnowMessageHandler::is_transmission_active()` false), so `DataSender` doesn’t push updates.

What to check:
- Confirm connection state on transmitter.
- Confirm `request_data_stream()`/`msg_request_data` is being issued by receiver and acknowledged.

### B) Message type mismatch (routing never hits `handle_data_message()`)
If `tx_data.type` isn’t `msg_data` or the payload is not sent as `espnow_payload_t`, router will ignore it.

What to check:
- Ensure `tx_data.type = msg_data` is in use and compiled.
- Ensure no other code path overwrites `tx_data.type` before send.

### C) Checksum mismatch (data received but rejected)
If checksum calculation differs between transmitter and receiver, handler will discard data.

What to check:
- Confirm transmitter checksum is `soc + power` (same as receiver).
- Confirm payload struct is packed and identical on both ends.

### D) TFT mutex or display stack not working
If `RTOS::tft_mutex` is null or not acquired, display update won’t run.

What to check:
- Mutex creation in [espnowreceiver_2/src/main.cpp](espnowreceiver_2/src/main.cpp).
- Verify `xSemaphoreTake()` succeeds in data handler (no deadlocks).

## Targeted Investigation Steps (Fastest Signal)

1) **Receiver proof of routing:**
   - Add a short log inside `handle_data_message()` to confirm it fires and prints SOC/Power.
   - Already logs: `Valid: SOC=... Power=...`.

2) **Transmitter proof of actual send:**
   - Confirm `TX_TASK` logs “Transient sent” messages.
   - If missing, connection or transmission state is the blocker.

3) **Verify data request/activation path:**
   - Confirm receiver sends `msg_request_data` and transmitter enables `is_transmission_active()`.

4) **Display draw test:**
   - Temporarily call `display_soc(55)` and `display_power(500)` once after init to confirm TFT drawing.

## Recommended Next Change (if display still blank)

- Add a temporary, minimal diagnostic step to prove draw path:
  - After `displayInitialScreen()` in [espnowreceiver_2/src/main.cpp](espnowreceiver_2/src/main.cpp), call `display_soc(55)` and `display_power(500)` under `RTOS::tft_mutex`.
  - If this renders, display layer is OK and the problem is in data flow.

## Notes
- The receiver default values in [espnowreceiver_2/src/globals.cpp](espnowreceiver_2/src/globals.cpp) are SOC=50, Power=0. If real data equals defaults, display functions previously skipped updates. This is now handled by the first-data forcing change.

---

If you want, I can add the temporary diagnostics and/or capture targeted logs on both transmitter and receiver to pinpoint the blockage in one run.
