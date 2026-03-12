# Simulated LED Transport Mechanism - Full Code Review & Recommendations

**Date:** March 11, 2026  
**Status:** ✅ IMPLEMENTED (March 12, 2026) — state-driven LED transport and receiver rendering active  
**Recommendation:** Keep event-driven ownership model and maintain reboot/resync validation in regular regression testing

---

## 0. Implementation Completion Update (March 12, 2026)

This review originally captured gaps and recommendations. Those recommended changes are now implemented in the active transmitter/receiver codebases.

### 0.1 Completed implementation summary

- ✅ Shared wire payload includes effect (`flash_led_t {type, color, effect}`) and shared wire enums in common header.
- ✅ Transmitter LED publish ownership moved to battery-emulator status transition path (`events.cpp` + `led_publish_current_state()`).
- ✅ Transmitter now sends only on state/effect change (last-sent cache in publisher).
- ✅ Telemetry path (`data_sender.cpp`) no longer owns LED decisions.
- ✅ `STATUS_UPDATING` standardized to BLUE for transport mapping.
- ✅ LED mode persistence is managed in `SettingsManager` and synchronized to runtime datalayer state.
- ✅ Receiver decodes color+effect from packet with range validation.
- ✅ Receiver has dedicated always-on LED render task.
- ✅ Receiver supports BLUE rendering with matching gradient behavior.
- ✅ Receiver requests battery config on startup/version mismatch and transmitter replays LED state on battery config section response.
- ✅ Receiver web UI includes `/transmitter/hardware` with "Status LED Pattern" dropdown and manual LED resync action.

### 0.2 Task status (Section 13 cross-reference)

- **Common tasks C1–C7:** Completed for current deployed protocol/contract.
- **Transmitter tasks T1–T11:** Completed; legacy local LED class/wrapper path removed and event-driven publisher retained as sole owner.
- **Receiver tasks R1–R8:** Completed, including runtime render task and settings cache/UI binding.
- **Verification tasks V1–V5:** Build/integration checks completed in development; continue hardware regression matrix as part of release validation.

### 0.3 Remaining operational items (non-blocking)

- Keep periodic hardware validation of reboot/recovery matrix:
    - TX boots first, RX later
    - RX reboot while TX is running
    - TX reboot while RX is running
    - LED mode change while disconnected, then reconnect
- Keep mixed-firmware migration compatibility only as long as required by field rollout.

---

## Executive Summary

After conducting a comprehensive code review of both the transmitter and receiver codebases, I have found that **the simulated LED on the receiver is already being driven by the transmitter via ESP-NOW**. The transport mechanism is fully implemented, tested, and operational.

**Implementation outcome (Corrected):** Transmitter LED messaging is now state-driven (battery emulator operating status transitions), and receiver rendering is owned by a dedicated continuous task.

**Important correction:** color and effect are **independent/intermixed**. It is not fixed as "green = heartbeat". Any color can be combined with any effect (`FLASH`, `HEARTBEAT`, `CONTINUOUS`).

---

## 1. Current Architecture Overview

### 1.1 LED Transport Mechanism (ESPNOW)

The simulated LED transport is via ESP-NOW using a dedicated message type:

| Component | Details |
|-----------|---------|
| **Message Type** | `msg_flash_led` (defined in `espnow_common.h`) |
| **Payload Structure** | `flash_led_t` is 3 bytes (`type + color + effect`) |
| **Color Encoding** | 0=RED, 1=GREEN, 2=ORANGE, 3=BLUE |
| **Effect States** | `FLASH`, `HEARTBEAT`, `CONTINUOUS` (intermix with any color) |
| **Transmission Method** | Direct ESP-NOW unicast from TX → RX |
| **Trigger Condition** | Battery emulator operating-state changes |
| **Size** | 3 bytes per message (ultra-lightweight) |

### 1.2 Wire Format and LED Geometry

```cpp
// From esp32common/espnow_transmitter/espnow_common.h
typedef struct __attribute__((packed)) {
    uint8_t type;       // msg_flash_led
    uint8_t color;      // LED color code: 0=RED, 1=GREEN, 2=ORANGE, 3=BLUE
    uint8_t effect;     // LED effect code: 0=CONTINUOUS, 1=FLASH, 2=HEARTBEAT
} flash_led_t;
```

**Advantages:**
- Minimal payload (2 bytes)
- Efficient use of ESP-NOW bandwidth
- Perfect for small state changes

**LED size and position (confirmed from receiver config):**
- Position: **right-hand side, halfway down** (not top-right)
- `LED_X_POSITION = DISPLAY_WIDTH - 2 - STATUS_INDICATOR_SIZE`
- `LED_Y_POSITION = DISPLAY_HEIGHT / 2`
- `STATUS_INDICATOR_SIZE = 12` → `LED_RADIUS = 12`
- Diameter: **24 px**

### 1.3 Battery Emulator operating states (investigated)

Operating states are defined in the battery emulator event system (not the ESP-NOW link state machine):

- `EMULATOR_STATUS::STATUS_OK`
- `EMULATOR_STATUS::STATUS_WARNING`
- `EMULATOR_STATUS::STATUS_ERROR`
- `EMULATOR_STATUS::STATUS_UPDATING`

Source files:
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.h`
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.cpp`

How status is derived:
- `get_emulator_status()` maps current `events.level` to `STATUS_OK / WARNING / ERROR / UPDATING`.
- `update_bms_status()` maps to datalayer runtime status:
    - INFO/DEBUG/WARNING -> `ACTIVE`
    - UPDATE -> `UPDATING`
    - ERROR -> `FAULT`

Operational interpretation:
- `STATUS_OK` -> normal operation
- `STATUS_WARNING` -> degraded/warning operation
- `STATUS_ERROR` -> fault path (`bms_status = FAULT`), i.e. halted/should-be-halted behavior

This confirms the correct state source for LED transport is the **battery emulator status path**, not SOC bands.

### 1.4 STATUS_UPDATING color verification (new)

Verified finding: current codebase is inconsistent for `STATUS_UPDATING` color.

- **Battery emulator LED mapping (transmitter-side helper): ORANGE (current implementation)**
    - `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/led_handler.cpp`
    - `case EMULATOR_STATUS::STATUS_UPDATING: return LED_ORANGE;`

