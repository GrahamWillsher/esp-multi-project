#include "transmitter_manager.h"
#include "transmitter_battery_spec_sync.h"
#include "transmitter_connection_state_resolver.h"
#include "transmitter_event_log_cache.h"
#include "transmitter_mac_query_helper.h"
#include "transmitter_mac_registration.h"
#include "transmitter_mqtt_cache.h"
#include "transmitter_network_cache.h"
#include "transmitter_nvs_persistence.h"
#include "transmitter_runtime_status_update.h"
#include "transmitter_settings_cache.h"
#include "transmitter_spec_cache.h"
#include "transmitter_status_cache.h"
#include "transmitter_write_through.h"
#include "../logging.h"

void TransmitterManager::init() {
    TransmitterNvsPersistence::init();
}

void TransmitterManager::loadFromNVS() {
    TransmitterNvsPersistence::loadFromNVS();
}

void TransmitterManager::saveToNVS() {
    TransmitterWriteThrough::persist_to_nvs();
}

void TransmitterManager::registerMAC(const uint8_t* transmitter_mac) {
    TransmitterMacRegistration::register_mac(transmitter_mac);
}

const uint8_t* TransmitterManager::getMAC() {
    return TransmitterMacQueryHelper::get_active_mac();
}

bool TransmitterManager::isMACKnown() {
    return TransmitterMacQueryHelper::is_mac_known();
}

String TransmitterManager::getMACString() {
    return TransmitterMacQueryHelper::get_mac_string();
}

void TransmitterManager::storeIPData(const uint8_t* transmitter_ip,
                                     const uint8_t* transmitter_gateway,
                                     const uint8_t* transmitter_subnet,
                                     bool is_static,
                                     uint32_t config_version) {
    if (!TransmitterNetworkCache::store_ip_data(transmitter_ip, transmitter_gateway, transmitter_subnet,
                                                is_static, config_version)) {
        return;
    }

    TransmitterWriteThrough::notify_and_persist();
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
    if (!TransmitterNetworkCache::store_network_config(curr_ip, curr_gateway, curr_subnet,
                                                       stat_ip, stat_gateway, stat_subnet, stat_dns1, stat_dns2,
                                                       is_static, config_version)) {
        return;
    }

    TransmitterWriteThrough::notify_and_persist();
}

// Current network configuration (active - could be DHCP or Static)
const uint8_t* TransmitterManager::getIP() {
    return TransmitterNetworkCache::get_ip();
}

const uint8_t* TransmitterManager::getGateway() {
    return TransmitterNetworkCache::get_gateway();
}

const uint8_t* TransmitterManager::getSubnet() {
    return TransmitterNetworkCache::get_subnet();
}

// Saved static configuration (from transmitter NVS)
const uint8_t* TransmitterManager::getStaticIP() {
    return TransmitterNetworkCache::get_static_ip();
}

const uint8_t* TransmitterManager::getStaticGateway() {
    return TransmitterNetworkCache::get_static_gateway();
}

const uint8_t* TransmitterManager::getStaticSubnet() {
    return TransmitterNetworkCache::get_static_subnet();
}

const uint8_t* TransmitterManager::getStaticDNSPrimary() {
    return TransmitterNetworkCache::get_static_dns_primary();
}

const uint8_t* TransmitterManager::getStaticDNSSecondary() {
    return TransmitterNetworkCache::get_static_dns_secondary();
}

bool TransmitterManager::isIPKnown() {
    return TransmitterNetworkCache::is_ip_known();
}

bool TransmitterManager::isStaticIP() {
    return TransmitterNetworkCache::is_static_ip();
}

uint32_t TransmitterManager::getNetworkConfigVersion() {
    return TransmitterNetworkCache::get_network_config_version();
}

void TransmitterManager::updateNetworkMode(bool is_static, uint32_t version) {
    TransmitterNetworkCache::update_network_mode(is_static, version);
}

String TransmitterManager::getIPString() {
    return TransmitterNetworkCache::get_ip_string();
}

String TransmitterManager::getURL() {
    return TransmitterNetworkCache::get_url();
}

// V2: Legacy version tracking functions removed

// Metadata management
void TransmitterManager::storeMetadata(bool valid, const char* env, const char* device,
                                       uint8_t major, uint8_t minor, uint8_t patch,
                                       const char* build_date_str) {
    TransmitterStatusCache::store_metadata(valid, env, device, major, minor, patch, build_date_str);
    TransmitterWriteThrough::persist_to_nvs();
}

