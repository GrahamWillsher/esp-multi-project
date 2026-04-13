# Waveshare ESP32-S3-Touch-LCD-7 — Minimal Display-Only Port Analysis

## Purpose

This document analyzes the existing `espnowreceiver_2` display implementation and defines exactly what is required to create a **new, stripped-down display-only application** for the **Waveshare ESP32-S3-Touch-LCD-7**.

Target outcome:

- keep only the visual effects you care about
- remove receiver/business logic that is not needed
- remove webserver, MQTT, ESP-NOW, config sync, OTA, and related infrastructure
- reproduce the following display behaviours only:
  - simulated flashing LED
  - SOC display text
  - power bar
  - power text

This document also defines the correct rendering architecture for the Waveshare RGB LCD panel.

---

## Executive Summary

### Short answer

Yes, the **visual behaviour** from `espnowreceiver_2` is portable.

No, the **receiver's `TFT_eSPI` backend** (designed for SPI/8080-type controllers) **cannot be ported** to the Waveshare ESP32-S3-Touch-LCD-7 — the display hardware architecture is fundamentally incompatible.

### Why

The current receiver display implementation is built around the **LilyGo T-Display-S3** and a **direct renderer using `TFT_eSPI`**, a library designed for SPI and 8080-type display controllers — which is exactly what the LilyGo board's ST7789 provides.

The Waveshare ESP32-S3-Touch-LCD-7 is a very different class of display board:

- official Waveshare product page identifies it as a **7-inch 800×480 RGB LCD**
- official Waveshare wiki says the board uses an **RGB LCD parallel bus**, not a SPI or 8080-style controller interface
- official Waveshare demos use **ESP32_Display_Panel** / `ESP_PanelBus_RGB`
- official Waveshare wiki says the board uses a **CH422G I/O expander** for board control signals such as backlight/reset-related functions
- official Waveshare wiki identifies the touch controller as **GT911** on the touch model

### Recommended direction

Build the new project as a **minimal direct-render RGB panel display app** for the Waveshare board, using the **Arduino framework + LovyanGFX**.

Specifically:

- preserve the **receiver's rendering concepts** and widget logic
- preserve the **simple non-LVGL philosophy**
- use **LovyanGFX** for panel bring-up, CH422G expander control, and drawing primitives — it is the correct RGB-panel equivalent of `TFT_eSPI`
- adapt widget code with minimal changes: `tft.` calls become `display.` calls, layout coordinates are rescaled to 800×480

That gives you the same visible effect you want without dragging in the receiver’s wider application stack.

---

## What Was Analyzed in `espnowreceiver_2`

The receiver codebase was analyzed specifically for the display path and its runtime dependencies.

### Core display API

The application-facing display surface is deliberately small:

- `src/display/display.h`
- `src/display/display.cpp`
- `src/display/display_interface.h`

These define a minimal hardware-agnostic contract:

- `init()`
- `display_initial_screen()`
- `update_soc(float)`
- `update_power(int32_t)`
- `show_status_page()`
- error/fatal screens
- `task_handler()`

This is good news: the higher-level display API is already quite small and is portable in concept.

### Current receiver backend (receiver-specific, not applicable to Waveshare)

The active receiver backend targets the LilyGo T-Display-S3:

- `src/display/tft_impl/tft_display.h`
- `src/display/tft_impl/tft_display.cpp`

It uses:

- `TFT_eSPI` (a library for SPI/8080-type controllers — not applicable to the Waveshare RGB LCD)
- direct drawing to the panel
- synchronous/blocking rendering
- manual timing loops instead of LVGL
- direct GPIO/backlight/panel power control

This backend is tightly coupled to the **LilyGo T-Display-S3** hardware layout and **cannot be used on the Waveshare board**.

### Visual elements you want to keep

#### 1. Simulated LED

Files:

- `src/display/display_led.h`
- `src/display/display_led.cpp`
- LED timing in `src/config/led_config.h`
- runtime animation loop in `src/main.cpp`

What it does:

- draws a filled circle as a fake LED
- supports these colours:
  - red
  - green
  - orange
  - blue
  - teal
- supports these effects:
  - continuous on
  - flash
  - heartbeat

