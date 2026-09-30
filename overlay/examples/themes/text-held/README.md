# Ribbon with live modifiers (`text-held`)

Recent projected actions form a right-anchored raised-keycap ribbon above a
fixed Ctrl / Shift / Alt / Super dock. All groups remain full-size and fully
opaque until a narrow fade zone at the far left edge (at most 12 px). There is
no age-based shrinking or fading. Chords move as one
group, with separate adjacent modifier and key caps. These are history visuals,
not evidence that a historical key is still down.

Install this entire directory in
`${XDG_DATA_HOME:-~/.local/share}/hyprcast/themes/text-held/` and select the ID.
Theme files are not watched: restart the overlay after editing QML.

## Demo configuration

Merge these sections into your config manually; the theme cannot enlarge the
application's surface. No personal configuration is changed by installation.

```toml
[window]
width = 600
height = 140

[theme]
id = "text-held"

[theme.options]
motion = "full"
dock_alignment = "center"
show_altgr = false
```

600 × 140 is the recommended starting surface. With default metrics, 400 × 120
is a useful lower target for four readable modifier labels. Narrower docks use
equal bounded slots and middle elision; long history labels are middle-elided
before fitting the whole group uniformly. Extremely long chords can still
become small: modifiers and final-key context are kept rather than dropping
arbitrary caps. Short surfaces reduce both cap heights/fonts proportionally;
insufficient space yields zero-height bands, never overlapping rows. Excessive
configured padding can leave no content area. There is no side-by-side fallback.

Dynamic sizing reserves a content-independent stage: preferred width 600,
preferred height `2 * height + row_gap + 2 * (inner_padding + panel border)`.
Shared minimum dimensions can increase this preference, but configured maximum
surface dimensions and actual available bounds always win. Presses, releases,
arrival/removal and idle state never resize the panel or either band. Hiding the
dock still reserves its band.

## State and lifetime

Slots match canonical `heldKeyItems.identity`, not displayed glyphs. Any matching
observed instance keeps a modifier lit, including multiple keyboards. Idle
slots use the same shared modifier mappings and symbol font as active slots.
`show_altgr = true` adds a distinct fifth AltGr slot; AltGr is never treated as
Alt. No left/right physical-key distinctions are implied. Idle means **not
currently observed held**: capture may have started with unknown held state.
The supplied held-list clearing immediately clears active styling; pressed
face travel converges over 90 ms and is disabled with reduced motion.

Idle dock slots are hidden unless there is active history, an expiration
snapshot, an observed dock modifier, or `panel_visibility = "always"`.
`with-content` follows those same content criteria: when history expires and no
modifier is held, panel decoration and the idle dock fade together with the
backend snapshot. `always` keeps panel decoration and the dock visible and opaque
while only history fades. An observed held modifier keeps the dock and panel
opaque independently of history expiration. `never` hides panel decoration, not
meaningful history/live feedback; its idle dock follows the same content fade.
The backend owns retention, Backspace, repeats and snapshot cleanup. Hidden
history is retained and may reappear after deletion.

A virtualized model view realizes only the bounded tail plus a small buffer.
Group spacing accounts for fitted widths. Groups fade only in the final
`min(12 px, 5% of viewport width)` before the left boundary (minimum 1 px);
no shaders or shared/private theme imports are needed. Normal caps remain
full-size; uniform scaling is only an overflow fallback for oversized groups. Arrivals
fade in without replaying existing rows on initial load, snapshot transfer,
repeat updates or Backspace. Motion retargets rather than queuing transitions.
`reduced` removes ribbon animation and pressed-face travel, while preserving
the left-edge fade and backend expiration fades.

## Options

All names below are theme-local, under `[theme.options]`. Validation bounds
are also declared in `theme.toml`; undeclared or mistyped settings are rejected.

