#include "config_manager.h"
#include <string.h>

ConfigManager::ConfigManager() {
    // Initialize with default values (constructors handle this)
    updateChecksum();
}

void ConfigManager::setFullConfig(const FullConfigSnapshot& config) {
    config_ = config;
}

bool ConfigManager::updateField(ConfigSection section, uint8_t field_id, 
                                const void* value, uint8_t value_length) {
    if (!value) return false;
    
    bool success = false;
    
    switch (section) {
        case CONFIG_MQTT:
            success = updateMqttField(field_id, value, value_length);
            break;
        case CONFIG_NETWORK:
            success = updateNetworkField(field_id, value, value_length);
            break;
        case CONFIG_BATTERY:
            success = updateBatteryField(field_id, value, value_length);
            break;
        case CONFIG_POWER:
            success = updatePowerField(field_id, value, value_length);
            break;
        case CONFIG_INVERTER:
            success = updateInverterField(field_id, value, value_length);
            break;
        case CONFIG_CAN:
            success = updateCanField(field_id, value, value_length);
            break;
        case CONFIG_CONTACTOR:
            success = updateContactorField(field_id, value, value_length);
            break;
        case CONFIG_SYSTEM:
            success = updateSystemField(field_id, value, value_length);
            break;
        default:
            return false;
    }
    
    if (success) {
        incrementGlobalVersion();
        incrementSectionVersion(section);
        updateChecksum();
    }
    
    return success;
}

uint16_t ConfigManager::getSectionVersion(ConfigSection section) const {
    uint8_t index = static_cast<uint8_t>(section) - 1;
    if (index >= 8) return 0;
    return config_.version.section_versions[index];
}

void ConfigManager::incrementGlobalVersion() {
    config_.version.global_version++;
}

void ConfigManager::incrementSectionVersion(ConfigSection section) {
    uint8_t index = static_cast<uint8_t>(section) - 1;
    if (index < 8) {
        config_.version.section_versions[index]++;
    }
}

void ConfigManager::updateChecksum() {
    config_.checksum = calculateCRC32((uint8_t*)&config_, 
                                     sizeof(FullConfigSnapshot) - sizeof(uint32_t));
}

bool ConfigManager::validateChecksum() const {
    uint32_t calculated = calculateCRC32((uint8_t*)&config_, 
                                        sizeof(FullConfigSnapshot) - sizeof(uint32_t));
    return calculated == config_.checksum;
}

// MQTT field updates
bool ConfigManager::updateMqttField(uint8_t field_id, const void* value, uint8_t value_length) {
    switch (field_id) {
        case MQTT_SERVER:
            if (value_length <= sizeof(config_.mqtt.server)) {
                memcpy(config_.mqtt.server, value, value_length);
                return true;
            }
            break;
        case MQTT_PORT:
            if (value_length == sizeof(uint16_t)) {
                config_.mqtt.port = *((uint16_t*)value);
                return true;
            }
            break;
        case MQTT_USERNAME:
            if (value_length <= sizeof(config_.mqtt.username)) {
                memcpy(config_.mqtt.username, value, value_length);
                return true;
            }
            break;
        case MQTT_PASSWORD:
            if (value_length <= sizeof(config_.mqtt.password)) {
                memcpy(config_.mqtt.password, value, value_length);
                return true;
            }
            break;
        case MQTT_CLIENT_ID:
            if (value_length <= sizeof(config_.mqtt.client_id)) {
                memcpy(config_.mqtt.client_id, value, value_length);
                return true;
            }
            break;
        case MQTT_TOPIC_PREFIX:
            if (value_length <= sizeof(config_.mqtt.topic_prefix)) {
                memcpy(config_.mqtt.topic_prefix, value, value_length);
                return true;
            }
            break;
        case MQTT_ENABLED:
            if (value_length == sizeof(bool)) {
                config_.mqtt.enabled = *((bool*)value);
                return true;
            }
            break;
        case MQTT_TIMEOUT:
            if (value_length == sizeof(uint16_t)) {
                config_.mqtt.timeout_ms = *((uint16_t*)value);
                return true;
            }
            break;
    }
    return false;
}

// Network field updates
bool ConfigManager::updateNetworkField(uint8_t field_id, const void* value, uint8_t value_length) {
    switch (field_id) {
        case NET_USE_STATIC:
            if (value_length == sizeof(bool)) {
                config_.network.use_static_ip = *((bool*)value);
                return true;
            }
            break;
        case NET_IP_ADDRESS:
            if (value_length == 4) {
                memcpy(config_.network.ip, value, 4);
                return true;
            }
            break;
        case NET_GATEWAY:
            if (value_length == 4) {
                memcpy(config_.network.gateway, value, 4);
                return true;
            }
            break;
        case NET_SUBNET:
            if (value_length == 4) {
                memcpy(config_.network.subnet, value, 4);
                return true;
            }
            break;
        case NET_DNS:
            if (value_length == 4) {
                memcpy(config_.network.dns, value, 4);
                return true;
            }
            break;
        case NET_HOSTNAME:
            if (value_length <= sizeof(config_.network.hostname)) {
                memcpy(config_.network.hostname, value, value_length);
                return true;
            }
            break;
    }
    return false;
}

