# Hyprcast overlay

The Qt Quick + LayerShellQt executable is a small proof surface and IPC diagnostic client. It is not yet the key-history UI.

## Build

Install Qt 6.5+ (Core, Gui, Network, Qml, Quick) and LayerShellQt, then build the overlay target. To build without the Hyprland plugin:

```sh
cmake --preset debug -DHYPRCAST_BUILD_PLUGIN=OFF -DHYPRCAST_BUILD_OVERLAY=ON
cmake --build --preset debug --target hyprcast-overlay overlay_ipc_tests
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

The surface requests an alpha-capable buffer and clears it transparent; only the QML background rectangle uses the configurable opacity. LayerShellQt is configured with no keyboard interactivity and no activation, while `Qt::WindowTransparentForInput` requests pointer click-through.

## Validation notes

The phase-1 implementation session reported live Hyprland checks for placement, scaling, clipping, output removal, and monitor capture. The user separately confirmed pointer click-through and that OBS can record the overlay, and installed LayerShellQt system-wide. Exact Qt/LayerShellQt versions were not recorded, so other environments should still verify their compositor/toolkit combination.
