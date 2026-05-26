#include "comm_contactorcontrol.h"
#include "../../devboard/hal/hal.h"
#include "../../devboard/safety/safety.h"
#include "../../inverter/INVERTERS.h"
#include "../../devboard/utils/compat.h"
#include "../../../network/time_manager.h"
#include "../../../../lib/ethernet_utilities/ethernet_utilities.h"

#include <ctime>

// TODO: Ensure valid values at run-time
// User can update all these values via Settings page
bool contactor_control_enabled = false;         //Should GPIO contactor control be performed?
bool contactor_control_inverted_logic = false;  //Should we control NC contactors? Extremely rare option
uint16_t precharge_time_ms = 100;               //Precharge time in ms. Adjust depending on capacitance in inverter
bool pwm_contactor_control = false;             //Should the contactors be economized via PWM after they are engaged?
bool contactor_control_enabled_double_battery = false;  //Should a contactor for the secondary battery be operated?
bool remote_bms_reset = false;                          //Is it possible to actuate BMS reset via MQTT?
bool periodic_bms_reset = false;                        //Should periodic BMS reset be performed each 24h?
bool bms_first_align_enabled = false;                   //Use first successful NTP sync to align first reset trigger
uint16_t bms_first_align_target_minutes = 120;          //Target time (minutes since midnight), default 02:00

// Parameters
enum State { DISCONNECTED, START_PRECHARGE, PRECHARGE, POSITIVE, PRECHARGE_OFF, COMPLETED, SHUTDOWN_REQUESTED };
State contactorStatus = DISCONNECTED;

const uint8_t ON = 1;
const uint8_t OFF = 0;

#define MAX_ALLOWED_FAULT_TICKS 1000  //1000 = 10 seconds
#define NEGATIVE_CONTACTOR_TIME_MS \
  500  // Time after negative contactor is turned on, to start precharge (not actual precharge time!)
#define PRECHARGE_COMPLETED_TIME_MS \
  1000  // After successful precharge, resistor is turned off after this delay (and contactors are economized if PWM enabled)
uint16_t pwm_frequency = 20000;
uint16_t pwm_hold_duty = 250;
#define PWM_ON_DUTY 1023
#define PWM_RESOLUTION 10
#define PWM_OFF_DUTY 0  //No need to have this userconfigurable
#define PWM_Positive_Channel 0
#define PWM_Negative_Channel 1
static unsigned long prechargeStartTime = 0;
unsigned long negativeStartTime = 0;
unsigned long prechargeCompletedTime = 0;
unsigned long timeSpentInFaultedMode = 0;
unsigned long currentTime = 0;
unsigned long lastPowerRemovalTime = 0;
unsigned long bmsPowerOnTime = 0;
const unsigned long powerRemovalInterval = 24 * 60 * 60 * 1000;  // 24 hours in milliseconds
const unsigned long bmsWarmupDuration = 3000;
const unsigned long kAlignmentMoveSafetyThresholdMs = 5UL * 60UL * 1000UL;  // 5 minutes

static bool firstAlignArmed = false;
static bool firstAlignConsumeOnSuccess = false;
static bool initialAlignCompletedThisBoot = false;
static unsigned long firstAlignAnchorMs = 0;
static unsigned long firstAlignDelayMs = 0;

enum class AlignArmReason : uint8_t { NONE = 0, INITIAL = 1, OFFSET_CHANGE = 2 };
static AlignArmReason alignArmReason = AlignArmReason::NONE;

static bool offset_monitor_initialized = false;
static int16_t last_seen_utc_offset_min = 0;
static bool defer_realign_after_current_cycle = false;

static void clear_alignment_arm_state(bool clear_initial_align_boot_state = false) {
  firstAlignArmed = false;
  firstAlignDelayMs = 0;
  firstAlignAnchorMs = 0;
  firstAlignConsumeOnSuccess = false;
  alignArmReason = AlignArmReason::NONE;
  defer_realign_after_current_cycle = false;
  if (clear_initial_align_boot_state) {
    initialAlignCompletedThisBoot = false;
  }
}

