# LilyGo T-Display-S3 — Relay GPIO Availability for External Relay Control

## Board Clarification

> **Note**: The receiver board in this project is the **LilyGo T-Display-S3** (ESP32-S3R8), **not** the T-Display-S2 (ESP32-S2) as sometimes referenced informally. This is confirmed by `platformio.ini` (`board = lilygo-t-display-s3`, `board_build.mcu = esp32s3`) and the project `hardware_config.h`.

## Receiver Variant Declaration (Important)

This relay-control design assumes the **non-touch** T-Display-S3 variant.

**Project declaration**: This repository design baseline is **T-Display-S3 non-touch**.

- GPIO16 and GPIO21 are treated as available because touch-controller lines are not populated on non-touch hardware.
- If a Touch variant is introduced, GPIO16/GPIO21 must be reallocated.

---

## Scope

This document investigates GPIO pin availability on the LilyGo T-Display-S3 (receiver board) for controlling up to 4 external relay channels, in the context of the two-board system:

- **Transmitter**: Olimex ESP32-POE2 — GPIO constrained by Ethernet RMII, CAN (HSPI), RS485 (VSPI)
- **Receiver**: LilyGo T-Display-S3 (ESP32-S3R8) — handles display, ESP-NOW receive, MQTT, relay I/O

---

## Authoritative Sources

- LilyGo T-Display-S3 official GitHub: `https://github.com/Xinyuan-LilyGO/T-Display-S3`
- Official factory `pin_config.h` (vendor source of truth)
- Project `src/hal/hardware_config.h` (confirmed in-firmware GPIO assignments)
- ESP32-S3 datasheet (ESP32-S3R8: 16MB flash, 8MB OPI PSRAM)

---

## T-Display-S3 GPIO Full Allocation Map

The ST7789 display is driven via an **8-bit parallel bus** (not SPI), consuming a large portion of GPIO.

### System-Allocated GPIO (must not use for relay)

| GPIO | Function | Source | Notes |
|------|----------|--------|-------|
| 0 | BOOT button (Button 1) | Vendor `pin_config.h` | Strapping pin; keep as input |
| 4 | Battery voltage ADC | Vendor `pin_config.h` | Analog input only |
| 5 | TFT RST (display reset) | `hardware_config.h` | Active LOW |
| 6 | TFT CS (chip select) | `hardware_config.h` | Active LOW |
| 7 | TFT DC (data/command) | `hardware_config.h` | |
| 8 | TFT WR (write strobe) | `hardware_config.h` | Active LOW |
| 9 | TFT RD (read strobe) | `hardware_config.h` | Active LOW |
| 11 | SD CLK (TF-card expansion) | Vendor `pin_config.h` | Only if SD shield fitted |
| 12 | SD D0 (TF-card expansion) | Vendor `pin_config.h` | Only if SD shield fitted |
| 13 | SD CMD (TF-card expansion) | Vendor `pin_config.h` | Only if SD shield fitted |
| 14 | Button 2 (user button) | Vendor `pin_config.h` | |
| 15 | Display power enable | `hardware_config.h` | **Must be HIGH** or screen off |
| 17 | I2C SCL | Vendor `pin_config.h` | Used if any I2C device present |
| 18 | I2C SDA | Vendor `pin_config.h` | Used if any I2C device present |
| 19 | USB D− | ESP32-S3 hardware | **Do not use** — breaks USB |
| 20 | USB D+ | ESP32-S3 hardware | **Do not use** — breaks USB |
| 33–37 | OPI PSRAM SPI | ESP32-S3 hardware | **Internally bonded — not accessible** |
| 38 | TFT backlight PWM | `hardware_config.h` | |
| 39 | TFT D0 | `hardware_config.h` | 8-bit parallel bus |
| 40 | TFT D1 | `hardware_config.h` | 8-bit parallel bus |
| 41 | TFT D2 | `hardware_config.h` | 8-bit parallel bus |
| 42 | TFT D3 | `hardware_config.h` | 8-bit parallel bus |
| 45 | TFT D4 | `hardware_config.h` | 8-bit parallel bus |
| 46 | TFT D5 | `hardware_config.h` | 8-bit parallel bus |
| 47 | TFT D6 | `hardware_config.h` | 8-bit parallel bus |
| 48 | TFT D7 | `hardware_config.h` | 8-bit parallel bus |

