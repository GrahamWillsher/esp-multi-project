#ifndef DATALAYER_SYSTEM_SETTINGS_SHIM_H_
#define DATALAYER_SYSTEM_SETTINGS_SHIM_H_

/**
 * @file src/datalayer/system_settings.h
 * @brief Canonical redirect — do not add definitions here.
 *
 * Phase 3 (Battery Emulator boundary rewrite):
 * The authoritative definition of these settings lives in the Battery Emulator
 * upstream boundary header. This file exists solely to satisfy relative-path
 * `#include "system_settings.h"` resolution from src/datalayer/ code.
 *
 * All edits must be made to:
 *   src/battery_emulator/system_settings.h
 */
#include "../battery_emulator/system_settings.h"

#endif
