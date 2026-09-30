# Overlay configuration and themes

The separate Qt Quick + LayerShellQt executable displays interpreted keyboard input. Its text-only bundled default shows typed characters, special-key labels, and shortcut chords using one editable history without requiring configuration. Keycaps is an optional example theme. User-loadable QML themes can change the composition while the C++ backend retains ownership of input interpretation, history, and window lifecycle. Held-key feedback is optional. Connection and protocol diagnostics stay in logs.

For build and installation instructions, see the [project README](../README.md).

## Run

### One-key operation

Load the Hyprcast plugin through your normal Hyprland configuration (see the [README](../README.md)), then bind a key to the installed executable:

```lua
hl.bind("SUPER + O", hl.dsp.exec_cmd("hyprcast-overlay toggle"))
```

For a source checkout, substitute the absolute path to `build/debug/overlay/hyprcast-overlay`. When updating the capture protocol, rebuild **and reload the plugin** as well as rebuilding the overlay.

- First press starts the overlay and requests capture enabled after connecting to the plugin.
- Later presses contact the resident overlay: disable capture, clear history and fade snapshots, and hide; or enable capture and show it again.
- The overlay remains running while disabled. Only one overlay may own each event socket, including during simultaneous launches. A toggle while a previous change is awaiting confirmation returns a busy error rather than silently losing the request.
- The plugin starts paused and pauses when its event client disconnects. Reconnection/plugin reload does not automatically resume capture.
- Starting `hyprcast-overlay` without `toggle` leaves it resident, following the plugin's capture state. This is suitable for optional session autostart; the same keybind works with either startup style.
- The first launch stays running in the foreground (normal for a GUI launched by a Hyprland exec keybind). Later invocations exit after the plugin confirms the state change. Errors go to stderr; no desktop notification or tray UI is provided yet.
- First-launch activation fails after three seconds without confirmation. A resident overlay reports an unavailable plugin immediately; it still retries its event connection in the background. Failed/timed-out requests are not replayed after reconnect.

The per-session control socket and ownership lock are next to the event socket (`events.sock.overlay` and `events.sock.overlay.owner-lock`). The advisory lock's inode stays in place; ownership is held across executable replacement. Stale sockets/locks are recovered after crashes; non-socket files are never removed. `--socket` or `--instance-signature` must select the same session on subsequent commands. Appearance/configuration options are used only when starting a new instance; edit the watched TOML file to configure a resident overlay.

The existing plugin `hyprcast.toggle` function still changes capture, but does **not** launch the overlay; use the executable keybind for the combined UX.

### Restarting the overlay

```sh
hyprcast-overlay restart
hyprcast-overlay restart --socket /absolute/path/to/events.sock
hyprcast-overlay restart --instance-signature SESSION
```

Use restart after installing a theme or editing its manifest, QML, helpers, or assets—even with the same theme ID/path—or after changing `window.monitor` or `window.click_through`. Ordinary TOML changes (including switching already discovered themes/options) still reload live. Only the selected session's overlay restarts; Hyprland and the plugin remain loaded. There is no PID search or broadcast.

Restart replaces the executable image and Qt runtime, rereads the resident's absolute config path, and rediscovers themes in normal precedence order. The resident's **original explicit CLI overrides** remain authoritative over current TOML; the caller's working directory/configuration does not replace them. Only session selectors plus help/version are valid for `restart`. `--quit-after-ms` is a first-launch smoke-test control and is not replayed in the replacement.

Confirmed active capture is explicitly enabled after the fresh plugin snapshot and shown only after acknowledgement; confirmed paused capture stays hidden. Plugin disconnect pauses capture during the transition. With no connected/confirmed plugin state, restart completes locally in a hidden resident without pending enable intent. History, fade snapshots, held keys, and repeat state are discarded. Input during restart may be missed; keys held across reconnect are not inferred.

Success is silent and means initialization and any required capture restoration are confirmed. Exit codes are 0 for completion, 1 for operational failure, and 2 for CLI misuse. No resident returns “No overlay is running for this session; use toggle to start it.” Startup, pending capture changes, and concurrent restart/capture commands fail boundedly; restart never kills an unresponsive resident or steals ownership. The operation has a 10-second budget after acceptance, with a slightly longer caller wait. A timeout/disconnection can mean **outcome unknown**; do not automatically retry.

Preflight validates current config, fresh manifests/options, output availability, executable availability, and selected QML in a separate engine. Detectable failures retain the existing accepted runtime. There is **no guaranteed rollback after exec**: disk changes or replacement startup failures can leave no working overlay. Failed capture restoration leaves the replacement hidden, disconnects if necessary to pause uncertain capture, and never automatically resumes on reconnect. QML is trusted executable code: preflight can have side effects, and GUI-thread timers cannot preempt a hanging theme; the external caller still has a bounded wait.