bool TransmitterManager::hasMetadata() {
    return TransmitterStatusCache::has_metadata();
}

bool TransmitterManager::isMetadataValid() {
    return TransmitterStatusCache::is_metadata_valid();
}

const char* TransmitterManager::getMetadataEnv() {
    return TransmitterStatusCache::get_metadata_env();
}

const char* TransmitterManager::getMetadataDevice() {
    return TransmitterStatusCache::get_metadata_device();
}

void TransmitterManager::getMetadataVersion(uint8_t& major, uint8_t& minor, uint8_t& patch) {
    TransmitterStatusCache::get_metadata_version(major, minor, patch);
}

uint32_t TransmitterManager::getMetadataVersionNumber() {
    return TransmitterStatusCache::get_metadata_version_number();
}

const char* TransmitterManager::getMetadataBuildDate() {
    return TransmitterStatusCache::get_metadata_build_date();
}

// ═══════════════════════════════════════════════════════════════════════
// BATTERY SETTINGS MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════

void TransmitterManager::storeBatterySettings(const BatterySettings& settings) {
    TransmitterSettingsCache::store_battery_settings(settings);

    LOG_INFO("[TX_MGR] Battery settings stored: %uWh, %uS, %umV-%umV",
             settings.capacity_wh, settings.cell_count,
             settings.min_voltage_mv, settings.max_voltage_mv);

    TransmitterWriteThrough::persist_to_nvs();
}

BatterySettings TransmitterManager::getBatterySettings() {
    return TransmitterSettingsCache::get_battery_settings();
}

bool TransmitterManager::hasBatterySettings() {
    return TransmitterSettingsCache::has_battery_settings();
}

void TransmitterManager::storeBatteryEmulatorSettings(const BatteryEmulatorSettings& settings) {
    TransmitterSettingsCache::store_battery_emulator_settings(settings);
    TransmitterWriteThrough::persist_to_nvs();
}

BatteryEmulatorSettings TransmitterManager::getBatteryEmulatorSettings() {
    return TransmitterSettingsCache::get_battery_emulator_settings();
}

bool TransmitterManager::hasBatteryEmulatorSettings() {
    return TransmitterSettingsCache::has_battery_emulator_settings();
}

void TransmitterManager::storePowerSettings(const PowerSettings& settings) {
    TransmitterSettingsCache::store_power_settings(settings);
    TransmitterWriteThrough::persist_to_nvs();
}

PowerSettings TransmitterManager::getPowerSettings() {
    return TransmitterSettingsCache::get_power_settings();
}

bool TransmitterManager::hasPowerSettings() {
    return TransmitterSettingsCache::has_power_settings();
}

void TransmitterManager::storeInverterSettings(const InverterSettings& settings) {
    TransmitterSettingsCache::store_inverter_settings(settings);
    TransmitterWriteThrough::persist_to_nvs();
}

InverterSettings TransmitterManager::getInverterSettings() {
    return TransmitterSettingsCache::get_inverter_settings();
}

bool TransmitterManager::hasInverterSettings() {
    return TransmitterSettingsCache::has_inverter_settings();
}

void TransmitterManager::storeCanSettings(const CanSettings& settings) {
    TransmitterSettingsCache::store_can_settings(settings);
    TransmitterWriteThrough::persist_to_nvs();
}

CanSettings TransmitterManager::getCanSettings() {
    return TransmitterSettingsCache::get_can_settings();
}

bool TransmitterManager::hasCanSettings() {
    return TransmitterSettingsCache::has_can_settings();
}

void TransmitterManager::storeContactorSettings(const ContactorSettings& settings) {
    TransmitterSettingsCache::store_contactor_settings(settings);
    TransmitterWriteThrough::persist_to_nvs();
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
    (void)connected;

    TransmitterMqttCache::store_config(enabled, server, port, username, password, client_id, version);

    const uint8_t* server_ip = TransmitterMqttCache::get_server();
    const uint8_t fallback[4] = {0, 0, 0, 0};
    if (server_ip == nullptr) {
        server_ip = fallback;
    }

    LOG_INFO("[TX_MGR] MQTT config stored: %s, %d.%d.%d.%d:%d, v%u",
             enabled ? "ENABLED" : "DISABLED",
             server_ip[0], server_ip[1], server_ip[2], server_ip[3], port,
             version);

    TransmitterWriteThrough::persist_to_nvs();
}

