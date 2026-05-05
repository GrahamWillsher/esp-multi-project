#include "espnow/espnow_runtime.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp32common/config/timing_config.h>
#include <esp32common/espnow/common.h>
#include <esp32common/espnow/connection_manager.h>
#include <esp32common/espnow/mac_utils.h>
#include <esp32common/espnow/tx_scheduler.h>
#include <rx_route_registry.h>
#include <channel_manager.h>
#include <espnow_peer_manager.h>
#include <firmware_version.h>

#include "common_lcd.h"
#include "espnow/battery_data_store.h"
#include "espnow/espnow_runtime_detail.h"
#include "espnow/espnow_send.h"
#include "espnow/rx_connection_handler.h"
#include "espnow/rx_heartbeat_manager.h"
#include "espnow/rx_state_machine.h"
#include "espnow/type_catalog_cache.h"
#include "helpers.h"
#include "logging_config.h"
#include "task_config.h"
#include "mqtt/mqtt_client.h"
#include "../../lib/webserver_lcd/utils/transmitter_manager.h"

// Defined in webserver runtime module.
void notify_sse_data_updated();

namespace ESPNowRuntime {
namespace {

constexpr uint32_t kWorkerPollMs = 100;
constexpr uint32_t kRxStateStaleTimeoutMs = 15000;
constexpr uint32_t kRxStateGraceWindowMs = 2000;
constexpr uint32_t kPowerSaveReassertMs = 60000;
constexpr int kQueueSize = 12;

esp_err_t send_config_section_request(const uint8_t* mac,
                                      config_section_t section,
                                      uint32_t requested_version = 0) {
    config_section_request_t request{};
    request.type = msg_config_section_request;
    request.section = section;
    request.requested_version = requested_version;
    return EspnowTxScheduler::send(mac, &request, sizeof(request), "CONFIG_SECTION_REQ");
}

bool send_lcd_initialization_burst(void* /*context*/, const uint8_t* transmitter_mac) {
    request_data_t request{msg_request_data, subtype_power_profile};
    esp_err_t result = EspnowTxScheduler::send(transmitter_mac, &request, sizeof(request), "REQUEST_DATA");
    if (result == ESP_OK) {
        LOG_INFO("RX_CONN", "[INIT] Sent power profile request");
    } else {
        LOG_WARN("RX_CONN", "[INIT] Failed power profile request: %s", esp_err_to_name(result));
    }

    version_announce_t announce{};
    announce.type = msg_version_announce;
    announce.firmware_version = FW_VERSION_NUMBER;
    announce.protocol_version = PROTOCOL_VERSION;
    strncpy(announce.device_type, DEVICE_NAME, sizeof(announce.device_type) - 1);
    strncpy(announce.build_date, __DATE__, sizeof(announce.build_date) - 1);
    strncpy(announce.build_time, __TIME__, sizeof(announce.build_time) - 1);

    result = EspnowTxScheduler::send(transmitter_mac, &announce, sizeof(announce), "VERSION_ANNOUNCE");
    if (result == ESP_OK) {
        LOG_INFO("RX_CONN", "[INIT] Sent version info: %d.%d.%d",
                 FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    } else {
        LOG_WARN("RX_CONN", "[INIT] Failed version announce: %s", esp_err_to_name(result));
    }

    LOG_INFO("RX_CONN", "[INIT] Paced init started - high-priority requests sent, config/LED/catalogs deferred to tick()");
    return true;
}

void on_lcd_connected(void* /*context*/) {
    RxStateMachine::instance().on_connection_established();
    RxHeartbeatManager::instance().on_connection_established();
}

void on_lcd_connection_lost(void* /*context*/) {
    RxStateMachine::instance().on_connection_lost();
}

void on_lcd_config_update_sent(void* /*context*/) {
    RxStateMachine::instance().on_config_update_sent();
}

void disconnect_lcd_mqtt(void* /*context*/) {
    MqttClient::disconnect();
}

void on_lcd_tx_reboot(void* /*context*/) {
    ReceiverConnectionHandler::instance().on_transmitter_reboot_detected();
}

void on_lcd_heartbeat_payload(void* /*context*/, const heartbeat_t* hb, const uint8_t* /*mac*/) {
    TransmitterManager::updateTimeData(hb->uptime_ms, hb->unix_time, hb->utc_offset_min, hb->time_source);
    TransmitterManager::updateHeartbeatFlags(hb->flags);
    notify_sse_data_updated();
}

void on_lcd_heartbeat_ack_enqueue_failure(void* /*context*/, esp_err_t err) {
    if (err == ESP_ERR_ESPNOW_NO_MEM) {
        ReceiverConnectionHandler::instance().on_ack_send_pressure("heartbeat ACK enqueue no-mem");
    }
}

uint8_t lcd_connection_state(void* /*context*/) {
    return static_cast<uint8_t>(RxStateMachine::instance().connection_state());
}

void run_lcd_project_tick(void* /*context*/, ReceiverConnectionHandler& handler, uint32_t now) {
    const uint8_t* transmitter_mac = handler.get_transmitter_mac();
    if (!EspNowMacUtils::has_valid_mac(transmitter_mac)) {
        return;
    }

    if ((now - handler.last_config_retry_ms()) >= handler.config_retry_interval_ms()) {
        bool requested_any = false;

        if (!TransmitterManager::isIPKnown()) {
            const esp_err_t result = send_config_section_request(transmitter_mac, config_section_network, 0);
            if (result != ESP_OK) {
                LOG_WARN("RX_CONN", "Retry NETWORK config request failed: %s", esp_err_to_name(result));
            } else {
                requested_any = true;
            }
        }

        if (!TransmitterManager::isMqttConfigKnown()) {
            const esp_err_t result = send_config_section_request(transmitter_mac, config_section_mqtt, 0);
            if (result != ESP_OK) {
                LOG_WARN("RX_CONN", "Retry MQTT config request failed: %s", esp_err_to_name(result));
            } else {
                requested_any = true;
            }
        }

        if (requested_any) {
            LOG_INFO("RX_CONN", "Retried missing config sections (network=%s mqtt=%s)",
                     TransmitterManager::isIPKnown() ? "cached" : "requested",
                     TransmitterManager::isMqttConfigKnown() ? "cached" : "requested");
        }

        handler.set_last_config_retry_ms(now);
    }

    auto& led_sync = handler.led_sync_policy();
    if (led_sync.is_pending()) {
        if (led_sync.tick(now) && send_led_state_request()) {
            led_sync.mark_sent(now);
        }
        if (!led_sync.is_pending() && led_sync.attempt_count() >= RxLedSyncPolicy::MAX_ATTEMPTS) {
            LOG_WARN("RX_CONN", "[LED_SYNC] No LED response after %u attempt(s)",
                     static_cast<unsigned>(led_sync.attempt_count()));
        }
    }

    auto& catalog_retry = handler.catalog_retry_policy();
    if (!catalog_retry.is_due(now, handler.connected_at_ms())) {
        return;
    }

    catalog_retry.tick_item("catalog versions",
        !catalog_retry.versions_received(),
        &send_type_catalog_versions_request,
        catalog_retry.versions_retry_count, now);

    catalog_retry.tick_item("battery catalog",
        TypeCatalogCache::battery_refresh_required() || !TypeCatalogCache::has_battery_entries(),
        &send_battery_types_request,
        catalog_retry.battery_retry_count, now);

    catalog_retry.tick_item("inverter catalog",
        TypeCatalogCache::inverter_refresh_required() || !TypeCatalogCache::has_inverter_entries(),
        &send_inverter_types_request,
        catalog_retry.inverter_retry_count, now);

    catalog_retry.tick_item("inverter interfaces",
        !TypeCatalogCache::has_inverter_interface_entries(),
        &send_inverter_interfaces_request,
        catalog_retry.interface_retry_count, now);

    catalog_retry.mark_ticked(now);
}

inline bool should_log_alive_for_message(uint8_t msg_type) {
    return msg_type != msg_heartbeat &&
           msg_type != msg_heartbeat_ack &&
           !is_discovery_message_type(msg_type);
}

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

    ReceiverConnectionHandlerHooks handler_hooks{};
    handler_hooks.on_connected = &on_lcd_connected;
    handler_hooks.on_connection_lost = &on_lcd_connection_lost;
    handler_hooks.disconnect_mqtt = &disconnect_lcd_mqtt;
    handler_hooks.on_config_update_sent = &on_lcd_config_update_sent;
    handler_hooks.send_initialization_burst = &send_lcd_initialization_burst;
    handler_hooks.run_project_specific_tick = &run_lcd_project_tick;
    // Re-install the esp_now_register_send_cb after an emergency ESP-NOW stack
    // reinitialisation (esp_now_deinit removes all registered callbacks).
    handler_hooks.reinstall_send_cb = [](void*) {
        if (esp_now_register_send_cb(Detail::on_data_sent) != ESP_OK) {
            LOG_ERROR("ESPNOW", "reinstall_send_cb: esp_now_register_send_cb failed");
        } else {
            LOG_INFO("ESPNOW", "reinstall_send_cb: send callback re-registered OK");
            // Also re-register recv callback.
            if (esp_now_register_recv_cb(Detail::on_data_recv) != ESP_OK) {
                LOG_ERROR("ESPNOW", "reinstall_send_cb: esp_now_register_recv_cb failed");
            }
        }
    };
    ReceiverConnectionHandler::instance().configure_hooks(handler_hooks);

    RxHeartbeatManagerHooks heartbeat_hooks{};
    heartbeat_hooks.on_transmitter_reboot_detected = &on_lcd_tx_reboot;
    heartbeat_hooks.on_heartbeat_payload = &on_lcd_heartbeat_payload;
    heartbeat_hooks.on_heartbeat_ack_enqueue_failure = &on_lcd_heartbeat_ack_enqueue_failure;
    heartbeat_hooks.get_connection_state = &lcd_connection_state;
    RxHeartbeatManager::instance().configure_hooks(heartbeat_hooks);

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

    // Register broadcast peer so the receiver can receive TX's broadcast PROBE
    // frames during channel-hop scanning.  No data is ever sent by the receiver
    // as a broadcast — this is receive-side registration only.
    if (!EspnowPeerManager::add_broadcast_peer()) {
        LOG_WARN("ESPNOW", "Failed to register broadcast peer — TX probe reception may fail");
    } else {
        LOG_INFO("ESPNOW", "Broadcast peer registered (RX-side, receive-only)");
    }

    Detail::g_state_initialized.store(true);
    LOG_INFO("ESPNOW", "ESP-NOW state initialized (staged LCD receiver lifecycle)");
    return true;
}

void task_worker(void* /*parameter*/) {
    auto& connection_handler = ReceiverConnectionHandler::instance();
    auto& connection_manager = EspNowConnectionManager::instance();

    espnow_queue_msg_t msg{};
    uint32_t last_ps_reassert_ms = millis();
    for (;;) {
        if (!Detail::g_state_initialized.load()) {
            smart_delay(kWorkerPollMs);
            continue;
        }

        const uint32_t now_ms = millis();
        if ((now_ms - last_ps_reassert_ms) >= kPowerSaveReassertMs) {
            last_ps_reassert_ms = now_ms;
            const esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_NONE);
            if (ps_err != ESP_OK) {
                LOG_WARN("ESPNOW", "Periodic esp_wifi_set_ps(WIFI_PS_NONE) failed: %d", static_cast<int>(ps_err));
            }
        }

        if (ESPNow::message_queue != nullptr &&
            xQueueReceive(ESPNow::message_queue, &msg, pdMS_TO_TICKS(kWorkerPollMs)) == pdPASS) {
            if (msg.len > 0) {
                const uint8_t msg_type = msg.data[0];

                const bool is_discovery_msg = is_discovery_message_type(msg_type);

                // Discovery traffic (PROBE/ACK) is expected while TX is channel-hopping.
                // Do not feed generic link-activity paths with these frames; only
                // payload-bearing and keepalive/status traffic should influence runtime
                // activity behavior.
                if (!is_discovery_msg) {
                    connection_handler.on_link_activity(msg.mac);
                }
                RxStateMachine::instance().on_message_processing(msg_type, ++Detail::g_rx_message_seq);

                if (!is_discovery_msg) {
                    if (!EspnowPeerManager::is_peer_registered(msg.mac)) {
                        if (EspnowPeerManager::add_peer(msg.mac, 0)) {
                            connection_handler.on_peer_registered(msg.mac);
                        }
                    } else if (!connection_manager.is_connected()) {
                        connection_handler.on_peer_registered(msg.mac);
                    }
                }

                if (!Detail::process_ingress_message(msg)) {
                    RxStateMachine::instance().on_message_error();
                    LOG_WARN("ESPNOW", "Unhandled message type=%u len=%d",
                             static_cast<unsigned>(msg_type), msg.len);
                } else {
                    RxStateMachine::instance().on_message_valid();

                    if (should_log_alive_for_message(msg_type)) {
                        const char* espnow_status = is_connected() ? "connected" : "waiting";
                        const char* conn_state = espnow_state_to_string(connection_manager.get_state());
                        const int channel = static_cast<int>(WiFi.channel());
                        const unsigned long rx_cb = static_cast<unsigned long>(
                            ESPNow::rx_callback_count.load(std::memory_order_relaxed));

                        LOG_INFO("MAIN", "alive: ms=%lu espnow=%s conn=%s ch=%d rxcb=%lu",
                                 static_cast<unsigned long>(now_ms),
                                 espnow_status,
                                 conn_state,
                                 channel,
                                 rx_cb);
                    }
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
