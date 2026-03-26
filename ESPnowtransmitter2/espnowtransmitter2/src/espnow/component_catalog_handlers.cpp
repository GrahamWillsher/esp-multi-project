#include "component_catalog_handlers.h"

#include "tx_send_guard.h"
#include "../system_settings.h"
#include "../datalayer/static_data.h"
#include "../network/mqtt_manager.h"
#include "../config/logging_config.h"

#if CONFIG_CAN_ENABLED
#include "../battery/battery_manager.h"
#include "../battery_emulator/battery/BATTERIES.h"
#include "../battery_emulator/battery/Battery.h"
#include "../battery_emulator/inverter/INVERTERS.h"
#include "../battery_emulator/inverter/InverterProtocol.h"
#endif

#include <Arduino.h>
#include <Preferences.h>
#include <firmware_version.h>
#include <esp32common/espnow/packet_utils.h>
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

bool is_valid_interface_id(uint8_t id) {
    return id <= 5;
}

void load_interface_selection(uint8_t& battery_interface, uint8_t& inverter_interface) {
    battery_interface = 0;
    inverter_interface = 0;

    Preferences prefs;
    if (!prefs.begin("batterySettings", true)) {
        return;
    }

    battery_interface = static_cast<uint8_t>(prefs.getUInt("BATTCOMM", 0));
    inverter_interface = static_cast<uint8_t>(prefs.getUInt("INVCOMM", 0));
    prefs.end();
}

bool save_interface_selection(uint8_t battery_interface, uint8_t inverter_interface) {
    Preferences prefs;
    if (!prefs.begin("batterySettings", false)) {
        return false;
    }

    const size_t batt_written = prefs.putUInt("BATTCOMM", battery_interface);
    const size_t inv_written = prefs.putUInt("INVCOMM", inverter_interface);
    prefs.end();

    if (batt_written != sizeof(uint32_t) || inv_written != sizeof(uint32_t)) {
        return false;
    }

    uint8_t verify_batt = 0;
    uint8_t verify_inv = 0;
    load_interface_selection(verify_batt, verify_inv);
    return verify_batt == battery_interface && verify_inv == inverter_interface;
}

void populate_component_apply_ack_checksum(component_apply_ack_t& ack) {
    ack.checksum = 0;
    ack.checksum = EspnowPacketUtils::calculate_checksum(
        reinterpret_cast<const uint8_t*>(&ack),
        static_cast<uint16_t>(sizeof(ack) - sizeof(ack.checksum)));
}

void send_component_apply_ack(const uint8_t* target_mac, component_apply_ack_t& ack) {
    populate_component_apply_ack_checksum(ack);
    esp_err_t result = TxSendGuard::send_to_receiver_guarded(
        target_mac,
        reinterpret_cast<const uint8_t*>(&ack),
        sizeof(ack),
        "component_apply_ack"
    );

    if (result == ESP_OK) {
        LOG_INFO("COMP_APPLY", "ACK sent: request_id=%lu success=%u reboot_required=%u ready=%u persisted_mask=0x%02X",
                 static_cast<unsigned long>(ack.request_id),
                 static_cast<unsigned>(ack.success),
                 static_cast<unsigned>(ack.reboot_required),
                 static_cast<unsigned>(ack.ready_for_reboot),
                 static_cast<unsigned>(ack.persisted_mask));
    } else {
        LOG_ERROR("COMP_APPLY", "Failed to send ACK for request_id=%lu: %s",
                  static_cast<unsigned long>(ack.request_id),
                  esp_err_to_name(result));
    }
}

#if CONFIG_CAN_ENABLED
bool is_supported_battery_selection(uint8_t raw_type) {
    const BatteryType type = static_cast<BatteryType>(raw_type);
    const std::vector<BatteryType> supported_types = supported_battery_types();
    return std::find(supported_types.begin(), supported_types.end(), type) != supported_types.end();
}

