# Architecture Diagrams: Transmitter vs Receiver Split

## Current Architecture (AFTER Cleanup)

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│          ESP-NOW Transmitter Device (4 MB Flash)              │
│                                                                 │
│  ┌──────────────────┐        ┌──────────────────┐             │
│  │  Battery Data    │        │   Data Sender    │             │
│  │  (CAN from BE)   ├───────┤  (ESP-NOW / MQTT)│             │
│  │                  │        │                  │             │
│  │ 96 Cells @ 3.5V  │        │ Periodic Beacon  │             │
│  │ Temp, Current    │        │ Battery Packet   │             │
│  │ SOC, Health      │        │ Cell Voltages    │             │
│  └──────────────────┘        └────────┬─────────┘             │
│                                       │                        │
│                    ┌──────────────────┼──────────────────┐    │
│                    │                  │                  │    │
│              ┌─────▼────┐      ┌──────▼────┐      ┌──────▼────┐
│              │ ESP-NOW  │      │   MQTT    │      │   HTTP    │
│              │   TX     │      │ Publisher │      │   OTA     │
│              │          │      │           │      │           │
│              │ Data to  │      │ Telemetry │      │ /ota_upload
│              │ Receiver │      │ to Broker │      │ /api/fw_info
│              └────┬─────┘      └──────┬────┘      └──────┬────┘
│                   │                   │                  │
│                   │             192.168.1.x        192.168.1.x
│                   │          (MQTT Broker)        (OTA Client)
│                   │                                      │
│  ┌────────────────┴──────────────────────────────────────┤
│  │ Ethernet (W5500 via SPI)                              │
│  └─────────────────────────────────────────────────────────┘
│
│ Dependencies:
│  ✅ PubSubClient (MQTT)
│  ✅ ArduinoJson (JSON)
│  ✅ ESP32Ping (Network test)
│  ✅ MCP2515 driver (CAN)
│  ✅ esp_http_server (native OTA)
│  ✅ Framework FS/Preferences
│
│ ❌ REMOVED:
│  ✗ ESPAsyncWebServer
│  ✗ AsyncTCP
│  ✗ ElegantOTA
│  ✗ eModbus
│
└─────────────────────────────────────────────────────────────────┘
```

## Receiver Device (Webserver - 16 MB Flash)

```
┌──────────────────────────────────────────────────────────────────┐
│                                                                  │
│          Receiver/Webserver Device (16 MB Flash)               │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │           HTTP Webserver UI                               │ │
│  │  ┌─────────────┐  ┌─────────────┐  ┌──────────────────┐ │ │
│  │  │  Dashboard  │  │  Settings   │  │  Firmware Update │ │ │
│  │  │             │  │             │  │  (ElegantOTA UI) │ │ │
│  │  │ Battery     │  │ Thresholds  │  │                  │ │ │
│  │  │ Monitor     │  │ Inverter    │  │ File selector    │ │ │
│  │  │             │  │ Charger     │  │ Progress bar     │ │ │
│  │  └──────┬──────┘  └──────┬──────┘  └────────┬─────────┘ │ │
│  │         │                │                 │           │ │
│  │         └────────────────┼─────────────────┘           │ │
│  │                          │                             │ │
│  │      ┌───────────────────▼──────────────────┐          │ │
│  │      │  ESPAsyncWebServer + AsyncTCP        │          │ │
│  │      │  (Full async HTTP framework)         │          │ │
│  │      │  - WebSockets for live updates       │          │ │
│  │      │  - Server-sent events (SSE)          │          │ │
│  │      │  - Heavy lifting for UI              │          │ │
│  │      └──────────────────┬─────────────────┘          │ │
│  │                         │                            │ │
│  └─────────────────────────┼────────────────────────────┘ │
│                            │                                │
│        ┌───────────────────┼───────────────────┐           │
│        │                   │                   │           │
│   ┌────▼────┐         ┌────▼─────┐      ┌─────▼────┐      │
│   │ MQTT    │         │ Database  │      │ Config   │      │
│   │ Client  │         │ (Events)  │      │ Storage  │      │
│   │         │         │           │      │ (NVS)    │      │
│   └────┬────┘         └───────────┘      └─────────┘      │
│        │                                                    │
│        │ 192.168.1.100:1883                               │
│        │ (MQTT broker)                                    │
│        │                                                    │
│  ┌─────▼────────────────────────────────────┐             │
│  │   Ethernet Interface (Ethernet shield)    │             │
│  └─────────────────────────────────────────┘             │
│                                                            │
│  Dependencies:                                           │
│   ✅ ESPAsyncWebServer (Full HTTP server)               │
│   ✅ AsyncTCP (Async TCP layer)                         │
│   ✅ ElegantOTA (Web-based OTA UI)                      │
│   ✅ ArduinoJson (JSON)                                 │
│   ✅ PubSubClient (MQTT client)                         │
│   ✅ All webserver/display libraries                    │
│                                                            │
└──────────────────────────────────────────────────────────────┘
```

## Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────┐
│         Data Flow: Battery Emulator → Transmitter           │
└─────────────────────────────────────────────────────────────┘

Battery Emulator
    │
    │ CAN Bus (500 kbps)
    │ - Cell voltages
    │ - Pack voltage/current
    │ - Temperature
    │ - SOC, Health
    │
    ▼
┌──────────────────────────────┐
│ Transmitter Device           │
│ ┌────────────────────────┐   │
│ │ CAN Receiver (MCP2515) │   │
│ └────────────┬───────────┘   │
│              │                │
│              ▼                │
│ ┌──────────────────────────┐ │
│ │ Datalayer (RAM buffer)   │ │
│ │ - number_of_cells = 96   │ │
│ │ - cell_voltages[]        │ │
│ │ - pack voltage/current   │ │
│ └────────┬─────────────────┘ │
│          │                    │
│          ▼                    │
│ ┌──────────────────────────┐ │
│ │ Data Serializers         │ │
│ │ ┌─────────────────────┐  │ │
│ │ │ ESP-NOW Packet      │  │ │
│ │ │ (48-byte max)       │  │ │
│ │ └─────────────────────┘  │ │
│ │ ┌─────────────────────┐  │ │
│ │ │ MQTT JSON Payload   │  │ │
│ │ │ (Full data)         │  │ │
│ │ └─────────────────────┘  │ │
│ └────────┬─────────────────┘ │
│          │                    │
└──────────┼────────────────────┘
           │
    ┌──────┴──────┬──────────────┐
    │             │              │
    │             │              │
    ▼             ▼              ▼
ESP-NOW       MQTT Broker    OTA Server
(Receiver)    (Dashboard)    (Updates)
```

