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
#include <vector>

struct wl_event_loop;

namespace hyprcast {

// Decides what the connected client is told about the keyboards, and when.
// Capture starts paused and pauses again whenever the client goes away, so
// input flows only after the client's own `enable`. Nothing else can unpause
// it.
//
// Input flows only while capture is enabled and the session is not locked.
// Otherwise the client is told the capture state and nothing else: a
// keyboard's modifiers, keymap and repeat settings are input too. The client
// knows of a keyboard only while input flows. When the flow starts every
// keyboard is announced as it is then, and when it stops they are all
// withdrawn, so the client never holds state from before a gap.
//
// The lock is noticed when the keyboard source next reports something or the
// client sends a command, not when it happens.
//
// The keyboard source reports to the on_*() functions and is asked for the
// current keyboards through one callback. Everything runs on the thread that
// dispatches `loop`.
class session {
public:
    // Every keyboard a client should know about, as it is now. Called each
    // time input starts to flow; a failure disconnects the client.
    using snapshot_cb = std::function<registry_snapshot()>;
    // Whether the session is locked, which is asked before anything derived
    // from input is sent. While it is, a password is being typed.
    using locked_cb = std::function<bool()>;

    // Serves the event socket in `sock_dir`. `loop` and `log` must outlive
    // the session.
    session(wl_event_loop* loop,
            const std::filesystem::path& sock_dir,
            log_sink& log,
            snapshot_cb take_snapshot,
            locked_cb is_locked);

    // The server's callbacks capture `this`, so the object must never move.
    session(const session&) = delete;
    session& operator=(const session&) = delete;
    session(session&&) = delete;
    session& operator=(session&&) = delete;
    ~session() = default;

    // What the keyboard source reports. Each throws if the message cannot be
    // encoded; while no input flows the report is dropped.
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
    // Starts or stops the flow of input to match the capture state and the
    // session lock, and returns whether input flows.
    bool sync_flow();
    void start_flow();
    void stop_flow();
    void forget_client() noexcept;

    snapshot_cb take_snapshot_;
    locked_cb is_locked_;
    bool paused_ = true;
    bool flowing_ = false;
    // The keyboards the client has been told about and not yet lost.
    std::vector<keyboard_id> announced_;
    // Last, so that its callbacks never see a half-built session.
    event_server server_;
};

} // namespace hyprcast
