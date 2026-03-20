#include "network/service_supervisor.h"

#include "network/ethernet_manager.h"
#include "config/network_config.h"
#include "network/mqtt_manager.h"
#include "network/ota_manager.h"
#include "config/logging_config.h"

ServiceSupervisor& ServiceSupervisor::instance() {
    static ServiceSupervisor instance;
    return instance;
}

void ServiceSupervisor::attach_to_ethernet() {
    if (!callbacks_registered_) {
        EthernetManager::instance().on_connected([] {
            ServiceSupervisor::instance().handle_ethernet_connected();
        });
        EthernetManager::instance().on_disconnected([] {
            ServiceSupervisor::instance().handle_ethernet_disconnected();
        });
        callbacks_registered_ = true;
    }

    if (EthernetManager::instance().is_connected()) {
        handle_ethernet_connected();
    }
}

void ServiceSupervisor::handle_ethernet_connected() {
    if (services_active_) {
        LOG_DEBUG("SERVICE_SUPERVISOR", "Ethernet services already active");
        return;
    }

    LOG_INFO("SERVICE_SUPERVISOR", "Ethernet connected - starting dependent services");
    OtaManager::instance().init_http_server();

    if (config::features::MQTT_ENABLED) {
        MqttManager::instance().init();
    }

    services_active_ = true;
}

void ServiceSupervisor::handle_ethernet_disconnected() {
    if (!services_active_) {
        LOG_DEBUG("SERVICE_SUPERVISOR", "Ethernet services already inactive");
        return;
    }

    LOG_WARN("SERVICE_SUPERVISOR", "Ethernet disconnected - stopping dependent services");
    OtaManager::instance().stop_http_server();

    if (config::features::MQTT_ENABLED) {
        MqttManager::instance().disconnect();
    }

    services_active_ = false;
}
