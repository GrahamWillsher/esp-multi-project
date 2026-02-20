#pragma once

// Shim to ensure FILE and __FILE are available when esp_event.h is included in C++ builds.
#include <stdio.h>

// Provide FILE and __FILE for toolchains that don't expose them in C++ mode.
struct __sFILE;
typedef struct __sFILE FILE;
typedef struct __sFILE __FILE;

// Pull in the real ESP-IDF header.
#include_next <esp_event.h>
