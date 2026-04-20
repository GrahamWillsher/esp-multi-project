#include "espnow/rx_connection_handler.h"

#include <Arduino.h>
#include <cstring>
#include <esp_now.h>
#include <esp32common/espnow/connection_event.h>
#include <esp32common/espnow/connection_manager.h>
#include <esp32common/espnow/mac_utils.h>
#include <esp32common/espnow/common.h>
#include <esp32common/espnow/tx_scheduler.h>
#include <channel_manager.h>
#include <espnow_discovery.h>
#include <espnow_peer_manager.h>
#include <firmware_version.h>

#include "espnow/espnow_send.h"
#include "espnow/rx_heartbeat_manager.h"
#include "espnow/rx_state_machine.h"
#include "espnow/type_catalog_cache.h"
#include "logging_config.h"
#include "../../lib/webserver_lcd/utils/transmitter_manager.h"

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

ReceiverConnectionHandler& ReceiverConnectionHandler::instance() {
    static ReceiverConnectionHandler instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ReceiverConnectionHandler::ReceiverConnectionHandler()
    : last_rx_time_ms_(0),
      power_data_confirmed_(false),
      connected_at_ms_(0),
      last_retry_ms_(0) {
    memset(transmitter_mac_, 0, sizeof(transmitter_mac_));
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

static esp_err_t send_config_section_request(const uint8_t* mac,
                                              config_section_t section,
                                              uint32_t requested_version = 0) {
    config_section_request_t request{};
    request.type              = msg_config_section_request;
    request.section           = section;
    request.requested_version = requested_version;
    return EspnowTxScheduler::send(mac, &request, sizeof(request), "CONFIG_SECTION_REQ");
}

static esp_err_t send_request_data_message(const uint8_t* mac, uint8_t subtype) {
    request_data_t request{msg_request_data, subtype};
    return EspnowTxScheduler::send(mac, &request, sizeof(request), "REQUEST_DATA");
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void ReceiverConnectionHandler::init() {
    last_rx_time_ms_ = millis();

    auto& connection_manager = EspNowConnectionManager::instance();
    connection_manager.set_heartbeat_timeout_ms(90000);
    connection_manager.set_heartbeat_timeout_enabled(true);

    connection_manager.register_state_callback(
        [](EspNowConnectionState old_state, EspNowConnectionState new_state) {
            LOG_INFO("RX_CONN", "State change: %s -> %s",
                     espnow_state_to_string(old_state),
                     espnow_state_to_string(new_state));

            auto& self = ReceiverConnectionHandler::instance();

            if (new_state == EspNowConnectionState::CONNECTED) {
                RxStateMachine::instance().on_connection_established();
                RxHeartbeatManager::instance().on_connection_established();

                // Suspend discovery while connected to avoid channel interference
                EspnowDiscovery::instance().suspend();

                self.deferred_peer_.reset();

                const uint8_t current_channel = ChannelManager::instance().get_channel();
                ChannelManager::instance().lock_channel(current_channel, "RX_CONN");
                LOG_INFO("RX_CONN", "Connected - channel locked at %u, discovery suspended",
                         static_cast<unsigned>(current_channel));

                self.connected_at_ms_      = millis();
                self.last_retry_ms_        = millis();
                self.last_config_retry_ms_ = millis();
                self.power_data_confirmed_ = false;

                self.send_initialization_requests(
                    EspNowConnectionManager::instance().get_peer_mac());

            } else if (old_state == EspNowConnectionState::CONNECTED &&
                       new_state == EspNowConnectionState::IDLE) {
                RxStateMachine::instance().on_connection_lost();
                self.on_connection_lost();
                self.deferred_peer_.reset();

                // Clean up peer registration
                const uint8_t* peer_mac = self.get_transmitter_mac();
                if (peer_mac && !EspNowMacUtils::is_broadcast_mac(peer_mac) &&
                    EspnowPeerManager::is_peer_registered(peer_mac)) {
                    if (EspnowPeerManager::remove_peer(peer_mac)) {
                        LOG_INFO("RX_CONN", "Removed peer on connection loss");
                    } else {
                        LOG_WARN("RX_CONN", "Failed to remove peer on connection loss");
                    }
                }

                ChannelManager::instance().unlock_channel("RX_CONN");
                EspnowDiscovery::instance().resume();
                LOG_INFO("RX_CONN",
                         "Connection lost - peer cleaned up, channel unlocked, discovery resumed");

            } else if (new_state == EspNowConnectionState::CONNECTING) {
                EspnowDiscovery::instance().resume();
                self.flush_deferred_peer_registered();
            }
        });

    (void)post_connection_event(EspNowEvent::CONNECTION_START, nullptr);
    LOG_INFO("RX_CONN", "Receiver connection handler initialized");
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

void ReceiverConnectionHandler::on_probe_received(const uint8_t* transmitter_mac) {
    if (transmitter_mac) {
        memcpy(transmitter_mac_, transmitter_mac, sizeof(transmitter_mac_));
    }
    last_rx_time_ms_ = millis();

    (void)post_connection_event(EspNowEvent::PEER_FOUND, transmitter_mac_);
    flush_deferred_peer_registered();
}

void ReceiverConnectionHandler::on_peer_registered(const uint8_t* transmitter_mac) {
    if (transmitter_mac) {
        memcpy(transmitter_mac_, transmitter_mac, sizeof(transmitter_mac_));
    }
    last_rx_time_ms_ = millis();

    const auto state = EspNowConnectionManager::instance().get_state();
    const bool is_connecting = (state == EspNowConnectionState::CONNECTING);
    const bool is_connected  = (state == EspNowConnectionState::CONNECTED);

    if (is_connected) {
        LOG_DEBUG("RX_CONN", "on_peer_registered() while CONNECTED - ignoring duplicate");
        return;
    }

    const bool should_post = deferred_peer_.on_peer_registered(
        transmitter_mac_, is_connecting, millis());

    if (should_post) {
        (void)post_connection_event(EspNowEvent::PEER_REGISTERED, transmitter_mac_);
        deferred_peer_.on_event_posted();
    }
}

void ReceiverConnectionHandler::on_data_received(const uint8_t* transmitter_mac) {
    if (transmitter_mac) {
        memcpy(transmitter_mac_, transmitter_mac, sizeof(transmitter_mac_));
    }
    last_rx_time_ms_ = millis();

    EspNowConnectionManager::instance().on_heartbeat_received();
    (void)post_connection_event(EspNowEvent::DATA_RECEIVED, transmitter_mac_);
}

void ReceiverConnectionHandler::on_link_activity(const uint8_t* transmitter_mac) {
    if (transmitter_mac) {
        memcpy(transmitter_mac_, transmitter_mac, sizeof(transmitter_mac_));
    }
    last_rx_time_ms_ = millis();

    if (connected_at_ms_ == 0 && EspNowMacUtils::has_valid_mac(transmitter_mac_)) {
        connected_at_ms_ = millis();
        last_retry_ms_   = millis();
    }

    flush_deferred_peer_registered();
}

void ReceiverConnectionHandler::on_power_data_received() {
    if (!power_data_confirmed_) {
        power_data_confirmed_ = true;
        LOG_INFO("RX_CONN", "Power-profile data confirmed");
    }
}

void ReceiverConnectionHandler::on_transmitter_reboot_detected() {
    power_data_confirmed_ = false;
    connected_at_ms_      = millis();
    last_retry_ms_        = 0;
    LOG_WARN("RX_CONN", "TX reboot detected - re-arming stream request retries");
}

void ReceiverConnectionHandler::on_type_catalog_versions_received() {
    catalog_retry_.on_versions_received();
}

void ReceiverConnectionHandler::on_led_state_received() {
    if (led_sync_.is_pending()) {
        LOG_DEBUG("RX_CONN", "Transmitter LED state received after %u attempt(s)",
                  static_cast<unsigned>(led_sync_.attempt_count()));
        led_sync_.on_response_received();
    }
}

void ReceiverConnectionHandler::on_config_update_sent() {
    RxStateMachine::instance().on_config_update_sent();
    LOG_DEBUG("RX_CONN", "Config update sent - stale detection grace window started");
}

void ReceiverConnectionHandler::on_connection_lost() {
    first_data_received_  = false;
    power_data_confirmed_ = false;
    connected_at_ms_      = 0;
    last_retry_ms_        = 0;
    last_config_retry_ms_ = 0;
    memset(transmitter_mac_, 0, sizeof(transmitter_mac_));

    led_sync_.reset();
    catalog_retry_.reset();
    deferred_peer_.reset();
}

// ---------------------------------------------------------------------------
// flush_deferred_peer_registered
// ---------------------------------------------------------------------------

void ReceiverConnectionHandler::flush_deferred_peer_registered() {
    const auto state = EspNowConnectionManager::instance().get_state();
    const bool is_connecting = (state == EspNowConnectionState::CONNECTING);

    uint8_t flush_mac[6] = {};
    if (deferred_peer_.try_flush(is_connecting, millis(), flush_mac)) {
        (void)post_connection_event(EspNowEvent::PEER_REGISTERED, flush_mac);
        deferred_peer_.on_event_posted();
        LOG_INFO("RX_CONN", "Flushed deferred PEER_REGISTERED in CONNECTING state");
    }
}

// ---------------------------------------------------------------------------
// send_initialization_requests
// ---------------------------------------------------------------------------

void ReceiverConnectionHandler::send_initialization_requests(const uint8_t* transmitter_mac) {
    if (!transmitter_mac || !EspNowMacUtils::has_valid_mac(transmitter_mac)) {
        LOG_WARN("RX_CONN", "Cannot send initialization - invalid transmitter MAC");
        return;
    }

    const auto state = EspNowConnectionManager::instance().get_state();
    if (state != EspNowConnectionState::CONNECTED) {
        LOG_WARN("RX_CONN", "Cannot send initialization - state is %u (need CONNECTED)",
                 static_cast<uint8_t>(state));
        return;
    }

    first_data_received_ = true;
    catalog_retry_.reset();
    LOG_INFO("RX_CONN", "[INIT] Connection confirmed - sending initialization requests");

    // Request static config sections
    static constexpr struct { config_section_t section; const char* label; } kSections[] = {
        {config_section_mqtt,     "MQTT"},
        {config_section_network,  "NETWORK"},
        {config_section_metadata, "METADATA"},
        {config_section_battery,  "BATTERY"},
    };
    for (const auto& s : kSections) {
        esp_err_t r = send_config_section_request(transmitter_mac, s.section, 0);
        if (r != ESP_OK) {
            LOG_WARN("RX_CONN", "Failed to request %s config section: %s",
                     s.label, esp_err_to_name(r));
        }
    }

    // Request power profile data stream
    esp_err_t r = send_request_data_message(transmitter_mac, subtype_power_profile);
    if (r == ESP_OK) {
        LOG_INFO("RX_CONN", "Requested power profile data stream");
    } else {
        LOG_WARN("RX_CONN", "Failed to request power profile: %s", esp_err_to_name(r));
    }

    // Send version announcement to transmitter
    version_announce_t announce{};
    announce.type             = msg_version_announce;
    announce.firmware_version = FW_VERSION_NUMBER;
    announce.protocol_version = PROTOCOL_VERSION;
    strncpy(announce.device_type, DEVICE_NAME, sizeof(announce.device_type) - 1);
    strncpy(announce.build_date, __DATE__, sizeof(announce.build_date) - 1);
    strncpy(announce.build_time, __TIME__, sizeof(announce.build_time) - 1);
    r = EspnowTxScheduler::send(transmitter_mac, &announce, sizeof(announce), "VERSION_ANNOUNCE");
    if (r == ESP_OK) {
        LOG_INFO("RX_CONN", "Sent version info: %d.%d.%d",
                 FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    } else {
        LOG_WARN("RX_CONN", "Failed to send version info: %s", esp_err_to_name(r));
    }

    // LED state sync (bounded retry via policy)
    const bool led_sent = send_led_state_request();
    led_sync_.arm(led_sent, millis());
    LOG_INFO("RX_CONN", "[INIT] LED sync %s", led_sent ? "armed" : "armed (initial send failed)");

    // Type catalog population — selective based on cache state
    send_type_catalog_versions_request();

    if (!TypeCatalogCache::has_battery_entries()) {
        send_battery_types_request();
        LOG_INFO("RX_CONN", "[INIT] Requested battery type catalog (cache empty)");
    }
    if (!TypeCatalogCache::has_inverter_entries()) {
        send_inverter_types_request();
        LOG_INFO("RX_CONN", "[INIT] Requested inverter type catalog (cache empty)");
    }
    if (!TypeCatalogCache::has_inverter_interface_entries()) {
        send_inverter_interfaces_request();
        LOG_INFO("RX_CONN", "[INIT] Requested inverter interface catalog (cache empty)");
    }

    LOG_INFO("RX_CONN", "[INIT] Initialization requests sent");
}

// ---------------------------------------------------------------------------
// tick
// ---------------------------------------------------------------------------

void ReceiverConnectionHandler::tick() {
    flush_deferred_peer_registered();

    if (!EspNowConnectionManager::instance().is_connected()) {
        return;
    }

    const uint32_t now = millis();

    // ---- Config section re-request (network + MQTT) ----
    // Initial requests are sent on CONNECTED transition. If those packets are
    // dropped under transient ESP-NOW pressure, /transmitter/config can remain
    // unpopulated for the whole session. Retry at a low cadence until cache is hydrated.
    if (EspNowMacUtils::has_valid_mac(transmitter_mac_) &&
        (now - last_config_retry_ms_) >= CONFIG_RETRY_INTERVAL_MS) {
        bool requested_any = false;

        if (!TransmitterManager::isIPKnown()) {
            const esp_err_t r = send_config_section_request(transmitter_mac_, config_section_network, 0);
            if (r != ESP_OK) {
                LOG_WARN("RX_CONN", "Retry NETWORK config request failed: %s", esp_err_to_name(r));
            } else {
                requested_any = true;
            }
        }

        if (!TransmitterManager::isMqttConfigKnown()) {
            const esp_err_t r = send_config_section_request(transmitter_mac_, config_section_mqtt, 0);
            if (r != ESP_OK) {
                LOG_WARN("RX_CONN", "Retry MQTT config request failed: %s", esp_err_to_name(r));
            } else {
                requested_any = true;
            }
        }

        if (requested_any) {
            LOG_INFO("RX_CONN", "Retried missing config sections (network=%s mqtt=%s)",
                     TransmitterManager::isIPKnown() ? "cached" : "requested",
                     TransmitterManager::isMqttConfigKnown() ? "cached" : "requested");
        }

        last_config_retry_ms_ = now;
    }

    // ---- LED sync bounded retry ----
    if (led_sync_.is_pending() && EspNowMacUtils::has_valid_mac(transmitter_mac_)) {
        if (led_sync_.tick(now)) {
            if (send_led_state_request()) {
                led_sync_.mark_sent(now);
            }
        }
        if (!led_sync_.is_pending() &&
            led_sync_.attempt_count() >= RxLedSyncPolicy::MAX_ATTEMPTS) {
            LOG_WARN("RX_CONN", "[LED_SYNC] No LED response after %u attempt(s)",
                     static_cast<unsigned>(led_sync_.attempt_count()));
        }
    }

    // ---- REQUEST_DATA retry ----
    const bool recent_power_data =
        last_rx_time_ms_ > 0 && ((now - last_rx_time_ms_) <= POWER_DATA_FRESHNESS_MS);

    if (power_data_confirmed_ && recent_power_data) {
        goto catalog_retry;
    }

    if (connected_at_ms_ == 0 || !EspNowMacUtils::has_valid_mac(transmitter_mac_)) {
        return;
    }

    if ((now - connected_at_ms_) < RETRY_REQUEST_TIMEOUT_MS) goto catalog_retry;
    if ((now - last_retry_ms_)   < RETRY_INTERVAL_MS)        goto catalog_retry;

    last_retry_ms_ = now;
    LOG_WARN("RX_CONN", "No power-profile data yet - retrying REQUEST_DATA");
    {
        esp_err_t r = send_request_data_message(transmitter_mac_, subtype_power_profile);
        if (r != ESP_OK) {
            LOG_WARN("RX_CONN", "REQUEST_DATA retry failed: %s", esp_err_to_name(r));
        }
    }

catalog_retry:
    // ---- Catalog retry engine ----
    if (!catalog_retry_.is_due(now, connected_at_ms_)) {
        return;
    }

    catalog_retry_.tick_item("catalog versions",
        !catalog_retry_.versions_received(),
        &send_type_catalog_versions_request,
        catalog_retry_.versions_retry_count, now);

    catalog_retry_.tick_item("battery catalog",
        TypeCatalogCache::battery_refresh_required() || !TypeCatalogCache::has_battery_entries(),
        &send_battery_types_request,
        catalog_retry_.battery_retry_count, now);

    catalog_retry_.tick_item("inverter catalog",
        TypeCatalogCache::inverter_refresh_required() || !TypeCatalogCache::has_inverter_entries(),
        &send_inverter_types_request,
        catalog_retry_.inverter_retry_count, now);

    catalog_retry_.tick_item("inverter interfaces",
        !TypeCatalogCache::has_inverter_interface_entries(),
        &send_inverter_interfaces_request,
        catalog_retry_.interface_retry_count, now);

    catalog_retry_.mark_ticked(now);
}
