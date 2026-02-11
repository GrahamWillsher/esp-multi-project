#include "page_definitions.h"
#include <string.h>

// Central page registry - all pages defined in one place
const PageInfo PAGE_DEFINITIONS[] = {
    // Landing page
    { "/",                          "Dashboard",                   subtype_none,          false },  // V2: New device-centric landing page
    
    // Transmitter section
    { "/transmitter",               "Transmitter Hub",             subtype_none,          false },  // Hub page with navigation
    { "/transmitter/config",        "Configuration",               subtype_none,          false },  // Moved from /
    
    // Receiver section
    { "/receiver/config",           "Configuration",               subtype_systeminfo,    false },  // Renamed from /systeminfo
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
