#include "ui/runtime/ui_backend.h"

#include <lvgl.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app_config.h"
#include "ui/colors.h"
#include "ui/layout.h"
#include "ui/runtime/splash_sequence.h"

namespace UI::Runtime::Backend {
namespace {

constexpr int kBarCount = AppConfig::BAR_SEGMENTS_PER_SIDE;
constexpr uint32_t kRippleMs = 1000;
constexpr int kBatteryBodyW = AppConfig::SCREEN_WIDTH / 2;
constexpr int kBatteryBodyH = (AppConfig::SCREEN_HEIGHT * 46) / 100;
constexpr int kBatteryBodyX = (AppConfig::SCREEN_WIDTH - kBatteryBodyW) / 2;
constexpr int kBatteryBodyY = 52;
constexpr int kBatteryBorderW = 4;
constexpr int kBatteryTerminalW = 64;
constexpr int kBatteryTerminalH = 24;

int battery_bottom_y() {
    return kBatteryBodyY + kBatteryBodyH;
}

int power_label_top_y() {
    return UI::Layout::power_text_y() - 18;
}

int bar_center_y_from_layout() {
    // Requested placement: halfway between battery bottom and power label top.
    return (battery_bottom_y() + power_label_top_y()) / 2;
}

lgfx::LGFX_Device* display_ = nullptr;
lv_disp_draw_buf_t draw_buf_;
lv_color_t* draw_buffer_1_ = nullptr;
lv_color_t* draw_buffer_2_ = nullptr;
lv_disp_drv_t disp_drv_;
lv_obj_t* screen_ = nullptr;
lv_obj_t* battery_body_ = nullptr;
lv_obj_t* battery_fill_ = nullptr;
lv_obj_t* battery_soc_text_ = nullptr;
lv_obj_t* battery_terminal_left_ = nullptr;
lv_obj_t* battery_terminal_right_ = nullptr;
lv_obj_t* power_label_ = nullptr;
lv_obj_t* led_obj_ = nullptr;
lv_obj_t* center_marker_ = nullptr;
lv_obj_t* left_bars_[kBarCount] = {};
lv_obj_t* right_bars_[kBarCount] = {};

float current_soc_percent_ = 0.0f;
int32_t current_power_w_ = 0;
uint32_t last_lv_tick_ms_ = 0;
uint32_t last_anim_ms_ = 0;
int last_soc_tenths_ = INT32_MIN;
int32_t last_power_text_ = INT32_MAX;
int last_bar_count_ = 0;
bool last_was_charging_ = false;
bool last_was_zero_ = true;
bool ripple_active_ = false;
uint32_t ripple_start_ms_ = 0;
int current_bar_count_ = 0;
bool current_is_charging_ = false;
bool current_is_zero_ = true;
int current_battery_fill_px_ = 0;
bool bars_initialized_ = false;

static void flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    auto* lcd = static_cast<lgfx::LGFX_Device*>(drv->user_data);
    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;
    lcd->startWrite();
    lcd->setAddrWindow(area->x1, area->y1, w, h);
    // LVGL buffer is little-endian RGB565 in memory; panel path expects byte-swapped words.
    // Without swap, yellow/green values can appear purple/blue.
    lcd->writePixels(reinterpret_cast<const uint16_t*>(color_p), static_cast<uint32_t>(w * h), true);
    lcd->endWrite();
    lv_disp_flush_ready(drv);
}

uint32_t rgb565_to_hex(uint16_t c) {
    const uint8_t r = static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31);
    const uint8_t g = static_cast<uint8_t>(((c >> 5) & 0x3F) * 255 / 63);
    const uint8_t b = static_cast<uint8_t>((c & 0x1F) * 255 / 31);
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

float clamp_soc(float soc_percent) {
    return std::max(0.0f, std::min(100.0f, soc_percent));
}

lv_color_t soc_pipeline_color(float soc_percent) {
    const float clamped_soc = clamp_soc(soc_percent);
    const float t = clamped_soc / 100.0f;

    uint8_t r8 = 0;
    uint8_t g8 = 0;
    if (t < 0.5f) {
        r8 = 255;
        g8 = static_cast<uint8_t>(510.0f * t);
    } else {
        r8 = static_cast<uint8_t>(255.0f * (1.0f - ((t - 0.5f) * 2.0f)));
        g8 = 255;
    }

    const uint16_t rgb565 = UI::rgb565(r8, g8, 0);
    return lv_color_hex(rgb565_to_hex(rgb565));
}

lv_color_t bar_lv_color(bool is_charging, int index, int max_index) {
    if (max_index <= 0) {
        return is_charging ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000);
    }
    const float t = static_cast<float>(index) / static_cast<float>(max_index);
    if (is_charging) {
        const uint8_t g = static_cast<uint8_t>(255.0f * t);
        const uint8_t b = static_cast<uint8_t>(255.0f * (1.0f - t));
        return lv_color_hex((0 << 16) | (g << 8) | b);
    }
    const uint8_t r = static_cast<uint8_t>(255.0f * t);
    const uint8_t b = static_cast<uint8_t>(255.0f * (1.0f - t));
    return lv_color_hex((r << 16) | (0 << 8) | b);
}

