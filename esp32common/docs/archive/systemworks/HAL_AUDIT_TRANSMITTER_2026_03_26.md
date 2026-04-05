# HAL Audit — Transmitter Build (Olimex ESP32-POE2)
## Date: 2026-03-26

---

## Executive Summary

The `src/battery_emulator/devboard/hal/` directory contains **11 files** (10 headers + 1 .cpp)
that were copied wholesale from the original Battery Emulator project. This HAL system was designed
to abstract over **6 different hardware boards** (LilyGo, LilyGo2CAN, LilyGo T-Connect Pro, Stark,
3LB, and DevKit). In the 2-device build (Olimex ESP32-POE2 transmitter + display receiver), **none
of these board variants apply** and the entire GPIO-allocation machinery is dead weight.

**Critical finding:** 5 of the 6 HAL variant files are **never compiled** under any active build flag.
The one that IS compiled — `DevKitHal` (via `-DHW_DEVKIT`) — returns GPIO assignments for an
**ESP32 DevKit V1 board**, which is entirely wrong for the Olimex ESP32-POE2. The transmitter's
actual MCP2515 CAN driver (`src/communication/can/can_driver.h`) already bypasses the HAL entirely
with hardcoded correct pins, meaning the HAL's pin assignments are simultaneously **wrong and unused
at the hardware level**.

The `esp32hal` pointer is nonetheless referenced from **14 compiled files** across the battery
emulator subsystem, all of which were copied unchanged from the Battery Emulator project and call
`esp32hal->xxx()` for GPIO pin numbers that simply do not exist on the Olimex board (no contactors,
no RS485 transceiver, no SD card, no wake-up pins, no equipment stop button).

**Recommendation:** Delete all 11 HAL files. Replace the multi-board `Esp32Hal` class with a
single minimal `TransmitterHal` that returns `GPIO_NUM_NC` for every peripheral not present on the
Olimex POE2, and inline the two non-trivial usages (`available_interfaces()` and
`name_for_comm_interface()`) directly in the call sites.

---

## 1. File Inventory

### 1.1 HAL directory contents

```
ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/hal/
├── hal.cpp                      (compiled)
├── hal.h                        (compiled — included by 14 files)
├── hal_minimal.h                (compiled — included by hal.h)
├── hal_stub.h                   (on disk — NOT compiled in any active path)
├── hw_devkit.h                  (compiled — active via -DHW_DEVKIT build flag)
├── hw_lilygo.h                  (NOT compiled — requires -DHW_LILYGO)
├── hw_lilygo2can.h              (NOT compiled — requires -DHW_LILYGO2CAN)
├── hw_lilygo_t_connect_pro.h    (NOT compiled — requires -DHW_LILYGO_T_CONNECT_PRO)
├── hw_lilygo_t_connect_pro.cpp  (EXCLUDED — in platformio.ini build_src_filter)
├── hw_stark.h                   (NOT compiled — requires -DHW_STARK)
└── hw_3LB.h                     (NOT compiled — requires -DHW_3LB)
```

### 1.2 Compilation decision per file

| File | Compiled? | Reason |
|------|-----------|--------|
| `hal.cpp` | ✅ YES | `build_src_filter = +<*>` catches it; `init_hal()` called from `main.cpp` |
| `hal.h` | ✅ YES (as header) | Included directly by 14 files |
| `hal_minimal.h` | ✅ YES (as header) | Included by `hal.h` |
| `hal_stub.h` | ❌ NO | No active `#include` found in any compiled path |
| `hw_devkit.h` | ✅ YES (as header) | Included inside `hal.cpp` via `#if defined(HW_DEVKIT)` → `-DHW_DEVKIT` is set in `platformio.ini` |
| `hw_lilygo.h` | ❌ NO | Guarded by `#if defined(HW_LILYGO)` — never defined |
| `hw_lilygo2can.h` | ❌ NO | Guarded by `#if defined(HW_LILYGO2CAN)` — never defined |
| `hw_lilygo_t_connect_pro.h` | ❌ NO | Guarded by `#if defined(HW_LILYGO_T_CONNECT_PRO)` — never defined |
| `hw_lilygo_t_connect_pro.cpp` | ❌ NO | Explicitly excluded: `-<battery_emulator/devboard/hal/hw_lilygo_t_connect_pro.cpp>` |
| `hw_stark.h` | ❌ NO | Guarded by `#if defined(HW_STARK)` — never defined |
| `hw_3LB.h` | ❌ NO | Guarded by `#if defined(HW_3LB)` — never defined |

