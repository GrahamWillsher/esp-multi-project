#include "channel_authority.h"

#include "channel_manager.h"
#include "unified_link_fsm.h"

#include <Arduino.h>
#include <Preferences.h>

namespace esp32common::espnow {
namespace {
constexpr const char* kPrefsNamespace = "esp_link";
constexpr const char* kLastGoodKey = "last_good";
}  // namespace

ChannelAuthority& ChannelAuthority::instance() {
    static ChannelAuthority authority;
    return authority;
}

bool ChannelAuthority::init() {
    if (!ChannelManager::instance().init()) {
        return false;
    }

    const uint32_t now_ms = static_cast<uint32_t>(millis());
    const uint8_t live_channel = ChannelManager::instance().get_channel();
    const uint8_t persisted_last_good = load_last_good_channel();

    portENTER_CRITICAL(&mux_);
    snapshot_.current_radio_channel = live_channel;
    snapshot_.candidate_channel = 0;
    snapshot_.operating_channel = live_channel;
    snapshot_.last_good_channel = is_valid_channel(persisted_last_good) ? persisted_last_good : live_channel;
    snapshot_.locked = ChannelManager::instance().is_locked();
    snapshot_.last_update_ms = now_ms;
    initialized_ = true;
    portEXIT_CRITICAL(&mux_);

    if (is_valid_channel(snapshot_.last_good_channel)) {
        UnifiedLinkFsm::instance().set_last_good_channel(snapshot_.last_good_channel, now_ms);
    }
    if (is_valid_channel(live_channel)) {
        UnifiedLinkFsm::instance().set_operating_channel(live_channel, now_ms);
    }

    return true;
}

ChannelAuthoritySnapshot ChannelAuthority::snapshot() const {
    portENTER_CRITICAL(const_cast<portMUX_TYPE*>(&mux_));
    const ChannelAuthoritySnapshot snapshot = snapshot_;
    portEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&mux_));
    return snapshot;
}

uint8_t ChannelAuthority::operating_channel() const {
    return snapshot().operating_channel;
}

uint8_t ChannelAuthority::candidate_channel() const {
    return snapshot().candidate_channel;
}

uint8_t ChannelAuthority::last_good_channel() const {
    return snapshot().last_good_channel;
}

bool ChannelAuthority::is_locked() const {
    return snapshot().locked;
}

uint8_t ChannelAuthority::reconnect_hint_channel() const {
    const ChannelAuthoritySnapshot snapshot = this->snapshot();
    if (is_valid_channel(snapshot.last_good_channel)) {
        return snapshot.last_good_channel;
    }
    if (is_valid_channel(snapshot.current_radio_channel)) {
        return snapshot.current_radio_channel;
    }
    return 1;
}

void ChannelAuthority::set_candidate_channel(uint8_t channel, const char* /*source*/) {
    if (!is_valid_channel(channel)) {
        return;
    }

    const uint32_t now_ms = static_cast<uint32_t>(millis());
    sync_from_channel_manager(now_ms);
    portENTER_CRITICAL(&mux_);
    snapshot_.candidate_channel = channel;
    snapshot_.last_update_ms = now_ms;
    portEXIT_CRITICAL(&mux_);
}

bool ChannelAuthority::apply_candidate_channel(const char* source) {
    const ChannelAuthoritySnapshot snapshot = this->snapshot();
    if (!is_valid_channel(snapshot.candidate_channel)) {
        return false;
    }

    const bool ok = ChannelManager::instance().set_channel(snapshot.candidate_channel, source);
    sync_from_channel_manager(static_cast<uint32_t>(millis()));
    if (ok) {
        UnifiedLinkFsm::instance().set_operating_channel(snapshot.candidate_channel);
    }
    return ok;
}

bool ChannelAuthority::commit_receiver_confirmed_channel(uint8_t channel,
                                                         const char* source,
                                                         bool persist_last_good) {
    if (!is_valid_channel(channel)) {
        return false;
    }

    ChannelManager::instance().lock_channel(channel, source);
    const uint32_t now_ms = static_cast<uint32_t>(millis());
    sync_from_channel_manager(now_ms);

    portENTER_CRITICAL(&mux_);
    snapshot_.candidate_channel = channel;
    snapshot_.operating_channel = channel;
    snapshot_.locked = true;
    snapshot_.last_update_ms = now_ms;
    if (persist_last_good) {
        snapshot_.last_good_channel = channel;
    }
    portEXIT_CRITICAL(&mux_);

    UnifiedLinkFsm::instance().set_operating_channel(channel, now_ms);
    if (persist_last_good) {
        UnifiedLinkFsm::instance().set_last_good_channel(channel, now_ms);
        save_last_good_channel(channel);
    }
    return true;
}

void ChannelAuthority::persist_last_good_channel(uint8_t channel, const char* /*source*/) {
    if (!is_valid_channel(channel)) {
        return;
    }

    const uint32_t now_ms = static_cast<uint32_t>(millis());
    portENTER_CRITICAL(&mux_);
    snapshot_.last_good_channel = channel;
    snapshot_.last_update_ms = now_ms;
    portEXIT_CRITICAL(&mux_);

    UnifiedLinkFsm::instance().set_last_good_channel(channel, now_ms);
    save_last_good_channel(channel);
}

void ChannelAuthority::unlock(const char* source) {
    ChannelManager::instance().unlock_channel(source);
    sync_from_channel_manager(static_cast<uint32_t>(millis()));
}

void ChannelAuthority::on_ack_channel_reported(uint8_t channel,
                                               bool apply_wifi_channel,
                                               const char* source) {
    set_candidate_channel(channel, source);
    if (apply_wifi_channel) {
        (void)apply_candidate_channel(source);
    }
}

void ChannelAuthority::sync_from_channel_manager(uint32_t now_ms) {
    const uint8_t live_channel = ChannelManager::instance().get_channel();
    const bool locked = ChannelManager::instance().is_locked();

    portENTER_CRITICAL(&mux_);
    snapshot_.current_radio_channel = live_channel;
    snapshot_.locked = locked;
    if (locked && is_valid_channel(live_channel)) {
        snapshot_.operating_channel = live_channel;
    } else if (!is_valid_channel(snapshot_.operating_channel)) {
        snapshot_.operating_channel = live_channel;
    }
    snapshot_.last_update_ms = now_ms;
    portEXIT_CRITICAL(&mux_);
}

uint8_t ChannelAuthority::load_last_good_channel() const {
    Preferences prefs;
    // Open read/write so missing namespace is created silently on first boot.
    if (!prefs.begin(kPrefsNamespace, false)) {
        return 0;
    }

    if (!prefs.isKey(kLastGoodKey)) {
        prefs.end();
        return 0;
    }

    const uint8_t channel = prefs.getUChar(kLastGoodKey, 0);
    prefs.end();
    return is_valid_channel(channel) ? channel : 0;
}

void ChannelAuthority::save_last_good_channel(uint8_t channel) const {
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
        return;
    }

    prefs.putUChar(kLastGoodKey, channel);
    prefs.end();
}

}  // namespace esp32common::espnow
