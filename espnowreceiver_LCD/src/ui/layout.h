#pragma once

#include "app_config.h"

namespace UI {
struct Layout {
    static constexpr int led_x() {
        return AppConfig::SCREEN_WIDTH - AppConfig::LED_MARGIN_RIGHT - AppConfig::LED_RADIUS;
    }

    static constexpr int led_y() {
        return AppConfig::SCREEN_HEIGHT / 2;
    }

    static constexpr int soc_x() {
        return AppConfig::SCREEN_WIDTH / 2;
    }

    static constexpr int soc_y() {
        return (AppConfig::SCREEN_HEIGHT * 27) / 100;
    }

    static constexpr int bar_center_x() {
        return AppConfig::SCREEN_WIDTH / 2;
    }

    static constexpr int bar_y() {
        return (AppConfig::SCREEN_HEIGHT * 62) / 100;
    }

    static constexpr int power_text_x() {
        return AppConfig::SCREEN_WIDTH / 2;
    }

    static constexpr int power_text_y() {
        return (AppConfig::SCREEN_HEIGHT * 88) / 100;
    }
};
}  // namespace UI
