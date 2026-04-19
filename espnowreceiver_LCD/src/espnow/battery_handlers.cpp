#include "espnow/battery_handlers.h"

#include <cstring>

#include <esp32common/espnow/packet_utils.h>

#include "espnow/battery_data_store.h"
#include "logging_config.h"

namespace {

template <typename MessageT, typename ApplyFn, typename LogFn>
bool process_status_message(const espnow_queue_msg_t* msg,
                            const char* label,
                            ApplyFn apply,
                            LogFn log_fn) {
    if (!msg || msg->len < static_cast<int>(sizeof(MessageT))) {
        LOG_ERROR("BATTERY", "%s: Invalid message size %d, expected >= %d",
                  label,
                  msg ? msg->len : -1,
                  static_cast<int>(sizeof(MessageT)));
        return false;
    }

    const MessageT* data = reinterpret_cast<const MessageT*>(msg->data);
    if (!validate_checksum(data, sizeof(*data))) {
        LOG_ERROR("BATTERY", "%s: Invalid checksum - message rejected", label);
        return false;
    }

    apply(*data);
    log_fn();
    return true;
}

}  // namespace

bool validate_checksum(const void* data, size_t len) {
    if (!data || len < sizeof(uint32_t)) {
        return false;
    }

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
    uint32_t stored = 0;
    memcpy(&stored, bytes + len - sizeof(uint32_t), sizeof(stored));
    return EspnowPacketUtils::crc32_packet(bytes, len - sizeof(uint32_t)) == stored;
}

bool handle_battery_status(const espnow_queue_msg_t* msg) {
    return process_status_message<battery_status_msg_t>(
        msg,
        "Battery status",
        [](const battery_status_msg_t& data) {
            BatteryData::update_battery_status(data);
        },
        []() {
            BatteryData::TelemetrySnapshot s{};
            if (BatteryData::read_snapshot(s)) {
                LOG_DEBUG("BATTERY", "Battery Status: SOC=%.2f%%, V=%.2fV, I=%.2fA, T=%.1fC, P=%ldW, BMS=%u",
                          static_cast<double>(s.soc_percent),
                          static_cast<double>(s.voltage_V),
                          static_cast<double>(s.current_A),
                          static_cast<double>(s.temperature_C),
                          static_cast<long>(s.power_W),
                          static_cast<unsigned>(s.bms_status));
            }
        });
}

    bool handle_battery_info(const espnow_queue_msg_t* msg) {
    if (!msg) {
        return false;
    }

    if (msg->len == static_cast<int>(sizeof(battery_settings_full_msg_t))) {
        const auto* data = reinterpret_cast<const battery_settings_full_msg_t*>(msg->data);
        if (!validate_checksum(data, sizeof(*data))) {
            LOG_ERROR("BATTERY", "Battery settings: Invalid checksum - message rejected");
            return false;
        }

        BatteryData::update_battery_info(*data, 0);
        LOG_INFO("BATTERY", "Battery settings received: %luWh, %uS, chemistry=%u",
                 static_cast<unsigned long>(data->capacity_wh),
                 static_cast<unsigned>(data->cell_count),
                 static_cast<unsigned>(data->chemistry));
        return true;
    }

    if (msg->len >= static_cast<int>(sizeof(battery_info_msg_t))) {
        const auto* data = reinterpret_cast<const battery_info_msg_t*>(msg->data);
        if (!validate_checksum(data, sizeof(*data))) {
            LOG_ERROR("BATTERY", "Battery info: Invalid checksum - message rejected");
            return false;
        }

        BatteryData::update_battery_info(*data);
        LOG_INFO("BATTERY", "Battery info received: total=%luWh reported=%luWh cells=%u chemistry=%u",
                 static_cast<unsigned long>(data->total_capacity_Wh),
                 static_cast<unsigned long>(data->reported_capacity_Wh),
                 static_cast<unsigned>(data->number_of_cells),
                 static_cast<unsigned>(data->chemistry));
        return true;
    }

    LOG_ERROR("BATTERY", "Battery info: Invalid message size %d", msg->len);
    return false;
}

bool handle_charger_status(const espnow_queue_msg_t* msg) {
    return process_status_message<charger_status_msg_t>(
        msg,
        "Charger status",
        [](const charger_status_msg_t& data) {
            BatteryData::update_charger_status(data);
        },
        []() {
            BatteryData::TelemetrySnapshot s{};
            if (BatteryData::read_snapshot(s)) {
                LOG_DEBUG("BATTERY", "Charger Status=%u, HV=%.1fV/%.1fA, AC=%uV, P=%uW",
                          static_cast<unsigned>(s.charger_status),
                          static_cast<double>(s.charger_hv_voltage_V),
                          static_cast<double>(s.charger_hv_current_A),
                          static_cast<unsigned>(s.charger_ac_voltage_V),
                          static_cast<unsigned>(s.charger_power_W));
            }
        });
}

bool handle_inverter_status(const espnow_queue_msg_t* msg) {
    return process_status_message<inverter_status_msg_t>(
        msg,
        "Inverter status",
        [](const inverter_status_msg_t& data) {
            BatteryData::update_inverter_status(data);
        },
        []() {
            BatteryData::TelemetrySnapshot s{};
            if (BatteryData::read_snapshot(s)) {
                LOG_DEBUG("BATTERY", "Inverter Status=%u, AC=%uV/%.1fA@%.1fHz, P=%ldW",
                          static_cast<unsigned>(s.inverter_status),
                          static_cast<unsigned>(s.inverter_ac_voltage_V),
                          static_cast<double>(s.inverter_ac_current_A),
                          static_cast<double>(s.inverter_ac_frequency_Hz),
                          static_cast<long>(s.inverter_power_W));
            }
        });
}

bool handle_system_status(const espnow_queue_msg_t* msg) {
    return process_status_message<system_status_msg_t>(
        msg,
        "System status",
        [](const system_status_msg_t& data) {
            BatteryData::update_system_status(data);
        },
        []() {
            BatteryData::TelemetrySnapshot s{};
            if (BatteryData::read_snapshot(s)) {
                LOG_DEBUG("BATTERY", "System Status: Contactors=0x%02X, Errors=0x%02X, Warnings=0x%02X, Uptime=%lus",
                          static_cast<unsigned>(s.contactor_state),
                          static_cast<unsigned>(s.error_flags),
                          static_cast<unsigned>(s.warning_flags),
                          static_cast<unsigned long>(s.uptime_seconds));
            }
        });
}

bool handle_component_config(const espnow_queue_msg_t* msg) {
    return process_status_message<component_config_msg_t>(
        msg,
        "Component config",
        [](const component_config_msg_t& data) {
            BatteryData::update_component_config(data);
        },
        []() {
            BatteryData::TelemetrySnapshot s{};
            if (BatteryData::read_snapshot(s)) {
                LOG_INFO("BATTERY", "Component config: batt=%u inv=%u chg=%u shunt=%u ver=%lu",
                         static_cast<unsigned>(s.battery_type),
                         static_cast<unsigned>(s.inverter_type),
                         static_cast<unsigned>(s.charger_type),
                         static_cast<unsigned>(s.shunt_type),
                         static_cast<unsigned long>(s.component_config_version));
            }
        });
}