---

## 2. `esp32hal` Usage Map (Compiled Files Only)

All 14 files below are **actively compiled** into the transmitter binary. All of them reference
`esp32hal` for GPIO assignments or interface queries.

### 2.1 `src/main.cpp`

```
init_hal();                                           // creates DevKitHal on heap
esp32hal->name()                                      // returns "ESP32 DevKit V1" — WRONG board name
```

**Impact:** `init_hal()` is the entry point. Removing it makes `esp32hal` null, causing crashes
everywhere else unless also cleaned up. The name "ESP32 DevKit V1" is meaningless for this board.

---

### 2.2 `src/espnow/component_catalog_handlers.cpp`

```cpp
const auto interfaces = esp32hal->available_interfaces();  // line 610
```

Used inside `handle_request_inverter_interfaces()` to populate the inverter interface catalog
sent to the receiver. With `DevKitHal`, this returns `{Modbus, RS485, CanNative}`, which does NOT
match the actual Olimex POE2 capabilities (which only has SPI-based MCP2515 CAN — not native TWAI).

**Impact:** The inverter interface catalog advertised to the receiver is wrong. This should be a
hardcoded list specific to this board.

---

### 2.3 `src/battery_emulator/devboard/utils/events.cpp`

```cpp
esp32hal->failed_allocator()       // lines 495, 506
esp32hal->conflicting_allocator()  // line 497
```

Used only in `EVENT_GPIO_CONFLICT` and `EVENT_GPIO_NOT_DEFINED` error message formatting.
These events are only fired by `alloc_pins()` inside the HAL — which means they can only trigger
from HAL-mediated pin allocation, which should not happen on a fixed-purpose transmitter.

**Impact:** Low. These are error paths that shouldn't fire in practice. Safe to replace with
hardcoded `"unknown"` strings.

---

### 2.4 `src/battery_emulator/battery/BATTERIES.cpp`

```cpp
esp32hal->name_for_comm_interface(comm)          // line 169 — used in name_for_comm_interface() wrapper
esp32hal->WUP_PIN2()                             // line 714 — passed to BmwI3Battery constructor for battery2
```

`name_for_comm_interface()` in `BATTERIES.cpp` is just a wrapper that delegates directly to
`Esp32Hal::name_for_comm_interface()`. This can be inlined trivially.

`WUP_PIN2()` in `hw_devkit.h` returns `GPIO_NUM_32`. For the Olimex POE2 transmitter, GPIO 32
is the MCP2515 interrupt pin (from `can_driver.h`). This is a **pin conflict** — the DevKit HAL
is silently assigning the CAN interrupt line as a battery wakeup pin.

**Impact:** Moderate risk if BMW I3 battery2 is configured. The wakeup pin would conflict with
the CAN interrupt. However, battery2 is unlikely to be configured in practice.

---

### 2.5 `src/battery_emulator/battery/BMW-I3-BATTERY.h/cpp`

```cpp
esp32hal->WUP_PIN1()    // h line 28 — stored in constructor
esp32hal->alloc_pins()  // cpp line 539
```

`WUP_PIN1()` in `hw_devkit.h` returns `GPIO_NUM_25`. On the Olimex POE2, GPIO 25 is `EMAC_RXD0`
(Ethernet receive data). Silently reserving the Ethernet pin for BMS wake-up is dangerous.

---

### 2.6 `src/battery_emulator/battery/CHADEMO-BATTERY.h/cpp`

