#include <Arduino.h>
#include <ESP.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <runtime_common_utils/bootstrap_phase_runner.h>
#include <runtime_common_utils/ota_boot_guard.h>
#include <runtime_common_utils/setup_health_gate.h>
#include <esp32common/espnow/connection_manager.h>
#include <espnow_discovery.h>

#include "common_lcd.h"
#include "config/wifi_setup.h"
#include "espnow/espnow_runtime.h"
#include "helpers.h"
#include "../../lib/webserver_lcd/utils/transmitter_manager.h"
#include "hal/lgfx_waveshare_7.h"
#include "logging_config.h"
#include <receiver_config_manager.h>
#include "runtime/display_update_queue.h"
#include "runtime/runtime_task_startup.h"
#include "ui/runtime/ui_runtime.h"
#include "ui/runtime/ui_backend.h"
#include <webserver_lcd.h>

namespace {
lgfx_custom::LGFX_Waveshare7* display = nullptr;

uint32_t last_log_ms = 0;
bool g_espnow_transport_enabled = false;

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
    // Delay slightly to ensure all runtime systems are fully up.
    vTaskDelay(pdMS_TO_TICKS(500));
    WebserverLcd::init();
    log_line("boot: webserver task done");
    vTaskDelete(nullptr);  // self-delete
}

void bootstrap_services() {
    TransmitterManager::init();  // Creates NVS debounce timer before ESP-NOW handlers run.

    // Start webserver in a background task to avoid blocking setup().
    xTaskCreate(webserver_startup_task, "ws_init", 4096, nullptr, 1, nullptr);
    log_line("boot: webserver startup task queued");
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

    if (now - last_log_ms >= 1000) {
        const char* espnow_status = "disabled";
        const char* conn_state = "n/a";
        const char* discovery_state = "n/a";
        int channel = static_cast<int>(WiFi.channel());
        const unsigned long rx_cb = static_cast<unsigned long>(ESPNow::rx_callback_count);
        if (g_espnow_transport_enabled) {
            espnow_status = ESPNowRuntime::is_connected() ? "connected" : "waiting";
            conn_state = espnow_state_to_string(EspNowConnectionManager::instance().get_state());
            if (!EspnowDiscovery::instance().is_running()) {
                discovery_state = "stopped";
            } else {
                discovery_state = EspnowDiscovery::instance().is_suspended() ? "suspended" : "running";
            }
        }
        LOG_INFO("MAIN", "alive: ms=%lu espnow=%s conn=%s discovery=%s ch=%d rxcb=%lu",
                 static_cast<unsigned long>(now),
                 espnow_status,
                 conn_state,
                 discovery_state,
                 channel,
                 rx_cb);
        last_log_ms = now;
    }

    smart_delay(10);
}
