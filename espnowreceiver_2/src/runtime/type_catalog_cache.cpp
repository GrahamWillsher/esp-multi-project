#include "type_catalog_cache.h"
#include "../config/logging_config.h"
#include <Arduino.h>
#include <cstring>

namespace {

constexpr size_t MAX_TYPE_ENTRIES = 128;
constexpr size_t MAX_FRAGMENT_TRACK = 64;

struct CatalogState {
    TypeCatalogCache::TypeEntry staging[MAX_TYPE_ENTRIES]{};
    size_t staging_count{0};

    TypeCatalogCache::TypeEntry committed[MAX_TYPE_ENTRIES]{};
    size_t committed_count{0};

    bool seen_fragment[MAX_FRAGMENT_TRACK]{};
    uint8_t fragments_seen{0};
    uint8_t fragment_total{0};
    uint16_t sequence{0};
    uint16_t announced_version{0};
    uint16_t applied_version{0};
};

CatalogState g_battery{};
CatalogState g_inverter{};
CatalogState g_inverter_interfaces{};

bool g_initialized = false;

void reset_state_for_sequence(CatalogState& state, uint16_t sequence, uint8_t fragment_total) {
    state.staging_count = 0;
    memset(state.seen_fragment, 0, sizeof(state.seen_fragment));
    state.fragments_seen = 0;
    state.fragment_total = fragment_total;
    state.sequence = sequence;
}

void upsert_entry(TypeCatalogCache::TypeEntry* entries,
                  size_t* count,
                  uint8_t id,
                  const char* name) {
    for (size_t i = 0; i < *count; ++i) {
        if (entries[i].id == id) {
            strncpy(entries[i].name, name, sizeof(entries[i].name) - 1);
            entries[i].name[sizeof(entries[i].name) - 1] = '\0';
            return;
        }
    }

    if (*count >= MAX_TYPE_ENTRIES) {
        return;
    }

    entries[*count].id = id;
    strncpy(entries[*count].name, name, sizeof(entries[*count].name) - 1);
    entries[*count].name[sizeof(entries[*count].name) - 1] = '\0';
    (*count)++;
}

bool validate_fragment(const type_catalog_fragment_t* fragment, size_t len) {
    if (!fragment) {
        return false;
    }

    const size_t header_size = offsetof(type_catalog_fragment_t, entries);
    if (len < header_size) {
        return false;
    }

    if (fragment->entry_count > TYPE_CATALOG_MAX_ENTRIES_PER_FRAGMENT) {
        return false;
    }

    const size_t expected_size = header_size +
                                 (static_cast<size_t>(fragment->entry_count) * sizeof(type_catalog_entry_t));

    if (len < expected_size) {
        return false;
    }

    if (fragment->fragment_total == 0 || fragment->fragment_total > MAX_FRAGMENT_TRACK) {
        return false;
    }

    if (fragment->fragment_index >= fragment->fragment_total) {
        return false;
    }

    return true;
}

void handle_fragment_common(CatalogState& state,
                            const char* tag,
                            const type_catalog_fragment_t* fragment,
                            size_t len) {
    if (!validate_fragment(fragment, len)) {
        LOG_WARN("TYPE_CATALOG", "%s fragment validation failed (len=%u)", tag, (unsigned)len);
        return;
    }

    const bool sequence_changed =
        (state.sequence != fragment->sequence) || (state.fragment_total != fragment->fragment_total);

    if (sequence_changed || fragment->fragment_index == 0) {
        reset_state_for_sequence(state, fragment->sequence, fragment->fragment_total);
    }

    for (uint8_t i = 0; i < fragment->entry_count; ++i) {
        const type_catalog_entry_t& wire = fragment->entries[i];
        upsert_entry(state.staging, &state.staging_count, wire.id, wire.name);
    }

    if (!state.seen_fragment[fragment->fragment_index]) {
        state.seen_fragment[fragment->fragment_index] = true;
        state.fragments_seen++;
    }

    if (state.fragments_seen >= state.fragment_total) {
        state.committed_count = state.staging_count;
        for (size_t i = 0; i < state.committed_count; ++i) {
            state.committed[i] = state.staging[i];
        }

        // Apply announced version only after a complete catalog commit.
        if (state.announced_version != 0) {
            state.applied_version = state.announced_version;
        }

        LOG_INFO("TYPE_CATALOG", "%s catalog refresh complete: %u entries", tag, (unsigned)state.committed_count);
    }
}

}  // namespace

