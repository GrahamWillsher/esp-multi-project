#pragma once

#include <Arduino.h>
#include <esp32common/contracts/shared_contracts.h>

namespace TransmitterConfigService {

uint32_t get_config_version(config_section_t section);

mqtt_config_ack_t build_mqtt_config_ack();
network_config_ack_t build_network_config_ack();
battery_settings_full_msg_t build_battery_settings_full_msg();

}  // namespace TransmitterConfigService