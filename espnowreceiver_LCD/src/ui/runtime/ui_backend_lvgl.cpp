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
constexpr uint32_t kPeakHoldMs = 1000;
constexpr uint32_t kLinearAttackMs = 90;
constexpr uint32_t kLinearReleaseMs = 1300;
constexpr uint32_t kLinearPeakReleaseMs = 900;
constexpr float kMeterDbFloor = -40.0f;
constexpr float kMeterEpsilon = 1e-3f;
constexpr int kBatteryBodyW = AppConfig::SCREEN_WIDTH / 2;
constexpr int kBatteryBodyH = (AppConfig::SCREEN_HEIGHT * 46) / 100;
constexpr int kBatteryBodyX = (AppConfig::SCREEN_WIDTH - kBatteryBodyW) / 2;
constexpr int kBatteryBodyY = 52;
constexpr int kBatteryBorderW = 4;
constexpr int kBatteryTerminalW = 64;
constexpr int kBatteryTerminalH = 48;

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
lv_obj_t* ip_label_ = nullptr;
lv_obj_t* center_marker_ = nullptr;
lv_obj_t* left_bars_[kBarCount] = {};
lv_obj_t* right_bars_[kBarCount] = {};
lv_obj_t* left_bar_covers_[kBarCount] = {};   // corner-squelch rectangles for OriginalRounded mode
lv_obj_t* right_bar_covers_[kBarCount] = {};
int left_bar_x_[kBarCount] = {};
int right_bar_x_[kBarCount] = {};
int bar_top_ = 0;
int bar_pitch_l_ = 0;
int bar_pitch_r_ = 0;

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
PowerBarRendererMode power_bar_mode_ = PowerBarRendererMode::Original;
bool link_connected_ = false;
bool remote_led_state_valid_ = false;
uint8_t remote_led_color_ = 2;
uint8_t remote_led_effect_ = 2;

float linear_display_level_ = 0.0f;
float linear_peak_level_ = 0.0f;
uint32_t linear_peak_hold_until_ms_ = 0;
uint32_t linear_last_update_ms_ = 0;
int hybrid_prev_active_segments_ = 0;

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

uint32_t blend_hex_rgb(uint32_t from, uint32_t to, float t) {
    const float clamped_t = std::max(0.0f, std::min(1.0f, t));
    const uint8_t from_r = static_cast<uint8_t>((from >> 16) & 0xFF);
    const uint8_t from_g = static_cast<uint8_t>((from >> 8) & 0xFF);
    const uint8_t from_b = static_cast<uint8_t>(from & 0xFF);
    const uint8_t to_r = static_cast<uint8_t>((to >> 16) & 0xFF);
    const uint8_t to_g = static_cast<uint8_t>((to >> 8) & 0xFF);
    const uint8_t to_b = static_cast<uint8_t>(to & 0xFF);

    const int out_r_i = static_cast<int>(from_r) + static_cast<int>((static_cast<int>(to_r) - static_cast<int>(from_r)) * clamped_t);
    const int out_g_i = static_cast<int>(from_g) + static_cast<int>((static_cast<int>(to_g) - static_cast<int>(from_g)) * clamped_t);
    const int out_b_i = static_cast<int>(from_b) + static_cast<int>((static_cast<int>(to_b) - static_cast<int>(from_b)) * clamped_t);
    const uint8_t out_r = static_cast<uint8_t>(std::max(0, std::min(255, out_r_i)));
    const uint8_t out_g = static_cast<uint8_t>(std::max(0, std::min(255, out_g_i)));
    const uint8_t out_b = static_cast<uint8_t>(std::max(0, std::min(255, out_b_i)));
    return (static_cast<uint32_t>(out_r) << 16) | (static_cast<uint32_t>(out_g) << 8) | out_b;
}

