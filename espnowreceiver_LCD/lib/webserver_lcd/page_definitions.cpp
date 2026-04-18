#include "page_definitions.h"
#include <string.h>

// Central page registry - all pages defined in one place
const PageInfo PAGE_DEFINITIONS[] = {
    // Landing + hub
    { "/",                          "Dashboard",                   subtype_none,            false, false },
    { "/transmitter",               "Transmitter Hub",             subtype_none,            false, false },

    // Transmitter configuration pages
    { "/transmitter/config",        "TX Config",                   subtype_none,            false, true  },
    { "/transmitter/hardware",      "HW Config",                   subtype_none,            false, false },
    { "/transmitter/battery",       "Battery Settings",            subtype_battery_config,  false, false },
    { "/transmitter/inverter",      "Inverter Settings",           subtype_inverter_config, false, false },
    { "/transmitter/monitor",       "Monitor",                     subtype_power_profile,   false, false },
    { "/transmitter/monitor2",      "Monitor SSE",                 subtype_power_profile,   true,  false },
    { "/transmitter/reboot",        "Reboot",                      subtype_none,            false, false },

    // Receiver pages
    { "/receiver/config",           "Receiver Info",               subtype_systeminfo,      false, true  },
    { "/receiver/network",          "Receiver Network",            subtype_network_config,  false, false },
    { "/cellmonitor",               "Cell Monitor",                subtype_cell_info,       true,  false },

    // Tooling pages
    { "/ota",                       "OTA",                         subtype_none,            false, false },
    { "/debug",                     "Debug",                       subtype_none,            false, false },
    { "/events",                    "Event Logs",                  subtype_events,          false, false },

    // Spec/display pages
    { "/battery_settings.html",     "Battery Specs",               subtype_none,            false, false },
    { "/inverter_settings.html",    "Inverter Specs",              subtype_none,            false, false },
    { "/charger_settings.html",     "Charger Specs",               subtype_none,            false, false },
    { "/system_settings.html",      "System Specs",                subtype_none,            false, false }
};

const int PAGE_COUNT = sizeof(PAGE_DEFINITIONS) / sizeof(PageInfo);

// Helper function to get subtype for a given URI
msg_subtype get_subtype_for_uri(const char* uri) {
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (strcmp(PAGE_DEFINITIONS[i].uri, uri) == 0) {
            return PAGE_DEFINITIONS[i].subtype;
        }
    }
    return subtype_none;  // Default if not found
}

// Helper function to check if URI needs SSE
bool uri_needs_sse(const char* uri) {
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (strcmp(PAGE_DEFINITIONS[i].uri, uri) == 0) {
            return PAGE_DEFINITIONS[i].needs_sse;
        }
    }
    return false;
}

// Helper function to get page info for a given URI
const PageInfo* get_page_info(const char* uri) {
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (strcmp(PAGE_DEFINITIONS[i].uri, uri) == 0) {
            return &PAGE_DEFINITIONS[i];
        }
    }
    return nullptr;
}
