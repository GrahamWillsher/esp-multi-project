#include "page_generator.h"
#include "common_styles.h"

#include <cstring>

static const char* COMMON_SCRIPT_HELPERS = R"rawliteral(
window.TransmitterReboot = window.TransmitterReboot || {
    COUNTDOWN_SECONDS: 10,

    run(options) {
        const cfg = Object.assign({
            countdownSeconds: 10,
            rebootEndpoint: '/api/reboot',
            redirectUrl: '/',
            redirectDelayMs: 3000,
            shouldRedirect: true,
            updateCountdown: null,
            onCommandStart: null,
            onSuccess: null,
            onFailure: null,
            onError: null
        }, options || {});

        let seconds = Number.isFinite(cfg.countdownSeconds)
            ? cfg.countdownSeconds
            : this.COUNTDOWN_SECONDS;

        const tick = () => {
            if (typeof cfg.updateCountdown === 'function') {
                cfg.updateCountdown(seconds);
            }

            if (seconds <= 0) {
                if (typeof cfg.onCommandStart === 'function') {
                    cfg.onCommandStart();
                }

                fetch(cfg.rebootEndpoint)
                    .then(response => response.json())
                    .then(data => {
                        if (data.success) {
                            if (typeof cfg.onSuccess === 'function') {
                                cfg.onSuccess(data);
                            }
                            if (cfg.shouldRedirect) {
                                setTimeout(() => {
                                    window.location.href = cfg.redirectUrl;
                                }, cfg.redirectDelayMs);
                            }
                        } else {
                            if (typeof cfg.onFailure === 'function') {
                                cfg.onFailure(data.message || 'Unknown error', data);
                            }
                        }
                    })
                    .catch(error => {
                        if (typeof cfg.onError === 'function') {
                            cfg.onError(error);
                        }
                    });
                return;
            }

            seconds--;
            setTimeout(tick, 1000);
        };

        tick();
    }
};

