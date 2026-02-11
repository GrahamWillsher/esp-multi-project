// Consolidated header for all page modules
// Include this single file instead of individual page headers

#pragma once

// V2: New landing and hub pages
#include "dashboard_page.h"
#include "transmitter_hub_page.h"

// Transmitter pages (renamed/moved)
#include "settings_page.h"           // Now at /transmitter/config
#include "battery_settings_page.h"   // Now at /transmitter/battery
#include "monitor_page.h"            // Now at /transmitter/monitor
#include "monitor2_page.h"           // Now at /transmitter/monitor2
#include "reboot_page.h"             // Now at /transmitter/reboot

// Receiver pages
#include "systeminfo_page.h"         // Now at /receiver/config

// System tool pages
#include "ota_page.h"
#include "debug_page.h"
