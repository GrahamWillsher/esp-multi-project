#include "inverter_specs_display_page_script.h"

String get_inverter_specs_page_inline_script() {
    return R"(
window.addEventListener('load', () => {
    function loadLabel(catalogEndpoint, selectedEndpoint, selectedKey, targetId, fallbackText, unavailableText, replaceIfCurrentIn) {
        const targetEl = document.getElementById(targetId);
        if (!targetEl) return Promise.resolve();

        return fetch(selectedEndpoint)
            .then(response => response.json())
            .then(selected => {
                const selectedId = Number(selected[selectedKey]);
                return fetch(catalogEndpoint)
                    .then(response => response.json())
                    .then(data => {
                        const types = Array.isArray(data.types) ? data.types : [];
                        const match = types.find(t => Number(t.id) === selectedId);
                        const label = match ? String(match.name) : fallbackText;

                        const current = (targetEl.textContent || '').trim();
                        const allowed = Array.isArray(replaceIfCurrentIn) ? replaceIfCurrentIn : null;
                        if (!allowed || allowed.includes(current)) {
                            targetEl.textContent = label;
                        }
                    });
            })
            .catch(() => {
                targetEl.textContent = unavailableText;
            });
    }

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
        loadLabel(
            '/api/get_inverter_types',
            '/api/get_selected_types',
            'inverter_type',
            'inverterProtocolValue',
            'Unknown',
            'Unknown',
            ['Unknown']
        );
    });

    loadLabel(
        '/api/get_inverter_interfaces',
        '/api/get_selected_interfaces',
        'inverter_interface',
        'inverterInterfaceValue',
        'Unknown',
        'Unavailable',
        null
    );
});
)";
}