A resident launched with a pre-feature binary does not understand `restart`. The new client reports its unsupported-command error rather than killing it; manually close/relaunch that overlay once to activate restart support. Then use your configured toggle binding as usual. Do not unload the plugin just to update themes.

### Socket discovery and configuration

The overlay discovers the socket using `XDG_RUNTIME_DIR` and `HYPRLAND_INSTANCE_SIGNATURE`:

```text
$XDG_RUNTIME_DIR/hyprcast/$HYPRLAND_INSTANCE_SIGNATURE/events.sock
```

Use `hyprctl monitors` to find an output name. Defaults are the primary output, bottom-right anchoring, 24-pixel margins, and a 600x88 logical-pixel surface. That surface remains at the configured maximum size; the visible panel grows with content from 240x64 up to 600x88. It hides when empty, and history expires after three seconds of inactivity with a 250 ms fade shared by the input and panel decoration. Adjacent equivalent inputs show up to three presses followed by a small, lowered total-count suffix (for example `aaa…4x`) from the fourth occurrence. For example:

```sh
./build/debug/overlay/hyprcast-overlay \
  --monitor eDP-1 \
  --anchor bottom-right \
  --margins 24,24,24,24 \
  --background-opacity 0.55
```

The default TOML file is `${XDG_CONFIG_HOME:-~/.config}/hyprcast/overlay.toml`. An absent default file is optional; its parent directory is created so it can be watched. `--config PATH` selects a custom file and requires that file to exist at startup. CLI settings explicitly supplied override TOML, which overrides built-in defaults. CLI options include `--monitor`, `--anchor`, `--margins`, `--width`, `--height`, `--background-opacity`, `--backspace-mode`, `--max-retained-utf16-code-units`, `--repeat-enabled true|false`, `--expire-after-ms`, and `--fade-duration-ms`.

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

[display]
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
space = "␣" # display-only label for input spaces; default is " "

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

- `[window]`: `monitor` (empty means primary output), `anchor` (`top`, `bottom`, `left`, `right`, or a corner), `margins` (four integers in left/top/right/bottom order), maximum `width` and `height` (1–8192), `dynamic_size` (default `true`), `min_width` and `min_height` (1–8192; defaults 240 and 64), and `click_through` (default `true`). With dynamic sizing enabled, the visible panel grows and shrinks between the minimum and maximum dimensions; the transparent Wayland surface remains at the maximum dimensions. Edge anchoring keeps the panel against the chosen edge as it changes size. Minimums must not exceed maximums when dynamic sizing is enabled. Anchor, margins, and size can be changed live. The target output must exist at startup. Changing `monitor` or `click_through` requires restart; LayerShellQt binds the output when creating the layer surface, so a live reload containing either transition is rejected as a whole.
- `[appearance]`: `background_color`, `background_opacity` (0–1; affects fill only), `corner_radius`, `panel_border_width`/`panel_border_color`, `foreground_color`, `font_family`, `font_size`, `font_weight`; `history_padding_x` and `text_extra_padding_x`. Border widths are 0–64 logical pixels; zero disables that border. The panel border defaults to one pixel. Colors accept Qt color names and strings including `#RRGGBB` and alpha-bearing `#AARRGGBB`; border alpha is independent of panel fill opacity. Font weight is one of `normal`, `medium`, `demibold`, or `bold`.
- `[display]`: `panel_visibility` (`always`, `with-content`, or `never`). In the bundled theme, `with-content` shows the panel while editable history or an expiration snapshot is visible; external themes may include their own content (such as held keys). `never` hides only the outer panel fill/border; input visuals remain. The default is `with-content`; the panel decoration is hidden when there is no content. Without `[theme]`, the bundled text theme is selected.
- `[history]`: `backspace` (`delete` or `symbol`) and `max_retained_utf16_code_units` (1–1,048,576). The budget counts the expanded canonical formatted history in UTF-16 code units, including raw labels and separators, before symbol substitution or repeat counting; it is independent of font, presentation, and viewport. Compressing repeats or choosing shorter symbols never lets history exceed the budget. Reducing it trims old history immediately; surviving entries keep their IDs.
- `[repeat]`: `enabled` (boolean) controls local auto-repeat generation; Hyprland still supplies each keyboard's repeat rate and delay. Disabling repeat cancels current repeat timers; enabling it for an already-held key restarts that key's configured delay. `presentation` is `expanded` or `counted` (the default), and `count_threshold` is 2–10,000 (default 4) total retained occurrences including the initial press. Counted mode groups adjacent equivalent inputs—including separate taps—whether or not auto-repeat is enabled; it changes presentation only, not repeat timing. From the threshold onward, the default text theme shows up to `count_threshold - 1` copies and a lowered `…Nx` suffix, where N counts total retained occurrences. Plain `displayText` includes the suffix without font styling; `displayRichText` styles it as subscript. The optional keycaps theme draws repeated caps and a lowered badge.
- `[symbols]`: `font_family` (empty inherits the regular font), `space` (non-empty text, default `" "`) for display-only substitution of literal U+0020 spaces in interpreted text, plus `[symbols.keys]` and `[symbols.modifiers]` tables mapping canonical key/modifier labels to display strings. Separators inserted between key labels remain ordinary spaces; other whitespace is unchanged. Themes using `displayHistory.displayLabel` or `displayRichText` see the substitution, while raw `text` and semantic `history` remain unchanged. Keys such as `Backspace` and modifiers such as `Ctrl` are mapped by exact canonical identity. Mappings never replace identity used for XKB interpretation, chords, or Backspace. The selected font must contain the glyphs; Qt/system font fallback applies, so arbitrary Nerd Font glyphs are not guaranteed on systems without that font.
- `[expiration]`: `after_ms` (0–86,400,000; zero disables future expiration) and `fade_duration_ms` (0–60,000; zero removes expired history without fading). Changing the inactivity interval restarts the current history's idle deadline. Changing fade duration during an active fade restarts that visual fade at full opacity. Expiration removes entries from editable history immediately; its separate visual snapshot cannot consume Backspace. New history cancels an old snapshot.

