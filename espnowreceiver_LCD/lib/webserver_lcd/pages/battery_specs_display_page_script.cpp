#include "battery_specs_display_page_script.h"

String get_battery_specs_page_inline_script() {
    return R"(
window.addEventListener('load', () => {
    function loadLabel(catalogEndpoint, selectedEndpoint, selectedKey, targetId, fallbackText, unavailableText, replaceIfCurrentIn) {
        const targetEl = document.getElementById(targetId);
        if (!targetEl) return;

        fetch(selectedEndpoint)
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

    loadLabel(
        '/api/get_battery_types',
        '/api/get_selected_types',
        'battery_type',
        'batteryTypeValue',
        'Unknown',
        'Unknown',
        ['Unknown', 'TEST_DUMMY']
    );

    loadLabel(
        '/api/get_battery_interfaces',
        '/api/get_selected_interfaces',
        'battery_interface',
        'batteryInterfaceValue',
        'Unknown',
        'Unavailable',
        null
    );
});
)";
}
