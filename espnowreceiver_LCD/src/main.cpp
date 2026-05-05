#include <Arduino.h>
#include <ESP.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <runtime_common_utils/bootstrap_phase_runner.h>
#include <runtime_common_utils/ota_boot_guard.h>
#include <runtime_common_utils/setup_health_gate.h>
#include <esp32common/espnow/connection_manager.h>

#include "common_lcd.h"
#include "config/wifi_setup.h"
#include "espnow/espnow_runtime.h"
#include "helpers.h"
#include "../../lib/webserver_lcd/utils/transmitter_manager.h"
#include "hal/lgfx_waveshare_7.h"
#include "logging_config.h"
#include "task_config.h"
#include <receiver_config_manager.h>
#include "runtime/display_update_queue.h"
#include "runtime/nvs_bootstrap.h"
#include "runtime/runtime_task_startup.h"
#include "ui/runtime/ui_runtime.h"
#include "ui/runtime/ui_backend.h"
#include <webserver_lcd.h>

namespace {
lgfx_custom::LGFX_Waveshare7* display = nullptr;

constexpr uint32_t kWebserverStartupTaskStack = 8192;
constexpr UBaseType_t kWebserverStartupTaskPriority = 1;

bool g_espnow_transport_enabled = false;

UI::Runtime::Backend::PowerBarRendererMode to_ui_power_bar_mode(ReceiverNetworkConfig::PowerBarRendererMode mode) {
    switch (mode) {
        case ReceiverNetworkConfig::PowerBarRendererMode::Original:
            return UI::Runtime::Backend::PowerBarRendererMode::Original;
        case ReceiverNetworkConfig::PowerBarRendererMode::Soft:
            return UI::Runtime::Backend::PowerBarRendererMode::Soft;
        case ReceiverNetworkConfig::PowerBarRendererMode::Linear:
            return UI::Runtime::Backend::PowerBarRendererMode::Linear;
        case ReceiverNetworkConfig::PowerBarRendererMode::Hybrid:
            return UI::Runtime::Backend::PowerBarRendererMode::Hybrid;
        case ReceiverNetworkConfig::PowerBarRendererMode::OriginalRounded:
            return UI::Runtime::Backend::PowerBarRendererMode::OriginalRounded;
        default:
            return UI::Runtime::Backend::PowerBarRendererMode::Original;
    }
}

void log_line(const char* msg) {
    LOG_INFO("MAIN", "%s", msg);
}

void bootstrap_hardware() {
    Serial.begin(115200);
    smart_delay(800);
    LOG_INFO("MAIN", "");
    LOG_INFO("MAIN", "espnowreceiver_LCD: boot");
    LOG_INFO("MAIN", "boot: serial online (USB CDC)");

    OtaBootGuard::begin("RX_LCD_BOOT_GUARD");

    display = new lgfx_custom::LGFX_Waveshare7();
    log_line("boot: LGFX object constructed");

    log_line("boot: display.init() begin");
    const bool display_ok = display->init();
    LOG_INFO("MAIN", "boot: display.init() result = %s", display_ok ? "ok" : "failed");

    if (!display_ok) {
        handle_error(ErrorSeverity::FATAL, "DISPLAY", "display initialization failed");
    }

    display->setRotation(0);
    log_line("boot: rotation set (landscape)");
}

void bootstrap_display() {
    (void)UI::Runtime::init(*display);
}

void bootstrap_display_content() {
    UI::Runtime::run_startup_sequence();
    log_line("boot: splash sequence done");
}

void bootstrap_filesystem() {
    const bool fs_ok = LittleFS.begin(true);
    LOG_INFO("MAIN", "boot: LittleFS=%s", fs_ok ? "ok" : "failed");

    const bool cfg_loaded = ReceiverNetworkConfig::loadConfig();
    LOG_INFO("MAIN", "boot: receiver config=%s", cfg_loaded ? "loaded" : "missing");

    const auto power_bar_mode = ReceiverNetworkConfig::getPowerBarRendererMode();
    UI::Runtime::set_power_bar_mode(to_ui_power_bar_mode(power_bar_mode));
    LOG_INFO("MAIN", "boot: power bar mode=%u", static_cast<unsigned>(power_bar_mode));

    bool wifi_ok = false;
    if (cfg_loaded) {
        wifi_ok = WiFiSetup::setup_from_loaded_config();
        LOG_INFO("MAIN", "boot: wifi=%s", wifi_ok ? "connected" : "not_connected");
    }

    if (!wifi_ok) {
        // No config saved or connection failed — fall back to AP so user can configure.
        log_line("boot: starting AP fallback (ESP32-LCD-Setup)");
        WiFiSetup::start_ap_fallback(/*has_credentials=*/cfg_loaded);
    }

    g_espnow_transport_enabled = WiFiSetup::is_sta_connected();
    LOG_INFO("MAIN", "boot: espnow transport=%s",
             g_espnow_transport_enabled ? "enabled (STA connected)" : "disabled (config AP mode)");

    char ip_buf[20] = {};
    WiFiSetup::get_ip_string(ip_buf, sizeof(ip_buf));
    // LVGL task has not started yet — safe to call LVGL API directly here.
    UI::Runtime::Backend::set_network_status(ip_buf, wifi_ok || WiFiSetup::is_ap_mode());
}

void webserver_startup_task(void* /*arg*/) {
    // Delay slightly to ensure WiFi bootstrap has started.
    vTaskDelay(pdMS_TO_TICKS(500));

    // Robust startup: do not permanently give up if WiFi is not yet ready
    // during early boot. Retry until httpd is running.
    constexpr uint32_t kRetryDelayMs = 1000;
    uint32_t attempts = 0;
    while (server == nullptr) {
        ++attempts;
        LOG_INFO("MAIN", "boot: webserver init attempt %lu", static_cast<unsigned long>(attempts));
        WebserverLcd::init();
        if (server != nullptr) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(kRetryDelayMs));
    }