static bool compute_arm_delay_seconds(uint32_t target_minutes, int* out_delta_seconds) {
  if (!out_delta_seconds) {
    return false;
  }
  if (target_minutes > 1439) {
    return false;
  }

  const uint64_t unix_time = TimeManager::instance().get_unix_time();
  if (unix_time == 0) {
    return false;
  }

  const time_t current_epoch = static_cast<time_t>(unix_time);
  struct tm local_tm{};
  localtime_r(&current_epoch, &local_tm);

  const int current_seconds = (local_tm.tm_hour * 3600) + (local_tm.tm_min * 60) + local_tm.tm_sec;
  const int target_seconds = static_cast<int>(target_minutes) * 60;

  int delta_seconds = target_seconds - current_seconds;
  if (delta_seconds <= 0) {
    delta_seconds += 24 * 3600;
  }

  *out_delta_seconds = delta_seconds;
  return true;
}

static bool arm_alignment_to_target_from_ntp_internal(unsigned long now_ms,
                                                      AlignArmReason reason,
                                                      const char* log_prefix,
                                                      bool allow_override_existing_arm) {
  if (!periodic_bms_reset || !bms_first_align_enabled) {
    return false;
  }

  if (!allow_override_existing_arm && firstAlignArmed) {
    return false;
  }

  int delta_seconds = 0;
  if (!compute_arm_delay_seconds(bms_first_align_target_minutes, &delta_seconds)) {
    return false;
  }

  firstAlignDelayMs = static_cast<unsigned long>(delta_seconds) * 1000UL;
  firstAlignAnchorMs = now_ms;
  firstAlignArmed = true;
  alignArmReason = reason;

  logging.printf("BMS reset %s armed: target=%02u:%02u delay=%lus\n",
                 log_prefix,
                 static_cast<unsigned>(bms_first_align_target_minutes / 60),
                 static_cast<unsigned>(bms_first_align_target_minutes % 60),
                 static_cast<unsigned long>(delta_seconds));
  return true;
}

static bool force_rearm_alignment_to_target_from_ntp(unsigned long now_ms,
                                                     AlignArmReason reason,
                                                     const char* log_prefix) {
  return arm_alignment_to_target_from_ntp_internal(now_ms, reason, log_prefix, true);
}

static unsigned long get_remaining_alignment_ms(unsigned long now_ms) {
  if (!firstAlignArmed) {
    return 0;
  }
  const unsigned long elapsed = now_ms - firstAlignAnchorMs;
  if (elapsed >= firstAlignDelayMs) {
    return 0;
  }
  return firstAlignDelayMs - elapsed;
}

static bool arm_alignment_to_target_from_ntp(unsigned long now_ms, AlignArmReason reason, const char* log_prefix) {
  return arm_alignment_to_target_from_ntp_internal(now_ms, reason, log_prefix, false);
}

static void maybe_arm_initial_alignment_from_ntp(unsigned long now_ms) {
  if (!periodic_bms_reset || !bms_first_align_enabled || firstAlignArmed || initialAlignCompletedThisBoot) {
    return;
  }

  arm_alignment_to_target_from_ntp(now_ms, AlignArmReason::INITIAL, "initial-align");
}

static void maybe_arm_offset_change_realign(unsigned long now_ms) {
  if (!periodic_bms_reset || !bms_first_align_enabled) {
    return;
  }

  const int16_t current_offset_min = get_cached_utc_offset_min();
  if (!offset_monitor_initialized) {
    last_seen_utc_offset_min = current_offset_min;
    offset_monitor_initialized = true;
    return;
  }

  if (current_offset_min == last_seen_utc_offset_min) {
    return;
  }

  logging.printf("BMS reset: UTC offset change detected (%d -> %d). Triggering re-alignment.\n",
                 static_cast<int>(last_seen_utc_offset_min),
                 static_cast<int>(current_offset_min));

  // Update immediately so this is edge-triggered once per observed change.
  last_seen_utc_offset_min = current_offset_min;

  (void)arm_alignment_to_target_from_ntp(now_ms, AlignArmReason::OFFSET_CHANGE, "offset-change-realign");
}

static uint8_t encode_bms_alignment_snapshot_data(uint64_t now_epoch) {
  (void)now_epoch;
  // Legacy event format retained for compatibility with current event text.
  // queue_count=0 and status=1 => "queue empty / none scheduled".
  return static_cast<uint8_t>((1u << 4) | 0u);
}