---

## Available GPIO for Relay Control

After removing all system-allocated pins, the following are technically unallocated in the current receiver firmware:

| GPIO | Notes | Relay Suitability |
|------|-------|------------------|
| **1** | General GPIO on ESP32-S3 (no special boot function unlike ESP32) | ✅ Usable |
| **2** | General GPIO on ESP32-S3 (strap pin; pull-up at boot to HIGH) | ⚠️ Usable, but boot pull-up needed |
| **3** | General GPIO on ESP32-S3 | ✅ Usable |
| **10** | No special function; clean general purpose | ✅ **Recommended** |
| **16** | Touch INT (non-touch board: pin unconnected internally) | ✅ **Free on non-touch version** |
| **21** | Touch RST (non-touch board: pin unconnected internally) | ✅ **Free on non-touch version** |
| **43** | UART0 TX (ESP32-S3) — but Serial is via USB CDC (GPIO19/20), NOT GPIO43 in this project | ✅ Usable |
| **44** | UART0 RX (ESP32-S3) — same, USB CDC handles serial | ✅ Usable |

### Key Insight: USB CDC frees UART0 pins

Because the project builds with `-D ARDUINO_USB_CDC_ON_BOOT=1`, all Serial output is routed through the **USB hardware peripheral** (GPIO19/GPIO20). UART0 (GPIO43/GPIO44) is **not claimed** and these pins can be freely used as digital outputs at runtime.

This is different from ESP32 classic where UART0 was the only serial programming interface.

---

## Recommended 4-Relay GPIO Assignment (Non-Touch T-Display-S3)

Use transmitter semantic labels from the HAL contactor interface (`POSITIVE_CONTACTOR_PIN`, `NEGATIVE_CONTACTOR_PIN`, `PRECHARGE_PIN`, `BMS_POWER`) instead of generic relay numbering.

| Transmitter control label | GPIO | Rationale |
|-------|------|-----------|
| `NEGATIVE_CONTACTOR_PIN` | **GPIO10** | Clean general purpose; no conflicts, no caveats |
| `PRECHARGE_PIN` | **GPIO16** | Touch INT pad — unconnected on non-touch board |
| `POSITIVE_CONTACTOR_PIN` | **GPIO21** | Touch RST pad — unconnected on non-touch board |
| `BMS_POWER` | **GPIO43** | UART0 TX — not used (USB CDC is active serial path) |

Optional 5th channel (if needed later): map `SECOND_BATTERY_CONTACTORS_PIN` to **GPIO44**.

All four are output-capable on ESP32-S3 and do not carry boot/strap sensitivity risks.

---

## Important Caveats

### Touch board vs. non-touch board
- On the **Touch version** of T-Display-S3, GPIO16 (touch INT) and GPIO21 (touch RST) are connected to the capacitive touch controller.
- If the touch version is used in future, these two relay assignments would need to move.
- The current receiver firmware does **not** configure touch pins, confirming non-touch usage.

### GPIO2 (strap pin)
- On ESP32-S3, GPIO2 is a strap pin that must be HIGH at boot for normal operation.
- It can be used as GPIO output after boot, but requires a pull-up resistor to avoid issues.
- **Not recommended** for relay control unless the other four are exhausted.

### SD Card expansion
- If a TF/SD card shield is fitted later, GPIO11/12/13 would be consumed.
- These are not currently used, but relay wiring must not interfere with them.

### USB must remain functional
- GPIO19/20 are used for USB D-/D+ and must never be driven as GPIO outputs.
- Doing so will destroy USB connectivity and lock out firmware reprogramming via USB.

---

## Physical Accessibility

The T-Display-S3 does not have a large DIP-style expansion header. Relay wiring options:

