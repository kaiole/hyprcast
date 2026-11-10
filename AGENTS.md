# hyprcast

Shows keystrokes on screen in Hyprland. This file is for the LLM agents that
maintain the repository; `README.md` and `docs/` are for users.

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
5. A sanitizer-built plugin (`build/dev`) must never be loaded into a
   compositor. Do not add anything that would do so.

## Components

- **plugin** (`libhyprcast.so`, `plugin/`): a module loaded into the Hyprland
  compositor. It hooks keyboard setup, listens to every keyboard and serves one
  client on a Unix socket. It runs on the compositor thread, so it must never
  block and never let an exception escape into Hyprland.
- **overlay** (`hyprcast-overlay`, `overlay/`): a Qt 6 / QML layer-shell window
  that connects to that socket, interprets the events with xkbcommon and shows
  them through a QML theme. One resident process per session; later
  invocations forward `toggle` or `restart` to it and exit.

Both are single-threaded by design.

Sockets, all below `$XDG_RUNTIME_DIR/hyprcast/<Hyprland instance signature>/`:

| Path | Owner | Carries |
|---|---|---|
| `events.sock` (lock: `server.lock`) | plugin | The wire protocol below. One client at a time; a new connection replaces the current one |
| `events.sock.overlay` (lock: `events.sock.overlay.owner-lock`) | resident overlay | `toggle` or `restart` from a later invocation, answered with `ok` or `error: ...` |

The overlay starts capture by sending `enable` and shows its window only while
the plugin confirms capture is on. `toggle` with no resident overlay starts
one; `restart` rebuilds the window and its QML engine inside the resident
process and keeps capture as it is.

## Directory map

| Path | Contents |
|---|---|
| `plugin/adapter/` | The module Hyprland loads and the only code that includes Hyprland headers: entry points, `setupKeyboard` hook, keyboard registry, Lua config glue, log sink, and the `keycaster` object that wires them to the session |
| `plugin/session/` | Capture rules: the paused flag, the session-lock check, what a client is told and when |
| `plugin/server/` | Event server: socket, lock file, the one client connection, bounded outgoing queue; RAII wrappers for the file lock and Wayland event sources |
| `plugin/wire/` | Encodes each protocol message as one JSON line |
| `plugin/config/` | The keyboard filter set by `hl.plugin.hyprcast.configure` |
| `plugin/log/` | The log sink the libraries report through, and `run_guarded` |
| `plugin/test_support/` | Test helpers: temporary directory, event loop, socket client, recording log |
| `plugin/<library>/tests/` | GoogleTest tests of that library; nothing loads the plugin |
| `plugin/adapter/tests/` | Not tests of the adapter, which cannot run without a compositor: tests of the hyprutils behaviour the adapter relies on |
| `protocol/include/hyprcast/protocol/` | Event types, and in `wire.hpp` every string of the wire format and the protocol version |
| `common/` | Code shared by both components; `version.hpp.in` is configured by CMake |
| `overlay/src/main.cpp` | Command line, the resident process, window and restart |
| `overlay/src/ipc/` | Socket client, message parsing, capture requests, and the single resident instance with the commands forwarded to it |
| `overlay/src/input/` | xkb interpretation of key events into actions |
| `overlay/src/history/` | The list of shown actions and its presentation model |
| `overlay/src/config/` | `overlay.toml` parsing, validation and live reload |
| `overlay/src/theme/` | Theme discovery, manifests and the QML runtime |
| `overlay/qml/`, `overlay/examples/themes/` | Theme host, bundled default theme, example themes |
| `overlay/tests/` | GoogleTest tests of the overlay, one executable per area; `Support.hpp` holds their shared `main`, which keeps them away from the live session |
| `docs/` | User documentation for configuration and themes |
| `cmake/`, `CMakePresets.json`, `check` | Build helpers, the `dev`, `lint` and `release` presets, and the gate |

Every plugin library has the same layout: `CMakeLists.txt`,
`include/hyprcast/<library>/` and, where it has any, `src/` and `tests/`, with
the target `hyprcast_<library>` and the alias `hyprcast::<library>`. The
libraries build and are tested without Hyprland. Logic belongs in a library,
and the adapter stays as thin as the Hyprland API allows. Only
`hyprcast-overlay`, `libhyprcast.so` and the example themes are installed.

## Wire protocol

The plugin writes one JSON object per line. Every line has `event`; all but
`hello` have `keyboard_id`.

| `event` | Other fields | When |
|---|---|---|
| `hello` | `protocol_version` | First line of every connection |
| `casting_state` | `paused` (`keyboard_id` is 0) | After the hello, and in answer to every command |
| `subscribe_keyboard` | `name`, `keymap`, `depressed`, `latched`, `locked`, `group`, `rate`, `delay` | For every keyboard when input starts to flow, and when one appears |
| `unsubscribe_keyboard` | | When a keyboard goes away or input stops flowing |
| `key` | `time_ms`, `keycode`, `state` (`pressed` or `released`) | While input flows |
| `modifiers` | `depressed`, `latched`, `locked`, `group` | While input flows |
| `keymap` | `keymap` | While input flows |
| `repeat_info` | `rate`, `delay` | While input flows |