Important detail:

The LED animation is currently **not** a complex subsystem. It is a small state machine driven by time (`millis()`) and simple draw/clear operations.

This is easy to port.

#### 2. SOC display

Files:

- `src/display/pages/status_page.cpp`
- `src/display/widgets/proportional_number_widget.h`
- `src/display/widgets/proportional_number_widget.cpp`
- gradient state in `src/common.h` / `src/globals.cpp`

What it does:

- renders a large central SOC number
- uses a proportional font while stabilizing layout by boxing each digit in equal-width regions
- supports configurable precision
- colours the SOC text via a precomputed gradient

Important detail:

The most reusable idea here is not the exact code but the rendering behaviour:

- centered value
- stable numeric layout
- colour gradient based on SOC percentage

That is portable.

#### 3. Power bar + power text

Files:

- `src/display/widgets/power_bar_widget.h`
- `src/display/widgets/power_bar_widget.cpp`
- equivalent logic also exists in the receiver's display backend helpers

What it does:

- renders a center marker
- displays discharge bars on one side and charge bars on the other
- uses colour gradients:
  - blue→red for discharge
  - blue→green for charge
- animates a pulse/ripple when power magnitude is unchanged but non-zero
- renders numeric watt text beneath the bar

Important detail:

This is also very portable because the logic is based on rectangles, gradients, and text, not on receiver-specific networking.

---

## Receiver Code That Is Not Required

For your new Waveshare display-only project, the following receiver areas are **not required**:

### Not required at all

- webserver code under `lib/webserver`
- MQTT stack under `src/mqtt`
- ESP-NOW code under `src/espnow`
- receiver/transmitter config managers
- connection state machines
- OTA boot guard
- firmware metadata/versioning extras beyond a minimal version string
- LittleFS splash image loading
- runtime task startup architecture
- queue-based display snapshot transport
- receiver health gate / bootstrap phase runner

### Can be removed or replaced with much simpler logic

- `RTOS::tft_mutex`
- `DisplayUpdateQueue`
- task-per-subsystem structure
- global receiver state in `common.h`
- most of `main.cpp`
- most compile-time logging infrastructure

### Optional only

- splash screen
- backlight fade effects
- touch support
- SD card support
- CAN/RS485/I2C peripheral support

For your stated goal, none of those are needed in the first implementation.

---

## Minimal Functional Scope for the New Project

The new project only needs to do this:

1. initialize the Waveshare display hardware
2. turn on panel/backlight correctly
3. clear the screen to a chosen background colour
4. render a fake LED indicator
5. render a SOC number
6. render a power bar
7. render a power text string
8. periodically update those elements from locally generated/demo values

That is all.

---

## Official Waveshare Hardware Findings

The following was gathered from official Waveshare sources:

- official product page: `https://www.waveshare.com/esp32-s3-touch-lcd-7.htm`
- official wiki: `https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7`

### Confirmed facts

#### Board and memory

- ESP32-S3 based board
- 16MB Flash
- 8MB PSRAM

#### Display

- 7-inch display
- `800×480` resolution
- `65K` colour
- `IPS` panel
- `170°` viewing angle

#### Interface type

This is the critical finding.

Official Waveshare material indicates:

- the board integrates an **RGB interface LCD**
- official Arduino demo creates `ESP_PanelBus_RGB`
- official wiki references Espressif RGB LCD documentation

So this board **cannot use `TFT_eSPI`** — it is an RGB LCD, not a SPI or 8080-type controller board. The correct graphics library is **LovyanGFX**, configured with a custom Waveshare board definition that targets the ESP32-S3 RGB panel driver internally.

#### Touch

For the touch version:

- `5-point` capacitive touch
- controller: `GT911`
- interface: `I2C`
- interrupt support available

Touch is optional for your display-only goal.

#### Board control signals

Official Waveshare material also states:

- the board uses a **CH422G** I/O expander
- the expander is used because the 7-inch RGB panel consumes most GPIOs
- reset / backlight-related control is handled through that expander path
- official demo descriptions explicitly reference these expander-controlled signals:
  - `TP_RST`
  - `LCD_BL`
  - `LCD_RST`
  - `SD_CS`
  - `USB_SEL`
