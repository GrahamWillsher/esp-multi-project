#include "transmitter_spec_storage_workflow.h"
#include "transmitter_spec_cache.h"
#include "transmitter_battery_spec_sync.h"

namespace TransmitterSpecStorageWorkflow {
    void store_static_specs(const JsonObject& specs) {
        TransmitterSpecCache::store_static_specs(specs);
    }
    
    void store_battery_specs(const JsonObject& specs) {
        TransmitterBatterySpecSync::store_battery_specs(specs);
    }
    
    void store_inverter_specs(const JsonObject& specs) {
        TransmitterSpecCache::store_inverter_specs(specs);
    }
    
    void store_charger_specs(const JsonObject& specs) {
        TransmitterSpecCache::store_charger_specs(specs);
    }
    
    void store_system_specs(const JsonObject& specs) {
        TransmitterSpecCache::store_system_specs(specs);
    }
    
    bool has_static_specs() {
        return TransmitterSpecCache::has_static_specs();
    }
    
    String get_static_specs_json() {
        return TransmitterSpecCache::get_static_specs_json();
    }
    
    String get_battery_specs_json() {
        return TransmitterSpecCache::get_battery_specs_json();
    }
    
    String get_inverter_specs_json() {
        return TransmitterSpecCache::get_inverter_specs_json();
    }
    
    String get_charger_specs_json() {
        return TransmitterSpecCache::get_charger_specs_json();
    }
    
    String get_system_specs_json() {
        return TransmitterSpecCache::get_system_specs_json();
    }
}
