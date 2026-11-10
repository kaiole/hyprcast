#include "WlEventSource.hpp"

#include <wayland-server-core.h>

namespace hyprcast {

void SWlEventSourceRemover::operator()(
    struct ::wl_event_source* eventSource) const noexcept {
    ::wl_event_source_remove(eventSource);
}

} // namespace hyprcast
