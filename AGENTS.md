# hyprcast

Shows keystrokes on screen in Hyprland. Two components talk over a Unix socket
at `$XDG_RUNTIME_DIR/hyprcast/<instance signature>/events.sock`:

- **plugin** (`libhyprcast.so`): loaded into the Hyprland compositor. Subscribes
  to keyboards and sends key, modifier, keymap and repeat-info events as JSON
  lines. Runs on the compositor thread and must never block or throw into it.
- **overlay** (`hyprcast-overlay`): a Qt 6 / QML layer-shell window that reads
  the events and renders them through a theme. One resident process per
  session; further invocations forward `toggle` or `restart` to it.

Both are single-threaded by design. The lint configuration bans threads and
blocking sleeps; lifting a ban means adding a tsan preset to the gate first.

## Hard safety rules

The owner's live desktop session runs on the machine this repository is
developed on. These rules have no exceptions.

1. Never interact with the running Hyprland: no `hyprctl`, never load or reload
   a plugin, never run the built `hyprcast-overlay` against the real session.
   Anything that could open the socket runs with `XDG_RUNTIME_DIR` pointing at
   a fresh temporary directory. Tests create their own temporary directories
   and never use the real runtime directory.
2. Never write to or delete `build/debug`. The running compositor has
   `build/debug/plugin/libhyprcast.so` loaded and keybinds point into
   `build/debug/overlay`. The presets build into `build/dev`, `build/lint` and
   `build/release`.
3. Never edit anything outside this repository, in particular not `~/dotfiles`
   (Hyprland hot-reloads its config). Scratch files go in a temporary
   directory, not in the repository.
4. Never push, open pull requests or tag. Local commits only.
5. A sanitizer-built plugin must never be loaded into a compositor. Do not add
   anything that would do so.

## Directory map

| Path | Contents |
|---|---|
| `plugin/src/keycaster.*` | Plugin root object: wires registry, config and event server |
| `plugin/src/hyprland/` | Plugin entry points, keyboard registry, Lua config glue |
| `plugin/src/ipc/` | Event server: socket, client connection, outgoing queue |
| `plugin/src/os/`, `plugin/src/wayland/` | RAII wrappers: file lock, Wayland event source |
| `plugin/src/config/`, `plugin/include/` | Plugin configuration parsing, free of Hyprland headers so it is testable |
| `plugin/tests/` | Plugin tests (configuration only; nothing loads the plugin) |
| `protocol/include/hyprcast/protocol/` | Event types and their JSON wire format |
| `common/` | Code shared by both components; `version.hpp.in` is configured by CMake |
| `overlay/src/ipc/` | Socket client, message parsing, single-instance and restart handling |
| `overlay/src/input/` | xkb interpretation of key events into actions |
| `overlay/src/history/` | The list of shown actions and its presentation model |
| `overlay/src/config/` | `overlay.toml` parsing, validation and live reload |
| `overlay/src/theme/` | Theme discovery, manifests and the QML runtime |
| `overlay/qml/`, `overlay/examples/themes/` | Theme host, bundled default theme, example themes |
| `overlay/tests/` | Overlay tests, one executable per area |
| `docs/` | User documentation for configuration and themes |
| `cmake/`, `CMakePresets.json` | Build helpers and the `dev`, `lint` and `release` presets |

## Build and gate

```sh
cmake --workflow --preset dev      # clang, ASan + UBSan, runs the tests
cmake --workflow --preset lint     # clang + clang-tidy, findings are errors
cmake --workflow --preset release  # gcc, RelWithDebInfo, runs the tests
./check                            # the gate: clang-format, then all three
```

`./check` must pass before every commit. Run it with the session variables
removed so nothing can reach the live compositor:

```sh
env -u WAYLAND_DISPLAY -u HYPRLAND_INSTANCE_SIGNATURE -u DISPLAY \
    XDG_RUNTIME_DIR="$(mktemp -d)" ./check
```

Do not weaken a check, warning or test to get green. A lint suppression is a
`NOLINTNEXTLINE(check-name)` on the one line it applies to, with the reason in
a comment directly above it. A check is disabled in `.clang-tidy` only when it
is wrong for the code base as a whole, with the reason next to it.

## Conventions

- C++23, formatted by `.clang-format`.
- Naming is enforced by the `.clang-tidy` files. `plugin/`, `protocol/` and
  `common/` follow the root config: snake_case for everything, private members
  with a trailing underscore, no Hyprland-style prefixes (`C`, `S`, `e`, `m_`,
  `g_p`), snake_case file names. `overlay/` overrides it with Qt style
  (CamelCase types and file names, camelCase functions, `m_` members). The
  namespaces `hyprcast` and `hyprcast::overlay` are lower case in both. Names
  owned by Hyprland, hyprutils, Lua, wayland or xkb keep their spelling.
- Do not give a variable or member the name of a type: gcc rejects what clang
  accepts (`-Wshadow`, `-Wchanges-meaning`). Types are named for what they
  hold (`modifier_state`, `keymap_text`), variables for their role.
- The QML theme API is public and versioned (`api_version` in `theme.toml`).
  Do not change it without bumping the version.
- Every behaviour change comes with a test that fails without it.

## Commits

Conventional Commits: `type(scope): summary`, imperative, subject under 50
characters. Add a body only for what the diff cannot show. One commit per
logical change; purely mechanical changes go in commits that contain nothing
else. Stage files explicitly and never commit build output. No
`Co-Authored-By` or other trailers.