uint8_t led_phase(uint32_t now_ms) {
    constexpr float pulse_period_ms = 2600.0f;
    constexpr float two_pi = 6.28318530718f;
    const float phase = (two_pi * static_cast<float>(now_ms % static_cast<uint32_t>(pulse_period_ms))) / pulse_period_ms;
    const float wave = 0.5f + (0.5f * sinf(phase));
    const float t = 0.15f + (0.85f * wave);
    return static_cast<uint8_t>(t * 255.0f);
}

// Content area = body size minus 2× border (lv_obj_set_pos on children is relative here).
int battery_inner_height() {
    return std::max(1, kBatteryBodyH - (2 * kBatteryBorderW));
}

int battery_inner_width() {
    return std::max(1, kBatteryBodyW - (2 * kBatteryBorderW));
}

void set_battery_fill_pixels(int fill_px) {
    if (!battery_fill_) {
        return;
    }
    const int inner_h = battery_inner_height();
    const int inner_w = battery_inner_width();
    const int clamped_fill = std::max(0, std::min(fill_px, inner_h));
    // pos is relative to the content area (inside the border, pad=0).
    // x=0 → flush against inner-left edge.  Anchor fill to bottom.
    lv_obj_set_pos(battery_fill_, 0, inner_h - clamped_fill);
    lv_obj_set_size(battery_fill_, inner_w, std::max(1, clamped_fill));
    current_battery_fill_px_ = clamped_fill;
}

void battery_fill_anim_cb(void* /*var*/, int32_t v) {
    set_battery_fill_pixels(static_cast<int>(v));
}

void set_bar_obj(lv_obj_t* obj, int left, int top, int width, int height) {
    lv_obj_set_pos(obj, left, top);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
}

void init_bar_geometry() {
    const int center_x = UI::Layout::bar_center_x();
    const int center_y = bar_center_y_from_layout();
    const int seg_h = AppConfig::BAR_SEGMENT_H;
    const int edge_margin = AppConfig::BAR_EDGE_MARGIN;
    const int center_gap = AppConfig::BAR_CENTER_GAP;
    const int left_space = std::max(1, center_x - center_gap - edge_margin);
    const int right_space = std::max(1, (AppConfig::SCREEN_WIDTH - edge_margin) - (center_x + center_gap));
    const int left_pitch = std::max(2, left_space / kBarCount);
    const int right_pitch = std::max(2, right_space / kBarCount);
    const int top = center_y - (seg_h / 2);

    for (int i = 0; i < kBarCount; ++i) {
        const int left_w = std::max(1, left_pitch);
        const int left_x = center_x - center_gap - ((i + 1) * left_pitch);
        set_bar_obj(left_bars_[i], left_x, top, left_w, seg_h);
        const int right_w = std::max(1, right_pitch);
        const int right_x = center_x + center_gap + (i * right_pitch);
        set_bar_obj(right_bars_[i], right_x, top, right_w, seg_h);
    }

    const int marker_w = std::max(1, std::max(left_pitch, right_pitch));
    set_bar_obj(center_marker_, center_x - (marker_w / 2), top, marker_w, seg_h);
}