In the default text theme, special keys and chords appear as plain labels separated by spaces (for example `a Ctrl+C Enter b`); brackets are not added automatically. To display a particular key with brackets, set a label such as `Enter = "[Enter]"` under `[symbols.keys]`. Symbol mappings also affect the keycaps example. The raw semantic history and retention budget retain their original canonical labels and accounting.

Appearance, layout, presentation, and behavior changes preserve existing history; behavior options apply prospectively, except that reducing the retention budget necessarily trims old content. Plugin Lua configuration continues to control capture filtering and controls.

## QML themes

The text-only bundled theme (`builtin:default`) needs no config or theme installation. Theme discovery occurs at startup in deterministic order: user data, system data directories in XDG order, then the bundled theme. A higher-precedence installation claims an ID even if its manifest is invalid; selecting it reports that error rather than silently falling through to a different lower-precedence theme. The bundled default is always available as `builtin:default`, even if an installed theme uses the ordinary ID `default`.

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

The example themes are `overlay/examples/themes/ledger/`, `overlay/examples/themes/keycaps/`, `overlay/examples/themes/text-held/`, and `overlay/examples/themes/cascade/`. For an uninstalled source checkout, copy the chosen package directory to `${XDG_DATA_HOME:-~/.local/share}/hyprcast/themes/<id>/`, then select its ID. `ledger` uses a vertically stacked action timeline with `accent`, `item_spacing`, `rail_width` (0–16 logical pixels), `inner_padding` (0–64 logical pixels), and `text_scale` (0.5–2.0, default 1.0). `keycaps` uses a stable horizontal row of raised labeled caps with cap-specific options. Its lower lip stays inside the declared `height`; `edge_depth` (0–24, default 3 logical pixels) is clamped to half that height, and `edge_color` defaults to `#101010`. Set `edge_depth = 0` for flat caps. The caps show input history, not held/pressed state. `text-held` is a self-contained horizontal ribbon of raised history caps above a fixed live modifier dock; its cap/ribbon/dock styling is declared through theme-local options. All work without an application rebuild.

`cascade` is a bottom-anchored receding stack of raised keycaps with grouped chords, bounded virtualized history, and full/reduced motion. Start with a 420 × 320 surface; shorter surfaces reduce visible depth. Dynamic sizing reserves a stable bounded stage rather than resizing on every key. Cap appearance is theme-local; shared panel appearance, anchor, sizing, font family/weight, symbols, repeats and expiration are honored. Its [package README](../overlay/examples/themes/cascade/README.md) includes installation, a complete demo config, sizing exceptions, and validation notes.

