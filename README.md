# Hyprcast

Shows your keystrokes on screen in Hyprland.

<p align="center">
  <img alt="Demo" src="docs/demo.avif" />
</p>

## Install

Requirements:

- Hyprland with Lua config
- CMake 3.27+ and a C++23 compiler
- Plugin: Hyprland headers matching your running version, Lua 5.5, libdrm, libinput, libudev, pixman, wayland-server, libxkbcommon
- Overlay: Qt 6.5+ (Core, Gui, Network, Qml, Quick), LayerShellQt, libxkbcommon

```sh
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix "$HOME/.local"
```

This installs `hyprcast-overlay` to `~/.local/bin`, `libhyprcast.so` to `~/.local/lib`, and the example themes to `~/.local/share/hyprcast/themes`.

Rebuild and reload the plugin after every Hyprland update.

## Setup

Load the plugin and bind a key in your Hyprland config:

```lua
hl.on("hyprland.start", function()
    hl.exec_cmd("hyprctl plugin load " .. os.getenv("HOME") .. "/.local/lib/libhyprcast.so")
end)

hl.bind("SUPER + O", hl.dsp.exec_cmd("hyprcast-overlay toggle"))
```

Press the key to show the overlay and start capturing. Press it again to hide it and stop capturing.

## Configure

Create `~/.config/hyprcast/overlay.toml`:

```toml
[window]
anchor = "bottom-right"

[appearance]
font_size = 28
background_opacity = 0.8

[theme]
id = "keycaps"
```

Changes apply on save. See [configuration](docs/configuration.md) for all settings.

## Themes

| Theme | Look |
|---|---|
| `builtin:default` | Plain text (default) |
| `keycaps` | Row of keycaps |
| `ledger` | Vertical timeline |
| `text-held` | Keycaps plus held modifiers |
| `cascade` | Stack of keycaps |

Select one with `[theme] id`. See [themes](docs/themes.md) for options and making your own.

## Privacy

- Capture is off until you press the toggle key, and stops when the overlay closes.
- Keystrokes go over a local socket in your Hyprland session only.
- Themes run as code with your user's permissions. Only install themes you trust.