window.SaveOperation = window.SaveOperation || {
    setButtonState(button, options) {
        if (!button) return;

        const cfg = Object.assign({
            text: button.textContent,
            backgroundColor: button.style.backgroundColor,
            disabled: button.disabled,
            cursor: button.style.cursor
        }, options || {});

        if (typeof cfg.text === 'string') {
            button.textContent = cfg.text;
        }
        if (typeof cfg.backgroundColor === 'string') {
            button.style.backgroundColor = cfg.backgroundColor;
        }
        if (typeof cfg.disabled === 'boolean') {
            button.disabled = cfg.disabled;
        }
        if (typeof cfg.cursor === 'string') {
            button.style.cursor = cfg.cursor;
        }
    },

    restoreAfter(callback, delayMs) {
        if (typeof callback === 'function') {
            setTimeout(callback, delayMs || 3000);
        }
    },

    showSuccess(button, text, restoreButton, delayMs) {
        this.setButtonState(button, {
            text: text || '✓ Saved',
            backgroundColor: '#28a745',
            disabled: true,
            cursor: 'not-allowed'
        });
        this.restoreAfter(restoreButton, delayMs);
    },

    showError(button, text, restoreButton, delayMs) {
        this.setButtonState(button, {
            text: text || '✗ Save Failed',
            backgroundColor: '#dc3545',
            disabled: true,
            cursor: 'not-allowed'
        });
        this.restoreAfter(restoreButton, delayMs);
    },

    runSequential(options) {
        const cfg = Object.assign({
            items: [],
            saveButton: null,
            delayBetweenMs: 100,
            stopOnFailure: true,
            onItemStart: null,
            executeItem: null,
            onItemSuccess: null,
            onItemFailure: null,
            onComplete: null,
            onError: null
        }, options || {});

        if (typeof cfg.executeItem !== 'function') {
            throw new Error('executeItem is required');
        }

        let index = 0;
        let successCount = 0;
        let failureCount = 0;

        const finish = () => {
            if (typeof cfg.onComplete === 'function') {
                cfg.onComplete({
                    successCount,
                    failureCount,
                    totalCount: cfg.items.length
                });
            }
        };

        const runNext = () => {
            if (index >= cfg.items.length) {
                finish();
                return;
            }

            const currentIndex = index;
            const item = cfg.items[currentIndex];
            index++;

            if (typeof cfg.onItemStart === 'function') {
                cfg.onItemStart(item, currentIndex, cfg.items.length, cfg.saveButton);
            }

            Promise.resolve(cfg.executeItem(item, currentIndex))
                .then(result => {
                    if (result && result.success) {
                        successCount++;
                        if (typeof cfg.onItemSuccess === 'function') {
                            cfg.onItemSuccess(item, result, currentIndex, cfg.saveButton);
                        }
                        setTimeout(runNext, cfg.delayBetweenMs);
                        return;
                    }

                    failureCount++;
                    if (typeof cfg.onItemFailure === 'function') {
                        cfg.onItemFailure(item, result || {}, currentIndex, cfg.saveButton);
                    }

                    if (cfg.stopOnFailure) {
                        finish();
                    } else {
                        setTimeout(runNext, cfg.delayBetweenMs);
                    }
                })
                .catch(error => {
                    failureCount++;

                    if (typeof cfg.onError === 'function') {
                        cfg.onError(item, error, currentIndex, cfg.saveButton);
                    }

                    if (cfg.stopOnFailure) {
                        finish();
                    } else {
                        setTimeout(runNext, cfg.delayBetweenMs);
                    }
                });
        };

        runNext();
    },

    runComponentApply(options) {
        const cfg = Object.assign({
            endpoint: '/api/component_apply',
            payload: null,
            saveButton: null,
            restoreButton: null,
            restoreDelayMs: 3000,
            onReadyForReboot: null,
            onApplyRequested: null,
            onRequestError: null
        }, options || {});

        if (!cfg.payload) {
            throw new Error('payload is required');
        }

        this.setButtonState(cfg.saveButton, {
            text: 'Waiting for transmitter confirmation...',
            backgroundColor: '#ff9800',
            disabled: true,
            cursor: 'not-allowed'
        });

        fetch(cfg.endpoint, {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(cfg.payload)
        })
            .then(response => response.json())
            .then(data => {
                if (!data.success) {
                    throw new Error(data.error || 'Component apply request failed');
                }

                if (typeof cfg.onApplyRequested === 'function') {
                    cfg.onApplyRequested(data);
                }

                ComponentApplyCoordinator.waitForReadyAndReboot({
                    requestId: data.request_id,
                    saveButton: cfg.saveButton,
                    onReadyForReboot: cfg.onReadyForReboot,
                    restoreButton: cfg.restoreButton,
                    restoreDelayMs: cfg.restoreDelayMs
                });
            })
            .catch(error => {
                if (typeof cfg.onRequestError === 'function') {
                    cfg.onRequestError(error, cfg.saveButton);
                } else {
                    this.showError(cfg.saveButton, '✗ Save Failed', cfg.restoreButton, cfg.restoreDelayMs);
                }
            });
    }
};