The overlay writes `enable` or `disable`, each on its own line. Anything else
disconnects it.

- The strings are defined once, in `protocol/include/hyprcast/protocol/wire.hpp`,
  and used by the encoder (`plugin/wire/src/messages.cpp`) and the parser
  (`overlay/src/ipc/Protocol.cpp`).
- Golden lines: `plugin/wire/tests/wire_test.cpp` (`WireMessages.*`).
  `overlay/tests/WireContract.cpp` (`WireContract.*`) feeds each encoded
  message to the overlay's parser.
- `protocol_version` in `wire.hpp` is the one definition. The overlay reads
  nothing else from, and sends nothing to, a plugin whose first line is not a
  hello with the same number. Raise it with every change an older plugin or
  overlay would misread, and never change the hello line itself.

## Invariants

The plugin sees every key pressed in the session, passwords included. The
privacy rules decide what leaves the compositor. A change that needs one of
the pinning tests weakened changes the rule: do not make it unless the owner
asked for exactly that.

Input flows only while capture is enabled and the session is not locked.
`session_test.cpp` is `plugin/session/tests/session_test.cpp` (`SessionTest.*`)
and `server_test.cpp` is `plugin/server/tests/server_test.cpp`
(`EventServerTest.*`).

| Privacy invariant | Pinned by |
|---|---|
| Capture starts paused | `session_test.cpp` (`StartsPausedWithoutAskingForKeyboards`, `TellsANewClientOnlyTheState`) |
| Capture pauses when the client goes away: closed, replaced, disconnected or shut down | `session_test.cpp` (`PausesWhen...`, `ShutdownDropsTheClientAndPauses`) |
| While paused the client is told the capture state and nothing else: no keys, modifiers, keymaps, repeat settings or keyboards | `session_test.cpp` (`SendsNothingDerivedFromInputWhilePaused`, `DropsEverythingWhileNoClientIsConnected`) |
| While the session is locked nothing derived from input is sent, enabled or not | `session_test.cpp` (`SendsNothingDerivedFromInputWhileLocked`, `EnableWhileLockedAnnouncesNoKeyboards`) |
| Nothing from a gap leaks out after it: every keyboard is announced afresh when input flows again, and the release of a key whose press was not sent is dropped | `session_test.cpp` (`EveryEnableSendsAFreshSnapshot`, `ResumesWithAFreshSnapshotAfterUnlock`, `DropsTheReleaseOf...`); the overlay side in `overlay/tests/Input.cpp` (`KeyboardInterpreterTest.ResentSnapshotReplacesKeyboardState`) and `overlay/tests/Ipc.cpp` (`ConnectionStateTest.FollowsResentSnapshots`) |
| Only the connected client's `enable` turns capture on: nothing on the compositor side (Lua function, config value, compositor event) does | `session_test.cpp` (`OnlyAClientCommandUnpauses`) |
| The overlay asks nothing of a plugin that speaks another protocol version | `overlay/tests/Ipc.cpp` (`IncompatiblePlugin.*`), `overlay/tests/Lifecycle.cpp` (`OverlayLifecycle.IncompatiblePluginIsAskedForNothing`) and `overlay/tests/Reload.cpp` (`OverlayStartup.IncompatiblePluginIsAskedForNothing`) |

"Locked" is Hyprland's session lock (`ext-session-lock-v1`, as `hyprlock`
takes it), which the adapter reads through the session's `locked_cb`; with no
lock manager to ask it answers locked. The session notices the lock when a
keyboard next reports something or the client sends a command. A password
typed anywhere else, such as a terminal or a polkit prompt, is captured like
any other input while capture is on.

| Structural invariant | Pinned by |
|---|---|
| Single-threaded, both components and their tests | Lint, not a test: the root `.clang-tidy` bans thread creation (`bugprone-unsafe-functions.CustomFunctions`) and the thread headers (`portability-restrict-system-includes`). The gate has no tsan preset for that reason; lifting a ban means adding one first |
| The plugin never blocks the compositor thread: sockets are non-blocking, a client that falls behind is queued for up to `event_server::max_queued_bytes` and then disconnected, a failed accept is retried on a timer | `server_test.cpp` (`DeliversAMessageLargerThanTheSocketBuffer`, `HoldsManySmallMessagesForASlowClient`, `DisconnectsAClientWhoseBacklogExceedsTheCap`, `LeavesTheListeningSocketAloneUntilTheRetry`); the same lint setting bans every sleep |
| No exception reaches the compositor from a callback: what Hyprland or the event loop calls runs inside `run_guarded`, which logs and then runs a recovery that cannot throw. `PLUGIN_INIT` alone throws, which is how Hyprland is told a load failed | `plugin/log/tests/log_test.cpp` (`RunGuarded.*`, `LogSink.SwallowsBackendFailures`), `server_test.cpp` (`FailureInThe...CallbackDisconnectsTheClient`), `session_test.cpp` (`DisconnectsAClient...`). The adapter's own use of it has no test: see the manual check below |
| The wire strings are a contract between plugin and overlay | `WireMessages.*` and `WireContract.*`, as above |
| Only `plugin/adapter/` includes compositor headers | `./check` greps for `hyprland/` and `hyprutils/` includes elsewhere |

