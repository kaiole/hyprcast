# Hyprcast overlay

The separate Qt Quick + LayerShellQt executable displays interpreted keyboard input. Its minimal default view shows typed characters, special-key labels, and shortcut chords. The C++ history model exposes ordered entries, stable IDs, and keyboard/key metadata; bundled text and keycap presentations use the same editable history. Held-key feedback is optional. Connection and protocol diagnostics stay in logs.

## Build

Install Qt 6.5+ (Core, Gui, Network, Qml, Quick), LayerShellQt, `libxkbcommon` 0.7+ development files, XKB keymap data for the input tests, and toml++ 3.4+. CMake uses an installed toml++ package when available, otherwise fetches the pinned v3.4.0 source. Build the overlay without the Hyprland plugin:

```sh
cmake --preset debug -DHYPRCAST_BUILD_PLUGIN=OFF -DHYPRCAST_BUILD_OVERLAY=ON
cmake --build --preset debug --target hyprcast-overlay overlay_ipc_tests overlay_input_tests overlay_history_tests overlay_config_tests
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
- `[display]`: `presentation` (`text` or `keycaps`) and `show_held_keys` (boolean).
- `[history]`: `backspace` (`delete` or `symbol`) and `max_retained_utf16_code_units` (1–1,048,576). The budget counts the formatted representation in UTF-16 code units, including labels and separators, independently of font, presentation, or viewport. Reducing it trims old history immediately; surviving entries keep their IDs.
- `[repeat]`: `enabled` (boolean). Hyprland still supplies each keyboard's repeat rate and delay. Disabling repeat cancels current repeat timers; enabling it for an already-held key restarts that key's configured delay.
- `[expiration]`: `after_ms` (0–86,400,000; zero disables future expiration) and `fade_duration_ms` (0–60,000; zero removes expired history without fading). Changing the inactivity interval restarts the current history's idle deadline. Changing fade duration during an active fade restarts that visual fade at full opacity. Expiration removes entries from editable history immediately; its separate visual snapshot cannot consume Backspace. New history cancels an old snapshot.

Appearance, layout, presentation, and behavior changes preserve existing history; behavior options apply prospectively, except that reducing the retention budget necessarily trims old content. Overlay TOML configures the bundled UI only. Optional user-loadable QML themes and their public API are a later phase. Plugin Lua configuration continues to control capture filtering and controls.

To select an instance when the environment variable is unavailable, pass `--instance-signature NAME`; `--socket PATH` can specify a socket directly. `--quit-after-ms 1500` is available for smoke tests. The client connects asynchronously and retries with exponential backoff capped at 10 seconds. It validates newline-delimited JSON messages and disconnects/retries on malformed or oversized frames (4 MiB maximum).

## Interpretation and current boundaries

The client applies each valid protocol transition before synchronously delivering that message to the input interpreter, preserving wire order. Key interpretation is independent per keyboard. Hyprland sends evdev keycodes; the overlay adds XKB's `+8` offset, compiles the protocol's XKB text-V1 keymap, and interprets text and modifier consumption with libxkbcommon. Plugin snapshots use the same V1 format as incremental keymap events. Repeats are generated locally from each keyboard's repeatability, rate, and delay.

Modifier changes from Hyprland are authoritative: key events update local XKB pressed-key state, then modifier messages reconcile the XKB masks. Pause, keymap replacement, keyboard removal, disconnect, and key release clear affected held/repeat state. Reconnect snapshots do not include held keys, so the overlay does not infer or repeat keys it did not observe pressed.

Interpreted actions are stored in a `QAbstractListModel` with stable entry IDs and roles for kind, text, key, modifiers, keyboard/key identity, event time, and repeat metadata. Model updates use row insertions/removals and `dataChanged` for partial text edits and retention trimming rather than resetting the list on each key. Both bundled presentations consume this model; text output is also derived from it.

Old text is trimmed at grapheme boundaries and special keys/chords at entry boundaries. The text viewport uses measured `Text.ElideLeft`; the keycap viewport tail-follows retained entries and hides a clipped leading cap whole. Neither viewport discards off-screen content, so older retained content reappears as newer content is erased.

Plain Backspace defaults to deletion: it removes one Unicode grapheme from text, or one whole special-key/chord entry. Generated Backspace repeats apply that deletion repeatedly. In `symbol` mode Backspace is recorded as a visible key. Modified shortcuts such as Ctrl+Backspace remain visible chords and do not emulate word deletion. Pause and connection reset preserve history while clearing input/repeat/held-key state. XKB translation is not a reconstruction of application- or IME-committed text.

Held-key feedback reports keys observed as currently pressed, including modifiers. It clears on pause, disconnect, keymap replacement, and keyboard removal; reconnect snapshots do not contain held keys, so unknown held state is not inferred.

## Validation

The debug build and all 23 configured CTest tests pass. Configuration tests cover defaults, partial TOML, strict validation, CLI precedence, atomic replacement, malformed-save retention, recovery, and all-or-nothing rejection of runtime-invalid candidates. Input/history tests cover dynamic repeat/history settings and expiration/fade lifecycle. Offscreen startup succeeded for both presentations. A live Hyprland smoke test using an isolated synthetic IPC socket also verified text-to-keycap and style reloads, preserved visible history through an invalid save, accepted more input, and recovered to text on the next valid save. This did not exercise a physical keyboard or monitor changes.
