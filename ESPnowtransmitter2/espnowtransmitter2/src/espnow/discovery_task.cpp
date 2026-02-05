#include "discovery_task.h"
#include "message_handler.h"
#include "../config/task_config.h"
#include "../config/logging_config.h"
#include <Arduino.h>
#include <espnow_discovery.h>

DiscoveryTask& DiscoveryTask::instance() {
    static DiscoveryTask instance;
    return instance;
}

void DiscoveryTask::start() {
    // Use common discovery component with callback
    EspnowDiscovery::instance().start(
        []() -> bool {
            return EspnowMessageHandler::instance().is_receiver_connected();
        },
        timing::ANNOUNCEMENT_INTERVAL_MS,
        task_config::PRIORITY_LOW,
        task_config::STACK_SIZE_ANNOUNCEMENT
    );
    
    task_handle_ = EspnowDiscovery::instance().get_task_handle();
    LOG_DEBUG("[DISCOVERY] Using common discovery component");
}
