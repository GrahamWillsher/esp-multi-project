#include "../battery_handlers.h"
#include "../battery_data_store.h"
#include "../battery_settings_cache.h"
#include "../../../lib/webserver/utils/transmitter_manager.h"

void handle_battery_info(const espnow_queue_msg_t* msg) {
    if (msg->len != sizeof(battery_settings_full_msg_t)) {
        LOG_ERROR("BATTERY", "Battery info: Invalid message size %d, expected %d (v2 full settings only)",
                  msg->len, sizeof(battery_settings_full_msg_t));
        return;
    }

    const battery_settings_full_msg_t* data = (battery_settings_full_msg_t*)msg->data;

    if (!validate_checksum(data, sizeof(*data))) {
        LOG_ERROR("BATTERY", "Battery settings: Invalid checksum - message rejected");
        return;
    }

    BatterySettings settings;
    settings.capacity_wh = data->capacity_wh;
    settings.max_voltage_mv = data->max_voltage_mv;
    settings.min_voltage_mv = data->min_voltage_mv;
    settings.max_charge_current_a = data->max_charge_current_a;
    settings.max_discharge_current_a = data->max_discharge_current_a;
    settings.soc_high_limit = data->soc_high_limit;
    settings.soc_low_limit = data->soc_low_limit;
    settings.cell_count = data->cell_count;
    settings.chemistry = data->chemistry;
    settings.version = BatterySettingsCache::instance().get_version();

    TransmitterManager::storeBatterySettings(settings);

    // Keep battery-emulator settings cache in sync for settings page placeholders
    BatteryEmulatorSettings emu = TransmitterManager::getBatteryEmulatorSettings();
    emu.led_mode = data->led_mode;
    TransmitterManager::storeBatteryEmulatorSettings(emu);

    BatteryData::update_battery_info(*data, settings.version);

    const char* chemistry_str[] = {"NCA", "NMC", "LFP", "LTO"};
    LOG_INFO("BATTERY", "Battery Settings: %dWh, %d-%dmV, %.1f/%.1fA, SOC:%d-%d%%, %dS %s, LED mode:%u",
             settings.capacity_wh, settings.min_voltage_mv, settings.max_voltage_mv,
             settings.max_charge_current_a, settings.max_discharge_current_a,
             settings.soc_low_limit, settings.soc_high_limit,
             settings.cell_count, chemistry_str[data->chemistry], data->led_mode);
}