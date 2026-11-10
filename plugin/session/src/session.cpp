#include "hyprcast/session/session.hpp"

#include "hyprcast/log/log_sink.hpp"
#include "hyprcast/protocol/key_event.hpp"
#include "hyprcast/protocol/keyboard_id.hpp"
#include "hyprcast/protocol/keyboard_snapshot.hpp"
#include "hyprcast/protocol/keymap_text.hpp"
#include "hyprcast/protocol/modifier_state.hpp"
#include "hyprcast/protocol/repeat_info.hpp"
#include "hyprcast/wire/messages.hpp"

#include <filesystem>
#include <utility>

namespace hyprcast {

session::session(wl_event_loop* loop,
                 const std::filesystem::path& sock_dir,
                 log_sink& log,
                 snapshot_cb take_snapshot)
    : take_snapshot_(std::move(take_snapshot)),
      server_(
          loop,
          sock_dir,
          log,
          [this] { send_snapshot(); },
          [this](bool enabled) { set_paused(!enabled); },
          [this] { paused_ = true; }) {}

void session::on_subscribe(keyboard_snapshot snapshot) {
    server_.queue_message(wire::subscribe_message(std::move(snapshot)));
}

void session::on_unsubscribe(keyboard_id id) {
    server_.queue_message(wire::unsubscribe_message(id));
}

void session::on_key_event(keyboard_id id, const key_event& event) {
    if (!paused_) {
        server_.queue_message(wire::key_message(id, event));
    }
}

void session::on_modifiers_event(keyboard_id id,
                                 const modifier_state& modifiers) {
    server_.queue_message(wire::modifiers_message(id, modifiers));
}

void session::on_keymap_event(keyboard_id id, keymap_text keymap) {
    server_.queue_message(wire::keymap_message(id, std::move(keymap)));
}

void session::on_repeat_info_event(keyboard_id id, const repeat_info& info) {
    server_.queue_message(wire::repeat_info_message(id, info));
}

void session::set_paused(bool paused) {
    // Always echo the authoritative state, including idempotent control
    // requests.
    paused_ = paused;
    server_.queue_message(wire::casting_state_message(paused_));
}

bool session::paused() const noexcept {
    return paused_;
}

void session::disconnect_client() noexcept {
    server_.disconnect_client();
}

void session::shutdown() noexcept {
    server_.shutdown();
}

void session::send_snapshot() {
    server_.queue_message(wire::casting_state_message(paused_));

    auto snapshots = take_snapshot_();

    for (auto& snapshot : snapshots) {
        server_.queue_message(wire::subscribe_message(std::move(snapshot)));
    }
}

} // namespace hyprcast
