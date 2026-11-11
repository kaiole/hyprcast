add_library(hyprcast_protocol INTERFACE)
add_library(hyprcast::protocol ALIAS hyprcast_protocol)

target_sources(
  hyprcast_protocol
  INTERFACE FILE_SET
            HEADERS
            BASE_DIRS
            include
            FILES
            include/hyprcast/protocol/key_event.hpp
            include/hyprcast/protocol/key_state.hpp
            include/hyprcast/protocol/keyboard_id.hpp
            include/hyprcast/protocol/keyboard_snapshot.hpp
            include/hyprcast/protocol/keymap_text.hpp
            include/hyprcast/protocol/modifier_state.hpp
            include/hyprcast/protocol/registry_snapshot.hpp
            include/hyprcast/protocol/repeat_info.hpp
            include/hyprcast/protocol/wire.hpp)