window.CatalogLoader = window.CatalogLoader || {
    setLoadingOption(selectEl, text) {
        if (!selectEl) return;
        selectEl.innerHTML = `<option value=''>${text || 'Loading...'}</option>`;
    },

    setEmptyOption(selectEl, text) {
        if (!selectEl) return;
        selectEl.innerHTML = `<option value=''>${text || 'No data available'}</option>`;
    },

    populateSelect(selectEl, types) {
        if (!selectEl) return;
        selectEl.innerHTML = '';
        (types || []).forEach(type => {
            const option = document.createElement('option');
            option.value = type.id;
            option.textContent = type.name;
            selectEl.appendChild(option);
        });
    },

    loadCatalogSelect(options) {
        const cfg = Object.assign({
            catalogEndpoint: '',
            selectedEndpoint: '',
            selectedKey: '',
            selectId: '',
            loadingText: 'Loading...',
            emptyText: 'No data available',
            maxRetries: 0,
            retryDelayMs: 1000,
            onCatalogLoaded: null,
            onSelectedLoaded: null,
            onError: null,
            logLabel: 'catalog'
        }, options || {});

        const selectEl = document.getElementById(cfg.selectId);
        if (!selectEl) {
            return Promise.resolve(null);
        }

        let attempts = 0;
        this.setLoadingOption(selectEl, cfg.loadingText);

        const loadCatalog = () => {
            return fetch(cfg.catalogEndpoint)
                .then(response => response.json())
                .then(data => {
                    const types = Array.isArray(data.types) ? data.types : [];
                    if (data.loading || types.length === 0) {
                        this.setLoadingOption(selectEl, cfg.loadingText);
                        if (attempts < cfg.maxRetries) {
                            attempts++;
                            setTimeout(loadCatalog, cfg.retryDelayMs);
                        } else {
                            this.setEmptyOption(selectEl, cfg.emptyText);
                        }
                        return null;
                    }

                    this.populateSelect(selectEl, types);
                    if (typeof cfg.onCatalogLoaded === 'function') {
                        cfg.onCatalogLoaded(types, data, selectEl);
                    }

                    if (!cfg.selectedEndpoint || !cfg.selectedKey) {
                        return types;
                    }

                    return fetch(cfg.selectedEndpoint)
                        .then(response => response.json())
                        .then(selected => {
                            const selectedValue = String(selected[cfg.selectedKey]);
                            selectEl.value = selectedValue;
                            if (typeof cfg.onSelectedLoaded === 'function') {
                                cfg.onSelectedLoaded(selectedValue, selected, selectEl, types);
                            }
                            return types;
                        });
                })
                .catch(error => {
                    if (typeof cfg.onError === 'function') {
                        cfg.onError(error, selectEl);
                    } else {
                        console.error(`Failed to load ${cfg.logLabel}:`, error);
                    }
                    return null;
                });
        };

        return loadCatalog();
    },

    loadCatalogLabel(options) {
        const cfg = Object.assign({
            catalogEndpoint: '',
            selectedEndpoint: '',
            selectedKey: '',
            targetId: '',
            fallbackText: 'Unknown',
            unavailableText: 'Unavailable',
            replaceIfCurrentIn: null,
            onResolved: null,
            onError: null,
            logLabel: 'catalog label'
        }, options || {});

        const targetEl = document.getElementById(cfg.targetId);
        if (!targetEl) {
            return Promise.resolve(null);
        }

        return fetch(cfg.selectedEndpoint)
            .then(response => response.json())
            .then(selected => {
                const selectedId = selected[cfg.selectedKey];
                return fetch(cfg.catalogEndpoint)
                    .then(response => response.json())
                    .then(data => {
                        const types = Array.isArray(data.types) ? data.types : [];
                        const match = types.find(t => t.id === selectedId);
                        const label = match ? `${match.name}` : cfg.fallbackText;
                        const current = (targetEl.textContent || '').trim();
                        const allowed = Array.isArray(cfg.replaceIfCurrentIn)
                            ? cfg.replaceIfCurrentIn
                            : null;

                        if (!allowed || allowed.includes(current)) {
                            targetEl.textContent = label;
                        }

                        if (typeof cfg.onResolved === 'function') {
                            cfg.onResolved(label, selected, targetEl, types);
                        }

                        return label;
                    });
            })
            .catch(error => {
                if (targetEl) {
                    targetEl.textContent = cfg.unavailableText;
                }
                if (typeof cfg.onError === 'function') {
                    cfg.onError(error, targetEl);
                } else {
                    console.error(`Failed to load ${cfg.logLabel}:`, error);
                }
                return null;
            });
    }
};

