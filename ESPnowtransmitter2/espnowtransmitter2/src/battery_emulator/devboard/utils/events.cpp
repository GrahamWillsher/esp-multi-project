#include "events.h"
#include "led_handler.h"
#include <Arduino.h>
#include "../../datalayer/datalayer.h"
#include "../../devboard/hal/hal.h"
#include "../../devboard/utils/logging.h"

typedef struct {
  EVENTS_STRUCT_TYPE entries[EVENT_NOF_EVENTS];
  EVENTS_LEVEL_TYPE level;
} EVENT_TYPE;

/* Local variables */
static EVENT_TYPE events;
static const char* EVENTS_ENUM_TYPE_STRING[] = {EVENTS_ENUM_TYPE(GENERATE_STRING)};
static const char* EVENTS_LEVEL_TYPE_STRING[] = {EVENTS_LEVEL_TYPE(GENERATE_STRING)};
static const char* EMULATOR_STATUS_STRING[] = {EMULATOR_STATUS(GENERATE_STRING)};

/* Local function prototypes */
static void set_event(EVENTS_ENUM_TYPE event, uint8_t data, bool latched);
static void update_event_level(void);
static void update_bms_status(void);

/* Initialization function */
void init_events(void) {
  for (uint16_t i = 0; i < EVENT_NOF_EVENTS; i++) {
    events.entries[i].data = 0;
    events.entries[i].timestamp = 0;
    events.entries[i].occurences = 0;
    events.entries[i].MQTTpublished = false;  // Not published by default
  }

  events.entries[EVENT_CANMCP2517FD_INIT_FAILURE].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_CANMCP2515_INIT_FAILURE].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_CANFD_BUFFER_FULL].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_CAN_BUFFER_FULL].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_TASK_OVERRUN].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_THERMAL_RUNAWAY].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_CAN_CORRUPTED_WARNING].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_CAN_NATIVE_TX_FAILURE].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_CAN_BATTERY_MISSING].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_CAN_BATTERY2_MISSING].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_CAN_CHARGER_MISSING].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_CAN_INVERTER_MISSING].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_CONTACTOR_WELDED].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_CONTACTOR_OPEN].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_CPU_OVERHEATING].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_CPU_OVERHEATED].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_WATER_INGRESS].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_CHARGE_LIMIT_EXCEEDED].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_DISCHARGE_LIMIT_EXCEEDED].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_12V_LOW].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_SOC_PLAUSIBILITY_ERROR].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_SOC_UNAVAILABLE].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_STALE_VALUE].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_KWH_PLAUSIBILITY_ERROR].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_BALANCING_START].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_BALANCING_END].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_BATTERY_EMPTY].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_BATTERY_FULL].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_BATTERY_FUSE].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_BATTERY_FROZEN].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_BATTERY_CAUTION].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_BATTERY_CHG_STOP_REQ].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_BATTERY_DISCHG_STOP_REQ].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_BATTERY_CHG_DISCHG_STOP_REQ].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_BATTERY_OVERHEAT].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_BATTERY_OVERVOLTAGE].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_BATTERY_UNDERVOLTAGE].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_BATTERY_VALUE_UNAVAILABLE].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_BATTERY_ISOLATION].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_BATTERY_SOC_RECALIBRATION].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_BATTERY_SOC_RESET_SUCCESS].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_BATTERY_SOC_RESET_FAIL].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_VOLTAGE_DIFFERENCE].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_SOH_DIFFERENCE].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_SOH_LOW].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_HVIL_FAILURE].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_PRECHARGE_FAILURE].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_AUTOMATIC_PRECHARGE_FAILURE].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_INTERNAL_OPEN_FAULT].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_INVERTER_OPEN_CONTACTOR].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_INTERFACE_MISSING].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_MODBUS_INVERTER_MISSING].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_NO_ENABLE_DETECTED].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_ERROR_OPEN_CONTACTOR].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_CELL_CRITICAL_UNDER_VOLTAGE].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_CELL_CRITICAL_OVER_VOLTAGE].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_CELL_UNDER_VOLTAGE].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_CELL_OVER_VOLTAGE].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_CELL_DEVIATION_HIGH].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_UNKNOWN_EVENT_SET].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_OTA_UPDATE].level = EVENT_LEVEL_UPDATE;
  events.entries[EVENT_OTA_UPDATE_TIMEOUT].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_DUMMY_INFO].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_DUMMY_DEBUG].level = EVENT_LEVEL_DEBUG;
  events.entries[EVENT_DUMMY_WARNING].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_DUMMY_ERROR].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_PERSISTENT_SAVE_INFO].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_SERIAL_RX_WARNING].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_SERIAL_RX_FAILURE].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_SERIAL_TX_FAILURE].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_SERIAL_TRANSMITTER_FAILURE].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_SMA_PAIRING].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_RESET_UNKNOWN].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_RESET_POWERON].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_RESET_EXT].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_RESET_SW].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_RESET_PANIC].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_RESET_INT_WDT].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_RESET_TASK_WDT].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_RESET_WDT].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_RESET_DEEPSLEEP].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_RESET_BROWNOUT].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_RESET_SDIO].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_RESET_USB].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_RESET_JTAG].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_RESET_EFUSE].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_RESET_PWR_GLITCH].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_RESET_CPU_LOCKUP].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_RJXZS_LOG].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_PAUSE_BEGIN].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_PAUSE_END].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_PID_FAILED].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_WIFI_CONNECT].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_WIFI_DISCONNECT].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_MQTT_CONNECT].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_MQTT_DISCONNECT].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_EQUIPMENT_STOP].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_SD_INIT_FAILED].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_PERIODIC_BMS_RESET].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_BMS_RESET_REQ_SUCCESS].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_BMS_RESET_REQ_FAIL].level = EVENT_LEVEL_INFO;
  events.entries[EVENT_BATTERY_TEMP_DEVIATION_HIGH].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_GPIO_CONFLICT].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_GPIO_NOT_DEFINED].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_BATTERY_TEMP_DEVIATION_HIGH].level = EVENT_LEVEL_WARNING;
}

