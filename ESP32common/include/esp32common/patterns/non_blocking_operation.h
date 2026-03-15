#pragma once

#include <cstdint>

/**
 * @brief Reusable base for state-based non-blocking operations.
 *
 * Call `update()` from your loop/task; no blocking delays are used.
 */
template <typename Context = void>
class NonBlockingOperation {
public:
    enum class State : uint8_t {
        IDLE = 0,
        IN_PROGRESS,
        SUCCESS,
        FAILED
    };

    virtual ~NonBlockingOperation() = default;

    State state() const { return state_; }
    bool is_in_progress() const { return state_ == State::IN_PROGRESS; }
    bool is_complete() const { return state_ == State::SUCCESS || state_ == State::FAILED; }

    void reset() {
        state_ = State::IDLE;
        start_ms_ = 0;
    }

    /**
     * @brief Drive operation state machine once.
     * @param now_ms Current monotonic time in ms
     * @param context Optional caller-provided context pointer
     */
    void update(uint32_t now_ms, Context* context = nullptr) {
        switch (state_) {
            case State::IDLE:
                state_ = State::IN_PROGRESS;
                start_ms_ = now_ms;
                on_start(now_ms, context);
                break;
            case State::IN_PROGRESS:
                on_update(now_ms, now_ms - start_ms_, context);
                break;
            case State::SUCCESS:
            case State::FAILED:
            default:
                break;
        }
    }

protected:
    void set_success() { state_ = State::SUCCESS; }
    void set_failed() { state_ = State::FAILED; }

    virtual void on_start(uint32_t now_ms, Context* context) = 0;
    virtual void on_update(uint32_t now_ms, uint32_t elapsed_ms, Context* context) = 0;

private:
    State state_{State::IDLE};
    uint32_t start_ms_{0};
};
