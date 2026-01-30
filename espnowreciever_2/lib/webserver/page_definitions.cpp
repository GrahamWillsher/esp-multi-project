#include "page_definitions.h"
#include <string.h>

// Central page registry - all pages defined in one place
const PageInfo PAGE_DEFINITIONS[] = {
    { "/",          "Settings",                    subtype_none,          false },  // Landing page
    { "/monitor",   "Battery Monitor (Polling)",   subtype_none,          false },  // Polling mode (uses API)
    { "/monitor2",  "Battery Monitor (SSE)",       subtype_power_profile, true  },  // Real-time SSE
    { "/systeminfo","System Info",                 subtype_systeminfo,    false },  // System info page
    { "/reboot",    "Reboot Transmitter",          subtype_none,          false },  // Reboot command action
    { "/ota",       "OTA Update",                  subtype_none,          false },  // OTA firmware update
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
