
# PROJECT GUIDELINES  
## Coding Standards, Architecture Rules & Best Practices  
**Version:** 1.0  
**Purpose:** Ensure all firmware in this repository is fast, readable, reliable, and maintainable using modern embedded‑systems best practices.

---

# 1. Project Philosophy

- **Clarity over cleverness:** Readability and intent are more important than “smart” code.  
- **One responsibility per module.**  
- **Never block critical tasks.**  
- **No work inside ISRs except queueing data.**  
- **Consistent naming and file structure.**  
- **ESP‑NOW protocol must remain stable and versioned.**  

---

# 2. Repository Structure

```
ESP32Projects/                          # Workspace root
├── esp32common/                        # Shared libraries and utilities
│   ├── docs/
│   │   └── project guidlines.md        # This file
│   ├── espnow_transmitter/             # ESP-NOW transmitter library
│   │   ├── espnow_common.h             # Protocol definitions
│   │   ├── espnow_transmitter.h
│   │   ├── espnow_transmitter.cpp
│   │   └── library.json
│   ├── espnow_common_utils/            # Common message routing/handling
│   │   ├── espnow_discovery.h/cpp      # Discovery protocol
│   │   ├── espnow_message_router.h/cpp # Message routing system
│   │   ├── espnow_packet_utils.h       # Packet utilities
│   │   ├── espnow_peer_manager.h/cpp   # Peer management
│   │   ├── espnow_standard_handlers.h/cpp # Standard PROBE/ACK handlers
│   │   └── library.json
│   ├── webserver/                      # Shared web UI components
│   │   ├── advanced_battery_html.h/cpp
│   │   └── ... (other web pages)
│   └── ethernet_config.h               # Shared Ethernet configuration
│
├── espnowreciever_2/                   # ESP-NOW Receiver (T-Display-S3)
│   ├── platformio.ini
│   ├── src/
│   │   ├── main.cpp
│   │   ├── common.h                    # Global definitions
│   │   ├── globals.cpp                 # Global state
│   │   ├── helpers.h/cpp               # Utility functions
│   │   ├── state_machine.h/cpp         # State management
│   │   ├── config/                     # Configuration modules
│   │   │   ├── wifi_setup.h/cpp
│   │   │   └── littlefs_init.h/cpp
│   │   ├── display/                    # Display management
│   │   │   ├── display_core.h/cpp      # Core display functions
│   │   │   ├── display_led.h/cpp       # LED indicator
│   │   │   └── display_splash.h/cpp    # Splash screen
│   │   ├── espnow/                     # ESP-NOW protocol
│   │   │   ├── espnow_callbacks.h/cpp  # ESP-NOW callbacks
│   │   │   └── espnow_tasks.h/cpp      # Message handling
│   │   └── test/                       # Test data generation
│   │       └── test_data.h/cpp
│   ├── lib/
│   │   └── webserver/                  # Web server module
│   └── data/
│       └── webserver/                  # Web UI assets
│
└── ESPnowtransmitter2/
    └── espnowtransmitter2/             # ESP-NOW Transmitter (Olimex ESP32-POE)
        ├── platformio.ini
        ├── src/
        │   ├── main.cpp
        │   ├── config/                 # Configuration headers
        │   │   ├── hardware_config.h   # ETH PHY pins
        │   │   ├── network_config.h    # MQTT/NTP config
        │   │   └── task_config.h       # FreeRTOS config
        │   ├── network/                # Network modules
        │   │   ├── ethernet_manager.h/cpp
        │   │   ├── mqtt_manager.h/cpp
        │   │   ├── mqtt_task.h/cpp
        │   │   └── ota_manager.h/cpp
        │   └── espnow/                 # ESP-NOW modules
        │       ├── message_handler.h/cpp
        │       ├── discovery_task.h/cpp
        │       └── data_sender.h/cpp
        └── lib/
            └── ethernet_utilities/     # Ethernet helper functions
```

**Rules:**

- Shared logic exists only in `esp32common/`.
- No duplicated code between projects - make code common/reusable.
- Each firmware project has its own `platformio.ini`.
- Web UI code is separate and static by default.
- Include guards required for all header files.
- Use `lib_extra_dirs` in platformio.ini to reference `esp32common/`.
- Do not use serial.println/f's always use the debugging function preferably the mqtt version.

---

# 3. CODING STYLE (C / C++)

## 3.1 Naming Conventions

| Category | Style | Example |
|---------|--------|----------|
| Types | `snake_case_t` | `espnow_msg_t` |
| Struct Types | `snake_case_t` | `device_state_t` |
| Functions | `snake_case()` | `start_webserver()` |
| Variables | `snake_case` | `last_update_time` |
| Constants | `UPPER_SNAKE_CASE` | `MAX_ESPNOW_PAYLOAD` |
| Enums | `UpperCamelCase` | `PacketType::Ack` |
| Files | `snake_case` | `display_driver.cpp` |

---

## 3.2 Struct Rules

- Use this format exclusively:
```c
typedef struct {
    ...
} name_t;


3.3 Function Rules

One function = one responsibility.
Prefer short functions.
Use meaningful names.
Use structured return values (bool, esp_err_t).
Avoid long parameter lists — define small structs instead.

4. PERFORMANCE & RELIABILITY
4.1 ESP‑NOW + Wi‑Fi Coexistence

ESP‑NOW and Wi‑Fi must share the same channel.
In STA mode, the router determines the channel.
ESP‑NOW senders use channel hopping to find the receiver.
Should the router change channels, nodes will re‑sync automatically.

4.2 Data Handling Rules

Always include a sequence number in ESP‑NOW packets.
Implement app‑layer ACK: receiver echoes sequence number.
Validate all packet sizes and types.

5. FREE RTOS TASK ARCHITECTURE
5.1 Required Tasks


5.2 Task Rules

No direct cross‑task calls. Use queues or message buffers.
Avoid blocking long delays (delay(2000) etc).
Protect shared state with mutexes.
Keep stack sizes modest; avoid large allocations.

6. ISR (INTERRUPT) GUIDELINES
Allowed in ISR (including ESP‑NOW receive callback):

Copy fixed-size data
Push to queue/ringbuffer (xQueueSendFromISR)
Set atomic flags
Return immediately

Forbidden in ISR:

Logging
Dynamic memory (malloc/free/new/delete)
ESP‑NOW send
Wi‑Fi calls
JSON parsing or string manipulation
Display writes
SPI / I2C
Any blocking or waiting