// Battery field updates
bool ConfigManager::updateBatteryField(uint8_t field_id, const void* value, uint8_t value_length) {
    switch (field_id) {
        case BATT_PACK_V_MAX:
            if (value_length == sizeof(uint16_t)) {
                config_.battery.pack_voltage_max = *((uint16_t*)value);
                return true;
            }
            break;
        case BATT_PACK_V_MIN:
            if (value_length == sizeof(uint16_t)) {
                config_.battery.pack_voltage_min = *((uint16_t*)value);
                return true;
            }
            break;
        case BATT_CELL_V_MAX:
            if (value_length == sizeof(uint16_t)) {
                config_.battery.cell_voltage_max = *((uint16_t*)value);
                return true;
            }
            break;
        case BATT_CELL_V_MIN:
            if (value_length == sizeof(uint16_t)) {
                config_.battery.cell_voltage_min = *((uint16_t*)value);
                return true;
            }
            break;
        case BATT_DOUBLE:
            if (value_length == sizeof(bool)) {
                config_.battery.double_battery = *((bool*)value);
                return true;
            }
            break;
        case BATT_USE_EST_SOC:
            if (value_length == sizeof(bool)) {
                config_.battery.use_estimated_soc = *((bool*)value);
                return true;
            }
            break;
        case BATT_CHEMISTRY:
            if (value_length == sizeof(uint8_t)) {
                config_.battery.chemistry = *((uint8_t*)value);
                return true;
            }
            break;
    }
    return false;
}

// Power field updates
bool ConfigManager::updatePowerField(uint8_t field_id, const void* value, uint8_t value_length) {
    switch (field_id) {
        case POWER_CHARGE_W:
            if (value_length == sizeof(uint16_t)) {
                config_.power.charge_power_w = *((uint16_t*)value);
                return true;
            }
            break;
        case POWER_DISCHARGE_W:
            if (value_length == sizeof(uint16_t)) {
                config_.power.discharge_power_w = *((uint16_t*)value);
                return true;
            }
            break;
        case POWER_MAX_PRECHARGE_MS:
            if (value_length == sizeof(uint16_t)) {
                config_.power.max_precharge_ms = *((uint16_t*)value);
                return true;
            }
            break;
        case POWER_PRECHARGE_DUR_MS:
            if (value_length == sizeof(uint16_t)) {
                config_.power.precharge_duration_ms = *((uint16_t*)value);
                return true;
            }
            break;
    }
    return false;
}

// Inverter field updates
bool ConfigManager::updateInverterField(uint8_t field_id, const void* value, uint8_t value_length) {
    switch (field_id) {
        case INV_TOTAL_CELLS:
            if (value_length == sizeof(uint8_t)) {
                config_.inverter.total_cells = *((uint8_t*)value);
                return true;
            }
            break;
        case INV_MODULES:
            if (value_length == sizeof(uint8_t)) {
                config_.inverter.modules = *((uint8_t*)value);
                return true;
            }
            break;
        case INV_CELLS_PER_MODULE:
            if (value_length == sizeof(uint8_t)) {
                config_.inverter.cells_per_module = *((uint8_t*)value);
                return true;
            }
            break;
        case INV_VOLTAGE_LEVEL:
            if (value_length == sizeof(uint16_t)) {
                config_.inverter.voltage_level = *((uint16_t*)value);
                return true;
            }
            break;
        case INV_CAPACITY_AH:
            if (value_length == sizeof(uint16_t)) {
                config_.inverter.capacity_ah = *((uint16_t*)value);
                return true;
            }
            break;
        case INV_BATTERY_TYPE:
            if (value_length == sizeof(uint8_t)) {
                config_.inverter.battery_type = *((uint8_t*)value);
                return true;
            }
            break;
    }
    return false;
}

// CAN field updates
bool ConfigManager::updateCanField(uint8_t field_id, const void* value, uint8_t value_length) {
    switch (field_id) {
        case CAN_FREQUENCY_KHZ:
            if (value_length == sizeof(uint16_t)) {
                config_.can.frequency_khz = *((uint16_t*)value);
                return true;
            }
            break;
        case CAN_FD_FREQ_MHZ:
            if (value_length == sizeof(uint16_t)) {
                config_.can.fd_frequency_mhz = *((uint16_t*)value);
                return true;
            }
            break;
        case CAN_SOFAR_ID:
            if (value_length == sizeof(uint16_t)) {
                config_.can.sofar_id = *((uint16_t*)value);
                return true;
            }
            break;
        case CAN_PYLON_INTERVAL:
            if (value_length == sizeof(uint16_t)) {
                config_.can.pylon_send_interval = *((uint16_t*)value);
                return true;
            }
            break;
    }
    return false;
}

// Contactor field updates
bool ConfigManager::updateContactorField(uint8_t field_id, const void* value, uint8_t value_length) {
    switch (field_id) {
        case CONT_CONTROL_EN:
            if (value_length == sizeof(bool)) {
                config_.contactor.control_enabled = *((bool*)value);
                return true;
            }
            break;
        case CONT_NC_MODE:
            if (value_length == sizeof(bool)) {
                config_.contactor.nc_contactor = *((bool*)value);
                return true;
            }
            break;
        case CONT_PWM_FREQ:
            if (value_length == sizeof(uint16_t)) {
                config_.contactor.pwm_frequency = *((uint16_t*)value);
                return true;
            }
            break;
    }
    return false;
}

// System field updates
bool ConfigManager::updateSystemField(uint8_t field_id, const void* value, uint8_t value_length) {
    switch (field_id) {
        case SYS_LED_MODE:
            if (value_length == sizeof(uint8_t)) {
                config_.system.led_mode = *((uint8_t*)value);
                return true;
            }
            break;
        case SYS_WEB_ENABLED:
            if (value_length == sizeof(bool)) {
                config_.system.web_enabled = *((bool*)value);
                return true;
            }
            break;
        case SYS_LOG_LEVEL:
            if (value_length == sizeof(uint16_t)) {
                config_.system.log_level = *((uint16_t*)value);
                return true;
            }
            break;
    }
    return false;
}