window.FormChangeTracker = window.FormChangeTracker || {
    getElementValue(element) {
        if (!element) return undefined;
        return element.type === 'checkbox' ? element.checked : element.value;
    },

    countChanges(initialValues, fieldIds) {
        let changes = 0;
        (fieldIds || []).forEach(fieldId => {
            const element = document.getElementById(fieldId);
            if (!element) return;
            const currentValue = this.getElementValue(element);
            if (initialValues[fieldId] !== currentValue) {
                changes++;
            }
        });
        return changes;
    },

    updateSaveButton(button, changedCount, options) {
        if (!button) return;

        const cfg = Object.assign({
            nothingText: 'Nothing to Save',
            changedSingularTemplate: 'Save 1 Change',
            changedPluralTemplate: 'Save {count} Changes',
            enabledColor: '#4CAF50',
            disabledColor: '#6c757d'
        }, options || {});

        if (changedCount > 0) {
            const text = changedCount === 1
                ? cfg.changedSingularTemplate
                : cfg.changedPluralTemplate.replace('{count}', String(changedCount));
            SaveOperation.setButtonState(button, {
                text,
                backgroundColor: cfg.enabledColor,
                disabled: false,
                cursor: 'pointer'
            });
        } else {
            SaveOperation.setButtonState(button, {
                text: cfg.nothingText,
                backgroundColor: cfg.disabledColor,
                disabled: true,
                cursor: 'not-allowed'
            });
        }
    }
};

window.ComponentApplyCoordinator = window.ComponentApplyCoordinator || {
    POLL_INTERVAL_MS: 1000,
    MAX_POLL_ATTEMPTS: 40,

    waitForReadyAndReboot(options) {
        const cfg = Object.assign({
            requestId: '',
            statusEndpoint: '/api/component_apply_status',
            saveButton: null,
            pollIntervalMs: this.POLL_INTERVAL_MS,
            maxPollAttempts: this.MAX_POLL_ATTEMPTS,
            rebootDelaySeconds: TransmitterReboot.COUNTDOWN_SECONDS,
            restoreDelayMs: 3000,
            restoreButton: null,
            onReadyForReboot: null
        }, options || {});

        if (!cfg.requestId) {
            throw new Error('requestId is required');
        }

        const saveButton = cfg.saveButton;

        const scheduleRestore = () => {
            if (typeof cfg.restoreButton === 'function') {
                setTimeout(() => cfg.restoreButton(), cfg.restoreDelayMs);
            }
        };

        const markError = (text) => {
            if (saveButton) {
                saveButton.textContent = text;
                saveButton.style.backgroundColor = '#dc3545';
            }
            scheduleRestore();
        };

        let statusPollAttempts = 0;
        const statusPollInterval = setInterval(() => {
            statusPollAttempts++;

            fetch(
                cfg.statusEndpoint + '?_=' + Date.now(),
                { cache: 'no-store' }
            )
                .then(response => response.json())
                .then(status => {
                    if (status.success && status.ready_for_reboot) {
                        clearInterval(statusPollInterval);

                        if (typeof cfg.onReadyForReboot === 'function') {
                            cfg.onReadyForReboot(status);
                        }

                        TransmitterReboot.run({
                            countdownSeconds: cfg.rebootDelaySeconds,
                            updateCountdown: (seconds) => {
                                if (!saveButton) return;
                                saveButton.disabled = true;
                                saveButton.style.cursor = 'not-allowed';
                                saveButton.style.backgroundColor = '#ff9800';
                                saveButton.textContent = `Reboot in ${seconds}s...`;
                            },
                            onCommandStart: () => {
                                if (!saveButton) return;
                                saveButton.textContent = 'Sending reboot command...';
                            },
                            onSuccess: () => {
                                if (!saveButton) return;
                                saveButton.textContent = '✓ Reboot command sent';
                                saveButton.style.backgroundColor = '#28a745';
                            },
                            onFailure: () => {
                                markError('✗ Reboot failed');
                            },
                            onError: (error) => {
                                console.error('Reboot request failed:', error);
                                markError('✗ Reboot request error');
                            }
                        });
                    } else if (status.success && status.in_progress) {
                        if (saveButton) {
                            saveButton.textContent = (status.message || 'Applying changes... please wait');
                        }
                    } else if (status.success && !status.in_progress && !status.ready_for_reboot && status.last_success === false) {
                        clearInterval(statusPollInterval);
                        markError('✗ Save Failed');
                    } else if (!status.success) {
                        clearInterval(statusPollInterval);
                        markError(status.message || '✗ Status check failed');
                    } else {
                        if (saveButton) {
                            saveButton.textContent = (status.message || 'Waiting for transmitter confirmation...');
                        }
                    }
                })
                .catch(() => {
                    // Ignore transient poll errors
                });

            if (statusPollAttempts >= cfg.maxPollAttempts) {
                clearInterval(statusPollInterval);
                markError('✗ Confirmation timed out');
            }
        }, cfg.pollIntervalMs);
    }
};