void set_event(EVENTS_ENUM_TYPE event, uint8_t data) {
  set_event(event, data, false);
}

void set_event_latched(EVENTS_ENUM_TYPE event, uint8_t data) {
  set_event(event, data, true);
}

void clear_event(EVENTS_ENUM_TYPE event) {
  if (events.entries[event].state == EVENT_STATE_ACTIVE) {
    events.entries[event].state = EVENT_STATE_INACTIVE;
    update_event_level();
    update_bms_status();
    led_publish_current_state(false, nullptr);
  }
}

void reset_all_events() {
  for (uint16_t i = 0; i < EVENT_NOF_EVENTS; i++) {
    events.entries[i].data = 0;
    events.entries[i].state = EVENT_STATE_INACTIVE;
    events.entries[i].timestamp = 0;
    events.entries[i].occurences = 0;
    events.entries[i].MQTTpublished = false;  // Not published by default
  }
  events.level = EVENT_LEVEL_INFO;
  update_bms_status();
  led_publish_current_state(false, nullptr);
}

void set_event_MQTTpublished(EVENTS_ENUM_TYPE event) {
  events.entries[event].MQTTpublished = true;
}

bool get_event_message(EVENTS_ENUM_TYPE event, char* out, size_t out_size) {
  if (!out || out_size == 0) {
    return false;
  }

  const char* message = nullptr;
  switch (event) {
    case EVENT_CANMCP2517FD_INIT_FAILURE:
      message = "CAN-FD initialization failed. Check hardware or bitrate settings";
      break;
    case EVENT_CANMCP2515_INIT_FAILURE:
      message = "CAN-MCP addon initialization failed. Check hardware";
      break;
    case EVENT_CANFD_BUFFER_FULL:
      message = "MCP2518FD message failed to send. Buffer full or no one on the bus to ACK the message!";
      break;
    case EVENT_CAN_BUFFER_FULL:
      message = "MCP2515 message failed to send. Buffer full or no one on the bus to ACK the message!";
      break;
    case EVENT_TASK_OVERRUN:
      message = "Task took too long to complete. CPU load might be too high. Info message, no action required.";
      break;
    case EVENT_THERMAL_RUNAWAY:
      message = "THERMAL RUNAWAY! POTENTIAL FIRE OR EXPLOSION IMMINENT!";
      break;
    case EVENT_CAN_CORRUPTED_WARNING:
      message = "High amount of corrupted CAN messages detected. Check CAN wire shielding!";
      break;
    case EVENT_CAN_NATIVE_TX_FAILURE:
      message = "CAN_NATIVE failed to transmit, or no one on the bus to ACK the message!";
      break;
    case EVENT_CAN_BATTERY_MISSING:
      message = "Battery not sending messages via CAN for the last 60 seconds. Check wiring!";
      break;
    case EVENT_CAN_BATTERY2_MISSING:
      message = "Secondary battery not sending messages via CAN for the last 60 seconds. Check wiring!";
      break;
    case EVENT_CAN_CHARGER_MISSING:
      message = "Charger not sending messages via CAN for the last 60 seconds. Check wiring!";
      break;
    case EVENT_CAN_INVERTER_MISSING:
      message = "Inverter not sending messages via CAN for the last 60 seconds. Check wiring!";
      break;
    case EVENT_CONTACTOR_WELDED:
      message = "Contactors sticking/welded. Inspect battery with caution!";
      break;
    case EVENT_CONTACTOR_OPEN:
      message = "Battery decided to open contactors. Inspect battery!";
      break;
    case EVENT_CPU_OVERHEATING:
      message = "Battery-Emulator CPU overheating! Increase airflow/cooling to increase hardware lifespan!";
      break;
    case EVENT_CPU_OVERHEATED:
      message = "Battery-Emulator CPU melting! Performing controlled shutdown until temperature drops!";
      break;
    case EVENT_CHARGE_LIMIT_EXCEEDED:
      message = "Inverter is charging faster than battery is allowing.";
      break;
    case EVENT_DISCHARGE_LIMIT_EXCEEDED:
      message = "Inverter is discharging faster than battery is allowing.";
      break;
    case EVENT_WATER_INGRESS:
      message = "Water leakage inside battery detected. Operation halted. Inspect battery!";
      break;
    case EVENT_12V_LOW:
      message = "12V battery source below required voltage to safely close contactors. Inspect the supply/battery!";
      break;
    case EVENT_SOC_PLAUSIBILITY_ERROR:
      message = "SOC reported by battery not plausible. Restart battery!";
      break;
    case EVENT_SOC_UNAVAILABLE:
      message = "SOC not sent by BMS. Calibrate BMS via app.";
      break;
    case EVENT_STALE_VALUE:
      message = "Important values detected as stale. System might have locked up!";
      break;
    case EVENT_KWH_PLAUSIBILITY_ERROR:
      message = "kWh remaining reported by battery not plausible. Battery needs cycling.";
      break;
    case EVENT_BALANCING_START:
      message = "Balancing has started";
      break;
    case EVENT_BALANCING_END:
      message = "Balancing has ended";
      break;
    case EVENT_BATTERY_EMPTY:
      message = "Battery is completely discharged";
      break;
    case EVENT_BATTERY_FULL:
      message = "Battery is fully charged";
      break;
    case EVENT_BATTERY_FUSE:
      message = "Battery internal fuse blown. Inspect battery";
      break;
    case EVENT_BATTERY_FROZEN:
      message = "Battery is too cold to operate optimally. Consider warming it up!";
      break;
    case EVENT_BATTERY_CAUTION:
      message = "Battery has raised a general caution flag. Might want to inspect it closely.";
      break;
    case EVENT_BATTERY_CHG_STOP_REQ:
      message = "Battery raised caution indicator AND requested charge stop. Inspect battery status!";
      break;
    case EVENT_BATTERY_DISCHG_STOP_REQ:
      message = "Battery raised caution indicator AND requested discharge stop. Inspect battery status!";
      break;
    case EVENT_BATTERY_CHG_DISCHG_STOP_REQ:
      message = "Battery raised caution indicator AND requested charge/discharge stop. Inspect battery status!";
      break;
    case EVENT_BATTERY_REQUESTS_HEAT:
      message = "COLD BATTERY! Battery requesting heating pads to activate!";
      break;
    case EVENT_BATTERY_WARMED_UP:
      message = "Battery requesting heating pads to stop. The battery is now warm enough.";
      break;
    case EVENT_BATTERY_OVERHEAT:
      message = "Battery overheated. Shutting down to prevent thermal runaway!";
      break;
    case EVENT_BATTERY_OVERVOLTAGE:
      message = "Battery exceeding maximum design voltage. Discharge battery to prevent damage!";
      break;
    case EVENT_BATTERY_UNDERVOLTAGE:
      message = "Battery under minimum design voltage. Charge battery to prevent damage!";
      break;
    case EVENT_BATTERY_VALUE_UNAVAILABLE:
      message = "Battery measurement unavailable. Check 12V power supply and battery wiring!";
      break;
    case EVENT_BATTERY_ISOLATION:
      message = "Battery reports isolation error. High voltage might be leaking to ground. Check battery!";
      break;
    case EVENT_BATTERY_SOC_RECALIBRATION:
      message = "The BMS updated the HV battery State of Charge (SOC) by more than 3pct based on SocByOcv.";
      break;
    case EVENT_BATTERY_SOC_RESET_SUCCESS:
      message = "SOC reset routine was successful.";
      break;
    case EVENT_BATTERY_SOC_RESET_FAIL:
      message = "SOC reset routine failed - check SOC is < 15 or > 90, and contactors are open.";
      break;
    case EVENT_VOLTAGE_DIFFERENCE:
      message = "Too large voltage diff between the batteries. Second battery cannot join the DC-link";
      break;
    case EVENT_SOH_DIFFERENCE:
      message = "Large deviation in State of health between packs. Inspect battery.";
      break;
    case EVENT_SOH_LOW:
      message = "State of health critically low. Battery internal resistance too high to continue. Recycle battery.";
      break;
    case EVENT_HVIL_FAILURE:
      message = "Battery interlock loop broken. Check that high voltage / low voltage connectors are seated. Battery will be disabled!";
      break;
    case EVENT_PRECHARGE_FAILURE:
      message = "Battery failed to precharge. Check that capacitor is seated on high voltage output.";
      break;
    case EVENT_AUTOMATIC_PRECHARGE_FAILURE:
      message = "Automatic precharge FAILURE. Failed to reach target voltage or BMS timeout. Reboot emulator to retry!";
      break;
    case EVENT_INTERNAL_OPEN_FAULT:
      message = "High voltage cable removed while battery running. Opening contactors!";
      break;
    case EVENT_INVERTER_OPEN_CONTACTOR:
      message = "Inverter side opened contactors. Normal operation.";
      break;
    case EVENT_INTERFACE_MISSING:
      message = "Configuration trying to use CAN interface not baked into the software. Recompile software!";
      break;
    case EVENT_ERROR_OPEN_CONTACTOR:
      message = "Too much time spent in error state. Opening contactors, not safe to continue. Check other active ERROR code for reason. Reboot emulator after problem is solved!";
      break;
    case EVENT_MODBUS_INVERTER_MISSING:
      message = "Modbus inverter has not sent any data. Inspect communication wiring!";
      break;
    case EVENT_NO_ENABLE_DETECTED:
      message = "Inverter Enable line has not been active for a long time. Check Wiring!";
      break;
    case EVENT_CELL_CRITICAL_UNDER_VOLTAGE:
      message = "CELL VOLTAGE CRITICALLY LOW! Not possible to continue. Inspect battery!";
      break;
    case EVENT_CELL_UNDER_VOLTAGE:
      message = "Cell undervoltage. Further discharge not possible. Check balancing of cells";
      break;
    case EVENT_CELL_OVER_VOLTAGE:
      message = "Cell overvoltage. Further charging not possible. Check balancing of cells";
      break;
    case EVENT_CELL_CRITICAL_OVER_VOLTAGE:
      message = "CELL VOLTAGE CRITICALLY HIGH! Not possible to continue. Inspect battery!";
      break;
    case EVENT_CELL_DEVIATION_HIGH:
      message = "Large cell voltage deviation! Check balancing of cells";
      break;
    case EVENT_UNKNOWN_EVENT_SET:
      message = "An unknown event was set! Review your code!";
      break;
    case EVENT_DUMMY_INFO:
      message = "The dummy info event was set!";  // Don't change this event message!
      break;
    case EVENT_DUMMY_DEBUG:
      message = "The dummy debug event was set!";  // Don't change this event message!
      break;
    case EVENT_DUMMY_WARNING:
      message = "The dummy warning event was set!";  // Don't change this event message!
      break;
    case EVENT_DUMMY_ERROR:
      message = "The dummy error event was set!";  // Don't change this event message!
      break;
    case EVENT_PERSISTENT_SAVE_INFO:
      message = "Failed to save user settings. Namespace full?";
      break;
    case EVENT_SERIAL_RX_WARNING:
      message = "Error in serial function: No data received for some time, see data for minutes";
      break;
    case EVENT_SERIAL_RX_FAILURE:
      message = "Error in serial function: No data for a long time!";
      break;
    case EVENT_SERIAL_TX_FAILURE:
      message = "Error in serial function: No ACK from receiver!";
      break;
    case EVENT_SERIAL_TRANSMITTER_FAILURE:
      message = "Error in serial function: Some ERROR level fault in transmitter, received by receiver";
      break;
    case EVENT_SMA_PAIRING:
      message = "SMA inverter trying to pair, contactors will close and open according to Enable line";
      break;
    case EVENT_OTA_UPDATE:
      message = "OTA update started!";
      break;
    case EVENT_OTA_UPDATE_TIMEOUT:
      message = "OTA update timed out!";
      break;
    case EVENT_RESET_UNKNOWN:
      message = "The board was reset unexpectedly, and reason can't be determined";
      break;
    case EVENT_RESET_POWERON:
      message = "The board was reset from a power-on event. Normal operation";
      break;
    case EVENT_RESET_EXT:
      message = "The board was reset from an external pin";
      break;
    case EVENT_RESET_SW:
      message = "The board was reset via software, webserver or OTA. Normal operation";
      break;
    case EVENT_RESET_PANIC:
      message = "The board was reset due to an exception or panic. Inform developers!";
      break;
    case EVENT_RESET_INT_WDT:
      message = "The board was reset due to an interrupt watchdog timeout. Inform developers!";
      break;
    case EVENT_RESET_TASK_WDT:
      message = "The board was reset due to a task watchdog timeout. Inform developers!";
      break;
    case EVENT_RESET_WDT:
      message = "The board was reset due to other watchdog timeout. Inform developers!";
      break;
    case EVENT_RESET_DEEPSLEEP:
      message = "The board was reset after exiting deep sleep mode";
      break;
    case EVENT_RESET_BROWNOUT:
      message = "The board was reset due to a momentary low voltage condition. This is expected during certain operations like flashing via USB";
      break;
    case EVENT_RESET_SDIO:
      message = "The board was reset over SDIO";
      break;
    case EVENT_RESET_USB:
      message = "The board was reset by the USB peripheral";
      break;
    case EVENT_RESET_JTAG:
      message = "The board was reset by JTAG";
      break;
    case EVENT_RESET_EFUSE:
      message = "The board was reset due to an efuse error";
      break;
    case EVENT_RESET_PWR_GLITCH:
      message = "The board was reset due to a detected power glitch";
      break;
    case EVENT_RESET_CPU_LOCKUP:
      message = "The board was reset due to CPU lockup. Inform developers!";
      break;
    case EVENT_RJXZS_LOG:
      message = "Error code active in RJXZS BMS. Clear via their smartphone app!";
      break;
    case EVENT_PAUSE_BEGIN:
      message = "The emulator is trying to pause the battery.";
      break;
    case EVENT_PAUSE_END:
      message = "The emulator is attempting to resume battery operation from pause.";
      break;
    case EVENT_PID_FAILED:
      message = "Failed to write PID request to battery";
      break;
    case EVENT_WIFI_CONNECT:
      message = "Wifi connected.";
      break;
    case EVENT_WIFI_DISCONNECT:
      message = "Wifi disconnected.";
      break;
    case EVENT_MQTT_CONNECT:
      message = "MQTT connected.";
      break;
    case EVENT_MQTT_DISCONNECT:
      message = "MQTT disconnected.";
      break;
    case EVENT_EQUIPMENT_STOP:
      message = "User requested stop, either via equipment stop circuit or webserver Open Contactor button";
      break;
    case EVENT_SD_INIT_FAILED:
      message = "SD card initialization failed, check hardware. Power must be removed to reset the SD card.";
      break;
    case EVENT_PERIODIC_BMS_RESET:
      message = "BMS reset event completed.";
      break;
    case EVENT_PERIODIC_BMS_RESET_FAILURE:
      message = "BMS reset aborted - contactors were still under load.";
      break;
    case EVENT_BMS_RESET_REQ_SUCCESS:
      message = "BMS reset request completed successfully.";
      break;
    case EVENT_BMS_RESET_REQ_FAIL:
      message = "BMS reset request failed - check contactors are open.";
      break;
    case EVENT_GPIO_CONFLICT: {
      const String failed_allocator_str =
        esp32hal ? esp32hal->failed_allocator() : String("unknown");
      const String conflicting_allocator_str =
        esp32hal ? esp32hal->conflicting_allocator() : String("unknown");
      const int n = snprintf(
          out, out_size,
          "GPIO Pin Conflict: The pin used by '%s' is already allocated by '%s'. Please check your configuration and assign different pins.",
        failed_allocator_str.c_str(), conflicting_allocator_str.c_str());
      return n > 0;
    }
    case EVENT_GPIO_NOT_DEFINED: {
      const String failed_allocator_str =
        esp32hal ? esp32hal->failed_allocator() : String("unknown");
      const int n = snprintf(
          out, out_size,
          "Missing GPIO Assignment: The component '%s' requires a GPIO pin that isn't configured. Please define a valid pin number in your settings.",
        failed_allocator_str.c_str());
      return n > 0;
    }
    default:
      out[0] = '\0';
      return false;
  }

  if (!message || message[0] == '\0') {
    out[0] = '\0';
    return false;
  }

  strlcpy(out, message, out_size);
  return true;
}

