# Preferences.h Resolution (Battery Emulator integration)

## Problem summary
When the Battery Emulator sources are pulled in via `lib_extra_dirs`, PlatformIO compiles those files as a library but **does not propagate framework-core include paths** (such as Arduino’s `Preferences` library) into that library’s compile units. As a result, `lib/battery_emulator_src/communication/nvm/comm_nvm.h` fails to resolve:

```
#include <Preferences.h>
```

This is why `Preferences.h` works when building Battery Emulator **standalone** or when used from the main project sources, but **fails** when the same code is compiled as a library from `lib_extra_dirs`.

## Root cause
- `battery_emulator_src` had **no `library.json`**, so PlatformIO treated it as a loose library without dependency metadata.
- Without dependency metadata, LDF did not attach the **framework Preferences include path** to the Battery Emulator compile units.
- Adding `Preferences` to `lib_deps` only affects the project graph, not the internal compile graph of a library lacking metadata.

## Permanent fix (implemented)
### 1) Add library metadata for Battery Emulator
A new file defines the library and its dependencies:
- [lib/battery_emulator_src/library.json](lib/battery_emulator_src/library.json)

Key settings:
- `srcDir: "."` to compile the Battery Emulator sources in place.
- `srcFilter` excludes nested `lib/` and non-source folders to avoid double-building.
- `dependencies` includes `Preferences` so PlatformIO supplies the include path to all Battery Emulator compile units.

### 2) Enable robust dependency propagation
- [platformio.ini](platformio.ini) now uses `lib_ldf_mode = chain+` to ensure dependency resolution is performed through nested includes.

## Why this is the correct fix
- It aligns with PlatformIO’s **library dependency model**.
- It is **stable across machines**, toolchains, and build directories.
- It does not rely on fragile, absolute include paths.
- It keeps `#include <Preferences.h>` intact (no shim or hack).

## Verification steps
1. Clean build:
   - `platformio run -e olimex_esp32_poe2`
2. Confirm that `comm_nvm.h` compiles without `Preferences.h` errors.
3. If any remaining errors appear, they should be unrelated to `Preferences.h`.

## If it still fails
- Confirm `lib/battery_emulator_src/library.json` is present and tracked.
- Ensure `lib_extra_dirs` still includes `lib/battery_emulator_src`.
- Run a verbose build (`platformio run -e olimex_esp32_poe2 -v`) and verify that the Preferences include path appears in the Battery Emulator compile command lines.

---

This resolves the `Preferences.h` issue at the PlatformIO dependency level (the correct long-term solution).