| Method | Description | Notes |
|--------|-------------|-------|
| **Solder pads on PCB** | The T-Display-S3 has exposed test pads for many signals | Requires soldering; GPIO10, 16, 21 pads present |
| **Bottom expansion connector** | Some revisions expose I2C + a few GPIO via JST or through-holes | Check board revision; I2C pins (17/18) accessible here |
| **T-Display-S3 TF Shield** | LilyGo expansion shield adds accessible headers | Also adds SD card slot; check pin conflicts |
| **External wiring from USB-C region** | Break-out board or custom PCB using board pins | Most flexible but requires custom hardware |

**Recommendation**: Use the exposed solder pads on the back of the T-Display-S3 PCB for GPIO10, GPIO16, GPIO21, and GPIO43, and wire directly to relay driver board (e.g., ULN2003/MOSFET relay module).

---

## Relay Driver Hardware

Directly connecting an ESP32 GPIO to a relay coil is **not safe** — relay coils draw 50–200mA at 5V/12V, far exceeding the 12mA GPIO drive current. Use a driver stage:

| Option | Suitable For | Notes |
|--------|-------------|-------|
| **ULN2803A** (Darlington array) | 4–8 x 5V/12V relay coils | Single IC, 500mA per channel, needs flyback diode |
| **MOSFET (e.g., 2N7000 / BSS138)** | Low-side switching | Individual component, good for mixed voltages |
| **Pre-built 4-ch relay module** | Direct plug-in | Typically accepts 3.3V logic trigger, built-in optoisolation |
| **Solid State Relay (SSR)** | AC loads | 3–32V DC trigger input; compatible with 3.3V GPIO directly |

**Recommended for this project**: 4-channel opto-isolated relay module (common off-the-shelf) — accepts 3.3V trigger directly from ESP32-S3 GPIO, built-in flyback and isolation, avoids all driver design work.

### User-provided relay reference

The provided AliExpress link currently resolves as an SSR family listing (`SSR-10DD`..`SSR-100DD`, typically 3–32V input, DC output switching). This is valid for direct GPIO-level triggering, but is generally **single-channel** per module.

- If using these SSR modules: allocate one GPIO per SSR channel (4 relays = 4 modules/channels).
- If using a 4-channel mechanical relay board: keep transistor/opto isolation and verify trigger polarity (`active LOW` vs `active HIGH`).

---

## Transmitter → Receiver Relay Control via ESP-NOW

Relay actuation should be owned by the **receiver** (T-Display-S3). The transmitter (Olimex) sends commands wirelessly via ESP-NOW.

### Control architecture

1. Olimex evaluates system state (CAN + RS485 + logic rules).
2. Olimex sends compact ESP-NOW relay command frames to T-Display-S3.
3. T-Display-S3 validates frame (`version`, `seq`, `crc/len`), applies relay outputs, and sends ACK/status.
4. Olimex retries if ACK timeout occurs.

**ACK semantics (important)**: `RELAY_ACK` confirms only that the receiver accepted/processed the ESP-NOW command frame for `seq` (i.e., message-level delivery/handling). It does **not** by itself prove the downstream physical relay contact changed state. Use `RELAY_STATUS`/telemetry (and optional electrical feedback inputs, if fitted) for physical-state confirmation.

### Recommended relay command contract

Use a deterministic binary payload (avoid ad-hoc strings):

- `msg_type` (`RELAY_CMD`, `RELAY_STATUS`, `RELAY_ACK`)
- `seq` (monotonic sequence number)
- `relay_mask` (bitmask for 4 relays)
- `mode` (`set`, `pulse`, `failsafe_off`)
- `pulse_ms` (optional pulse time for monostable actions)
- `origin_ts_ms` (transmitter timestamp)

Example compact packet definition:

```c
typedef struct __attribute__((packed)) {
    uint8_t  msg_type;      // 0x31=RELAY_CMD, 0x32=RELAY_ACK, 0x33=RELAY_STATUS
    uint8_t  version;       // protocol version (start with 1)
    uint16_t seq;           // monotonic sequence
    uint8_t  relay_mask;    // bit0..bit3 = relay states
    uint8_t  mode;          // 0=set, 1=pulse, 2=failsafe_off
    uint16_t pulse_ms;      // used when mode=pulse
    uint32_t origin_ts_ms;  // sender timestamp
    uint16_t crc16;         // optional integrity check
} RelayEspNowFrame;
```

