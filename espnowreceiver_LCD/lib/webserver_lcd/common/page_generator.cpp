#include "page_generator.h"
#include "common_styles.h"
#include "../logging.h"
#include "../webserver.h"

#include <cstring>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <WiFi.h>

namespace {
constexpr uint32_t kHttpLongResponseWarnMs = 2000;
constexpr uint32_t kMinInternalHeapMidRenderBytesCritical = 4U * 1024U;
constexpr uint32_t kMinInternalHeapMidRenderBytesNormalWarn = 8U * 1024U;

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

struct ChunkSendPolicy {
    size_t chunk_bytes = 512;
    uint32_t inter_chunk_delay_ms = 1;
};

struct ActiveRenderContext {
    httpd_req_t* req = nullptr;
    const char* title = nullptr;
    uint32_t started_ms = 0;
    size_t* total_bytes_sent = nullptr;
    size_t* total_chunks_sent = nullptr;
    const char** failure_stage = nullptr;
    size_t* failed_chunk_index = nullptr;
    size_t* failed_chunk_offset = nullptr;
    size_t* failed_chunk_len = nullptr;
};

ActiveRenderContext* g_active_render_context = nullptr;

ChunkSendPolicy policy_for_sample(uint32_t free_heap,
                                  uint32_t largest_block) {
    ChunkSendPolicy policy{};

    policy.chunk_bytes = 512;
    policy.inter_chunk_delay_ms = 1;

    // Additional guard under low internal heap or small largest block.
    if ((free_heap < 16U * 1024U) || (largest_block < 4U * 1024U)) {
        if (policy.chunk_bytes > 192) {
            policy.chunk_bytes = 192;
        }
        if (policy.inter_chunk_delay_ms < 5) {
            policy.inter_chunk_delay_ms = 5;
        }
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
    if (len == 0) {
        return ESP_OK;
    }

    size_t offset = 0;
    size_t chunk_index = 0;
    while (offset < len) {
        const uint32_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const uint32_t min_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const uint32_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

        const bool critically_low_internal_heap = free_heap < kMinInternalHeapMidRenderBytesCritical;

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

        if (critically_low_internal_heap) {
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

            log_page_send_summary(req,
                                  title,
                                  total_bytes_sent,
                                  total_chunks_sent,
                                  started_ms,
                                  ESP_ERR_NO_MEM,
                                  failure_stage,
                                  failed_chunk_index,
                                  failed_chunk_offset,
                                  failed_chunk_len);
            return ESP_ERR_NO_MEM;
        }

        const ChunkSendPolicy chunk_policy = policy_for_sample(free_heap, largest_block);
        const size_t part_len = (len - offset > chunk_policy.chunk_bytes)
                                    ? chunk_policy.chunk_bytes
                                    : (len - offset);
        webserver_on_request_progress(millis());
        if (chunk_policy.inter_chunk_delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(chunk_policy.inter_chunk_delay_ms));
        }
        const esp_err_t rc = httpd_resp_send_chunk(req, data + offset, part_len);
        if (rc != ESP_OK) {
            failure_stage = stage;
            failed_chunk_index = chunk_index;
            failed_chunk_offset = offset;
            failed_chunk_len = part_len;
            log_page_send_summary(req,
                                  title,
                                  total_bytes_sent,
                                  total_chunks_sent,
                                  started_ms,
                                  rc,
                                  failure_stage,
                                  failed_chunk_index,
                                  failed_chunk_offset,
                                  failed_chunk_len);
            return rc;
        }

        offset += part_len;
        ++chunk_index;
        ++total_chunks_sent;
        total_bytes_sent += part_len;
    }

    return ESP_OK;
}

esp_err_t send_page_content_chunk_impl(httpd_req_t* req,
                                       const char* stage,
                                       const char* data,
                                       size_t len) {
    if (!req || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return ESP_OK;
    }

    ActiveRenderContext* active = g_active_render_context;
    if (active != nullptr && active->req == req &&
        active->total_bytes_sent != nullptr &&
        active->total_chunks_sent != nullptr &&
        active->failure_stage != nullptr &&
        active->failed_chunk_index != nullptr &&
        active->failed_chunk_offset != nullptr &&
        active->failed_chunk_len != nullptr) {
        const char* safe_stage = (stage && stage[0] != '\0') ? stage : "content";
        return send_chunk_stage(req,
                                active->title,
                                safe_stage,
                                data,
                                len,
                                active->started_ms,
                                *active->total_bytes_sent,
                                *active->total_chunks_sent,
                                *active->failure_stage,
                                *active->failed_chunk_index,
                                *active->failed_chunk_offset,
                                *active->failed_chunk_len);
    }

    // Fallback path for non-page-render contexts.
    return httpd_resp_send_chunk(req, data, len);
}
}