void create_objects() {
    screen_ = lv_obj_create(nullptr);
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen_, 0, 0);
    lv_obj_set_style_pad_all(screen_, 0, 0);

    battery_body_ = lv_obj_create(screen_);
    lv_obj_remove_style_all(battery_body_);
    lv_obj_set_pos(battery_body_, kBatteryBodyX, kBatteryBodyY);
    lv_obj_set_size(battery_body_, kBatteryBodyW, kBatteryBodyH);
    lv_obj_set_style_radius(battery_body_, 18, 0);
    lv_obj_set_style_bg_opa(battery_body_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_body_, kBatteryBorderW, 0);
    lv_obj_set_style_border_color(battery_body_, lv_color_white(), 0);
    lv_obj_set_style_pad_all(battery_body_, 0, 0);
    lv_obj_clear_flag(battery_body_, LV_OBJ_FLAG_SCROLLABLE);   // prevent child offset drift
    lv_obj_set_style_clip_corner(battery_body_, true, 0);        // clip fill within rounded corners

    battery_fill_ = lv_obj_create(battery_body_);
    lv_obj_remove_style_all(battery_fill_);
    lv_obj_set_style_radius(battery_fill_, 0, 0);  // clip_corner on parent handles corners
    const lv_color_t initial_fill = soc_pipeline_color(0.0f);
    lv_obj_set_style_bg_color(battery_fill_, initial_fill, 0);
    lv_obj_set_style_bg_grad_color(battery_fill_, initial_fill, 0);
    lv_obj_set_style_bg_grad_dir(battery_fill_, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(battery_fill_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(battery_fill_, 0, 0);
    set_battery_fill_pixels(0);

    battery_soc_text_ = lv_label_create(battery_body_);
    lv_obj_set_style_text_font(battery_soc_text_, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(battery_soc_text_, lv_color_white(), 0);
    lv_obj_set_style_text_opa(battery_soc_text_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_align(battery_soc_text_, LV_TEXT_ALIGN_CENTER, 0);
    // Semi-transparent dark pill behind the text: white text stays legible against
    // any fill colour (red/yellow/green) without touching the update logic.
    // bg renders as a rounded rectangle snug around the text glyphs via padding.
    lv_obj_set_style_bg_color(battery_soc_text_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(battery_soc_text_, LV_OPA_50, 0);
    lv_obj_set_style_radius(battery_soc_text_, 16, 0);
    lv_obj_set_style_pad_hor(battery_soc_text_, 14, 0);
    lv_obj_set_style_pad_ver(battery_soc_text_, 6, 0);
    lv_label_set_text(battery_soc_text_, "0.0%");
    lv_obj_center(battery_soc_text_);

    // Position terminals at 1/4 and 3/4 of battery width, centered at those positions.
    const int terminal_y = kBatteryBodyY - kBatteryTerminalH + 4;  // overlap to connect
    const int battery_quarter = kBatteryBodyW / 4;
    const int left_x = kBatteryBodyX + battery_quarter - (kBatteryTerminalW / 2);
    const int right_x = kBatteryBodyX + (3 * battery_quarter) - (kBatteryTerminalW / 2);

    battery_terminal_left_ = lv_obj_create(screen_);
    lv_obj_remove_style_all(battery_terminal_left_);
    lv_obj_set_pos(battery_terminal_left_, left_x, terminal_y);
    lv_obj_set_size(battery_terminal_left_, kBatteryTerminalW, kBatteryTerminalH);
    lv_obj_set_style_radius(battery_terminal_left_, 8, 0);
    lv_obj_set_style_bg_color(battery_terminal_left_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(battery_terminal_left_, LV_OPA_COVER, 0);

    battery_terminal_right_ = lv_obj_create(screen_);
    lv_obj_remove_style_all(battery_terminal_right_);
    lv_obj_set_pos(battery_terminal_right_, right_x, terminal_y);
    lv_obj_set_size(battery_terminal_right_, kBatteryTerminalW, kBatteryTerminalH);
    lv_obj_set_style_radius(battery_terminal_right_, 8, 0);
    lv_obj_set_style_bg_color(battery_terminal_right_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(battery_terminal_right_, LV_OPA_COVER, 0);

    power_label_ = lv_label_create(screen_);
    lv_obj_set_style_text_font(power_label_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(power_label_, lv_color_white(), 0);
    lv_label_set_text(power_label_, "0W");
    lv_obj_align(power_label_, LV_ALIGN_TOP_MID, 0, UI::Layout::power_text_y() - 18);

    // Plain circle: lv_obj with LV_RADIUS_CIRCLE, opacity-based pulse, no shadow/gradient/LED widget.
    led_obj_ = lv_obj_create(screen_);
    lv_obj_remove_style_all(led_obj_);   // strip every theme/default style
    lv_obj_set_size(led_obj_, AppConfig::LED_RADIUS * 2, AppConfig::LED_RADIUS * 2);
    lv_obj_set_style_radius(led_obj_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(led_obj_, lv_color_hex(0x00DD00), 0);  // solid green
    lv_obj_set_style_bg_opa(led_obj_, LV_OPA_70, 0);
    lv_obj_set_style_border_width(led_obj_, 0, 0);
    lv_obj_set_style_shadow_width(led_obj_, 0, 0);
    lv_obj_set_style_outline_width(led_obj_, 0, 0);
    lv_obj_set_style_pad_all(led_obj_, 0, 0);
    lv_obj_set_pos(led_obj_, UI::Layout::led_x() - AppConfig::LED_RADIUS, UI::Layout::led_y() - AppConfig::LED_RADIUS);

    center_marker_ = lv_obj_create(screen_);
    lv_obj_set_style_bg_color(center_marker_, lv_color_hex(rgb565_to_hex(UI::Colors::BLUE)), 0);
    lv_obj_set_style_bg_opa(center_marker_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(center_marker_, 0, 0);
    lv_obj_add_flag(center_marker_, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < kBarCount; ++i) {
        left_bars_[i] = lv_obj_create(screen_);
        lv_obj_remove_style_all(left_bars_[i]);
        lv_obj_set_style_bg_opa(left_bars_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(left_bars_[i], 0, 0);
        lv_obj_set_style_radius(left_bars_[i], 0, 0);
        lv_obj_add_flag(left_bars_[i], LV_OBJ_FLAG_HIDDEN);

        right_bars_[i] = lv_obj_create(screen_);
        lv_obj_remove_style_all(right_bars_[i]);
        lv_obj_set_style_bg_opa(right_bars_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(right_bars_[i], 0, 0);
        lv_obj_set_style_radius(right_bars_[i], 0, 0);
        lv_obj_add_flag(right_bars_[i], LV_OBJ_FLAG_HIDDEN);
    }

    init_bar_geometry();
    lv_scr_load(screen_);
}

void hide_all_bars() {
    for (int i = 0; i < kBarCount; ++i) {
        lv_obj_add_flag(left_bars_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(right_bars_[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(center_marker_, LV_OBJ_FLAG_HIDDEN);
}

void apply_power_bars(int active_segments, bool is_charging, int ripple_idx = -1) {
    hide_all_bars();
    if (current_is_zero_) {
        lv_obj_clear_flag(center_marker_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    for (int i = 0; i < active_segments; ++i) {
        lv_obj_t* obj = is_charging ? left_bars_[i] : right_bars_[i];
        lv_obj_set_style_bg_color(obj, bar_lv_color(is_charging, i, kBarCount - 1), 0);
        lv_obj_set_style_bg_opa(obj, (i == ripple_idx) ? LV_OPA_50 : LV_OPA_COVER, 0);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }

    bars_initialized_ = true;
}

void update_soc() {
    const int soc_tenths = static_cast<int>(current_soc_percent_ * 10.0f + (current_soc_percent_ >= 0.0f ? 0.5f : -0.5f));
    if (soc_tenths == last_soc_tenths_) {
        return;
    }
    const float clamped_soc = std::max(AppConfig::SOC_MIN, std::min(AppConfig::SOC_MAX, current_soc_percent_));

    char buf[16];
    snprintf(buf, sizeof(buf), "%4.1f%%", static_cast<float>(soc_tenths) / 10.0f);
    lv_label_set_text(battery_soc_text_, buf);
    lv_obj_center(battery_soc_text_);

    const lv_color_t fill_color = soc_pipeline_color(clamped_soc);
    lv_obj_set_style_bg_color(battery_fill_, fill_color, 0);
    lv_obj_set_style_bg_grad_color(battery_fill_, fill_color, 0);
    lv_obj_set_style_bg_grad_dir(battery_fill_, LV_GRAD_DIR_NONE, 0);

    const int target_fill = static_cast<int>((clamped_soc * static_cast<float>(battery_inner_height())) / 100.0f + 0.5f);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, nullptr);
    lv_anim_set_values(&anim, current_battery_fill_px_, target_fill);
    lv_anim_set_time(&anim, 500);
    lv_anim_set_exec_cb(&anim, battery_fill_anim_cb);
    lv_anim_start(&anim);

    last_soc_tenths_ = soc_tenths;
}

void update_power_label() {
    if (current_power_w_ == last_power_text_) {
        return;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%ldW", static_cast<long>(current_power_w_));
    lv_label_set_text(power_label_, buf);
    lv_obj_align(power_label_, LV_ALIGN_TOP_MID, 0, UI::Layout::power_text_y() - 18);
    last_power_text_ = current_power_w_;
}

void update_power_state(uint32_t now_ms) {
    const int32_t clamped = std::max(-AppConfig::MAX_POWER_W, std::min(AppConfig::MAX_POWER_W, current_power_w_));
    const int32_t abs_power = std::abs(clamped);
    current_is_charging_ = clamped < 0;
    current_is_zero_ = abs_power < 10;
    current_bar_count_ = (clamped == 0) ? 0 : static_cast<int>((static_cast<int64_t>(abs_power) * kBarCount) / AppConfig::MAX_POWER_W);
    if (!current_is_zero_ && current_bar_count_ == 0) current_bar_count_ = 1;
    if (current_bar_count_ > kBarCount) current_bar_count_ = kBarCount;

    const bool new_value = (last_power_text_ != current_power_w_);
    const bool same_direction = !last_was_zero_ && !current_is_zero_ && (last_was_charging_ == current_is_charging_);

    if (new_value && same_direction && current_bar_count_ == last_bar_count_ && current_bar_count_ > 0) {
        ripple_active_ = true;
        ripple_start_ms_ = now_ms;
    }
    if (current_is_zero_ || (!same_direction && !current_is_zero_)) {
        ripple_active_ = false;
    }

    const bool bars_changed = (!bars_initialized_) ||
                              (current_bar_count_ != last_bar_count_) ||
                              (current_is_charging_ != last_was_charging_) ||
                              (current_is_zero_ != last_was_zero_);

    if (!ripple_active_ && bars_changed) {
        apply_power_bars(current_bar_count_, current_is_charging_);
    }

    update_power_label();
    last_bar_count_ = current_bar_count_;
    last_was_charging_ = current_is_charging_;
    last_was_zero_ = current_is_zero_;
}

void animate_led(uint32_t now_ms) {
    // Pulse opacity between dim (40) and fully opaque (255) — size stays fixed.
    const uint8_t phase = led_phase(now_ms);  // 0–255
    const lv_opa_t opa = static_cast<lv_opa_t>(40U + ((static_cast<uint16_t>(phase) * 215U) / 255U));
    lv_obj_set_style_bg_opa(led_obj_, opa, 0);
}

void animate_power_ripple(uint32_t now_ms) {
    if (!ripple_active_) {
        return;
    }
    const uint32_t elapsed = now_ms - ripple_start_ms_;
    if (elapsed >= kRippleMs) {
        ripple_active_ = false;
        apply_power_bars(current_bar_count_, current_is_charging_);
        return;
    }
    const int ripple_idx = static_cast<int>((static_cast<uint64_t>(elapsed) * current_bar_count_) / kRippleMs);
    apply_power_bars(current_bar_count_, current_is_charging_, ripple_idx);
}

}  // namespace

bool init(lgfx::LGFX_Device& display) {
    display_ = &display;
    lv_init();

    // Two partial buffers (120 rows each) in PSRAM.
    // Keep buffer traffic low to avoid saturating PSRAM bandwidth on RGB panels.
    // With RGB panel-side double framebuffer enabled in LGFX (use_psram=2),
    // partial flushes are still presented coherently at VSYNC.
    const size_t buffer_pixels = static_cast<size_t>(AppConfig::SCREEN_WIDTH) * 120U;
    draw_buffer_1_ = static_cast<lv_color_t*>(heap_caps_malloc(buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    draw_buffer_2_ = static_cast<lv_color_t*>(heap_caps_malloc(buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!draw_buffer_1_ || !draw_buffer_2_) {
        return false;
    }

    lv_disp_draw_buf_init(&draw_buf_, draw_buffer_1_, draw_buffer_2_, static_cast<uint32_t>(buffer_pixels));
    lv_disp_drv_init(&disp_drv_);
    disp_drv_.hor_res = AppConfig::SCREEN_WIDTH;
    disp_drv_.ver_res = AppConfig::SCREEN_HEIGHT;
    disp_drv_.flush_cb = flush_cb;
    disp_drv_.draw_buf = &draw_buf_;
    disp_drv_.user_data = display_;
    // Keep LVGL dirty-area refresh enabled to avoid full-screen copies each frame.
    // Full-refresh on RGB+PSRAM can increase bandwidth pressure and visible jitter.
    disp_drv_.full_refresh = 0;
    lv_disp_drv_register(&disp_drv_);

    create_objects();
    return true;
}

void run_startup_sequence() {
    if (!display_) {
        return;
    }
    UI::Runtime::run_splash_sequence(*display_);
    lv_obj_invalidate(screen_);
}

void set_state(float soc_percent, int32_t power_w) {
    current_soc_percent_ = soc_percent;
    current_power_w_ = power_w;
}

void tick(uint32_t now_ms) {
    if (!display_) {
        return;
    }

    if (last_lv_tick_ms_ == 0) {
        last_lv_tick_ms_ = now_ms;
        last_anim_ms_ = now_ms;
    }

    const uint32_t delta = now_ms - last_lv_tick_ms_;
    if (delta > 0) {
        lv_tick_inc(delta);
        last_lv_tick_ms_ = now_ms;
    }

    update_soc();
    update_power_state(now_ms);

    if (now_ms - last_anim_ms_ >= 33) {
        animate_led(now_ms);
        animate_power_ripple(now_ms);
        // Rate-limit rendering to ~30 fps: all pending object changes (SOC, bars, LED)
        // are batched and flushed in one full-screen pass per tick.  Calling
        // lv_timer_handler() more frequently than the panel's vsync rate (~38 fps)
        // would queue back-to-back full-screen writePixels calls with no benefit.
        lv_timer_handler();
        last_anim_ms_ = now_ms;
    }
}

}  // namespace UI::Runtime::Backend
