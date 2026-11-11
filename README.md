# Hyprcast

Shows your keystrokes on screen in Hyprland.

<p align="center">
  <img alt="Demo" src="docs/demo.avif" />
</p>

## Install

### Arch Linux

```sh
curl -O https://raw.githubusercontent.com/kaiole/hyprcast/main/PKGBUILD
makepkg -si
```

This builds the `main` branch and installs the package `hyprcast-git`: `hyprcast-overlay` in `/usr/bin`, `libhyprcast.so` in `/usr/lib` and the example themes in `/usr/share/hyprcast/themes`. In Setup below, load `/usr/lib/libhyprcast.so`.

Run the same two commands again after every Hyprland update, and to update Hyprcast.

### From source

Requirements:

- Hyprland with Lua config
- CMake 3.27+, Ninja and GCC with C++23 support
- Plugin: Hyprland headers matching your running version, Lua 5.5, libdrm, libinput, libudev, pixman, wayland-server, libxkbcommon
- Overlay: Qt 6.5+ (Core, Gui, Network, Qml, Quick), LayerShellQt, libxkbcommon, toml++ 3.4+ (downloaded if not installed)
- Tests, built by default: GoogleTest (downloaded if not installed) and hyprutils

```sh
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix "$HOME/.local"
```

The first command needs network access: it downloads libjson, and toml++ and GoogleTest if they are not installed. Add `-DBUILD_TESTING=OFF` to it to leave the tests out.

This installs `hyprcast-overlay` to `~/.local/bin`, `libhyprcast.so` to `~/.local/lib`, and the example themes to `~/.local/share/hyprcast/themes`. The key binding below needs `~/.local/bin` on the `PATH` of your Hyprland session.

Build and install again after every Hyprland update: the plugin refuses to load into a Hyprland it was not built for.

After updating Hyprcast, reload the plugin (`hyprctl plugin unload` and then `hyprctl plugin load` with the path from Setup, or restart Hyprland) and end the running overlay, with `hyprcast-overlay quit`: the next `hyprcast-overlay toggle` starts the new one. `hyprcast-overlay restart` is not enough, because it keeps the running program. The overlay refuses to capture with a plugin from another version of the protocol and tells you which of the two to replace.

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
- Nothing is sent to the overlay while the session is locked by a session-lock screen such as `hyprlock`, so a password typed there is never shown.
- A password typed anywhere else, such as a terminal, a polkit prompt or a locker that is an ordinary window, is shown like any other input while capture is on.
- Keystrokes go over a local Unix socket under `$XDG_RUNTIME_DIR`, to one overlay at a time.
- Themes run as code with your user's permissions. Only install themes you trust.

## License

[MIT](LICENSE)