```cpp
esp32hal->CHADEMO_PIN_2/10/4/7()     // h lines 12–15
esp32hal->CHADEMO_LOCK()             // h line 16
esp32hal->PRECHARGE_PIN()            // h line 19
esp32hal->POSITIVE_CONTACTOR_PIN()   // h line 20
esp32hal->alloc_pins()               // cpp line 871
```

All CHAdeMO-specific and contactor pins. `DevKitHal` returns `GPIO_NUM_NC` for all CHAdeMO
pins (they are not defined), so these would silently fail `alloc_pins()` with
`EVENT_GPIO_NOT_DEFINED`. The Olimex POE2 has no CHAdeMO hardware.

---

### 2.7 `src/battery_emulator/battery/CMP-SMART-CAR-BATTERY.h/cpp`

```cpp
esp32hal->WUP_PIN1()    // same collision as BMW-I3 — GPIO_NUM_25 on DevKit = Ethernet RX on POE2
esp32hal->alloc_pins()
```

---

### 2.8 `src/battery_emulator/battery/DALY-BMS.cpp`

```cpp
auto rx_pin = esp32hal->RS485_RX_PIN();  // DevKitHal → GPIO_NUM_3 (UART0 RX!)
auto tx_pin = esp32hal->RS485_TX_PIN();  // DevKitHal → GPIO_NUM_1 (UART0 TX!)
esp32hal->alloc_pins()
```

`GPIO_NUM_1` and `GPIO_NUM_3` are the **serial debug port** (UART0 RX/TX). The DevKit HAL
assigns the RS485 UART to the same pins used by the Arduino Serial monitor. On the Olimex POE2
this would corrupt serial debug output if DALY-BMS RS485 is selected.

---

### 2.9 `src/battery_emulator/communication/can/comm_can.cpp`

This file is **compiled but not called** from the main transmitter flow (the transmitter uses
`src/communication/can/can_driver.cpp` instead). It uses HAL for ALL CAN pin lookups.

```cpp
esp32hal->CAN_TX_PIN()      // DevKitHal → GPIO_NUM_27
esp32hal->CAN_RX_PIN()      // DevKitHal → GPIO_NUM_26
esp32hal->CAN_SE_PIN()      // DevKitHal → GPIO_NUM_NC
esp32hal->MCP2515_CS()      // DevKitHal → GPIO_NUM_18  ⚠️ Olimex Ethernet MDIO = GPIO 18
esp32hal->MCP2515_INT()     // DevKitHal → GPIO_NUM_23  ⚠️ Olimex Ethernet MDC = GPIO 23
esp32hal->MCP2515_SCK()     // DevKitHal → GPIO_NUM_22  ⚠️ Olimex Ethernet TXD1 = GPIO 22
esp32hal->MCP2515_MOSI()    // DevKitHal → GPIO_NUM_21  ⚠️ Olimex Ethernet TX_EN = GPIO 21
esp32hal->MCP2515_MISO()    // DevKitHal → GPIO_NUM_19  ⚠️ Olimex Ethernet TXD0 = GPIO 19
esp32hal->MCP2517_CS/INT/SCK/SDO/SDI()
esp32hal->alloc_pins()
```

**5 of 5 MCP2515 DevKit HAL pins are Ethernet RMII pins on the Olimex POE2.** If
`comm_can.cpp` were ever called, it would silently destroy the Ethernet interface.

The actual transmitter CAN driver (`can_driver.h`) correctly uses GPIO 4, 13, 14, 15, 32 and is
completely independent of the HAL. **These two CAN stacks are in direct conflict.**

---

### 2.10 `src/battery_emulator/communication/contactorcontrol/comm_contactorcontrol.cpp`

