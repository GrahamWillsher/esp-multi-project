#include "page_generator.h"
#include "common_styles.h"
#include "../logging.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
const char* resolve_theme_body_class(const char* uri) {
    if (uri && std::strncmp(uri, "/systemtools", 12) == 0) {
        return "theme-systemtools";
    }
    if (uri && std::strncmp(uri, "/receiver", 9) == 0) {
        return "theme-receiver";
    }
    return "theme-transmitter";
}

bool should_inject_dashboard_nav(const char* uri, const PageRenderOptions& options) {
    if (!options.include_template_dashboard_nav) {
        return false;
    }

    // Do not show a dashboard button on the dashboard itself.
    return !(uri && std::strcmp(uri, "/") == 0);
}

const char* dashboard_nav_html() {
    return "<div class='template-top-nav'><a href='/' class='button dashboard-link'>← Dashboard</a></div>";
}

constexpr uint32_t kHttpLongResponseWarnMs = 2000;
constexpr uint32_t kMinInternalHeapPreflightBytes = 20U * 1024U;
constexpr uint32_t kMinInternalHeapMidRenderBytesCritical = 4U * 1024U;
constexpr uint32_t kMinInternalHeapMidRenderBytesNormalWarn = 8U * 1024U;

struct ChunkSendPolicy {
    size_t chunk_bytes = 512;
    uint32_t inter_chunk_delay_ms = 1;
};

ChunkSendPolicy policy_for_sample(uint32_t free_heap, uint32_t largest_block) {
    ChunkSendPolicy policy{};
    policy.chunk_bytes = 512;
    policy.inter_chunk_delay_ms = 1;
    if ((free_heap < 16U * 1024U) || (largest_block < 4U * 1024U)) {
        if (policy.chunk_bytes > 192) policy.chunk_bytes = 192;
        if (policy.inter_chunk_delay_ms < 5) policy.inter_chunk_delay_ms = 5;
    }
    return policy;
}

const char* method_to_string(int method) {
    switch (method) {
        case HTTP_GET:    return "GET";
        case HTTP_POST:   return "POST";
        case HTTP_PUT:    return "PUT";
        case HTTP_PATCH:  return "PATCH";
        case HTTP_DELETE: return "DELETE";
        case HTTP_HEAD:   return "HEAD";
        default:          return "UNKNOWN";
    }
}

const char* wifi_mode_to_string(wifi_mode_t mode) {
    switch (mode) {
        case WIFI_MODE_STA:   return "STA";
        case WIFI_MODE_AP:    return "AP";
        case WIFI_MODE_APSTA: return "APSTA";
        case WIFI_MODE_NULL:  return "NULL";
        default:              return "UNKNOWN";
    }
}

