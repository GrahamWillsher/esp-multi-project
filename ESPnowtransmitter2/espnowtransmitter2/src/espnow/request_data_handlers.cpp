#include "request_data_handlers.h"

#include "tx_state_machine.h"
#include "tx_send_guard.h"
#include "../network/ethernet_manager.h"
#include "../settings/settings_manager.h"
#include "../config/logging_config.h"

#include <Arduino.h>
#include <esp_now.h>
#include <esp32common/espnow/packet_utils.h>
#include <esp32common/espnow/connection_manager.h>
#include <cstddef>

namespace {

battery_settings_full_msg_t build_battery_settings_message() {
    battery_settings_full_msg_t settings_msg{};
    settings_msg.type = msg_battery_info;
    settings_msg.capacity_wh = SettingsManager::instance().get_battery_capacity_wh();
    settings_msg.max_voltage_mv = SettingsManager::instance().get_battery_max_voltage_mv();
    settings_msg.min_voltage_mv = SettingsManager::instance().get_battery_min_voltage_mv();
    settings_msg.max_charge_current_a = SettingsManager::instance().get_battery_max_charge_current_a();
    settings_msg.max_discharge_current_a = SettingsManager::instance().get_battery_max_discharge_current_a();
    settings_msg.soc_high_limit = SettingsManager::instance().get_battery_soc_high_limit();
    settings_msg.soc_low_limit = SettingsManager::instance().get_battery_soc_low_limit();
    settings_msg.cell_count = SettingsManager::instance().get_battery_cell_count();
    settings_msg.chemistry = SettingsManager::instance().get_battery_chemistry();
    settings_msg.led_mode = SettingsManager::instance().get_battery_led_mode();

    uint16_t sum = 0;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&settings_msg);
    for (size_t i = 0; i < offsetof(battery_settings_full_msg_t, checksum); i++) {
        sum += bytes[i];
    }
    settings_msg.checksum = sum;

    return settings_msg;
}

const char* battery_chemistry_label(uint8_t chemistry) {
    static constexpr const char* kChemistryLabels[] = {"NCA", "NMC", "LFP", "LTO"};
    if (chemistry < (sizeof(kChemistryLabels) / sizeof(kChemistryLabels[0]))) {
        return kChemistryLabels[chemistry];
    }
    return "UNKNOWN";
}

} // namespace

namespace TxRequestDataHandlers {

void handle_request_data(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(request_data_t)) {
        LOG_WARN("DATA_REQUEST", "Packet too short: %d bytes", msg.len);
        return;
    }

    const request_data_t* req = reinterpret_cast<const request_data_t*>(msg.data);
    LOG_INFO("DATA_REQUEST", "REQUEST_DATA received (subtype=%d) from %02X:%02X:%02X:%02X:%02X:%02X",
             req->subtype, msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);

    if (req->subtype == subtype_power_profile) {
        TxStateMachine::instance().on_transmission_started();
        LOG_INFO("DATA_REQUEST", ">>> Power profile transmission ACTIVATED <<<");
        return;
    }

    auto& conn_mgr = EspNowConnectionManager::instance();
    auto state = conn_mgr.get_state();

    if (state != EspNowConnectionState::CONNECTED) {
        LOG_WARN("DATA_REQUEST", "Cannot respond to data request - receiver state is %u (need CONNECTED)",
                 (uint8_t)state);
        return;
    }

    switch (req->subtype) {
        case subtype_power_profile:
            break;

        case subtype_network_config: {
            if (EthernetManager::instance().is_connected()) {
                IPAddress local_ip = EthernetManager::instance().get_local_ip();
                IPAddress gateway = EthernetManager::instance().get_gateway_ip();
                IPAddress subnet = EthernetManager::instance().get_subnet_mask();

                espnow_packet_t packet;
                packet.type = msg_packet;
                packet.subtype = subtype_network_config;
                packet.seq = esp_random();
                packet.frag_index = 0;
                packet.frag_total = 1;
                packet.payload_len = 12;

                for (int i = 0; i < 4; i++) {
                    packet.payload[i] = local_ip[i];
                    packet.payload[4 + i] = gateway[i];
                    packet.payload[8 + i] = subnet[i];
                }

                packet.checksum = EspnowPacketUtils::calculate_checksum(packet.payload, packet.payload_len);
                esp_err_t result = TxSendGuard::send_to_receiver_guarded(
                    msg.mac,
                    (const uint8_t*)&packet,
                    sizeof(packet),
                    "network_config"
                );

                if (result == ESP_OK) {
                    LOG_DEBUG("DATA_REQUEST", "Sent network config: %s, GW: %s, Subnet: %s",
                              local_ip.toString().c_str(), gateway.toString().c_str(), subnet.toString().c_str());
                } else {
                    LOG_WARN("DATA_REQUEST", "Failed to send network config: %s", esp_err_to_name(result));
                }
            } else {
                LOG_WARN("DATA_REQUEST", "Ethernet not connected, cannot send network config");
            }
            break;
        }

        case subtype_battery_config: {
            LOG_DEBUG("DATA_REQUEST", ">>> Battery config request - sending FULL battery settings");

            const battery_settings_full_msg_t settings_msg = build_battery_settings_message();

            esp_err_t result = TxSendGuard::send_to_receiver_guarded(
                msg.mac,
                (const uint8_t*)&settings_msg,
                sizeof(settings_msg),
                "battery_settings"
            );
            if (result == ESP_OK) {
                LOG_INFO("DATA_REQUEST", "Sent FULL battery settings: %dWh, %dS, %s, %.1fA/%.1fA, SOC:%d-%d%%",
                         settings_msg.capacity_wh, settings_msg.cell_count, battery_chemistry_label(settings_msg.chemistry),
                         settings_msg.max_charge_current_a, settings_msg.max_discharge_current_a,
                         settings_msg.soc_low_limit, settings_msg.soc_high_limit);
            } else {
                LOG_WARN("DATA_REQUEST", "Failed to send battery settings: %s", esp_err_to_name(result));
            }
            break;
        }

        case subtype_charger_config:
        case subtype_inverter_config:
        case subtype_system_config:
        case subtype_events:
        case subtype_logs:
        case subtype_cell_info:
            LOG_DEBUG("DATA_REQUEST", "Subtype %d not implemented yet", req->subtype);
            break;

        default:
            LOG_WARN("DATA_REQUEST", "Unknown subtype: %d", req->subtype);
            break;
    }
}

void handle_abort_data(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(abort_data_t)) {
        return;
    }

    const abort_data_t* abort = reinterpret_cast<const abort_data_t*>(msg.data);
    LOG_DEBUG("DATA_ABORT", "ABORT_DATA (subtype=%d) from %02X:%02X:%02X:%02X:%02X:%02X",
              abort->subtype, msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);

    switch (abort->subtype) {
        case subtype_power_profile:
            TxStateMachine::instance().on_transmission_stopped();
            LOG_INFO("DATA_ABORT", ">>> Power profile transmission STOPPED");
            break;

        case subtype_events:
        case subtype_logs:
        case subtype_cell_info:
            LOG_DEBUG("DATA_ABORT", "Subtype %d not implemented yet", abort->subtype);
            break;

        default:
            LOG_WARN("DATA_ABORT", "Unknown subtype: %d", abort->subtype);
            break;
    }
}

} // namespace TxRequestDataHandlers
