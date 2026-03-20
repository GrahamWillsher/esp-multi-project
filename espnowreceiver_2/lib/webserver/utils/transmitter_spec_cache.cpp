#include "transmitter_spec_cache.h"

#include "../logging.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
    struct ScopedMutex {
        explicit ScopedMutex(SemaphoreHandle_t mutex)
            : mutex_(mutex), locked_(false) {
            if (mutex_ != nullptr) {
                locked_ = (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE);
            }
        }

        ~ScopedMutex() {
            if (locked_) {
                xSemaphoreGive(mutex_);
            }
        }

        bool locked() const { return locked_; }

    private:
        SemaphoreHandle_t mutex_;
        bool locked_;
    };

    struct SpecCache {
        String static_specs_json;
        String battery_specs_json;
        String inverter_specs_json;
        String charger_specs_json;
        String system_specs_json;
        bool static_specs_known = false;
    };

    SemaphoreHandle_t spec_cache_mutex = nullptr;
    SpecCache spec_cache;

    void ensure_mutex() {
        if (spec_cache_mutex == nullptr) {
            spec_cache_mutex = xSemaphoreCreateMutex();
        }
    }

    void serialize_json_to_string(const JsonVariantConst& json, String& out) {
        DynamicJsonDocument doc(2048);
        doc.set(json);
        out = "";
        serializeJson(doc, out);
    }
}

namespace TransmitterSpecCache {

void store_static_specs(const JsonObject& specs) {
    ensure_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        LOG_WARN("[SPEC_CACHE] Failed to lock spec cache mutex");
        return;
    }

    serialize_json_to_string(specs, spec_cache.static_specs_json);

    if (specs.containsKey("battery")) {
        serialize_json_to_string(specs["battery"], spec_cache.battery_specs_json);
    }

    if (specs.containsKey("inverter")) {
        serialize_json_to_string(specs["inverter"], spec_cache.inverter_specs_json);
    }

    if (specs.containsKey("charger")) {
        serialize_json_to_string(specs["charger"], spec_cache.charger_specs_json);
    }

    if (specs.containsKey("system")) {
        serialize_json_to_string(specs["system"], spec_cache.system_specs_json);
    }

    spec_cache.static_specs_known = true;
    LOG_INFO("[SPEC_CACHE] Stored static specs from MQTT");
}

void store_battery_specs(const JsonObject& specs) {
    ensure_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        LOG_WARN("[SPEC_CACHE] Failed to lock spec cache mutex");
        return;
    }

    serialize_json_to_string(specs, spec_cache.battery_specs_json);
    LOG_INFO("[SPEC_CACHE] Stored battery specs from MQTT");
}

void store_inverter_specs(const JsonObject& specs) {
    ensure_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        LOG_WARN("[SPEC_CACHE] Failed to lock spec cache mutex");
        return;
    }

    serialize_json_to_string(specs, spec_cache.inverter_specs_json);
    LOG_INFO("[SPEC_CACHE] Stored inverter specs from MQTT");
}

void store_charger_specs(const JsonObject& specs) {
    ensure_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        LOG_WARN("[SPEC_CACHE] Failed to lock spec cache mutex");
        return;
    }

    serialize_json_to_string(specs, spec_cache.charger_specs_json);
    LOG_INFO("[SPEC_CACHE] Stored charger specs from MQTT");
}

void store_system_specs(const JsonObject& specs) {
    ensure_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        LOG_WARN("[SPEC_CACHE] Failed to lock spec cache mutex");
        return;
    }

    serialize_json_to_string(specs, spec_cache.system_specs_json);
    LOG_INFO("[SPEC_CACHE] Stored system specs from MQTT");
}

bool has_static_specs() {
    ensure_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        return false;
    }

    return spec_cache.static_specs_known;
}

String get_static_specs_json() {
    ensure_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        return String();
    }

    return spec_cache.static_specs_json;
}

String get_battery_specs_json() {
    ensure_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        return String();
    }

    return spec_cache.battery_specs_json;
}

String get_inverter_specs_json() {
    ensure_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        return String();
    }

    return spec_cache.inverter_specs_json;
}

String get_charger_specs_json() {
    ensure_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        return String();
    }

    return spec_cache.charger_specs_json;
}

String get_system_specs_json() {
    ensure_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        return String();
    }

    return spec_cache.system_specs_json;
}

} // namespace TransmitterSpecCache