void log_page_send_summary(httpd_req_t* req,
                           const char* title,
                           size_t total_bytes,
                           size_t total_chunks,
                           uint32_t started_ms,
                           esp_err_t final_rc,
                           const char* failure_stage,
                           size_t failed_chunk_index,
                           size_t failed_chunk_offset,
                           size_t failed_chunk_len) {
    const uint32_t elapsed_ms = millis() - started_ms;
    const uint32_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t min_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const wifi_mode_t wifi_mode = WiFi.getMode();
    const int wifi_channel = static_cast<int>(WiFi.channel());

    if (final_rc == ESP_OK) {
        if (elapsed_ms >= kHttpLongResponseWarnMs) {
            LOG_WARN("HTTP_PAGE",
                     "render slow uri=%s method=%s title=%s dur=%lu ms bytes=%lu chunks=%lu wifi_mode=%s sta=%s ch=%d heap=%lu min_heap=%lu largest=%lu",
                     req && req->uri ? req->uri : "<unknown>",
                     req ? method_to_string(req->method) : "UNKNOWN",
                     title ? title : "<unknown>",
                     static_cast<unsigned long>(elapsed_ms),
                     static_cast<unsigned long>(total_bytes),
                     static_cast<unsigned long>(total_chunks),
                     wifi_mode_to_string(wifi_mode),
                     (WiFi.status() == WL_CONNECTED) ? "up" : "down",
                     wifi_channel,
                     static_cast<unsigned long>(free_heap),
                     static_cast<unsigned long>(min_heap),
                     static_cast<unsigned long>(largest_block));
        } else {
            LOG_INFO("HTTP_PAGE",
                     "render ok uri=%s method=%s title=%s dur=%lu ms bytes=%lu chunks=%lu wifi_mode=%s sta=%s ch=%d heap=%lu min_heap=%lu largest=%lu",
                     req && req->uri ? req->uri : "<unknown>",
                     req ? method_to_string(req->method) : "UNKNOWN",
                     title ? title : "<unknown>",
                     static_cast<unsigned long>(elapsed_ms),
                     static_cast<unsigned long>(total_bytes),
                     static_cast<unsigned long>(total_chunks),
                     wifi_mode_to_string(wifi_mode),
                     (WiFi.status() == WL_CONNECTED) ? "up" : "down",
                     wifi_channel,
                     static_cast<unsigned long>(free_heap),
                     static_cast<unsigned long>(min_heap),
                     static_cast<unsigned long>(largest_block));
        }
        return;
    }

    LOG_ERROR("HTTP_PAGE",
              "render fail uri=%s method=%s title=%s rc=%d (%s) stage=%s chunk=%lu off=%lu len=%lu dur=%lu ms bytes=%lu chunks=%lu wifi_mode=%s sta=%s ch=%d heap=%lu min_heap=%lu largest=%lu",
              req && req->uri ? req->uri : "<unknown>",
              req ? method_to_string(req->method) : "UNKNOWN",
              title ? title : "<unknown>",
              static_cast<int>(final_rc),
              esp_err_to_name(final_rc),
              failure_stage ? failure_stage : "<none>",
              static_cast<unsigned long>(failed_chunk_index),
              static_cast<unsigned long>(failed_chunk_offset),
              static_cast<unsigned long>(failed_chunk_len),
              static_cast<unsigned long>(elapsed_ms),
              static_cast<unsigned long>(total_bytes),
              static_cast<unsigned long>(total_chunks),
              wifi_mode_to_string(wifi_mode),
              (WiFi.status() == WL_CONNECTED) ? "up" : "down",
              wifi_channel,
              static_cast<unsigned long>(free_heap),
              static_cast<unsigned long>(min_heap),
              static_cast<unsigned long>(largest_block));
}