- one exact official detail: `SD_CS` is driven by **CH422G EXIO4**

#### Related component references from official resources

Waveshare’s official wiki resources also link:

- `ST7262` datasheet
- `GT911` datasheet
- `CH422G` datasheet
- schematic PDF
- official demo zip

### Important constraint from official Waveshare notes

The official wiki states that the **7-inch screen occupies the vast majority of the GPIO**.

That matters because it means:

- board bring-up is not a simple GPIO assignment — the RGB parallel bus consumes nearly all available ESP32-S3 GPIOs
- the panel interface and control path should be treated as vendor-board-specific
- the safest path is to start from the official board/display configuration model

---

## What Could Not Be Confirmed Directly From the Readable Official HTML

The official HTML pages clearly establish the overall display architecture, but they do **not** cleanly expose all exact GPIO numbers for:

- RGB data pins
- `DE`
- `VSYNC`
- `HSYNC`
- `PCLK`
- exact expander pin mapping for `LCD_BL`, `LCD_RST`, `TP_RST`

To lock those down precisely, the next official sources to consult are:

- Waveshare schematic PDF: `ESP32-S3-Touch-LCD-7-Sch.pdf`
- Waveshare demo zip: `ESP32-S3-Touch-LCD-7-Demo.zip`
- especially board config files such as `ESP_Panel_Board_Custom.h` or equivalent

For the purpose of architecture and feasibility analysis, the currently confirmed official information is already enough.

---

## Rendering Architecture for the Waveshare RGB LCD

## Bottom-line recommendation

**Build this new Waveshare project as a minimal direct-render RGB panel project.**

The Waveshare ESP32-S3-Touch-LCD-7 uses an **RGB LCD interface** — this is fundamentally different from the SPI/8080 controller paradigm used on the receiver board and requires a completely different hardware bring-up and rendering path.

### Why the receiver board uses a different approach

The receiver board is a **LilyGo T-Display-S3** with an ST7789 controller driven via an 8-bit parallel (8080) bus:

- configured via `src/hal/tft_espi_user_setup.h`
- uses `TFT_eSPI`, which targets SPI and 8080-type display controllers
- direct control of panel power and backlight on local GPIOs

That stack is appropriate on the receiver because the LilyGo hardware is SPI/8080-compatible. It is not transferable to the Waveshare board.

### Why the Waveshare board requires RGB LCD drivers

The Waveshare ESP32-S3-Touch-LCD-7 is an **RGB LCD display board**:

- officially described as an **RGB interface LCD** on the Waveshare product page and wiki
- initialized with `ESP_PanelBus_RGB` in all official Waveshare examples
- dependent on `CH422G` expander-controlled signals for backlight, reset, and board functions
- documented with Espressif RGB LCD driver guidance
- the RGB parallel bus occupies the vast majority of available GPIOs
- panel chip: **ST7262**

`TFT_eSPI` does **not** support the RGB panel interface and is not a valid library choice for this board.

### Correct rendering architecture for the Waveshare board

The correct approach is:

- **LovyanGFX** as the graphics library — it natively supports the ESP32-S3 RGB LCD interface, handles the CH422G expander internally, and provides a drawing API almost identical to `TFT_eSPI`
- **Arduino framework** via PlatformIO — lower complexity than ESP-IDF for this scope, consistent with existing workflow
- **no `TFT_eSPI`** — incompatible with the RGB panel interface
- **no LVGL** — not required for this scope

See the Framework Selection section for the full comparison and rationale.

### Rendering philosophy

The receiver's rendering philosophy — direct circle/rect/text drawing, no UI framework overhead, no LVGL — is exactly right for this project.

LovyanGFX implements that philosophy natively on the RGB panel. The widget code from the receiver adapts with minimal changes: `tft.fillCircle(...)` becomes `display.fillCircle(...)`, `tft.drawString(...)` becomes `display.drawString(...)`, and so on.

In other words:

- keep the receiver's **rendering philosophy and widget concepts**
- replace `TFT_eSPI` with **LovyanGFX** configured for the Waveshare RGB panel hardware

---

