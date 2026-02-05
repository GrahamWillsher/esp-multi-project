# Quick Logging Reference

## When to Use Each Log Level

| Level | Use For | Example |
|-------|---------|---------|
| `LOG_ERROR` | Critical failures that prevent operation | OTA update failed, Ethernet init failed, MQTT connection lost |
| `LOG_WARN` | Recoverable issues or unexpected states | Ethernet disconnected, unknown packet type, missing peer |
| `LOG_INFO` | Important operational events | Connection established, state transitions, data published |
| `LOG_DEBUG` | Detailed operational data | Packet sequences, progress updates, configuration values |
| `LOG_TRACE` | Verbose packet-level details | Raw packet data, test mode cycles, detailed dumps |

## Quick Syntax

```cpp
// Error with details
LOG_ERROR("[MODULE] Failed to connect: %s", error_msg);

// Warning without parameters
LOG_WARN("[MODULE] Retrying connection");

// Info with formatted values
LOG_INFO("[MODULE] Connected to %s:%d", server, port);

// Debug with multiple parameters
LOG_DEBUG("[MODULE] Packet seq=%u, len=%d", seq, len);

// Trace for verbose details
LOG_TRACE("[MODULE] Raw data: %02X %02X %02X", buf[0], buf[1], buf[2]);
```

## Changing Log Levels

### At Compile Time (platformio.ini)
```ini
# Production: Only errors
-D COMPILE_LOG_LEVEL=LOG_ERROR

# Normal operation: Info and above
-D COMPILE_LOG_LEVEL=LOG_INFO

# Debugging: Include debug messages
-D COMPILE_LOG_LEVEL=LOG_DEBUG

# Maximum verbosity: Everything
-D COMPILE_LOG_LEVEL=LOG_TRACE
```

### At Runtime (in code)
```cpp
// Enable all logs
current_log_level = LOG_TRACE;

// Back to normal
current_log_level = LOG_INFO;

// Only errors
current_log_level = LOG_ERROR;

// Completely silent
current_log_level = LOG_NONE;
```

## How It Works

1. **Compile-time filtering**: 
   - If `COMPILE_LOG_LEVEL=LOG_INFO`, all `LOG_DEBUG` and `LOG_TRACE` macros expand to nothing
   - Code is literally removed from the binary - zero overhead

2. **Runtime filtering**:
   - Messages still compiled in are checked against `current_log_level`
   - If message level > current_log_level, it's not printed
   - Can be changed anytime without recompiling

3. **Best practice**:
   - Set `COMPILE_LOG_LEVEL=LOG_INFO` for production (removes debug/trace code)
   - Set `COMPILE_LOG_LEVEL=LOG_TRACE` for development (keeps all code, use runtime control)
   - Use `current_log_level` to adjust verbosity while running

## Example Module
```cpp
#include "config/logging_config.h"

void MyModule::init() {
    LOG_INFO("[MYMOD] Initializing...");
    
    if (!begin()) {
        LOG_ERROR("[MYMOD] Initialization failed");
        return;
    }
    
    LOG_DEBUG("[MYMOD] Config: rate=%d, mode=%d", rate, mode);
    LOG_INFO("[MYMOD] Ready");
}

void MyModule::process_data(uint8_t* data, size_t len) {
    LOG_TRACE("[MYMOD] Processing %d bytes", len);
    
    if (len == 0) {
        LOG_WARN("[MYMOD] Empty packet received");
        return;
    }
    
    // Process data
    LOG_DEBUG("[MYMOD] Processed successfully");
}
```