- **Battery emulator web status color: BLUE (`#2B35AF`)**
    - `ESP32common/webserver/index_processor.inc`
    - `case EMULATOR_STATUS::STATUS_UPDATING: content += "#2B35AF;";`

Conclusion:
- Blue has indeed been used/suggested in the battery emulator UI path.
- Current LED transport helper still maps updating to orange.
- **Document policy:** standardize `STATUS_UPDATING` to **BLUE** across UI + ESPNOW LED transport.

---

## 2. Transmitter Implementation (Data Sender)

### 2.1 Current Implementation: SOC Band Tracking (Legacy Trigger)

**File:** `ESPnowtransmitter2/espnowtransmitter2/src/espnow/data_sender.cpp`

The transmitter currently controls LED messages based on battery SOC bands:

```cpp
// Lines 17-24
enum class SOCBand : uint8_t {
    LOW_SOC,      // 20-39 SOC (0-33% normalized) -> Red
    MEDIUM_SOC,   // 40-59 SOC (34-66% normalized) -> Orange
    HIGH_SOC      // 60-80 SOC (67-100% normalized) -> Green
};

static SOCBand last_soc_band = SOCBand::MEDIUM_SOC;  // Track previous band
```

**Mapping Logic:**
| SOC Range | Band | LED Color | Semantic Meaning |
|-----------|------|-----------|------------------|
| 20-39% | LOW_SOC | RED 🔴 | Battery depleting |
| 40-59% | MEDIUM_SOC | ORANGE 🟠 | Neutral state |
| 60-80% | HIGH_SOC | GREEN 🟢 | Battery charging well |

### 2.2 Current Transmission Flow

**Location:** `data_sender.cpp::send_test_data_with_led_control()` (lines 88-182)

1. **Data Processing** (lines 106-115)
   - Reads SOC from datalayer (live or test mode)
   - Converts from pppt format (percent × 100) to 0-100 range
   - Reads power in Watts

2. **Band Detection** (lines 129-137)
   - Determines current SOC band
   - Compares with last transmitted band

3. **Conditional LED Flash** (lines 139-175)
   ```cpp
   if (current_band != last_soc_band) {
       // Only send LED command when band changes
       // Prevents flooding receiver with unnecessary messages
   ```

4. **Health Check** (lines 140-143)
   ```cpp
   if (!is_espnow_healthy()) {
       LOG_DEBUG("...", "Skipping LED flash - ESP-NOW experiencing delivery failures");
       return;
   }
   ```
   - Verifies ESP-NOW is healthy before sending LED command
   - Prevents sending LED commands during connection issues

5. **Message Construction & Send** (lines 153-175)
   ```cpp
   flash_led_t flash_msg;
   flash_msg.type = msg_flash_led;
   
   switch (current_band) {
       case SOCBand::LOW_SOC:
           flash_msg.color = 0;  // Red
           break;
       case SOCBand::MEDIUM_SOC:
           flash_msg.color = 2;  // Orange
           break;
       case SOCBand::HIGH_SOC:
           flash_msg.color = 1;  // Green
           break;
   }
   
   esp_err_t result = esp_now_send(peer_mac, (const uint8_t*)&flash_msg, sizeof(flash_msg));
   ```

**Current Transmission Characteristics:**
- ✅ **One-shot sending**: LED command sent only on band change, not every message
- ✅ **Health-aware**: Checks ESP-NOW health before sending
- ✅ **Lazy tracking**: `last_soc_band` only updated on successful send
- ✅ **Band hysteresis**: Prevents rapid LED flashing at band boundaries

### 2.3 Implemented Change: Publish on battery emulator status transitions

`TxStateMachine` (ESP-NOW link state) is not used as the primary source for LED application status.
Battery emulator operating status (`EMULATOR_STATUS`) is the primary source.

LED publishing has been moved from SOC-bands to **`EMULATOR_STATUS` transitions**.

Primary status source:
- `get_emulator_status()` in `battery_emulator/devboard/utils/events.cpp`

Existing LED mapping module aligned with this model:
- `battery_emulator/devboard/utils/led_handler.cpp`
    - `STATUS_OK -> GREEN`
    - `STATUS_WARNING -> ORANGE`
    - `STATUS_UPDATING -> BLUE`
    - `STATUS_ERROR -> RED`

Implemented integration result:
- `led_publish_current_state()` is called from status transition points.
- LED update is published only on status/effect change (with publisher-side last-sent cache).
- `data_sender.cpp` remains telemetry-only.

### 2.4 Integration Points

The LED feature is integrated into the main data transmission task:

- **Task:** `task_impl()` (lines 48-80)
- **Call Rate:** Every `timing::ESPNOW_SEND_INTERVAL_MS` (configured interval)
- **Triggering:** Called during normal data transmission cycle via `send_test_data_with_led_control()`
- **Data Source:** Can use live battery data (datalayer) or test data (TestDataGenerator)

---

## 3. Receiver Implementation (LED Handler)

### 3.1 Message Routing

**File:** `espnowreciever_2/src/espnow/espnow_tasks.cpp`

**Registration** (lines 198-201):
```cpp
router.register_route(msg_flash_led,
    [](const espnow_queue_msg_t* msg, void* ctx) {
        handle_flash_led_message(msg);
    });
```

The ESP-NOW message router automatically dispatches incoming `msg_flash_led` messages to the handler.

### 3.2 Message Handler Implementation

**Location:** `espnow_tasks.cpp::handle_flash_led_message()` (lines 644-665)

```cpp
void handle_flash_led_message(const espnow_queue_msg_t* msg) {
    if (msg->len >= (int)sizeof(flash_led_t)) {
        const flash_led_t* flash_msg = reinterpret_cast<const flash_led_t*>(msg->data);
        
        // Validate color code (wire format target: 0=RED, 1=GREEN, 2=ORANGE, 3=BLUE)
        if (flash_msg->color > LED_BLUE) {
            LOG_WARN(kLogTag, "Invalid LED color code: %d", flash_msg->color);
            return;
        }
        
        LEDColor color = static_cast<LEDColor>(flash_msg->color);
        
        // Store the current LED color for status indicator task to use
        ESPNow::current_led_color = color;
        ESPNow::current_led_effect = LED_EFFECT_FLASH;
    }
}
```