bool TransmitterManager::isMqttEnabled() {
    return TransmitterMqttCache::is_enabled();
}

const uint8_t* TransmitterManager::getMqttServer() {
    return TransmitterMqttCache::get_server();
}

uint16_t TransmitterManager::getMqttPort() {
    return TransmitterMqttCache::get_port();
}

const char* TransmitterManager::getMqttUsername() {
    return TransmitterMqttCache::get_username();
}

const char* TransmitterManager::getMqttPassword() {
    return TransmitterMqttCache::get_password();
}

const char* TransmitterManager::getMqttClientId() {
    return TransmitterMqttCache::get_client_id();
}

bool TransmitterManager::isMqttConnected() {
    return TransmitterMqttCache::is_connected();
}

bool TransmitterManager::isMqttConfigKnown() {
    return TransmitterMqttCache::is_config_known();
}

String TransmitterManager::getMqttServerString() {
    return TransmitterMqttCache::get_server_string();
}

// Phase 4: Version and runtime status tracking
uint32_t TransmitterManager::getMqttConfigVersion() {
    return TransmitterMqttCache::get_config_version();
}

void TransmitterManager::updateRuntimeStatus(bool mqtt_conn, bool eth_conn) {
    TransmitterRuntimeStatusUpdate::update_runtime_status(mqtt_conn, eth_conn);
}

bool TransmitterManager::isEthernetConnected() {
    return TransmitterStatusCache::is_ethernet_connected();
}

unsigned long TransmitterManager::getLastBeaconTime() {
    return TransmitterStatusCache::get_last_beacon_time();
}

// Phase 4: Get transmitter time and uptime data
uint64_t TransmitterManager::getUptimeMs() {
    return TransmitterStatusCache::get_uptime_ms();
}

uint64_t TransmitterManager::getUnixTime() {
    return TransmitterStatusCache::get_unix_time();
}

uint8_t TransmitterManager::getTimeSource() {
    return TransmitterStatusCache::get_time_source();
}

// Phase 4: Update time/uptime data from heartbeat
void TransmitterManager::updateTimeData(uint64_t new_uptime_ms, uint64_t new_unix_time, uint8_t new_time_source) {
    TransmitterStatusCache::update_time_data(new_uptime_ms, new_unix_time, new_time_source);
}

void TransmitterManager::updateSendStatus(bool success) {
    TransmitterStatusCache::update_send_status(success);
}

bool TransmitterManager::wasLastSendSuccessful() {
    return TransmitterStatusCache::was_last_send_successful();
}

bool TransmitterManager::isTransmitterConnected() {
    return TransmitterConnectionStateResolver::is_transmitter_connected();
}

// Phase 3: Static spec data storage (battery emulator specs via MQTT)
void TransmitterManager::storeStaticSpecs(const JsonObject& specs) {
    TransmitterSpecCache::store_static_specs(specs);
}

void TransmitterManager::storeBatterySpecs(const JsonObject& specs) {
    TransmitterBatterySpecSync::store_battery_specs(specs);
}

void TransmitterManager::storeInverterSpecs(const JsonObject& specs) {
    TransmitterSpecCache::store_inverter_specs(specs);
}

void TransmitterManager::storeChargerSpecs(const JsonObject& specs) {
    TransmitterSpecCache::store_charger_specs(specs);
}

void TransmitterManager::storeSystemSpecs(const JsonObject& specs) {
    TransmitterSpecCache::store_system_specs(specs);
}

bool TransmitterManager::hasStaticSpecs() {
    return TransmitterSpecCache::has_static_specs();
}

String TransmitterManager::getStaticSpecsJson() {
    return TransmitterSpecCache::get_static_specs_json();
}

String TransmitterManager::getBatterySpecsJson() {
    return TransmitterSpecCache::get_battery_specs_json();
}

String TransmitterManager::getInverterSpecsJson() {
    return TransmitterSpecCache::get_inverter_specs_json();
}

String TransmitterManager::getChargerSpecsJson() {
    return TransmitterSpecCache::get_charger_specs_json();
}

String TransmitterManager::getSystemSpecsJson() {
    return TransmitterSpecCache::get_system_specs_json();
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
