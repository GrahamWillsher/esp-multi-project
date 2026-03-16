#include "component_catalog_handlers.h"

#include "tx_send_guard.h"
#include "../system_settings.h"
#include "../datalayer/static_data.h"
#include "../network/mqtt_manager.h"
#include "../config/logging_config.h"

#if CONFIG_CAN_ENABLED
#include "../battery/battery_manager.h"
#include "../battery_emulator/battery/Battery.h"
#include "../battery_emulator/inverter/InverterProtocol.h"
#endif

#include <Arduino.h>
#include <Preferences.h>
#include <firmware_version.h>
#include <vector>
#include <cstddef>
#include <cstring>

namespace {

constexpr uint16_t MAX_CATALOG_BASE_VERSION = 32767;

uint16_t catalog_base_version() {
    const uint32_t fw = FW_VERSION_NUMBER;
    return static_cast<uint16_t>((fw > MAX_CATALOG_BASE_VERSION) ? MAX_CATALOG_BASE_VERSION : fw);
}

uint16_t battery_type_catalog_version() {
    return static_cast<uint16_t>(catalog_base_version() * 2u);
}

uint16_t inverter_type_catalog_version() {
    return static_cast<uint16_t>((catalog_base_version() * 2u) + 1u);
}

#if CONFIG_CAN_ENABLED
uint8_t comm_interface_to_wire_id(comm_interface iface) {
    switch (iface) {
        case comm_interface::Modbus:
            return 0;
        case comm_interface::RS485:
            return 1;
        case comm_interface::CanNative:
            return 2;
        case comm_interface::CanFdNative:
            return 3;
        case comm_interface::CanAddonMcp2515:
            return 4;
        case comm_interface::CanFdAddonMcp2518:
            return 5;
        default:
            return 0xFF;
    }
}
#endif

void send_type_catalog_fragments(const uint8_t* target_mac,
                                 uint8_t response_type,
                                 const std::vector<type_catalog_entry_t>& entries,
                                 const char* log_tag) {
    if (!target_mac) {
        return;
    }

    const size_t per_fragment = TYPE_CATALOG_MAX_ENTRIES_PER_FRAGMENT;
    const size_t total_fragments_sz = entries.empty() ? 1 : ((entries.size() + per_fragment - 1) / per_fragment);
    const uint8_t total_fragments = static_cast<uint8_t>(total_fragments_sz);
    const uint16_t sequence = static_cast<uint16_t>(esp_random() & 0xFFFF);

    for (uint8_t fragment_index = 0; fragment_index < total_fragments; ++fragment_index) {
        type_catalog_fragment_t fragment{};
        fragment.type = response_type;
        fragment.sequence = sequence;
        fragment.fragment_index = fragment_index;
        fragment.fragment_total = total_fragments;

        const size_t start = static_cast<size_t>(fragment_index) * per_fragment;
        const size_t remaining = (start < entries.size()) ? (entries.size() - start) : 0;
        const size_t count = (remaining > per_fragment) ? per_fragment : remaining;
        fragment.entry_count = static_cast<uint8_t>(count);

        for (size_t i = 0; i < count; ++i) {
            fragment.entries[i] = entries[start + i];
        }

        const size_t wire_size = offsetof(type_catalog_fragment_t, entries) +
                                 (static_cast<size_t>(fragment.entry_count) * sizeof(type_catalog_entry_t));

        esp_err_t result = TxSendGuard::send_to_receiver_guarded(
            target_mac,
            reinterpret_cast<const uint8_t*>(&fragment),
            wire_size,
            log_tag
        );

        if (result != ESP_OK) {
            LOG_WARN("TYPE_CATALOG", "%s fragment %u/%u failed: %s",
                     log_tag,
                     static_cast<unsigned>(fragment_index + 1),
                     static_cast<unsigned>(total_fragments),
                     esp_err_to_name(result));
            break;
        }
    }
}

} // namespace

namespace TxComponentCatalogHandlers {

void handle_component_config(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(component_config_msg_t)) {
        LOG_WARN("COMP_CFG", "Invalid component config packet size: %d", msg.len);
        return;
    }

    const component_config_msg_t* config = reinterpret_cast<const component_config_msg_t*>(msg.data);

    uint16_t calculated = 0;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(config);
    for (size_t i = 0; i < sizeof(component_config_msg_t) - sizeof(config->checksum); i++) {
        calculated += data[i];
    }

    if (calculated != config->checksum) {
        LOG_WARN("COMP_CFG", "Checksum mismatch: calc=%u, recv=%u", calculated, config->checksum);
        return;
    }

