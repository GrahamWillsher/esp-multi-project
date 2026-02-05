#include "nav_buttons.h"
#include "../page_definitions.h"
#include <string.h>

// Helper function to generate navigation buttons from page definitions
String generate_nav_buttons(const char* current_uri) {
    String buttons = "";
    for (int i = 0; i < PAGE_COUNT; i++) {
        // Skip current page in navigation
        if (current_uri != nullptr && strcmp(PAGE_DEFINITIONS[i].uri, current_uri) == 0) {
            continue;
        }
        buttons += "<a href='" + String(PAGE_DEFINITIONS[i].uri) + "' class='button'>";
        buttons += String(PAGE_DEFINITIONS[i].name) + "</a>\n    ";
    }
    return buttons;
}