String get_event_message_string(EVENTS_ENUM_TYPE event) {
  char message[384] = {0};
  if (!get_event_message(event, message, sizeof(message))) {
    return "";
  }
  return String(message);
}

const char* get_event_enum_string(EVENTS_ENUM_TYPE event) {
  // Return the event name but skip "EVENT_" that should always be first
  return EVENTS_ENUM_TYPE_STRING[event] + 6;
}

const char* get_event_level_string(EVENTS_ENUM_TYPE event) {
  // Return the event level but skip "EVENT_LEVEL_" that should always be first
  return EVENTS_LEVEL_TYPE_STRING[events.entries[event].level] + 12;
}

const char* get_event_level_string(EVENTS_LEVEL_TYPE event_level) {
  // Return the event level but skip "EVENT_LEVEL_TYPE_" that should always be first
  return EVENTS_LEVEL_TYPE_STRING[event_level] + 17;
}

const EVENTS_STRUCT_TYPE* get_event_pointer(EVENTS_ENUM_TYPE event) {
  return &events.entries[event];
}

EVENTS_LEVEL_TYPE get_event_level(void) {
  return events.level;
}

EMULATOR_STATUS get_emulator_status() {
  switch (events.level) {
    case EVENT_LEVEL_DEBUG:
    case EVENT_LEVEL_INFO:
      return EMULATOR_STATUS::STATUS_OK;
    case EVENT_LEVEL_WARNING:
      return EMULATOR_STATUS::STATUS_WARNING;
    case EVENT_LEVEL_UPDATE:
      return EMULATOR_STATUS::STATUS_UPDATING;
    case EVENT_LEVEL_ERROR:
      return EMULATOR_STATUS::STATUS_ERROR;
    default:
      return EMULATOR_STATUS::STATUS_OK;
  }
}

