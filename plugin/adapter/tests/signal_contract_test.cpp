#include <gtest/gtest.h>
#include <hyprutils/signal/Listener.hpp>
#include <hyprutils/signal/Signal.hpp>

#include <memory>
#include <utility>

namespace {

using Hyprutils::Signal::CHyprSignalListener;
using Hyprutils::Signal::CSignalT;

struct record {
    CHyprSignalListener listener;
};

// keyboard_registry::remove_keyboard() destroys a keyboard's record from
// inside that record's own destroy listener. That is sound only while a signal
// keeps a listener, and with it everything the listener captured, alive until
// the listener has returned.
TEST(HyprutilsSignal, ListenerDroppedFromItsOwnCallbackOutlivesTheCall) {
    CSignalT<> signal;
    auto owner = std::make_unique<record>();
    auto state = std::make_shared<int>(7);
    const std::weak_ptr<int> watched = state;
    int calls = 0;
    bool alive_after_drop = false;

    owner->listener = signal.listen([&owner,
                                     &watched,
                                     &calls,
                                     &alive_after_drop,
                                     captured = std::move(state)] {
        ++calls;
        // The only reference to the listener outside the signal.
        owner.reset();
        alive_after_drop = !watched.expired() && *captured == 7;
    });
    signal.emit();

    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(alive_after_drop);
    // The emission was all that held it: it is gone now and not called again.
    EXPECT_TRUE(watched.expired());
    signal.emit();
    EXPECT_EQ(calls, 1);
}

} // namespace
