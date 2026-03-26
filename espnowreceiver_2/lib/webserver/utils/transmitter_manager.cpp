#include "transmitter_manager.h"
#include "transmitter_event_log_cache.h"
#include "transmitter_identity.h"
#include "transmitter_mqtt_specs.h"
#include "transmitter_network.h"
#include "transmitter_nvs_persistence.h"
#include "transmitter_settings_cache.h"
#include "transmitter_state.h"
#include "../logging.h"

void TransmitterManager::init() {
    TransmitterNvsPersistence::init();
}

void TransmitterManager::loadFromNVS() {
    TransmitterNvsPersistence::loadFromNVS();
}

void TransmitterManager::saveToNVS() {
    TransmitterNvsPersistence::persist();
}

void TransmitterManager::registerMAC(const uint8_t* transmitter_mac) {
    TransmitterIdentity::register_mac(transmitter_mac);
}

const uint8_t* TransmitterManager::getMAC() {
    return TransmitterIdentity::get_active_mac();
}

bool TransmitterManager::isMACKnown() {
    return TransmitterIdentity::is_mac_known();
}

String TransmitterManager::getMACString() {
    return TransmitterIdentity::get_mac_string();
}

void TransmitterManager::storeIPData(const uint8_t* transmitter_ip,
                                     const uint8_t* transmitter_gateway,
                                     const uint8_t* transmitter_subnet,
                                     bool is_static,
                                     uint32_t config_version) {
    (void)TransmitterNetwork::store_ip_data(transmitter_ip, transmitter_gateway, transmitter_subnet,
                                            is_static, config_version, true);
}

// Store complete network configuration (current + static)
void TransmitterManager::storeNetworkConfig(const uint8_t* curr_ip, 
                                           const uint8_t* curr_gateway,
                                           const uint8_t* curr_subnet,
                                           const uint8_t* stat_ip,
                                           const uint8_t* stat_gateway,
                                           const uint8_t* stat_subnet,
                                           const uint8_t* stat_dns1,
                                           const uint8_t* stat_dns2,
                                           bool is_static,
                                           uint32_t config_version) {
    (void)TransmitterNetwork::store_network_config(curr_ip, curr_gateway, curr_subnet,
                                                   stat_ip, stat_gateway, stat_subnet, stat_dns1, stat_dns2,
                                                   is_static, config_version, true);
}

// Current network configuration (active - could be DHCP or Static)
const uint8_t* TransmitterManager::getIP() {
    return TransmitterNetwork::get_ip();
}

const uint8_t* TransmitterManager::getGateway() {
    return TransmitterNetwork::get_gateway();
}

const uint8_t* TransmitterManager::getSubnet() {
    return TransmitterNetwork::get_subnet();
}

// Saved static configuration (from transmitter NVS)
const uint8_t* TransmitterManager::getStaticIP() {
    return TransmitterNetwork::get_static_ip();
}

const uint8_t* TransmitterManager::getStaticGateway() {
    return TransmitterNetwork::get_static_gateway();
}

const uint8_t* TransmitterManager::getStaticSubnet() {
    return TransmitterNetwork::get_static_subnet();
}

const uint8_t* TransmitterManager::getStaticDNSPrimary() {
    return TransmitterNetwork::get_static_dns_primary();
}

const uint8_t* TransmitterManager::getStaticDNSSecondary() {
    return TransmitterNetwork::get_static_dns_secondary();
}

bool TransmitterManager::isIPKnown() {
    return TransmitterNetwork::is_ip_known();
}

bool TransmitterManager::isStaticIP() {
    return TransmitterNetwork::is_static_ip();
}

uint32_t TransmitterManager::getNetworkConfigVersion() {
    return TransmitterNetwork::get_network_config_version();
}

void TransmitterManager::updateNetworkMode(bool is_static, uint32_t version) {
    TransmitterNetwork::update_network_mode(is_static, version);
}

String TransmitterManager::getIPString() {
    return TransmitterNetwork::get_ip_string();
}

String TransmitterManager::getURL() {
    return TransmitterNetwork::get_url();
}

// V2: Legacy version tracking functions removed

// Metadata management
void TransmitterManager::storeMetadata(bool valid, const char* env, const char* device,
                                       uint8_t major, uint8_t minor, uint8_t patch,
                                       const char* build_date_str) {
    TransmitterState::store_metadata(valid, env, device, major, minor, patch, build_date_str);
    TransmitterNvsPersistence::persist();
}

bool TransmitterManager::hasMetadata() {
    return TransmitterState::has_metadata();
}

