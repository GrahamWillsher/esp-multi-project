#include "inverter_specs_display_page_script.h"
#include "../common/spec_page_layout.h"

String get_inverter_specs_page_nav_links_html() {
    static const SpecPageNavLink kNavLinks[] = {
        {"/", "&#8592; Back to Dashboard"},
        {"/battery_settings.html", "&#8592; Battery Specs"},
        {"/charger_settings.html", "Charger Specs &#8594;"},
    };
    return ::build_spec_page_nav_links(kNavLinks, sizeof(kNavLinks) / sizeof(kNavLinks[0]));
}

String get_inverter_specs_page_inline_script() {
    return R"(
window.addEventListener('load', () => {
    const protocolEl = document.getElementById('inverterProtocolValue');
    const typeIdEl = document.getElementById('inverterTypeIdValue');
    const transmitterTypeId = typeIdEl ? parseInt((typeIdEl.textContent || '').trim(), 10) : NaN;

    const resolveByTransmitterTypeId = Number.isFinite(transmitterTypeId) && transmitterTypeId >= 0
        ? fetch('/api/get_inverter_types')
            .then(response => response.json())
            .then(data => {
                const types = Array.isArray(data.types) ? data.types : [];
                const match = types.find(t => Number(t.id) === transmitterTypeId);
                const current = protocolEl ? (protocolEl.textContent || '').trim() : '';
                if (match && protocolEl && (current === 'Unknown' || current === String(transmitterTypeId))) {
                    protocolEl.textContent = match.name;
                }
            })
            .catch(() => {})
        : Promise.resolve();

    resolveByTransmitterTypeId.finally(() => {
        CatalogLoader.loadCatalogLabel({
            catalogEndpoint: '/api/get_inverter_types',
            selectedEndpoint: '/api/get_selected_types',
            selectedKey: 'inverter_type',
            targetId: 'inverterProtocolValue',
            fallbackText: 'Unknown',
            unavailableText: 'Unknown',
            replaceIfCurrentIn: ['Unknown']
        });
    });

    CatalogLoader.loadCatalogLabel({
        catalogEndpoint: '/api/get_inverter_interfaces',
        selectedEndpoint: '/api/get_selected_interfaces',
        selectedKey: 'inverter_interface',
        targetId: 'inverterInterfaceValue',
        fallbackText: 'Unknown',
        unavailableText: 'Unavailable'
    });
});
)";
}
