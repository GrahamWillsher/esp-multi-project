#pragma once

#include <cstdint>

/**
 * @brief Coordinates OTA intent between paired devices.
 *
 * This abstraction does not enforce a transport. Applications can bind transport
 * hooks (ESP-NOW/MQTT/HTTP signaling) through `set_hooks()`.
 */
class OtaCoordinator {
public:
    enum class DeviceType : uint8_t {
        RECEIVER = 0,
        TRANSMITTER = 1
    };

    using NotifyPeerBeforeUpdateFn = bool (*)(DeviceType device_type, const char* url);
    using WaitForPeerReadyFn = bool (*)(DeviceType device_type, uint32_t timeout_ms);

    static OtaCoordinator& instance() {
        static OtaCoordinator inst;
        return inst;
    }

    void set_hooks(NotifyPeerBeforeUpdateFn notify_fn,
                   WaitForPeerReadyFn wait_fn,
                   uint32_t default_wait_timeout_ms = 5000) {
        notify_peer_before_update_fn_ = notify_fn;
        wait_for_peer_ready_fn_ = wait_fn;
        wait_timeout_ms_ = default_wait_timeout_ms;
    }

    bool request_update(DeviceType device_type, const char* url) {
        if (update_in_progress_ || url == nullptr) {
            return false;
        }

        if (!is_device_available(device_type)) {
            return false;
        }

        if (notify_peer_before_update_fn_ && !notify_peer_before_update_fn_(device_type, url)) {
            return false;
        }

        if (wait_for_peer_ready_fn_ && !wait_for_peer_ready_fn_(device_type, wait_timeout_ms_)) {
            return false;
        }

        active_device_ = device_type;
        active_url_ = url;
        update_in_progress_ = true;
        return true;
    }

    bool cancel_update() {
        if (!update_in_progress_) {
            return false;
        }

        update_in_progress_ = false;
        active_url_ = nullptr;
        return true;
    }

    void complete_update() {
        update_in_progress_ = false;
        active_url_ = nullptr;
    }

    bool is_update_in_progress() const { return update_in_progress_; }

    bool is_device_available(DeviceType device_type) const {
        return device_available_[static_cast<uint8_t>(device_type)];
    }

    void set_device_available(DeviceType device_type, bool available) {
        device_available_[static_cast<uint8_t>(device_type)] = available;
    }

    DeviceType active_device() const { return active_device_; }
    const char* active_url() const { return active_url_; }

private:
    OtaCoordinator() = default;

    bool device_available_[2]{true, true};
    bool update_in_progress_{false};
    DeviceType active_device_{DeviceType::RECEIVER};
    const char* active_url_{nullptr};

    NotifyPeerBeforeUpdateFn notify_peer_before_update_fn_{nullptr};
    WaitForPeerReadyFn wait_for_peer_ready_fn_{nullptr};
    uint32_t wait_timeout_ms_{5000};
};
