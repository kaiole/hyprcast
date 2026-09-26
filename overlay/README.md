# Hyprcast overlay

The separate Qt Quick + LayerShellQt executable displays interpreted keyboard input. Its bundled default theme shows typed characters, special-key labels, and shortcut chords using one editable history; users may select text or keycap rendering. User-loadable QML themes can change the composition while the C++ backend retains ownership of input interpretation, history, and window lifecycle. Held-key feedback is optional. Connection and protocol diagnostics stay in logs.

## Build

Install Qt 6.5+ (Core, Gui, Network, Qml, Quick), LayerShellQt, `libxkbcommon` 0.7+ development files, XKB keymap data for the input tests, and toml++ 3.4+. CMake uses an installed toml++ package when available, otherwise fetches the pinned v3.4.0 source. Build the overlay without the Hyprland plugin:

```sh
cmake --preset debug -DHYPRCAST_BUILD_PLUGIN=OFF -DHYPRCAST_BUILD_OVERLAY=ON
cmake --build --preset debug --target hyprcast-overlay overlay_ipc_tests overlay_input_tests overlay_history_tests overlay_config_tests overlay_theme_tests
ctest --preset debug
```

## Run

The overlay discovers the socket using `XDG_RUNTIME_DIR` and `HYPRLAND_INSTANCE_SIGNATURE`:

```text
$XDG_RUNTIME_DIR/hyprcast/$HYPRLAND_INSTANCE_SIGNATURE/events.sock
```

Use `hyprctl monitors` to find an output name. Defaults are the primary output, bottom-right anchoring, 24-pixel margins, and a 600x88 logical-pixel surface. Existing command-line options remain available:

```sh
./build/debug/overlay/hyprcast-overlay \
  --monitor eDP-1 \
  --anchor bottom-right \
  --margins 24,24,24,24 \
  --background-opacity 0.55 \
  --presentation keycaps --show-held-keys
```

The default TOML file is `${XDG_CONFIG_HOME:-~/.config}/hyprcast/overlay.toml`. An absent default file is optional; its parent directory is created so it can be watched. `--config PATH` selects a custom file and requires that file to exist at startup. CLI settings explicitly supplied override TOML, which overrides built-in defaults. CLI options include `--monitor`, `--anchor`, `--margins`, `--width`, `--height`, `--background-opacity`, `--presentation`, `--show-held-keys` / `--hide-held-keys`, `--backspace-mode`, `--max-retained-utf16-code-units`, `--repeat-enabled true|false`, `--expire-after-ms`, and `--fade-duration-ms`.

The watcher observes both the file and its parent directory, so atomic-save editors are supported. Saves are debounced. A valid candidate is parsed, validated, and checked against current runtime constraints before any settings are accepted. Invalid saves keep the previous working configuration and report the error; a later valid save recovers normally. At startup, a missing default config means defaults, while a missing `--config` file or invalid initial file is fatal. Deleting the optional default file during a run attempts to restore defaults, subject to restart-only settings; deleting an explicitly selected file keeps the last accepted configuration and reports an error.

### Example TOML

Settings are optional; omitted keys keep their defaults. This example changes the look and behavior without requiring QML knowledge:

```toml
[window]
monitor = "eDP-1"
anchor = "bottom-right"
margins = [24, 24, 24, 24] # left, top, right, bottom; logical pixels
width = 700
height = 100

[appearance]
background_color = "#101722"
background_opacity = 0.82
corner_radius = 14
foreground_color = "#f4f6fa"
font_family = "Sans Serif"
font_size = 28
font_weight = "medium" # normal, medium, demibold, bold
keycap_key_background = "#394b66"
keycap_text_background = "#28384f"
keycap_border_color = "#7185a3"

[display]
presentation = "keycaps" # text or keycaps
show_held_keys = true

[history]
backspace = "delete" # delete or symbol
max_retained_utf16_code_units = 4096

[repeat]
enabled = true # rate and delay still come from Hyprland

[expiration]
after_ms = 5000
fade_duration_ms = 250
```

### Supported settings

All sizes and margins are logical pixels. Colors accept Qt color names or color strings such as `#RRGGBB`. Unknown keys, wrong TOML types, unsupported enum values, and out-of-range values are rejected rather than ignored.