Relay bit mapping (semantic):

- bit0 → `NEGATIVE_CONTACTOR_PIN` (`GPIO10`)
- bit1 → `PRECHARGE_PIN` (`GPIO16`)
- bit2 → `POSITIVE_CONTACTOR_PIN` (`GPIO21`)
- bit3 → `BMS_POWER` (`GPIO43`)

Example command sequence:

1. Transmitter sends `RELAY_CMD seq=1042 relay_mask=0b0001 mode=set` (`NEGATIVE_CONTACTOR_PIN` ON).
2. Receiver applies GPIO10=ON (`NEGATIVE_CONTACTOR_PIN`), leaves others unchanged/off.
3. Receiver returns `RELAY_ACK seq=1042` (command receipt/processing confirmation only).
4. If transmitter sees no ACK in 150 ms, it retries up to 3 times.

### Reliability rules (recommended)

- ACK timeout: 100–200 ms
- Retries: up to 3 attempts per command
- Deduplicate by `seq` on receiver (ignore already-applied duplicate frames)
- Heartbeat/status publish every 1–2 s while connected
- Failsafe: if no valid command/heartbeat for configurable timeout (e.g., 5–10 s), force relays to safe state

### Safety and startup behavior

- On receiver boot: initialize all relay GPIO as outputs and force `OFF` before enabling command processing.
- Require explicit first valid `RELAY_CMD` to transition from safe state.
- Persist last known safe policy (not last on-state) across reboot.

### Integration point in current project

- **Transmitter side**: emit `RELAY_CMD` in existing ESP-NOW send path when relay decisions change.
- **Receiver side**: handle `RELAY_CMD` in existing ESP-NOW receive callback and update GPIO outputs atomically.
- **Telemetry**: include relay state in receiver MQTT/status payload for observability.

---

## Transmitter HAL Compatibility: "Pseudo GPIO" Allocation

The transmitter codebase expects relay outputs to be represented in a HAL GPIO model. Because physical relay pins are moved to the receiver, implement **virtual (pseudo) GPIO relay pins** on the transmitter.

### Goal

Keep existing transmitter relay logic unchanged (it still calls HAL-style GPIO APIs), while redirecting those calls to ESP-NOW relay messages.

### Recommended design

1. Define 4 virtual relay pin IDs in transmitter HAL config (IDs outside real GPIO range).
2. Mark them as `VIRTUAL_OUTPUT` in HAL metadata.
3. In `digitalWrite()`/relay-set path, detect virtual pin IDs and call ESP-NOW sender instead of touching hardware registers.
4. Mirror last commanded state locally for readback (`digitalRead()`/status pages).

### Suggested virtual pin IDs (semantic)

- `GPIO_NEGATIVE_CONTACTOR_VIRTUAL = 200`
- `GPIO_PRECHARGE_VIRTUAL = 201`
- `GPIO_POSITIVE_CONTACTOR_VIRTUAL = 202`
- `GPIO_BMS_POWER_VIRTUAL = 203`
- Optional: `GPIO_SECOND_BATTERY_CONTACTORS_VIRTUAL = 204`

Using IDs above valid ESP32 GPIO space prevents accidental hardware writes.

### Suggested HAL behavior

- `pinMode(virtualPin, OUTPUT)` → register virtual pin mode in software only.
- `digitalWrite(virtualPin, state)` → update local shadow state and enqueue `RELAY_CMD` via ESP-NOW.
- `digitalRead(virtualPin)` → return last receiver-reported state from `RELAY_STATUS` (preferred) or last commanded state (fallback); do not treat ACK alone as proof of physical switching.
- `flushVirtualRelayOutputs()` (optional) → batch multiple writes into one `relay_mask` packet.

### Suggested message mapping