bool TransmitterManager::isMetadataValid() {
    return TransmitterState::is_metadata_valid();
}

const char* TransmitterManager::getMetadataEnv() {
    return TransmitterState::get_metadata_env();
}

const char* TransmitterManager::getMetadataDevice() {
    return TransmitterState::get_metadata_device();
}

void TransmitterManager::getMetadataVersion(uint8_t& major, uint8_t& minor, uint8_t& patch) {
    TransmitterState::get_metadata_version(major, minor, patch);
}

uint32_t TransmitterManager::getMetadataVersionNumber() {
    return TransmitterState::get_metadata_version_number();
}

const char* TransmitterManager::getMetadataBuildDate() {
    return TransmitterState::get_metadata_build_date();
}

// ═══════════════════════════════════════════════════════════════════════
// BATTERY SETTINGS MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════

void TransmitterManager::storeBatterySettings(const BatterySettings& settings) {
    TransmitterSettingsCache::store_battery_settings(settings);

    LOG_INFO("TX_MGR", "Battery settings stored: %uWh, %uS, %umV-%umV",
             settings.capacity_wh, settings.cell_count,
             settings.min_voltage_mv, settings.max_voltage_mv);

    TransmitterNvsPersistence::persist();
}

BatterySettings TransmitterManager::getBatterySettings() {
    return TransmitterSettingsCache::get_battery_settings();
}

bool TransmitterManager::hasBatterySettings() {
    return TransmitterSettingsCache::has_battery_settings();
}

void TransmitterManager::storeBatteryEmulatorSettings(const BatteryEmulatorSettings& settings) {
    TransmitterSettingsCache::store_battery_emulator_settings(settings);
    TransmitterNvsPersistence::persist();
}

BatteryEmulatorSettings TransmitterManager::getBatteryEmulatorSettings() {
    return TransmitterSettingsCache::get_battery_emulator_settings();
}

bool TransmitterManager::hasBatteryEmulatorSettings() {
    return TransmitterSettingsCache::has_battery_emulator_settings();
}

void TransmitterManager::storePowerSettings(const PowerSettings& settings) {
    TransmitterSettingsCache::store_power_settings(settings);
    TransmitterNvsPersistence::persist();
}

PowerSettings TransmitterManager::getPowerSettings() {
    return TransmitterSettingsCache::get_power_settings();
}

bool TransmitterManager::hasPowerSettings() {
    return TransmitterSettingsCache::has_power_settings();
}

void TransmitterManager::storeInverterSettings(const InverterSettings& settings) {
    TransmitterSettingsCache::store_inverter_settings(settings);
    TransmitterNvsPersistence::persist();
}

InverterSettings TransmitterManager::getInverterSettings() {
    return TransmitterSettingsCache::get_inverter_settings();
}

bool TransmitterManager::hasInverterSettings() {
    return TransmitterSettingsCache::has_inverter_settings();
}

void TransmitterManager::storeCanSettings(const CanSettings& settings) {
    TransmitterSettingsCache::store_can_settings(settings);
    TransmitterNvsPersistence::persist();
}

CanSettings TransmitterManager::getCanSettings() {
    return TransmitterSettingsCache::get_can_settings();
}

bool TransmitterManager::hasCanSettings() {
    return TransmitterSettingsCache::has_can_settings();
}

void TransmitterManager::storeContactorSettings(const ContactorSettings& settings) {
    TransmitterSettingsCache::store_contactor_settings(settings);
    TransmitterNvsPersistence::persist();
}

ContactorSettings TransmitterManager::getContactorSettings() {
    return TransmitterSettingsCache::get_contactor_settings();
}

bool TransmitterManager::hasContactorSettings() {
    return TransmitterSettingsCache::has_contactor_settings();
}

// ═══════════════════════════════════════════════════════════════════════
// MQTT CONFIGURATION MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════

void TransmitterManager::storeMqttConfig(bool enabled, const uint8_t* server, uint16_t port,
                                        const char* username, const char* password,
                                        const char* client_id, bool connected, uint32_t version) {
    TransmitterMqttSpecs::store_mqtt_config(enabled, server, port, username, password,
                                            client_id, connected, version, true);
}

bool TransmitterManager::isMqttEnabled() {
    return TransmitterMqttSpecs::is_enabled();
}

const uint8_t* TransmitterManager::getMqttServer() {
    return TransmitterMqttSpecs::get_server();
}

uint16_t TransmitterManager::getMqttPort() {
    return TransmitterMqttSpecs::get_port();
}