- `[window]`: `monitor` (empty means primary output), `anchor` (`top`, `bottom`, `left`, `right`, or a corner), `margins` (four integers in left/top/right/bottom order), `width` and `height` (1–8192), and `click_through` (default `true`). Anchor, margins, and size can be changed live. The target output must exist at startup. Changing `monitor` or `click_through` requires restart; LayerShellQt binds the output when creating the layer surface, so a live reload containing either transition is rejected as a whole.
- `[appearance]`: `background_color`, `background_opacity` (0–1), `corner_radius`, `foreground_color`, `font_family`, `font_size`, `font_weight`; `history_padding_x` and `text_extra_padding_x`; keycap `keycap_font_size`, `keycap_height`, `keycap_padding_x`, `keycap_radius`, `keycap_spacing`, `keycap_inner_spacing`, `keycap_text_background`, `keycap_key_background`, `keycap_border_color`, `keycap_text_color`; held-key `held_font_size`, `held_key_height`, `held_key_padding_x`, `held_key_radius`, `held_key_spacing`, `held_row_padding_x`, `held_row_padding_bottom`, `held_key_background`, and `held_key_text_color`. Font weight is one of `normal`, `medium`, `demibold`, or `bold`.
- `[display]`: `presentation` (`text` or `keycaps`) and `show_held_keys` (boolean). Existing files continue to select the bundled default theme when no `[theme]` table is present; the bundled theme honors `presentation`.
- `[history]`: `backspace` (`delete` or `symbol`) and `max_retained_utf16_code_units` (1–1,048,576). The budget counts the formatted representation in UTF-16 code units, including labels and separators, independently of font, presentation, or viewport. Reducing it trims old history immediately; surviving entries keep their IDs.
- `[repeat]`: `enabled` (boolean). Hyprland still supplies each keyboard's repeat rate and delay. Disabling repeat cancels current repeat timers; enabling it for an already-held key restarts that key's configured delay.
- `[expiration]`: `after_ms` (0–86,400,000; zero disables future expiration) and `fade_duration_ms` (0–60,000; zero removes expired history without fading). Changing the inactivity interval restarts the current history's idle deadline. Changing fade duration during an active fade restarts that visual fade at full opacity. Expiration removes entries from editable history immediately; its separate visual snapshot cannot consume Backspace. New history cancels an old snapshot.

Appearance, layout, presentation, and behavior changes preserve existing history; behavior options apply prospectively, except that reducing the retention budget necessarily trims old content. Plugin Lua configuration continues to control capture filtering and controls.

## QML themes

The default remains the bundled theme (`builtin:default`), so existing TOML files work unchanged. Theme discovery occurs at startup in deterministic order: user data, system data directories in XDG order, then the bundled theme. A higher-precedence installation claims an ID even if its manifest is invalid; selecting it reports that error rather than silently falling through to a different lower-precedence theme. The bundled default is always available as `builtin:default`, even if an installed theme uses the ordinary ID `default`.

Install a theme by copying a directory containing `theme.toml` and its QML/assets into one of these locations:

- `${XDG_DATA_HOME:-~/.local/share}/hyprcast/themes/<id>/`
- `<data-directory>/hyprcast/themes/<id>/` for each directory in `XDG_DATA_DIRS` (normally `/usr/local/share` and `/usr/share`)

The manifest ID must match the directory name. Package IDs use lowercase letters, digits, dots, underscores, and hyphens; `builtin.*` is reserved. `builtin:default` is a special selection alias, not a package ID. Entry points must be relative to the theme directory. Direct filesystem paths are not selectable IDs.

Example manifest:

```toml
[theme]
id = "ledger"
name = "Ledger timeline"
api_version = 1
entry = "Main.qml"

[options.accent]
type = "color"
default = "#72d6b0"
description = "Accent color for the history rail."

[options.item_spacing]
type = "integer"
default = 5
minimum = 0
maximum = 24
description = "Spacing between recent actions in logical pixels."
```

Theme option types are `boolean`, `integer`, `number`, `string`, `enum`, and `color`. Every option needs a typed `default`; numeric options may declare inclusive `minimum`/`maximum` bounds; enums declare a non-empty string `values` array. Unknown manifest keys, unsupported API versions, invalid defaults, and out-of-range or mistyped settings are rejected. Descriptions are optional.

Select a theme and override its declared options in the overlay TOML:

```toml
[theme]
id = "ledger"

[theme.options]
accent = "#50c9aa"
item_spacing = 8
```

The installed example is `overlay/examples/themes/ledger/`. For an uninstalled source checkout, copy it to `${XDG_DATA_HOME:-~/.local/share}/hyprcast/themes/ledger/`, then select `ledger` as above. The example uses a vertically stacked action timeline and a separate held-key row; it is a different composition, not just a palette variant. It declares `accent` and `item_spacing` without requiring an application rebuild.

### Presentation API version 1

A theme's `Main.qml` must create a `QQuickItem` visual root (for example QML `Item` or `Rectangle`). The overlay owns the Wayland layer surface and supplies the root's parent/available size. QML receives one context object named `hyprcast`:

- `version`, `themeId`
- `history`, `expiredHistory`: read-only model views of active editable history and the visual-only expiration snapshot. Supported roles are `entryId`, `kind` (`text`, `key`, `chord`), `text`, `key`, `modifiers`, `label`, `repeated`, and `repeatCount`; `history.displayText` is also supported. Other currently exposed C++ model roles are not part of the theme API.
- `heldKeys`, `heldKeyCount`
- `fading`, `fadeDurationMs`: C++ removes expired history immediately and owns snapshot cleanup; a theme may animate the snapshot but must not use animation completion to control history lifetime.
- `settings`: the read-only keys `width`, `height`, `backgroundColor`, `backgroundOpacity`, `cornerRadius`, `foregroundColor`, `fontFamily`, `fontSize`, `fontWeight`, `historyPaddingX`, `textExtraPaddingX`, `keycapFontSize`, `keycapHeight`, `keycapPaddingX`, `keycapRadius`, `keycapSpacing`, `keycapInnerSpacing`, `keycapTextBackground`, `keycapKeyBackground`, `keycapBorderColor`, `keycapTextColor`, `heldFontSize`, `heldKeyHeight`, `heldKeyPaddingX`, `heldKeyRadius`, `heldKeySpacing`, `heldRowPaddingX`, `heldRowPaddingBottom`, `heldKeyBackground`, `heldKeyTextColor`, `presentation`, and `showHeldKeys`. Their names map to the documented TOML settings (for example `keycapFontSize` maps to `keycap_font_size`).
- `options`: effective theme options after declared defaults and TOML overrides.