const char* get_emulator_status_string(EMULATOR_STATUS status) {
  // Return the status string but skip "STATUS_" that should always be first
  return EMULATOR_STATUS_STRING[status] + 7;
}

/* Local functions */

static void set_event(EVENTS_ENUM_TYPE event, uint8_t data, bool latched) {
  // Just some defensive stuff if someone sets an unknown event
  if (event >= EVENT_NOF_EVENTS) {
    event = EVENT_UNKNOWN_EVENT_SET;
  }

  // If the event is already set, no reason to continue
  if ((events.entries[event].state != EVENT_STATE_ACTIVE) &&
      (events.entries[event].state != EVENT_STATE_ACTIVE_LATCHED)) {
    events.entries[event].occurences++;
    events.entries[event].MQTTpublished = false;

    DEBUG_PRINTF("Event: %s\n", get_event_message_string(event).c_str());
  }

  // We should set the event, update event info
  events.entries[event].timestamp = millis64();
  events.entries[event].data = data;
  // Check if the event is latching
  events.entries[event].state = latched ? EVENT_STATE_ACTIVE_LATCHED : EVENT_STATE_ACTIVE;

  // Update event level, only upwards. Downward changes are done in Software.ino:loop()
  events.level = (EVENTS_LEVEL_TYPE)max(events.level, events.entries[event].level);

  update_bms_status();
  led_publish_current_state(false, nullptr);
}

