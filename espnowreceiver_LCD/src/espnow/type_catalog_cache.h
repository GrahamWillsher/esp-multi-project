#pragma once

#include <cstddef>
#include <cstdint>
#include <esp32common/espnow/common.h>

namespace TypeCatalogCache {

struct TypeEntry {
    uint8_t id;
    char name[TYPE_CATALOG_NAME_MAX];
};

void init();

void handle_battery_fragment(const type_catalog_fragment_t* fragment, size_t len);
void handle_inverter_fragment(const type_catalog_fragment_t* fragment, size_t len);
void handle_inverter_interface_fragment(const type_catalog_fragment_t* fragment, size_t len);

void replace_battery_entries(const TypeEntry* entries, size_t count, uint16_t version);
void replace_inverter_entries(const TypeEntry* entries, size_t count, uint16_t version);

size_t copy_battery_entries(TypeEntry* out, size_t max_entries);
size_t copy_inverter_entries(TypeEntry* out, size_t max_entries);
size_t copy_inverter_interface_entries(TypeEntry* out, size_t max_entries);

bool has_battery_entries();
bool has_inverter_entries();
bool has_inverter_interface_entries();

void update_announced_versions(uint16_t battery_version, uint16_t inverter_version);
bool battery_refresh_required();
bool inverter_refresh_required();
uint16_t battery_applied_version();
uint16_t inverter_applied_version();

}  // namespace TypeCatalogCache
