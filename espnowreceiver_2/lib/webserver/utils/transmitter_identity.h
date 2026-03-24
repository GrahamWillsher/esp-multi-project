#ifndef TRANSMITTER_IDENTITY_H
#define TRANSMITTER_IDENTITY_H

#include <Arduino.h>
#include <stdint.h>

/**
 * Consolidated transmitter identity module.
 * 
 * Combines functionality from:
 * - transmitter_identity_cache (MAC registration & formatting)
 * - transmitter_active_mac_resolver (MAC resolution priority)
 * - transmitter_mac_registration (registration workflow)
 * - transmitter_mac_query_helper (query convenience functions)
 * 
 * Provides unified interface for MAC address tracking and retrieval.
 */

namespace TransmitterIdentity {

// ===== Registration (from transmitter_mac_registration) =====

/**
 * Register a transmitter MAC address.
 * Called when a probe or metadata packet arrives.
 * Triggers SSE notification and peer registry update.
 */
void register_mac(const uint8_t* transmitter_mac);

// ===== Cache Management (from transmitter_identity_cache) =====

/**
 * Store MAC in internal cache.
 * Called by register_mac(); also used during initialization.
 */
void cache_mac(const uint8_t* mac);

/**
 * Get the currently registered MAC.
 * Returns nullptr if no MAC has been registered yet.
 */
const uint8_t* get_registered_mac();

/**
 * Check if a MAC has been registered.
 */
bool has_registered_mac();

// ===== Resolution (from transmitter_active_mac_resolver) =====

/**
 * Get the active MAC address with resolution priority:
 * 1. Registered MAC (from probe packet) - takes precedence
 * 2. Runtime MAC from ESP-NOW (transmitter_mac global)
 * 3. nullptr if no MAC available
 */
const uint8_t* get_active_mac();

// ===== Formatting (from transmitter_identity_cache) =====

/**
 * Format MAC address to human-readable string.
 * Format: "XX:XX:XX:XX:XX:XX"
 * Returns "Unknown" if mac is nullptr.
 */
String format_mac(const uint8_t* mac);

// ===== Query Helpers (from transmitter_mac_query_helper) =====

/**
 * Check if active MAC is known.
 * Convenience wrapper for (get_active_mac() != nullptr).
 */
bool is_mac_known();

/**
 * Get formatted active MAC as String.
 * Returns "Unknown" if no MAC is available.
 */
String get_mac_string();

} // namespace TransmitterIdentity

#endif // TRANSMITTER_IDENTITY_H