## What Must Be Carried Forward From `espnowreceiver_2`

Only a small subset of the receiver needs to be conceptually preserved.

### Keep conceptually

#### LED model

Keep:

- `LEDColor`
- `LEDEffect`
- flash timing
- heartbeat timing

The current receiver timings are:

- flash: `500ms on / 500ms off`
- heartbeat: `120 / 100 / 120 / 760 ms`

These can be carried across directly.

#### SOC rendering behaviour

Keep:

- centered large number
- precision control
- colour gradient by value
- stable digit layout

#### Power bar behaviour

Keep:

- center marker
- left/right semantic meaning
- colour gradients
- pulse/ripple for stable non-zero power
- text value below the bar

#### Colour/theme behaviour

Keep or adapt:

- black background
- white text for neutral labels
- blue center marker
- green charge side
- red discharge side
- coloured LED indicator

### Do not carry forward as-is

- `common.h` global state sprawl
- FreeRTOS display queueing
- receiver mutex patterns unless needed
- display interface complexity beyond the new app’s needs
- splash/JPEG/LittleFS pipeline
- error-state screens unless you actually want them

---

## Minimal New Project Architecture Required

The new project should be intentionally tiny.

## Suggested modules

### 1. Board bring-up and drawing surface

With LovyanGFX chosen as the graphics library, this module is a **LovyanGFX board configuration struct** rather than bespoke init code.

Purpose:

- initialize the Waveshare RGB LCD via LovyanGFX's `LGFX_Device` with a custom board config
- configure the RGB bus pin mapping
- configure the CH422G I2C expander for backlight and reset control
- configure panel dimensions (800×480), color depth, and timing

Suggested files:

- `src/board/waveshare_lgfx_config.h` — the custom `LGFX_Device` subclass with pin definitions
- `src/board/board_init.h` / `board_init.cpp` — thin wrapper: `board_init()` → `display.init()`, `backlight_on()`, `backlight_off()`

Responsibilities:

- one-call board initialization
- backlight on/off
- panel reset

LovyanGFX also exposes the drawing API directly on the device object — no separate renderer layer is required.

### 2. Display reference

LovyanGFX provides all drawing primitives directly — no separate renderer wrapper layer is needed.

The display object (the `LGFX_Device` subclass instance) is passed by reference to widgets. Widgets call:

- `display.fillCircle(x, y, r, colour)` — LED
- `display.fillRect(x, y, w, h, colour)` — bar segments, clear regions
- `display.setFont(...)` / `display.drawString(...)` — SOC number, power text

This is the same call pattern as the receiver's `tft.` calls, so widget code adapts with minimal effort.

### 3. Widgets

Suggested files:

- `src/widgets/led_widget.h`
- `src/widgets/led_widget.cpp`
- `src/widgets/soc_widget.h`
- `src/widgets/soc_widget.cpp`
- `src/widgets/power_bar_widget.h`
- `src/widgets/power_bar_widget.cpp`

Responsibilities:

- own visual state
- own redraw logic
- own dirty-region logic where useful

### 4. Demo state model

Suggested files:

- `src/app/demo_model.h`
- `src/app/demo_model.cpp`

Responsibilities:

- hold current SOC, power, LED colour, LED effect
- generate demo values if no external input source exists

### 5. Main loop

Suggested file:

- `src/main.cpp`

Responsibilities:

- initialize board and renderer
- initialize widgets
- periodically update demo state
- drive LED timing
- redraw only changed regions

---

## Exact Functional Requirements to Carry Out Development

To build this properly, the following are required.

## Required technical inputs

### 1. Framework selection

**Decision: Arduino framework via PlatformIO, with LovyanGFX as the graphics library.**

#### Arduino vs ESP-IDF for this project

The official Waveshare wiki describes both frameworks:

> *"Arduino is suitable for beginners and non-professionals because it is easy to learn and quick to get started. ESP-IDF is a better choice for developers with a professional background or high performance requirements, as it provides more advanced development tools and greater control capabilities for the development of complex projects."*

Applied to this specific project:

