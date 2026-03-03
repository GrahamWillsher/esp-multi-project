#include "../battery_handlers.h"
#include "../battery_data_store.h"

void handle_inverter_status(const espnow_queue_msg_t* msg) {
    const inverter_status_msg_t* data = (inverter_status_msg_t*)msg->data;

    if (!validate_checksum(data, sizeof(*data))) {
        LOG_ERROR("BATTERY", "Inverter status: Invalid checksum - message rejected");
        return;
    }

    BatteryData::inverter_ac_voltage_V = data->ac_voltage_V;
    BatteryData::inverter_ac_frequency_Hz = data->ac_frequency_dHz / 10.0;
    BatteryData::inverter_ac_current_A = data->ac_current_dA / 10.0;
    BatteryData::inverter_power_W = data->power_W;
    BatteryData::inverter_status = data->inverter_status;
    BatteryData::inverter_received = true;

    LOG_DEBUG("BATTERY", "Inverter Status=%d, AC=%dV/%.1fA@%.1fHz, P=%dW",
              BatteryData::inverter_status, BatteryData::inverter_ac_voltage_V,
              BatteryData::inverter_ac_current_A, BatteryData::inverter_ac_frequency_Hz,
              BatteryData::inverter_power_W);
}