#ifndef SYSTEM_SETTINGS_H_
#define SYSTEM_SETTINGS_H_

/**
 * @file src/battery_emulator/system_settings.h
 * @brief Upstream Battery Emulator boundary header — canonical owner.
 *
 * Phase 3 (Battery Emulator boundary rewrite):
 * This is the single canonical definition of task priorities and cell-count
 * limits for the Battery Emulator integration layer.
 *
 * src/datalayer/system_settings.h redirects here.
 * Do NOT duplicate these definitions elsewhere.
 */

#define TASK_CORE_PRIO 4
#define TASK_CONNECTIVITY_PRIO 3
#define TASK_MQTT_PRIO 2
#define TASK_MODBUS_PRIO 8
#define TASK_ACAN2515_PRIORITY 10
#define TASK_ACAN2517FD_PRIORITY 10

/** Maximum number of individual cell voltages in the datalayer array. */
#define MAX_AMOUNT_CELLS 192

#endif
