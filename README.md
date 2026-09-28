# Hyprcast

Hyprcast displays keyboard input in a customizable overlay for Hyprland. A Hyprland plugin streams keyboard events to a separate Qt Quick/LayerShellQt overlay, which renders typed text, shortcuts, and optional held-key feedback. The overlay supports TOML configuration and user-installable QML themes.

<p align="center">
  <img width="500" alt="Demo" src="https://github.com/user-attachments/assets/5fe39522-1e70-4c16-b6f0-dabea60e886a" />
</p>

## Requirements

- Hyprland with Lua plugin support; build the plugin against headers matching the running Hyprland version.
- CMake 3.27+ and a C++23 compiler.
- Plugin: Lua 5.5 development files, Hyprland development files, libdrm, libinput, libudev, pixman, Wayland server, and libxkbcommon. CMake fetches the pinned libjson dependency.
- Overlay: Qt 6.5+ (Core, Gui, Network, Qml, Quick), LayerShellQt, libxkbcommon 0.7+, and toml++ 3.4+ (fetched if not installed).

The required development packages must be discoverable through CMake/pkg-config. The bundled default theme needs no separate installation.

## Build and install

```sh
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix "$HOME/.local"
```

To build only the overlay, add `-DHYPRCAST_BUILD_PLUGIN=OFF` to the configure command. The plugin is version-sensitive: rebuild and reload it when updating Hyprland. Ensure the installed `bin` directory is on your `PATH`, or use an absolute path in the keybind.

## Use

Load `libhyprcast.so` through your Hyprland plugin setup, then add a Lua keybind to your Hyprland configuration:

```lua
hl.bind("SUPER + O", hl.dsp.exec_cmd("hyprcast-overlay toggle"))
```

The first press launches the overlay and enables capture; subsequent presses hide/clear it and pause capture, or show it again. The overlay runs in the foreground on first launch and remains resident while hidden. Plugin loading and keybind configuration depend on your Hyprland setup; use the installed plugin path (for example `$HOME/.local/lib/libhyprcast.so`) when loading it. Keyboard events are exposed through a per-session local socket; only run the overlay in a session you trust.

To run from a source checkout instead, use absolute paths to `build/release/plugin/libhyprcast.so` and `build/release/overlay/hyprcast-overlay`. The plugin and overlay must both be up to date.

## Configuration and themes

The overlay reads `${XDG_CONFIG_HOME:-~/.config}/hyprcast/overlay.toml` if present. CLI options can override settings; see `hyprcast-overlay --help`. For a complete configuration example, live-reload behavior, theme installation, and the theme API, see [overlay configuration and themes](docs/overlay.md). An example theme is included in [`overlay/examples/themes/ledger/`](overlay/examples/themes/ledger/).

## Tests

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The automated suite does not replace testing with a live Hyprland session and physical keyboard input.
