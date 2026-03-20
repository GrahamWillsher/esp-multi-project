#pragma once

/**
 * @brief Owns lifecycle of Ethernet-dependent services
 *
 * Centralizes start/stop transitions for services that depend on Ethernet
 * readiness, so `main.cpp` only wires bootstrap phases and the Ethernet state
 * machine remains the single event source.
 */
class ServiceSupervisor {
public:
    static ServiceSupervisor& instance();

    /**
     * @brief Register Ethernet callbacks and replay current state once
     *
     * Safe to call more than once; callback registration only happens on the
     * first call.
     */
    void attach_to_ethernet();

    /**
     * @brief Start all Ethernet-dependent services if not already active
     */
    void handle_ethernet_connected();

    /**
     * @brief Stop all Ethernet-dependent services if active
     */
    void handle_ethernet_disconnected();

    bool services_active() const { return services_active_; }

private:
    ServiceSupervisor() = default;
    ~ServiceSupervisor() = default;

    ServiceSupervisor(const ServiceSupervisor&) = delete;
    ServiceSupervisor& operator=(const ServiceSupervisor&) = delete;

    bool callbacks_registered_{false};
    bool services_active_{false};
};
