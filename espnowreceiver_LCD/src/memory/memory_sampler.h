#pragma once

#include <freertos/FreeRTOS.h>
#include <cstddef>
#include <cstdint>

/**
 * @file memory_sampler.h
 * @brief Lightweight periodic heap health sampler for internal RAM and PSRAM.
 *
 * Captures free bytes, largest free block, and fragmentation estimate for
 * both MALLOC_CAP_INTERNAL and MALLOC_CAP_SPIRAM at a configurable cadence.
 *
 * Sampling rates:
 *   - Baseline: every 30 s (normal operation)
 *   - Burst:    every 1 s  (active SSE/OTA/API stress windows)
 *
 * Threshold warnings are emitted via LOG_WARN when sampled values breach
 * configured limits.
 * The ring buffer stores the last SAMPLE_RING_SIZE samples for diagnostics.
 * All ring buffer access is mutex-protected for multi-task safety.
 */

namespace MemorySampler {

// ── Ring buffer capacity ────────────────────────────────────────────────────
static constexpr size_t SAMPLE_RING_SIZE = 20;

// ── Warning thresholds ──────────────────────────────────────────────────────
static constexpr uint32_t INTERNAL_LARGEST_WARN_THRESHOLD = 32u * 1024u;   // 32 KB
static constexpr uint32_t PSRAM_LARGEST_WARN_THRESHOLD    = 128u * 1024u;  // 128 KB
static constexpr float    FRAG_EST_WARN_THRESHOLD          = 0.70f;

// ── Sample structure ────────────────────────────────────────────────────────
typedef struct {
    uint32_t timestamp_ms;
    uint32_t internal_free;
    uint32_t internal_largest;
    float    internal_frag_est;
    uint32_t psram_free;
    uint32_t psram_largest;
    float    psram_frag_est;
} memory_sample_t;

/**
 * @brief FreeRTOS task entry point for the memory sampler.
 *        Launched by RuntimeTaskStartup::start_runtime_tasks().
 */
void task_memory_sampler(void* parameter);

/**
 * @brief Activate or deactivate burst-mode sampling directly.
 *        Prefer burst_clients_add / burst_clients_release for ref-counted use.
 */
void set_burst_mode(bool active);

/**
 * @brief Increment the shared burst-mode reference count.
 *        Activates burst sampling (1 s cadence) when the count goes from 0 → 1.
 *        Call when an SSE client connects or an OTA upload begins.
 */
void burst_clients_add();

/**
 * @brief Decrement the shared burst-mode reference count.
 *        Deactivates burst sampling when the count reaches 0.
 *        Call when an SSE client disconnects or an OTA upload completes/aborts.
 */
void burst_clients_release();

/**
 * @brief Copy the ring buffer samples (oldest-first) under mutex lock.
 * @param out       Caller-allocated array of at least max_count entries.
 * @param max_count Maximum entries to copy.
 * @return          Number of entries actually copied.
 */
size_t copy_samples(memory_sample_t* out, size_t max_count);

/**
 * @brief Get the most recent sample under mutex lock.
 * @param out  Pointer to caller-allocated sample_t to fill.
 * @return     true if a sample exists and was written to *out.
 */
bool get_latest(memory_sample_t* out);

} // namespace MemorySampler
