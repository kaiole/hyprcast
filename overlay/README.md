# Overlay windowing proof

This small Qt Quick + LayerShellQt executable validates the compositor surface before the key-history UI is built. It is not connected to Hyprcast IPC yet.

## Build

Install Qt 6.5+ (Core, Gui, Qml, Quick) and LayerShellQt, then build the overlay target. To build without the Hyprland plugin:

```sh
cmake --preset debug -DHYPRCAST_BUILD_PLUGIN=OFF -DHYPRCAST_BUILD_OVERLAY=ON
cmake --build --preset debug --target hyprcast-overlay
```

## Run

Use `hyprctl monitors` to find an output name. The surface defaults to the primary output, anchored bottom-right, with a 600x88 logical-pixel size:

```sh
./build/debug/overlay/hyprcast-overlay \
  --monitor eDP-1 \
  --anchor bottom-right \
  --margins 24,24,24,24 \
  --background-opacity 0.55
```

Supported anchors are `top`, `bottom`, `left`, `right`, and corners such as `top-left` or `bottom-right`. Margins are in left,top,right,bottom order. `--width`, `--height`, and `--text` change the sample; long text is elided/clipped. `--quit-after-ms 1500` is available for a bounded smoke test.

The window requests an alpha-capable surface and clears it transparent; only the QML background rectangle uses the configurable opacity. LayerShellQt is configured with no keyboard interactivity and no activation, while `Qt::WindowTransparentForInput` requests pointer click-through. These behaviors must still be verified on the target Qt/LayerShellQt/compositor versions.
