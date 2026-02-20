# MQTT Telemetry Publishing - Transmitter Component

## Overview

The MQTT module in Battery Emulator is a **real-time telemetry publisher** that is completely independent of the webserver UI. It publishes battery status data to an external MQTT broker for monitoring and integration with systems like Home Assistant.

**Status**: ✓ KEEP ON TRANSMITTER

---

## MQTT Module Architecture

### Location
- `Software/src/devboard/mqtt/mqtt.h`
- `Software/src/devboard/mqtt/mqtt.cpp`
- `Software/src/devboard/mqtt/mqtt_client.h`
- `Software/src/devboard/mqtt/mqtt_client.cpp`

### Dependencies (CONTROL-ONLY, NO UI DEPENDENCIES)
```cpp
#include "mqtt.h"
#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <src/communication/nvm/comm_nvm.h>        // Settings
#include <src/battery/BATTERIES.h>                 // Battery data
#include <src/communication/contactorcontrol/>     // Control status
#include <src/datalayer/datalayer.h>               // Real-time state
#include <src/devboard/utils/events.h>             // Event tracking
#include "../lib/bblanchon-ArduinoJson/ArduinoJson.h"  // JSON serialization
```

**Key Point**: NO includes from webserver, NO includes from display, NO includes from UI.

---

## What MQTT Publishes

### Real-Time Battery Telemetry

The MQTT module publishes continuous streams of:

**Status Data**:
- Online status
- System uptime
- CPU temperature
- Task performance metrics

**Battery Status** (reads from datalayer):
- State of Charge (SOC) - scaled and real
- State of Health (SOH)
- Temperature min/max
- Current (Amps)
- Voltage (V)
- Power (Watts)
- Cell voltages (min/max if enabled)
- Cell balancing status

**Control Status**:
- Contactor state
- Equipment stop button state
- Error events
- Warning events

**Configuration Info** (static):
- Battery type selected
- Inverter configuration
- Device settings
- Firmware version

### Publication Flow

```
FreeRTOS Task: mqtt_loop (runs every 10ms)
    ↓
mqtt_client_loop()
    ↓
Checks if 10 seconds elapsed
    ↓
publish_values() called
    ├─ publish_common_info()      → SOC, current, voltage, temps, etc.
    ├─ publish_events()           → Error/warning events
    ├─ publish_cell_voltages()    → Individual cell voltages (if enabled)
    └─ publish_cell_balancing()   → Cell balancing status
    ↓
Publishes to external MQTT broker (e.g., Mosquitto, HA MQTT)
    ↓
External systems (Home Assistant, Grafana, etc.) consume data
```

---

## MQTT Integration Example (Home Assistant)

Users can configure Home Assistant to consume this telemetry:

```yaml
# In Home Assistant configuration.yaml
mqtt:
  sensor:
    - name: "Battery SOC"
      state_topic: "battery/info"
      value_template: "{{ value_json.soc_scaled }}"
      unit_of_measurement: "%"
      
    - name: "Battery Power"
      state_topic: "battery/info"
      value_template: "{{ value_json.battery_power }}"
      unit_of_measurement: "W"
      
    - name: "Battery Temp Max"
      state_topic: "battery/info"
      value_template: "{{ value_json.temperature_max }}"
      unit_of_measurement: "°C"
```

---

## Why MQTT Stays on Transmitter

1. **Only Source of Real-Time Data**
   - Transmitter owns datalayer (real-time state)
   - MQTT reads directly from datalayer
   - No other component publishes this telemetry

2. **Independent of Display**
   - Works with OR without receiver
   - User can have system running with just transmitter + external monitoring
   - Receiver is optional display layer

3. **Complements ESP-NOW Communication**
   - ESP-NOW sends snapshots to receiver (display)
   - MQTT publishes continuous stream to external system (monitoring)
   - Two independent communication channels

4. **Critical for Integration**
   - Users may monitor via external MQTT without using receiver
   - Home Assistant integration requires MQTT on device with datalayer
   - That device is the transmitter

---

## MQTT vs Receiver Communication

| Aspect | MQTT | ESP-NOW to Receiver |
|--------|------|-------------------|
| **Destination** | External MQTT broker (cloud/local) | Local LilyGo receiver only |
| **Purpose** | Real-time telemetry monitoring | Local display refresh |
| **Data** | Complete telemetry stream | Battery snapshot |
| **Frequency** | Every 10 seconds | Every 100ms |
| **Dependency** | Independent | Needs transmitter running |
| **UI-Related** | No | Yes (receiver renders UI) |

---

## Configuration in platformio.ini

**Transmitter keeps MQTT dependencies**:

```ini
[env:olimex_esp32_poe2]
platform = espressif32@6.5.0
board = esp32-poe2

lib_deps = 
    # ... battery emulator deps ...
    # MQTT is part of devboard/mqtt/ - include it
    # (no additional dependencies needed for MQTT client)
    
build_flags = 
    -D MQTT_ENABLED=1  # Enable MQTT publishing
    # (or leave disabled if user doesn't want external monitoring)
```

---

## Summary

**MQTT is NOT a webserver component** - it's an independent **real-time telemetry publisher**:

- ✓ Stays on transmitter
- ✓ Reads from datalayer (real-time battery state)
- ✓ Publishes to external MQTT broker
- ✓ Enables Home Assistant/Grafana/etc. integration
- ✓ Works independently of receiver display
- ✓ Only place battery telemetry is published

**Receiver does NOT get MQTT** - it receives snapshots via ESP-NOW instead.