window.ReceiverNetworkFormController = window.ReceiverNetworkFormController || {
    setOctets(prefix, value) {
        if (!value || typeof value !== 'string') return;
        const parts = value.split('.');
        if (parts.length !== 4) return;
        for (let i = 0; i < 4; i++) {
            const el = document.getElementById(prefix + i);
            if (el) el.value = parts[i];
        }
    },

    collectOctets(prefix) {
        const parts = [];
        for (let i = 0; i < 4; i++) {
            const el = document.getElementById(prefix + i);
            parts.push((el && el.value) || '0');
        }
        return parts.join('.');
    },

    updateNetworkModeBadge(useStatic, badgeId) {
        const badge = document.getElementById(badgeId || 'networkModeBadge');
        if (!badge) return;
        badge.textContent = useStatic ? 'Static IP' : 'DHCP';
        badge.className = useStatic ? 'network-mode-badge badge-static' : 'network-mode-badge badge-dhcp';
    },

    toggleStaticIpFields(useStatic, rowIds) {
        (rowIds || []).forEach(rowId => {
            const row = document.getElementById(rowId);
            if (!row) return;
            row.style.display = useStatic ? 'grid' : 'none';
            row.querySelectorAll('input').forEach(input => {
                input.disabled = !useStatic;
            });
        });
    }
};
)rawliteral";

// Generate standard HTML page with common template
String renderPage(const String& title, const String& content, const PageRenderOptions& options) {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='utf-8'>";
    html += "<title>" + title + "</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    // Prevent browser favicon.ico request/404 noise by using an inline empty icon.
    html += "<link rel='icon' href='data:,'>";
    html += "<style>" + String(COMMON_STYLES) + options.extra_styles + "</style>";
    if (options.include_common_script_helpers) {
        html += "<script>" + String(COMMON_SCRIPT_HELPERS) + options.script + "</script>";
    } else {
        html += "<script>" + options.script + "</script>";
    }
    html += "</head><body>";
    html += content;
    html += "</body></html>";
    return html;
}