- Virtual pin 200 (`GPIO_NEGATIVE_CONTACTOR_VIRTUAL`) controls bit0
- Virtual pin 201 (`GPIO_PRECHARGE_VIRTUAL`) controls bit1
- Virtual pin 202 (`GPIO_POSITIVE_CONTACTOR_VIRTUAL`) controls bit2
- Virtual pin 203 (`GPIO_BMS_POWER_VIRTUAL`) controls bit3

`digitalWrite(GPIO_PRECHARGE_VIRTUAL, HIGH)` should produce `RELAY_CMD relay_mask = previous_mask | 0b0010`.

### Fault handling policy

- If ACK not received after retry budget, mark virtual pin state as `UNKNOWN`/`STALE`.
- Expose comms fault to transmitter state machine and UI/logging.
- Optional policy: auto-send `failsafe_off` command after repeated link failures.

### Minimal integration pattern (transmitter)

```cpp
// transmitter HAL virtual semantic IDs
constexpr uint16_t GPIO_NEGATIVE_CONTACTOR_VIRTUAL = 200;
constexpr uint16_t GPIO_PRECHARGE_VIRTUAL = 201;
constexpr uint16_t GPIO_POSITIVE_CONTACTOR_VIRTUAL = 202;
constexpr uint16_t GPIO_BMS_POWER_VIRTUAL = 203;

bool halDigitalWrite(uint16_t pin, bool level) {
    if (pin >= 200 && pin <= 203) {
        relayShadowMask = updateBit(relayShadowMask, pin - 200, level);
        return sendRelayCmdEspNow(relayShadowMask); // async + ACK tracking
    }
    return writePhysicalGpio(pin, level);
}
```

This preserves existing transmitter HAL contracts while physically actuating relays on the receiver.

---

## Timing Re-Review (Transmitter) for Remote Relay Control

I reviewed transmitter timing paths relevant to relay/control-plane actuation.

### Observed timing in current transmitter code

- `TimingConfig::ESPNOW_SEND_INTERVAL_MS = 2000 ms` (telemetry sender cadence)
- `TransmissionTask` background send loop runs every `50 ms` (20 msg/s max)
- `TimingConfig::HEARTBEAT_INTERVAL_MS = 10000 ms` (link liveness only)
- `TimingConfig::MAIN_LOOP_DELAY_MS = 1000 ms`
- `TxSendGuard` applies failure backoff from `2000 ms` up to `30000 ms`

### Existing contactor/precharge timing expectations in transmitter logic

From `comm_contactorcontrol.cpp`:

- Negative contactor settle delay: `500 ms` (`NEGATIVE_CONTACTOR_TIME_MS`)
- Configured precharge dwell: `precharge_time_ms` (default `100 ms`)
- Post-close precharge-off delay: `1000 ms` (`PRECHARGE_COMPLETED_TIME_MS`)

This is a sub-second state machine. Therefore, relay commands must **not** be coupled to 1s/2s periodic paths.

### Compatibility conclusion

The pseudo GPIO approach **will work** if virtual relay writes are routed to a **fast command path** (event-driven, immediate send), not to telemetry cadence.

### Required implementation rules (to guarantee timing)

1. **Do not use `DataSender` (2s interval)** for relay commands.
2. Dispatch relay commands immediately from HAL virtual `digitalWrite()` (or enqueue to a dedicated high-priority relay TX task).
3. Preferred relay TX cycle time: `10–20 ms` task tick (or immediate send on edge).
4. Keep retry window short for control frames:
    - ACK timeout `100–150 ms`
    - retry interval `25–50 ms`
    - max retries `3`
5. Apply command coalescing per cycle (single `relay_mask` frame) to avoid burst collisions.
6. Use strict idempotency (`seq`) and deduplication on receiver.

### TxSendGuard interaction (important)

Current `TxSendGuard` backoff (`2s..30s`) is appropriate for bulk telemetry resilience but too coarse for relay control timing. For relay command frames:

- either bypass long backoff for `RELAY_CMD` only,
- or add a control-frame policy that limits pause to `<=150 ms` while still surfacing link-fault state.

