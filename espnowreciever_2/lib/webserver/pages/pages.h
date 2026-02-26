// Consolidated header for all page modules
// Include this single file instead of individual page headers

#pragma once

// V2: New landing and hub pages
#include "dashboard_page.h"
#include "transmitter_hub_page.h"

// Transmitter pages (renamed/moved)
#include "settings_page.h"           // Now at /transmitter/config
#include "battery_settings_page.h"   // Now at /transmitter/battery
#include "inverter_settings_page.h"  // Now at /transmitter/inverter
#include "monitor_page.h"            // Now at /transmitter/monitor
#include "monitor2_page.h"           // Now at /transmitter/monitor2
#include "reboot_page.h"             // Now at /transmitter/reboot

// Receiver pages
#include "systeminfo_page.h"         // Now at /receiver/config

// Battery Emulator Spec Pages (Phase 3)
#include "battery_specs_display_page.h"    // Battery specs from MQTT
#include "inverter_specs_display_page.h"   // Inverter specs from MQTT
#include "charger_specs_display_page.h"    // Charger specs from MQTT
#include "system_specs_display_page.h"     // System specs from MQTT

// Cell monitor page (simulation/live)
#include "cellmonitor_page.h"

// System tool pages
#include "ota_page.h"
#include "debug_page.h"
#include "event_logs_page.h"