esp_err_t send_rendered_page(httpd_req_t* req,
                             const String& title,
                             const String& content,
                             const PageRenderOptions& options,
                             const char* content_type) {
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_resp_set_type(req, content_type ? content_type : "text/html");

    const auto send_chunk = [&](const char* data, size_t len) -> bool {
        return len == 0 || (httpd_resp_send_chunk(req, data, len) == ESP_OK);
    };

    static const char kDocHeadStart[] =
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<title>";
    if (!send_chunk(kDocHeadStart, sizeof(kDocHeadStart) - 1)) return ESP_FAIL;
    if (!send_chunk(title.c_str(), title.length())) return ESP_FAIL;

    static const char kDocHeadMiddle[] =
        "</title>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<link rel='icon' href='data:,'>"
        "<style>";
    if (!send_chunk(kDocHeadMiddle, sizeof(kDocHeadMiddle) - 1)) return ESP_FAIL;
    if (!send_chunk(COMMON_STYLES, sizeof(COMMON_STYLES) - 1)) return ESP_FAIL;

    const char* extra_styles_data = options.extra_styles_static ? options.extra_styles_static : options.extra_styles.c_str();
    const size_t extra_styles_len = options.extra_styles_static ? strlen(options.extra_styles_static) : options.extra_styles.length();
    if (!send_chunk(extra_styles_data, extra_styles_len)) return ESP_FAIL;

    static const char kStyleCloseScriptOpen[] = "</style><script>";
    if (!send_chunk(kStyleCloseScriptOpen, sizeof(kStyleCloseScriptOpen) - 1)) return ESP_FAIL;

    if (options.include_common_script_helpers) {
        if (!send_chunk(COMMON_SCRIPT_HELPERS, strlen(COMMON_SCRIPT_HELPERS))) return ESP_FAIL;
    }

    const char* script_data = options.script_static ? options.script_static : options.script.c_str();
    const size_t script_len = options.script_static ? strlen(options.script_static) : options.script.length();
    if (!send_chunk(script_data, script_len)) return ESP_FAIL;

    static const char kBodyOpen[] = "</script></head><body>";
    if (!send_chunk(kBodyOpen, sizeof(kBodyOpen) - 1)) return ESP_FAIL;
    if (!send_chunk(content.c_str(), content.length())) return ESP_FAIL;

    static const char kDocClose[] = "</body></html>";
    if (!send_chunk(kDocClose, sizeof(kDocClose) - 1)) return ESP_FAIL;

    return httpd_resp_send_chunk(req, nullptr, 0);
}
// Non-allocating overload — const char* title and content avoid String heap allocation
// for fully-static page bodies (e.g. cellmonitor, event logs static content).
esp_err_t send_rendered_page(httpd_req_t* req,
                             const char* title,
                             const char* content,
                             const PageRenderOptions& options,
                             const char* content_type) {
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_resp_set_type(req, content_type ? content_type : "text/html");

    const auto send_chunk = [&](const char* data, size_t len) -> bool {
        return len == 0 || (httpd_resp_send_chunk(req, data, len) == ESP_OK);
    };

    static const char kDocHeadStart[] =
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<title>";
    if (!send_chunk(kDocHeadStart, sizeof(kDocHeadStart) - 1)) return ESP_FAIL;
    if (!send_chunk(title, strlen(title))) return ESP_FAIL;

    static const char kDocHeadMiddle[] =
        "</title>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<link rel='icon' href='data:,'>"
        "<style>";
    if (!send_chunk(kDocHeadMiddle, sizeof(kDocHeadMiddle) - 1)) return ESP_FAIL;
    if (!send_chunk(COMMON_STYLES, sizeof(COMMON_STYLES) - 1)) return ESP_FAIL;

    const char* extra_styles_data = options.extra_styles_static ? options.extra_styles_static : options.extra_styles.c_str();
    const size_t extra_styles_len = options.extra_styles_static ? strlen(options.extra_styles_static) : options.extra_styles.length();
    if (!send_chunk(extra_styles_data, extra_styles_len)) return ESP_FAIL;

    static const char kStyleCloseScriptOpen[] = "</style><script>";
    if (!send_chunk(kStyleCloseScriptOpen, sizeof(kStyleCloseScriptOpen) - 1)) return ESP_FAIL;

    if (options.include_common_script_helpers) {
        if (!send_chunk(COMMON_SCRIPT_HELPERS, strlen(COMMON_SCRIPT_HELPERS))) return ESP_FAIL;
    }

    const char* script_data = options.script_static ? options.script_static : options.script.c_str();
    const size_t script_len = options.script_static ? strlen(options.script_static) : options.script.length();
    if (!send_chunk(script_data, script_len)) return ESP_FAIL;

    static const char kBodyOpen[] = "</script></head><body>";
    if (!send_chunk(kBodyOpen, sizeof(kBodyOpen) - 1)) return ESP_FAIL;
    if (!send_chunk(content, strlen(content))) return ESP_FAIL;

    static const char kDocClose[] = "</body></html>";
    if (!send_chunk(kDocClose, sizeof(kDocClose) - 1)) return ESP_FAIL;

    return httpd_resp_send_chunk(req, nullptr, 0);
}