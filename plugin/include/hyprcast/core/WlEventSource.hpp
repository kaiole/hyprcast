#pragma once

#include <memory>

struct wl_event_source;

namespace Hyprcast {
    struct SWaylandEventSourceRemover {
        void operator()(struct wl_event_source* eventSource) const noexcept;
    };

    using CWlEventSource = std::unique_ptr<wl_event_source, SWaylandEventSourceRemover>;
}
