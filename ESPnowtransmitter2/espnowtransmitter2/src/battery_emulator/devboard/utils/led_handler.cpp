#include "led_handler.h"
#include "../../datalayer/datalayer.h"
#include "events.h"

/*
 * LED Handler - Simplified Implementation
 * 
 * The Olimex ESP32-POE2 has no physical LED. This module determines
 * the system status and sends it to the receiver as a color value.
 * 
 * The receiver's simulated LED display handles all animation/flashing.
 * We only send:
 *   RED (0)    = Error/Fault
 *   GREEN (1)  = Normal operation
 *   ORANGE (2) = Warning/Updating
 */

static LED* led;

bool led_init(void) {
    // Allocate LED object (no actual hardware GPIO initialization needed)
    led = new LED(datalayer.battery.status.led_mode, GPIO_NUM_NC, 255);
    return true;
}

void led_exe(void) {
    // Determine current LED color based on system status
    led->exe();
}

uint8_t LED::get_led_color_for_status(void) {
    // Map emulator status to LED color
    // The receiver will automatically handle flashing/animation
    switch (get_emulator_status()) {
        case EMULATOR_STATUS::STATUS_OK:
            return LED_GREEN;  // Normal operation - green
            
        case EMULATOR_STATUS::STATUS_WARNING:
            return LED_ORANGE;  // Warning state - orange
            
        case EMULATOR_STATUS::STATUS_UPDATING:
            return LED_ORANGE;  // Updating - orange (temporary state)
            
        case EMULATOR_STATUS::STATUS_ERROR:
            return LED_RED;  // Error/Fault - red
            
        default:
            return LED_ORANGE;  // Unknown state - orange (warning)
    }
}

void LED::exe(void) {
    // Get the current LED color based on system status
    uint8_t color = get_led_color_for_status();
    
    // TODO: Send color via ESP-NOW to receiver
    // This should be called from the main transmitter message handler
    // when the color changes from the last value.
    //
    // Example:
    // if (color != last_led_color) {
    //     flash_led_t msg;
    //     msg.type = msg_flash_led;
    //     msg.color = color;
    //     esp_now_send(receiver_mac, (const uint8_t*)&msg, sizeof(msg));
    //     last_led_color = color;
    // }
}

