# Configuration

Config file: `~/.config/hyprcast/overlay.toml`. Every setting is optional.

Changes apply on save. If the file is invalid, the overlay prints the error and keeps the last working config.

These need a restart:

- Changing `window.monitor` or `window.click_through`
- Installing or editing a theme

```sh
hyprcast-overlay restart
```

## Example

```toml
[window]
monitor = "eDP-1"
anchor = "bottom-right"
margins = [24, 24, 24, 24]
width = 700
height = 120

[appearance]
background_color = "#101722"
background_opacity = 0.82
corner_radius = 14
foreground_color = "#f4f6fa"
font_family = "JetBrains Mono"
font_size = 28

[symbols.keys]
Backspace = "⌫"
Enter = "⏎"

[symbols.modifiers]
Ctrl = "⌃"
Super = "◆"

[expiration]
after_ms = 5000
```

## Settings

Sizes are in logical pixels. Colors are names (`white`) or hex (`#RRGGBB`, `#AARRGGBB`).

### `[window]`

| Key | Default | Values |
|---|---|---|
| `monitor` | primary | Output name from `hyprctl monitors`. Needs restart. |
| `anchor` | `"bottom-right"` | `top`, `bottom`, `left`, `right`, `top-left`, `top-right`, `bottom-left`, `bottom-right` |
| `margins` | `[24, 24, 24, 24]` | Left, top, right, bottom |
| `width` | `600` | Max width, 1–8192 |
| `height` | `88` | Max height, 1–8192 |
| `dynamic_size` | `true` | Panel grows and shrinks with content |
| `min_width` | `240` | Up to `width` |
| `min_height` | `64` | Up to `height` |
| `click_through` | `true` | Clicks pass through the overlay. Needs restart. |

### `[appearance]`

| Key | Default | Values |
|---|---|---|
| `background_color` | `"#0e1116"` | Color |
| `background_opacity` | `0.78` | 0–1. Background only. |
| `corner_radius` | `12` | 0–256 |
| `panel_border_width` | `1` | 0–64. `0` removes the border. |
| `panel_border_color` | `"#ffffff"` | Color |
| `foreground_color` | `"#ffffff"` | Text color |
| `font_family` | `"Sans Serif"` | Font name |
| `font_size` | `30` | 1–256 |
| `font_weight` | `"medium"` | `normal`, `medium`, `demibold`, `bold` |
| `history_padding_x` | `16` | Panel side padding, 0–512 |
| `text_extra_padding_x` | `4` | Extra text side padding, 0–512 |

### `[display]`

| Key | Default | Values |
|---|---|---|
| `panel_visibility` | `"with-content"` | `always`, `with-content`, `never` (hides the panel, keeps the keys) |

### `[history]`

| Key | Default | Values |
|---|---|---|
| `backspace` | `"delete"` | `delete`: Backspace erases. `symbol`: Backspace is shown as a key. |
| `max_retained_utf16_code_units` | `4096` | How much history to keep (about one per character), 1–1048576 |

### `[repeat]`

| Key | Default | Values |
|---|---|---|
| `enabled` | `true` | Show auto-repeat from held keys |
| `presentation` | `"counted"` | `counted`: `aaa…5x`. `expanded`: `aaaaa`. |
| `count_threshold` | `4` | Presses before counting starts, 2–10000 |

### `[expiration]`

| Key | Default | Values |
|---|---|---|
| `after_ms` | `3000` | Clear after this much idle time. `0` never clears. Max 86400000. |
| `fade_duration_ms` | `250` | Fade-out time. `0` disables fading. Max 60000. |

### `[symbols]`

| Key | Default | Values |
|---|---|---|
| `font_family` | `""` | Font for symbols. Empty uses `appearance.font_family`. |
| `space` | `" "` | What to show for a typed space, e.g. `"␣"` |

`[symbols.keys]` replaces key labels: `Enter`, `Esc`, `Tab`, `Backspace`, `Delete`, `Insert`, `Home`, `End`, `Page Up`, `Page Down`, `Left`, `Right`, `Up`, `Down`, `Space`, `Print Screen`, `Pause`, `Menu`, `F1`–`F35`.

`[symbols.modifiers]` replaces modifier labels: `Ctrl`, `Shift`, `Alt`, `Super`, `AltGr`.

Quote keys with spaces: `"Page Up" = "⇞"`. Symbol glyphs must exist in the chosen font.

### `[theme]`

See [themes](themes.md).

## Keyboard filter

By default every keyboard is captured. To limit it, add this to your Hyprland config. Names come from `hyprctl devices`.

```lua
hyprcast.configure({
    filter = "exclude", -- or "include"
    keyboards = { "my-macro-pad" },
})
```

The filter applies on the next config reload after the plugin is loaded.

## Command line

Flags override the config file. They apply only when starting the overlay.

```sh
hyprcast-overlay --help
```

`--config PATH` uses a different config file.