`text-held` always reserves two stable bands: newest history at the right above fixed Ctrl / Shift / Alt / Super slots. `show_altgr` adds a separate AltGr slot; `dock_alignment` selects left/center/right alignment. Groups remain full-size and opaque except for a narrow fade at the far left edge, chords retain separate caps, and `motion = "reduced"` disables ribbon transitions and pressed-face travel. Start with a 600 × 140 surface. Dynamic sizing reserves a content-independent stage, and short/narrow surfaces reduce geometry or elide labels rather than switching to side-by-side. Its [package README](../overlay/examples/themes/text-held/README.md) documents all option defaults/bounds, shared-setting exceptions, and migration: obsolete `layout`, `held_side`, `compact_held_fraction`, and `held_*` styling names are rejected with documented replacements. These examples are starting points to fork, not exhaustive layout editors.

For example, after installing `text-held`:

```toml
[window]
width = 600
height = 140

[theme]
id = "text-held"
[theme.options]
show_held_keys = true
dock_alignment = "center"
edge_depth = 3
```

The modifier dock follows canonical observed presses/releases independently of history and expiration; a standalone modifier release may still create a history entry. Duplicate display glyphs do not merge slot identities, and matching observations from multiple keyboards are aggregated. Idle slots mean not currently observed held, not proof of physical release; placeholders alone do not keep `with-content` decoration visible. `heldKeyItems` supplies canonical identity, kind, and symbol-resolved labels; text-kind labels do not use `[symbols].space` substitution.

### Presentation API version 1

A theme's `Main.qml` must create a `QQuickItem` visual root (for example QML `Item` or `Rectangle`). The overlay owns the Wayland layer surface and supplies the root's parent/available size. QML receives one context object named `hyprcast`:

- `version`, `themeId`
- `history`, `expiredHistory`: read-only model views of semantic active history and the visual-only expiration snapshot. Their API-v1 roles and `history.displayText` retain the original unprojected labels/actions; other currently exposed C++ model roles are not part of the theme API.
- `displayHistory`, `expiredDisplayHistory`: compatible presentation projections with `displayText` and `displayRichText`, plus roles `entryId`, `kind`, `text`, `key`, `modifiers`, `label`, `displayLabel`, `displayKey`, `displayModifiers`, `counted`, and `repeatCount`. These apply configured symbols and counted-repeat grouping without changing the semantic models. `entryId` is anchored to the first retained semantic action represented by the projected row.
- `historyCount`, `expiredHistoryCount`: current semantic row counts, suitable for panel-visibility decisions.
- `heldKeys`, `heldKeyCount`, `heldKeyItems`: the original string list/count plus structured items (`kind`, canonical `identity`, resolved `label`) for themes that want shared symbol substitution.
- `fading`, `fadeDurationMs`: C++ removes expired history immediately and owns snapshot cleanup; a theme may animate the snapshot but must not use animation completion to control history lifetime.
- `settings`: read-only presentation values including `width`, `height`, `anchor`, `dynamicSize`, `minWidth`, `minHeight`, `panelBorderWidth`, `panelBorderColor`, `panelVisibility`, shared appearance properties, `symbolFontFamily`, `keySymbols`, `modifierSymbols`, `repeatPresentation`, and `repeatCountThreshold`. Names map to TOML settings.
- `options`: effective theme options after declared defaults and TOML overrides.

`ThemeApi` sends `heldKeysChanged`, history-count notifications, `fadingChanged`, `fadeDurationMsChanged`, `settingsChanged`, and `optionsChanged`; both semantic and projected models use standard `QAbstractItemModel` notifications. Additions are backward-compatible API-v1 properties/roles. Existing custom themes that keep using only raw `history`/`heldKeys` continue to work but must opt into `displayHistory`, `displayRichText`, and `heldKeyItems` to show shared substitutions and counted-repeat presentation. The theme API version is intentionally narrower than the underlying C++ implementation; undocumented properties and roles may change.

The app owns interpretation, editable history, repeat/expiration lifetime, window lifecycle, and the maximum Wayland surface. Themes own their visual layout, panel/background/borders, typography, visible-panel sizing, and animations; the API does not impose a panel wrapper. The bundled text theme honors shared appearance, sizing, panel visibility, symbol and repeat settings without held-key visuals. `ledger` honors panel borders, panel visibility, font/colors, symbols, counted repeats, and dynamic height (not dynamic width). `keycaps` honors the shared panel, font-family/weight, foreground, symbol, repeat, and visible-panel sizing settings. `text-held` honors shared panel appearance, stable bounded sizing, font family/weight and symbols; cap typography/colors/geometry replace shared text-specific appearance settings. Keycaps' cap-specific colors, font size, height, padding, radius, spacing, and border are declared options, not global settings. For example:

```toml
[theme]
id = "keycaps"
[theme.options]
height = 44
edge_depth = 3
spacing = 8
border_width = 0
key_background = "#394b66"
```

