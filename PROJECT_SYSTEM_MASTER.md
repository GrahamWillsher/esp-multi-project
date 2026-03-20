# ESP Multi-Project System Master Architecture

**Last Updated:** March 20, 2026  
**Repository:** esp-multi-project  
**Attribution:** Derived from Dala the Great's Battery Emulator project: https://github.com/dalathegreat/Battery-Emulator

---

## 1) What this project does

This repository splits battery-emulator responsibilities across two ESP32 devices:

- **Transmitter (control node):** Olimex ESP32-POE2. Owns CAN/battery control, Ethernet services, OTA endpoint, and publishes telemetry/control state.
- **Receiver (display/control UI node):** LilyGo T-Display-S3. Owns local display, web UI/API, and receiver-side orchestration while subscribing to transmitter state over ESP-NOW.

Design goal: display/UI failure must not block core battery-control behavior.

---

## 2) How both devices work together

1. Transmitter reads/derives battery state from CAN and internal datalayer.
2. Transmitter sends state/heartbeat/version information over ESP-NOW.
3. Receiver ingests ESP-NOW messages, updates caches/state machines, and renders UI/web pages.
4. Receiver forwards selected control/config workflows back to transmitter.
5. OTA flow is receiver-driven but transmitter-validated:
   - receiver requests OTA arm session,
   - transmitter issues short-lived signed challenge,
   - receiver streams firmware with auth headers to transmitter.

---

## 3) Mandatory architecture references

### Device master documents
- `ESPnowtransmitter2/espnowtransmitter2/PROJECT_ARCHITECTURE_MASTER.md`
- `espnowreceiver_2/PROJECT_ARCHITECTURE_MASTER.md`

### Shared protocol/governance documents
- `esp32common/docs/ESP-NOW_Communication_Architecture.md`
- `esp32common/docs/ESPNOW_HEARTBEAT.md`
- `esp32common/docs/MQTT_LOGGER_IMPLEMENTATION.md`
- `esp32common/docs/project guidlines.md`
- `esp32common/OTA_CROSS_DEVICE_COMPATIBILITY_CHECKLIST.md`

---

## 4) Coverage checklist (system-level)

The two device master docs jointly cover:

- ESP-NOW and MQTT communications
- pin layout / hardware mapping
- inter-device compatibility (versioning + heartbeat + OTA auth)
- NTP/timing model
- NVS persistence model
- firmware metadata embedding in `.bin`/`.elf` (`.rodata` metadata block)
- state machines and their owning modules
- PSRAM/memory behavior at architecture level
- HAL boundaries and hardware abstraction ownership

---

## 5) Operational rule

Any change to shared protocol, OTA auth/session flow, heartbeat semantics, or version compatibility must update:

1. this system master document,
2. both device architecture master docs,
3. the retention register document (`DOCUMENT_REFERENCE_RETENTION_REGISTER.md`).
