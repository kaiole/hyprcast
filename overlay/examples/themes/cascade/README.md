# Cascade

A bottom-anchored, receding stack of raised monochrome keycaps. The newest two
groups stay full-size and opaque; older groups shrink and fade. Chords contain
separate modifier/key caps that move together. These are historical actions,
not indicators of keys still held.

## Install

The normal CMake install includes this package. From a source checkout:

```sh
mkdir -p "${XDG_DATA_HOME:-$HOME/.local/share}/hyprcast/themes"
cp -r overlay/examples/themes/cascade "${XDG_DATA_HOME:-$HOME/.local/share}/hyprcast/themes/"
hyprcast-overlay restart
```

Install only trusted themes: QML executes in the overlay process and is not
sandboxed. Package helpers are local; no other example or extra effect module
is required. Restart after changing any theme files; configuration options can
reload live.

## Demo configuration

Save as a separate TOML file and launch with `hyprcast-overlay --config PATH`,
or merge selected sections deliberately. Do not overwrite your personal config.

```toml
[window]
width = 420
height = 320
min_width = 240
min_height = 50
anchor = "bottom-right"
margins = [24, 24, 24, 24]
dynamic_size = false

[appearance]
font_family = "monospace"
font_weight = "normal"
background_color = "#191919"
background_opacity = 0.0
panel_border_width = 0
history_padding_x = 12

[display]
panel_visibility = "never"

[repeat]
presentation = "counted"
count_threshold = 4

[expiration]
after_ms = 3000
fade_duration_ms = 250

[theme]
id = "cascade"

[theme.options]
visible_groups = 6
group_alignment = "right"
motion = "full"
```

See `theme.toml` for annotated options and bounds. Caps use 24-pixel labels,
16-pixel horizontal padding, and a 3-pixel dark lip included inside the
48-pixel total height. The count
badge shows one group plus `×N` (not the expanded initial occurrences); count
updates do not pulse or replay entry motion. Symbols and symbol fonts come from
the shared configuration. Text is plain, including markup and Unicode.

## Sizing and shared settings

420 × 320 is the recommended starting point. Around 70 pixels high fits one
48-pixel row with the default border and safety inset. Smaller stages reduce
capacity rather than squeeze rows; a stage unable to fit a row shows no caps.
The surface and content viewport clip safely, including during movement.

Shared anchor, maximum/minimum dimensions, horizontal history padding, panel
visibility/background/opacity/border/radius, font family/weight, symbols, repeat
projection and expiration are honored. Cap colors and metrics are theme-local;
shared foreground/font size and text extra padding do not style these caps.

Dynamic sizing intentionally reserves a stable preferred stage: width 420 and
height sufficient for the configured visible capacity, bounded by the available
surface and shared minimum/maximum sizes. It does not resize for each insertion.
Groups align independently using `group_alignment`. Very long labels use middle
elision, then the entire group scales uniformly to fit the available width;
modifier/key context is preserved, though extreme chords can become small.

## Motion and model behavior

Arrival slides upward approximately 16 pixels while fading in. Movement and
depth changes retarget current positions rather than queue old transitions.
Removal fades the outgoing group and closes the gap. Loading existing history
has no per-key replay. Previously offscreen retained entries can return without
fresh arrivals. A virtualized ListView creates only the tail viewport plus a
small cache/transition buffer; no theme-owned copy of retained history exists.

`motion = "reduced"` or `transition_ms = 0` disables entry, displacement and
removal animation; static depth styling remains. Both modes use the backend
expiration duration for a simple snapshot fade. The backend owns all cleanup,
including immediate zero-duration expiration. Active and expired stacks are
never layered together. Clear/pause/disconnect cannot revive cached history.

## Validation

Build `overlay_theme_tests`, then run:

```sh
ctest --preset debug -R overlay-theme --output-on-failure
```

Tests cover discovery/loading, final geometry, bounded delegate count, small
surfaces, Unicode/chords, counted updates/Backspace, and snapshot transfer,
fresh input during fading, and zero-duration expiration. To inspect a static
software-rendered example from the test fixture:

```sh
HYPRCAST_CASCADE_SCREENSHOT=/tmp/cascade.png QT_QUICK_BACKEND=software \
  ctest --preset debug -R overlay-theme --output-on-failure
```

The fixture uses reduced motion and its existing symbol overrides; its white
background is the test window, not a theme panel. Static software rendering has
been inspected. Live Hyprland animation, rapid-input smoothness, and installed
font glyph coverage still require practical validation.