### Practical worst-case timing with recommended policy

If one command attempt fails:

- send immediately (`t=0`)
- timeout at `150 ms`
- retries at `~200 ms` and `~250 ms`

Total worst-case command confirmation ≈ `300–450 ms`, which remains compatible with precharge-stage transitions (500 ms and above).

### Design note for deterministic sequencing

Keep the precharge/contactor sequencing authority on transmitter state logic, and transmit **state transitions** (`relay_mask` changes) to receiver outputs. Do not let receiver invent timing; receiver should be an actuator mirror with ACK/status.

---

## Proposed ESP-NOW Messaging Upgrade: Immediate vs Delayed Send Policy

To support relay-critical control without breaking existing telemetry flow, add a **transmit policy** field and extend the send API with a policy argument.

### 1) Message structure extension (backward-compatible)

Add a compact common header used by control-capable frames:

```c
typedef enum : uint8_t {
        TX_POLICY_DELAYED   = 0,   // existing behavior (queued/background)
        TX_POLICY_IMMEDIATE = 1    // bypass delayed path, send now
} TxPolicy;

typedef struct __attribute__((packed)) {
        uint8_t  msg_type;
        uint8_t  version;
        uint16_t seq;
        uint8_t  tx_policy;        // TxPolicy
        uint8_t  flags;            // bit0=require_ack, bit1=dedupe, bit2=failsafe_allowed
        uint16_t ack_timeout_ms;   // 0 => default profile for this msg_type
} EspNowControlHeader;
```

For relay command packets, prepend this header and keep existing relay fields (`relay_mask`, `mode`, `pulse_ms`, etc.).

### 2) Send API signature extension

Current transmitter path uses guarded send helpers. Extend function signature with policy:

```cpp
enum class SendPolicy : uint8_t { Delayed = 0, Immediate = 1 };

esp_err_t send_to_receiver_guarded(
        const uint8_t* mac,
        const uint8_t* data,
        size_t len,
        const char* tag,
        SendPolicy policy = SendPolicy::Delayed,
        uint16_t ack_timeout_ms = 0
);
```

Behavior:

- `Delayed` (default): keeps current architecture unchanged.
- `Immediate`: bypass delayed/background queue path and transmit control frame now (with short ACK/retry profile).

### 3) Recommended routing behavior

- `RELAY_CMD` default policy = `Immediate`
- telemetry/config sync default policy = `Delayed`
- optional override per call site

This preserves existing behavior for all non-relay messages while enabling deterministic relay actuation.

### 4) Retry profiles by policy

- **Immediate profile** (relay/control):
    - ACK timeout: `100–150 ms`
    - Retry interval: `25–50 ms`
    - Retries: `3`
- **Delayed profile** (telemetry/state):
    - existing send guard/backoff behavior (seconds-scale allowed)

### 5) Example usage

```cpp
// Pseudo GPIO write for relay control -> immediate send
send_to_receiver_guarded(peerMac,
                                                 reinterpret_cast<const uint8_t*>(&relayCmd),
                                                 sizeof(relayCmd),
                                                 "relay_cmd",
                                                 SendPolicy::Immediate,
                                                 150);

// Telemetry remains delayed/default
send_to_receiver_guarded(peerMac,
                                                 reinterpret_cast<const uint8_t*>(&telemetry),
                                                 sizeof(telemetry),
                                                 "telemetry");
```

### 6) Receiver-side handling requirement

Receiver should honor `tx_policy` only for diagnostics/metrics; execution semantics are driven by `msg_type` and `seq` dedupe. For `RELAY_CMD`, apply atomically and ACK immediately.

### 7) Why this solves the current problem

- No need to redesign entire ESP-NOW stack.
- Existing code keeps default delayed behavior.
- Relay path gains explicit immediate transport semantics.
- HAL pseudo GPIO can call one API with policy flag and remain cleanly abstracted.

## Codebase Impact Check ("relays triggered early")

I re-checked transmitter flow with the assumption that relay decisions happen in early control logic.

