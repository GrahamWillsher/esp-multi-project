#ifndef SSE_NOTIFIER_H
#define SSE_NOTIFIER_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

class SSENotifier {
private:
    static EventGroupHandle_t event_group;
    static const EventBits_t DATA_UPDATED_BIT = (1 << 0);
    
public:
    static void init();
    static void notifyDataUpdated();
    static bool waitForUpdate(TickType_t timeout_ms);
    static EventGroupHandle_t getEventGroup();
};

#endif