bool is_supported_inverter_selection(uint8_t raw_type) {
    const InverterProtocolType type = static_cast<InverterProtocolType>(raw_type);
    const std::vector<InverterProtocolType> supported_types = supported_inverter_protocols();
    return std::find(supported_types.begin(), supported_types.end(), type) != supported_types.end();
}

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
    if (is_supported_battery_selection(config->battery_type)) {
#else
    if (true) {
#endif
        if (settings.get_battery_profile_type() != config->battery_type) {
            if (settings.set_battery_profile_type(config->battery_type)) {
                battery_updated = true;
            } else {
                LOG_WARN("COMP_CFG", "Rejected battery type during settings update: %u", config->battery_type);
            }
        }

#if CONFIG_CAN_ENABLED
        if (battery_updated || settings.get_battery_profile_type() == config->battery_type) {
            user_selected_battery_type = static_cast<BatteryType>(config->battery_type);
            if (!BatteryManager::instance().is_primary_battery_initialized()) {
                BatteryManager::instance().init_primary_battery(static_cast<BatteryType>(config->battery_type));
            } else {
                LOG_WARN("COMP_CFG", "Battery already initialized - change will apply on reboot");
            }
        }
#endif
    } else {
        LOG_WARN("COMP_CFG", "Unsupported battery type: %u", config->battery_type);
    }

#if CONFIG_CAN_ENABLED
    if (is_supported_inverter_selection(config->inverter_type)) {
#else
    if (true) {
#endif
        if (settings.get_inverter_type() != config->inverter_type) {
            if (settings.set_inverter_type(config->inverter_type)) {
                inverter_updated = true;
            } else {
                LOG_WARN("COMP_CFG", "Rejected inverter type during settings update: %u", config->inverter_type);
            }
        }

#if CONFIG_CAN_ENABLED
        if (inverter_updated || settings.get_inverter_type() == config->inverter_type) {
            user_selected_inverter_protocol = static_cast<InverterProtocolType>(config->inverter_type);
            if (!BatteryManager::instance().is_inverter_initialized()) {
                BatteryManager::instance().init_inverter(static_cast<InverterProtocolType>(config->inverter_type));
            } else {
                LOG_WARN("COMP_CFG", "Inverter already initialized - change will apply on reboot");
            }
        }
#endif
    } else {
        LOG_WARN("COMP_CFG", "Unsupported inverter type: %u", config->inverter_type);
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
        LOG_INFO("COMP_CFG", "Component selection saved; awaiting explicit reboot command to apply changes");
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

    LOG_INFO("COMP_IF", "Component interface selection saved; awaiting explicit reboot command to apply changes");
}

void handle_component_apply_request(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(component_apply_request_t)) {
        LOG_WARN("COMP_APPLY", "Invalid apply request size: %d", msg.len);
        return;
    }

    const component_apply_request_t* request = reinterpret_cast<const component_apply_request_t*>(msg.data);

    component_apply_ack_t ack{};
    ack.type = msg_component_apply_ack;
    ack.request_id = request->request_id;
    ack.apply_mask = request->apply_mask;
    ack.success = 0;
    ack.reboot_required = 0;
    ack.ready_for_reboot = 0;
    ack.persisted_mask = 0;
    ack.settings_version = static_cast<uint32_t>(SystemSettings::instance().get_config_version());
    strncpy(ack.message, "Apply failed", sizeof(ack.message) - 1);
    ack.message[sizeof(ack.message) - 1] = '\0';

    const uint16_t calculated = EspnowPacketUtils::calculate_checksum(
        reinterpret_cast<const uint8_t*>(request),
        static_cast<uint16_t>(sizeof(component_apply_request_t) - sizeof(request->checksum)));
    if (calculated != request->checksum) {
        strncpy(ack.message, "Checksum mismatch", sizeof(ack.message) - 1);
        load_interface_selection(ack.battery_interface, ack.inverter_interface);
        ack.battery_type = SystemSettings::instance().get_battery_profile_type();
        ack.inverter_type = SystemSettings::instance().get_inverter_type();
        send_component_apply_ack(msg.mac, ack);
        return;
    }

    SystemSettings& settings = SystemSettings::instance();

    uint8_t current_battery_interface = 0;
    uint8_t current_inverter_interface = 0;
    load_interface_selection(current_battery_interface, current_inverter_interface);

    ack.battery_type = settings.get_battery_profile_type();
    ack.inverter_type = settings.get_inverter_type();
    ack.battery_interface = current_battery_interface;
    ack.inverter_interface = current_inverter_interface;

    const bool wants_battery_type = (request->apply_mask & component_apply_battery_type) != 0;
    const bool wants_inverter_type = (request->apply_mask & component_apply_inverter_type) != 0;
    const bool wants_battery_interface = (request->apply_mask & component_apply_battery_interface) != 0;
    const bool wants_inverter_interface = (request->apply_mask & component_apply_inverter_interface) != 0;

    if (request->apply_mask == 0) {
        ack.success = 1;
        strncpy(ack.message, "No changes requested", sizeof(ack.message) - 1);
        send_component_apply_ack(msg.mac, ack);
        return;
    }

#if CONFIG_CAN_ENABLED
    if (wants_battery_type && !is_supported_battery_selection(request->battery_type)) {
        strncpy(ack.message, "Unsupported battery type", sizeof(ack.message) - 1);
        send_component_apply_ack(msg.mac, ack);
        return;
    }

    if (wants_inverter_type && !is_supported_inverter_selection(request->inverter_type)) {
        strncpy(ack.message, "Unsupported inverter type", sizeof(ack.message) - 1);
        send_component_apply_ack(msg.mac, ack);
        return;
    }
#endif

    if (wants_battery_interface && !is_valid_interface_id(request->battery_interface)) {
        strncpy(ack.message, "Invalid battery interface", sizeof(ack.message) - 1);
        send_component_apply_ack(msg.mac, ack);
        return;
    }

    if (wants_inverter_interface && !is_valid_interface_id(request->inverter_interface)) {
        strncpy(ack.message, "Invalid inverter interface", sizeof(ack.message) - 1);
        send_component_apply_ack(msg.mac, ack);
        return;
    }

    bool changed_any = false;
    bool battery_type_changed = false;
    bool inverter_type_changed = false;

    if (wants_battery_type) {
        if (settings.get_battery_profile_type() == request->battery_type) {
            ack.persisted_mask |= component_apply_battery_type;
        } else if (settings.set_battery_profile_type(request->battery_type)) {
            ack.persisted_mask |= component_apply_battery_type;
            changed_any = true;
            battery_type_changed = true;
            ack.battery_type = request->battery_type;
        } else {
            strncpy(ack.message, "Failed saving battery type", sizeof(ack.message) - 1);
            send_component_apply_ack(msg.mac, ack);
            return;
        }
    }

    if (wants_inverter_type) {
        if (settings.get_inverter_type() == request->inverter_type) {
            ack.persisted_mask |= component_apply_inverter_type;
        } else if (settings.set_inverter_type(request->inverter_type)) {
            ack.persisted_mask |= component_apply_inverter_type;
            changed_any = true;
            inverter_type_changed = true;
            ack.inverter_type = request->inverter_type;
        } else {
            strncpy(ack.message, "Failed saving inverter type", sizeof(ack.message) - 1);
            send_component_apply_ack(msg.mac, ack);
            return;
        }
    }

    const uint8_t target_battery_interface = wants_battery_interface ? request->battery_interface : current_battery_interface;
    const uint8_t target_inverter_interface = wants_inverter_interface ? request->inverter_interface : current_inverter_interface;

    if (wants_battery_interface || wants_inverter_interface) {
        const bool interface_changed =
            (target_battery_interface != current_battery_interface) ||
            (target_inverter_interface != current_inverter_interface);

        if (!interface_changed || save_interface_selection(target_battery_interface, target_inverter_interface)) {
            if (wants_battery_interface) {
                ack.persisted_mask |= component_apply_battery_interface;
            }
            if (wants_inverter_interface) {
                ack.persisted_mask |= component_apply_inverter_interface;
            }
            changed_any = changed_any || interface_changed;
            ack.battery_interface = target_battery_interface;
            ack.inverter_interface = target_inverter_interface;
        } else {
            strncpy(ack.message, "Failed saving interfaces", sizeof(ack.message) - 1);
            send_component_apply_ack(msg.mac, ack);
            return;
        }
    }

#if CONFIG_CAN_ENABLED
    if (wants_battery_type) {
        user_selected_battery_type = static_cast<BatteryType>(ack.battery_type);
        if (!BatteryManager::instance().is_primary_battery_initialized()) {
            BatteryManager::instance().init_primary_battery(static_cast<BatteryType>(ack.battery_type));
        }
    }

    if (wants_inverter_type) {
        user_selected_inverter_protocol = static_cast<InverterProtocolType>(ack.inverter_type);
        if (!BatteryManager::instance().is_inverter_initialized()) {
            BatteryManager::instance().init_inverter(static_cast<InverterProtocolType>(ack.inverter_type));
        }
    }
#endif

    if (battery_type_changed) {
        StaticData::update_battery_specs(ack.battery_type);
    }

    if (inverter_type_changed) {
        StaticData::update_inverter_specs(ack.inverter_type);
    }

    if ((battery_type_changed || inverter_type_changed) && MqttManager::instance().is_connected()) {
        if (battery_type_changed) {
            MqttManager::instance().publish_battery_specs();
            MqttManager::instance().publish_battery_type_catalog();
        }
        if (inverter_type_changed) {
            MqttManager::instance().publish_inverter_specs();
            MqttManager::instance().publish_inverter_type_catalog();
        }
        MqttManager::instance().publish_static_specs();
    }

    ack.success = (ack.persisted_mask == request->apply_mask) ? 1 : 0;
    ack.reboot_required = (changed_any && ack.success) ? 1 : 0;
    ack.ready_for_reboot = (ack.success && ack.reboot_required) ? 1 : 0;
    ack.settings_version = static_cast<uint32_t>(settings.get_config_version());

    if (ack.success) {
        if (ack.reboot_required) {
            strncpy(ack.message, "Persisted - reboot required", sizeof(ack.message) - 1);
        } else {
            strncpy(ack.message, "No reboot required", sizeof(ack.message) - 1);
        }
    } else {
        strncpy(ack.message, "Persisted mask mismatch", sizeof(ack.message) - 1);
    }

    send_component_apply_ack(msg.mac, ack);
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
    // Fixed transmitter hardware: only MCP2515 add-on CAN interface is valid.
    // Do not derive this list from legacy Battery Emulator HAL.
    {
        constexpr comm_interface kTxInterfaces[] = {
            comm_interface::CanAddonMcp2515,
        };

        for (const auto iface : kTxInterfaces) {
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
