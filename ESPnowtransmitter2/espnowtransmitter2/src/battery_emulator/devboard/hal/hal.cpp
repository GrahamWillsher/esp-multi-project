#include "hal.h"

#include <Arduino.h>

Esp32Hal* esp32hal = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// TransmitterHal — Fixed single-board HAL for the Olimex ESP32-POE2
//
// The original Battery Emulator project supported multiple hardware variants
// (LilyGo, Stark, 3LB, DevKit, etc.) through a polymorphic HAL.
// The 2-device ESP-NOW build targets one board only; all variant files have
// been removed. This class replaces the compile-time board selection.
//
// GPIO policy:
//  - All peripheral pins return GPIO_NUM_NC.
//  - CAN (MCP2515) pin management is handled in can_driver.h, NOT here.
//  - Contactor, RS485, SD card, equipment stop, and WUP pins are not present
//    on the Olimex ESP32-POE2 and are not wired.
// ─────────────────────────────────────────────────────────────────────────────
class TransmitterHal : public Esp32Hal {
 public:
  const char* name() override { return "Olimex ESP32-POE2"; }

  // Only CAN add-on (MCP2515 via HSPI) is present on this board.
  // Actual pin assignments live in can_driver.h, not here.
  std::vector<comm_interface> available_interfaces() override {
    return {comm_interface::CanAddonMcp2515};
  }
};

void init_hal() {
  esp32hal = new TransmitterHal();
}

bool Esp32Hal::system_booted_up() {
  return milliseconds(millis()) > BOOTUP_TIME();
}