esp_err_t send_chunk_stage(httpd_req_t* req,
                           const char* title,
                           const char* stage,
                           const char* data,
                           size_t len,
                           uint32_t started_ms,
                           size_t& total_bytes_sent,
                           size_t& total_chunks_sent,
                           const char*& failure_stage,
                           size_t& failed_chunk_index,
                           size_t& failed_chunk_offset,
                           size_t& failed_chunk_len) {
    if (len == 0) return ESP_OK;

    size_t offset = 0;
    size_t chunk_index = 0;
    while (offset < len) {
        const uint32_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const uint32_t min_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const uint32_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const bool critically_low = free_heap < kMinInternalHeapMidRenderBytesCritical;

        if (free_heap < kMinInternalHeapMidRenderBytesNormalWarn) {
            LOG_WARN("HTTP_PAGE",
                     "render low heap sample uri=%s title=%s stage=%s heap=%lu min_heap=%lu largest=%lu floor_warn_normal=%lu",
                     req && req->uri ? req->uri : "<unknown>",
                     title ? title : "<unknown>",
                     stage ? stage : "<unknown>",
                     static_cast<unsigned long>(free_heap),
                     static_cast<unsigned long>(min_heap),
                     static_cast<unsigned long>(largest_block),
                     static_cast<unsigned long>(kMinInternalHeapMidRenderBytesNormalWarn));
        }

        if (critically_low) {
            failure_stage = stage;
            failed_chunk_index = chunk_index;
            failed_chunk_offset = offset;
            failed_chunk_len = 0;
            LOG_WARN("HTTP_PAGE",
                     "render abort uri=%s title=%s stage=%s reason=low_internal_heap heap=%lu min_heap=%lu largest=%lu floor_critical=%lu",
                     req && req->uri ? req->uri : "<unknown>",
                     title ? title : "<unknown>",
                     stage ? stage : "<unknown>",
                     static_cast<unsigned long>(free_heap),
                     static_cast<unsigned long>(min_heap),
                     static_cast<unsigned long>(largest_block),
                     static_cast<unsigned long>(kMinInternalHeapMidRenderBytesCritical));
            if (total_chunks_sent == 0) {
                httpd_resp_set_status(req, "503 Service Unavailable");
                (void)httpd_resp_sendstr(req, "Server busy");
            }
            log_page_send_summary(req, title, total_bytes_sent, total_chunks_sent, started_ms,
                                  ESP_ERR_NO_MEM, failure_stage, failed_chunk_index,
                                  failed_chunk_offset, failed_chunk_len);
            return ESP_ERR_NO_MEM;
        }

        const ChunkSendPolicy policy = policy_for_sample(free_heap, largest_block);
        const size_t part_len = (len - offset > policy.chunk_bytes) ? policy.chunk_bytes : (len - offset);
        if (policy.inter_chunk_delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(policy.inter_chunk_delay_ms));
        }
        const esp_err_t rc = httpd_resp_send_chunk(req, data + offset, part_len);
        if (rc != ESP_OK) {
            failure_stage = stage;
            failed_chunk_index = chunk_index;
            failed_chunk_offset = offset;
            failed_chunk_len = part_len;
            log_page_send_summary(req, title, total_bytes_sent, total_chunks_sent, started_ms,
                                  rc, failure_stage, failed_chunk_index,
                                  failed_chunk_offset, failed_chunk_len);
            return rc;
        }
        offset += part_len;
        ++chunk_index;
        ++total_chunks_sent;
        total_bytes_sent += part_len;
    }
    return ESP_OK;
}
}

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

