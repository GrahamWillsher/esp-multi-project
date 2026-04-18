#include "memory_sampler.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/portmacro.h>

#include <logging_config.h>

namespace MemorySampler {

// ── Sampling cadence ────────────────────────────────────────────────────────
static constexpr uint32_t BASELINE_INTERVAL_MS = 30000u;
static constexpr uint32_t BURST_INTERVAL_MS    = 1000u;

// ── Ring buffer state ────────────────────────────────────────────────────────
static memory_sample_t    s_ring[SAMPLE_RING_SIZE];
static size_t             s_head  = 0;
static size_t             s_count = 0;
static SemaphoreHandle_t  s_mutex = nullptr;

// ── Burst mode flag (written from external tasks) ───────────────────────────
static volatile bool     s_burst_mode      = false;

// ── Burst-mode reference counter (counts active stress clients) ──────────────
static volatile uint32_t s_burst_ref_count = 0u;
static portMUX_TYPE      s_burst_mux       = portMUX_INITIALIZER_UNLOCKED;

// ── Internal helpers ─────────────────────────────────────────────────────────

static float calc_frag_est(uint32_t free_bytes, uint32_t largest_block) {
    if (free_bytes == 0) return 0.0f;
    return 1.0f - (static_cast<float>(largest_block) / static_cast<float>(free_bytes));
}

static void take_and_store_sample() {
    memory_sample_t sample;
    sample.timestamp_ms      = millis();
    sample.internal_free     = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    sample.internal_largest  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    sample.internal_frag_est = calc_frag_est(sample.internal_free, sample.internal_largest);
    sample.psram_free        = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    sample.psram_largest     = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    sample.psram_frag_est    = calc_frag_est(sample.psram_free, sample.psram_largest);

    // Threshold warnings — emit on every crossing so degradation trends are visible in logs.
    if (sample.internal_largest < INTERNAL_LARGEST_WARN_THRESHOLD) {
        LOG_WARN("MEM_SAMPLER",
                 "Internal largest free block LOW: %lu bytes (threshold: %lu)",
                 static_cast<unsigned long>(sample.internal_largest),
                 static_cast<unsigned long>(INTERNAL_LARGEST_WARN_THRESHOLD));
    }
    if (sample.psram_free > 0 && sample.psram_largest < PSRAM_LARGEST_WARN_THRESHOLD) {
        LOG_WARN("MEM_SAMPLER",
                 "PSRAM largest free block LOW: %lu bytes (threshold: %lu)",
                 static_cast<unsigned long>(sample.psram_largest),
                 static_cast<unsigned long>(PSRAM_LARGEST_WARN_THRESHOLD));
    }
    if (sample.internal_frag_est > FRAG_EST_WARN_THRESHOLD) {
        LOG_WARN("MEM_SAMPLER",
                 "Internal heap fragmentation HIGH: %.2f (threshold: %.2f)",
                 sample.internal_frag_est, FRAG_EST_WARN_THRESHOLD);
    }
    if (sample.psram_free > 0 && sample.psram_frag_est > FRAG_EST_WARN_THRESHOLD) {
        LOG_WARN("MEM_SAMPLER",
                 "PSRAM fragmentation HIGH: %.2f (threshold: %.2f)",
                 sample.psram_frag_est, FRAG_EST_WARN_THRESHOLD);
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_ring[s_head] = sample;
        s_head = (s_head + 1) % SAMPLE_RING_SIZE;
        if (s_count < SAMPLE_RING_SIZE) {
            s_count++;
        }
        xSemaphoreGive(s_mutex);
    }
}

// ── Public API ───────────────────────────────────────────────────────────────

void task_memory_sampler(void* parameter) {
    (void)parameter;

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == nullptr) {
        LOG_ERROR("MEM_SAMPLER", "Failed to create ring buffer mutex — task exiting");
        vTaskDelete(nullptr);
        return;
    }

    LOG_INFO("MEM_SAMPLER", "Started (baseline=%lums burst=%lums ring=%zu)",
             static_cast<unsigned long>(BASELINE_INTERVAL_MS),
             static_cast<unsigned long>(BURST_INTERVAL_MS),
             SAMPLE_RING_SIZE);

    for (;;) {
        take_and_store_sample();
        const uint32_t interval = s_burst_mode ? BURST_INTERVAL_MS : BASELINE_INTERVAL_MS;
        vTaskDelay(pdMS_TO_TICKS(interval));
    }
}

void set_burst_mode(bool active) {
    s_burst_mode = active;
}

void burst_clients_add() {
    uint32_t prev;
    portENTER_CRITICAL(&s_burst_mux);
    prev = s_burst_ref_count++;
    portEXIT_CRITICAL(&s_burst_mux);
    if (prev == 0u) {
        s_burst_mode = true;
        LOG_DEBUG("MEM_SAMPLER", "Burst mode ON (ref_count=%lu)",
                  static_cast<unsigned long>(s_burst_ref_count));
    }
}

void burst_clients_release() {
    uint32_t curr;
    portENTER_CRITICAL(&s_burst_mux);
    if (s_burst_ref_count > 0u) { --s_burst_ref_count; }
    curr = s_burst_ref_count;
    portEXIT_CRITICAL(&s_burst_mux);
    if (curr == 0u) {
        s_burst_mode = false;
        LOG_DEBUG("MEM_SAMPLER", "Burst mode OFF (ref_count=0)");
    }
}

size_t copy_samples(memory_sample_t* out, size_t max_count) {
    if (s_mutex == nullptr || out == nullptr || max_count == 0) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;

    const size_t total   = s_count;
    const size_t to_copy = (total < max_count) ? total : max_count;

    // Oldest entry is at (s_head - total) mod SAMPLE_RING_SIZE when buffer is full.
    for (size_t i = 0; i < to_copy; i++) {
        const size_t src_idx = (s_head + SAMPLE_RING_SIZE - total + i) % SAMPLE_RING_SIZE;
        out[i] = s_ring[src_idx];
    }

    xSemaphoreGive(s_mutex);
    return to_copy;
}

bool get_latest(memory_sample_t* out) {
    if (s_mutex == nullptr || out == nullptr) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;

    const bool has_sample = (s_count > 0);
    if (has_sample) {
        const size_t latest_idx = (s_head + SAMPLE_RING_SIZE - 1) % SAMPLE_RING_SIZE;
        *out = s_ring[latest_idx];
    }

    xSemaphoreGive(s_mutex);
    return has_sample;
}

} // namespace MemorySampler
