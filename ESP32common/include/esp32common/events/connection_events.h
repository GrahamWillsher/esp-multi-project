#pragma once

#include <cstddef>
#include <cstdint>

struct DataFrameView {
    const uint8_t* data{nullptr};
    size_t length{0};
};

struct ConnectionError {
    uint16_t code{0};
    const char* message{nullptr};
};

/**
 * @brief Listener interface for connection lifecycle events.
 */
class IConnectionListener {
public:
    virtual ~IConnectionListener() = default;

    virtual void on_transmitter_connected() {}
    virtual void on_transmitter_disconnected() {}
    virtual void on_data_received(const DataFrameView&) {}
    virtual void on_error(const ConnectionError&) {}
};

/**
 * @brief Lightweight listener registry and dispatcher.
 *
 * Uses a fixed-size listener list to avoid heap allocations.
 */
class ConnectionEventManager {
public:
    static ConnectionEventManager& instance() {
        static ConnectionEventManager inst;
        return inst;
    }

    bool register_listener(IConnectionListener* listener) {
        if (!listener) {
            return false;
        }

        for (size_t i = 0; i < listener_count_; ++i) {
            if (listeners_[i] == listener) {
                return true;
            }
        }

        if (listener_count_ >= MAX_LISTENERS) {
            return false;
        }

        listeners_[listener_count_++] = listener;
        return true;
    }

    void unregister_listener(IConnectionListener* listener) {
        if (!listener) {
            return;
        }

        for (size_t i = 0; i < listener_count_; ++i) {
            if (listeners_[i] == listener) {
                for (size_t j = i + 1; j < listener_count_; ++j) {
                    listeners_[j - 1] = listeners_[j];
                }
                listeners_[listener_count_ - 1] = nullptr;
                listener_count_--;
                return;
            }
        }
    }

    void notify_connected() {
        for (size_t i = 0; i < listener_count_; ++i) {
            listeners_[i]->on_transmitter_connected();
        }
    }

    void notify_disconnected() {
        for (size_t i = 0; i < listener_count_; ++i) {
            listeners_[i]->on_transmitter_disconnected();
        }
    }

    void notify_data_received(const DataFrameView& data) {
        for (size_t i = 0; i < listener_count_; ++i) {
            listeners_[i]->on_data_received(data);
        }
    }

    void notify_error(const ConnectionError& error) {
        for (size_t i = 0; i < listener_count_; ++i) {
            listeners_[i]->on_error(error);
        }
    }

private:
    static constexpr size_t MAX_LISTENERS = 16;

    ConnectionEventManager() = default;

    IConnectionListener* listeners_[MAX_LISTENERS]{nullptr};
    size_t listener_count_{0};
};
