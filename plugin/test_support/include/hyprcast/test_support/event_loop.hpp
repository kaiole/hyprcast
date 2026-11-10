#pragma once

#include <wayland-server-core.h>

#include <chrono>
#include <memory>
#include <stdexcept>

namespace hyprcast::test_support {

// A real Wayland event loop for the code under test to register with, driven
// by the test instead of a compositor. Destroy everything registered with it
// before the loop itself.
class event_loop {
public:
    event_loop() : loop_(::wl_event_loop_create()) {
        if (!loop_) {
            throw std::runtime_error("Cannot create a Wayland event loop");
        }
    }

    [[nodiscard]] ::wl_event_loop* get() const noexcept { return loop_.get(); }

    // Dispatches events until `done` returns true. Returns false if `timeout`
    // passes first.
    template <typename Predicate>
    [[nodiscard]] bool
    run_until(const Predicate& done,
              std::chrono::milliseconds timeout = std::chrono::seconds {5}) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!done()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            // The other end of a test's socket is not registered with the
            // loop, so wake up regularly to let `done` look at it.
            ::wl_event_loop_dispatch(loop_.get(), poll_interval_ms);
        }
        return true;
    }

    // Dispatches the events that are ready now, without waiting.
    void dispatch_pending() { ::wl_event_loop_dispatch(loop_.get(), 0); }

private:
    static constexpr int poll_interval_ms = 2;

    struct destroyer {
        void operator()(::wl_event_loop* loop) const noexcept {
            ::wl_event_loop_destroy(loop);
        }
    };

    std::unique_ptr<::wl_event_loop, destroyer> loop_;
};

} // namespace hyprcast::test_support
