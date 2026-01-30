# ESP32 Multi-Project Repository

This repository contains multiple ESP32 projects that work together using ESP-NOW protocol.

## Projects

### 1. ESP32 Common (`esp32common/`)
Common libraries and utilities shared across all ESP32 projects:
- **espnow_common_utils/**: ESP-NOW protocol utilities (discovery, message routing, peer management)
- **espnow_transmitter/**: Transmitter-specific ESP-NOW code
- **webserver/**: Common web server components and HTML pages

### 2. ESP-NOW Receiver (`espnowreciever_2/`)
Receiver device that collects data from transmitters via ESP-NOW and displays it via a web interface.

**Features:**
- ESP-NOW data reception and processing
- Web server with monitoring interface
- TFT display support
- State machine architecture

### 3. ESP-NOW Transmitter (`espnowtransmitter2/`)
Transmitter device that sends data via ESP-NOW and publishes to MQTT.

**Features:**
- ESP-NOW data transmission
- MQTT client support
- Ethernet connectivity
- OTA updates

## Configuration

Before building any project, you need to configure your credentials:

### WiFi Configuration (Receiver)
Edit `espnowreciever_2/src/globals.cpp`:
```cpp
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
```

### MQTT Configuration (Transmitter)
Edit `espnowtransmitter2/src/config/network_config.h`:
```cpp
const char* username{"YOUR_MQTT_USERNAME"};
const char* password{"YOUR_MQTT_PASSWORD"};
```

## Building

All projects use PlatformIO. To build:

```bash
cd [project_directory]
pio run
```

To upload:
```bash
pio run --target upload
```

## Hardware

- **Receiver**: LILYGO T-Display S3
- **Transmitter**: ESP32-POE-ISO (with Ethernet)

## License

See individual project directories for license information.