esp_err_t send_page_content_chunk(httpd_req_t* req,
                                  const char* stage,
                                  const char* data,
                                  size_t len) {
    return send_page_content_chunk_impl(req, stage, data, len);
}

static const char kCommonScriptHelpers[] = R"rawliteral(
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

// Streaming render: callback-based content generation (Section 1: fully fixed solution)
// Allows handlers to emit content incrementally without building the full body in a String.
esp_err_t send_rendered_page_streaming(httpd_req_t* req,
									   const char* title,
									   page_content_generator_t content_generator,
									   const PageRenderOptions& options,
									   const char* content_type) {
    if (!req || !title || !content_generator) {
        return ESP_ERR_INVALID_ARG;
    }

    // Phase 3: Preflight heap check — pressure-aware policy per design Section 6
    // Section 6 Remediation (2026-05-14):
    //   - NORMAL pressure: Allow requests (logging warnings is handled in mid-render)
    //   - Critically low internal heap (< 4KB): Refuse immediately (genuine memory exhaustion)
    {
        constexpr uint32_t kCriticalHeapThreshold = 4U * 1024U;
        const uint32_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

        // Only reject if heap is critically low
        if (free_heap < kCriticalHeapThreshold) {
            LOG_WARN("HTTP_PAGE",
                     "preflight_reject uri=%s heap=%lu critical_threshold=%lu",
                     req->uri ? req->uri : "<unknown>",
                     static_cast<unsigned long>(free_heap),
                     static_cast<unsigned long>(kCriticalHeapThreshold));
            httpd_resp_set_status(req, "503 Service Unavailable");
            static const char kBusy[] = "Server busy (heap critical)";
            (void)httpd_resp_sendstr(req, kBusy);
            return ESP_FAIL;
        }
    }

    httpd_resp_set_type(req, content_type ? content_type : "text/html");
    httpd_resp_set_hdr(req, "Connection", "close");

    const uint32_t started_ms = millis();
    // Phase 4: Request accounting start
    webserver_on_request_start(started_ms);
    size_t total_bytes_sent = 0;
    size_t total_chunks_sent = 0;
    const char* failure_stage = nullptr;
    size_t failed_chunk_index = 0;
    size_t failed_chunk_offset = 0;
    size_t failed_chunk_len = 0;

    LOG_INFO("HTTP_PAGE", "render_streaming start uri=%s method=%s title=%s (callback-based, no pre-built content)",
             req->uri ? req->uri : "<unknown>",
             method_to_string(req->method),
             title);

    const auto send_chunk = [&](const char* stage, const char* data, size_t len) -> bool {
        const esp_err_t rc = send_chunk_stage(req,
                                              title,
                                              stage,
                                              data,
                                              len,
                                              started_ms,
                                              total_bytes_sent,
                                              total_chunks_sent,
                                              failure_stage,
                                              failed_chunk_index,
                                              failed_chunk_offset,
                                              failed_chunk_len);
        return rc == ESP_OK;
    };

    // Send HTML head (same as before)
    static const char kDocHeadStart[] =
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<title>";
    if (!send_chunk("doc_head_start", kDocHeadStart, sizeof(kDocHeadStart) - 1)) return ESP_FAIL;
    if (!send_chunk("title", title, strlen(title))) return ESP_FAIL;

    static const char kDocHeadMiddle[] =
        "</title>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<link rel='icon' href='data:,'>"
        "<style>";
    if (!send_chunk("doc_head_middle", kDocHeadMiddle, sizeof(kDocHeadMiddle) - 1)) return ESP_FAIL;
    if (!send_chunk("common_styles", COMMON_STYLES, sizeof(COMMON_STYLES) - 1)) return ESP_FAIL;

    const char* extra_styles_data = options.extra_styles_static ? options.extra_styles_static : options.extra_styles.c_str();
    const size_t extra_styles_len = options.extra_styles_static ? strlen(options.extra_styles_static) : options.extra_styles.length();
    if (!send_chunk("extra_styles", extra_styles_data, extra_styles_len)) return ESP_FAIL;

    // Phase 2: COMMON_SCRIPT_HELPERS served as an external cacheable resource at /static/helpers.js
    // (previously sent inline — was the largest single-chunk allocation, ~12 KB)
    if (options.include_common_script_helpers) {
        static const char kHelpersRef[] = "</style><script src='/static/helpers.js'></script><script>";
        if (!send_chunk("helpers_ref", kHelpersRef, sizeof(kHelpersRef) - 1)) return ESP_FAIL;
    } else {
        static const char kStyleCloseScriptOpen[] = "</style><script>";
        if (!send_chunk("style_close_script_open", kStyleCloseScriptOpen, sizeof(kStyleCloseScriptOpen) - 1)) return ESP_FAIL;
    }

    const char* script_data = options.script_static ? options.script_static : options.script.c_str();
    const size_t script_len = options.script_static ? strlen(options.script_static) : options.script.length();
    if (!send_chunk("script", script_data, script_len)) return ESP_FAIL;

    static const char kBodyOpenPrefix[] = "</script></head><body class='";
    if (!send_chunk("body_open_prefix", kBodyOpenPrefix, sizeof(kBodyOpenPrefix) - 1)) return ESP_FAIL;
    const char* theme_class = resolve_theme_body_class(req->uri);
    if (!send_chunk("body_open_class", theme_class, strlen(theme_class))) return ESP_FAIL;
    static const char kBodyOpenSuffix[] = "'>";
    if (!send_chunk("body_open_suffix", kBodyOpenSuffix, sizeof(kBodyOpenSuffix) - 1)) return ESP_FAIL;

    if (should_inject_dashboard_nav(req->uri, options)) {
        const char* nav_html = dashboard_nav_html();
        if (!send_chunk("template_dashboard_nav", nav_html, strlen(nav_html))) return ESP_FAIL;
    }

    // Invoke the callback to emit page content incrementally
    // The callback should use the same send_chunk_stage path for safety checks and abort protection
    ActiveRenderContext active_context{};
    active_context.req = req;
    active_context.title = title;
    active_context.started_ms = started_ms;
    active_context.total_bytes_sent = &total_bytes_sent;
    active_context.total_chunks_sent = &total_chunks_sent;
    active_context.failure_stage = &failure_stage;
    active_context.failed_chunk_index = &failed_chunk_index;
    active_context.failed_chunk_offset = &failed_chunk_offset;
    active_context.failed_chunk_len = &failed_chunk_len;

    g_active_render_context = &active_context;
    LOG_INFO("HTTP_PAGE", "render_streaming invoking content callback");
    const esp_err_t callback_rc = content_generator(req);
    if (g_active_render_context == &active_context) {
        g_active_render_context = nullptr;
    }
    if (callback_rc != ESP_OK) {
        LOG_WARN("HTTP_PAGE", "render_streaming content callback returned %d", static_cast<int>(callback_rc));
        failure_stage = "content_generator_callback";
        log_page_send_summary(req,
                              title,
                              total_bytes_sent,
                              total_chunks_sent,
                              started_ms,
                              callback_rc,
                              failure_stage,
                              failed_chunk_index,
                              failed_chunk_offset,
                              failed_chunk_len);
        webserver_on_request_end(millis(), millis() - started_ms, false);
        return callback_rc;
    }

    static const char kDocClose[] = "</body></html>";
    if (!send_chunk("doc_close", kDocClose, sizeof(kDocClose) - 1)) return ESP_FAIL;

    const esp_err_t tail_rc = httpd_resp_send_chunk(req, nullptr, 0);
    if (tail_rc != ESP_OK) {
        failure_stage = "chunk_terminator";
        log_page_send_summary(req,
                              title,
                              total_bytes_sent,
                              total_chunks_sent,
                              started_ms,
                              tail_rc,
                              failure_stage,
                              total_chunks_sent,
                              total_bytes_sent,
                              0);
        webserver_on_request_end(millis(), millis() - started_ms, false);
        return tail_rc;
    }

    log_page_send_summary(req,
                          title,
                          total_bytes_sent,
                          total_chunks_sent,
                          started_ms,
                          ESP_OK,
                          nullptr,
                          0,
                          0,
                          0);
    // Phase 4: Request accounting end
    webserver_on_request_end(millis(), millis() - started_ms, true);
    return ESP_OK;
}

// Phase 2: /static/helpers.js — serves kCommonScriptHelpers with long-lived cache header
// so browsers only fetch it once per session, eliminating the ~12 KB inline send.
static esp_err_t helpers_js_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400, immutable");
    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_send(req, kCommonScriptHelpers, sizeof(kCommonScriptHelpers) - 1);
}

esp_err_t register_static_helpers_js(httpd_handle_t server) {
    static const httpd_uri_t uri = {
        .uri      = "/static/helpers.js",
        .method   = HTTP_GET,
        .handler  = helpers_js_handler,
        .user_ctx = nullptr
    };
    return httpd_register_uri_handler(server, &uri);
}