esp_err_t send_rendered_page(httpd_req_t* req,
                             const String& title,
                             const String& content,
                             const PageRenderOptions& options,
                             const char* content_type) {
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    // Heap preflight: refuse before sending anything if heap is critically low.
    {
        const uint32_t pre_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (pre_heap < kMinInternalHeapPreflightBytes) {
            LOG_WARN("HTTP_PAGE",
                     "render refused uri=%s title=%s reason=preflight_low_heap heap=%lu floor=%lu",
                     req->uri ? req->uri : "<unknown>",
                     title.c_str(),
                     static_cast<unsigned long>(pre_heap),
                     static_cast<unsigned long>(kMinInternalHeapPreflightBytes));
            httpd_resp_set_status(req, "503 Service Unavailable");
            return httpd_resp_sendstr(req, "Server busy");
        }
    }

    httpd_resp_set_type(req, content_type ? content_type : "text/html");
    // Section 10.D: Cache static pages aggressively — browser avoids re-download on revisit.
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");

    const uint32_t started_ms = millis();
    size_t total_bytes_sent = 0;
    size_t total_chunks_sent = 0;
    const char* failure_stage = nullptr;
    size_t failed_chunk_index = 0;
    size_t failed_chunk_offset = 0;
    size_t failed_chunk_len = 0;
    const char* title_cstr = title.c_str();

    const auto chunk = [&](const char* stage, const char* data, size_t len) -> esp_err_t {
        return send_chunk_stage(req, title_cstr, stage, data, len, started_ms,
                                total_bytes_sent, total_chunks_sent, failure_stage,
                                failed_chunk_index, failed_chunk_offset, failed_chunk_len);
    };

    static const char kDocHeadStart[] =
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<title>";
    esp_err_t rc;
    if ((rc = chunk("head-start", kDocHeadStart, sizeof(kDocHeadStart) - 1)) != ESP_OK) return rc;
    if ((rc = chunk("title", title.c_str(), title.length())) != ESP_OK) return rc;

    static const char kDocHeadMiddle[] =
        "</title>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<link rel='icon' href='data:,'>"
        "<style>";
    if ((rc = chunk("head-middle", kDocHeadMiddle, sizeof(kDocHeadMiddle) - 1)) != ESP_OK) return rc;
    if ((rc = chunk("common-styles", COMMON_STYLES, sizeof(COMMON_STYLES) - 1)) != ESP_OK) return rc;

    const char* extra_styles_data = options.extra_styles_static ? options.extra_styles_static : options.extra_styles.c_str();
    const size_t extra_styles_len = options.extra_styles_static ? strlen(options.extra_styles_static) : options.extra_styles.length();
    if ((rc = chunk("extra-styles", extra_styles_data, extra_styles_len)) != ESP_OK) return rc;

    static const char kStyleCloseScriptOpen[] = "</style><script>";
    if ((rc = chunk("style-close", kStyleCloseScriptOpen, sizeof(kStyleCloseScriptOpen) - 1)) != ESP_OK) return rc;

    if (options.include_common_script_helpers) {
        if ((rc = chunk("script-helpers", COMMON_SCRIPT_HELPERS, strlen(COMMON_SCRIPT_HELPERS))) != ESP_OK) return rc;
    }

    const char* script_data = options.script_static ? options.script_static : options.script.c_str();
    const size_t script_len = options.script_static ? strlen(options.script_static) : options.script.length();
    if ((rc = chunk("script", script_data, script_len)) != ESP_OK) return rc;

    static const char kBodyOpenPrefix[] = "</script></head><body class='";
    if ((rc = chunk("body-open", kBodyOpenPrefix, sizeof(kBodyOpenPrefix) - 1)) != ESP_OK) return rc;
    const char* theme_class = resolve_theme_body_class(req->uri);
    if ((rc = chunk("theme-class", theme_class, strlen(theme_class))) != ESP_OK) return rc;
    static const char kBodyOpenSuffix[] = "'>";
    if ((rc = chunk("body-open-suffix", kBodyOpenSuffix, sizeof(kBodyOpenSuffix) - 1)) != ESP_OK) return rc;
    if (should_inject_dashboard_nav(req->uri, options)) {
        const char* nav_html = dashboard_nav_html();
        if ((rc = chunk("nav", nav_html, strlen(nav_html))) != ESP_OK) return rc;
    }
    if ((rc = chunk("content", content.c_str(), content.length())) != ESP_OK) return rc;

    static const char kDocClose[] = "</body></html>";
    if ((rc = chunk("doc-close", kDocClose, sizeof(kDocClose) - 1)) != ESP_OK) return rc;

    rc = httpd_resp_send_chunk(req, nullptr, 0);
    log_page_send_summary(req, title_cstr, total_bytes_sent, total_chunks_sent, started_ms,
                          rc, failure_stage, failed_chunk_index, failed_chunk_offset, failed_chunk_len);
    return rc;
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

    // Heap preflight: refuse before sending anything if heap is critically low.
    {
        const uint32_t pre_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (pre_heap < kMinInternalHeapPreflightBytes) {
            LOG_WARN("HTTP_PAGE",
                     "render refused uri=%s title=%s reason=preflight_low_heap heap=%lu floor=%lu",
                     req->uri ? req->uri : "<unknown>",
                     title ? title : "<unknown>",
                     static_cast<unsigned long>(pre_heap),
                     static_cast<unsigned long>(kMinInternalHeapPreflightBytes));
            httpd_resp_set_status(req, "503 Service Unavailable");
            return httpd_resp_sendstr(req, "Server busy");
        }
    }

    httpd_resp_set_type(req, content_type ? content_type : "text/html");
    // Section 10.D: Cache static pages aggressively — browser avoids re-download on revisit.
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");

    const uint32_t started_ms = millis();
    size_t total_bytes_sent = 0;
    size_t total_chunks_sent = 0;
    const char* failure_stage = nullptr;
    size_t failed_chunk_index = 0;
    size_t failed_chunk_offset = 0;
    size_t failed_chunk_len = 0;

    const auto chunk = [&](const char* stage, const char* data, size_t len) -> esp_err_t {
        return send_chunk_stage(req, title, stage, data, len, started_ms,
                                total_bytes_sent, total_chunks_sent, failure_stage,
                                failed_chunk_index, failed_chunk_offset, failed_chunk_len);
    };

    static const char kDocHeadStart[] =
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<title>";
    esp_err_t rc;
    if ((rc = chunk("head-start", kDocHeadStart, sizeof(kDocHeadStart) - 1)) != ESP_OK) return rc;
    if ((rc = chunk("title", title, title ? strlen(title) : 0)) != ESP_OK) return rc;

    static const char kDocHeadMiddle[] =
        "</title>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<link rel='icon' href='data:,'>"
        "<style>";
    if ((rc = chunk("head-middle", kDocHeadMiddle, sizeof(kDocHeadMiddle) - 1)) != ESP_OK) return rc;
    if ((rc = chunk("common-styles", COMMON_STYLES, sizeof(COMMON_STYLES) - 1)) != ESP_OK) return rc;

    const char* extra_styles_data = options.extra_styles_static ? options.extra_styles_static : options.extra_styles.c_str();
    const size_t extra_styles_len = options.extra_styles_static ? strlen(options.extra_styles_static) : options.extra_styles.length();
    if ((rc = chunk("extra-styles", extra_styles_data, extra_styles_len)) != ESP_OK) return rc;

    static const char kStyleCloseScriptOpen[] = "</style><script>";
    if ((rc = chunk("style-close", kStyleCloseScriptOpen, sizeof(kStyleCloseScriptOpen) - 1)) != ESP_OK) return rc;

    if (options.include_common_script_helpers) {
        if ((rc = chunk("script-helpers", COMMON_SCRIPT_HELPERS, strlen(COMMON_SCRIPT_HELPERS))) != ESP_OK) return rc;
    }

    const char* script_data = options.script_static ? options.script_static : options.script.c_str();
    const size_t script_len = options.script_static ? strlen(options.script_static) : options.script.length();
    if ((rc = chunk("script", script_data, script_len)) != ESP_OK) return rc;

    static const char kBodyOpenPrefix[] = "</script></head><body class='";
    if ((rc = chunk("body-open", kBodyOpenPrefix, sizeof(kBodyOpenPrefix) - 1)) != ESP_OK) return rc;
    const char* theme_class = resolve_theme_body_class(req->uri);
    if ((rc = chunk("theme-class", theme_class, strlen(theme_class))) != ESP_OK) return rc;
    static const char kBodyOpenSuffix[] = "'>";
    if ((rc = chunk("body-open-suffix", kBodyOpenSuffix, sizeof(kBodyOpenSuffix) - 1)) != ESP_OK) return rc;
    if (should_inject_dashboard_nav(req->uri, options)) {
        const char* nav_html = dashboard_nav_html();
        if ((rc = chunk("nav", nav_html, strlen(nav_html))) != ESP_OK) return rc;
    }
    if ((rc = chunk("content", content, content ? strlen(content) : 0)) != ESP_OK) return rc;

    static const char kDocClose[] = "</body></html>";
    if ((rc = chunk("doc-close", kDocClose, sizeof(kDocClose) - 1)) != ESP_OK) return rc;

    rc = httpd_resp_send_chunk(req, nullptr, 0);
    log_page_send_summary(req, title, total_bytes_sent, total_chunks_sent, started_ms,
                          rc, failure_stage, failed_chunk_index, failed_chunk_offset, failed_chunk_len);
    return rc;
}