| Option | Default | Bounds / meaning |
| --- | --- | --- |
| `height` | 48 | 1–512 px, both cap bands |
| `font_size` | 24 | 1–256 px, additionally bounded by band height |
| `padding_x` | 16 | 0–512 px, clamped to half band height |
| `radius` | 7 | 0–256 px, clamped to cap geometry |
| `edge_depth` | 3 | 0–24 px, clamped to half cap height; 0 is safe |
| `edge_color` | `#101010` | color, lower lip |
| `border_width` | 1 | 0–64 px, clamped; 0 hides outline |
| `border_color` | `#444444` | color |
| `text_color` | `#cccccc` | color, labels and repeat badge |
| `text_background` | `#191919` | color, printable actions |
| `key_background` | `#2a2a2a` | color, special/modifier caps |
| `inner_spacing` | 3 | 0–128 px, within chords |
| `visible_groups` | 12 | 1–12, maximum tail; width may hide more |
| `group_spacing` | 8 | 0–48 px, between fitted groups |
| `motion` | `full` | `full` / `reduced` |
| `transition_ms` | 180 | 0–600 ms, ribbon only; 0 stops ribbon animation. Dock presses still animate for 90 ms; use `motion = "reduced"` to disable both. |
| `show_held_keys` | true | boolean, show dock without changing layout |
| `show_altgr` | false | boolean, optional fifth fixed slot |
| `dock_alignment` | `center` | `left` / `center` / `right` |
| `dock_spacing` | 6 | 0–128 px, bounded further on narrow surfaces |
| `dock_idle_opacity` | 0.45 | 0.1–1.0 |
| `dock_active_background` | `#444444` | color, held face |
| `row_gap` | 12 | 0–64 px, separation between bands |
| `inner_padding` | 10 | 0–64 px, vertical inset |

Shared anchor, surface/max/min sizing, dynamic sizing, history horizontal
padding, panel background/opacity/border/radius/visibility, font family/weight,
symbol font/mappings, repeat presentation and expiration are honored.
Shared text font size, foreground, line/separator spacing, text extra padding,
and text highlight/background geometry are replaced by cap options. Caps have
their own opaque faces; panel background opacity affects decoration only.

## Migration from the receding ribbon

Remove `depth_scale` and `depth_opacity` overrides: those controls have been
removed and are rejected, not silently ignored. Cap size and opacity no longer
depend on history rank. The default `visible_groups` is now 12 to keep more
full-size caps available near the left boundary; a lower configured limit can
still truncate the tail before it reaches that boundary.

## Migration from the former text/chip example

This redesign deliberately removes the compact and automatic side-by-side
compositions. Old settings are rejected rather than accepted as inert aliases.
Remove `layout` and `compact_held_fraction`; replace `held_side` with
`dock_alignment`. `show_held_keys` remains supported but now means modifiers
only, not all ordinary held keys.

| Old styling | Replacement |
| --- | --- |
| `held_key_height` / `held_font_size` | `height` / `font_size` |
| `held_key_padding_x` / `held_key_radius` | `padding_x` / `radius` |
| `held_key_spacing` | `dock_spacing` |
| `held_row_padding_x` | shared `[appearance].history_padding_x` |
| `held_row_padding_bottom` | `inner_padding` (symmetric vertical inset) |
| `held_key_background` / `held_text_background` | `key_background` / `text_background` |
| `held_key_text_color` | `text_color` |
| `held_key_border_width` / `held_key_border_color` | `border_width` / `border_color` |

Cap styling now applies to both rows, so these replacements are not identical
visual presets. Config reload rejects obsolete options and preserves the last
accepted configuration. Increase the surface height explicitly when migrating.

## Validation

```sh
cmake --build build/debug --target overlay_theme_tests -j2
ctest --preset debug -R overlay-theme --output-on-failure
```

Tests exercise final-state geometry with reduced motion, Unicode/plain labels,
chord grouping, bounded delegates with retained history/full-motion bursts,
repeat/Backspace, expiration, identity collisions, multi-keyboard aggregation,
held resets, retention trimming, narrow/short geometry and live options. Use
`QT_QUICK_BACKEND=software HYPRCAST_TEXT_HELD_SCREENSHOT=/tmp/text-held.png`
when running the test for an optional offscreen rendering artifact. Screenshots
inspect static styling, not motion timing or actual Wayland placement.