```cpp
esp32hal->POSITIVE_CONTACTOR_PIN()         // DevKitHal → GPIO_NUM_5
esp32hal->NEGATIVE_CONTACTOR_PIN()         // DevKitHal → GPIO_NUM_16
esp32hal->PRECHARGE_PIN()                  // DevKitHal → GPIO_NUM_17
esp32hal->BMS_POWER()                      // DevKitHal → GPIO_NUM_NC
esp32hal->SECOND_BATTERY_CONTACTORS_PIN()  // DevKitHal → GPIO_NUM_32 ⚠️ CAN INT pin
esp32hal->always_enable_bms_power()
esp32hal->alloc_pins()
```

The Olimex POE2 transmitter has **no physical contactors**. GPIO 5 on the Olimex POE2 is safe but
unused. GPIO 32 is the CAN interrupt line — `SECOND_BATTERY_CONTACTORS_PIN()` would conflict.

---

### 2.11 `src/battery_emulator/communication/equipmentstopbutton/comm_equipmentstopbutton.cpp`

```cpp
esp32hal->EQUIPMENT_STOP_PIN()   // DevKitHal → GPIO_NUM_12
esp32hal->alloc_pins()
```

GPIO 12 on the Olimex POE2 is the Ethernet PHY power enable pin. Assigning it as an equipment
stop button input would immediately power down the LAN8720 Ethernet PHY.

---

### 2.12 `src/battery_emulator/communication/nvm/comm_nvm.cpp`

```cpp
esp32hal->set_default_configuration_values();   // line 47
```

`DevKitHal` inherits the base class no-op implementation, so this call does nothing. Safe to
remove.

---

### 2.13 `src/battery_emulator/communication/rs485/comm_rs485.cpp`

```cpp
esp32hal->RS485_EN_PIN()    // DevKitHal → GPIO_NUM_NC
esp32hal->RS485_SE_PIN()    // DevKitHal → GPIO_NUM_NC
esp32hal->PIN_5V_EN()       // DevKitHal → GPIO_NUM_NC
esp32hal->alloc_pins_ignore_unused()
```

All return `GPIO_NUM_NC`. The function effectively does nothing on this board. Safe to remove.

---

### 2.14 `src/battery_emulator/communication/precharge_control/precharge_control.cpp`

```cpp
esp32hal->HIA4V1_PIN()                       // DevKitHal → GPIO_NUM_4  ⚠️ CAN MISO pin
esp32hal->INVERTER_DISCONNECT_CONTACTOR_PIN()// DevKitHal → GPIO_NUM_5
esp32hal->alloc_pins()
```

GPIO 4 on the Olimex POE2 is the MCP2515 MISO line (`CANConfig::MISO_PIN = 4`). The HAL
assigns it as the auto-precharge control pin.

---

### 2.15 `src/battery_emulator/devboard/sdcard/sdcard.cpp`

```cpp
esp32hal->SD_MISO_PIN()    // DevKitHal → GPIO_NUM_NC
esp32hal->SD_MOSI_PIN()    // DevKitHal → GPIO_NUM_NC
esp32hal->SD_SCLK_PIN()    // DevKitHal → GPIO_NUM_NC
esp32hal->alloc_pins()
```

All return `GPIO_NUM_NC`. No SD card on the Olimex POE2.

---

### 2.16 `src/battery_emulator/inverter/SmaInverterBase.h` (+ SMA-BYD-H/HVS, SMA-TRIPOWER headers)

```cpp
contactorEnablePin = esp32hal->INVERTER_CONTACTOR_ENABLE_PIN();  // DevKitHal → GPIO_NUM_14
esp32hal->alloc_pins("SMA inverter", contactorEnablePin)
contactorLedPin = esp32hal->INVERTER_CONTACTOR_ENABLE_LED_PIN()  // DevKitHal → GPIO_NUM_2
esp32hal->alloc_pins("SMA inverter", contactorLedPin)
```

GPIO 14 on the Olimex POE2 is the MCP2515 SPI clock (`CANConfig::SCK_PIN = 14`). The SMA
inverter HAL would reserve the CAN SPI clock as an inverter contactor enable input.

---

## 3. GPIO Conflict Summary

