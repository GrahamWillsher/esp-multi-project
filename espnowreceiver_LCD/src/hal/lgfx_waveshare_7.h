#pragma once

// NOTE:
// This configuration is based on the public LovyanGFX Waveshare RGB example
// and is intended as a phased bring-up baseline.
// Verify all RGB + CH422G mappings against ESP32-S3-Touch-LCD-7 schematic
// before production use.

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <driver/i2c.h>
#include <Wire.h>

namespace lgfx_custom {

class Light_CH422G : public lgfx::ILight {
public:
    Light_CH422G() : backlight_pin_(2), exio_shadow_(0) {}

    bool init(uint8_t brightness) override {
        setBrightness(brightness);
        return true;
    }

    void setBrightness(uint8_t brightness) override {
        const bool state = (brightness > 0);
        write_exio(backlight_pin_, state);
    }

    void setBacklightPin(uint8_t pin) { backlight_pin_ = pin; }
    void setInitialState(uint8_t shadow) { exio_shadow_ = shadow; }

private:
    uint8_t backlight_pin_;
    uint8_t exio_shadow_;

    void write_exio(uint8_t pin, bool state) {
        if (state) {
            exio_shadow_ |= static_cast<uint8_t>(1U << pin);
        } else {
            exio_shadow_ &= static_cast<uint8_t>(~(1U << pin));
        }

        // CH422G write enable + output state.
        Wire.beginTransmission(0x24);
        Wire.write(0x01);
        Wire.endTransmission();

        Wire.beginTransmission(0x38);
        Wire.write(exio_shadow_);
        Wire.endTransmission();
    }
};

class LGFX_Waveshare7 : public lgfx::LGFX_Device {
public:
    lgfx::Bus_RGB bus_;
    lgfx::Panel_RGB panel_;
    Light_CH422G light_;

    LGFX_Waveshare7() {
        {
            auto cfg = panel_.config();
            cfg.memory_width = 800;
            cfg.memory_height = 480;
            cfg.panel_width = 800;
            cfg.panel_height = 480;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            panel_.config(cfg);
        }

        {
            auto cfg = panel_.config_detail();
            // use_psram = 2: LovyanGFX allocates two 750 KB framebuffers in PSRAM
            // (front + back).  writePixels targets the inactive back buffer;
            // at vsync the pointers swap atomically, so the LCD DMA never reads
            // a partially-written frame.  This is the correct fix for RGB panel tearing.
            // N16R8 has 8 MB OPI PSRAM so the extra 750 KB is trivial.
            cfg.use_psram = 2;
            panel_.config_detail(cfg);
        }

        {
            auto cfg = bus_.config();
            cfg.panel = &panel_;

            cfg.pin_d0 = GPIO_NUM_14;   // B0
            cfg.pin_d1 = GPIO_NUM_38;   // B1
            cfg.pin_d2 = GPIO_NUM_18;   // B2
            cfg.pin_d3 = GPIO_NUM_17;   // B3
            cfg.pin_d4 = GPIO_NUM_10;   // B4
            cfg.pin_d5 = GPIO_NUM_39;   // G0
            cfg.pin_d6 = GPIO_NUM_0;    // G1
            cfg.pin_d7 = GPIO_NUM_45;   // G2
            cfg.pin_d8 = GPIO_NUM_48;   // G3
            cfg.pin_d9 = GPIO_NUM_47;   // G4
            cfg.pin_d10 = GPIO_NUM_21;  // G5
            cfg.pin_d11 = GPIO_NUM_1;   // R0
            cfg.pin_d12 = GPIO_NUM_2;   // R1
            cfg.pin_d13 = GPIO_NUM_42;  // R2
            cfg.pin_d14 = GPIO_NUM_41;  // R3
            cfg.pin_d15 = GPIO_NUM_40;  // R4

            cfg.pin_henable = GPIO_NUM_5;  // DE
            cfg.pin_vsync = GPIO_NUM_3;
            cfg.pin_hsync = GPIO_NUM_46;
            cfg.pin_pclk = GPIO_NUM_7;
            // 14 MHz reduces PSRAM/EDMA contention on RGB panels and is a common
            // practical setting to mitigate intermittent flicker/tearing.
            cfg.freq_write = 14000000;

            cfg.hsync_polarity = 0;
            cfg.hsync_front_porch = 8;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch = 8;
            cfg.vsync_polarity = 0;
            cfg.vsync_front_porch = 16;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch = 16;
            cfg.pclk_idle_high = 1;

            bus_.config(cfg);
        }

        panel_.setBus(&bus_);
        panel_.light(&light_);
        setPanel(&panel_);
    }

    bool init_impl(bool use_reset, bool use_clear) override {
        Wire.begin(8, 9);
        Wire.setClock(400000);

        // CH422G initial state: TP_RST=1, LCD_BL=1, LCD_RST=1
        // Mapping follows Waveshare CH422G pattern.
        const uint8_t initial_state = static_cast<uint8_t>((1U << 1) | (1U << 2) | (1U << 3));
        light_.setInitialState(initial_state);
        light_.setBacklightPin(2);
        write_ch422g(initial_state);
        delay(10);

        return lgfx::LGFX_Device::init_impl(use_reset, use_clear);
    }

private:
    void write_ch422g(uint8_t data) {
        Wire.beginTransmission(0x24);
        Wire.write(0x01);
        Wire.endTransmission();

        Wire.beginTransmission(0x38);
        Wire.write(data);
        Wire.endTransmission();
    }
};

}  // namespace lgfx_custom
