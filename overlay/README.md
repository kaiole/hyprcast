# Hyprcast overlay

The separate Qt Quick + LayerShellQt executable displays interpreted keyboard input. The default view is deliberately minimal: typed characters, special-key labels and shortcut chords. The C++ history model exposes ordered typed entries, stable IDs and keyboard/key metadata to QML; the bundled text and keycap presentations render that same editable history. Held-key feedback is available but off by default. Connection/protocol diagnostics stay in logs rather than the normal overlay.

## Build

Install Qt 6.5+ (Core, Gui, Network, Qml, Quick), LayerShellQt, `libxkbcommon` 0.7+ development files, and XKB keymap data for the input tests. Build the overlay without the Hyprland plugin:

```sh
cmake --preset debug -DHYPRCAST_BUILD_PLUGIN=OFF -DHYPRCAST_BUILD_OVERLAY=ON
cmake --build --preset debug --target hyprcast-overlay overlay_ipc_tests overlay_input_tests overlay_history_tests
ctest --preset debug
```

## Run

The overlay discovers the socket using `XDG_RUNTIME_DIR` and `HYPRLAND_INSTANCE_SIGNATURE`:

```text
$XDG_RUNTIME_DIR/hyprcast/$HYPRLAND_INSTANCE_SIGNATURE/events.sock
```

Use `hyprctl monitors` to find an output name. The surface defaults to the primary output, anchored bottom-right, with a 600x88 logical-pixel size:

```sh
./build/debug/overlay/hyprcast-overlay \
  --monitor eDP-1 \
  --anchor bottom-right \
  --margins 24,24,24,24 \
  --background-opacity 0.55
```

The text presentation is the default. Try the bundled keycaps and optional held-key feedback with:

```sh
./build/debug/overlay/hyprcast-overlay --presentation keycaps --show-held-keys
```

History expiration and fading are opt-in. For example, `--expire-after-ms 5000 --fade-duration-ms 250` removes history after five seconds without a history-changing action and visually fades its final snapshot over 250 ms. An expiration value of `0` disables expiration; a fade value of `0` removes expired history without fading.

To select an instance when the environment variable is unavailable, pass `--instance-signature NAME`; `--socket PATH` can specify a socket directly. The client connects asynchronously and retries with exponential backoff capped at 10 seconds. It validates newline-delimited JSON messages and disconnects/retries on malformed or oversized frames (4 MiB maximum).

Supported anchors are `top`, `bottom`, `left`, `right`, and corners such as `top-left` or `bottom-right`. Margins are in left,top,right,bottom order. `--width`, `--height`, and `--quit-after-ms 1500` are available for layout and smoke tests.

## Interpretation and current boundaries

The client applies each valid protocol transition before synchronously delivering that message to the input interpreter, preserving wire order. Key interpretation is independent per keyboard. Hyprland sends evdev keycodes; the overlay adds XKB's `+8` offset, compiles the protocol's XKB text-V1 keymap, and interprets text and modifier consumption with libxkbcommon. Plugin snapshots now use the same V1 format as incremental keymap events. Repeats are generated locally using the keyboard's repeatability, rate, and delay settings.

Modifier changes from Hyprland are authoritative: key events update local XKB pressed-key state, then modifier messages reconcile the XKB masks. Pause, keymap replacement, keyboard removal, disconnect, and key release clear affected held/repeat state. Reconnect snapshots do not include held keys, so the overlay does not infer or repeat keys that it did not observe pressed.

Interpreted actions are stored in a `QAbstractListModel` with stable entry IDs and roles for kind, text, key, modifiers, keyboard/key identity, event time and repeat metadata. Model updates use row insertions/removals and `dataChanged` for partial text edits/retention trimming rather than resetting the list on each key. Both bundled presentations consume this model; text output is also derived from it.

The history retains at most 4096 UTF-16 code units of its rendered representation (including key labels and synthetic separators), independent of surface size or font metrics. Old text is trimmed at grapheme boundaries and special keys/chords at entry boundaries. The text viewport uses measured `Text.ElideLeft`; the keycap viewport tail-follows the same retained entries and hides a clipped leading cap whole. Neither viewport discards off-screen content, so older retained content reappears as newer content is erased.

Plain Backspace currently uses the temporary phase-4 default of deletion: it removes one Unicode grapheme from text, or one whole special-key/chord entry. Generated Backspace repeats apply that deletion repeatedly. An explicit internal option also supports displaying Backspace as a key; this is not yet a user-facing setting. Modified shortcuts such as Ctrl+Backspace remain visible chords and do not emulate word deletion. Pause and connection reset preserve history while clearing input/repeat/held-key state. XKB translation is not a reconstruction of application- or IME-committed text.

Held-key feedback reports keys observed as currently pressed, including modifiers. It clears on pause, disconnect, keymap replacement and keyboard removal; reconnect snapshots do not contain held keys, so unknown held state is not inferred.

Expiration is disabled by default. When enabled, its inactivity timer restarts after a history change. At the deadline, expired entries are removed immediately from editable history. If fading is enabled, the presenter copies them to a separate visual-only model; only this expired snapshot fades, while active history and live held-key feedback remain fully opaque. New history cancels the snapshot and renders at full opacity. Expired entries cannot consume Backspace or reappear. C++ clears the snapshot using its own timer, independently of QML animation completion. `--fade-duration-ms` has no effect unless expiration is enabled.

## Validation notes

Deterministic local tests cover model roles/notifications, stable IDs, grapheme and atomic deletion, retention, held-state resets, and expiration/fade lifecycle, including held modifiers remaining live through expiration. `ctest --preset debug` passes all 22 configured tests. Live rendering was checked on Hyprland using an isolated temporary IPC socket (not the plugin's single-client socket): both presentations rendered mixed text/chords, held-key feedback appeared, and deleting newer content revealed older retained entries in both layouts. Physical-keyboard testing exposed two opacity defects: held modifiers faded with expired history, and new input faded in when interrupting a fade. The focused physical-keyboard retests for those fixes are still required; C++ lifecycle tests do not verify QML opacity animations. The prior phase-1 session reported live checks for placement, scaling, clipping, output removal, and monitor capture, and the user confirmed pointer click-through and OBS capture. Exact Qt/LayerShellQt versions were not recorded.
