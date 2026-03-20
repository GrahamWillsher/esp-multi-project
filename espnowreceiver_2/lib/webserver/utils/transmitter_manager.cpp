#include "transmitter_manager.h"
#include "transmitter_connection_state_resolver.h"
#include "transmitter_mac_query_helper.h"
#include "transmitter_mac_registration.h"
#include "transmitter_metadata_store_workflow.h"
#include "transmitter_mqtt_config_workflow.h"
#include "transmitter_network_store_workflow.h"
#include "transmitter_nvs_persistence.h"
#include "transmitter_runtime_status_update.h"
#include "transmitter_settings_store_workflow.h"
#include "transmitter_time_status_update_workflow.h"
#include "transmitter_write_through.h"
#include "transmitter_status_query_helper.h"
#include "transmitter_spec_storage_workflow.h"
#include "transmitter_event_logs_workflow.h"
#include "transmitter_metadata_query_helper.h"
#include "transmitter_network_query_helper.h"
#include "transmitter_settings_query_helper.h"
#include "transmitter_mqtt_query_helper.h"

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
    TransmitterNetworkStoreWorkflow::store_ip_data(transmitter_ip, transmitter_gateway, transmitter_subnet,
                                                   is_static, config_version);
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
    TransmitterNetworkStoreWorkflow::store_network_config(curr_ip, curr_gateway, curr_subnet,
                                                          stat_ip, stat_gateway, stat_subnet, stat_dns1, stat_dns2,
                                                          is_static, config_version);
}

// Current network configuration (active - could be DHCP or Static)
const uint8_t* TransmitterManager::getIP() {
    return TransmitterNetworkQueryHelper::get_ip();
}

const uint8_t* TransmitterManager::getGateway() {
    return TransmitterNetworkQueryHelper::get_gateway();
}

const uint8_t* TransmitterManager::getSubnet() {
    return TransmitterNetworkQueryHelper::get_subnet();
}

// Saved static configuration (from transmitter NVS)
const uint8_t* TransmitterManager::getStaticIP() {
    return TransmitterNetworkQueryHelper::get_static_ip();
}

const uint8_t* TransmitterManager::getStaticGateway() {
    return TransmitterNetworkQueryHelper::get_static_gateway();
}

const uint8_t* TransmitterManager::getStaticSubnet() {
    return TransmitterNetworkQueryHelper::get_static_subnet();
}

const uint8_t* TransmitterManager::getStaticDNSPrimary() {
    return TransmitterNetworkQueryHelper::get_static_dns_primary();
}

const uint8_t* TransmitterManager::getStaticDNSSecondary() {
    return TransmitterNetworkQueryHelper::get_static_dns_secondary();
}

bool TransmitterManager::isIPKnown() {
    return TransmitterNetworkQueryHelper::is_ip_known();
}

bool TransmitterManager::isStaticIP() {
    return TransmitterNetworkQueryHelper::is_static_ip();
}

uint32_t TransmitterManager::getNetworkConfigVersion() {
    return TransmitterNetworkQueryHelper::get_network_config_version();
}

void TransmitterManager::updateNetworkMode(bool is_static, uint32_t version) {
    TransmitterNetworkQueryHelper::update_network_mode(is_static, version);
}

String TransmitterManager::getIPString() {
    return TransmitterNetworkQueryHelper::get_ip_string();
}

String TransmitterManager::getURL() {
    return TransmitterNetworkQueryHelper::get_url();
}

// V2: Legacy version tracking functions removed

// Metadata management
void TransmitterManager::storeMetadata(bool valid, const char* env, const char* device,
                                       uint8_t major, uint8_t minor, uint8_t patch,
                                       const char* build_date_str) {
    TransmitterMetadataStoreWorkflow::store_metadata(valid, env, device, major, minor, patch,
                                                     build_date_str);
}

bool TransmitterManager::hasMetadata() {
    return TransmitterMetadataQueryHelper::has_metadata();
}

bool TransmitterManager::isMetadataValid() {
    return TransmitterMetadataQueryHelper::is_metadata_valid();
}

const char* TransmitterManager::getMetadataEnv() {
    return TransmitterMetadataQueryHelper::get_metadata_env();
}

const char* TransmitterManager::getMetadataDevice() {
    return TransmitterMetadataQueryHelper::get_metadata_device();
}

void TransmitterManager::getMetadataVersion(uint8_t& major, uint8_t& minor, uint8_t& patch) {
    TransmitterMetadataQueryHelper::get_metadata_version(major, minor, patch);
}