namespace TypeCatalogCache {

void init() {
    if (g_initialized) {
        return;
    }
    g_initialized = true;
}

void handle_battery_fragment(const type_catalog_fragment_t* fragment, size_t len) {
    handle_fragment_common(g_battery, "battery", fragment, len);
}

void handle_inverter_fragment(const type_catalog_fragment_t* fragment, size_t len) {
    handle_fragment_common(g_inverter, "inverter", fragment, len);
}

void handle_inverter_interface_fragment(const type_catalog_fragment_t* fragment, size_t len) {
    handle_fragment_common(g_inverter_interfaces, "inverter_interface", fragment, len);
}

size_t copy_battery_entries(TypeEntry* out, size_t max_entries) {
    if (!out || max_entries == 0) {
        return 0;
    }

    const size_t count = (g_battery.committed_count < max_entries) ? g_battery.committed_count : max_entries;
    for (size_t i = 0; i < count; ++i) {
        out[i] = g_battery.committed[i];
    }
    return count;
}

size_t copy_inverter_entries(TypeEntry* out, size_t max_entries) {
    if (!out || max_entries == 0) {
        return 0;
    }

    const size_t count = (g_inverter.committed_count < max_entries) ? g_inverter.committed_count : max_entries;
    for (size_t i = 0; i < count; ++i) {
        out[i] = g_inverter.committed[i];
    }
    return count;
}

size_t copy_inverter_interface_entries(TypeEntry* out, size_t max_entries) {
    if (!out || max_entries == 0) {
        return 0;
    }

    const size_t count = (g_inverter_interfaces.committed_count < max_entries)
                             ? g_inverter_interfaces.committed_count
                             : max_entries;
    for (size_t i = 0; i < count; ++i) {
        out[i] = g_inverter_interfaces.committed[i];
    }
    return count;
}

bool has_battery_entries() {
    return g_battery.committed_count > 0;
}

bool has_inverter_entries() {
    return g_inverter.committed_count > 0;
}

bool has_inverter_interface_entries() {
    return g_inverter_interfaces.committed_count > 0;
}

void replace_battery_entries(const TypeEntry* entries, size_t count, uint16_t version) {
    if (!entries || count == 0) {
        return;
    }

    const size_t copy_count = (count > MAX_TYPE_ENTRIES) ? MAX_TYPE_ENTRIES : count;
    g_battery.committed_count = copy_count;
    for (size_t i = 0; i < copy_count; ++i) {
        g_battery.committed[i] = entries[i];
    }

    if (version != 0) {
        g_battery.announced_version = version;
        g_battery.applied_version = version;
    }

    LOG_INFO("TYPE_CATALOG", "Battery catalog replaced from MQTT: %u entries (version=%u)",
             (unsigned)copy_count,
             (unsigned)version);
}

void replace_inverter_entries(const TypeEntry* entries, size_t count, uint16_t version) {
    if (!entries || count == 0) {
        return;
    }

    const size_t copy_count = (count > MAX_TYPE_ENTRIES) ? MAX_TYPE_ENTRIES : count;
    g_inverter.committed_count = copy_count;
    for (size_t i = 0; i < copy_count; ++i) {
        g_inverter.committed[i] = entries[i];
    }

    if (version != 0) {
        g_inverter.announced_version = version;
        g_inverter.applied_version = version;
    }

    LOG_INFO("TYPE_CATALOG", "Inverter catalog replaced from MQTT: %u entries (version=%u)",
             (unsigned)copy_count,
             (unsigned)version);
}

void update_announced_versions(uint16_t battery_version, uint16_t inverter_version) {
    g_battery.announced_version = battery_version;
    g_inverter.announced_version = inverter_version;
}

bool battery_refresh_required() {
    if (g_battery.committed_count == 0) {
        return true;
    }
    if (g_battery.announced_version == 0) {
        return false;
    }
    return g_battery.announced_version != g_battery.applied_version;
}

bool inverter_refresh_required() {
    if (g_inverter.committed_count == 0) {
        return true;
    }
    if (g_inverter.announced_version == 0) {
        return false;
    }
    return g_inverter.announced_version != g_inverter.applied_version;
}

uint16_t battery_applied_version() {
    return g_battery.applied_version;
}

uint16_t inverter_applied_version() {
    return g_inverter.applied_version;
}

}  // namespace TypeCatalogCache
