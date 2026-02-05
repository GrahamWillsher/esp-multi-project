# ESP32 Projects

Multi-workspace project containing ESP-NOW communication system for ESP32 devices.

## Project Structure

### ESP32 Common
Shared libraries and utilities used across all ESP32 projects:
- **config_sync**: Configuration synchronization between devices
- **espnow_common_utils**: Common ESP-NOW utilities
- **logging_utilities**: MQTT and serial logging
- **webserver**: Web interface components

### ESPnowtransmitter2
ESP32-POE-ISO based transmitter device:
- Ethernet connectivity
- ESP-NOW transmitter
- MQTT telemetry publishing
- OTA updates

### espnowreciever_2
LilyGo T-Display-S3 based receiver device:
- WiFi connectivity
- ESP-NOW receiver
- TFT display interface
- Web-based configuration
- OTA updates

## Configuration

Before building, update the following configuration files with your network credentials:

### Network Configuration
Edit `ESPnowtransmitter2/espnowtransmitter2/src/config/network_config.h`:
```cpp
const char* server{"YOUR_MQTT_BROKER_IP"};
const char* username{"YOUR_MQTT_USERNAME"};
const char* password{"YOUR_MQTT_PASSWORD"};
```

Edit `esp32common/ethernet_config.h` for static IP settings if needed.

## Building

This project uses PlatformIO. To build:

```bash
cd espnowreciever_2
pio run

cd ../ESPnowtransmitter2/espnowtransmitter2
pio run
```

## Firmware Versioning

Firmware files are automatically versioned using the format:
```
<environment>_fw_<MAJOR>_<MINOR>_<PATCH>.bin
```

Example: `lilygo-t-display-s3_fw_1_0_1.bin`

Version numbers are set in platformio.ini build flags:
```ini
-D FW_VERSION_MAJOR=1
-D FW_VERSION_MINOR=0
-D FW_VERSION_PATCH=1
```

## Features

- **ESP-NOW Communication**: Peer-to-peer wireless communication
- **OTA Updates**: Over-the-air firmware updates via web interface
- **MQTT Integration**: Telemetry publishing to MQTT broker
- **Configuration Sync**: Automatic config synchronization between devices
- **Web Interface**: Configuration and monitoring via web browser
- **Display Support**: TFT display for receiver status

## Hardware

- **Transmitter**: Olimex ESP32-POE-ISO
- **Receiver**: LilyGo T-Display-S3

## License

[Add your license here]