static void emit_bms_alignment_snapshot_event() {
  const uint64_t now_epoch = TimeManager::instance().get_unix_time();
  const uint8_t snapshot_data = encode_bms_alignment_snapshot_data(now_epoch);
  set_event(EVENT_BMS_RESET_ALIGNMENT_STATUS, snapshot_data);
  clear_event(EVENT_BMS_RESET_ALIGNMENT_STATUS);
}

void on_bms_reset_alignment_settings_changed() {
  const unsigned long now_ms = millis();

  if (!periodic_bms_reset) {
    clear_alignment_arm_state();
    return;
  }

  if (!bms_first_align_enabled) {
    clear_alignment_arm_state();
    return;
  }

  if (datalayer.system.status.bms_reset_status != BMS_RESET_IDLE) {
    defer_realign_after_current_cycle = true;
    logging.println("BMS reset alignment: settings changed during active reset, deferring re-arm");
    return;
  }

  if (!firstAlignArmed) {
    if (!force_rearm_alignment_to_target_from_ntp(now_ms, AlignArmReason::INITIAL, "settings-change")) {
      logging.println("BMS reset alignment: settings changed but time not available yet, waiting for NTP");
    }
    return;
  }

  const unsigned long remaining_ms = get_remaining_alignment_ms(now_ms);
  if (remaining_ms >= kAlignmentMoveSafetyThresholdMs) {
    if (force_rearm_alignment_to_target_from_ntp(now_ms, AlignArmReason::INITIAL, "settings-change-move")) {
      defer_realign_after_current_cycle = false;
    }
    return;
  }

  defer_realign_after_current_cycle = true;
  logging.printf("BMS reset alignment: pending arm within safety threshold (%lu ms < %lu ms), applying new target next cycle\n",
                 static_cast<unsigned long>(remaining_ms),
                 static_cast<unsigned long>(kAlignmentMoveSafetyThresholdMs));
}

void set(uint8_t pin, bool direction, uint32_t pwm_freq = 0xFFFF) {

  if (contactor_control_inverted_logic) {
    direction = !direction;  //Invert direction for NC contactors
  }

  if (pwm_contactor_control) {
    if (pwm_freq != 0xFFFF) {
      ledcWrite(pin, pwm_freq);
      return;
    }
  }
  if (direction == 1) {
    digitalWrite(pin, HIGH);
  } else {  // 0
    digitalWrite(pin, LOW);
  }
}

// Initialization functions

const char* contactors = "Contactors";

bool init_contactors() {
  // Init contactor pins
  if (contactor_control_enabled) {
    auto posPin = esp32hal->POSITIVE_CONTACTOR_PIN();
    auto negPin = esp32hal->NEGATIVE_CONTACTOR_PIN();
    auto precPin = esp32hal->PRECHARGE_PIN();

    if (!esp32hal->alloc_pins(contactors, posPin, negPin, precPin)) {
      DEBUG_PRINTF("GPIO controlled contactor setup failed\n");
      return false;
    }

    if (pwm_contactor_control) {
      // Setup PWM Channel Frequency and Resolution
      ledcAttachChannel(posPin, pwm_frequency, PWM_RESOLUTION, PWM_Positive_Channel);
      ledcAttachChannel(negPin, pwm_frequency, PWM_RESOLUTION, PWM_Negative_Channel);
      // Set all pins OFF (0% PWM)
      ledcWrite(posPin, PWM_OFF_DUTY);
      ledcWrite(negPin, PWM_OFF_DUTY);
    } else {  //Normal CONTACTOR_CONTROL
      pinMode(posPin, OUTPUT);
      set(posPin, OFF);
      pinMode(negPin, OUTPUT);
      set(negPin, OFF);
    }  // Precharge never has PWM regardless of setting
    pinMode(precPin, OUTPUT);
    set(precPin, OFF);
  }

  if (contactor_control_enabled_double_battery) {
    auto second_contactors = esp32hal->SECOND_BATTERY_CONTACTORS_PIN();
    if (!esp32hal->alloc_pins(contactors, second_contactors)) {
      DEBUG_PRINTF("Secondary battery contactor control setup failed\n");
      return false;
    }

    pinMode(second_contactors, OUTPUT);
    set(second_contactors, OFF);
  }

  // Init BMS contactor
  if (periodic_bms_reset || remote_bms_reset || esp32hal->always_enable_bms_power()) {
    auto pin = esp32hal->BMS_POWER();
    if (!esp32hal->alloc_pins("BMS power", pin)) {
      DEBUG_PRINTF("BMS power setup failed\n");
      return false;
    }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }

  return true;
}

