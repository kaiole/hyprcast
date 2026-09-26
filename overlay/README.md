# Hyprcast overlay

The separate Qt Quick + LayerShellQt executable displays interpreted keyboard input. The default view is deliberately minimal: typed characters, special-key labels and shortcut chords. Held state is tracked for chord grouping and repeat cancellation, but the default view does not render a held-key indicator. Connection/protocol diagnostics stay in logs rather than the normal overlay.

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

To select an instance when the environment variable is unavailable, pass `--instance-signature NAME`; `--socket PATH` can specify a socket directly. The client connects asynchronously and retries with exponential backoff capped at 10 seconds. It validates newline-delimited JSON messages and disconnects/retries on malformed or oversized frames (4 MiB maximum).

Supported anchors are `top`, `bottom`, `left`, `right`, and corners such as `top-left` or `bottom-right`. Margins are in left,top,right,bottom order. `--width`, `--height`, and `--quit-after-ms 1500` are available for layout and smoke tests.

## Interpretation and current boundaries

The client applies each valid protocol transition before synchronously delivering that message to the input interpreter, preserving wire order. Key interpretation is independent per keyboard. Hyprland sends evdev keycodes; the overlay adds XKB's `+8` offset, compiles the protocol's XKB text-V1 keymap, and interprets text and modifier consumption with libxkbcommon. Plugin snapshots now use the same V1 format as incremental keymap events. Repeats are generated locally using the keyboard's repeatability, rate, and delay settings.

Modifier changes from Hyprland are authoritative: key events update local XKB pressed-key state, then modifier messages reconcile the XKB masks. Pause, keymap replacement, keyboard removal, disconnect, and key release clear affected held/repeat state. Reconnect snapshots do not include held keys, so the overlay does not infer or repeat keys that it did not observe pressed.

Interpreted actions are stored as typed history entries with stable entry IDs and keyboard/key identity; display text is derived from those entries. The history retains at most 4096 UTF-16 code units of its rendered representation (including key labels and synthetic separators), independent of surface size or font metrics. Old text is trimmed at grapheme boundaries and special keys/chords at entry boundaries. The QML `Text.ElideLeft` viewport measures the actual font/layout width and shows the history tail without discarding off-screen entries, so older retained content reappears as newer content is erased.

Plain Backspace currently uses the temporary phase-4 default of deletion: it removes one Unicode grapheme from text, or one whole special-key/chord entry. Generated Backspace repeats apply that deletion repeatedly. An explicit internal option also supports displaying Backspace as a key; this is not yet a user-facing setting. Modified shortcuts such as Ctrl+Backspace remain visible chords and do not emulate word deletion. Pause and connection reset preserve the history while clearing input/repeat state. XKB translation is not a reconstruction of application- or IME-committed text.

## Validation notes

Phase-3 keyboard interpretation and phase-4 history behavior have deterministic local tests, including Unicode deletion, mixed keyboards, retention, and pause/reset preservation. Automated tests passed for the configured overlay targets. Phase-4 changes have not been live-tested in Hyprland in this session; physical keyboard and viewport behavior remain environment-specific. The prior phase-1 session reported live checks for placement, scaling, clipping, output removal, and monitor capture, and the user confirmed pointer click-through and OBS capture. Exact Qt/LayerShellQt versions were not recorded.
