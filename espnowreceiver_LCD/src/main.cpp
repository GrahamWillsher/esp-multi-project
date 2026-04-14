#include <Arduino.h>

#include "app/demo_model.h"
#include "app_config.h"
#include "hal/lgfx_waveshare_7.h"
#include "ui/runtime/ui_runtime.h"

namespace {
lgfx_custom::LGFX_Waveshare7* display = nullptr;
DemoModel demo_model;

uint32_t last_state_ms = 0;
uint32_t last_log_ms = 0;

void log_line(const char* msg) {
    Serial.println(msg);
    Serial0.println(msg);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    Serial0.begin(115200);
    delay(800);
    log_line("");
    log_line("espnowreceiver_LCD: boot");
    log_line("boot: serial online (USB CDC + UART0)");

    // Defer LGFX construction until Arduino runtime is initialized.
    display = new lgfx_custom::LGFX_Waveshare7();
    log_line("boot: LGFX object constructed");

    log_line("boot: display.init() begin");
    const bool display_ok = display->init();
    Serial.printf("boot: display.init() result = %s\n", display_ok ? "ok" : "failed");
    Serial0.printf("boot: display.init() result = %s\n", display_ok ? "ok" : "failed");

    if (!display_ok) {
        log_line("FATAL: display initialization failed. Halting for diagnostics.");
        while (true) {
            delay(1000);
        }
    }

    display->setRotation(0);  // 0 = native landscape (800x480)
    log_line("boot: rotation set (landscape)");

    UI::Runtime::init(*display);
    UI::Runtime::run_startup_sequence();
    log_line("boot: splash sequence done");

    log_line("Phase 1 complete: panel initialized.");
    log_line("Phase 2/3/4 running: relative layout + widgets + demo model.");
}

void loop() {
    const uint32_t now = millis();

    if (now - last_state_ms >= AppConfig::STATE_UPDATE_MS) {
        demo_model.update(now);
        last_state_ms = now;
    }

    UI::Runtime::set_state(demo_model.state().soc_percent, demo_model.state().power_w);
    UI::Runtime::tick(now);

    if (now - last_log_ms >= 1000) {
        Serial.printf("alive: ms=%lu soc=%.1f power=%ld\n", static_cast<unsigned long>(now), demo_model.state().soc_percent, static_cast<long>(demo_model.state().power_w));
        Serial0.printf("alive: ms=%lu soc=%.1f power=%ld\n", static_cast<unsigned long>(now), demo_model.state().soc_percent, static_cast<long>(demo_model.state().power_w));
        last_log_ms = now;
    }
}
