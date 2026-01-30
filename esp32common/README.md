# ESP32 Common Library

This folder contains shared code for ESP-NOW communication between ESP32 devices.

## Architecture

The library follows a **utility-based** design where applications have full control over their message handling:

### espnow_common.h
Shared protocol definitions used by all ESP-NOW applications:
- Message types (msg_probe, msg_ack, msg_data, msg_request_data, msg_abort_data, msg_packet)
- Message subtypes (subtype_none, subtype_settings, subtype_events, subtype_logs, subtype_cell_info)
- Data structures (espnow_payload_t, probe_t, ack_t, espnow_packet_t, etc.)

### espnow_transmitter/
ESP-NOW utility library providing:
- WiFi initialization
- ESP-NOW initialization
- Channel discovery and locking
- Helper functions (send_probe, ensure_peer_added, set_channel)
- Example send function (send_test_data)

**The library does NOT:**
- Create tasks
- Process received messages
- Make decisions about what data to send

## Usage Pattern

Each application should:

1. **Include the common header:**
   ```cpp
   #include <espnow_transmitter/espnow_common.h>
   ```

2. **Create its own queue:**
   ```cpp
   QueueHandle_t espnow_rx_queue = xQueueCreate(10, sizeof(espnow_queue_msg_t));
   ```

3. **Initialize ESP-NOW:**
   ```cpp
   init_wifi();
   init_espnow(espnow_rx_queue);
   ```

4. **Create its own RX task to handle messages:**
   ```cpp
   xTaskCreate(app_espnow_rx_task, "app_espnow_rx", 4096, NULL, 5, NULL);
   ```

5. **Discover and lock channel:**
   ```cpp
   discover_and_lock_channel();
   ```

6. **Handle messages in the RX task:**
   ```cpp
   void app_espnow_rx_task(void* parameter) {
       espnow_queue_msg_t msg;
       while (true) {
           if (xQueueReceive(espnow_rx_queue, &msg, portMAX_DELAY) == pdTRUE) {
               uint8_t msg_type = msg.data[0];
               switch (msg_type) {
                   case msg_ack:
                       // Handle ACK for channel discovery
                       break;
                   case msg_request_data:
                       // Start sending data
                       break;
                   case msg_abort_data:
                       // Stop sending data
                       break;
                   // ... handle other message types
               }
           }
       }
   }
   ```

## Benefits

- **Simple**: Library provides utilities, applications control flow
- **Maintainable**: Clear separation between library and application code
- **Flexible**: Each application can handle messages differently
- **Efficient**: No unnecessary tasks or message processing
- **Testable**: Applications can mock the queue for testing

## Projects

- **ESPnowtransmitter**: Battery emulator transmitter (T-Connect Pro)
- **ESPNowreciever**: Data receiver with display (T-Display-S3)