`ThemeApi` sends `heldKeysChanged`, `fadingChanged`, `fadeDurationMsChanged`, `settingsChanged`, and `optionsChanged`; history models use the standard `QAbstractItemModel` row/data notifications. The theme API version is intentionally narrower than the underlying C++ implementation; undocumented properties and roles may change.

The bundled theme honors the shared appearance/layout settings and `display.presentation` (`text` or `keycaps`). The `ledger` example honors the shared background, foreground/font, held-key visibility, and the held-key font/color/size/radius/spacing settings it uses; it deliberately ignores `presentation` and always renders its timeline layout. Themes need not implement every shared setting or reproduce the bundled text/keycap modes. Prefer declared options for a theme's own layout behavior, and document which shared settings your theme honors. Relative QML imports and assets resolve from the theme files.

**Trust warning:** themes are executable QML running in the overlay process with the capabilities of the Qt modules they import. They are not sandboxed. The read-only presentation API and package-relative entry-point rule are interface/organization boundaries, not security isolation; install only themes from authors you trust.

At startup, an invalid selected theme is a clear startup error. On live config reload, manifest/API/option checks and synchronous QML component creation are performed before accepting a theme switch; detectable failures reject the whole candidate and leave the previous accepted config and theme active. Staging does execute theme QML and cannot guarantee rollback from arbitrary runtime errors, side effects, or hangs. Theme files are not watched; restart after installing a theme or editing its manifest/QML to ensure discovery and loading use the updated package.

To select an instance when the environment variable is unavailable, pass `--instance-signature NAME`; `--socket PATH` can specify a socket directly. `--quit-after-ms 1500` is available for smoke tests. The client connects asynchronously and retries with exponential backoff capped at 10 seconds. It validates newline-delimited JSON messages and disconnects/retries on malformed or oversized frames (4 MiB maximum).

## Interpretation and current boundaries

The client applies each valid protocol transition before synchronously delivering that message to the input interpreter, preserving wire order. Key interpretation is independent per keyboard. Hyprland sends evdev keycodes; the overlay adds XKB's `+8` offset, compiles the protocol's XKB text-V1 keymap, and interprets text and modifier consumption with libxkbcommon. Plugin snapshots use the same V1 format as incremental keymap events. Repeats are generated locally from each keyboard's repeatability, rate, and delay.

Modifier changes from Hyprland are authoritative: key events update local XKB pressed-key state, then modifier messages reconcile the XKB masks. Pause, keymap replacement, keyboard removal, disconnect, and key release clear affected held/repeat state. Reconnect snapshots do not include held keys, so the overlay does not infer or repeat keys it did not observe pressed.

Interpreted actions are stored in a `QAbstractListModel` with stable entry IDs and roles for kind, text, key, modifiers, keyboard/key identity, event time, and repeat metadata. Model updates use row insertions/removals and `dataChanged` for partial text edits and retention trimming rather than resetting the list on each key. Both bundled presentations consume this model; text output is also derived from it.

Old text is trimmed at grapheme boundaries and special keys/chords at entry boundaries. The text viewport uses measured `Text.ElideLeft`; the keycap viewport tail-follows retained entries and hides a clipped leading cap whole. Neither viewport discards off-screen content, so older retained content reappears as newer content is erased.

Plain Backspace defaults to deletion: it removes one Unicode grapheme from text, or one whole special-key/chord entry. Generated Backspace repeats apply that deletion repeatedly. In `symbol` mode Backspace is recorded as a visible key. Modified shortcuts such as Ctrl+Backspace remain visible chords and do not emulate word deletion. Pause and connection reset preserve history while clearing input/repeat/held-key state. XKB translation is not a reconstruction of application- or IME-committed text.

Held-key feedback reports keys observed as currently pressed, including modifiers. It clears on pause, disconnect, keymap replacement, and keyboard removal; reconnect snapshots do not contain held keys, so unknown held state is not inferred.

## Validation

The debug build with the plugin and overlay enabled passes all 24 configured CTest tests. Theme tests cover user/system/bundled precedence, the protected bundled alias, API-version and manifest errors, option defaults/type/range/enum validation, real QML component creation, live option updates, rejected theme switches, and recovery while preserving history. Offscreen smoke tests loaded the bundled text and keycap presentations and the installed `ledger` example without QML warnings; invalid theme options failed at startup with diagnostics. These checks do not verify live compositor rendering or theme switching under Hyprland. The earlier phase-6 live smoke test was separate and did not exercise user themes.