static void update_bms_status(void) {
  switch (events.level) {
    case EVENT_LEVEL_INFO:
    case EVENT_LEVEL_WARNING:
    case EVENT_LEVEL_DEBUG:
      datalayer.battery.status.bms_status = ACTIVE;
      break;
    case EVENT_LEVEL_UPDATE:
      datalayer.battery.status.bms_status = UPDATING;
      break;
    case EVENT_LEVEL_ERROR:
      datalayer.battery.status.bms_status = FAULT;
      break;
    default:
      break;
  }
}

// Function to compare events by timestamp descending
bool compareEventsByTimestampDesc(const EventData& a, const EventData& b) {
  return a.event_pointer->timestamp > b.event_pointer->timestamp;
}

// Function to compare events by timestamp ascending
bool compareEventsByTimestampAsc(const EventData& a, const EventData& b) {
  return a.event_pointer->timestamp < b.event_pointer->timestamp;
}

static void update_event_level(void) {
  EVENTS_LEVEL_TYPE temporary_level = EVENT_LEVEL_INFO;
  for (uint8_t i = 0u; i < EVENT_NOF_EVENTS; i++) {
    if ((events.entries[i].state == EVENT_STATE_ACTIVE) || (events.entries[i].state == EVENT_STATE_ACTIVE_LATCHED)) {
      temporary_level = (EVENTS_LEVEL_TYPE)max(events.entries[i].level, temporary_level);
    }
  }
  events.level = temporary_level;
}
