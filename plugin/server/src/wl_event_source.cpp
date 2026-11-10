#include "hyprcast/server/wl_event_source.hpp"

#include <wayland-server-core.h>

namespace hyprcast {

void wl_event_source_remover::operator()(
    struct ::wl_event_source* event_source) const noexcept {
    ::wl_event_source_remove(event_source);
}

} // namespace hyprcast
