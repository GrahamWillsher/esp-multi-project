#include "espnow/espnow_runtime.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp32common/config/timing_config.h>
#include <esp32common/espnow/connection_manager.h>
#include <esp32common/espnow/message_router.h>
#include <channel_manager.h>
#include <espnow_discovery.h>
#include <espnow_peer_manager.h>

#include "common_lcd.h"
#include "espnow/battery_data_store.h"
#include "espnow/espnow_runtime_detail.h"
#include "espnow/rx_connection_handler.h"
#include "espnow/rx_heartbeat_manager.h"
#include "espnow/rx_state_machine.h"
#include "helpers.h"
#include "logging_config.h"
#include "task_config.h"

namespace ESPNowRuntime {
namespace {

constexpr uint32_t kWorkerPollMs = 100;
constexpr uint32_t kRxStateStaleTimeoutMs = 15000;
constexpr uint32_t kRxStateGraceWindowMs = 2000;
constexpr int kQueueSize = 32;

}  // namespace

bool init_radio() {
    if (Detail::g_radio_initialized.load()) {
        return true;
    }

    const esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ps_err != ESP_OK) {
        LOG_WARN("ESPNOW", "esp_wifi_set_ps(WIFI_PS_NONE) failed: %d", static_cast<int>(ps_err));
    }

    const esp_err_t init_rc = esp_now_init();
    if (init_rc != ESP_OK) {
        LOG_ERROR("ESPNOW", "esp_now_init failed: %d", static_cast<int>(init_rc));
        return false;
    }

    Detail::g_radio_initialized.store(true);
    LOG_INFO("ESPNOW", "ESP-NOW radio initialized on WiFi channel %d", static_cast<int>(WiFi.channel()));
    return true;
}

bool prepare_runtime() {
    if (ESPNow::message_queue == nullptr) {
        ESPNow::message_queue = xQueueCreate(kQueueSize, sizeof(espnow_queue_msg_t));
        if (ESPNow::message_queue == nullptr) {
            LOG_ERROR("ESPNOW", "Failed to create message queue");
            return false;
        }
    }

    Detail::setup_message_routes();
    return true;
}

bool init_state() {
    if (Detail::g_state_initialized.load()) {
        return true;
    }

    if (!Detail::g_radio_initialized.load()) {
        LOG_ERROR("ESPNOW", "init_state called before init_radio");
        return false;
    }

    if (!ChannelManager::instance().init()) {
        LOG_WARN("ESPNOW", "Channel manager init failed");
    }

    if (!EspNowConnectionManager::instance().init()) {
        LOG_ERROR("ESPNOW", "Connection manager init failed");
        return false;
    }
    EspNowConnectionManager::instance().set_auto_reconnect(true);
    EspNowConnectionManager::instance().set_connecting_timeout_ms(TimingConfig::ESPNOW_CONNECTING_TIMEOUT_MS);

    if (!RxStateMachine::instance().init()) {
        LOG_ERROR("ESPNOW", "RX state machine init failed");
        return false;
    }

    ReceiverConnectionHandler::instance().init();
    RxHeartbeatManager::instance().init();

    if (!Detail::g_callbacks_registered.load()) {
        if (esp_now_register_recv_cb(Detail::on_data_recv) != ESP_OK) {
            LOG_ERROR("ESPNOW", "esp_now_register_recv_cb failed");
            return false;
        }
        if (esp_now_register_send_cb(Detail::on_data_sent) != ESP_OK) {
            LOG_ERROR("ESPNOW", "esp_now_register_send_cb failed");
            return false;
        }
        Detail::g_callbacks_registered.store(true);
    }

    // Discovery is started in RuntimeTaskStartup::start_runtime_tasks() to
    // mirror receiver_2 startup ordering.  Keep a guarded fallback here.
    if (!EspnowDiscovery::instance().is_running()) {
        EspnowDiscovery::instance().start(
            []() {
                return EspNowConnectionManager::instance().is_connected();
            },
            TimingConfig::ANNOUNCEMENT_INTERVAL_MS,
            TaskConfig::ANNOUNCEMENT_PRIORITY,
            TaskConfig::ANNOUNCEMENT_TASK_STACK);
    }

    Detail::g_discovery_started.store(EspnowDiscovery::instance().is_running());
    if (!Detail::g_discovery_started.load()) {
        LOG_ERROR("ESPNOW", "Discovery task is not running after init_state");
        return false;
    }

    LOG_INFO("ESPNOW", "Discovery state: running=%s suspended=%s",
             EspnowDiscovery::instance().is_running() ? "yes" : "no",
             EspnowDiscovery::instance().is_suspended() ? "yes" : "no");

    Detail::g_state_initialized.store(true);
    LOG_INFO("ESPNOW", "ESP-NOW state initialized (staged LCD receiver lifecycle)");
    return true;
}

void task_worker(void* /*parameter*/) {
    auto& router = EspnowMessageRouter::instance();
    auto& connection_handler = ReceiverConnectionHandler::instance();
    auto& connection_manager = EspNowConnectionManager::instance();

    espnow_queue_msg_t msg{};
    for (;;) {
        if (!Detail::g_state_initialized.load()) {
            smart_delay(kWorkerPollMs);
            continue;
        }

        if (ESPNow::message_queue != nullptr &&
            xQueueReceive(ESPNow::message_queue, &msg, pdMS_TO_TICKS(kWorkerPollMs)) == pdPASS) {
            if (msg.len > 0) {
                const uint8_t msg_type = msg.data[0];

                connection_handler.on_link_activity(msg.mac);
                RxStateMachine::instance().on_message_processing(msg_type, ++Detail::g_rx_message_seq);

                if (!EspnowPeerManager::is_peer_registered(msg.mac)) {
                    if (EspnowPeerManager::add_peer(msg.mac, 0)) {
                        connection_handler.on_peer_registered(msg.mac);
                    }
                } else if (!connection_manager.is_connected()) {
                    connection_handler.on_peer_registered(msg.mac);
                }

                if (!router.route_message(msg)) {
                    RxStateMachine::instance().on_message_error();
                    LOG_WARN("ESPNOW", "Unhandled message type=%u len=%d",
                             static_cast<unsigned>(msg_type), msg.len);
                } else {
                    RxStateMachine::instance().on_message_valid();
                }
            }
        }

        connection_manager.process_events();
        connection_handler.tick();
        RxHeartbeatManager::instance().tick();
        BatteryData::refresh_staleness();
        RxStateMachine::instance().check_stale(kRxStateStaleTimeoutMs, kRxStateGraceWindowMs);
    }
}

bool is_connected() {
    return Detail::g_state_initialized.load() && EspNowConnectionManager::instance().is_connected();
}

}  // namespace ESPNowRuntime