static void dbg_contactors(const char* state) {
  logging.print("[");
  logging.print(millis());
  logging.print(" ms] contactors control: ");
  logging.println(state);
}

// Main functions of the handle_contactors include checking if inverter allows for closing, checking battery 2, checking BMS power output, and actual contactor closing/precharge via GPIO
void handle_contactors() {
  if (inverter && inverter->controls_contactor()) {
    datalayer.system.status.inverter_allows_contactor_closing = inverter->allows_contactor_closing();
  }

  auto posPin = esp32hal->POSITIVE_CONTACTOR_PIN();
  auto negPin = esp32hal->NEGATIVE_CONTACTOR_PIN();
  auto prechargePin = esp32hal->PRECHARGE_PIN();
  auto bms_power_pin = esp32hal->BMS_POWER();

  if (bms_power_pin != GPIO_NUM_NC) {
    handle_BMSpower();  // Some batteries need to be periodically power cycled
  }

  if (contactor_control_enabled_double_battery) {
    handle_contactors_battery2();
  }

  if (contactor_control_enabled) {
    // First check if we have any active errors, incase we do, turn off the battery
    if (datalayer.battery.status.bms_status == FAULT) {
      timeSpentInFaultedMode++;
    } else {
      timeSpentInFaultedMode = 0;
    }

    //handle contactor control SHUTDOWN_REQUESTED
    if (timeSpentInFaultedMode > MAX_ALLOWED_FAULT_TICKS) {
      contactorStatus = SHUTDOWN_REQUESTED;
    }

    if (contactorStatus == SHUTDOWN_REQUESTED) {
      set(prechargePin, OFF);
      set(negPin, OFF, PWM_OFF_DUTY);
      set(posPin, OFF, PWM_OFF_DUTY);
      set_event(EVENT_ERROR_OPEN_CONTACTOR, 0);
      datalayer.system.status.contactors_engaged = 2;
      return;  // A fault scenario latches the contactor control. It is not possible to recover without a powercycle (and investigation why fault occured)
    }

    // After that, check if we are OK to start turning on the battery
    if (contactorStatus == DISCONNECTED) {
      set(prechargePin, OFF);
      set(negPin, OFF, PWM_OFF_DUTY);
      set(posPin, OFF, PWM_OFF_DUTY);
      datalayer.system.status.contactors_engaged = 0;

      if (datalayer.system.status.inverter_allows_contactor_closing && !datalayer.system.info.equipment_stop_active) {
        contactorStatus = START_PRECHARGE;
      }
    }

    // In case the inverter requests contactors to open, set the state accordingly
    if (contactorStatus == COMPLETED) {
      //Incase inverter (or estop) requests contactors to open, make state machine jump to Disconnected state (recoverable)
      if (!datalayer.system.status.inverter_allows_contactor_closing || datalayer.system.info.equipment_stop_active) {
        contactorStatus = DISCONNECTED;
      }
      // Skip running the state machine below if it has already completed
      return;
    }

    currentTime = millis();

    if (currentTime < INTERVAL_10_S) {
      // Skip running the state machine before system has started up.
      // Gives the system some time to detect any faults from battery before blindly just engaging the contactors
      return;
    }

    // Handle actual state machine. This first turns on Negative, then Precharge, then Positive, and finally turns OFF precharge
    switch (contactorStatus) {
      case START_PRECHARGE:
        set(negPin, ON, PWM_ON_DUTY);
        dbg_contactors("NEGATIVE");
        prechargeStartTime = currentTime;
        contactorStatus = PRECHARGE;
        datalayer.system.status.contactors_engaged = 3;
        break;

      case PRECHARGE:
        if (currentTime - prechargeStartTime >= NEGATIVE_CONTACTOR_TIME_MS) {
          set(prechargePin, ON);
          dbg_contactors("PRECHARGE");
          negativeStartTime = currentTime;
          contactorStatus = POSITIVE;
          datalayer.system.status.contactors_engaged = 3;
        }
        break;

      case POSITIVE:
        if (currentTime - negativeStartTime >= precharge_time_ms) {
          set(posPin, ON, PWM_ON_DUTY);
          dbg_contactors("POSITIVE");
          prechargeCompletedTime = currentTime;
          contactorStatus = PRECHARGE_OFF;
          datalayer.system.status.contactors_engaged = 3;
        }
        break;

      case PRECHARGE_OFF:
        if (currentTime - prechargeCompletedTime >= PRECHARGE_COMPLETED_TIME_MS) {
          set(prechargePin, OFF);
          set(negPin, ON, pwm_hold_duty);
          set(posPin, ON, pwm_hold_duty);
          dbg_contactors("PRECHARGE_OFF");
          contactorStatus = COMPLETED;
          datalayer.system.status.contactors_engaged = 1;
        }
        break;
      default:
        break;
    }
  }
}

