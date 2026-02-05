#ifndef PAGE_GENERATOR_H
#define PAGE_GENERATOR_H

#include <Arduino.h>

// Generate standard HTML page with common template
String generatePage(const String& title, const String& content, const String& extraStyles = "", const String& script = "");

#endif