| Criterion | Arduino + LovyanGFX | ESP-IDF + `esp_lcd` |
|-----------|---------------------|---------------------|
| Setup complexity | Low — PlatformIO Arduino target, familiar workflow | High — VS Code IDF plugin, separate `idf.py` toolchain |
| RGB LCD bring-up | LovyanGFX handles panel bus, expander, and frame buffer in one library | `esp_lcd` RGB component — lower level, more boilerplate |
| CH422G expander | Handled internally by LovyanGFX board config | Requires separate I2C expander component |
| Graphics API | Near-identical to `TFT_eSPI` — `fillCircle`, `fillRect`, `drawString` | None — must build own primitive renderer on raw frame buffer |
| Widget porting effort | **Low** — existing widget algorithms adapt with minimal code changes | High — all draw calls must target raw frame buffer |
| Font support | Same FreeFont family as the receiver's `TFT_eSPI` code | Requires custom font pipeline |
| Update rate for this UI | More than adequate — UI updates at 1–10 Hz | More capable, but irrelevant at this update rate |
| Official Waveshare demos | `08_DrawColorBar` (RGB panel test) provided for Arduino | `08_lvgl_Porting` provided for ESP-IDF |
| PlatformIO compatible | Yes — `lvgl/lvgl` and `moononournation/GFX Library for Arduino` both available | Yes — but different project structure |

#### Verdict

For a minimal non-LVGL direct-render display app, **Arduino + LovyanGFX is the correct choice**:

- LovyanGFX natively supports the ESP32-S3 RGB LCD interface
- LovyanGFX internally manages the CH422G expander for backlight and reset — no separate expander library required
- LovyanGFX provides the same drawing primitives (`fillCircle`, `fillRect`, `drawString`) as `TFT_eSPI`, making the receiver widget algorithms directly portable
- Consistent with the existing PlatformIO-based workflow
- No LVGL dependency required

ESP-IDF would be the right choice only if frame rate optimization or fine-grained DMA/PSRAM control becomes a requirement. For this project's scope it adds unnecessary complexity.

#### LVGL advantages (and why it is still optional here)

LVGL does have real advantages, even on this board:

- rich built-in widgets (buttons, charts, gauges, lists, animations)
- built-in touch/event model and focus/navigation handling
- theme/style system for consistent multi-screen UI design
- easier long-term expansion to menus/settings pages
- large ecosystem and many examples

However, for this specific project (single status screen: LED + SOC + power bar + power text), LVGL is not required and introduces avoidable complexity:

- larger memory footprint and integration overhead
- more task/tick/flush plumbing than immediate-mode drawing
- slower iteration for a very small fixed UI

Decision for this project:

- **start without LVGL** (LovyanGFX direct rendering)
- keep widget APIs clean so LVGL can be adopted later if UI scope expands

Trigger to adopt LVGL later:

- more than one interactive screen
- touch-driven settings UI
- reusable UI controls beyond the current three custom widgets

#### Chosen library stack

| Layer | Choice |
|-------|--------|
| Build framework | Arduino via PlatformIO |
| Board target | `esp32s3dev` (ESP32-S3 Dev Module, 16MB Flash, 8MB OPI PSRAM) |
| RGB panel + expander + frame buffer | **LovyanGFX** with custom Waveshare board configuration |
| Graphics drawing API | LovyanGFX (`fillCircle`, `fillRect`, `drawString`, etc.) |
| LVGL | Not used |

### 2. Exact board init details

Required to proceed confidently:

- exact RGB panel pin mapping
- exact expander setup sequence
- exact backlight/reset control sequence
- panel timing values used by the official board config

These should be taken from official Waveshare resources, preferably from:

- schematic PDF
- demo zip board config

### 3. Font strategy

**Resolved — use LovyanGFX built-in FreeFont family, scaled for 800×480.**

LovyanGFX includes the same FreeFont family used in the receiver's `TFT_eSPI` implementation. The same font names are available, meaning the font calls in existing widget code require only object substitution.

| Element | Font | Rationale |
|---------|------|-----------|
| SOC number | `FreeSansBold`, large (48–64pt) | Dominant screen element — must be clearly readable at distance |
| Power text | `FreeSansBold`, medium (18–24pt) | Secondary information — readable but subordinate to SOC |
| Any bar labels | `FreeSansBold`, medium (18pt) | Consistent with power text |

