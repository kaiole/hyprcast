#pragma once

#include "hyprcast/log/log_sink.hpp"
#include "hyprcast/protocol/key_event.hpp"
#include "hyprcast/protocol/keyboard_id.hpp"
#include "hyprcast/protocol/keyboard_snapshot.hpp"
#include "hyprcast/protocol/keymap_text.hpp"
#include "hyprcast/protocol/modifier_state.hpp"
#include "hyprcast/protocol/registry_snapshot.hpp"
#include "hyprcast/protocol/repeat_info.hpp"
#include "hyprcast/server/event_server.hpp"

#include <filesystem>
#include <functional>

struct wl_event_loop;

namespace hyprcast {

// Decides what the connected client is told about the keyboards, and when.
// Capture starts paused and pauses again whenever the client goes away, so
// key events flow only after the client's own `enable`. Nothing else can
// unpause it.
//
// The keyboard source reports to the on_*() functions and is asked for the
// current keyboards through one callback. Everything runs on the thread that
// dispatches `loop`.
class session {
public:
    // Every keyboard a client should know about. Called each time a client
    // connects; a failure disconnects that client.
    using snapshot_cb = std::function<registry_snapshot()>;

    // Serves the event socket in `sock_dir`. `loop` and `log` must outlive
    // the session.
    session(wl_event_loop* loop,
            const std::filesystem::path& sock_dir,
            log_sink& log,
            snapshot_cb take_snapshot);

    // The server's callbacks capture `this`, so the object must never move.
    session(const session&) = delete;
    session& operator=(const session&) = delete;
    session(session&&) = delete;
    session& operator=(session&&) = delete;
    ~session() = default;

    // What the keyboard source reports. Each throws if the message cannot be
    // encoded; without a client the message is dropped.
    void on_subscribe(keyboard_snapshot snapshot);
    void on_unsubscribe(keyboard_id id);
    void on_key_event(keyboard_id id, const key_event& event);
    void on_modifiers_event(keyboard_id id, const modifier_state& modifiers);
    void on_keymap_event(keyboard_id id, keymap_text keymap);
    void on_repeat_info_event(keyboard_id id, const repeat_info& info);

    [[nodiscard]] bool paused() const noexcept;

    void disconnect_client() noexcept;

    // Drops the client and stops listening for good.
    void shutdown() noexcept;

private:
    // Pauses or resumes capture and tells the client the resulting state,
    // even if it did not change.
    void set_paused(bool paused);
    void send_snapshot();

    snapshot_cb take_snapshot_;
    bool paused_ = true;
    // Last, so that its callbacks never see a half-built session.
    event_server server_;
};

} // namespace hyprcast