Themes may use only the shared settings relevant to their composition. Package-local QML helpers and assets resolve relative to the theme files; examples do not depend on private bundled QML.

**Trust warning:** themes are executable QML running in the overlay process with the capabilities of the Qt modules they import. They are not sandboxed. The read-only presentation API and package-relative entry-point rule are interface/organization boundaries, not security isolation; install only themes from authors you trust.

At startup, an invalid selected theme is a clear startup error. On live config reload, manifest/API/option checks and synchronous QML component creation are performed before accepting a theme switch; detectable failures reject the whole candidate and leave the previous accepted config and theme active. Staging does execute theme QML and cannot guarantee rollback from arbitrary runtime errors, side effects, or hangs. Theme files are not watched; run `hyprcast-overlay restart` after installing a theme or editing its manifest/QML/helpers/assets to ensure discovery and loading use the updated package.

To select an instance when the environment variable is unavailable, pass `--instance-signature NAME`; `--socket PATH` can specify a socket directly. `--quit-after-ms 1500` is available for smoke tests. The client connects asynchronously and retries with exponential backoff capped at 10 seconds. It validates newline-delimited JSON messages and disconnects/retries on malformed or oversized frames (4 MiB maximum).

## Interpretation and current boundaries

The client applies each valid protocol transition before synchronously delivering that message to the input interpreter, preserving wire order. Key interpretation is independent per keyboard. Hyprland sends evdev keycodes; the overlay adds XKB's `+8` offset, compiles the protocol's XKB text-V1 keymap, and interprets text and modifier consumption with libxkbcommon. Plugin snapshots use the same V1 format as incremental keymap events. Repeats are generated locally from each keyboard's repeatability, rate, and delay.

Modifier changes from Hyprland are authoritative: key events update local XKB pressed-key state, then modifier messages reconcile the XKB masks. Pause, keymap replacement, keyboard removal, disconnect, and key release clear affected held/repeat state. Reconnect snapshots do not include held keys, so the overlay does not infer or repeat keys it did not observe pressed.

Interpreted actions are stored in a semantic `QAbstractListModel` with stable entry IDs and roles for kind, text, key, modifiers, keyboard/key identity, event time, and repeat metadata. Model updates use row insertions/removals and `dataChanged` for partial text edits and retention trimming rather than resetting the list on each key. A separate C++ presentation projection supplies shared symbol-resolved labels and optional counted groups; the bundled text theme and example themes consume that projection while custom API-v1 themes may continue using the original semantic model.

Old text is trimmed at grapheme boundaries and special keys/chords at entry boundaries. The text viewport uses measured `Text.ElideLeft`; the optional keycaps theme tail-follows retained entries and hides a clipped leading cap whole. Neither viewport discards off-screen content, so older retained content reappears as newer content is erased.

Plain Backspace defaults to deletion: it removes one Unicode grapheme from text, or one whole special-key/chord entry. Generated Backspace repeats apply that deletion repeatedly. In `symbol` mode Backspace is recorded as a visible key. Modified shortcuts such as Ctrl+Backspace remain visible chords and do not emulate word deletion. In counted mode, adjacent equivalent inputs collapse after the configured threshold; separate taps and generated repeats of the same input count together, and the initial press/tap counts as occurrence one. Key identity, interpreted text/chord/modifiers, and keyboard identity determine equivalence; display-symbol substitutions and event timing do not. There is no tap-timeout. A different intervening input or interpretation starts a new group; deleting that intervening input does not retroactively join the groups. Once collapsed, a group stays collapsed while Backspace decrements its underlying retained occurrences, returning to a plain action at one; another equivalent input can extend the same group. Deletion still follows the existing grapheme/atomic-key semantics. Multi-grapheme actions and text whose grapheme boundaries cross action edges remain expanded. Storage, expiration, and retention operate on individual semantic actions, never on the shortened counter. The input presenter itself preserves history on pause/reset, but the application lifecycle clears active and fading history whenever capture pauses or the connection resets; old input does not reappear when casting resumes. XKB translation is not a reconstruction of application- or IME-committed text.

Themes may opt into held-key feedback through the read-only API; the bundled theme does not render it. The `text-held` example renders fixed slots for modifiers observed as currently pressed. It clears on pause, disconnect, keymap replacement, and keyboard removal; reconnect snapshots do not contain held keys, so unknown held state is not inferred.

## Validation

Run the debug test suite with `ctest --preset debug` after building the test targets. The automated tests cover configuration, input interpretation, history, IPC/lifecycle, and theme behavior (including offscreen QML checks). They do not verify physical input timing, installed font glyphs, or rendering under a live Hyprland session; test those separately before release.
