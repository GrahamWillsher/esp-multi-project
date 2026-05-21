# Transmitter Config MQTT Writeback Investigation (2026-05-20)

## Scope
Investigate why edits made on receiver `/transmitter/config` are not being persisted to transmitter NVS via MQTT command flow.

No code changes were made during this investigation.

---

## Systems examined
- Receiver web UI save path (`/transmitter/config`) in receiver_2
- Receiver API save handlers (`/api/save_network_config`, `/api/save_mqtt_config`)
- Receiver MQTT command/ACK correlation logic
- Transmitter MQTT command subscriptions and handlers
- Transmitter MQTT NVS persistence logic (`MqttConfigManager::saveConfig()`)

---

## End-to-end path reviewed

1. User edits fields on `/transmitter/config` and clicks Save.
2. UI posts two requests in parallel:
   - `/api/save_network_config`
   - `/api/save_mqtt_config`
3. Receiver API publishes MQTT command and waits for transmitter ACK (`request_id` correlation).
4. Transmitter receives command topic, validates payload, saves to NVS, publishes ACK.
5. Receiver API returns success/failure to UI.

---

## Findings

## 1) Topic contract mismatch in receiver_2 (critical)

receiver_2 API handlers publish and wait on **different topic names** than the transmitter uses.

### receiver_2 publishes/waits for:
- `batt-emu/mqtt-v1/rx/cmd/network/update`
- `batt-emu/mqtt-v1/tx/ack/network/update`
- `batt-emu/mqtt-v1/rx/cmd/mqtt/update`
- `batt-emu/mqtt-v1/tx/ack/mqtt/update`

### transmitter subscribes/publishes:
- `batt-emu/mqtt-v1/rx/cmd/update/network`
- `batt-emu/mqtt-v1/tx/ack/network`
- `batt-emu/mqtt-v1/rx/cmd/update/mqtt`
- `batt-emu/mqtt-v1/tx/ack/mqtt`

Result:
- transmitter never receives receiver_2 command topics (wrong command topic path)
- even if an ACK existed, receiver_2 waits for wrong ACK topic string
- `publishJsonAndWaitForAck()` times out and API returns command-channel unavailable/failure
- transmitter NVS write function is never reached for these requests from receiver_2

---

## 2) ACK matcher requires exact topic equality (amplifies mismatch)

receiver_2 stores ACKs by `(request_id, topic)` and consumes only when topic exactly equals expected `ack_topic`.

So `tx/ack/mqtt` does **not** satisfy wait for `tx/ack/mqtt/update`.

This confirms the mismatch is fatal to command completion.

---

## 3) Transmitter persistence implementation appears correct

Transmitter `handle_mqtt_command()` (for `rx/cmd/update/mqtt`) calls:
- `MqttConfigManager::saveConfig(...)` (writes NVS keys)
- `MqttConfigManager::applyConfig()`
- `publish_mqtt_ack(..., success=true/false, ...)`

No defect found in transmitter save path itself during this review.

---

## 4) LCD receiver path appears aligned to shared topic contract

LCD command client sends MQTT updates using shared receiver topic constants (`update/mqtt`, `update/network`) and ACK tracking.

The hardcoded legacy constants in LCD `api_network_handlers.cpp` are present but MQTT save path currently uses `MqttCommandClient`, which uses the shared contract constants.

---

## Evidence summary (files inspected)

- `espnowreceiver_2/lib/webserver/api/api_network_handlers.cpp`
  - defines `rx/cmd/network/update`, `tx/ack/network/update`, `rx/cmd/mqtt/update`, `tx/ack/mqtt/update`
  - uses these constants in `api_save_network_config_handler()` and `api_save_mqtt_config_handler()`

- `espnowreceiver_2/src/mqtt/mqtt_client.cpp`
  - `publishJsonAndWaitForAck(...)` waits for exact topic
  - `tryConsumePendingAck(...)` matches exact `(request_id + ack_topic)`

- `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp`
  - subscribes to `rx/cmd/update/network` and `rx/cmd/update/mqtt`
  - handles commands in `handle_network_command()` / `handle_mqtt_command()`
  - publishes ACK on `tx/ack/network` and `tx/ack/mqtt`

- `ESPnowtransmitter2/espnowtransmitter2/lib/mqtt_manager/mqtt_manager.cpp`
  - `MqttConfigManager::saveConfig(...)` writes MQTT config to NVS

- `espnowreceiver_LCD/src/mqtt/mqtt_command_client.cpp`
  - sends updates via shared `MqttTopicsReceiver::RxCmd::UPDATE_*`

---

## Conclusion

The current failure to persist `/transmitter/config` updates from receiver_2 is caused by a **topic path mismatch** in receiver_2 API command/ACK constants, not by transmitter NVS save logic.

Primary root cause:
- receiver_2 uses `.../rx/cmd/network/update` and `.../rx/cmd/mqtt/update`
- transmitter expects `.../rx/cmd/update/network` and `.../rx/cmd/update/mqtt`

Secondary root cause:
- receiver_2 waits for `.../tx/ack/network/update` and `.../tx/ack/mqtt/update`
- transmitter publishes `.../tx/ack/network` and `.../tx/ack/mqtt`

---

## Suggested next action (not applied here)

Standardise receiver_2 API command/ACK constants to the same shared contract used by transmitter and LCD command client (`update/<model>` and `tx/ack/<model>`), then re-test save flow and verify NVS persistence with a reboot/readback cycle.
