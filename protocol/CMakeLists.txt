add_library(hyprcast_protocol INTERFACE)
add_library(hyprcast::protocol ALIAS hyprcast_protocol)

target_sources(
  hyprcast_protocol
  INTERFACE FILE_SET
            HEADERS
            BASE_DIRS
            include
            FILES
            include/hyprcast/protocol/KeyEvent.hpp
            include/hyprcast/protocol/KeyState.hpp
            include/hyprcast/protocol/KeyboardId.hpp
            include/hyprcast/protocol/KeyboardSnapshot.hpp
            include/hyprcast/protocol/Keymap.hpp
            include/hyprcast/protocol/Modifiers.hpp
            include/hyprcast/protocol/RegistrySnapshot.hpp
            include/hyprcast/protocol/RepeatInfo.hpp
            include/hyprcast/protocol/Wire.hpp)
