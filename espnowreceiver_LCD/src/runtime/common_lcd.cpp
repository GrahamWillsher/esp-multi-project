#include "common_lcd.h"

#include "helpers.h"
#include "logging_config.h"

namespace RTOS {

SemaphoreHandle_t lvgl_mutex = nullptr;
TaskHandle_t lvgl_task = nullptr;
TaskHandle_t display_renderer_task = nullptr;
TaskHandle_t espnow_worker_task = nullptr;
TaskHandle_t mqtt_client_task = nullptr;
QueueHandle_t display_update_queue = nullptr;

}  // namespace RTOS

namespace ESPNow {

uint8_t transmitter_mac[6] = {0, 0, 0, 0, 0, 0};
uint8_t* peer_mac = transmitter_mac;  // Alias for LCD runtime compatibility
QueueHandle_t message_queue = nullptr;
QueueHandle_t& queue = message_queue;  // Alias of message_queue

std::atomic<uint32_t> rx_callback_count{0};
std::atomic<uint32_t> rx_queue_drop_count{0};
std::atomic<uint32_t> rx_queue_high_watermark{0};

std::atomic<uint8_t> current_led_color{0};
std::atomic<uint8_t> current_led_effect{0};
std::atomic<bool> receiver_ota_led_override_active{false};

}  // namespace ESPNow

void handle_error(ErrorSeverity severity, const char* component, const char* message) {
	switch (severity) {
		case ErrorSeverity::WARNING:
			LOG_WARN(component, "%s", message);
			return;
		case ErrorSeverity::ERROR:
			LOG_ERROR(component, "%s", message);
			return;
		case ErrorSeverity::FATAL:
			LOG_ERROR(component, "FATAL: %s", message);
			break;
	}

	while (true) {
		smart_delay(500);
	}
}
