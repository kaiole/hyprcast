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

Use `hyprctl monitors` to find an output name. Defaults are the primary output, bottom-right anchoring, 24-pixel margins, and a 600x88 logical-pixel surface. That surface remains at the configured maximum size; optional content sizing changes only the visible panel inside it. Existing command-line options remain available:

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
width = 700 # maximum surface/panel width
height = 120 # maximum surface/panel height
dynamic_size = true
min_width = 220
min_height = 64

[appearance]
background_color = "#101722"
background_opacity = 0.82
corner_radius = 14
panel_border_width = 2
panel_border_color = "#806f86a8" # color alpha is independent of fill opacity
foreground_color = "#f4f6fa"
font_family = "Sans Serif"
font_size = 28
font_weight = "medium" # normal, medium, demibold, bold
keycap_key_background = "#394b66"
keycap_text_background = "#28384f"
keycap_border_color = "#7185a3"
keycap_border_width = 1
held_key_border_color = "#7185a3"
held_key_border_width = 1

[display]
presentation = "keycaps" # text or keycaps
show_held_keys = true
panel_visibility = "with-content" # always, with-content, or never

[history]
backspace = "delete" # delete or symbol
max_retained_utf16_code_units = 4096

[repeat]
enabled = true # rate and delay still come from Hyprland
presentation = "counted" # expanded or counted
count_threshold = 4 # total occurrences, including the initial press

[symbols]
font_family = "Symbols Nerd Font" # empty inherits the regular font

[symbols.keys]
Backspace = "⌫"

[symbols.modifiers]
Ctrl = "⌃"

[expiration]
after_ms = 5000
fade_duration_ms = 250
```

### Supported settings

All sizes and margins are logical pixels. Colors accept Qt color names or color strings such as `#RRGGBB` and alpha-bearing `#AARRGGBB`. Unknown keys, wrong TOML types, unsupported enum values, and out-of-range values are rejected rather than ignored.