| HAL Pin | HAL Returns | Olimex POE2 Reality | Conflict |
|---------|-------------|---------------------|----------|
| `MCP2515_CS()` | GPIO 18 | Ethernet MDIO | ⚠️ ETHERNET |
| `MCP2515_INT()` | GPIO 23 | Ethernet MDC | ⚠️ ETHERNET |
| `MCP2515_SCK()` | GPIO 22 | Ethernet TXD1 | ⚠️ ETHERNET |
| `MCP2515_MOSI()` | GPIO 21 | Ethernet TX_EN | ⚠️ ETHERNET |
| `MCP2515_MISO()` | GPIO 19 | Ethernet TXD0 | ⚠️ ETHERNET |
| `EQUIPMENT_STOP_PIN()` | GPIO 12 | Ethernet PHY power | ⚠️ ETHERNET |
| `WUP_PIN1()` | GPIO 25 | Ethernet EMAC_RXD0 | ⚠️ ETHERNET |
| `INVERTER_CONTACTOR_ENABLE_PIN()` | GPIO 14 | CAN SPI SCK | ⚠️ CAN |
| `HIA4V1_PIN()` | GPIO 4 | CAN SPI MISO | ⚠️ CAN |
| `SECOND_BATTERY_CONTACTORS_PIN()` | GPIO 32 | CAN SPI INT | ⚠️ CAN |
| `WUP_PIN2()` | GPIO 32 | CAN SPI INT | ⚠️ CAN |
| `RS485_TX_PIN()` | GPIO 1 | UART0 TX (debug) | ⚠️ UART0 |
| `RS485_RX_PIN()` | GPIO 3 | UART0 RX (debug) | ⚠️ UART0 |
| `POSITIVE_CONTACTOR_PIN()` | GPIO 5 | Safe (unused) | ✅ |
| `NEGATIVE_CONTACTOR_PIN()` | GPIO 16 | Safe (unused) | ✅ |
| `PRECHARGE_PIN()` | GPIO 17 | Safe (unused) | ✅ |

All 5 MCP2515 HAL pins conflict with the Ethernet RMII interface. The actual transmitter CAN
driver in `can_driver.h` uses the correct safe pins (GPIO 4, 13, 14, 15, 32) and is completely
independent of the HAL.

---

## 4. Why The Build Has Not Blown Up

The build currently works **despite** these conflicts for the following reasons:

1. **`comm_can.cpp` is compiled but never called.** The transmitter's `main.cpp` calls
   `CANDriver::instance().init()` from `src/communication/can/can_driver.cpp`. The battery
   emulator's `comm_can.cpp` `setup()` function is only invoked via `setup_battery()` →
   `battery->setup()`. Most batteries do not call `comm_can`, and the CAN channels used by
   batteries go through the BatteryEmulator's ACAN library, not the HAL-initialized TWAI controller.

2. **`alloc_pins()` is a software-only registry.** It only sets a flag and fires events — it
   does NOT call `pinMode()` or configure hardware. The conflicting GPIO assignments are only
   applied if `pinMode()` is later called with those pins. In practice, the conflicting features
   (CHAdeMO, equipment stop, precharge, contactors) are never initialised on this board because
   the battery types that use them are not selected.

3. **`DevKitHal::available_interfaces()` returns a plausible but wrong list.** The catalog
   is accepted by the receiver without validation — so the false capability advertisement goes
   undetected.

4. **`hw_lilygo_t_connect_pro.cpp` is explicitly excluded** from the build — this was the only
   HAL `.cpp` file that did hardware configuration at runtime.

---

## 5. File Deletion Scope

### 5.1 Files to delete (clear dead code — never compiled)

| File | Reason |
|------|--------|
| `hw_lilygo.h` | LilyGo v1 board — not this product |
| `hw_lilygo2can.h` | LilyGo 2-CAN board — not this product |
| `hw_lilygo_t_connect_pro.h` | LilyGo T-Connect Pro board — not this product |
| `hw_lilygo_t_connect_pro.cpp` | Already excluded from build; still on disk |
| `hw_stark.h` | Stark board — not this product |
| `hw_3LB.h` | 3LB board — not this product |
| `hal_stub.h` | Test stub — no active include |

