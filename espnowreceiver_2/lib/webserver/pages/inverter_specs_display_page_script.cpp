#include "inverter_specs_display_page_script.h"

String get_inverter_specs_page_nav_links_html() {
    return R"(
            <a href="/" class="btn btn-secondary">&#8592; Back to Dashboard</a>
            <a href="/battery_settings.html" class="btn btn-secondary">&#8592; Battery Specs</a>
            <a href="/charger_settings.html" class="btn btn-secondary">Charger Specs &#8594;</a>
)";
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
