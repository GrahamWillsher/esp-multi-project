#ifndef NAV_BUTTONS_H
#define NAV_BUTTONS_H

#include <Arduino.h>

// Helper function to generate navigation buttons from page definitions
String generate_nav_buttons(const char* current_uri = nullptr);

#endif
