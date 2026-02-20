#pragma once

// Shim header to make Arduino Preferences available when compiling Battery Emulator as a library.
// This file is picked up first from the local include path and forwards to the real Arduino header.
#if defined(__has_include_next)
  #include_next <Preferences.h>
#else
  #include <Preferences.h>
#endif