- `[window]`: `monitor` (empty means primary output), `anchor` (`top`, `bottom`, `left`, `right`, or a corner), `margins` (four integers in left/top/right/bottom order), maximum `width` and `height` (1–8192), `dynamic_size` (default `false`), `min_width` and `min_height` (1–8192; defaults 240 and 64), and `click_through` (default `true`). With dynamic sizing enabled, the visible panel grows and shrinks between the minimum and maximum dimensions; the transparent Wayland surface remains at the maximum dimensions. Edge anchoring keeps the panel against the chosen edge as it changes size. Minimums must not exceed maximums when dynamic sizing is enabled. Anchor, margins, and size can be changed live. The target output must exist at startup. Changing `monitor` or `click_through` requires restart; LayerShellQt binds the output when creating the layer surface, so a live reload containing either transition is rejected as a whole.
- `[appearance]`: `background_color`, `background_opacity` (0–1; affects fill only), `corner_radius`, `panel_border_width`/`panel_border_color`, `foreground_color`, `font_family`, `font_size`, `font_weight`; `history_padding_x` and `text_extra_padding_x`; keycap `keycap_font_size`, `keycap_height`, `keycap_padding_x`, `keycap_radius`, `keycap_spacing`, `keycap_inner_spacing`, `keycap_text_background`, `keycap_key_background`, `keycap_border_color`, `keycap_border_width`, `keycap_text_color`; held-key `held_font_size`, `held_key_height`, `held_key_padding_x`, `held_key_radius`, `held_key_spacing`, `held_row_padding_x`, `held_row_padding_bottom`, `held_key_background`, `held_key_text_color`, `held_key_border_width`, and `held_key_border_color`. Border widths are 0–64 logical pixels; zero disables that border. Panel and held-key borders default to zero width; keycap borders default to one pixel to preserve current rendering. Colors accept Qt color names and strings including `#RRGGBB` and alpha-bearing `#AARRGGBB`; border alpha is independent of panel fill opacity. Font weight is one of `normal`, `medium`, `demibold`, or `bold`.
- `[display]`: `presentation` (`text` or `keycaps`), `show_held_keys` (boolean), and `panel_visibility` (`always`, `with-content`, or `never`). `with-content` shows the panel while editable history, an expiration snapshot, or enabled held-key feedback is visible. `never` hides only the outer panel fill/border; input visuals remain. Defaults preserve the current appearance. Existing files continue to select the bundled default theme when no `[theme]` table is present.
- `[history]`: `backspace` (`delete` or `symbol`) and `max_retained_utf16_code_units` (1–1,048,576). The budget counts the expanded canonical formatted history in UTF-16 code units, including raw labels and separators, before symbol substitution or repeat counting; it is independent of font, presentation, and viewport. Compressing repeats or choosing shorter symbols never lets history exceed the budget. Reducing it trims old history immediately; surviving entries keep their IDs.
- `[repeat]`: `enabled` (boolean) controls local auto-repeat generation; Hyprland still supplies each keyboard's repeat rate and delay. Disabling repeat cancels current repeat timers; enabling it for an already-held key restarts that key's configured delay. `presentation` is `expanded` (the default) or `counted`, and `count_threshold` is 2–10,000 (default 4) total retained occurrences including the initial press. Counted mode groups adjacent equivalent inputs—including separate taps—whether or not auto-repeat is enabled; it changes presentation only, not repeat timing.
- `[symbols]`: `font_family` (empty inherits the regular font), plus `[symbols.keys]` and `[symbols.modifiers]` tables mapping canonical key/modifier labels to display strings. Keys such as `Backspace` and modifiers such as `Ctrl` are mapped by exact canonical identity. Mappings never replace identity used for XKB interpretation, chords, or Backspace. The selected font must contain the glyphs; Qt/system font fallback applies, so arbitrary Nerd Font glyphs are not guaranteed on systems without that font.
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
- `history`, `expiredHistory`: read-only model views of semantic active history and the visual-only expiration snapshot. Their API-v1 roles and `history.displayText` retain the original unprojected labels/actions; other currently exposed C++ model roles are not part of the theme API.
- `displayHistory`, `expiredDisplayHistory`: compatible presentation projections with `displayText` and `displayRichText`, plus roles `entryId`, `kind`, `text`, `key`, `modifiers`, `label`, `displayLabel`, `displayKey`, `displayModifiers`, `counted`, and `repeatCount`. These apply configured symbols and counted-repeat grouping without changing the semantic models. `entryId` is anchored to the first retained semantic action represented by the projected row.
- `historyCount`, `expiredHistoryCount`: current semantic row counts, suitable for panel-visibility decisions.
- `heldKeys`, `heldKeyCount`, `heldKeyItems`: the original string list/count plus structured items (`kind`, canonical `identity`, resolved `label`) for themes that want shared symbol substitution.
- `fading`, `fadeDurationMs`: C++ removes expired history immediately and owns snapshot cleanup; a theme may animate the snapshot but must not use animation completion to control history lifetime.
- `settings`: read-only presentation values including `width`, `height`, `anchor`, `dynamicSize`, `minWidth`, `minHeight`, `panelBorderWidth`, `panelBorderColor`, `panelVisibility`, all existing appearance properties, `keycapBorderWidth`, `heldKeyBorderWidth`, `heldKeyBorderColor`, `symbolFontFamily`, `keySymbols`, `modifierSymbols`, `presentation`, `showHeldKeys`, `repeatPresentation`, and `repeatCountThreshold`. Names map to TOML settings (for example `keycapFontSize` maps to `keycap_font_size`).
- `options`: effective theme options after declared defaults and TOML overrides.

`ThemeApi` sends `heldKeysChanged`, history-count notifications, `fadingChanged`, `fadeDurationMsChanged`, `settingsChanged`, and `optionsChanged`; both semantic and projected models use standard `QAbstractItemModel` notifications. Additions are backward-compatible API-v1 properties/roles. Existing custom themes that keep using only raw `history`/`heldKeys` continue to work but must opt into `displayHistory`, `displayRichText`, and `heldKeyItems` to show shared substitutions and counted-repeat presentation. The theme API version is intentionally narrower than the underlying C++ implementation; undocumented properties and roles may change.

The bundled theme honors the shared appearance/layout settings, dynamic visible-panel width and height bounds, panel visibility, symbol substitutions, counted repeats, and `display.presentation` (`text` or `keycaps`). The `ledger` example honors the panel and held-key borders, panel visibility, symbol substitutions, counted repeats, and dynamic height for its vertical timeline; its width remains fixed at the configured maximum, and it deliberately ignores `presentation`. Themes need not implement every shared setting or reproduce the bundled text/keycap modes. Prefer declared options for a theme's own layout behavior, and document which shared settings your theme honors. Relative QML imports and assets resolve from the theme files.

**Trust warning:** themes are executable QML running in the overlay process with the capabilities of the Qt modules they import. They are not sandboxed. The read-only presentation API and package-relative entry-point rule are interface/organization boundaries, not security isolation; install only themes from authors you trust.

