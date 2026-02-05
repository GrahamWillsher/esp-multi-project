# ESP-NOW Transmitter (Modular Architecture)

**Hardware**: Olimex ESP32-POE-ISO (WROVER)

## Overview

This is a refactored, modular version of the ESP-NOW transmitter that reduces the main.cpp from 866 lines to ~150 lines by properly separating concerns into reusable, testable modules.

## Features

- **ESP-NOW Protocol**: Bidirectional communication with ESP-NOW receivers
- **Ethernet Connectivity**: W5500 chip for reliable wired networking
- **MQTT Telemetry**: Publish battery/power data to MQTT broker
- **HTTP OTA Updates**: Remote firmware updates via HTTP
- **NTP Time Sync**: Automatic time synchronization
- **FreeRTOS Tasks**: 4 concurrent tasks for optimal performance

## Architecture

### Directory Structure

```
espnowtransmitter2/
├── platformio.ini           # Build configuration
├── src/
│   ├── main.cpp            # Entry point (~150 lines)
│   ├── config/             # Configuration headers
│   │   ├── hardware_config.h    # ETH PHY pins, GPIO
│   │   ├── network_config.h     # MQTT, NTP, Ethernet IP
│   │   └── task_config.h        # FreeRTOS stack sizes, priorities
│   ├── network/            # Network services (singletons)
│   │   ├── ethernet_manager.h/cpp   # Ethernet initialization & events
│   │   ├── mqtt_manager.h/cpp       # MQTT telemetry publishing
│   │   ├── ota_manager.h/cpp        # HTTP OTA server
│   │   └── mqtt_task.h/cpp          # MQTT FreeRTOS task wrapper
│   └── espnow/             # ESP-NOW protocol (singletons)
│       ├── message_handler.h/cpp    # RX task, message routing
│       ├── discovery_task.h/cpp     # Periodic announcements
│       └── data_sender.h/cpp        # Test data transmission
```

### Key Design Patterns

1. **Singleton Pattern**: All managers use thread-safe singletons
   - Single global access point via `instance()`
   - Deleted copy constructors/assignment operators
   - No manual memory management needed

2. **Configuration Separation**: All magic numbers extracted to config headers
   - `hardware_config.h`: Physical pin mappings
   - `network_config.h`: Network credentials, endpoints
   - `task_config.h`: FreeRTOS timing and priorities

3. **Task-Based Architecture**: 4 FreeRTOS tasks with proper priorities
   - **Critical (3)**: ESP-NOW RX handler (message_handler)
   - **Normal (2)**: Data sender, discovery announcements
   - **Low (1)**: MQTT publishing

## FreeRTOS Tasks

| Task | Priority | Stack | Function |
|------|----------|-------|----------|
| **message_handler** | Critical (3) | 4KB | Process incoming ESP-NOW messages |
| **data_sender** | Normal (2) | 4KB | Send test data every 2s when active |
| **discovery_task** | Normal (2) | 3KB | Broadcast PROBE every 5s until connected |
| **mqtt_task** | Low (1) | 4KB | Publish telemetry, handle reconnection |

## ESP-NOW Message Flow

### Discovery Phase
1. Transmitter broadcasts `msg_probe` every 5 seconds
2. Receiver responds with `msg_ack` (contains receiver MAC)
3. Discovery task automatically stops (receiver connected)

### Data Transmission
1. Receiver sends `msg_request_data` to start transmission
2. Data sender task sends test data every 2 seconds
3. Receiver sends `msg_abort_data` to stop transmission

### Configuration Sync
1. Receiver sends `msg_probe` with `subtype_settings`
2. Transmitter responds with IP configuration via `send_settings()`

### Remote Control
- `msg_reboot`: Reboot ESP32 via `ESP.restart()`
- `msg_ota_start`: Trigger OTA update via MQTT

## Configuration

### Ethernet Settings
Edit [network_config.h](src/config/network_config.h#L8-L14):
```cpp
static IPAddress static_ip(192, 168, 1, 100);
static IPAddress gateway(192, 168, 1, 1);
static IPAddress subnet(255, 255, 255, 0);
static IPAddress dns(8, 8, 8, 8);
```

### MQTT Settings
Edit [network_config.h](src/config/network_config.h#L17-L27):
```cpp
static constexpr const char* MQTT_SERVER = "192.168.1.50";
static constexpr uint16_t MQTT_PORT = 1883;
static constexpr const char* MQTT_TOPIC_DATA = "battery/data";
static constexpr const char* MQTT_TOPIC_STATUS = "battery/status";
```

### Task Timing
Edit [task_config.h](src/config/task_config.h#L16-L23):
```cpp
static constexpr uint32_t ESPNOW_SEND_INTERVAL_MS = 2000;    // Data send interval
static constexpr uint32_t DISCOVERY_INTERVAL_MS = 5000;      // Announcement interval
static constexpr uint32_t MQTT_PUBLISH_INTERVAL_MS = 5000;   // Telemetry interval
```

## Building

```bash
cd espnowtransmitter2
pio run
```

## Uploading

```bash
pio run --target upload
```

## Monitoring

```bash
pio device monitor
```

## Code Reduction

| Metric | Original | Modular | Improvement |
|--------|----------|---------|-------------|
| main.cpp | 866 lines | 150 lines | **82% reduction** |
| Files | 1 | 19 | Better separation |
| Testability | Poor | Excellent | Singleton mocking |
| Magic numbers | ~20 | 0 | All named constants |
| Global variables | ~15 | 1 (queue) | Clean encapsulation |

## Dependencies

### Local Libraries (../../esp32common)
- `espnow_transmitter`: Core ESP-NOW protocol implementation
- `ethernet_utilities`: Ethernet helper functions

### PlatformIO Libraries
- `PubSubClient`: MQTT client
- `ArduinoJson`: JSON serialization
- `ESPAsyncWebServer`: Async HTTP server for OTA

## Comparison with Original

### Original Architecture
- Single 866-line main.cpp
- Global variables scattered throughout
- Magic numbers (2000, 5000, 4096, etc.)
- Difficult to test individual components
- Hard to navigate and maintain

### Modular Architecture
- 19 focused files (average 50 lines each)
- Singleton pattern for clean access
- Named constants in config headers
- Each module independently testable
- Easy to navigate and understand

## Future Improvements

1. **Unit Testing**: Add GoogleTest framework for module testing
2. **State Machine**: Extract discovery/transmission state logic
3. **Logging**: Add structured logging with levels (DEBUG, INFO, WARN, ERROR)
4. **Metrics**: Add performance counters (messages/sec, latency)
5. **Web UI**: Add web dashboard for configuration and monitoring

## License

See parent project LICENSE file.
