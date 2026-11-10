#include "hyprcast/session/session.hpp"

#include "hyprcast/log/log_sink.hpp"
#include "hyprcast/protocol/key_event.hpp"
#include "hyprcast/protocol/keyboard_id.hpp"
#include "hyprcast/protocol/keyboard_snapshot.hpp"
#include "hyprcast/protocol/keymap_text.hpp"
#include "hyprcast/protocol/modifier_state.hpp"
#include "hyprcast/protocol/repeat_info.hpp"
#include "hyprcast/wire/messages.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

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
          [this] {
              server_.queue_message(wire::casting_state_message(paused_));
          },
          [this](bool enabled) { set_paused(!enabled); },
          [this] { forget_client(); }) {}

void session::on_subscribe(keyboard_snapshot snapshot) {
    if (!sync_flow()) {
        return;
    }
    const auto id = snapshot.id;
    auto message = wire::subscribe_message(std::move(snapshot));
    if (!std::ranges::contains(announced_, id)) {
        announced_.push_back(id);
    }
    server_.queue_message(std::move(message));
}

void session::on_unsubscribe(keyboard_id id) {
    if (sync_flow() && std::erase(announced_, id) > 0) {
        server_.queue_message(wire::unsubscribe_message(id));
    }
}

void session::on_key_event(keyboard_id id, const key_event& event) {
    if (sync_flow()) {
        server_.queue_message(wire::key_message(id, event));
    }
}

void session::on_modifiers_event(keyboard_id id,
                                 const modifier_state& modifiers) {
    if (sync_flow()) {
        server_.queue_message(wire::modifiers_message(id, modifiers));
    }
}

void session::on_keymap_event(keyboard_id id, keymap_text keymap) {
    if (sync_flow()) {
        server_.queue_message(wire::keymap_message(id, std::move(keymap)));
    }
}

void session::on_repeat_info_event(keyboard_id id, const repeat_info& info) {
    if (sync_flow()) {
        server_.queue_message(wire::repeat_info_message(id, info));
    }
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

void session::set_paused(bool paused) {
    // Always echo the authoritative state, including idempotent control
    // requests.
    paused_ = paused;
    server_.queue_message(wire::casting_state_message(paused_));
    sync_flow();
}

// Queueing a message can drop the client, which resets the state below
// through forget_client(). So each step settles its own state before it
// queues anything, and nothing here relies on that state afterwards.

bool session::sync_flow() {
    const bool wanted = !paused_;
    if (wanted && !flowing_) {
        start_flow();
    } else if (!wanted && flowing_) {
        stop_flow();
    }
    return flowing_;
}

void session::start_flow() {
    // Encode everything first: the client is told about all of the keyboards
    // or, if one cannot be encoded, about none.
    std::vector<keyboard_id> ids;
    std::vector<std::string> messages;
    for (auto& snapshot : take_snapshot_()) {
        ids.push_back(snapshot.id);
        messages.push_back(wire::subscribe_message(std::move(snapshot)));
    }

    announced_ = std::move(ids);
    flowing_ = true;
    for (auto& message : messages) {
        server_.queue_message(std::move(message));
    }
}

void session::stop_flow() {
    flowing_ = false;
    for (const auto id : std::exchange(announced_, {})) {
        server_.queue_message(wire::unsubscribe_message(id));
    }
}

void session::forget_client() noexcept {
    paused_ = true;
    flowing_ = false;
    announced_.clear();
}

} // namespace hyprcast
