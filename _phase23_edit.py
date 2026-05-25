from pathlib import Path

def replace_text(path, old, new):
    p = Path(path)
    txt = p.read_text(encoding='utf-8')
    if old not in txt:
        raise SystemExit(f"Pattern not found in {path}: {old[:120]!r}")
    txt = txt.replace(old, new, 1)
    p.write_text(txt, encoding='utf-8')

replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_2\lib\webserver\pages\hardware_config_page_script.cpp",
"                const settingsReady = (data.settings_ready === true) || (data.success === true);\n",
"                const hasVersions = (Number(data.battery_version) > 0)\n                    && (Number(data.power_version) > 0)\n                    && (Number(data.can_version) > 0)\n                    && (Number(data.contactor_version) > 0);\n                const settingsReady = hasVersions;\n")

replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_2\src\mqtt\mqtt_client.cpp",
"        power.no_inverter_disconnect_contactor = p[\"no_inverter_disconnect_contactor\"] | power.no_inverter_disconnect_contactor;\n        TransmitterManager::storePowerSettings(power);\n",
"        power.no_inverter_disconnect_contactor = p[\"no_inverter_disconnect_contactor\"] | power.no_inverter_disconnect_contactor;\n        power.version = p[\"version\"] | power.version;\n        TransmitterManager::storePowerSettings(power);\n")
replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_2\src\mqtt\mqtt_client.cpp",
"        can.use_canfd_as_classic = c[\"use_canfd_as_classic\"] | can.use_canfd_as_classic;\n        TransmitterManager::storeCanSettings(can);\n",
"        can.use_canfd_as_classic = c[\"use_canfd_as_classic\"] | can.use_canfd_as_classic;\n        can.version = c[\"version\"] | can.version;\n        TransmitterManager::storeCanSettings(can);\n")
replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_2\src\mqtt\mqtt_client.cpp",
"        contactor.bms_first_align_target_minutes = c[\"bms_first_align_target_minutes\"] | c[\"bms_first_align_target_mins\"] | contactor.bms_first_align_target_minutes;\n        TransmitterManager::storeContactorSettings(contactor);\n",
"        contactor.bms_first_align_target_minutes = c[\"bms_first_align_target_minutes\"] | c[\"bms_first_align_target_mins\"] | contactor.bms_first_align_target_minutes;\n        contactor.version = c[\"version\"] | contactor.version;\n        TransmitterManager::storeContactorSettings(contactor);\n")

replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\lib\webserver_lcd\utils\transmitter_settings_types.h",
"    bool external_precharge_enabled;\n    bool no_inverter_disconnect_contactor;\n};\n",
"    bool external_precharge_enabled;\n    bool no_inverter_disconnect_contactor;\n    uint32_t version;  // Version tracking for synchronization\n};\n")
replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\lib\webserver_lcd\utils\transmitter_settings_types.h",
"    uint16_t pylon_send_interval_ms;\n    bool use_canfd_as_classic;\n};\n",
"    uint16_t pylon_send_interval_ms;\n    bool use_canfd_as_classic;\n    uint32_t version;  // Version tracking for synchronization\n};\n")
replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\lib\webserver_lcd\utils\transmitter_settings_types.h",
"    bool periodic_bms_reset;\n    bool bms_first_align_enabled;\n    uint16_t bms_first_align_target_minutes;\n};\n",
"    bool periodic_bms_reset;\n    bool bms_first_align_enabled;\n    uint16_t bms_first_align_target_minutes;\n    uint32_t version;  // Version tracking for synchronization\n};\n")

replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\lib\webserver_lcd\utils\transmitter_settings_cache.cpp",
"            .equipment_stop_type = 0,\n            .external_precharge_enabled = false,\n            .no_inverter_disconnect_contactor = false\n        };\n",
"            .equipment_stop_type = 0,\n            .external_precharge_enabled = false,\n            .no_inverter_disconnect_contactor = false,\n            .version = 0\n        };\n")
replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\lib\webserver_lcd\utils\transmitter_settings_cache.cpp",
"            .sofar_id = 0,\n            .pylon_send_interval_ms = 0,\n            .use_canfd_as_classic = false\n        };\n",
"            .sofar_id = 0,\n            .pylon_send_interval_ms = 0,\n            .use_canfd_as_classic = false,\n            .version = 0\n        };\n")
replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\lib\webserver_lcd\utils\transmitter_settings_cache.cpp",
"            .periodic_bms_reset = false,\n            .bms_first_align_enabled = false,\n            .bms_first_align_target_minutes = 120\n        };\n",
"            .periodic_bms_reset = false,\n            .bms_first_align_enabled = false,\n            .bms_first_align_target_minutes = 120,\n            .version = 0\n        };\n")

replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\lib\webserver_lcd\utils\transmitter_settings_cache.h",
"void update_battery_cell_count(uint16_t cell_count);\n\n} // namespace TransmitterSettingsCache\n",
"void update_battery_cell_count(uint16_t cell_count);\n\n// Version getters for cache validation\nuint32_t get_battery_settings_version();\nuint32_t get_power_settings_version();\nuint32_t get_can_settings_version();\nuint32_t get_contactor_settings_version();\n\n} // namespace TransmitterSettingsCache\n")

replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\lib\webserver_lcd\utils\transmitter_settings_cache.cpp",
"void update_battery_cell_count(uint16_t cell_count) {\n    if (cell_count > 0 && cell_count <= 255) {\n        ensure_mutex();\n        ScopedMutex lock(cache_mutex);\n        settings_cache.battery_settings.cell_count = static_cast<uint8_t>(cell_count);\n    }\n}\n\n} // namespace TransmitterSettingsCache\n",
"void update_battery_cell_count(uint16_t cell_count) {\n    if (cell_count > 0 && cell_count <= 255) {\n        ensure_mutex();\n        ScopedMutex lock(cache_mutex);\n        settings_cache.battery_settings.cell_count = static_cast<uint8_t>(cell_count);\n    }\n}\n\nuint32_t get_battery_settings_version() {\n    ensure_mutex();\n    ScopedMutex lock(cache_mutex);\n    return settings_cache.battery_settings.version;\n}\n\nuint32_t get_power_settings_version() {\n    ensure_mutex();\n    ScopedMutex lock(cache_mutex);\n    return settings_cache.power_settings.version;\n}\n\nuint32_t get_can_settings_version() {\n    ensure_mutex();\n    ScopedMutex lock(cache_mutex);\n    return settings_cache.can_settings.version;\n}\n\nuint32_t get_contactor_settings_version() {\n    ensure_mutex();\n    ScopedMutex lock(cache_mutex);\n    return settings_cache.contactor_settings.version;\n}\n\n} // namespace TransmitterSettingsCache\n")

replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\lib\webserver_lcd\utils\transmitter_manager.h",
"    static void storeContactorSettings(const ContactorSettings& settings);\n    static ContactorSettings getContactorSettings();\n    static bool hasContactorSettings();\n    \n    // Time data management\n",
"    static void storeContactorSettings(const ContactorSettings& settings);\n    static ContactorSettings getContactorSettings();\n    static bool hasContactorSettings();\n\n    // Version getters for cache validation\n    static uint32_t getBatterySettingsVersion();\n    static uint32_t getPowerSettingsVersion();\n    static uint32_t getCanSettingsVersion();\n    static uint32_t getContactorSettingsVersion();\n    \n    // Time data management\n")

replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\lib\webserver_lcd\utils\transmitter_manager.cpp",
"bool TransmitterManager::hasContactorSettings() {\n    return TransmitterSettingsCache::has_contactor_settings();\n}\n\n// ═══════════════════════════════════════════════════════════════════════\n// MQTT CONFIGURATION MANAGEMENT\n",
"bool TransmitterManager::hasContactorSettings() {\n    return TransmitterSettingsCache::has_contactor_settings();\n}\n\nuint32_t TransmitterManager::getBatterySettingsVersion() {\n    return TransmitterSettingsCache::get_battery_settings_version();\n}\n\nuint32_t TransmitterManager::getPowerSettingsVersion() {\n    return TransmitterSettingsCache::get_power_settings_version();\n}\n\nuint32_t TransmitterManager::getCanSettingsVersion() {\n    return TransmitterSettingsCache::get_can_settings_version();\n}\n\nuint32_t TransmitterManager::getContactorSettingsVersion() {\n    return TransmitterSettingsCache::get_contactor_settings_version();\n}\n\n// ═══════════════════════════════════════════════════════════════════════\n// MQTT CONFIGURATION MANAGEMENT\n")

replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\lib\webserver_lcd\api\api_settings_handlers.cpp",
"    const bool battery_known = TransmitterManager::hasBatterySettings();\n    const bool power_known = TransmitterManager::hasPowerSettings();\n    const bool can_known = TransmitterManager::hasCanSettings();\n    const bool contactor_known = TransmitterManager::hasContactorSettings();\n    const bool settings_ready = battery_known && power_known && can_known && contactor_known;\n",
"    const bool battery_known = TransmitterManager::hasBatterySettings();\n    const bool power_known = TransmitterManager::hasPowerSettings();\n    const bool can_known = TransmitterManager::hasCanSettings();\n    const bool contactor_known = TransmitterManager::hasContactorSettings();\n\n    const uint32_t battery_version = TransmitterManager::getBatterySettingsVersion();\n    const uint32_t power_version = TransmitterManager::getPowerSettingsVersion();\n    const uint32_t can_version = TransmitterManager::getCanSettingsVersion();\n    const uint32_t contactor_version = TransmitterManager::getContactorSettingsVersion();\n\n    const bool battery_valid = battery_known && battery_version > 0;\n    const bool power_valid = power_known && power_version > 0;\n    const bool can_valid = can_known && can_version > 0;\n    const bool contactor_valid = contactor_known && contactor_version > 0;\n\n    const bool settings_ready = battery_valid && power_valid && can_valid && contactor_valid;\n")
replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\lib\webserver_lcd\api\api_settings_handlers.cpp",
"    doc[\"contactor_known\"]           = contactor_known;\n    doc[\"requested\"]                 = requested;\n",
"    doc[\"contactor_known\"]           = contactor_known;\n    doc[\"requested\"]                 = requested;\n    doc[\"battery_version\"]           = battery_version;\n    doc[\"power_version\"]             = power_version;\n    doc[\"can_version\"]               = can_version;\n    doc[\"contactor_version\"]         = contactor_version;\n")

replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\lib\webserver_lcd\pages\hardware_config_page_script.cpp",
"                const settingsReady = (data.settings_ready === true) || (data.success === true);\n",
"                const hasVersions = (Number(data.battery_version) > 0)\n                    && (Number(data.power_version) > 0)\n                    && (Number(data.can_version) > 0)\n                    && (Number(data.contactor_version) > 0);\n                const settingsReady = hasVersions;\n")

replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\src\mqtt\mqtt_client.cpp",
"        power.no_inverter_disconnect_contactor = p[\"no_inverter_disconnect_contactor\"] | power.no_inverter_disconnect_contactor;\n        TransmitterManager::storePowerSettings(power);\n",
"        power.no_inverter_disconnect_contactor = p[\"no_inverter_disconnect_contactor\"] | power.no_inverter_disconnect_contactor;\n        power.version = p[\"version\"] | power.version;\n        TransmitterManager::storePowerSettings(power);\n")
replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\src\mqtt\mqtt_client.cpp",
"        can.use_canfd_as_classic = c[\"use_canfd_as_classic\"] | can.use_canfd_as_classic;\n        TransmitterManager::storeCanSettings(can);\n",
"        can.use_canfd_as_classic = c[\"use_canfd_as_classic\"] | can.use_canfd_as_classic;\n        can.version = c[\"version\"] | can.version;\n        TransmitterManager::storeCanSettings(can);\n")
replace_text(r"C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\src\mqtt\mqtt_client.cpp",
"        contactor.bms_first_align_target_minutes = c[\"bms_first_align_target_minutes\"] | c[\"bms_first_align_target_mins\"] | contactor.bms_first_align_target_minutes;\n        TransmitterManager::storeContactorSettings(contactor);\n",
"        contactor.bms_first_align_target_minutes = c[\"bms_first_align_target_minutes\"] | c[\"bms_first_align_target_mins\"] | contactor.bms_first_align_target_minutes;\n        contactor.version = c[\"version\"] | contactor.version;\n        TransmitterManager::storeContactorSettings(contactor);\n")

print('Edits applied successfully.')