### Findings

1. Main control loop executes core battery/CAN work first (`CANDriver::update()`, `BatteryManager::update_transmitters()`), then periodic service tasks.
2. Existing ESP-NOW traffic classes (telemetry, heartbeat, config, catalogs, etc.) all send through `TxSendGuard::send_to_receiver_guarded(...)`.
3. `TxSendGuard` currently has a single default behavior and backoff profile; introducing a **defaulted** policy parameter preserves current behavior for all existing call sites.

### Conclusion

If immediate mode is enabled **only** for relay command frames, impact on overall behavior is minimal:

- Existing traffic remains delayed/backoff-managed exactly as today.
- Only relay commands use immediate timing semantics.
- No scheduler-wide behavior change is required.

### Safe rollout recommendation

1. Add signature with default policy (`Delayed`) so current call sites compile unchanged.
2. Introduce one dedicated relay send wrapper:
    - `send_relay_cmd_immediate(...)` internally calls `send_to_receiver_guarded(..., SendPolicy::Immediate, 150)`.
3. Restrict immediate policy to `msg_type == RELAY_CMD` via runtime assert/log.
4. Keep all non-relay send paths untouched in phase 1.

This gives deterministic relay response while preserving existing system behavior.

---

## Summary: Solution to the Relay Problem

| Problem | Solution |
|---------|----------|
| Olimex ESP32-POE2 has no free GPIO after CAN + RS485 + Ethernet allocation | Move relay control to receiver (T-Display-S3) |
| T-Display-S3 display uses 13 GPIO for 8-bit parallel bus | Many pins consumed, but several remain free |
| Need 4 relay outputs | GPIO10, GPIO16, GPIO21, GPIO43 all available and safe |
| GPIO physical access | Solder pads on PCB back; consider TF Shield or custom breakout |
| Logic level / current | Use 4-ch opto-isolated relay module; 3.3V compatible |
| Transmitter must still command relays | Use ESP-NOW `RELAY_CMD`/`ACK` between Olimex → T-Display-S3 |
| Transmitter code expects HAL GPIO outputs | Provide virtual semantic GPIO IDs (`GPIO_NEGATIVE_CONTACTOR_VIRTUAL`, `GPIO_PRECHARGE_VIRTUAL`, `GPIO_POSITIVE_CONTACTOR_VIRTUAL`, `GPIO_BMS_POWER_VIRTUAL`) mapped to ESP-NOW bitmask |

The receiver board has sufficient GPIO for 4 relay channels without hardware modification to the firmware display or ESP-NOW communication paths.

---

## Firmware Implementation Notes

When adding relay control to the receiver firmware:

```cpp
// In hardware_config.h — add semantic contactor/BMS pin definitions
constexpr uint8_t GPIO_NEGATIVE_CONTACTOR = 10;
constexpr uint8_t GPIO_PRECHARGE = 16;
constexpr uint8_t GPIO_POSITIVE_CONTACTOR = 21;
constexpr uint8_t GPIO_BMS_POWER = 43;

// In setup() — initialise relay outputs
void initRelays() {
    pinMode(HardwareConfig::GPIO_NEGATIVE_CONTACTOR, OUTPUT);
    pinMode(HardwareConfig::GPIO_PRECHARGE, OUTPUT);
    pinMode(HardwareConfig::GPIO_POSITIVE_CONTACTOR, OUTPUT);
    pinMode(HardwareConfig::GPIO_BMS_POWER, OUTPUT);
    // Ensure relays default to OFF
    digitalWrite(HardwareConfig::GPIO_NEGATIVE_CONTACTOR, LOW);
    digitalWrite(HardwareConfig::GPIO_PRECHARGE, LOW);
    digitalWrite(HardwareConfig::GPIO_POSITIVE_CONTACTOR, LOW);
    digitalWrite(HardwareConfig::GPIO_BMS_POWER, LOW);
}
```

> Relay command triggers can be driven from the ESP-NOW message handler already present in the receiver, giving the transmitter (Olimex) indirect relay control over the wireless link.
