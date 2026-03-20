#include "page_generator.h"
#include "common_styles.h"

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
)rawliteral";

// Generate standard HTML page with common template
String renderPage(const String& title, const String& content, const PageRenderOptions& options) {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='utf-8'>";
    html += "<title>" + title + "</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>" + String(COMMON_STYLES) + options.extra_styles + "</style>";
    html += "<script>" + String(COMMON_SCRIPT_HELPERS) + options.script + "</script>";
    html += "</head><body>";
    html += content;
    html += "</body></html>";
    return html;
}