void handle_contactors_battery2() {
  auto second_contactors = esp32hal->SECOND_BATTERY_CONTACTORS_PIN();

  if ((contactorStatus == COMPLETED) && datalayer.system.status.battery2_allowed_contactor_closing) {
    set(second_contactors, ON);
    datalayer.system.status.contactors_battery2_engaged = true;
  } else {  // Closing contactors on secondary battery not allowed
    set(second_contactors, OFF);
    datalayer.system.status.contactors_battery2_engaged = false;
  }
}

/* PERIODIC_BMS_RESET - Once every 24 hours we remove power from the BMS_power pin for 30 seconds.
REMOTE_BMS_RESET - Allows the user to remotely powercycle the BMS by sending a command to the emulator via MQTT.

This makes the BMS recalculate all SOC% and avoid memory leaks
During that time we also set the emulator state to paused in order to not try and send CAN messages towards the battery
Feature is only used if user has enabled PERIODIC_BMS_RESET */

void bms_power_off() {
  digitalWrite(esp32hal->BMS_POWER(), LOW);
}

void bms_power_on() {
  digitalWrite(esp32hal->BMS_POWER(), HIGH);
}

void handle_BMSpower() {
  if (periodic_bms_reset || remote_bms_reset) {
    currentTime = millis();

    const uint64_t unix_time = TimeManager::instance().get_unix_time();

    if (!periodic_bms_reset && firstAlignArmed) {
      clear_alignment_arm_state();
    }

    if (periodic_bms_reset && !bms_first_align_enabled && firstAlignArmed) {
      clear_alignment_arm_state();
    }

    if (periodic_bms_reset) {
      maybe_arm_initial_alignment_from_ntp(currentTime);
      maybe_arm_offset_change_realign(currentTime);
    }

    if (datalayer.system.status.bms_reset_status == BMS_RESET_IDLE) {
      // Idle state, no reset ongoing

      // Check if it's time to perform a periodic BMS reset
      if (periodic_bms_reset) {
        const bool aligned_due = firstAlignArmed && (currentTime - firstAlignAnchorMs >= firstAlignDelayMs);
        const bool elapsed_due = (currentTime - lastPowerRemovalTime >= powerRemovalInterval);

        if (aligned_due) {
          if (alignArmReason == AlignArmReason::OFFSET_CHANGE) {
            logging.println("BMS reset: triggering offset-change re-aligned reset");
          } else {
            logging.println("BMS reset: triggering initial aligned reset");
          }
          firstAlignConsumeOnSuccess = true;
          start_bms_reset();
        } else if (!firstAlignArmed && elapsed_due) {
          if (unix_time == 0) {
            logging.println("BMS reset: time unsynced, using 24h elapsed fallback");
          }
          start_bms_reset();
        }
      }
    } else if (datalayer.system.status.bms_reset_status == BMS_RESET_WAITING_FOR_PAUSE) {
      // We've already issued a pause, now we're waiting for that to take effect.

      int16_t battery_current_dA = datalayer.battery.status.current_dA;
      int16_t battery2_current_dA = datalayer.battery2.status.current_dA;  // Should be 0 if no battery2

      if (
          // No current, safe to cut power
          (battery_current_dA == 0 && battery2_current_dA == 0)
          // or reasonably low current and 5 seconds has passed
          || (abs(battery_current_dA) < 10 && abs(battery2_current_dA) < 10 &&
              currentTime - lastPowerRemovalTime >= 5000)) {

        bms_power_off();
        lastPowerRemovalTime = currentTime;
        datalayer.system.status.bms_reset_status = BMS_RESET_POWERED_OFF;
      } else if (currentTime - lastPowerRemovalTime >= 10000) {
        // There's still current, and we don't want to weld the contactors, so give up.

        logging.printf("BMS reset: Aborting, contactors are still under load.\n");

        datalayer.system.status.bms_reset_status = BMS_RESET_IDLE;
        set_event(EVENT_PERIODIC_BMS_RESET_FAILURE, 0);
        clear_event(EVENT_PERIODIC_BMS_RESET_FAILURE);
        emit_bms_alignment_snapshot_event();
      }
    } else if (datalayer.system.status.bms_reset_status == BMS_RESET_POWERED_OFF) {
      // Check if the user configured duration has passed
      if (currentTime - lastPowerRemovalTime >= datalayer.battery.settings.user_set_bms_reset_duration_ms) {
        bms_power_on();
        bmsPowerOnTime = currentTime;
        datalayer.system.status.bms_reset_status = BMS_RESET_POWERING_ON;
      }
    } else if (datalayer.system.status.bms_reset_status == BMS_RESET_POWERING_ON) {
      // Wait for BMS to start up before unpausing
      if (currentTime - bmsPowerOnTime >= bmsWarmupDuration) {
        // Unpause the battery
        setBatteryPause(false, false, false, false);

        // Reset is complete

        if (firstAlignConsumeOnSuccess) {
          firstAlignConsumeOnSuccess = false;
          firstAlignArmed = false;
          firstAlignDelayMs = 0;
          firstAlignAnchorMs = 0;

          if (alignArmReason == AlignArmReason::INITIAL) {
            initialAlignCompletedThisBoot = true;
            logging.println("BMS reset: initial alignment consumed, continuing with pure elapsed 24h cycle");
          } else if (alignArmReason == AlignArmReason::OFFSET_CHANGE) {
            logging.println("BMS reset: offset-change re-alignment consumed, continuing with pure elapsed 24h cycle");
          }

          alignArmReason = AlignArmReason::NONE;
        }

        if (defer_realign_after_current_cycle && periodic_bms_reset && bms_first_align_enabled) {
          if (force_rearm_alignment_to_target_from_ntp(currentTime,
                                                       AlignArmReason::INITIAL,
                                                       "settings-change-next-cycle")) {
            defer_realign_after_current_cycle = false;
          }
        }

        datalayer.system.status.bms_reset_status = BMS_RESET_IDLE;
        set_event(EVENT_PERIODIC_BMS_RESET, 0);
        clear_event(EVENT_PERIODIC_BMS_RESET);
        emit_bms_alignment_snapshot_event();
      }
    }
  }
}

void start_bms_reset() {
  if (periodic_bms_reset || remote_bms_reset) {
    if (datalayer.system.status.bms_reset_status == BMS_RESET_IDLE) {
      // Record when we started the BMS reset process
      lastPowerRemovalTime = millis();

      // Issue a pause, which should stop charge/discharge whilst the reset is ongoing
      setBatteryPause(true, false, false, false);

      if (contactor_control_enabled) {
        // We power the contactors directly, so we can avoid closing/opening them
        // during reset.

        // Thus we can cut the BMS power now
        bms_power_off();

        // and jump straight to powered off state, no need to wait.
        datalayer.system.status.bms_reset_status = BMS_RESET_POWERED_OFF;
      } else {
        // The BMS powers the contactors, so we need to wait for the pause to
        // take effect before cutting power to it, or the contactors might drop
        // out under load and be damaged.

        datalayer.system.status.bms_reset_status = BMS_RESET_WAITING_FOR_PAUSE;
      }
    }
  }
}
