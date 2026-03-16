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
            redirectDelayMs: 1500,
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
)rawliteral";

// Generate standard HTML page with common template
String generatePage(const String& title, const String& content, const String& extraStyles, const String& script) {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='utf-8'>";
    html += "<title>" + title + "</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>" + String(COMMON_STYLES) + extraStyles + "</style>";
    html += "<script>" + String(COMMON_SCRIPT_HELPERS) + script + "</script>";
    html += "</head><body>";
    html += content;
    html += "</body></html>";
    return html;
}