const char* TransmitterManager::getMqttUsername() {
    return TransmitterMqttSpecs::get_username();
}

const char* TransmitterManager::getMqttPassword() {
    return TransmitterMqttSpecs::get_password();
}

const char* TransmitterManager::getMqttClientId() {
    return TransmitterMqttSpecs::get_client_id();
}

bool TransmitterManager::isMqttConnected() {
    return TransmitterMqttSpecs::is_connected();
}

bool TransmitterManager::isMqttConfigKnown() {
    return TransmitterMqttSpecs::is_config_known();
}

String TransmitterManager::getMqttServerString() {
    return TransmitterMqttSpecs::get_server_string();
}

// Phase 4: Version and runtime status tracking
uint32_t TransmitterManager::getMqttConfigVersion() {
    return TransmitterMqttSpecs::get_config_version();
}

void TransmitterManager::updateRuntimeStatus(bool mqtt_conn, bool eth_conn) {
    TransmitterState::update_runtime_status(mqtt_conn, eth_conn);
}

bool TransmitterManager::isEthernetConnected() {
    return TransmitterState::is_ethernet_connected();
}

unsigned long TransmitterManager::getLastBeaconTime() {
    return TransmitterState::get_last_beacon_time();
}

// Phase 4: Get transmitter time and uptime data
uint64_t TransmitterManager::getUptimeMs() {
    return TransmitterState::get_uptime_ms();
}

uint64_t TransmitterManager::getUnixTime() {
    return TransmitterState::get_unix_time();
}

uint8_t TransmitterManager::getTimeSource() {
    return TransmitterState::get_time_source();
}

// Phase 4: Update time/uptime data from heartbeat
void TransmitterManager::updateTimeData(uint64_t new_uptime_ms, uint64_t new_unix_time, uint8_t new_time_source) {
    TransmitterState::update_time_data(new_uptime_ms, new_unix_time, new_time_source);
}

void TransmitterManager::updateSendStatus(bool success) {
    TransmitterState::update_send_status(success);
}

bool TransmitterManager::wasLastSendSuccessful() {
    return TransmitterState::was_last_send_successful();
}

bool TransmitterManager::isTransmitterConnected() {
    return TransmitterState::is_transmitter_connected();
}

// Phase 3: Static spec data storage (battery emulator specs via MQTT)
void TransmitterManager::storeStaticSpecs(const JsonObject& specs) {
    TransmitterMqttSpecs::store_static_specs(specs);
}

void TransmitterManager::storeBatterySpecs(const JsonObject& specs) {
    TransmitterMqttSpecs::store_battery_specs(specs);
}

void TransmitterManager::storeInverterSpecs(const JsonObject& specs) {
    TransmitterMqttSpecs::store_inverter_specs(specs);
}

void TransmitterManager::storeChargerSpecs(const JsonObject& specs) {
    TransmitterMqttSpecs::store_charger_specs(specs);
}

void TransmitterManager::storeSystemSpecs(const JsonObject& specs) {
    TransmitterMqttSpecs::store_system_specs(specs);
}

bool TransmitterManager::hasStaticSpecs() {
    return TransmitterMqttSpecs::has_static_specs();
}

String TransmitterManager::getStaticSpecsJson() {
    return TransmitterMqttSpecs::get_static_specs_json();
}

String TransmitterManager::getBatterySpecsJson() {
    return TransmitterMqttSpecs::get_battery_specs_json();
}

String TransmitterManager::getInverterSpecsJson() {
    return TransmitterMqttSpecs::get_inverter_specs_json();
}

String TransmitterManager::getChargerSpecsJson() {
    return TransmitterMqttSpecs::get_charger_specs_json();
}

String TransmitterManager::getSystemSpecsJson() {
    return TransmitterMqttSpecs::get_system_specs_json();
}

void TransmitterManager::storeEventLogs(const JsonObject& logs) {
    TransmitterEventLogCache::store_event_logs(logs);
}

bool TransmitterManager::hasEventLogs() {
    return TransmitterEventLogCache::has_event_logs();
}

void TransmitterManager::getEventLogsSnapshot(std::vector<EventLogEntry>& out_logs, uint32_t* out_last_update_ms) {
    TransmitterEventLogCache::get_event_logs_snapshot(out_logs, out_last_update_ms);
}

uint32_t TransmitterManager::getEventLogCount() {
    return TransmitterEventLogCache::get_event_log_count();
}

uint32_t TransmitterManager::getEventLogsLastUpdateMs() {
    return TransmitterEventLogCache::get_event_logs_last_update_ms();
}
