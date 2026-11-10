#pragma once

#include <memory>

struct wl_event_source;

namespace hyprcast {

struct wl_event_source_remover {
    void operator()(struct ::wl_event_source* event_source) const noexcept;
};

using unique_wl_event_source =
    std::unique_ptr<::wl_event_source, wl_event_source_remover>;

} // namespace hyprcast