For reference, the receiver uses `FreeSansBold18pt7b` for SOC and `FreeSansBold9pt7b` for power text on a 170px-tall screen. Scaling proportionally to 480px height gives approximately 48pt for SOC and 25pt for power text. Final sizes should be confirmed visually on the hardware.

### 4. Layout for 800×480

The current receiver UI was built for `320×170`. The Waveshare board is `800×480`. A direct coordinate copy would look wrong.

The new layout uses **proportional coordinates** (fractions of SCREEN_WIDTH / SCREEN_HEIGHT) so it scales correctly — this is consistent with how the receiver layout was already structured. The full layout is defined in the Layout Recommendations section of this document, including derived pixel values.

### 5. Minimal test/demo behaviour

Need a local animation/demo mode to prove the port works without any receiver stack.

Recommended demo sequence:

- LED cycles through continuous / flash / heartbeat
- SOC slowly ramps from 0–100 and back
- power sweeps negative to positive to demonstrate both sides of the bar
- power text updates in sync

This removes all dependency on networking or external telemetry.

---

## Receiver Display Logic That Can Be Reused Almost Verbatim

### Good candidates for direct reuse after adaptation

- LED timing constants from `src/config/led_config.h`
- LED state machine logic from `task_led_renderer()` in `src/main.cpp`
- power bar segment-count logic from `power_bar_widget.cpp`
- pulse-phase logic from `power_bar_widget.cpp`
- proportional numeric display concepts from `proportional_number_widget.cpp`
- SOC gradient concept and thresholds

### Logic that should be rewritten, not copied

- any code depending on `TFT_eSPI`
- any code depending on LilyGo GPIO definitions
- FreeRTOS task/mutex queue choreography
- LittleFS/JPEG splash handling
- receiver application bootstrap sequencing

---

## Layout Recommendations for the Waveshare 800×480 Screen

### Positioning approach: proportional coordinates

All element positions are expressed as **proportional fractions of SCREEN_WIDTH and SCREEN_HEIGHT**, not as hard-coded pixel offsets. This means the layout scales correctly to any display resolution — the 800×480 Waveshare screen and any future screen share the same proportional layout logic.

This was confirmed valid for this project: the original receiver layout is also proportionally derived (e.g., SOC at `SCREEN_WIDTH/2`, LED at `DISPLAY_WIDTH - 2 - STATUS_INDICATOR_SIZE`). The same proportional approach carries forward.

### Proposed screen composition

- **right-side center**: simulated LED — vertically centered on screen, right-aligned with small margin from right edge
- **upper-middle**: SOC number
- **mid-lower**: power bar
- **bottom-middle**: power text

### Proposed proportional layout

| Element | X | Y | Notes |
|---------|---|---|-------|
| LED center | `SCREEN_W - LED_MARGIN_RIGHT - LED_RADIUS` | `SCREEN_H / 2` | Vertically centered, right-aligned |
| SOC center | `SCREEN_W / 2` | `SCREEN_H × 27%` | Upper third of screen |
| Power bar centerline | `SCREEN_W / 2` | `SCREEN_H × 62%` | Below center |
| Power text baseline | `SCREEN_W / 2` | `SCREEN_H × 88%` | Near bottom |

### Derived pixel values for 800×480

Using `SCREEN_W = 800`, `SCREEN_H = 480`, `LED_RADIUS = 25`, `LED_MARGIN_RIGHT = 20`:

```cpp
constexpr int SCREEN_W          = 800;
constexpr int SCREEN_H          = 480;

// LED — vertically centred, right-aligned with margin
constexpr int LED_RADIUS        = 25;
constexpr int LED_MARGIN_RIGHT  = 20;
constexpr int LED_X             = SCREEN_W - LED_MARGIN_RIGHT - LED_RADIUS;  // 755
constexpr int LED_Y             = SCREEN_H / 2;                              // 240

// SOC number
constexpr int SOC_X             = SCREEN_W / 2;                              // 400
constexpr int SOC_Y             = SCREEN_H * 27 / 100;                       // 130

// Power bar
constexpr int BAR_CENTRE_X      = SCREEN_W / 2;                              // 400
constexpr int BAR_Y             = SCREEN_H * 62 / 100;                       // 298

// Power text
constexpr int POWER_TEXT_X      = SCREEN_W / 2;                              // 400
constexpr int POWER_TEXT_Y      = SCREEN_H * 88 / 100;                       // 422
```

