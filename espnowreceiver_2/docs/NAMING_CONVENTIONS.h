#ifndef NAMING_CONVENTIONS_H
#define NAMING_CONVENTIONS_H

/**
 * @file naming_conventions.h
 * @brief Coding standards and naming conventions for the ESP-NOW Receiver codebase
 * 
 * This file documents the naming conventions and coding standards used throughout
 * the receiver codebase to maintain consistency and improve readability.
 * 
 * @author Graham Willsher
 * @date March 5, 2026
 */

namespace NamingConventions {

    /**
     * @section Naming Conventions
     * 
     * The receiver codebase uses the following naming conventions to ensure
     * consistency and improve code readability:
     * 
     * ## Variables
     * - **Local variables**: `lower_snake_case`
     *   ```cpp
     *   int current_soc = 0;
     *   uint8_t transmitter_mac[6];
     *   bool is_connected = false;
     *   ```
     *
     * - **Member variables**: `lower_snake_case_` (trailing underscore)
     *   ```cpp
     *   class ConnectionState {
     *   private:
     *       bool transmitter_connected_;
     *       uint8_t received_soc_;
     *       uint32_t last_update_time_ms_;
     *   };
     *   ```
     *
     * - **Static variables**: `s_lower_snake_case` or module namespace
     *   ```cpp
     *   static int s_display_counter = 0;
     *   static uint16_t s_color_cache[256];
     *   ```
     *
     * - **Global variables**: Avoid when possible. If necessary, use clear names in namespace.
     *   ```cpp
     *   namespace Display {
     *       extern int16_t tft_background;
     *       extern uint8_t current_backlight_brightness;
     *   }
     *   ```
     *
     * ## Constants
     * - **Compile-time constants**: `UPPER_SNAKE_CASE`
     *   ```cpp
     *   constexpr uint16_t DISPLAY_WIDTH = 320;
     *   constexpr uint32_t BOOT_TIMEOUT_MS = 30000;
     *   #define MAX_RETRIES 5
     *   ```
     *
     * - **Named constants in namespaces**: Also `UPPER_SNAKE_CASE` or descriptive lower_snake_case
     *   ```cpp
     *   namespace DisplayConfig {
     *       constexpr uint16_t DISPLAY_WIDTH = 320;
     *       constexpr uint16_t COLOR_BACKGROUND = 0x0000;
     *   }
     *   ```
     *
     * ## Functions
     * - **Regular functions**: `lower_snake_case`
     *   ```cpp
     *   void initialize_display();
     *   uint8_t get_soc_value();
     *   bool is_transmitter_connected();
     *   void reset_connection_timeout();
     *   ```
     *
     * - **Getter methods**: `get_<property>()` or `is_<condition>()`
     *   ```cpp
     *   uint8_t get_state_of_charge() const;
     *   uint32_t get_last_update_time_ms() const;
     *   bool is_data_stale(uint32_t timeout_ms) const;
     *   bool has_transmitter_connection() const;
     *   ```
     *
     * - **Setter methods**: `set_<property>()`
     *   ```cpp
     *   void set_state_of_charge(uint8_t soc);
     *   void set_transmitter_connected(bool connected);
     *   ```
     *
     * - **Predicate methods**: `is_<condition>()` or `has_<thing>()`
     *   ```cpp
     *   bool is_initialized() const;
     *   bool has_data_available() const;
     *   bool is_connection_stale() const;
     *   ```
     *
     * - **Action methods**: `<verb>_<object>()`
     *   ```cpp
     *   void reset_connection();
     *   void force_update();
     *   void clear_cache();
     *   void flush_buffer();
     *   ```
     *
     * - **Private methods**: Same as public, no underscore prefix
     *   ```cpp
     *   private:
     *       void update_display_();  // DO NOT use trailing underscore
     *       void check_timeout_();
     *   ```
     *
     * ## Classes
     * - **Class names**: `PascalCase`
     *   ```cpp
     *   class ConnectionState { };
     *   class DisplayManager { };
     *   class StateManager { };
     *   class TftDisplay { };
     *   ```
     *
     * - **Abstract base classes**: Prefix with `I` (Interface)
     *   ```cpp
     *   class IDisplayDriver { };
     *   class IEventHandler { };
     *   ```
     *
     * - **Exception classes**: Suffix with `Exception`
     *   ```cpp
     *   class TimeoutException : public std::exception { };
     *   ```
     *
     * ## Enums
     * - **Enum names**: `PascalCase`
     *   ```cpp
     *   enum class SystemState {
     *       BOOTING,
     *       WAITING_FOR_TRANSMITTER,
     *       NORMAL_OPERATION,
     *       ERROR_STATE
     *   };
     *   ```
     *
     * - **Enum values**: `UPPER_SNAKE_CASE`
     *   ```cpp
     *   enum class LEDColor {
     *       LED_RED = 0,
     *       LED_GREEN = 1,
     *       LED_ORANGE = 2
     *   };
     *   ```
     *
     * ## Namespaces
     * - **Namespace names**: `lower_snake_case` or `PascalCase` (project preference: snake_case)
     *   ```cpp
     *   namespace connection_state { }
     *   namespace display_config { }
     *   namespace api_handlers { }
     *   ```
     *
     * - **Namespaces for related components**: Group logically
     *   ```cpp
     *   namespace Connection {
     *       enum class State { };
     *       class Manager { };
     *   }
     *   ```
     *
     * ## Macros
     * - **All uppercase with underscores**: `UPPER_SNAKE_CASE`
     *   ```cpp
     *   #define LED_FADE_STEPS 50
     *   #define MAX_CONNECTION_RETRIES 3
     *   #define BUFFER_SIZE_BYTES 1024
     *   ```
     *
     * - **Avoid magic numbers**: Use named constants instead
     *   ```cpp
     *   // BAD
     *   if (timeout > 5000) { ... }
     *   
     *   // GOOD
     *   constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 5000;
     *   if (timeout > HEARTBEAT_TIMEOUT_MS) { ... }
     *   ```
     *
     * ## File Names
     * - **Source files**: `lower_snake_case.cpp`
     * - **Header files**: `lower_snake_case.h`
     * - **Test files**: `lower_snake_case_test.cpp`
     *   ```
     *   connection_state_manager.h
     *   connection_state_manager.cpp
     *   display_config.h
     *   state_machine.cpp
     *   ```
     *
     * ## Abbreviations
     * - **Avoid single-letter abbreviations** in variable names (except loop counters `i`, `j`)
     *   ```cpp
     *   // BAD
     *   uint8_t soc;  // State of charge - unclear
     *   bool tx_active;  // Transmitter - abbreviation
     *   uint16_t rx_count;  // Receiver - abbreviation
     *   
     *   // GOOD
     *   uint8_t state_of_charge;
     *   bool transmitter_active;
     *   uint16_t receiver_count;
     *   ```
     *
     * - **Common accepted abbreviations** (use full form in most cases):
     *   - `ms` = milliseconds (time suffix)
     *   - `Hz` = frequency
     *   - `V` = voltage
     *   - `W` = watts
     *   - `A` = amperes
     *   - `id` = identifier (common in APIs)
     *   - `max` / `min` = maximum / minimum
     *
     * ## Comments & Documentation
     * - **Function documentation**: Use Doxygen format
     *   ```cpp
     *   /**
     *    * @brief Brief description of what function does
     *    *
     *    * @param param1 Description of first parameter
     *    * @param param2 Description of second parameter
     *    * @return Description of return value
     *    * @note Any special notes or warnings
     *    */
     *   void some_function(int param1, float param2);
     *   ```
     *
     * - **Inline comments**: Explain the "why", not the "what"
     *   ```cpp
     *   // BAD
     *   x++;  // Increment x
     *   
     *   // GOOD
     *   timeout++;  // Check again after timeout period
     *   ```
     *
     * ## Boolean Variables
     * - Start with `is_`, `has_`, `can_`, or `should_`
     *   ```cpp
     *   bool is_connected = false;
     *   bool has_data = false;
     *   bool can_transmit = true;
     *   bool should_retry = true;
     *   ```
     *
     * - **Never use negative names for booleans**
     *   ```cpp
     *   // BAD
     *   bool is_not_connected = true;
     *   bool no_data = true;
     *   
     *   // GOOD
     *   bool is_connected = false;
     *   bool has_data = false;
     *   ```
     *
     * ## Pointers & References
     * - **Pointer declaration style**: Space before `*`
     *   ```cpp
     *   uint8_t* mac_address;
     *   const char* name;
     *   ```
     *
     * - **Reference declaration style**: Space before `&`
     *   ```cpp
     *   const ConnectionState& state;
     *   uint8_t& data_buffer;
     *   ```
     *
     * ## Const Correctness
     * - Use `const` for parameters that won't be modified
     * - Mark const methods appropriately
     *   ```cpp
     *   uint8_t get_soc() const;
     *   void set_soc(const uint8_t new_soc);
     *   void handle_data(const uint8_t* data, size_t length);
     *   ```
     *
     * @section Rationale
     * 
     * These conventions were chosen to:
     * 1. **Clarity**: Names should clearly indicate purpose and scope
     * 2. **Consistency**: Same construct always named the same way
     * 3. **Searchability**: Easy to find all instances with grep/IDE tools
     * 4. **Safety**: Naming conventions prevent common bugs
     * 5. **Maintainability**: Code is easier to understand and modify
     *
     * @section Exceptions
     * 
     * Some exceptions to these rules exist for:
     * - Third-party library integrations (maintain their naming)
     * - Hardware register names (use manufacturer conventions)
     * - Mathematical variable names in algorithms (x, y, theta may be appropriate)
     * - Legacy code (gradual refactoring preferred over breaking changes)
     */

} // namespace NamingConventions

#endif // NAMING_CONVENTIONS_H