## Component Responsibility Matrix

```
┌──────────────────────────────────────────────────────────────────┐
│  Component                │ Transmitter | Receiver | Battery Emu  │
├──────────────────────────────────────────────────────────────────┤
│ Battery Data Collection   │     NO      │    NO    │     YES      │
│ CAN Bus Interface         │     YES     │    NO    │     YES      │
│ Data Buffering            │     YES     │    NO    │     NO       │
│ ESP-NOW TX                │     YES     │    NO    │     NO       │
│ ESP-NOW RX                │     NO      │    YES   │     NO       │
│ MQTT Publishing           │     YES     │    NO    │     NO       │
│ MQTT Subscription         │     NO      │    YES   │     NO       │
│ HTTP Webserver UI         │     NO      │    YES   │     NO       │
│ Web-based OTA             │     NO      │    YES   │     NO       │
│ Binary OTA (via HTTP POST)│     YES     │    YES   │     NO       │
│ Database/Logging          │     NO      │    YES   │     NO       │
│ Device Configuration      │     MIN     │    YES   │     YES      │
│ Network Management        │     YES     │    YES   │     NO       │
└──────────────────────────────────────────────────────────────────┘
```

## Library Dependency Tree

### TRANSMITTER (After Cleanup)
```
transmitter/
├── Core
│   ├── ESP-NOW (framework built-in)
│   ├── Ethernet (W5500 via SPI)
│   └── CAN Bus (MCP2515 via SPI)
│
├── Data Handling
│   ├── ArduinoJson ✅
│   ├── Datalayer (custom)
│   └── Serializers (custom)
│
├── Connectivity
│   ├── PubSubClient (MQTT) ✅
│   ├── ESP32Ping ✅
│   └── NTP (framework)
│
├── Firmware Updates
│   └── esp_http_server (native)
│       └── Update library (native)
│
└── Utilities
    ├── Preferences (NVS)
    ├── FS (file system)
    └── Logging (custom)
```

### RECEIVER (Webserver - Unchanged)
```
receiver/
├── Core
│   ├── Ethernet (GPIO interface)
│   └── WiFi (optional)
│
├── Web Framework
│   ├── ESPAsyncWebServer ✅
│   └── AsyncTCP ✅
│
├── Web UI Features
│   ├── Dashboard (HTML/CSS/JS)
│   ├── Settings panel
│   ├── Monitoring graphs
│   └── Configuration tools
│
├── Firmware Updates
│   ├── ElegantOTA ✅ (Web UI)
│   └── Update library (native)
│
├── Connectivity
│   ├── PubSubClient (MQTT) ✅
│   └── ArduinoJson ✅
│
└── Data Storage
    ├── Database (custom)
    ├── NVS (settings)
    └── SPIFFS (web assets)
```

## Clean Separation After Cleanup

```
Before Cleanup:
┌─────────────────────────────────────────┐
│ Transmitter                             │
│                                         │
│ (Correct code)                          │
│ + (Unused webserver libraries) ← BAD   │
│ = Confusion, bloat, build failures      │
└─────────────────────────────────────────┘

After Cleanup:
┌──────────────────┐        ┌──────────────────┐
│ Transmitter      │        │ Receiver         │
│                  │        │                  │
│ ✅ OTA           │        │ ✅ WebUI         │
│ ✅ ESP-NOW       │        │ ✅ Web OTA       │
│ ✅ MQTT          │        │ ✅ Dashboard     │
│ ✅ CAN           │        │ ✅ Config panel  │
│                  │        │                  │
│ 100% transmit    │        │ 100% webserver   │
│ focused          │        │ focused          │
└──────────────────┘        └──────────────────┘
```

## Compilation Pipeline (After Cleanup)

```
Source Code
    │
    ├─ platformio.ini (clean deps)
    ├─ src/**/*.cpp (transmitter code)
    ├─ src/battery_emulator/** (no embedded libs)
    │
    ▼
PlatformIO Builder
    │
    ├─ Include lib_deps only
    │  ├─ PubSubClient
    │  ├─ ArduinoJson
    │  ├─ ESP32Ping
    │  ├─ MCP2515
    │  └─ Framework libs
    │
    ├─ ✗ Skip embedded libs (deleted)
    │
    ▼
Compiler/Linker
    │
    ├─ Compile transmitter code
    ├─ Compile required libraries
    ├─ Link all objects
    │
    ▼
Output (1.2 MB)
    ├─ transmitter.bin
    ├─ transmitter.elf
    └─ transmitter.map

Result: ✅ Clean, fast, no conflicts
```

---

**These diagrams show the correct architecture after cleanup.**
**The transmitter is a focused data forwarder, not a full webserver.**