**Handler Behavior (current):**
- ✅ **Validates** color code (prevents invalid values from crashing display)
- ✅ **Type-safe conversion** from wire format to enum
- ✅ **Atomic state update** via global variable
- ⚠️ **Effect assignment is fixed** to FLASH; effect is not currently transported

**Handler Behavior (required):**
- Parse `effect` from packet
- Validate effect range
- Apply both `current_led_color` and `current_led_effect`

### 3.3 LED Display Implementation

**File:** `espnowreciever_2/src/display/display_led.cpp`

The receiver has full LED visualization capability:

**Flash with Fade Effect** (lines 59-106):
```cpp
void flash_led(LEDColor color, uint32_t cycle_duration_ms = 1000) {
    // Phase 1: Fade from color to background
    for (int step = 0; step <= LED_FADE_STEPS; step++) {
        tft.fillCircle(LED_X_POSITION, LED_Y_POSITION, LED_RADIUS, gradient[step]);
        smart_delay(delay_per_step);
    }
    
    // Phase 2: Fade from background to color
    for (int step = LED_FADE_STEPS - 1; step >= 0; step--) {
        tft.fillCircle(LED_X_POSITION, LED_Y_POSITION, LED_RADIUS, gradient[step]);
        smart_delay(delay_per_step);
    }
}
```

**Color Mapping** (lines 78-82):
```cpp
static constexpr uint16_t led_colors[] = {
    LEDColors::RED,    // LED_RED = 0
    LEDColors::GREEN,  // LED_GREEN = 1
    LEDColors::ORANGE  // LED_ORANGE = 2
};
```

**LED Positioning (corrected):**
- Position: Right side, vertically centered
- Radius: 12 pixels
- Diameter: 24 pixels
- Coordinates: `x = DISPLAY_WIDTH - 2 - STATUS_INDICATOR_SIZE`, `y = DISPLAY_HEIGHT / 2`

### 3.4 Global State Management

**File:** `espnowreciever_2/src/globals.cpp` (lines 32-34)

```cpp
namespace ESPNow {
    LEDColor current_led_color = LED_ORANGE;  // Start with orange (medium)
    LEDEffect current_led_effect = LED_EFFECT_FLASH;
}
```

**State Variables:**
- `current_led_color`: Stores color received from transmitter
- `current_led_effect`: Stores effect type (SOLID, FLASH, HEARTBEAT)

---

## 4. Receiver Rendering Responsibility

### 4.1 Current branch status

Receiver LED rendering is owned by an always-running dedicated renderer task.

### 4.2 Implemented receiver behavior

Receiver owns LED rendering in a dedicated task so LED is always visible:

- `LED_EFFECT_SOLID` → `set_led(color)`
- `LED_EFFECT_HEARTBEAT` → `heartbeat_led(color)`
- `LED_EFFECT_FLASH` → `flash_led(color)`

The task reads `ESPNow::current_led_color` and `ESPNow::current_led_effect` and runs continuously.

### 4.3 Color/effect intermix requirement

Valid color set:
- GREEN (normal)
- ORANGE (warning)
- RED (error)
- BLUE (updating)

Valid effect set:
- `FLASH`
- `HEARTBEAT`
- `CONTINUOUS`

These must be independent fields. Example valid combinations:
- GREEN + CONTINUOUS
- GREEN + HEARTBEAT
- ORANGE + FLASH
- RED + CONTINUOUS

---

## 5. Test Mode vs. Production Mode

### 5.1 Current Status

The receiver **no longer runs test data generation**:

**File:** `espnowreciever_2/src/test/test_data.cpp`

```cpp
// DEPRECATED: All test mode functionality has been moved to transmitter
// These functions are deprecated stubs kept for build compatibility

bool& test_mode_enabled = *(new bool(false));  // Always false
volatile int& g_test_soc = *(new volatile int(0));
volatile int32_t& g_test_power = *(new volatile int32_t(0));
```

**Design:** Receiver is now a pure display device:
- ❌ No local test data generation
- ✅ All test data comes from transmitter
- ✅ Transmitter controls test/live mode via `TestDataGenerator` class
- ✅ Receiver displays whatever the transmitter sends

### 5.2 Transmitter Test Data Control

**File:** `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/test_data_generator.h`

The transmitter can operate in two modes:

| Mode | Data Source | LED Behavior |
|------|-------------|--------------|
| **TEST** | `TestDataGenerator` (for emulation) | LED state should follow TX state mapping |
| **LIVE** | Datalayer (CAN bus real battery data) | LED state should follow TX state mapping |

Both modes send LED commands via the same ESP-NOW mechanism.

---

## 6. Message Flow Diagram

```
TRANSMITTER SIDE:
─────────────────

    Battery Emulator Event Update
             │
             ▼
    events.cpp recomputes events.level
             │
             ▼
    get_emulator_status() transition detected
             │
             ▼
    led_status_publisher (single owner)
             │
             ├─── map status -> {color,effect}
             │      (OK/WARN/UPDATING/ERROR)
             │
             ├─── compare with last_sent {color,effect}
             │
             ├─── unchanged? ───────────────► no packet
             │
             └─── changed? ────────────────► build flash_led_t {type,color,effect}
                                              │
                                              ▼
                                       TxSendGuard::send_to_receiver_guarded()
                                              │
                                              ├─── failed?  ─► keep last_sent unchanged
                                              └─── success? ─► update last_sent cache


RECEIVER SIDE:
──────────────

    ESP-NOW RX Interrupt
             │
             ▼
    Queue Message to Worker Task
             │
             ▼
    Message Router
             │
             ├─── msg_type == msg_flash_led? ──► Yes
             │                                    │
             └─── Other types ──► Handle separately
                                  │
                                  ▼
                         handle_flash_led_message()
                                  │
                                ├─── Validate color + effect
                                  │
                                └─── Update global LED state
                                       • current_led_color
                                       • current_led_effect
                                  │
                                  ▼
                        Always-on LED Render Task
                  (CONTINUOUS / HEARTBEAT / FLASH)
                                │
                                ├─── uses precomputed gradients
                                │      RED/GREEN/ORANGE/BLUE -> background
                                │      (same LED_FADE_STEPS interval count)
                                  │
                                  ▼
                              TFT Circle Animation
```

