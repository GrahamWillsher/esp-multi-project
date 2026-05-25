#include "transmitter_config_service.h"

#include "../network/ethernet_manager.h"
#include "../network/mqtt_manager.h"
#include "../network/mqtt_task.h"
#include "../settings/settings_manager.h"

#include "mqtt_config_manager.h"
#include <firmware_version.h>
#include <runtime_common_utils/crc_utils.h>

namespace TransmitterConfigService {

uint32_t get_config_version(config_section_t section) {
    switch (section) {
        case config_section_mqtt:
            return MqttConfigManager::getConfigVersion();
        case config_section_network:
            return EthernetManager::instance().get_network_config_version();
        case config_section_battery:
            return SettingsManager::instance().get_battery_settings_version();
        case config_section_power_profile:
            return SettingsManager::instance().get_power_settings_version();
        case config_section_metadata:
            return FW_VERSION_NUMBER;
        default:
            return 0;
    }
}

mqtt_config_ack_t build_mqtt_config_ack() {
    mqtt_config_ack_t mqtt_msg{};
    mqtt_msg.type = msg_mqtt_config_ack;
    mqtt_msg.success = 1;
    mqtt_msg.enabled = MqttConfigManager::isEnabled() ? 1 : 0;

    IPAddress mqtt_server = MqttConfigManager::getServer();
    mqtt_msg.server[0] = mqtt_server[0];
    mqtt_msg.server[1] = mqtt_server[1];
    mqtt_msg.server[2] = mqtt_server[2];
    mqtt_msg.server[3] = mqtt_server[3];

    mqtt_msg.port = MqttConfigManager::getPort();
    strncpy(mqtt_msg.username, MqttConfigManager::getUsername(), sizeof(mqtt_msg.username) - 1);
    strncpy(mqtt_msg.password, MqttConfigManager::getPassword(), sizeof(mqtt_msg.password) - 1);
    strncpy(mqtt_msg.client_id, MqttConfigManager::getClientId(), sizeof(mqtt_msg.client_id) - 1);
    mqtt_msg.username[sizeof(mqtt_msg.username) - 1] = '\0';
    mqtt_msg.password[sizeof(mqtt_msg.password) - 1] = '\0';
    mqtt_msg.client_id[sizeof(mqtt_msg.client_id) - 1] = '\0';

    mqtt_msg.connected = MqttTask::instance().is_connected() ? 1 : 0;
    mqtt_msg.config_version = MqttConfigManager::getConfigVersion();
    strncpy(mqtt_msg.message, "Config sent in response to version mismatch", sizeof(mqtt_msg.message) - 1);
    mqtt_msg.message[sizeof(mqtt_msg.message) - 1] = '\0';
    mqtt_msg.checksum = RuntimeCrcUtils::calculate_message_crc32_zeroed(&mqtt_msg);
    return mqtt_msg;
}

network_config_ack_t build_network_config_ack() {
    network_config_ack_t net_msg{};
    net_msg.type = msg_network_config_ack;
    net_msg.success = 1;

    IPAddress current_ip = EthernetManager::instance().get_local_ip();
    IPAddress current_gateway = EthernetManager::instance().get_gateway_ip();
    IPAddress current_subnet = EthernetManager::instance().get_subnet_mask();

    net_msg.current_ip[0] = current_ip[0];
    net_msg.current_ip[1] = current_ip[1];
    net_msg.current_ip[2] = current_ip[2];
    net_msg.current_ip[3] = current_ip[3];
    net_msg.current_gateway[0] = current_gateway[0];
    net_msg.current_gateway[1] = current_gateway[1];
    net_msg.current_gateway[2] = current_gateway[2];
    net_msg.current_gateway[3] = current_gateway[3];
    net_msg.current_subnet[0] = current_subnet[0];
    net_msg.current_subnet[1] = current_subnet[1];
    net_msg.current_subnet[2] = current_subnet[2];
    net_msg.current_subnet[3] = current_subnet[3];

    IPAddress static_ip = EthernetManager::instance().get_static_ip();
    IPAddress static_gateway = EthernetManager::instance().get_static_gateway();
    IPAddress static_subnet = EthernetManager::instance().get_static_subnet_mask();
    IPAddress static_dns1 = EthernetManager::instance().get_static_dns_primary();
    IPAddress static_dns2 = EthernetManager::instance().get_static_dns_secondary();

    net_msg.static_ip[0] = static_ip[0];
    net_msg.static_ip[1] = static_ip[1];
    net_msg.static_ip[2] = static_ip[2];
    net_msg.static_ip[3] = static_ip[3];
    net_msg.static_gateway[0] = static_gateway[0];
    net_msg.static_gateway[1] = static_gateway[1];
    net_msg.static_gateway[2] = static_gateway[2];
    net_msg.static_gateway[3] = static_gateway[3];
    net_msg.static_subnet[0] = static_subnet[0];
    net_msg.static_subnet[1] = static_subnet[1];
    net_msg.static_subnet[2] = static_subnet[2];
    net_msg.static_subnet[3] = static_subnet[3];
    net_msg.static_dns_primary[0] = static_dns1[0];
    net_msg.static_dns_primary[1] = static_dns1[1];
    net_msg.static_dns_primary[2] = static_dns1[2];
    net_msg.static_dns_primary[3] = static_dns1[3];
    net_msg.static_dns_secondary[0] = static_dns2[0];
    net_msg.static_dns_secondary[1] = static_dns2[1];
    net_msg.static_dns_secondary[2] = static_dns2[2];
    net_msg.static_dns_secondary[3] = static_dns2[3];

    net_msg.use_static_ip = EthernetManager::instance().is_static_ip() ? 1 : 0;
    net_msg.config_version = EthernetManager::instance().get_network_config_version();
    strncpy(net_msg.message, "Config sent in response to version mismatch", sizeof(net_msg.message) - 1);
    net_msg.message[sizeof(net_msg.message) - 1] = '\0';
    return net_msg;
}

battery_settings_full_msg_t build_battery_settings_full_msg() {
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
    settings_msg.checksum = RuntimeCrcUtils::calculate_message_crc32_zeroed(&settings_msg);
    return settings_msg;
}

}  // namespace TransmitterConfigService