The LED radius of 25px is proportionally equivalent to the receiver's 9px radius on a 170px-tall screen (9/170 × 480 ≈ 25px).

### Suggested power bar treatment

Because the screen is much wider than the LilyGo screen, increase the bar sophistication slightly:

- 16 to 24 segments per side
- persistent center marker
- 3 to 6 pixel gaps depending on chosen geometry
- thicker bar height than the receiver version

This will preserve the look while taking advantage of the larger display.

---

## Minimal Non-LVGL Rendering Strategy

This is the recommended implementation style.

### Library: LovyanGFX

LovyanGFX is the chosen graphics library. It:

- initializes the RGB panel bus, the CH422G expander, and the frame buffer in a single board configuration
- exposes a drawing API that is a near drop-in replacement for `TFT_eSPI`
- requires no LVGL
- runs within the Arduino framework under PlatformIO

A custom board configuration struct must be written for the Waveshare ESP32-S3-Touch-LCD-7, specifying the RGB bus pin mapping, panel dimensions, CH422G I2C address, and backlight/reset expander pin assignments. The exact values come from the Waveshare schematic PDF.

### Principles

- no LVGL
- no widget framework overhead beyond a few custom classes
- no webserver
- no network stack
- no filesystem dependency
- no asynchronous command plane required
- redraw only what changed

### Draw operations needed

Only a very small drawing surface is needed:

- clear screen
- fill rectangle
- draw line
- fill circle
- draw text
- optional off-screen canvas or frame buffer region

### Why this is enough

Your requested UI contains only:

- one circle
- one large numeric field
- one segmented bar
- one text label/value

So the new project can remain extremely small.

---

## Risks and Constraints

## Main technical risk

The biggest risk is **not** the UI logic.

The biggest risk is **board bring-up** on the Waveshare RGB display path if the exact board configuration is not pulled from official resources.

### Why

The receiver UI code is simple.

The Waveshare board hardware path is the non-trivial part because of:

- RGB LCD timing
- CH422G-controlled signals
- board-specific init order

### Risk level by area

- LED/SOC/power widget logic: **low risk**
- 800×480 layout adaptation: **low risk**
- text rendering choice: **low risk**
- Waveshare panel bring-up: **medium risk**
- using `TFT_eSPI` on the Waveshare RGB LCD: **not viable** (incompatible with RGB panel interface — `TFT_eSPI` does not support RGB parallel bus)

---

## Recommended Development Plan

### Implementation location

Active implementation codebase:

- `esp32common/espnowreceiver_LCD`

### Current phase status (2026-04-13)

- ✅ Phase 1 complete — hardware bring-up baseline implemented
- ✅ Phase 2 complete — primitive drawing path implemented
- ✅ Phase 3 complete — minimal widgets implemented
- ✅ Phase 4 complete — autonomous demo state engine implemented
- ⏳ Phase 5 in progress — fit/polish and hardware-specific validation remaining

### Phase-5 investigation update — boot reset loop

Observed on hardware:

- repeated ROM boot with `rst:0x3 (RTC_SW_SYS_RST)` and `Saved PC:0x403cdada`

Root cause identified:

- the project was initially using `esp32-s3-devkitc-1` (N8/no-PSRAM profile), which does not match Waveshare ESP32-S3-Touch-LCD-7 hardware (N16R8)

Corrective action implemented:

- created custom board definition `boards/waveshare_esp32s3_n16r8.json`
- switched project board to `waveshare_esp32s3_n16r8` in `platformio.ini`
- corrected image header flash size to 8MB to match detected hardware
- added phase-5 bring-up diagnostics: startup color-cycle test and dual logging over `Serial` + `Serial0`

This fix addresses the board-profile mismatch layer before display-driver tuning and improves hardware observability.

## Phase 1 — Hardware bring-up only

Deliverable:

