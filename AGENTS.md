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
| `plugin/adapter/` | The module Hyprland loads and the only code that includes Hyprland headers: entry points, `setupKeyboard` hook, keyboard registry, Lua config glue, log sink, and the `keycaster` object that wires them to the session |
| `plugin/session/` | Capture rules: the paused flag, the session-lock check, what a client is told and when, routing of keyboard events to the encoder and the server |
| `plugin/server/` | Event server: socket, lock file, the one client connection, outgoing queue; RAII wrappers for the file lock and Wayland event sources |
| `plugin/wire/` | Encodes each protocol message as one JSON line |
| `plugin/config/` | Plugin configuration parsing |
| `plugin/log/` | The log sink the libraries report through, and `run_guarded` |
| `plugin/test_support/` | Test helpers: temporary directory, event loop, socket client, recording log |
| `plugin/<library>/tests/` | GoogleTest tests of that library; nothing loads the plugin |
| `protocol/include/hyprcast/protocol/` | Event types, their JSON wire format and the protocol version |
| `common/` | Code shared by both components; `version.hpp.in` is configured by CMake |
| `overlay/src/ipc/` | Socket client, message parsing, single-instance and restart handling |
| `overlay/src/input/` | xkb interpretation of key events into actions |
| `overlay/src/history/` | The list of shown actions and its presentation model |
| `overlay/src/config/` | `overlay.toml` parsing, validation and live reload |
| `overlay/src/theme/` | Theme discovery, manifests and the QML runtime |
| `overlay/qml/`, `overlay/examples/themes/` | Theme host, bundled default theme, example themes |
| `overlay/tests/` | Overlay tests, one executable per area; `WireContract.cpp` feeds the plugin's encoder output to the overlay's parser |
| `docs/` | User documentation for configuration and themes |
| `cmake/`, `CMakePresets.json` | Build helpers and the `dev`, `lint` and `release` presets |

Every plugin library has the same layout: `CMakeLists.txt`,
`include/hyprcast/<library>/` and, where it has any, `src/` and `tests/`, with
the target `hyprcast_<library>` and the alias `hyprcast::<library>`. The libraries build
and are tested without Hyprland; `./check` rejects a Hyprland include outside
`plugin/adapter/`. Logic belongs in a library, and the adapter stays as thin
as the Hyprland API allows.

## Privacy invariants

The plugin sees every key pressed in the session, passwords included. These
rules decide what leaves the compositor. Tests pin each one. A change that
needs one of those tests weakened changes the rule: do not make it unless the
owner asked for exactly that.

| Invariant | Pinned by |
|---|---|
| Capture starts paused | `plugin/session/tests/session_test.cpp` (`StartsPausedWithoutAskingForKeyboards`, `TellsANewClientOnlyTheState`) |
| Capture pauses when the client goes away: closed, replaced, disconnected or shut down | `session_test.cpp` (`PausesWhen...`, `ShutdownDropsTheClientAndPauses`) |
| While paused the client is told the capture state and nothing else: no keys, modifiers, keymaps, repeat settings or keyboards | `session_test.cpp` (`SendsNothingDerivedFromInputWhilePaused`, `DropsEverythingWhileNoClientIsConnected`) |
| While the session is locked nothing derived from input is sent, enabled or not | `session_test.cpp` (`SendsNothingDerivedFromInputWhileLocked`, `EnableWhileLockedAnnouncesNoKeyboards`) |
| Nothing from a gap leaks out after it: every keyboard is announced afresh when input flows again, and the release of a key whose press was not sent is dropped | `session_test.cpp` (`EveryEnableSendsAFreshSnapshot`, `ResumesWithAFreshSnapshotAfterUnlock`, `DropsTheReleaseOf...`); the overlay side in `overlay/tests/Input.cpp` (`testResentSnapshotReplacesKeyboardState`) and `overlay/tests/Ipc.cpp` (`testConnectionStateFollowsResentSnapshots`) |
| Only the connected client's `enable` turns capture on: nothing on the compositor side (Lua function, config value, compositor event) does | `session_test.cpp` (`OnlyAClientCommandUnpauses`) |
| The overlay asks nothing of a plugin that speaks another protocol version | `overlay/tests/Ipc.cpp` (`testIncompatiblePluginIsRefused`), `overlay/tests/Lifecycle.cpp` and `overlay/tests/Restart.cpp` (`testIncompatiblePlugin`) |

"Locked" is Hyprland's session lock (`ext-session-lock-v1`, as `hyprlock`
takes it), which the adapter reads through the session's `locked_cb`; with no
lock manager to ask it answers locked. A password typed anywhere else, such as
a terminal or a polkit prompt, is captured like any other input while capture
is on.

## Protocol version

`protocol_version` in `protocol/include/hyprcast/protocol/wire.hpp` is the one
definition. The plugin opens every connection with a `hello` line carrying it,
and the overlay reads nothing else from, and sends nothing to, a plugin whose
first line is not a hello with the same number. Raise it with every change an
older plugin or overlay would misread, and never change the hello line itself.
The golden lines are in `plugin/wire/tests/wire_test.cpp`, and
`overlay/tests/WireContract.cpp` feeds each encoded message to the overlay's
parser.

## Build and gate

```sh
cmake --workflow --preset dev      # clang, ASan + UBSan, runs the tests
cmake --workflow --preset lint     # clang + clang-tidy, findings are errors
cmake --workflow --preset release  # gcc, RelWithDebInfo, runs the tests
./check                            # the gate: format, layering, then all three
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
