# Waveshare 7" LCD (espnowreceiver_LCD) — LVGL Migration Plan and Compile-Time Backend Switch

Date: 2026-04-13
Project reviewed: espnowreceiver_LCD

## 1) Current non-LVGL UI architecture (what exists today)

The current implementation is a custom immediate-mode renderer using LovyanGFX draw calls:

- `src/main.cpp`
  - `run_splash_sequence()` (JPEG splash + backlight fade)
  - `render_frame()` (manual per-widget draw)
- `src/ui/soc_widget.cpp`
  - manual text rendering + selective character redraw optimization
- `src/ui/power_bar_widget.cpp`
  - manual segmented bars + ripple + text updates
- `src/ui/led_widget.cpp`
  - manual LED effects (`CONTINUOUS`, `FLASH`, `HEARTBEAT`, `PULSE`)

This works, but polished UI behavior (styles, animation composition, hierarchy, theming) is harder than with LVGL.

---

## 2) Goal

Create a **full LVGL backend** that performs the same UI jobs as today, with the ability to select backend at compile time:

- Backend A: existing non-LVGL renderer
- Backend B: LVGL renderer

Compile-time selectable with one macro and/or one PlatformIO environment.

---

## 3) Recommended compile-time switch design

### 3.1 Add a single backend macro

Use one macro in build flags:

- `-DUI_BACKEND_LVGL=1`  -> LVGL backend
- absent or `0`          -> non-LVGL backend

### 3.2 Add two PlatformIO environments

In `espnowreceiver_LCD/platformio.ini`:

- `env:waveshare_esp32s3_lcd7` (existing non-LVGL)
- `env:waveshare_esp32s3_lcd7_lvgl` (same board + `-DUI_BACKEND_LVGL=1` + LVGL deps)

### 3.3 Backend façade (single app-facing API)

Create a backend-neutral layer under `src/ui/runtime/`:

- `ui_runtime.h/.cpp` (public façade)
- `ui_backend_non_lvgl.cpp`
- `ui_backend_lvgl.cpp`

App (`main.cpp`) calls only façade functions; backend chosen by macro.

---

## 4) Suggested new LVGL functions (same work as current code)

These are the concrete functions to add.

## 4.1 Backend lifecycle

```cpp
bool ui_backend_init(lgfx::LGFX_Device& display);
void ui_backend_set_brightness(uint8_t level);
void ui_backend_tick(uint32_t now_ms);
```

Purpose:
- initialize LVGL display driver and root objects
- central place for `lv_timer_handler()` timing

## 4.2 Splash sequence (LVGL version)

```cpp
void ui_splash_show_image(const char* spiffs_path);
void ui_splash_fade_in(uint32_t duration_ms);
void ui_splash_hold(uint32_t duration_ms);
void ui_splash_fade_out(uint32_t duration_ms);
void ui_splash_clear(uint32_t post_black_delay_ms);
```

LVGL implementation notes:
- Create full-screen container + `lv_img` (or decoded RGB565 canvas)
- Fade using `lv_obj_set_style_opa()` animation on splash container
- Keep backlight control available as optional physical fade path

## 4.3 SOC widget (LVGL)

```cpp
void ui_soc_create(lv_obj_t* parent, int center_x, int center_y);
void ui_soc_set_value(float soc_percent);
void ui_soc_set_label(const char* text);              // "State of Charge"
void ui_soc_set_visible(bool visible);
```

Parity with current behavior:
- 1 decimal precision
- color gradient based on SOC
- large anti-aliased numeric font
- label above value

Why LVGL helps:
- LVGL already does dirty-area redraw, so manual character-box diff logic is unnecessary.

## 4.4 Power bar widget (LVGL)

```cpp
void ui_power_create(lv_obj_t* parent, int center_x, int center_y);
void ui_power_set_value(int32_t power_w, uint32_t now_ms);
void ui_power_set_max(int32_t max_power_w);
void ui_power_set_visible(bool visible);
```

Parity requirements:
- 30 segments per side
- left side for charge, right side for discharge
- center marker at near-zero
- one-pass ripple only on qualifying updates
- power text redraw only when changed

LVGL implementation approach:
- precreate segment objects once
- toggle visibility/color/opa instead of recreate/delete per frame
- use `lv_anim_t` for ripple highlight movement

## 4.5 LED status widget (LVGL)

```cpp
void ui_led_create(lv_obj_t* parent, int x, int y, int radius);
void ui_led_set_color(LEDColor c);
void ui_led_set_effect(LEDEffect e);
void ui_led_update(uint32_t now_ms);
void ui_led_set_visible(bool visible);
```

Parity requirements:
- same effects and timing as today
- independent tempo from power ripple

LVGL approach:
- circular object with animated opa/color for pulse/flash/heartbeat

---

## 5) Suggested folder/file layout

For `espnowreceiver_LCD/src/ui/`:

- `runtime/ui_runtime.h`
- `runtime/ui_runtime.cpp`
- `runtime/ui_backend_non_lvgl.cpp`
- `runtime/ui_backend_lvgl.cpp`
- `lvgl/widgets/lvgl_soc_widget.h/.cpp`
- `lvgl/widgets/lvgl_power_widget.h/.cpp`
- `lvgl/widgets/lvgl_led_widget.h/.cpp`
- `lvgl/lvgl_port.h/.cpp` (display flush, tick, buffer)

This keeps both backends cleanly separated.

---

## 6) Minimal integration changes in main.cpp

Replace direct widget calls with backend façade:

- Current direct calls:
  - `soc_widget.draw(...)`
  - `power_bar_widget.draw(...)`
  - `draw_led(...)`

- New façade calls:
  - `ui_runtime_init(*display)`
  - `ui_runtime_show_splash("/BatteryEmulator_LCD.jpg")`
  - `ui_runtime_update_state(st.soc_percent, st.power_w, now_ms)`
  - `ui_runtime_tick(now_ms)`

This keeps the app logic stable while swapping rendering backend.

---

## 7) LVGL polish improvements recommended

1. Use Montserrat/Noto fonts for anti-aliased text hierarchy:
   - SOC numeric large
   - label medium
   - power text medium-small
2. Use style objects (`lv_style_t`) for consistent theme.
3. Use object tree:
   - root screen
   - top area (SOC)
   - middle area (power bars)
   - side area (LED)
4. Animate with LVGL timers/animations instead of manual frame loops where possible.
5. Keep splash and runtime UI in separate containers for clean transitions.

---

## 8) Risk and compatibility notes

- LVGL RAM usage must be profiled on this board config (800x480).
- Use partial display buffer (not full framebuffer in LVGL) if memory pressure appears.
- Keep current non-LVGL backend intact until LVGL parity is verified.

---

## 9) Recommended implementation sequence

1. Add compile-time switch + dual environments.
2. Add façade (`ui_runtime_*`) and route existing non-LVGL through it.
3. Add LVGL port and blank LVGL screen.
4. Implement LVGL SOC widget parity.
5. Implement LVGL power widget parity.
6. Implement LVGL LED effects.
7. Implement LVGL splash with fade.
8. Validate parity and timings against current behavior.

---

## 10) Bottom line

Yes — the same UI can be implemented fully in LVGL with better polish and cleaner redraw behavior.

The best path is a **compile-time backend abstraction** so both versions coexist:
- `UI_BACKEND_LVGL=0`: current proven renderer
- `UI_BACKEND_LVGL=1`: polished LVGL renderer

This gives safe migration and easy A/B comparison on-device.