uint32_t TransmitterManager::getMetadataVersionNumber() {
    return TransmitterMetadataQueryHelper::get_metadata_version_number();
}

const char* TransmitterManager::getMetadataBuildDate() {
    return TransmitterMetadataQueryHelper::get_metadata_build_date();
}

// ═══════════════════════════════════════════════════════════════════════
// BATTERY SETTINGS MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════

void TransmitterManager::storeBatterySettings(const BatterySettings& settings) {
    TransmitterSettingsStoreWorkflow::store_battery_settings(settings);
}

BatterySettings TransmitterManager::getBatterySettings() {
    return TransmitterSettingsQueryHelper::get_battery_settings();
}

bool TransmitterManager::hasBatterySettings() {
    return TransmitterSettingsQueryHelper::has_battery_settings();
}

void TransmitterManager::storeBatteryEmulatorSettings(const BatteryEmulatorSettings& settings) {
    TransmitterSettingsStoreWorkflow::store_battery_emulator_settings(settings);
}

BatteryEmulatorSettings TransmitterManager::getBatteryEmulatorSettings() {
    return TransmitterSettingsQueryHelper::get_battery_emulator_settings();
}

bool TransmitterManager::hasBatteryEmulatorSettings() {
    return TransmitterSettingsQueryHelper::has_battery_emulator_settings();
}

void TransmitterManager::storePowerSettings(const PowerSettings& settings) {
    TransmitterSettingsStoreWorkflow::store_power_settings(settings);
}

PowerSettings TransmitterManager::getPowerSettings() {
    return TransmitterSettingsQueryHelper::get_power_settings();
}

bool TransmitterManager::hasPowerSettings() {
    return TransmitterSettingsQueryHelper::has_power_settings();
}

void TransmitterManager::storeInverterSettings(const InverterSettings& settings) {
    TransmitterSettingsStoreWorkflow::store_inverter_settings(settings);
}

InverterSettings TransmitterManager::getInverterSettings() {
    return TransmitterSettingsQueryHelper::get_inverter_settings();
}

bool TransmitterManager::hasInverterSettings() {
    return TransmitterSettingsQueryHelper::has_inverter_settings();
}

void TransmitterManager::storeCanSettings(const CanSettings& settings) {
    TransmitterSettingsStoreWorkflow::store_can_settings(settings);
}

CanSettings TransmitterManager::getCanSettings() {
    return TransmitterSettingsQueryHelper::get_can_settings();
}

bool TransmitterManager::hasCanSettings() {
    return TransmitterSettingsQueryHelper::has_can_settings();
}

void TransmitterManager::storeContactorSettings(const ContactorSettings& settings) {
    TransmitterSettingsStoreWorkflow::store_contactor_settings(settings);
}

ContactorSettings TransmitterManager::getContactorSettings() {
    return TransmitterSettingsQueryHelper::get_contactor_settings();
}

bool TransmitterManager::hasContactorSettings() {
    return TransmitterSettingsQueryHelper::has_contactor_settings();
}

// ═══════════════════════════════════════════════════════════════════════
// MQTT CONFIGURATION MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════

void TransmitterManager::storeMqttConfig(bool enabled, const uint8_t* server, uint16_t port,
                                        const char* username, const char* password,
                                        const char* client_id, bool connected, uint32_t version) {
    TransmitterMqttConfigWorkflow::store_mqtt_config(enabled, server, port, username, password,
                                                     client_id, connected, version);
}

bool TransmitterManager::isMqttEnabled() {
    return TransmitterMqttQueryHelper::is_enabled();
}

const uint8_t* TransmitterManager::getMqttServer() {
    return TransmitterMqttQueryHelper::get_server();
}

uint16_t TransmitterManager::getMqttPort() {
    return TransmitterMqttQueryHelper::get_port();
}

const char* TransmitterManager::getMqttUsername() {
    return TransmitterMqttQueryHelper::get_username();
}

const char* TransmitterManager::getMqttPassword() {
    return TransmitterMqttQueryHelper::get_password();
}

const char* TransmitterManager::getMqttClientId() {
    return TransmitterMqttQueryHelper::get_client_id();
}

bool TransmitterManager::isMqttConnected() {
    return TransmitterMqttQueryHelper::is_connected();
}

bool TransmitterManager::isMqttConfigKnown() {
    return TransmitterMqttQueryHelper::is_config_known();
}

