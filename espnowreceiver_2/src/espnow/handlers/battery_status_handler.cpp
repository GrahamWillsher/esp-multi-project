#include "../battery_handlers.h"
#include "../battery_data_store.h"

void handle_battery_status(const espnow_queue_msg_t* msg) {
    const battery_status_msg_t* data = (battery_status_msg_t*)msg->data;

    if (!validate_checksum(data, sizeof(*data))) {
        LOG_ERROR("BATTERY", "Battery status: Invalid checksum - message rejected");
        return;
    }

    BatteryData::soc_percent = data->soc_percent_100 / 100.0;
    BatteryData::voltage_V = data->voltage_mV / 1000.0;
    BatteryData::current_A = data->current_mA / 1000.0;
    BatteryData::temperature_C = data->temperature_dC / 10.0;
    BatteryData::power_W = data->power_W;
    BatteryData::max_charge_power_W = data->max_charge_power_W;
    BatteryData::max_discharge_power_W = data->max_discharge_power_W;
    BatteryData::bms_status = data->bms_status;
    BatteryData::status_received = true;

    LOG_DEBUG("BATTERY", "Battery Status: SOC=%.2f%%, V=%.2fV, I=%.2fA, T=%.1fC, P=%dW, BMS=%d",
              BatteryData::soc_percent, BatteryData::voltage_V, BatteryData::current_A,
              BatteryData::temperature_C, BatteryData::power_W, BatteryData::bms_status);
}