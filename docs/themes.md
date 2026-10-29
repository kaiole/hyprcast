# Themes

## Use a theme

```toml
[theme]
id = "keycaps"

[theme.options]
key_background = "#394b66"
spacing = 8
```

| Theme | Options | Suggested size |
|---|---|---|
| `builtin:default` | none | default |
| `keycaps` | [theme.toml](../overlay/examples/themes/keycaps/theme.toml) | default |
| `ledger` | [theme.toml](../overlay/examples/themes/ledger/theme.toml) | default |
| `text-held` | [README](../overlay/examples/themes/text-held/README.md) | `width = 600`, `height = 140` |
| `cascade` | [README](../overlay/examples/themes/cascade/README.md) | `width = 420`, `height = 320` |

Set the size under `[window]`. Each option in `theme.toml` lists its default, range, and description.

## Install a theme

Copy the theme folder to `~/.local/share/hyprcast/themes/<id>/`, then run:

```sh
hyprcast-overlay restart
```

Themes are also searched in `/usr/local/share/hyprcast/themes` and `/usr/share/hyprcast/themes`. If two folders have the same ID, the one in your home directory wins.

Themes are QML code that runs with your user's permissions. Only install themes you trust.

## Make a theme

A theme is a folder with a `theme.toml` and a QML entry file. Copy an example theme and edit it.

After every edit, run `hyprcast-overlay restart`.

### `theme.toml`

```toml
[theme]
id = "my-theme"      # must match the folder name
name = "My theme"
api_version = 1
entry = "Main.qml"   # relative to the theme folder

[options.accent]
type = "color"
default = "#72d6b0"
description = "Accent color."

[options.spacing]
type = "integer"
default = 5
minimum = 0
maximum = 24
```

- `id`: lowercase letters, digits, `.`, `_`, `-`. `builtin.*` is reserved.
- Option names: lowercase letters, digits, `_`, starting with a letter.
- Option types: `boolean`, `integer`, `number`, `string`, `enum`, `color`.
- Every option needs `default`. `integer` and `number` can set `minimum` and `maximum`. `enum` needs `values = ["a", "b"]`.
- Unknown keys and invalid values are errors.

### QML

`Main.qml` must have a visual root such as `Item` or `Rectangle`. It gets the overlay's full size. The theme draws everything, including the panel background.

The theme reads everything from one object, `hyprcast`:

| Property | Contents |
|---|---|
| `displayHistory` | Model of current input, with symbols and repeat counts applied. Use this one. |
| `expiredDisplayHistory` | Same, for history that is fading out |
| `displayHistory.displayText` | Whole history as plain text |
| `displayHistory.displayRichText` | Whole history as rich text (repeat counts as subscript) |
| `history`, `expiredHistory` | Raw models, without symbols or repeat counts |
| `historyCount`, `expiredHistoryCount` | Row counts |
| `fading`, `fadeDurationMs` | Whether expired history is fading, and for how long |
| `heldKeyItems` | Held keys: `{ kind, identity, label }` |
| `heldKeys`, `heldKeyCount` | Held key labels and count |
| `settings` | Config values in camelCase, e.g. `settings.fontSize`, `settings.panelBorderWidth` |
| `options` | This theme's options, e.g. `options.accent` |
| `version`, `themeId` | API version and theme ID |

`displayHistory` row roles:

| Role | Contents |
|---|---|
| `entryId` | Stable ID, for use as a delegate key |
| `kind` | `text`, `key`, or `chord` |
| `text` | Typed text (for `text`) |
| `key`, `modifiers` | Key and modifiers (for `key` and `chord`) |
| `label` | Raw label |
| `displayLabel`, `displayKey`, `displayModifiers` | Labels with symbols applied |
| `counted`, `repeatCount` | Whether the row is a repeat group, and its count |

The overlay decides when history expires. Animate fading with `fading` and `fadeDurationMs`, but don't use the animation to remove history.

Properties and roles not listed here may change.
