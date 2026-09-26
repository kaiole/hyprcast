# Hyprcast overlay

The separate Qt Quick + LayerShellQt executable displays interpreted keyboard input. The default view is deliberately minimal: typed characters, special-key labels and shortcut chords. Held state is tracked for chord grouping and repeat cancellation, but the default view does not render a held-key indicator. Connection/protocol diagnostics stay in logs rather than the normal overlay.

## Build

Install Qt 6.5+ (Core, Gui, Network, Qml, Quick), LayerShellQt, `libxkbcommon` 0.7+ development files, and XKB keymap data for the input tests. Build the overlay without the Hyprland plugin:

```sh
cmake --preset debug -DHYPRCAST_BUILD_PLUGIN=OFF -DHYPRCAST_BUILD_OVERLAY=ON
cmake --build --preset debug --target hyprcast-overlay overlay_ipc_tests overlay_input_tests
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

The visible output is a bounded, transient display buffer; this phase does not implement editable history, Backspace deletion, retention settings, configuration files, or user themes. XKB translation is not a reconstruction of application- or IME-committed text.

## Validation notes

The prior phase-1 implementation session reported live Hyprland checks for placement, scaling, clipping, output removal, and monitor capture. The user separately confirmed pointer click-through and that OBS can record the overlay, and installed LayerShellQt system-wide. Exact Qt/LayerShellQt versions were not recorded, so other environments should still verify their compositor/toolkit combination. Phase-3 keyboard interpretation is covered by deterministic local tests; a live physical-keyboard check remains environment-specific.
