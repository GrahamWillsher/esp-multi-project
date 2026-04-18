#include "transmitter_nvs_persistence.h"
#include "transmitter_state.h"
#include "transmitter_settings_cache.h"
#include "transmitter_mqtt_specs.h"
#include "transmitter_network.h"
#include "sse_notifier.h"
#include "../logging.h"

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

namespace {
    constexpr const char* kTxCacheNamespace = "tx_cache";
    constexpr uint32_t kNvsSaveDebounceMs = 2000;
    TimerHandle_t g_nvs_save_timer = nullptr;

    // Set when a save is scheduled; cleared only after a successful persist.
    // Allows callers to detect that a scheduled save failed to execute.
    volatile bool g_nvs_save_pending = false;

    void persist_to_nvs_now() {
        Preferences prefs;
        if (!prefs.begin(kTxCacheNamespace, false)) {
            LOG_ERROR("TX_NVS", "prefs.begin failed — NVS save skipped (namespace=%s)",
                      kTxCacheNamespace);
            return;
        }

        TransmitterMqttSpecs::save_to_prefs(&prefs);
        TransmitterNetwork::save_to_prefs(&prefs);
        TransmitterState::save_metadata_to_prefs(&prefs);
        TransmitterSettingsCache::save_to_prefs(&prefs);

        prefs.end();
        g_nvs_save_pending = false;
        LOG_DEBUG("TX_NVS", "NVS persist complete");
    }

    void nvs_save_timer_callback(TimerHandle_t timer) {
        (void)timer;
        persist_to_nvs_now();
    }

    void schedule_nvs_save() {
        g_nvs_save_pending = true;

        if (g_nvs_save_timer == nullptr) {
            // Timer was never created (allocation failed at init); persist immediately.
                LOG_WARN("TX_NVS", "NVS save timer unavailable — persisting synchronously");
            persist_to_nvs_now();
            return;
        }

        const BaseType_t ok =
            (xTimerIsTimerActive(g_nvs_save_timer) == pdTRUE)
                ? xTimerReset(g_nvs_save_timer, pdMS_TO_TICKS(50))
                : xTimerStart(g_nvs_save_timer, pdMS_TO_TICKS(50));

        if (ok != pdPASS) {
            LOG_WARN("TX_NVS", "NVS save timer schedule failed — persisting synchronously");
            persist_to_nvs_now();
        }
    }
}

void TransmitterNvsPersistence::init() {
    if (g_nvs_save_timer == nullptr) {
        g_nvs_save_timer = xTimerCreate(
            "TxCacheNVS",
            pdMS_TO_TICKS(kNvsSaveDebounceMs),
            pdFALSE,
            nullptr,
            nvs_save_timer_callback
        );
        if (g_nvs_save_timer == nullptr) {
            LOG_ERROR("TX_NVS", "Failed to create NVS debounce timer");
        }
    }

    loadFromNVS();
}

void TransmitterNvsPersistence::loadFromNVS() {
    Preferences prefs;
    if (!prefs.begin(kTxCacheNamespace, true)) {
        return;
    }

    TransmitterMqttSpecs::load_from_prefs(&prefs);
    TransmitterNetwork::load_from_prefs(&prefs);
    TransmitterState::load_metadata_from_prefs(&prefs);
    TransmitterSettingsCache::load_from_prefs(&prefs);

    prefs.end();
}

void TransmitterNvsPersistence::saveToNVS() {
    schedule_nvs_save();
}

void TransmitterNvsPersistence::persist() {
    saveToNVS();
}

void TransmitterNvsPersistence::notifyAndPersist() {
    SSENotifier::notifyDataUpdated();
    saveToNVS();
}
