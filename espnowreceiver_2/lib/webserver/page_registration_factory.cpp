#include "page_registration_factory.h"
#include "pages/pages.h"
#include "logging.h"

namespace PageRegistrationFactory {

/**
 * Page handler registration dispatch table.
 * Maps page URIs to their corresponding registration functions.
 * Reduces ~280 lines of boilerplate if all pages are registered here.
 */
struct PageHandlerDescriptor {
    const char* uri;
    esp_err_t (*register_fn)(httpd_handle_t);
};

// Table of all page handlers (order matches PAGE_DEFINITIONS for maintainability)
// Use :: to reference global-scope registration functions from pages/pages.h
const PageHandlerDescriptor PAGE_HANDLERS[] = {
    // Landing + hub
    { "/",                      ::register_dashboard_page },
    { "/transmitter",           ::register_transmitter_hub_page },

    // Transmitter configuration pages
    { "/transmitter/config",    ::register_settings_page },
    { "/transmitter/hardware",  ::register_hardware_config_page },
    { "/transmitter/battery",   ::register_battery_settings_page },
    { "/transmitter/inverter",  ::register_inverter_settings_page },
    { "/transmitter/monitor",   ::register_monitor_page },
    { "/transmitter/monitor2",  ::register_monitor2_page },
    { "/transmitter/reboot",    ::register_reboot_page },

    // Receiver pages
    { "/receiver/config",       ::register_systeminfo_page },
    { "/receiver/network",      ::register_network_config_page },
    { "/cellmonitor",           ::register_cellmonitor_page },

    // Tooling pages
    { "/ota",                   ::register_ota_page },
    { "/debug",                 ::register_debug_page },
    { "/events",                ::register_event_logs_page },

    // Spec/display pages
    { "/battery_settings.html", ::register_battery_specs_page },
    { "/inverter_settings.html",::register_inverter_specs_page },
    { "/charger_settings.html", ::register_charger_specs_page },
    { "/system_settings.html",  ::register_system_specs_page },
};

const int PAGE_HANDLER_COUNT = sizeof(PAGE_HANDLERS) / sizeof(PageHandlerDescriptor);

// ===== Factory Implementation =====

int register_all_pages(httpd_handle_t server) {
    if (server == NULL) {
        LOG_ERROR("PAGE_FACTORY", "Server handle is NULL");
        return 0;
    }
    
    int registered_count = 0;
    
    LOG_DEBUG("PAGE_FACTORY", "Registering %d pages...", PAGE_HANDLER_COUNT);
    
    for (int i = 0; i < PAGE_HANDLER_COUNT; i++) {
        const PageHandlerDescriptor& desc = PAGE_HANDLERS[i];
        
        if (desc.register_fn == NULL) {
            LOG_WARN("PAGE_FACTORY", "Handler for URI '%s' is NULL", desc.uri);
            continue;
        }
        
        esp_err_t result = desc.register_fn(server);
        if (result == ESP_OK) {
            registered_count++;
            LOG_DEBUG("PAGE_FACTORY", "✓ %s (%d/%d)", desc.uri, registered_count, PAGE_HANDLER_COUNT);
        } else {
            LOG_WARN("PAGE_FACTORY", "✗ %s - Error: %s", desc.uri, esp_err_to_name(result));
        }
    }
    
    LOG_INFO("PAGE_FACTORY", "Page registration complete: %d/%d successful", 
             registered_count, PAGE_HANDLER_COUNT);
    
    return registered_count;
}

int get_expected_page_handler_count() {
    return PAGE_HANDLER_COUNT;
}

} // namespace PageRegistrationFactory