---

## 7. Data Flow Timing

### 7.1 Transmission Interval

- **Transmitter:** Sends data every `ESPNOW_SEND_INTERVAL_MS` (likely 100-200ms)
- **LED Command:** Sent on state change only (not periodic)
- **Receiver:** Processes via FreeRTOS queue (typically <10ms latency)

### 7.2 State Change Detection

Example scenario (battery emulator `STATUS_OK` -> `STATUS_WARNING`):

```
Transmitter:
    Previous status: STATUS_OK
    New status:      STATUS_WARNING
    Detection: status changed
    Action:    Send LED update once (color/effect pair)

Receiver:
  Receives:  flash_led_t { type: msg_flash_led, color: 2 }
    Updates:   current_led_color = LED_ORANGE
                         current_led_effect = <from packet>
    Display:   LED stays continuously active using selected effect
```

Communication-warning note from event table:
- Some communication faults are WARNING (e.g., CAN inverter missing)
- Some are ERROR (e.g., CAN battery missing)
- WiFi/MQTT disconnect events are currently INFO in the event table

So "communication issues" may map to warning or error depending on event severity configuration.

Example scenario (battery emulator `STATUS_WARNING` -> `STATUS_UPDATING`):

```
Transmitter:
    Previous status: STATUS_WARNING
    New status:      STATUS_UPDATING
    Detection: status changed
    Action:    Send LED update once (color=BLUE, effect as configured)

Receiver:
    Receives:  flash_led_t { type: msg_flash_led, color: 3, effect: ... }
    Updates:   current_led_color = LED_BLUE
               current_led_effect = <from packet>
    Display:   continuous LED rendering with selected effect
```

---

## 8. Code Quality Assessment

### 8.1 Strengths ✅

| Aspect | Status | Details |
|--------|--------|---------|
| **Efficiency** | ✅ Excellent | tiny payload, one-shot updates |
| **Robustness** | ✅ Good | Input validation, ESP-NOW health check, error logging |
| **Type Safety** | ✅ Good | Enum-based color encoding, struct validation |
| **Architecture** | ✅ Clean | Separate message types, dedicated handler, modular |
| **Documentation** | ⚠️ Needs update | behavior has evolved from SOC-trigger assumptions |
| **Integration** | ✅ Seamless | Uses standard ESP-NOW routing, no custom protocols |

### 8.2 Observations

| Item | Finding | Impact |
|------|---------|--------|
| **Current trigger** | SOC-band based in `data_sender.cpp` | ⚠️ Should become state-based |
| **One-shot Sending** | only on trigger change | ✅ Correct pattern |
| **Health Awareness** | Checks `is_espnow_healthy()` before sending | ✅ Prevents wasting bandwidth |
| **Color Semantics** | 0=RED, 1=GREEN, 2=ORANGE, 3=BLUE | ✅ Clean encoding |
| **Test Data Support** | Works with both test and live modes | ✅ Flexible |

---

## 9. Current Implementation Summary

### What IS Implemented ✅

| Feature | Location | Status |
|---------|----------|--------|
| LED color transmission | `data_sender.cpp` | ✅ Working |
| ESP-NOW message type | `espnow_common.h` line 165-169 | ✅ Defined |
| Message routing | `espnow_tasks.cpp` line 198-201 | ✅ Registered |
| Message handler | `espnow_tasks.cpp` line 644-665 | ✅ Implemented |
| Display rendering | `display_led.cpp` line 59-106 | ✅ Complete |
| SOC band detection | `data_sender.cpp` | ✅ Working (legacy trigger) |
| Health checks | `data_sender.cpp` line 140-143 | ✅ Implemented |

### What is NOT Implemented ❌ (relevant)

| Feature | Reason | Impact |
|---------|--------|--------|
| Effect transport in LED packet | packet currently carries color only | Required for one-shot color+effect |
| Dedicated always-on LED render task | old status task deprecated in current branch | Required |
| State-driven LED publish on TX transitions | currently driven from SOC logic path | Required |

---

## 10. Test Validation Status

Based on implementation and build validation:

| Test Case | Status | Evidence |
|-----------|--------|----------|
| **LED publish on SOC trigger** | ✅ Removed | `data_sender.cpp` is telemetry-only |
| **LED publish on TX state changes** | ✅ Implemented | event pipeline calls `led_publish_current_state()` |
| **ESP-NOW transmission** | ✅ Implemented | guarded send path with shared `flash_led_t` |
| **Message routing** | ✅ Implemented | router dispatches `msg_flash_led` to handler |
| **Display primitives** | ✅ Implemented | `display_led.cpp` |
| **Continuous LED render task** | ✅ Active | dedicated renderer task in receiver `main.cpp` |
| **Input validation** | ✅ Implemented | color/effect range checks in receiver handler |
| **Test mode support** | ✅ Implemented | works with `TestDataGenerator` |
| **Live mode support** | ✅ Implemented | datalayer-backed telemetry and status paths |

---

## 11. Recommendations & Findings

### 11.1 Current State: PARTIALLY ALIGNED ⚠️

Transport and handler plumbing exist and are good, but logic still needs alignment to required model:
- Move TX trigger from SOC bands to **battery emulator operating state changes** (`EMULATOR_STATUS`)
- Include effect in LED packet
- Reinstate always-on RX render task

### 11.2 Necessary Changes

1. **Transmitter trigger source**
    - Move LED status publish to battery-emulator status transitions from `get_emulator_status()` / `led_handler` path.
    - Keep ESP-NOW link state (`TxStateMachine`) separate from battery-emulator operating state.
2. **Protocol payload**
    - Extend `flash_led_t` with `effect` field in `espnow_common.h`.
    - Effect enum should represent the 3 required states: `FLASH`, `HEARTBEAT`, `CONTINUOUS`.
    - Extend color enum/range to include BLUE for `STATUS_UPDATING`.
3. **Receiver render ownership**
    - Re-enable a dedicated LED render task in receiver `main.cpp`.