### 5.2 Files to replace (compiled, requires cleanup)

| File | Action |
|------|--------|
| `hal.cpp` | Replace `init_hal()` with direct `TransmitterHal` instantiation; remove all `#if defined(HW_xxx)` branches |
| `hal.h` | Strip `Esp32Hal` class to minimal stub with `GPIO_NUM_NC` defaults; remove `WIFI_CORE` macro; rename class to `TransmitterHal` |
| `hw_devkit.h` | Delete — DevKit pin assignments are wrong for POE2; replaced by hardcoded NC values in `TransmitterHal` |
| `hal_minimal.h` | Keep (provides `hal/gpio_types.h` include) |

### 5.3 Call-site fixes required in non-HAL files

| File | Fix Required |
|------|-------------|
| `src/main.cpp` | Remove `init_hal()` call + `esp32hal->name()` log line; add direct `LOG_INFO` with board name |
| `src/espnow/component_catalog_handlers.cpp` | Replace `esp32hal->available_interfaces()` with hardcoded `{CanAddonMcp2515}` |
| `src/battery_emulator/devboard/utils/events.cpp` | Replace `esp32hal->failed_allocator()` with `"unknown"` |
| `src/battery_emulator/battery/BATTERIES.cpp` | Inline `name_for_comm_interface()` from base class |
| All other files (§2.5–§2.16) | Remove HAL includes; replace pin calls with `GPIO_NUM_NC` or remove the GPIO alloc block |

---

## 6. Recommended Implementation Plan

### Step 1 — Delete the 7 dead HAL variant files

```
hw_lilygo.h
hw_lilygo2can.h
hw_lilygo_t_connect_pro.h
hw_lilygo_t_connect_pro.cpp
hw_stark.h
hw_3LB.h
hal_stub.h
```

Build remains green — none of these are included under current build flags.

### Step 2 — Replace `hal.cpp` + `hal.h` + `hw_devkit.h`

Replace the multi-board `Esp32Hal` polymorphic system with a single fixed
`TransmitterHal` class whose every GPIO method returns `GPIO_NUM_NC`, and whose
`available_interfaces()` returns `{comm_interface::CanAddonMcp2515}`.

Then delete `hw_devkit.h`. Rebuild to verify.

### Step 3 — Fix call sites

Remove `init_hal()` from `main.cpp`.
Replace `esp32hal->available_interfaces()` in `component_catalog_handlers.cpp` with
the correct hardcoded list.
Replace GPIO-conflict event strings in `events.cpp` with static `"unknown"`.
Remove the `alloc_pins()` calls from battery/inverter/communication files (they are
no-ops on the POE2) or wrap them to return `true` unconditionally.

### Step 4 — Remove `-DHW_DEVKIT` build flag from `platformio.ini`

This flag served only to select `DevKitHal`. Once the HAL is replaced with a single
fixed `TransmitterHal`, it has no meaning.

### Step 5 — Rebuild and verify

Run `pio run -j 12` to confirm clean build.

---

## 7. Scope Not Covered Here

This audit covers **only** `src/battery_emulator/devboard/hal/`. The following related
issues are out of scope for this document but are noted:

- `comm_can.cpp` (Battery Emulator CAN setup) conflicts with `can_driver.cpp` (transmitter CAN
  driver). Both are compiled; only `can_driver.cpp` is called at runtime. `comm_can.cpp` should be
  excluded from the build or removed from the codebase.
- `devboard/wifi/wifi.cpp` is already excluded from the build via `build_src_filter`.
- `devboard/safety/safety.cpp`, `devboard/sdcard/sdcard.cpp`, `devboard/utils/led_handler.cpp`
  all reference GPIO pins that don't exist on the Olimex POE2. These are compiled but their
  `setup()` functions are not called from the transmitter's `main.cpp`.

---

*Report generated by audit pass — 2026-03-26*
*Next action: implement Steps 1–5 above*