## Verifying a change

```sh
cmake --workflow --preset dev      # configure, build, test
cmake --workflow --preset lint     # configure, build
cmake --workflow --preset release  # configure, build, test
./check                            # the gate: format, layering, then all three
```

| Preset | Build | For |
|---|---|---|
| `dev` | clang, Debug, ASan + UBSan, `_GLIBCXX_ASSERTIONS`, in `build/dev` | Day-to-day work. A memory error, undefined behaviour or a leak fails the test that hits it. Its plugin is never loaded anywhere |
| `lint` | clang with clang-tidy, every finding an error, in `build/lint` | Static checks, naming and the function bans. Runs no tests. Ninja lints only what it rebuilds: `cmake --build build/lint --target clean` first for a full pass |
| `release` | g++, RelWithDebInfo, in `build/release` | What users build and install, and the diagnostics only gcc gives. The only plugin a human may load |

All three treat warnings as errors and compile every header under an
`include/` directory on its own.

`./check` must pass before every commit: `clang-format --dry-run --Werror` over
every `.cpp` and `.hpp` outside `build/`, the include rule above, then the
three workflows. It takes under ten minutes from empty build directories. Run
it with the session variables removed so nothing can reach the live
compositor:

```sh
env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET -u DISPLAY \
    -u HYPRLAND_INSTANCE_SIGNATURE XDG_RUNTIME_DIR="$(mktemp -d)" ./check
```

One test, under the same `env` prefix: `ctest --preset dev -R 'SessionTest\.'`.

Every behaviour change comes with a test that fails without it. Do not weaken
a check, warning or test to get green.

## Lint suppressions

- A suppression names its check and has the reason in a comment next to it:
  `// NOLINTNEXTLINE(check-name)` for one line, `NOLINTBEGIN(check-name)` and
  `NOLINTEND(check-name)` around a few declarations that share one reason.
  Never a bare `NOLINT`.
- A check is disabled in a `.clang-tidy` file only when it is wrong for the
  code base as a whole, with the reason next to it. `overlay/.clang-tidy`
  changes naming only; the `tests/.clang-tidy` files relax naming and member
  visibility and keep every ban.
- A banned function is used only where the ban's reason does not apply, and
  the comment says why. The one case today is the sleep in
  `OverlayInstance::forward` (`overlay/src/ipc/OverlayInstance.cpp`): a
  forwarding invocation has no event loop to wait on.
- A leak that LeakSanitizer reports inside a library goes in
  `overlay/tests/lsan.supp` by the name of the leaking function, with the
  reason above it; ctest passes that file to the theme tests.

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
- The QML theme API is public and versioned (`api_version` in `theme.toml`,
  currently 1, documented in `docs/themes.md`). Do not change it without
  bumping the version.
- Dependencies fetched at configure time are pinned by URL and hash
  (`plugin/CMakeLists.txt`, `overlay/CMakeLists.txt`, `cmake/gtest.cmake`).
- A user-visible change updates `README.md` or `docs/` in the same commit
  series.

## Commits

Conventional Commits: `type(scope): summary`, imperative, subject under 50
characters. Add a body only for what the diff cannot show. One commit per
logical change; purely mechanical changes go in commits that contain nothing
else. Stage files explicitly and never commit build output. No
`Co-Authored-By` or other trailers.

## Manual check of the Hyprland adapter

**For a human at the machine only. An agent never runs any of this** (safety
rule 1). `plugin/adapter/` cannot be tested without a compositor, so after a
change to it, or a Hyprland update, ask the owner to run this and report back.

1. Build: `cmake --workflow --preset release`. Never the `dev` build.
2. Clear the way: `hyprctl plugin list`. If a hyprcast plugin is loaded, unload
   it with `hyprctl plugin unload <its path>`, since two copies cannot share
   the socket. End a running overlay with `killall hyprcast-overlay`.
3. Load: `hyprctl plugin load "$PWD/build/release/plugin/libhyprcast.so"`.
   `hyprctl plugin list` shows `Hyprcast`.
4. Start: `build/release/overlay/hyprcast-overlay toggle` in a terminal. Type
   in another window: the keys appear.
5. Toggle: run the same command from a second terminal. The overlay hides and
   typing shows nothing. Run it again: typing shows again.
6. Lock: with capture on, lock the session (`hyprlock`), type a few keys,
   unlock. Nothing typed while locked appears; keys typed after unlocking do.
7. Restart: `build/release/overlay/hyprcast-overlay restart` prints nothing,
   exits 0, and the window comes back with capture still on.
8. Unload: stop the overlay with Ctrl+C, then
   `hyprctl plugin unload "$PWD/build/release/plugin/libhyprcast.so"`.
   Hyprland keeps running, `hyprctl plugin list` no longer shows the plugin,
   and Hyprland's log has no hyprcast error.
9. Load again whatever step 2 unloaded.