    SystemSettings& settings = SystemSettings::instance();
    bool battery_updated = false;
    bool inverter_updated = false;

#if CONFIG_CAN_ENABLED
    if (config->battery_type < static_cast<uint8_t>(BatteryType::Highest)) {
#else
    if (true) {
#endif
        if (settings.get_battery_profile_type() != config->battery_type) {
            settings.set_battery_profile_type(config->battery_type);
            battery_updated = true;
        }

#if CONFIG_CAN_ENABLED
        user_selected_battery_type = static_cast<BatteryType>(config->battery_type);
        if (!BatteryManager::instance().is_primary_battery_initialized()) {
            BatteryManager::instance().init_primary_battery(static_cast<BatteryType>(config->battery_type));
        } else {
            LOG_WARN("COMP_CFG", "Battery already initialized - change will apply on reboot");
        }
#endif
    } else {
        LOG_WARN("COMP_CFG", "Invalid battery type: %u", config->battery_type);
    }

#if CONFIG_CAN_ENABLED
    if (config->inverter_type < static_cast<uint8_t>(InverterProtocolType::Highest)) {
#else
    if (true) {
#endif
        if (settings.get_inverter_type() != config->inverter_type) {
            settings.set_inverter_type(config->inverter_type);
            inverter_updated = true;
        }

#if CONFIG_CAN_ENABLED
        user_selected_inverter_protocol = static_cast<InverterProtocolType>(config->inverter_type);
        if (!BatteryManager::instance().is_inverter_initialized()) {
            BatteryManager::instance().init_inverter(static_cast<InverterProtocolType>(config->inverter_type));
        } else {
            LOG_WARN("COMP_CFG", "Inverter already initialized - change will apply on reboot");
        }
#endif
    } else {
        LOG_WARN("COMP_CFG", "Invalid inverter type: %u", config->inverter_type);
    }

    if (battery_updated) {
        StaticData::update_battery_specs(config->battery_type);
    }

    if (inverter_updated) {
        StaticData::update_inverter_specs(config->inverter_type);
    }

    if (battery_updated || inverter_updated) {
        if (MqttManager::instance().is_connected()) {
            if (battery_updated) {
                MqttManager::instance().publish_battery_specs();
                MqttManager::instance().publish_battery_type_catalog();
            }
            if (inverter_updated) {
                MqttManager::instance().publish_inverter_specs();
                MqttManager::instance().publish_inverter_type_catalog();
            }
            MqttManager::instance().publish_static_specs();
        }
    }

    LOG_INFO("COMP_CFG", "Applied component selection: battery=%u inverter=%u",
             config->battery_type, config->inverter_type);

    if (battery_updated || inverter_updated) {
        LOG_WARN("COMP_CFG", ">>> Rebooting to apply component selection...");
        Serial.flush();

        MqttManager::instance().disconnect();

        delay(1000);
        ESP.restart();
    }
}

void handle_component_interface(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(component_interface_msg_t)) {
        LOG_WARN("COMP_IF", "Invalid component interface packet size: %d", msg.len);
        return;
    }

    const component_interface_msg_t* config = reinterpret_cast<const component_interface_msg_t*>(msg.data);

    uint16_t calculated = 0;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(config);
    for (size_t i = 0; i < sizeof(component_interface_msg_t) - sizeof(config->checksum); i++) {
        calculated += data[i];
    }

    if (calculated != config->checksum) {
        LOG_WARN("COMP_IF", "Checksum mismatch: calc=%u, recv=%u", calculated, config->checksum);
        return;
    }

    if (config->battery_interface > 5) {
        LOG_WARN("COMP_IF", "Invalid battery interface: %u", config->battery_interface);
        return;
    }

    if (config->inverter_interface > 5) {
        LOG_WARN("COMP_IF", "Invalid inverter interface: %u", config->inverter_interface);
        return;
    }

    Preferences prefs;
    if (!prefs.begin("batterySettings", false)) {
        LOG_WARN("COMP_IF", "Failed to open NVS for interface save");
        return;
    }

    prefs.putUInt("BATTCOMM", config->battery_interface);
    prefs.putUInt("INVCOMM", config->inverter_interface);
    prefs.end();

    LOG_INFO("COMP_IF", "Applied component interface selection: battery_if=%u inverter_if=%u",
             config->battery_interface, config->inverter_interface);

    LOG_WARN("COMP_IF", ">>> Rebooting to apply component interface selection...");
    Serial.flush();

    MqttManager::instance().disconnect();

    delay(1000);
    ESP.restart();
}

void handle_request_battery_types(const espnow_queue_msg_t& msg, uint8_t* receiver_mac) {
    memcpy(receiver_mac, msg.mac, 6);

    std::vector<type_catalog_entry_t> entries;

#if CONFIG_CAN_ENABLED
    for (int id = 0; id < static_cast<int>(BatteryType::Highest); ++id) {
        const BatteryType type = static_cast<BatteryType>(id);
        const char* name = name_for_battery_type(type);
        if (!name || name[0] == '\0') {
            continue;
        }

        type_catalog_entry_t entry{};
        entry.id = static_cast<uint8_t>(id);
        strncpy(entry.name, name, sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';
        entries.push_back(entry);
    }
#else
    type_catalog_entry_t fallback{};
    fallback.id = 0;
    strncpy(fallback.name, "None", sizeof(fallback.name) - 1);
    entries.push_back(fallback);
#endif

    send_type_catalog_fragments(msg.mac, msg_battery_types_fragment, entries, "battery_types_fragment");
    LOG_INFO("TYPE_CATALOG", "Sent battery catalog (%u entries)", (unsigned)entries.size());
}

void handle_request_inverter_types(const espnow_queue_msg_t& msg, uint8_t* receiver_mac) {
    memcpy(receiver_mac, msg.mac, 6);

    std::vector<type_catalog_entry_t> entries;

#if CONFIG_CAN_ENABLED
    for (int id = 0; id < static_cast<int>(InverterProtocolType::Highest); ++id) {
        const InverterProtocolType type = static_cast<InverterProtocolType>(id);
        const char* name = name_for_inverter_type(type);
        if (!name || name[0] == '\0') {
            continue;
        }

        type_catalog_entry_t entry{};
        entry.id = static_cast<uint8_t>(id);
        strncpy(entry.name, name, sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';
        entries.push_back(entry);
    }
#else
    type_catalog_entry_t fallback{};
    fallback.id = 0;
    strncpy(fallback.name, "None", sizeof(fallback.name) - 1);
    entries.push_back(fallback);
#endif

    send_type_catalog_fragments(msg.mac, msg_inverter_types_fragment, entries, "inverter_types_fragment");
    LOG_INFO("TYPE_CATALOG", "Sent inverter catalog (%u entries)", (unsigned)entries.size());
}

void handle_request_inverter_interfaces(const espnow_queue_msg_t& msg, uint8_t* receiver_mac) {
    memcpy(receiver_mac, msg.mac, 6);

    std::vector<type_catalog_entry_t> entries;

#if CONFIG_CAN_ENABLED
    if (esp32hal) {
        const auto interfaces = esp32hal->available_interfaces();
        for (const auto iface : interfaces) {
            const uint8_t wire_id = comm_interface_to_wire_id(iface);
            if (wire_id == 0xFF) {
                continue;
            }

            const char* name = name_for_comm_interface(iface);
            if (!name || name[0] == '\0') {
                continue;
            }

            type_catalog_entry_t entry{};
            entry.id = wire_id;
            strncpy(entry.name, name, sizeof(entry.name) - 1);
            entry.name[sizeof(entry.name) - 1] = '\0';
            entries.push_back(entry);
        }
    }
#endif

    if (entries.empty()) {
        const char* fallback_names[] = {
            "Modbus",
            "RS485",
            "CAN (Native)",
            "CAN-FD (Native)",
            "CAN (MCP2515 add-on)",
            "CAN-FD (MCP2518 add-on)"
        };

        for (uint8_t id = 0; id < 6; ++id) {
            type_catalog_entry_t entry{};
            entry.id = id;
            strncpy(entry.name, fallback_names[id], sizeof(entry.name) - 1);
            entry.name[sizeof(entry.name) - 1] = '\0';
            entries.push_back(entry);
        }
    }

    send_type_catalog_fragments(msg.mac, msg_inverter_interfaces_fragment, entries, "inverter_interfaces_fragment");
    LOG_INFO("TYPE_CATALOG", "Sent inverter interface catalog (%u entries)", (unsigned)entries.size());
}

void handle_request_type_catalog_versions(const espnow_queue_msg_t& msg, uint8_t* receiver_mac) {
    memcpy(receiver_mac, msg.mac, 6);

    type_catalog_versions_t versions{};
    versions.type = msg_type_catalog_versions;
    versions.battery_catalog_version = battery_type_catalog_version();
    versions.inverter_catalog_version = inverter_type_catalog_version();

    esp_err_t result = TxSendGuard::send_to_receiver_guarded(
        msg.mac,
        reinterpret_cast<const uint8_t*>(&versions),
        sizeof(versions),
        "type_catalog_versions"
    );

    if (result == ESP_OK) {
        LOG_INFO("TYPE_CATALOG", "Sent catalog versions: battery=%u inverter=%u",
                 (unsigned)versions.battery_catalog_version,
                 (unsigned)versions.inverter_catalog_version);
    } else {
        LOG_WARN("TYPE_CATALOG", "Failed to send catalog versions: %s", esp_err_to_name(result));
    }
}

} // namespace TxComponentCatalogHandlers