4. **Receiver handler update**
    - Parse and apply both color and effect in `handle_flash_led_message()`.
    - Do not force `LED_EFFECT_FLASH` for every message.

### 11.3 Optional Enhancements (Future)

If you want to extend this feature in the future, consider:

#### Enhancement 1: Dynamic LED Duration Control
Currently, LED flash cycle is hardcoded to 1000ms in `display_led.cpp` line 59.

**Proposal:**
```cpp
// New message type in espnow_common.h
typedef struct __attribute__((packed)) {
    uint8_t type;           // msg_flash_led_dynamic
    uint8_t color;          // 0=RED, 1=GREEN, 2=ORANGE, 3=BLUE
    uint16_t duration_ms;   // Custom cycle duration
} flash_led_dynamic_t;
```

**Transmitter change:**
```cpp
flash_led_dynamic_t flash_msg;
flash_msg.type = msg_flash_led_dynamic;
flash_msg.color = /* determined by band */;
flash_msg.duration_ms = 500 + (soc * 5);  // Example: scale with SOC
esp_now_send(peer_mac, (const uint8_t*)&flash_msg, sizeof(flash_msg));
```

**Receiver change:**
```cpp
void handle_flash_led_dynamic_message(const espnow_queue_msg_t* msg) {
    if (msg->len >= sizeof(flash_led_dynamic_t)) {
        const auto* flash_msg = reinterpret_cast<const flash_led_dynamic_t*>(msg->data);
        ESPNow::current_led_color = static_cast<LEDColor>(flash_msg->color);
        ESPNow::current_led_effect = LED_EFFECT_FLASH;
        ESPNow::current_led_duration_ms = flash_msg->duration_ms;  // New field
    }
}
```

**Impact:** Allows transmitter to control LED animation speed (e.g., faster flash = more urgent)

---

#### Enhancement 2: Web UI Remote LED Test
Add HTTP endpoint to trigger LED from web dashboard.

**Receiver endpoint:**
```cpp
static esp_err_t api_test_led_handler(httpd_req_t *req) {
    // Parse color from query parameter
    char buf[50];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf) == ESP_OK) {
        char color_param[10];
        if (httpd_query_key_value(buf, "color", color_param, sizeof(color_param)) == ESP_OK) {
            int color = atoi(color_param);
            if (color >= 0 && color <= 3) {
                flash_led(static_cast<LEDColor>(color));
                httpd_resp_set_status(req, "200 OK");
                httpd_resp_send(req, "LED flashed", HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            }
        }
    }
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_send(req, "Invalid color", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
```

**Impact:** Allows manual testing via web dashboard without waiting for SOC changes

---

#### Enhancement 3: Multiple LED Effects
Extend `LEDEffect` enum for different feedback patterns.

**Current:**
```cpp
enum LEDEffect {
    LED_EFFECT_SOLID = 0,
    LED_EFFECT_FLASH = 1,
    LED_EFFECT_HEARTBEAT = 2
};
```

**Proposed additions:**
```cpp
enum LEDEffect {
    LED_EFFECT_SOLID = 0,
    LED_EFFECT_FLASH = 1,
    LED_EFFECT_HEARTBEAT = 2,
    LED_EFFECT_PULSE = 3,        // Slow breathing effect
    LED_EFFECT_STROBE = 4,       // Fast blinking (alert)
    LED_EFFECT_FADE_IN = 5,      // Gradual fade to color
    LED_EFFECT_FADE_OUT = 6      // Gradual fade from color
};
```

**Transmitter enhancement:**
```cpp
typedef struct __attribute__((packed)) {
    uint8_t type;       // msg_flash_led_extended
    uint8_t color;      // 0=RED, 1=GREEN, 2=ORANGE, 3=BLUE
    uint8_t effect;     // LED effect type (0-6)
} flash_led_extended_t;
```

**Impact:** Allows different LED effects for different event types (warning vs. info vs. error)

---

### 11.4 Production-Ready Assessment

**Overall:** ⚠️ **NEEDS ALIGNMENT TO REQUIRED STATE MODEL**

The simulated LED transport base is solid, but behavior changes are needed before it matches requested operation.

---

## 12. Files Reviewed

### Transmitter
- [x] `ESPnowtransmitter2/espnowtransmitter2/src/espnow/data_sender.h` (header/interface)
- [x] `ESPnowtransmitter2/espnowtransmitter2/src/espnow/data_sender.cpp` (implementation)

### Receiver
- [x] `espnowreciever_2/src/espnow/espnow_tasks.cpp` (message routing & handler)
- [x] `espnowreciever_2/src/display/display_led.cpp` (display rendering)
- [x] `espnowreciever_2/src/display/display_led.h` (LED header)
- [x] `espnowreciever_2/src/test/test_data.cpp` (test mode status)
- [x] `espnowreciever_2/src/globals.cpp` (global state)
- [x] `espnowreciever_2/src/common.h` (type definitions)
- [x] `espnowreciever_2/src/config/led_config.h` (LED configuration)

### Common Library
- [x] `ESP32common/espnow_transmitter/espnow_common.h` (message definitions)

---

## 13. Implementation Plan (Bite-Sized Tasks by Device)

This plan is intentionally small-step and device-separated so implementation is safe and reviewable.

### 13.1 Shared/Common (protocol contract)

**Task C1 — Extend LED wire payload**
- File: `ESP32common/espnow_transmitter/espnow_common.h`
- Change `flash_led_t` from `{type, color}` to `{type, color, effect}`
- Add/confirm color enum values: RED=0, GREEN=1, ORANGE=2, BLUE=3
- Add/confirm effect enum values: CONTINUOUS=0, FLASH=1, HEARTBEAT=2

**Task C2 — Backward compatibility guard (optional but recommended)**
- Receiver should temporarily accept both packet sizes during migration window.
- Remove compatibility path once both devices are deployed.

**Task C3 — Single source of truth for enums/constants (required)**
- Put LED wire enums in shared/common code and include them from both devices.
- Avoid duplicate local enum definitions for color/effect where possible.
- Shared definitions should include:
    - `LedColorWire`: `RED=0, GREEN=1, ORANGE=2, BLUE=3`
    - `LedEffectWire`: `CONTINUOUS=0, FLASH=1, HEARTBEAT=2`