float normalized_db_level(float normalized_linear) {
    const float x = std::max(kMeterEpsilon, std::min(1.0f, normalized_linear));
    const float db = 20.0f * log10f(x);
    const float mapped = (db - kMeterDbFloor) / (0.0f - kMeterDbFloor);
    return std::max(0.0f, std::min(1.0f, mapped));
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

uint32_t bar_hex_color(bool is_charging, int index, int max_index) {
    if (max_index <= 0) {
        return is_charging ? 0x00FF00 : 0xFF0000;
    }
    const float t = static_cast<float>(index) / static_cast<float>(max_index);
    if (is_charging) {
        const uint8_t g = static_cast<uint8_t>(255.0f * t);
        const uint8_t b = static_cast<uint8_t>(255.0f * (1.0f - t));
        return (0u << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
    }
    const uint8_t r = static_cast<uint8_t>(255.0f * t);
    const uint8_t b = static_cast<uint8_t>(255.0f * (1.0f - t));
    return (static_cast<uint32_t>(r) << 16) | (0u << 8) | static_cast<uint32_t>(b);
}

lv_color_t led_wire_color(uint8_t color) {
    switch (color) {
        case 0:
            return lv_color_hex(0xDD0000);
        case 1:
            return lv_color_hex(0x00DD00);
        case 2:
            return lv_color_hex(0xFF9900);
        case 3:
            return lv_color_hex(0x0066FF);
        default:
            return lv_color_hex(0xFF9900);
    }
}

lv_opa_t led_effect_opacity(uint8_t effect, uint32_t now_ms) {
    switch (effect) {
        case 0:
            return LV_OPA_COVER;
        case 1:
            return ((now_ms / 500U) % 2U) == 0U ? LV_OPA_COVER : LV_OPA_TRANSP;
        case 2: {
            const uint32_t phase = now_ms % 1100U;
            if (phase < 120U) {
                return LV_OPA_COVER;
            }
            if (phase < 220U) {
                return LV_OPA_TRANSP;
            }
            if (phase < 340U) {
                return LV_OPA_COVER;
            }
            return LV_OPA_TRANSP;
        }
        default:
            return LV_OPA_COVER;
    }
}

uint8_t led_phase(uint32_t now_ms) {
    constexpr float pulse_period_ms = 2600.0f;
    constexpr float two_pi = 6.28318530718f;
    const float phase = (two_pi * static_cast<float>(now_ms % static_cast<uint32_t>(pulse_period_ms))) / pulse_period_ms;
    const float wave = 0.5f + (0.5f * sinf(phase));  // 0.0 – 1.0
    return static_cast<uint8_t>(wave * 255.0f);  // full range: fully off → fully on
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

    bar_top_     = top;
    bar_pitch_l_ = left_pitch;
    bar_pitch_r_ = right_pitch;

    for (int i = 0; i < kBarCount; ++i) {
        const int left_w = std::max(1, left_pitch);
        const int left_x = center_x - center_gap - ((i + 1) * left_pitch);
        left_bar_x_[i] = left_x;
        set_bar_obj(left_bars_[i], left_x, top, left_w, seg_h);
        const int right_w = std::max(1, right_pitch);
        const int right_x = center_x + center_gap + (i * right_pitch);
        right_bar_x_[i] = right_x;
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

    // Bold, high-visibility polarity markers.
    constexpr int kPolarityStrokeW = 8;
    constexpr int kPolarityLong = 28;

    lv_obj_t* left_plus_h = lv_obj_create(battery_terminal_left_);
    lv_obj_remove_style_all(left_plus_h);
    lv_obj_set_size(left_plus_h, kPolarityLong, kPolarityStrokeW);
    lv_obj_set_style_bg_color(left_plus_h, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(left_plus_h, LV_OPA_COVER, 0);
    lv_obj_center(left_plus_h);

    lv_obj_t* left_plus_v = lv_obj_create(battery_terminal_left_);
    lv_obj_remove_style_all(left_plus_v);
    lv_obj_set_size(left_plus_v, kPolarityStrokeW, kPolarityLong);
    lv_obj_set_style_bg_color(left_plus_v, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(left_plus_v, LV_OPA_COVER, 0);
    lv_obj_center(left_plus_v);

    battery_terminal_right_ = lv_obj_create(screen_);
    lv_obj_remove_style_all(battery_terminal_right_);
    lv_obj_set_pos(battery_terminal_right_, right_x, terminal_y);
    lv_obj_set_size(battery_terminal_right_, kBatteryTerminalW, kBatteryTerminalH);
    lv_obj_set_style_radius(battery_terminal_right_, 8, 0);
    lv_obj_set_style_bg_color(battery_terminal_right_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(battery_terminal_right_, LV_OPA_COVER, 0);

    lv_obj_t* right_minus = lv_obj_create(battery_terminal_right_);
    lv_obj_remove_style_all(right_minus);
    lv_obj_set_size(right_minus, kPolarityLong, kPolarityStrokeW);
    lv_obj_set_style_bg_color(right_minus, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(right_minus, LV_OPA_COVER, 0);
    lv_obj_center(right_minus);

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

    // IP address label — bottom-left corner; updated by set_network_status() after WiFi connects.
    ip_label_ = lv_label_create(screen_);
    lv_obj_set_style_text_font(ip_label_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ip_label_, lv_color_hex(0xAAAAAA), 0);  // dim grey
    lv_obj_set_style_text_opa(ip_label_, LV_OPA_COVER, 0);
    lv_label_set_text(ip_label_, "WiFi: ---.---.---.---");
    lv_obj_align(ip_label_, LV_ALIGN_BOTTOM_LEFT, 8, -4);

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

    // Cover rectangles for OriginalRounded mode — created after bars so they render on top.
    // Colour/opacity is set per-frame to match the owning bar segment.
    for (int i = 0; i < kBarCount; ++i) {
        left_bar_covers_[i] = lv_obj_create(screen_);
        lv_obj_remove_style_all(left_bar_covers_[i]);
        lv_obj_set_style_bg_color(left_bar_covers_[i], lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(left_bar_covers_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(left_bar_covers_[i], 0, 0);
        lv_obj_set_style_radius(left_bar_covers_[i], 0, 0);
        lv_obj_add_flag(left_bar_covers_[i], LV_OBJ_FLAG_HIDDEN);

        right_bar_covers_[i] = lv_obj_create(screen_);
        lv_obj_remove_style_all(right_bar_covers_[i]);
        lv_obj_set_style_bg_color(right_bar_covers_[i], lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(right_bar_covers_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(right_bar_covers_[i], 0, 0);
        lv_obj_set_style_radius(right_bar_covers_[i], 0, 0);
        lv_obj_add_flag(right_bar_covers_[i], LV_OBJ_FLAG_HIDDEN);
    }

    init_bar_geometry();
    lv_scr_load(screen_);
}

void hide_all_bars() {
    for (int i = 0; i < kBarCount; ++i) {
        lv_obj_add_flag(left_bars_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_radius(left_bars_[i], 0, 0);     // reset any rounding set by OriginalRounded
        lv_obj_add_flag(right_bars_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_radius(right_bars_[i], 0, 0);
        if (left_bar_covers_[i])  lv_obj_add_flag(left_bar_covers_[i],  LV_OBJ_FLAG_HIDDEN);
        if (right_bar_covers_[i]) lv_obj_add_flag(right_bar_covers_[i], LV_OBJ_FLAG_HIDDEN);
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

void apply_power_bars_original_rounded(int active_segments, bool is_charging, int ripple_idx = -1) {
    hide_all_bars();
    if (current_is_zero_) {
        lv_obj_clear_flag(center_marker_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // LVGL v8 has no per-corner radius, so we use a cover-rectangle approach.
    // Each end bar gets LV_RADIUS_CIRCLE (pill), then a small black rectangle is placed
    // over its internal-facing side, visually squaring off the unwanted rounded corners.
    // This gives four logical block shapes:
    //   single bar      → full pill (no cover)
    //   outermost bar   → external end rounded, internal end flat (cover on inner side)
    //   innermost bar   → centre-facing end rounded, outer end flat (cover on outer side)
    //   middle bars     → flat both sides (radius = 0, no cover)
    //
    // Bar index 0 = nearest centre; index active_segments-1 = outermost.
    // LEFT side: external end = LEFT edge of bar[active-1]; centre end = RIGHT edge of bar[0].
    // RIGHT side: external end = RIGHT edge of bar[active-1]; centre end = LEFT edge of bar[0].

    const int seg_h    = AppConfig::BAR_SEGMENT_H;
    const int corner_r = seg_h / 2;  // radius applied by LV_RADIUS_CIRCLE on seg_h-tall bars

    for (int i = 0; i < active_segments; ++i) {
        lv_obj_t* bar    = is_charging ? left_bars_[i]       : right_bars_[i];
        lv_obj_t* cover  = is_charging ? left_bar_covers_[i] : right_bar_covers_[i];
        const int  bar_x = is_charging ? left_bar_x_[i]      : right_bar_x_[i];
        const int  bar_w = is_charging ? bar_pitch_l_         : bar_pitch_r_;

        const lv_color_t seg_color = bar_lv_color(is_charging, i, kBarCount - 1);
        const lv_opa_t seg_opa = (i == ripple_idx) ? LV_OPA_50 : LV_OPA_COVER;

        lv_obj_set_style_bg_color(bar, seg_color, 0);
        lv_obj_set_style_bg_opa(bar, seg_opa, 0);
        lv_obj_set_style_bg_color(cover, seg_color, 0);
        lv_obj_set_style_bg_opa(cover, seg_opa, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);

        if (active_segments == 1) {
            // Single bar — fully rounded pill, no cover needed.
            lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);

        } else if (i == active_segments - 1) {
            // Outermost bar: round the external (far-from-centre) end only.
            // LEFT side:  external = LEFT edge  → cover squashes the RIGHT (inner) corners.
            // RIGHT side: external = RIGHT edge → cover squashes the LEFT (inner) corners.
            lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
            const int cover_x = is_charging ? (bar_x + bar_w - corner_r) : bar_x;
            lv_obj_set_pos(cover,  cover_x, bar_top_);
            lv_obj_set_size(cover, corner_r, seg_h);
            lv_obj_clear_flag(cover, LV_OBJ_FLAG_HIDDEN);

        } else if (i == 0) {
            // Innermost bar (adjacent to centre marker): round the centre-facing end only.
            // LEFT side:  centre-facing = RIGHT edge → cover squashes the LEFT (outer) corners.
            // RIGHT side: centre-facing = LEFT edge  → cover squashes the RIGHT (outer) corners.
            lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
            const int cover_x = is_charging ? bar_x : (bar_x + bar_w - corner_r);
            lv_obj_set_pos(cover,  cover_x, bar_top_);
            lv_obj_set_size(cover, corner_r, seg_h);
            lv_obj_clear_flag(cover, LV_OBJ_FLAG_HIDDEN);

        } else {
            // Middle bar — flat on both sides.
            lv_obj_set_style_radius(bar, 0, 0);
        }
    }

    bars_initialized_ = true;
}

void apply_power_bars_soft(int active_segments, bool is_charging, int ripple_idx = -1) {
    hide_all_bars();
    if (current_is_zero_) {
        lv_obj_clear_flag(center_marker_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    constexpr uint32_t kHighlight = 0xFFFFFF;
    constexpr uint32_t kShadow = 0x111111;
    for (int i = 0; i < active_segments; ++i) {
        lv_obj_t* obj = is_charging ? left_bars_[i] : right_bars_[i];
        const uint32_t base_hex = bar_hex_color(is_charging, i, kBarCount - 1);
        const uint32_t top_hex = blend_hex_rgb(base_hex, kHighlight, 0.28f);
        const uint32_t bottom_hex = blend_hex_rgb(base_hex, kShadow, 0.22f);

        lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(top_hex), 0);
        lv_obj_set_style_bg_grad_color(obj, lv_color_hex(bottom_hex), 0);
        lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(obj, (i == ripple_idx) ? LV_OPA_60 : LV_OPA_90, 0);
        lv_obj_set_style_shadow_width(obj, 6, 0);
        lv_obj_set_style_shadow_color(obj, lv_color_hex(base_hex), 0);
        lv_obj_set_style_shadow_opa(obj, LV_OPA_30, 0);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }

    bars_initialized_ = true;
}

void apply_power_bars_linear(int active_segments, bool is_charging, int peak_idx = -1) {
    hide_all_bars();
    if (current_is_zero_ && active_segments <= 0 && peak_idx < 0) {
        lv_obj_clear_flag(center_marker_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    for (int i = 0; i < active_segments; ++i) {
        lv_obj_t* obj = is_charging ? left_bars_[i] : right_bars_[i];
        const lv_color_t color = bar_lv_color(is_charging, i, kBarCount - 1);
        lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(obj, color, 0);
        lv_obj_set_style_bg_grad_color(obj, color, 0);
        lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);
        lv_obj_set_style_shadow_width(obj, 0, 0);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }

    if (peak_idx >= 0 && peak_idx < kBarCount) {
        lv_obj_t* peak_obj = is_charging ? left_bars_[peak_idx] : right_bars_[peak_idx];
        lv_obj_set_style_radius(peak_obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(peak_obj, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_grad_color(peak_obj, lv_color_hex(0xFFD966), 0);
        lv_obj_set_style_bg_grad_dir(peak_obj, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(peak_obj, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(peak_obj, 6, 0);
        lv_obj_set_style_shadow_color(peak_obj, lv_color_hex(0xFFD966), 0);
        lv_obj_set_style_shadow_opa(peak_obj, LV_OPA_40, 0);
        lv_obj_clear_flag(peak_obj, LV_OBJ_FLAG_HIDDEN);
    }

    bars_initialized_ = true;
}

void apply_power_bars_hybrid(int active_segments, bool is_charging, int peak_idx = -1, int trail_idx = -1) {
    hide_all_bars();
    if (current_is_zero_ && active_segments <= 0 && peak_idx < 0) {
        lv_obj_clear_flag(center_marker_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    constexpr uint32_t kHighlight = 0xFFFFFF;
    for (int i = 0; i < active_segments; ++i) {
        lv_obj_t* obj = is_charging ? left_bars_[i] : right_bars_[i];
        const uint32_t base_hex = bar_hex_color(is_charging, i, kBarCount - 1);
        const uint32_t top_hex = blend_hex_rgb(base_hex, kHighlight, 0.22f);

        lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(top_hex), 0);
        lv_obj_set_style_bg_grad_color(obj, lv_color_hex(base_hex), 0);
        lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);
        lv_obj_set_style_shadow_width(obj, 4, 0);
        lv_obj_set_style_shadow_color(obj, lv_color_hex(base_hex), 0);
        lv_obj_set_style_shadow_opa(obj, LV_OPA_20, 0);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }

    if (trail_idx >= 0 && trail_idx < kBarCount) {
        lv_obj_t* trail_obj = is_charging ? left_bars_[trail_idx] : right_bars_[trail_idx];
        const lv_color_t trail_color = bar_lv_color(is_charging, trail_idx, kBarCount - 1);
        lv_obj_set_style_radius(trail_obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(trail_obj, trail_color, 0);
        lv_obj_set_style_bg_grad_color(trail_obj, trail_color, 0);
        lv_obj_set_style_bg_grad_dir(trail_obj, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_bg_opa(trail_obj, LV_OPA_30, 0);
        lv_obj_set_style_shadow_width(trail_obj, 0, 0);
        lv_obj_clear_flag(trail_obj, LV_OBJ_FLAG_HIDDEN);
    }

    if (peak_idx >= 0 && peak_idx < kBarCount) {
        lv_obj_t* peak_obj = is_charging ? left_bars_[peak_idx] : right_bars_[peak_idx];
        lv_obj_set_style_radius(peak_obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(peak_obj, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_grad_color(peak_obj, lv_color_hex(0xFFF0AA), 0);
        lv_obj_set_style_bg_grad_dir(peak_obj, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(peak_obj, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(peak_obj, 8, 0);
        lv_obj_set_style_shadow_color(peak_obj, lv_color_hex(0xFFE066), 0);
        lv_obj_set_style_shadow_opa(peak_obj, LV_OPA_50, 0);
        lv_obj_clear_flag(peak_obj, LV_OBJ_FLAG_HIDDEN);
    }

    bars_initialized_ = true;
}

void render_power_bars(int active_segments, bool is_charging, int ripple_idx = -1, int peak_idx = -1, int trail_idx = -1) {
    switch (power_bar_mode_) {
        case PowerBarRendererMode::Original:
            apply_power_bars(active_segments, is_charging, ripple_idx);
            return;
        case PowerBarRendererMode::OriginalRounded:
            apply_power_bars_original_rounded(active_segments, is_charging, ripple_idx);
            return;
        case PowerBarRendererMode::Soft:
            apply_power_bars_soft(active_segments, is_charging, ripple_idx);
            return;
        case PowerBarRendererMode::Linear:
            apply_power_bars_linear(active_segments, is_charging, peak_idx);
            return;
        case PowerBarRendererMode::Hybrid:
            apply_power_bars_hybrid(active_segments, is_charging, peak_idx, trail_idx);
            return;
        default:
            apply_power_bars(active_segments, is_charging, ripple_idx);
            return;
    }
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

    int peak_idx = -1;
    int trail_idx = -1;

    const bool use_ballistic_meter =
        (power_bar_mode_ == PowerBarRendererMode::Linear) ||
        (power_bar_mode_ == PowerBarRendererMode::Hybrid);

    if (use_ballistic_meter) {
        if (linear_last_update_ms_ == 0) {
            linear_last_update_ms_ = now_ms;
        }
        const uint32_t dt_ms = std::max(1U, now_ms - linear_last_update_ms_);
        linear_last_update_ms_ = now_ms;

        const float normalized_linear = static_cast<float>(abs_power) / static_cast<float>(AppConfig::MAX_POWER_W);
        const float target_level = current_is_zero_ ? 0.0f : normalized_db_level(normalized_linear);

        if (target_level >= linear_display_level_) {
            const float alpha = std::min(1.0f, static_cast<float>(dt_ms) / static_cast<float>(kLinearAttackMs));
            linear_display_level_ += (target_level - linear_display_level_) * alpha;
        } else {
            const float alpha = std::min(1.0f, static_cast<float>(dt_ms) / static_cast<float>(kLinearReleaseMs));
            linear_display_level_ += (target_level - linear_display_level_) * alpha;
        }
        linear_display_level_ = std::max(0.0f, std::min(1.0f, linear_display_level_));

        if (linear_display_level_ >= linear_peak_level_) {
            linear_peak_level_ = linear_display_level_;
            linear_peak_hold_until_ms_ = now_ms + kPeakHoldMs;
        } else if (now_ms >= linear_peak_hold_until_ms_) {
            const float decay = static_cast<float>(dt_ms) / static_cast<float>(kLinearPeakReleaseMs);
            linear_peak_level_ = std::max(linear_display_level_, linear_peak_level_ - decay);
        }

        const int meter_segments = static_cast<int>(linear_display_level_ * static_cast<float>(kBarCount) + 0.5f);
        current_bar_count_ = std::max(0, std::min(kBarCount, meter_segments));
        if (!current_is_zero_ && current_bar_count_ == 0 && linear_display_level_ > 0.01f) {
            current_bar_count_ = 1;
        }

        if (linear_peak_level_ > 0.0f) {
            peak_idx = static_cast<int>(ceilf(linear_peak_level_ * static_cast<float>(kBarCount))) - 1;
            peak_idx = std::max(0, std::min(kBarCount - 1, peak_idx));
        }

        if (power_bar_mode_ == PowerBarRendererMode::Hybrid) {
            if (current_bar_count_ > hybrid_prev_active_segments_) {
                trail_idx = std::min(kBarCount - 1, current_bar_count_);
            } else if (current_bar_count_ < hybrid_prev_active_segments_) {
                trail_idx = std::max(0, hybrid_prev_active_segments_ - 1);
            }
            hybrid_prev_active_segments_ = current_bar_count_;
        }
    }

    const bool new_value = (last_power_text_ != current_power_w_);
    const bool same_direction = !last_was_zero_ && !current_is_zero_ && (last_was_charging_ == current_is_charging_);

    if (!use_ballistic_meter) {
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
            render_power_bars(current_bar_count_, current_is_charging_);
        }
    } else {
        ripple_active_ = false;
        render_power_bars(current_bar_count_, current_is_charging_, -1, peak_idx, trail_idx);
    }

    update_power_label();
    last_bar_count_ = current_bar_count_;
    last_was_charging_ = current_is_charging_;
    last_was_zero_ = current_is_zero_;
}

void animate_led(uint32_t now_ms) {
    uint8_t color = 2;
    uint8_t effect = 2;

    if (!link_connected_) {
        color = 0;
        effect = 1;
    } else if (remote_led_state_valid_) {
        color = remote_led_color_;
        effect = remote_led_effect_;
    }

    lv_obj_set_style_bg_color(led_obj_, led_wire_color(color), 0);
    lv_obj_set_style_bg_opa(led_obj_, led_effect_opacity(effect, now_ms), 0);
}

void animate_power_ripple(uint32_t now_ms) {
    if (power_bar_mode_ == PowerBarRendererMode::Linear || power_bar_mode_ == PowerBarRendererMode::Hybrid) {
        return;
    }
    if (!ripple_active_) {
        return;
    }
    const uint32_t elapsed = now_ms - ripple_start_ms_;
    if (elapsed >= kRippleMs) {
        ripple_active_ = false;
        render_power_bars(current_bar_count_, current_is_charging_);
        return;
    }
    const int ripple_idx = static_cast<int>((static_cast<uint64_t>(elapsed) * current_bar_count_) / kRippleMs);
    render_power_bars(current_bar_count_, current_is_charging_, ripple_idx);
}

}  // namespace

void set_network_status(const char* ip, bool wifi_ok) {
    if (!ip_label_) {
        return;
    }
    char buf[40];
    if (wifi_ok && ip && ip[0] != '\0') {
        snprintf(buf, sizeof(buf), "WiFi: %s", ip);
    } else if (wifi_ok) {
        snprintf(buf, sizeof(buf), "WiFi: AP mode");
    } else {
        snprintf(buf, sizeof(buf), "WiFi: ---.---.---.---");
    }
    lv_label_set_text(ip_label_, buf);
    lv_obj_set_style_text_color(
        ip_label_,
        wifi_ok ? lv_color_hex(0x66FF66) : lv_color_hex(0xAAAAAA),
        0);
    lv_obj_align(ip_label_, LV_ALIGN_BOTTOM_LEFT, 8, -4);
}

void set_led_state(uint8_t color, uint8_t effect) {
    remote_led_color_ = color;
    remote_led_effect_ = effect;
    remote_led_state_valid_ = true;
}

void set_link_connected(bool connected) {
    link_connected_ = connected;
}

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

void set_power_bar_mode(PowerBarRendererMode mode) {
    power_bar_mode_ = mode;
    bars_initialized_ = false;
    ripple_active_ = false;
    linear_display_level_ = 0.0f;
    linear_peak_level_ = 0.0f;
    linear_peak_hold_until_ms_ = 0;
    linear_last_update_ms_ = 0;
    hybrid_prev_active_segments_ = 0;

    if (display_ && screen_) {
        render_power_bars(current_bar_count_, current_is_charging_);
    }
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
