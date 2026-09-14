#include "hyprcast/core/WlEventSource.hpp"

#include <wayland-server-core.h>

namespace Hyprcast {
    void SWlEventSourceRemover::operator()(struct wl_event_source* eventSource) const noexcept {
        wl_event_source_remove(eventSource);
    }
}
