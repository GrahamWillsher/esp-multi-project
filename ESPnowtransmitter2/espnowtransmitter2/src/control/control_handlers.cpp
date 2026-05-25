#include "control_handlers.h"
#include "../config/logging_config.h"
#include <Preferences.h>
#include <mqtt_logger.h>

namespace TxControlHandlers {
void save_debug_level(uint8_t level) {
    Preferences prefs;
    if (prefs.begin("debug", false)) {
        prefs.putUChar("log_level", level);
        prefs.end();
        LOG_DEBUG("DEBUG_CTRL", "Debug level saved to NVS: %u", level);
    } else {
        LOG_WARN("DEBUG_CTRL", "Failed to open preferences for debug level save");
    }
}

uint8_t load_debug_level() {
    Preferences prefs;
    uint8_t level = MQTT_LOG_INFO;

    if (prefs.begin("debug", true)) {
        level = prefs.getUChar("log_level", MQTT_LOG_INFO);
        prefs.end();
        LOG_INFO("DEBUG_CTRL", "Debug level loaded from NVS: %u", level);
    } else {
        LOG_INFO("DEBUG_CTRL", "No saved debug level, using default: INFO");
    }

    return level;
}

} // namespace TxControlHandlers