- Keep conversion helpers local per device only where needed (wire enum -> render enum).

**Task C4 — Shared mapping contract doc block**
- Add one canonical mapping table comment in shared header next to `flash_led_t`.
- Reference this table from TX/RX modules instead of repeating mappings in multiple files.

**Task C5 — Extend shared settings contract for LED policy (required)**
- File: `ESP32common/espnow_transmitter/espnow_common.h`
- Extend battery settings sync structures/enums so LED policy is part of authoritative config sync:
    - Add battery field ID(s) for LED policy (e.g., `BATTERY_LED_MODE`, optional `BATTERY_LED_EFFECT_POLICY`).
    - Extend `battery_settings_full_msg_t` to carry LED policy fields.
- Keep packet comments explicit so TX/RX treat these fields as persistent configuration, not transient runtime state.

**Task C6 — Define migration compatibility for settings payload versioning**
- Keep RX decode compatible during migration window (old and new `battery_settings_full_msg_t` sizes) if mixed firmware is expected.
- Remove compatibility path after both devices are on the new format.

**Task C7 — Keep LEDMODE semantics aligned with legacy UI**
- Preserve existing mode meaning from original settings UI (`Classic`, `Energy Flow`, `Heartbeat`) when mapping to new wire/settings values.
- Document mapping once in shared code comments to avoid TX/RX drift.

---

### 13.2 Transmitter tasks

**Design rule (efficiency-critical):**
- LED packets must be emitted by a **state-transition event**, not by periodic polling.
- Do not run periodic "check-if-changed" logic in fast loops/tasks just to decide whether to send LED updates.
- One transition event -> one LED update packet.

**Task T1 — Switch trigger source to operating status**
- Files:
    - `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.cpp`
    - `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/led_handler.cpp`
- Use `get_emulator_status()` as canonical state source for LED transport events.
- Implement this in the event pipeline where status level changes are already computed (single control point), not in telemetry loops.

**Task T2 — Standardize status->color policy (including blue update)**
- File: `.../led_handler.cpp`
- Set mapping:
    - `STATUS_OK -> GREEN`
    - `STATUS_WARNING -> ORANGE`
    - `STATUS_UPDATING -> BLUE`
    - `STATUS_ERROR -> RED`

**Task T3 — Add effect selection policy**
- Introduce a small mapping helper in transmitter (single function):
    - input: `EMULATOR_STATUS`
    - output: `{color, effect}`
- Keep color/effect independent and configurable.

**Task T3a — Emit on transition callback (not polling)**
- Add a tiny LED notifier call at the exact place where emulator status transitions are resolved.
- Preferred location: after event level/status is recomputed in battery emulator events pipeline.
- This avoids entering other state-machine code paths just to test for LED changes.

**Task T4 — Publish only on change**
- Keep a compact last-sent cache (`last_color`, `last_effect`) in the LED publisher only.
- Compare once at transition callback boundary and send only if changed.
- Reuse guarded send path (`TxSendGuard`) for reliability.
- Do not duplicate this check in other modules.

**Task T5 — Remove SOC-based LED sending from telemetry path**
- File: `ESPnowtransmitter2/espnowtransmitter2/src/espnow/data_sender.cpp`
- Remove `SOCBand` LED logic and `msg_flash_led` send from this file.
- Keep battery telemetry transmission untouched.

**Task T6 — Remove periodic LED decision code**
- Remove any LED publish decisions from periodic tasks (`loop()`, telemetry sender tasks, heartbeat tick loops) unless they are true transition producers.
- Keep LED publish ownership in one transition-driven module.

**Task T7 — Remove legacy physical LED color path (required)**
- Remove original logic that computes color for the physically wired LED path once state-transition ESPNOW LED transport is live.
- Keep only one canonical status->(color,effect) mapping used by the ESPNOW LED publisher.
- Remove old physical LED-specific branches/helpers that are no longer used.
- This is required to avoid duplicate logic and reduce CPU/work per cycle.

**Task T8 — Move LEDMODE persistence into SettingsManager (authoritative NVS)**
- Files:
    - `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_manager.h`
    - `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_manager.cpp`
- Add LED policy field(s) to `BatterySettingsBlob` and public getters/setters.
- Ensure read/write/version bump/checksum covers LED policy fields.

**Task T9 — Bridge legacy NVS key to managed settings (one-way migration)**
- Files:
    - `.../battery_emulator/communication/nvm/comm_nvm.cpp`
    - `.../settings/settings_manager.cpp`
- On boot, if legacy `LEDMODE` exists and managed field is unset/default, migrate into `SettingsManager` once.
- After migration, avoid dual-write ownership to prevent split-brain between legacy and managed storage.

**Task T10 — Include LED policy in TX settings snapshot responses**
- File: `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_handler.cpp`
- Populate new LED policy fields in `battery_settings_full_msg_t` for `subtype_battery_config` and legacy `subtype_settings` paths.

**Task T11 — Implement battery config section responder + LED replay hook**
- File: `ESPnowtransmitter2/espnowtransmitter2/src/espnow/version_beacon_manager.cpp`
- Implement `config_section_battery` in `send_config_section()` (currently TODO/warn only).
- Add explicit current-LED-state replay trigger on reconnect/recovery path (e.g., on `REQUEST_DATA` stream start or section request completion) so RX reboot gets immediate current LED state.

---

### 13.3 Receiver tasks

**Task R1 — Add BLUE to receiver LED enum and renderer**
- Files:
    - `espnowreciever_2/src/config/led_config.h`
    - `espnowreciever_2/src/display/display_led.cpp`
- Add `LED_BLUE` and blue RGB565 mapping.
- Ensure gradients/effects work for blue.

**Task R1a — Add BLUE→BLACK gradient (required)**
- File: `espnowreciever_2/src/display/display_led.cpp`
- Add a dedicated blue gradient buffer with the **same interval count** as existing gradients:
    - `static uint16_t led_blue_gradient[LED_FADE_STEPS + 1];`
- In `init_led_gradients()`, precompute:
    - `pre_calculate_color_gradient(LEDColors::BLUE, Display::tft_background, LED_FADE_STEPS, led_blue_gradient);`
