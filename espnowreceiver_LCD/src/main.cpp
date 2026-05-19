#include <Arduino.h>
#include <ESP.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <runtime_common_utils/bootstrap_phase_runner.h>
#include <runtime_common_utils/ota_boot_guard.h>
#include <runtime_common_utils/setup_health_gate.h>
#include <runtime_common_utils/device_temperature.h>

#include "common_lcd.h"
#include "config/wifi_setup.h"
#include "helpers.h"
#include "../../lib/webserver_lcd/utils/receiver_config_manager.h"
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

bool g_runtime_ap_recovery_started = false;
uint32_t g_sta_disconnect_since_ms = 0;
uint32_t g_last_net_diag_ms = 0;
bool g_last_sta_connected = false;
uint32_t g_last_webserver_retry_ms = 0;
uint32_t g_webserver_retry_attempts = 0;

constexpr uint32_t kNetDiagIntervalMs = 10000;
constexpr uint32_t kWebserverRetryIntervalMs = 3000;

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

    char net_buf[80] = {};
    const bool ap_mode = WiFiSetup::is_ap_mode();
    const bool sta_mode = WiFiSetup::is_sta_connected();
    if (ap_mode && sta_mode) {
        snprintf(net_buf, sizeof(net_buf),
                 "WiFi APSTA A:%s S:%s",
                 WiFi.softAPIP().toString().c_str(),
                 WiFi.localIP().toString().c_str());
    } else if (ap_mode) {
        snprintf(net_buf, sizeof(net_buf), "WiFi AP %s", WiFi.softAPIP().toString().c_str());
    } else if (sta_mode) {
        snprintf(net_buf, sizeof(net_buf), "WiFi STA %s", WiFi.localIP().toString().c_str());
    } else {
        snprintf(net_buf, sizeof(net_buf), "WiFi disconnected");
    }
    // LVGL task has not started yet — safe to call LVGL API directly here.
    UI::Runtime::set_network_status(net_buf, sta_mode || ap_mode);
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

    // Keep receiver identity/version fields available for dashboard and APIs.
    ReceiverConfigManager::init();

    // Seed temperature reading early so dashboard/API has a valid initial value.
    DeviceTemperature::init();
    DeviceTemperature::sample_now();

    TransmitterManager::init();  // Creates NVS debounce timer before MQTT-driven cache updates.

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



}  // namespace

void setup() {
    static const BootstrapPhaseRunner::Phase kBootstrapPhases[] = {
        {"hardware",         bootstrap_hardware},
        {"display",          bootstrap_display},
        {"display_content",  bootstrap_display_content},
        {"filesystem",       bootstrap_filesystem},
        {"services",         bootstrap_services},
        {"tasks",            bootstrap_tasks},
    };

    BootstrapPhaseRunner::run_phases(
        kBootstrapPhases,
        sizeof(kBootstrapPhases) / sizeof(kBootstrapPhases[0]));

    {
        const SetupHealthGate::Check checks[] = {
            {"heap_ok", ESP.getFreeHeap() > 32768},
            {"lvgl_mutex_ok", RTOS::lvgl_mutex != nullptr},
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
    const bool sta_connected = WiFiSetup::is_sta_connected();
    const bool ap_mode = WiFiSetup::is_ap_mode();
    const bool network_available = sta_connected || ap_mode;

    if (sta_connected != g_last_sta_connected) {
        LOG_INFO("MAIN", "STA transition: %s -> %s",
                 g_last_sta_connected ? "up" : "down",
                 sta_connected ? "up" : "down");

        // Keep an already-running server alive across STA recovery transitions.
        // Forcing stop/start here can race socket teardown and cause listen/task
        // startup failures on some reconnect paths.
        if (sta_connected) {
            if (server == nullptr) {
                LOG_INFO("MAIN", "STA recovered with webserver down; watchdog will reinitialize");
            } else {
                LOG_INFO("MAIN", "STA recovered; keeping existing webserver instance running");
            }
        }

        g_last_sta_connected = sta_connected;
    }

    // If AP+STA fallback is active, continue retrying STA in background and
    // recover services in place once connection has stabilized.
    if (ap_mode && WiFiSetup::service_recovery()) {
        LOG_INFO("MAIN", "STA recovery succeeded; applying in-place runtime recovery");
        g_runtime_ap_recovery_started = false;
        g_sta_disconnect_since_ms = 0;

        if (server == nullptr) {
            LOG_INFO("MAIN", "Webserver not running after STA recovery; reinitializing");
            WebserverLcd::init();
        }
    }

    // Webserver watchdog: main.cpp is the SINGLE recovery coordinator (FSM spec section 3.6).
    // Check backoff before attempting init to prevent dual-ownership recovery.
    if (network_available && server == nullptr &&
        (g_last_webserver_retry_ms == 0 || (now - g_last_webserver_retry_ms) >= kWebserverRetryIntervalMs)) {
        // Respect deterministic backoff: do not call init while backoff active
        if (!WebserverLcd::is_webserver_backoff_active()) {
            // Backoff expired or never entered; attempt init
            g_last_webserver_retry_ms = now;
            ++g_webserver_retry_attempts;
            LOG_WARN("MAIN", "Webserver not running while network is available; reinitializing");
            WebserverLcd::init();
        }
    }

    if ((now - g_last_net_diag_ms) >= kNetDiagIntervalMs) {
        g_last_net_diag_ms = now;
        const wifi_mode_t mode = WiFi.getMode();
        const char* mode_str =
            (mode == WIFI_MODE_STA)   ? "STA" :
            (mode == WIFI_MODE_AP)    ? "AP" :
            (mode == WIFI_MODE_APSTA) ? "APSTA" : "NULL";

        LOG_INFO("NET", "mode=%s sta=%s ap=%s sta_ip=%s ap_ip=%s ch=%d",
                 mode_str,
                 sta_connected ? "up" : "down",
                 ap_mode ? "up" : "down",
                 WiFi.localIP().toString().c_str(),
                 WiFi.softAPIP().toString().c_str(),
                 static_cast<int>(WiFi.channel()));

        LOG_INFO("NET", "webserver=%s retry_attempts=%lu last_retry_ms=%lu",
                 (server != nullptr) ? "running" : "stopped",
             static_cast<unsigned long>(g_webserver_retry_attempts),
                 static_cast<unsigned long>(g_last_webserver_retry_ms));

        WebserverRuntimeMetrics web_metrics{};
        get_webserver_runtime_metrics(web_metrics);
        LOG_INFO("NET", "http metrics: init=%lu/%lu fail=%lu req_total=%lu req_fail=%lu active=%lu max_dur=%lu ms last_dur=%lu ms recycle=%lu",
                 static_cast<unsigned long>(web_metrics.init_successes),
                 static_cast<unsigned long>(web_metrics.init_attempts),
                 static_cast<unsigned long>(web_metrics.init_failures),
                 static_cast<unsigned long>(web_metrics.request_total),
                 static_cast<unsigned long>(web_metrics.request_failures),
                 static_cast<unsigned long>(web_metrics.active_requests),
                 static_cast<unsigned long>(web_metrics.max_request_duration_ms),
                 static_cast<unsigned long>(web_metrics.last_request_duration_ms),
                 static_cast<unsigned long>(web_metrics.recycle_count));
    }

    smart_delay(10);
}