At startup, an invalid selected theme is a clear startup error. On live config reload, manifest/API/option checks and synchronous QML component creation are performed before accepting a theme switch; detectable failures reject the whole candidate and leave the previous accepted config and theme active. Staging does execute theme QML and cannot guarantee rollback from arbitrary runtime errors, side effects, or hangs. Theme files are not watched; restart after installing a theme or editing its manifest/QML to ensure discovery and loading use the updated package.

To select an instance when the environment variable is unavailable, pass `--instance-signature NAME`; `--socket PATH` can specify a socket directly. `--quit-after-ms 1500` is available for smoke tests. The client connects asynchronously and retries with exponential backoff capped at 10 seconds. It validates newline-delimited JSON messages and disconnects/retries on malformed or oversized frames (4 MiB maximum).

## Interpretation and current boundaries

The client applies each valid protocol transition before synchronously delivering that message to the input interpreter, preserving wire order. Key interpretation is independent per keyboard. Hyprland sends evdev keycodes; the overlay adds XKB's `+8` offset, compiles the protocol's XKB text-V1 keymap, and interprets text and modifier consumption with libxkbcommon. Plugin snapshots use the same V1 format as incremental keymap events. Repeats are generated locally from each keyboard's repeatability, rate, and delay.

Modifier changes from Hyprland are authoritative: key events update local XKB pressed-key state, then modifier messages reconcile the XKB masks. Pause, keymap replacement, keyboard removal, disconnect, and key release clear affected held/repeat state. Reconnect snapshots do not include held keys, so the overlay does not infer or repeat keys it did not observe pressed.

Interpreted actions are stored in a semantic `QAbstractListModel` with stable entry IDs and roles for kind, text, key, modifiers, keyboard/key identity, event time, and repeat metadata. Model updates use row insertions/removals and `dataChanged` for partial text edits and retention trimming rather than resetting the list on each key. A separate C++ presentation projection supplies shared symbol-resolved labels and optional counted groups; both bundled presentations consume that projection while custom API-v1 themes may continue using the original semantic model.

Old text is trimmed at grapheme boundaries and special keys/chords at entry boundaries. The text viewport uses measured `Text.ElideLeft`; the keycap viewport tail-follows retained entries and hides a clipped leading cap whole. Neither viewport discards off-screen content, so older retained content reappears as newer content is erased.

Plain Backspace defaults to deletion: it removes one Unicode grapheme from text, or one whole special-key/chord entry. Generated Backspace repeats apply that deletion repeatedly. In `symbol` mode Backspace is recorded as a visible key. Modified shortcuts such as Ctrl+Backspace remain visible chords and do not emulate word deletion. In counted mode, adjacent equivalent inputs collapse after the configured threshold; separate taps and generated repeats of the same input count together, and the initial press/tap counts as occurrence one. Key identity, interpreted text/chord/modifiers, and keyboard identity determine equivalence; display-symbol substitutions and event timing do not. There is no tap-timeout. A different intervening input or interpretation starts a new group; deleting that intervening input does not retroactively join the groups. Once collapsed, a group stays collapsed while Backspace decrements its underlying retained occurrences, returning to a plain action at one; another equivalent input can extend the same group. Deletion still follows the existing grapheme/atomic-key semantics. Multi-grapheme actions and text whose grapheme boundaries cross action edges remain expanded. Storage, expiration, and retention operate on individual semantic actions, never on the shortened counter. Pause and connection reset preserve history while clearing input/repeat/held-key state; adjacent equivalent input after reconnect can continue its retained group. XKB translation is not a reconstruction of application- or IME-committed text.

Held-key feedback reports keys observed as currently pressed, including modifiers. It clears on pause, disconnect, keymap replacement, and keyboard removal; reconnect snapshots do not contain held keys, so unknown held state is not inferred.

## Validation

The debug suite covers parser validation, semantic deletion/retention, counted-repeat projection and stable model notifications, symbol identity/substitution, and theme loading; all 24 configured CTest tests pass. Offscreen QML tests assert default-panel width growth/shrink/capping, ledger timeline height growth/shrink/capping, with-content visibility, and transparent-fill/border independence. Theme tests also cover user/system/bundled precedence, API/manifest/options validation, live option updates, rejected theme switches, and history preservation. An isolated acceptance pack at `/tmp/hyprcast-customization-acceptance/` provides a safe no-socket QML smoke script and a separately confirmed live-input launcher. These checks do not verify physical input/repeat timing, installed font glyphs, or compositor rendering under Hyprland.