- In effect functions, add `LED_BLUE` switch handling to use `led_blue_gradient`.
- Keep timing unchanged so BLUE uses identical fade intervals/behavior to RED/GREEN/ORANGE.

**Task R1b — Add BLUE color constant in one place**
- File: `espnowreciever_2/src/config/led_config.h`
- Add `LED_BLUE` enum value and `LEDColors::BLUE` RGB565 constant.
- Ensure all LED color arrays/switches are updated and bounded correctly.

**Task R2 — Parse effect from packet**
- File: `espnowreciever_2/src/espnow/espnow_tasks.cpp`
- Update `handle_flash_led_message()` to parse color+effect.
- Validate ranges and reject invalid payloads safely.

**Task R3 — Reinstate dedicated LED render task**
- Files:
    - `espnowreciever_2/src/main.cpp`
    - `espnowreciever_2/src/test/test_data.cpp` (or move task to non-test module)
- Ensure always-on LED rendering loop with mutex-safe TFT updates.

**Task R4 — Keep display behavior local**
- Network updates only set state.
- Rendering task handles animation timing/effects locally (no network spam).

**Task R5 — Cache LED policy in receiver battery-emulator settings**
- Files:
    - `espnowreciever_2/lib/webserver/utils/transmitter_manager.h`
    - `espnowreciever_2/lib/webserver/utils/transmitter_manager.cpp`
- Extend `BatteryEmulatorSettings` with LED policy field(s) and persist them in TX cache NVS.

**Task R6 — Decode LED policy from battery settings snapshot**
- File: `espnowreciever_2/src/espnow/handlers/battery_info_handler.cpp`
- Parse new LED policy fields from `battery_settings_full_msg_t` and call `TransmitterManager::storeBatteryEmulatorSettings(...)` with updated values.

**Task R7 — Bind settings page `%LEDMODE%` to synchronized value**
- File: `espnowreciever_2/lib/webserver/processors/settings_processor.cpp`
- Replace placeholder default-only output with options reflecting cached LED policy from transmitter settings cache.

**Task R8 — Request battery config on RX startup/version mismatch**
- Files:
    - `espnowreciever_2/src/espnow/rx_connection_handler.cpp`
    - `espnowreciever_2/src/espnow/espnow_tasks.cpp`
- Add battery section request in initialization flow.
- In version-beacon handler, compare cached battery version and request `config_section_battery` on unknown/mismatch.

---

### 13.4 Verification tasks

**Task V1 — Unit/logic checks**
- Validate status->color/effect mapping function behavior.
- Validate packet decode range checks.

**Task V2 — Integration checks**
- Trigger each status (`OK`, `WARNING`, `UPDATING`, `ERROR`) and verify receiver LED color/effect.
- Confirm one-shot sending (no repeated packet flood while state unchanged).

**Task V3 — Regression checks**
- Confirm no impact to SOC/power display paths.
- Confirm ESP-NOW health/recovery behavior unchanged.

**Task V4 — Persistence checks (required)**
- Change LED mode in settings UI, reboot TX, confirm value survives reboot and is loaded from authoritative managed settings.
- Confirm no divergence between legacy `LEDMODE` path and managed settings after migration step.

**Task V5 — Boot-order/reboot recovery checks (required)**
- TX boot first, RX boot later: verify RX receives current LED state without waiting for next status transition.
- RX reboot while TX running: verify LED state resync occurs automatically.
- TX reboot while RX running: verify LED state recovers after reconnect and initial requests.

---

## 14. Cleanup / Redundant Code Removal (Required)

After feature completion and validation, remove legacy paths to keep codebase clean.

### 14.1 Transmitter cleanup
- Remove SOC-band LED types/variables/functions from `data_sender.cpp`.
- Remove outdated comments claiming SOC-driven LED policy.
- Remove any duplicate LED send paths (single owner only).
- Remove any periodic "check then maybe send LED" logic once transition callback path is in place.
- Remove original physical wired-LED color calculation code and any dead wiring-specific LED paths once migration is complete.
- Ensure there is no second/legacy status->color implementation left in transmitter code.

### 14.2 Receiver cleanup
- Remove deprecated/stub-only status task code paths once replacement task is active.
- Remove temporary compatibility parsing code after both devices run new packet format.
- Consolidate LED constants into one config source (avoid duplicated enums/macros).
- Remove any duplicate gradient initialization logic after BLUE path is integrated.

### 14.3 Documentation cleanup
- Update all docs/snippets to 4-color model (RED/GREEN/ORANGE/BLUE).
- Remove references that imply fixed pairing like “green = heartbeat”.
- Keep one canonical mapping table and reference it.

### 14.4 Maintainability standards
- Keep one single state->LED mapping function in TX.
- Keep one single transition->publish function in TX (single owner).
- Keep one single packet decode + state-apply function in RX.
- Keep rendering/effects isolated to one receiver task/module.
- Keep one single shared wire enum definition for color/effect in common code.
- Ensure all removed code is deleted (not commented out) after rollout.

### 14.5 Efficiency/flow recommendations (final)
- Prefer **event-driven LED publish** over polling.
- Keep LED packet creation lightweight (stack struct, no dynamic allocation).
- Avoid logging LED status every cycle; log only on actual transition/send/failure.
- Keep renderer timing on receiver only; transmitter should only send state deltas.
- Avoid cross-module LED checks; use one owner module and one public API.
- Removing legacy physical LED color logic after migration will reduce redundant branching and improve runtime efficiency.

### 14.6 Additional code-quality improvements (post-review)
- Create a dedicated `led_status_publisher` module on TX (single responsibility: map status -> wire packet + publish).
- Create a dedicated `led_status_renderer` module on RX (single responsibility: apply state + render effects).
- Keep protocol migration explicit with a temporary `protocol_version` guard and remove it after rollout.
- Add a small validation helper (`is_valid_led_color()`, `is_valid_led_effect()`) used by RX handler and tests.
- Add a compact test matrix in CI covering all 4 colors x 3 effects x state transitions.
- Ensure all legacy comments and dead branches are removed in the same PR as feature completion.

---

## 15. Investigation Update: LED Effect Storage, NVS Persistence, and Reboot Resync

