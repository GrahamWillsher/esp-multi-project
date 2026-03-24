#include "battery_specs_display_page_script.h"

String get_battery_specs_page_nav_links_html() {
    return R"(
            <a href="/" class="btn btn-secondary">&#8592; Back to Dashboard</a>
            <a href="/charger_settings.html" class="btn btn-secondary">Charger Specs &#8594;</a>
            <a href="/inverter_settings.html" class="btn btn-secondary">Inverter Specs &#8594;</a>
)";
}

String get_battery_specs_page_inline_script() {
    return R"(
window.addEventListener('load', () => {
    CatalogLoader.loadCatalogLabel({
        catalogEndpoint: '/api/get_battery_types',
        selectedEndpoint: '/api/get_selected_types',
        selectedKey: 'battery_type',
        targetId: 'batteryTypeValue',
        fallbackText: 'Unknown',
        unavailableText: 'Unknown',
        replaceIfCurrentIn: ['Unknown', 'TEST_DUMMY']
    });

    CatalogLoader.loadCatalogLabel({
        catalogEndpoint: '/api/get_battery_interfaces',
        selectedEndpoint: '/api/get_selected_interfaces',
        selectedKey: 'battery_interface',
        targetId: 'batteryInterfaceValue',
        fallbackText: 'Unknown',
        unavailableText: 'Unavailable'
    });
});
)";
}
