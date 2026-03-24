#include "battery_specs_display_page_script.h"
#include "../common/spec_page_layout.h"

String get_battery_specs_page_nav_links_html() {
    static const SpecPageNavLink kNavLinks[] = {
        {"/", "&#8592; Back to Dashboard"},
        {"/charger_settings.html", "Charger Specs &#8594;"},
        {"/inverter_settings.html", "Inverter Specs &#8594;"},
    };
    return ::build_spec_page_nav_links(kNavLinks, sizeof(kNavLinks) / sizeof(kNavLinks[0]));
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