    LOG_INFO("MAIN", "boot: webserver task done (attempts=%lu)",
             static_cast<unsigned long>(attempts));
    vTaskDelete(nullptr);  // self-delete
}

void bootstrap_services() {
    if (!RuntimeNvs::ensure_initialized()) {
        handle_error(ErrorSeverity::FATAL, "NVS", "default NVS initialization failed");
    }

    TransmitterManager::init();  // Creates NVS debounce timer before ESP-NOW handlers run.

    // Start webserver in a background task to avoid blocking setup().
    const BaseType_t rc = xTaskCreate(
        webserver_startup_task,
        "ws_init",
        kWebserverStartupTaskStack,
        nullptr,
        kWebserverStartupTaskPriority,
        nullptr);

    if (rc != pdPASS) {
        handle_error(ErrorSeverity::FATAL, "WEBSERVER", "failed to create webserver startup task");
    }

    LOG_INFO("MAIN", "boot: webserver startup task queued (stack=%lu)",
             static_cast<unsigned long>(kWebserverStartupTaskStack));
}

void bootstrap_espnow_radio() {
    if (!g_espnow_transport_enabled) {
        LOG_INFO("MAIN", "boot: espnow radio skipped (STA not connected)");
        return;
    }

    const bool espnow_ok = ESPNowRuntime::init_radio();
    LOG_INFO("MAIN", "boot: espnow radio=%s", espnow_ok ? "ok" : "failed");
    if (!espnow_ok) {
        handle_error(ErrorSeverity::FATAL, "ESPNOW", "ESP-NOW radio initialization failed");
    }
}

void bootstrap_tasks() {
    const bool primitives_ok = RuntimeTaskStartup::create_runtime_primitives();
    const bool tasks_ok = RuntimeTaskStartup::start_runtime_tasks();
    LOG_INFO("MAIN", "boot: runtime primitives=%s tasks=%s", primitives_ok ? "ok" : "failed", tasks_ok ? "ok" : "failed");
    if (!primitives_ok) {
        handle_error(ErrorSeverity::FATAL, "RTOS", "runtime primitives init failed");
    }
    if (!tasks_ok) {
        handle_error(ErrorSeverity::FATAL, "RTOS", "runtime task startup failed");
    }
}

void bootstrap_espnow_state() {
    if (!g_espnow_transport_enabled) {
        LOG_INFO("MAIN", "boot: espnow state skipped (config AP mode)");
        return;
    }

    const bool espnow_ok = ESPNowRuntime::init_state();
    LOG_INFO("MAIN", "boot: espnow state=%s", espnow_ok ? "ok" : "failed");
    if (!espnow_ok) {
        handle_error(ErrorSeverity::FATAL, "ESPNOW", "ESP-NOW state initialization failed");
    }
}

}  // namespace

void setup() {
    static const BootstrapPhaseRunner::Phase kBootstrapPhases[] = {
        {"hardware",         bootstrap_hardware},
        {"display",          bootstrap_display},
        {"display_content",  bootstrap_display_content},
        {"filesystem",       bootstrap_filesystem},
        {"services",         bootstrap_services},
        {"espnow_radio",     bootstrap_espnow_radio},
        {"tasks",            bootstrap_tasks},
        {"espnow_state",     bootstrap_espnow_state},
    };

    BootstrapPhaseRunner::run_phases(
        kBootstrapPhases,
        sizeof(kBootstrapPhases) / sizeof(kBootstrapPhases[0]));

    {
        const SetupHealthGate::Check checks[] = {
            {"heap_ok", ESP.getFreeHeap() > 32768},
            {"lvgl_mutex_ok", RTOS::lvgl_mutex != nullptr},
            {"espnow_queue_ok", !g_espnow_transport_enabled || ESPNow::message_queue != nullptr},
            {"display_queue_ok", RTOS::display_update_queue != nullptr},
        };

        const SetupHealthGate::Outcome outcome = SetupHealthGate::apply(
            "RX_LCD_BOOT_GUARD",
            checks,
            sizeof(checks) / sizeof(checks[0]),
            "receiver LCD setup health gate failed",
            "receiver LCD setup health gate passed");

        if (outcome == SetupHealthGate::Outcome::Error) {
            LOG_ERROR("BOOT_GUARD", "Receiver LCD setup health gate helper returned error");
        }
    }

    log_line("Phase 1 complete: panel initialized.");
    log_line("Phase B complete: dedicated LVGL task + display queue active.");
}

void loop() {
    const uint32_t now = millis();

    if (!g_espnow_transport_enabled && WiFiSetup::service_recovery()) {
        LOG_INFO("MAIN", "STA recovery succeeded; rebooting to initialize full runtime stack in STA mode");
        smart_delay(250);
        ESP.restart();
    }

    smart_delay(10);
}