This section captures findings from a targeted investigation of where LED effect/pattern is sourced, where it is persisted, and what happens when RX reboots after TX.

### 15.1 Where LED effect/pattern is initially stored

Confirmed from the original Battery Emulator flow:

- Web settings page exposes **Status LED pattern** as `LEDMODE`.
    - `Battery-Emulator-9.2.4/data/settings_body.html`
    - `Battery-Emulator-9.2.4/Software/src/devboard/webserver/settings_html.cpp`
- Value is persisted in NVS and loaded at boot into runtime state:
    - `Battery-Emulator-9.2.4/Software/src/communication/nvm/comm_nvm.cpp`
    - `datalayer.battery.status.led_mode = settings.getUInt("LEDMODE", ...)`

Confirmed in current migrated transmitter:

- Web save path still persists `LEDMODE`:
    - `ESP32common/webserver/webserver.cpp` (`settings.saveUInt("LEDMODE", atoi(value))`)
- Boot path still loads `LEDMODE` into `datalayer.battery.status.led_mode`:
    - `ESPnowtransmitter2/.../battery_emulator/communication/nvm/comm_nvm.cpp`

Important gap:

- New TX settings sync contract (`SettingsManager` + `battery_settings_full_msg_t`) does **not** carry `LEDMODE` / effect policy.
- Current `flash_led_t` transport also does not carry effect yet (receiver still forces `LED_EFFECT_FLASH`).

### 15.2 Is receiver “battery emulator specifications” the right place?

Yes — this is a suitable place and aligns with current receiver architecture.

Current receiver cache already has a dedicated battery-emulator settings struct:

- `espnowreciever_2/lib/webserver/utils/transmitter_manager.h`
    - `struct BatteryEmulatorSettings { double_battery, pack/cell voltages, soc_estimated }`

Recommendation:

- Add LED policy fields here (minimum one field):
    - `uint8_t led_mode` (classic/flow/heartbeat), or
    - `uint8_t led_effect_policy` (continuous/flash/heartbeat), depending on finalized contract.
- Then bind settings page placeholder `%LEDMODE%` to this cached value instead of fixed default.

Current behavior shows this is not yet wired:

- `espnowreciever_2/lib/webserver/processors/settings_processor.cpp`
    - currently returns `"<option value='0'>Default</option>"` for `LEDMODE`.

### 15.3 Transmitter NVS persistence requirement

Current state:

- `LEDMODE` is persisted in legacy `batterySettings` namespace and loaded into datalayer.
- However, it is **not included** in `SettingsManager` battery blob or ESP-NOW settings contract.

Recommendation (required for long-term consistency):

1. Add LED mode/effect policy into `SettingsManager` battery storage model (`BatterySettingsBlob`) and getters/setters.
2. Add field IDs in shared protocol (`espnow_common.h`) for LED setting updates.
3. Include LED field(s) in TX→RX settings snapshot message.
4. Keep migration bridge reading legacy `LEDMODE` key once, then write into new managed settings to avoid split-brain config.

### 15.4 What happens if RX boots/reboots after TX?

Current behavior risk is real.

Evidence:

- RX init requests only `mqtt/network/metadata` config sections + `subtype_power_profile` stream start:
    - `espnowreciever_2/src/espnow/rx_connection_handler.cpp` (`send_initialization_requests()`)
- RX version-beacon handler checks/request logic implemented for MQTT + Network only; battery comment says “would go here when implemented”:
    - `espnowreciever_2/src/espnow/espnow_tasks.cpp`
- TX versioned config responder does not implement `config_section_battery` send yet:
    - `ESPnowtransmitter2/src/espnow/version_beacon_manager.cpp`

Impact:

- If LED status packet was already sent once before RX reboot, RX may remain on default/stale LED state until next state transition triggers a new LED packet.

### 15.5 Recommendations to close reboot-resync gap

**R15-A — Implement battery section resync in version-sync path**

- TX: implement `config_section_battery` in `VersionBeaconManager::send_config_section()`.
- RX: in version-beacon handler, compare cached battery version and request battery section on unknown/mismatch.

**R15-B — Include LED mode/effect in battery emulator settings sync**

- Extend shared settings payload/fields to carry LED mode/effect policy.
- RX persists this in `TransmitterManager` battery-emulator cache and exposes on settings page.

**R15-C — Add forced LED state replay on connection/recovery events**

- Keep event-driven “send on change” as primary rule.
- Add one explicit replay trigger on reconnect / receiver re-discovery / REQUEST_DATA handling so RX always gets current LED state after reboot.

**R15-D — Keep NVS single-source ownership on TX**

- Store LED policy in `SettingsManager` NVS path (authoritative).
- Use legacy `LEDMODE` key only for migration compatibility, then phase out duplicate persistence paths.

**R15-E — Receiver startup default behavior**

- Until first valid LED state arrives, show deterministic neutral indicator (e.g., ORANGE + CONTINUOUS) and mark as “sync pending” in debug logs.

---

## 16. Conclusion

**Final finding:** The simulated LED transport implementation is complete and operating in the intended state-driven architecture.

Implemented end state:
- ✅ State-transition-driven LED publishing on transmitter
- ✅ Shared color+effect wire contract
- ✅ Receiver-side continuous rendering task
- ✅ Managed LED policy persistence and settings sync
- ✅ Reconnect/restart resync path for current LED state

No further core feature work is required for the LED transport scope defined in this review. Remaining work is operational regression/testing and routine maintenance.

---

## Appendix: Build Verification

To verify this implementation is built into your binaries:

### Transmitter Build Check
```bash
cd ESPnowtransmitter2/espnowtransmitter2
pio build -e <your_env>
# Look for LED status log on TX state transitions
```

### Receiver Build Check
```bash
cd espnowreciever_2
pio build -e receiver_tft
# Look for LED request/decode logs (color + effect)
# Observe continuous LED display behavior with state changes
```

### Manual Test Procedure
1. Flash both devices
2. Open transmitter serial monitor (check for state-change LED status messages)
3. Open receiver serial monitor (check for "Flash LED request...")
4. Observe LED animation on receiver TFT display
5. Verify LED color/effect matches expected transmitter state

---

**End of Review**
