# Device Temperature Investigation and TX→RX Implementation (2026-04-04)

## Scope

This note records:

1. how transmitter temperature is currently obtained,
2. what the ESP-IDF-native alternative offers,
3. hardware/framework constraints across the two devices in this workspace,
4. the transport/UI design that was implemented, and
5. follow-up recommendations.

The user request was to:

- investigate transmitter temperature acquisition,
- compare that with the ESP-IDF alternative,
- add a new ESP-NOW message carrying transmitter temperature to the receiver at heartbeat cadence,
- sample receiver temperature locally at the same cadence,
- show both temperatures in the top device cards on `/`, and
- document findings under `esp32common/docs/systemworks`.

## Hardware and framework context

### Transmitter

- Project: `ESPnowtransmitter2/espnowtransmitter2`
- PlatformIO environment: `olimex_esp32_poe2`
- MCU family: classic ESP32
- Framework: Arduino on `espressif32@6.5.0`

### Receiver

- Project: `espnowreceiver_2`
- PlatformIO environments: `lilygo-t-display-s3`, `lilygo-t-display-s3_tft`
- MCU family: ESP32-S3
- Framework: Arduino on `espressif32@6.5.0`

This distinction matters because temperature-sensor API availability differs by chip family.

## Investigation findings

## 1. Existing application-level temperature path

A repo-wide search found no existing dedicated device-temperature acquisition or transport path in either project.

The only direct transmitter reference was a safety comment in `ESPnowtransmitter2/espnowtransmitter2/src/main.cpp`:

- "Safety watchdog: CAN alive countdown, CPU temperature, voltage/cell checks."

That comment indicates intent, but there was no existing implementation feeding temperature into ESP-NOW, MQTT, the receiver cache, or the web UI.

The receiver also had no existing local chip-temperature sampling path.

## 2. Heartbeat cadence / best insertion point

The existing heartbeat cadence is already centralized:

- transmitter: `HeartbeatManager::tick()` sends a heartbeat every `TimingConfig::HEARTBEAT.interval_ms`
- receiver: `RxHeartbeatManager::tick()` runs continuously and already owns heartbeat timing/state

That made heartbeat cadence the cleanest timing source for the new feature.

## 3. Dashboard insertion point

The `/` dashboard already renders a transmitter card and receiver card in `espnowreceiver_2/lib/webserver/pages/dashboard_page_content.cpp`.

Each card already has a device-name line near the top, which is the correct place to add a right-aligned temperature label without disturbing the rest of the layout.

## 4. Arduino temperature API availability in the installed framework

The installed Arduino core exposes `temperatureRead()` in `cores/esp32/esp32-hal.h` / `esp32-hal-misc.c`.

Inspection of the installed framework shows that `temperatureRead()` is a compatibility wrapper over different lower-level implementations depending on target:

- on classic ESP32, it uses the older ROM-backed `temprature_sens_read()` path,
- on newer chips such as ESP32-S3, it uses the IDF temperature-sensor driver path via `driver/temp_sensor.h`.

This is important because it means a single `temperatureRead()` call works across both current projects while still using the chip-appropriate backend.

## 5. ESP-IDF-native alternative

### ESP32-S3

Espressif documents the handle-based temperature sensor driver for ESP32-S3, typically via:

- `driver/temperature_sensor.h`
- `temperature_sensor_install()`
- `temperature_sensor_enable()`
- `temperature_sensor_get_celsius()`

Advantages of the handle-based ESP-IDF path:

- explicit resource lifecycle,
- explicit operating range selection,
- clearer ownership model,
- easier future extension if more detailed sensor management is required.

Important ESP-IDF caveat:

- the on-chip sensor reflects silicon temperature, not ambient air temperature,
- it is useful for thermal trend/health visibility,
- it is not suitable as a precision ambient thermometer.

### Classic ESP32 transmitter

In the installed Arduino/PlatformIO framework package for this workspace, there is no equivalent modern `driver/temperature_sensor.h` path exposed for the classic ESP32 target. The practical temperature path available here is the Arduino wrapper, which on classic ESP32 falls back to the older ROM-based mechanism.

That makes a direct modern-IDF-only solution a poor fit for the current transmitter hardware/framework combination.

## Design decision

## Chosen acquisition strategy

Use `temperatureRead()` for both devices.

Reasons:

1. It compiles across both current targets in the installed framework.
2. It already maps to chip-appropriate lower-level implementations.
3. It avoids splitting the code into separate classic-ESP32 vs ESP32-S3 driver branches.
4. It keeps the new feature simple and low-risk.

## Why not use only the newer ESP-IDF handle-based API?

Because the two devices are not on equivalent silicon/API footing:

- receiver (ESP32-S3): modern ESP-IDF temperature driver support is good,
- transmitter (classic ESP32): support in this installed framework is not symmetric.

A single unified Arduino wrapper is therefore the safest cross-project implementation for this codebase today.

## What was implemented

## 1. Shared local temperature sampler

Added a reusable shared helper:

- `esp32common/runtime_common_utils/device_temperature.h`
- `esp32common/runtime_common_utils/device_temperature.cpp`

Responsibilities:

- read chip temperature via `temperatureRead()`,
- validate/clamp the result,
- cache the latest reading as centi-degrees Celsius,
- support periodic sampling using a caller-supplied interval.

## 2. New ESP-NOW message

Added a new shared wire message in `esp32common/espnow_transmitter/espnow_common.h`:

- `msg_temperature_report`
- `temperature_report_t`

Payload fields:

- `seq`
- `temperature_centi_c`
- `valid`
- `uptime_ms`

This is transmitter → receiver only.

## 3. Transmitter behavior

The transmitter now:

- samples its local chip temperature,
- sends a `msg_temperature_report` immediately after each successfully-sent heartbeat,
- therefore uses heartbeat cadence without altering heartbeat ACK semantics.

This work was added in:

- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/heartbeat_manager.h`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/heartbeat_manager.cpp`

## 4. Receiver behavior

The receiver now:

- samples its own local chip temperature using the same shared helper,
- uses the same heartbeat interval as the local sampling cadence,
- accepts incoming `msg_temperature_report` packets,
- stores the latest transmitter temperature in `TransmitterManager`.

Receiver changes were added in:

- `espnowreceiver_2/src/espnow/rx_heartbeat_manager.cpp`
- `espnowreceiver_2/src/espnow/espnow_tasks.cpp`
- `espnowreceiver_2/lib/webserver/utils/transmitter_manager.h`
- `espnowreceiver_2/lib/webserver/utils/transmitter_manager.cpp`

## 5. API / web dashboard

The receiver dashboard APIs now expose temperature values:

- transmitter temperature via cached ESP-NOW report,
- receiver temperature via local shared sampler.

Updated files:

- `espnowreceiver_2/lib/webserver/api/api_telemetry_handlers.cpp`

The `/` dashboard cards now show a right-aligned thermometer label on the same line as the device name.

Updated files:

- `espnowreceiver_2/lib/webserver/pages/dashboard_page_content.cpp`
- `espnowreceiver_2/lib/webserver/pages/dashboard_page_script.cpp`

Implementation choice for icon:

- used the thermometer emoji `🌡️`

This is lightweight and avoids pulling in extra image/icon assets.

## Behavioral notes

### What the number means

The displayed temperature is chip/internal temperature, not ambient enclosure temperature.

It is best interpreted as:

- a thermal trend indicator,
- a relative load/heating indicator,
- a quick health/sanity signal.

It should not be treated as a calibrated ambient thermometer.

### Cadence

- transmitter report cadence: heartbeat interval (`TimingConfig::HEARTBEAT.interval_ms`)
- receiver local sampling cadence: same interval

### Transport design choice

A separate ESP-NOW message was used rather than overloading the heartbeat payload.

Why:

- it keeps heartbeat semantics stable,
- it avoids expanding the heartbeat structure used by time/state logic,
- it limits risk to existing ACK/CRC behavior,
- it matches the user request for "another ESP-NOW message" while still using heartbeat periodicity.

## Suggestions / follow-up recommendations

## 1. If you need true ambient temperature, add an external sensor

If the requirement becomes "ambient air temperature near the board" rather than chip temperature, the internal sensor is the wrong source. Use an external sensor such as:

- I2C temperature sensor,
- combined temp/humidity sensor,
- board-level thermistor with calibration.

## 2. If the transmitter hardware migrates away from classic ESP32, reconsider the API

If both ends later run on a newer family with consistent temperature-driver support, it would then make sense to standardize on the newer ESP-IDF handle-based driver everywhere.

## 3. Consider stale-age display later

At present the dashboard shows the latest value. A later enhancement could also show:

- freshness / last-update age,
- disconnected / stale visual state,
- optional min/max observed chip temperatures.

## 4. Consider using temperature for safety only after validation

There was already a comment hinting at CPU-temperature safety checks. That should only be promoted into protection logic after:

- observing the values on real hardware,
- confirming stable range and drift,
- determining sensible warning/shutdown thresholds for each board type.

Internal ESP temperatures vary significantly with silicon load and board conditions, so UI visibility should come before safety enforcement.

## Validation expectation

The implementation should be validated by building both projects and observing:

1. transmitter dashboard card updates with a temperature,
2. receiver dashboard card updates with its own local temperature,
3. values refresh roughly at heartbeat cadence,
4. transmitter value survives normal disconnect/reconnect cycles.