String TransmitterManager::getMqttServerString() {
    return TransmitterMqttQueryHelper::get_server_string();
}

// Phase 4: Version and runtime status tracking
uint32_t TransmitterManager::getMqttConfigVersion() {
    return TransmitterMqttQueryHelper::get_config_version();
}

void TransmitterManager::updateRuntimeStatus(bool mqtt_conn, bool eth_conn) {
    TransmitterRuntimeStatusUpdate::update_runtime_status(mqtt_conn, eth_conn);
}

bool TransmitterManager::isEthernetConnected() {
    return TransmitterStatusQueryHelper::is_ethernet_connected();
}

unsigned long TransmitterManager::getLastBeaconTime() {
    return TransmitterStatusQueryHelper::get_last_beacon_time();
}

// Phase 4: Get transmitter time and uptime data
uint64_t TransmitterManager::getUptimeMs() {
    return TransmitterStatusQueryHelper::get_uptime_ms();
}

uint64_t TransmitterManager::getUnixTime() {
    return TransmitterStatusQueryHelper::get_unix_time();
}

uint8_t TransmitterManager::getTimeSource() {
    return TransmitterStatusQueryHelper::get_time_source();
}

// Phase 4: Update time/uptime data from heartbeat
void TransmitterManager::updateTimeData(uint64_t new_uptime_ms, uint64_t new_unix_time, uint8_t new_time_source) {
    TransmitterTimeStatusUpdateWorkflow::update_time_data(new_uptime_ms, new_unix_time, new_time_source);
}

void TransmitterManager::updateSendStatus(bool success) {
    TransmitterTimeStatusUpdateWorkflow::update_send_status(success);
}

bool TransmitterManager::wasLastSendSuccessful() {
    return TransmitterStatusQueryHelper::was_last_send_successful();
}

bool TransmitterManager::isTransmitterConnected() {
    return TransmitterConnectionStateResolver::is_transmitter_connected();
}

// Phase 3: Static spec data storage (battery emulator specs via MQTT)
void TransmitterManager::storeStaticSpecs(const JsonObject& specs) {
    TransmitterSpecStorageWorkflow::store_static_specs(specs);
}

void TransmitterManager::storeBatterySpecs(const JsonObject& specs) {
    TransmitterSpecStorageWorkflow::store_battery_specs(specs);
}

void TransmitterManager::storeInverterSpecs(const JsonObject& specs) {
    TransmitterSpecStorageWorkflow::store_inverter_specs(specs);
}

void TransmitterManager::storeChargerSpecs(const JsonObject& specs) {
    TransmitterSpecStorageWorkflow::store_charger_specs(specs);
}

void TransmitterManager::storeSystemSpecs(const JsonObject& specs) {
    TransmitterSpecStorageWorkflow::store_system_specs(specs);
}

bool TransmitterManager::hasStaticSpecs() {
    return TransmitterSpecStorageWorkflow::has_static_specs();
}

String TransmitterManager::getStaticSpecsJson() {
    return TransmitterSpecStorageWorkflow::get_static_specs_json();
}

String TransmitterManager::getBatterySpecsJson() {
    return TransmitterSpecStorageWorkflow::get_battery_specs_json();
}

String TransmitterManager::getInverterSpecsJson() {
    return TransmitterSpecStorageWorkflow::get_inverter_specs_json();
}

String TransmitterManager::getChargerSpecsJson() {
    return TransmitterSpecStorageWorkflow::get_charger_specs_json();
}

String TransmitterManager::getSystemSpecsJson() {
    return TransmitterSpecStorageWorkflow::get_system_specs_json();
}

void TransmitterManager::storeEventLogs(const JsonObject& logs) {
    TransmitterEventLogsWorkflow::store_event_logs(logs);
}

bool TransmitterManager::hasEventLogs() {
    return TransmitterEventLogsWorkflow::has_event_logs();
}

void TransmitterManager::getEventLogsSnapshot(std::vector<EventLogEntry>& out_logs, uint32_t* out_last_update_ms) {
    TransmitterEventLogsWorkflow::get_event_logs_snapshot(out_logs, out_last_update_ms);
}

uint32_t TransmitterManager::getEventLogCount() {
    return TransmitterEventLogsWorkflow::get_event_log_count();
}

uint32_t TransmitterManager::getEventLogsLastUpdateMs() {
    return TransmitterEventLogsWorkflow::get_event_logs_last_update_ms();
}