- blank screen comes up reliably
- backlight can be controlled
- one test rectangle can be drawn
- one line of text can be drawn

No LED/SOC/power logic yet.

**Implemented in this phase:**

- custom LovyanGFX RGB/CH422G bring-up class created in `src/hal/lgfx_waveshare_7.h`
- PlatformIO Arduino project scaffold created in `platformio.ini`
- startup "PHASE 1 OK" banner + rectangle bring-up check added in `src/main.cpp`
- successful compile validation completed with `pio run -j 2`

**Important note:**

The current RGB/CH422G pin mapping is a phased baseline and must still be cross-checked against the official **ESP32-S3-Touch-LCD-7** schematic before final hardware sign-off.

## Phase 2 — Primitive rendering layer

Deliverable:

- circle draw works
- rect fill works
- text draw works
- partial region clear works

**Implemented in this phase:**

- direct render path built with LovyanGFX primitives (`fillScreen`, `fillRect`, `fillCircle`, `drawString`)
- relative layout helper added in `src/ui/layout.h`
- color helpers added in `src/ui/colors.h`

## Phase 3 — Minimal widgets

Deliverable:

- LED widget implemented
- SOC widget implemented
- power bar widget implemented
- power text implemented

**Implemented in this phase:**

- LED widget with continuous/flash/heartbeat timing added in `src/ui/led_widget.{h,cpp}`
- SOC widget with large text rendering added in `src/ui/soc_widget.{h,cpp}`
- power bar + medium power text widget added in `src/ui/power_bar_widget.{h,cpp}`
- LED placement implemented as requested: vertically centered and right-aligned with margin

## Phase 4 — Demo state engine

Deliverable:

- autonomous demo animation
- no receiver dependencies
- no network dependencies

**Implemented in this phase:**

- autonomous SOC/power waveform model added in `src/app/demo_model.{h,cpp}`
- LED effect/color cycling integrated in `src/main.cpp`
- frame/update scheduling loop added with no receiver stack dependencies

## Phase 5 — Fit and polish

Deliverable:

- tuned positions
- tuned font sizes
- tuned animation timing
- tuned gradients/colours

---

## Acceptance Criteria for the New Project

The new implementation is successful if it:

- boots directly to a working display on the Waveshare board
- uses only minimal board/display dependencies
- does not include receiver webserver/network/config stack
- shows a simulated LED with continuous, flash, and heartbeat effects
- shows SOC in a large readable numeric format
- shows a bidirectional power bar
- shows power text in watts
- updates smoothly and reliably on the 800×480 panel

Optional success criteria:

- touch disabled entirely in first version
- no LVGL dependency
- no filesystem dependency
- no background tasks required beyond the main loop

---

## Final Recommendation

To carry out this development, I do **not** need the receiver’s non-display subsystems.

I only need:

- the display concepts and small pieces of rendering logic from `espnowreceiver_2`
- official Waveshare board/display configuration details
- a new, clean project structure built around the Waveshare RGB panel

### Recommended implementation stance

- **reuse the receiver's display behaviour and widget algorithms**
- **do not reuse the receiver's application architecture**
- **use Arduino framework + LovyanGFX** — `TFT_eSPI` is incompatible with this board's RGB interface; LovyanGFX is its correct equivalent for RGB panels
- **build a simple non-LVGL direct-render UI** — LovyanGFX provides all the required drawing primitives

That is the cleanest and lowest-risk way to get exactly what you asked for:

- flashing simulated LED
- SOC
- power bar
- power text
- nothing else

---

## Practical Next Step

With phases 1-4 now implemented, the next concrete steps are:

1. verify every RGB/CH422G mapping in `src/hal/lgfx_waveshare_7.h` against `ESP32-S3-Touch-LCD-7-Sch.pdf`
2. flash and validate on real hardware (backlight, sync stability, orientation, artifact-free refresh)
3. tune phase-5 fit/polish items (segment geometry, font sizing, spacing) directly on panel
4. add dirty-region redraw optimization (optional performance pass)
5. add touch integration only if/when interactive pages are required

At that point the receiver codebase no longer needs to come along except as a visual reference and a source of the widget algorithms.
