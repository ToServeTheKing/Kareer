# Contributing to Kareer

## Build

Kareer is a KDE/Kirigami application (C++20 + QML) built with CMake and
Extra CMake Modules. See the README for the full list of system packages
to install, then:

```sh
cmake -B build -G Ninja
cmake --build build
./build/bin/kareer
```

Every GUI action is also available from the `kareer` CLI
(`add|list|show|update|stage|delete|stats|stages`) — run `./build/bin/kareer
--help` to see the subcommands.

To develop against a throwaway database instead of your real one, pass
`--db` (the file is created if it doesn't exist); it works for the GUI and
every subcommand:

```sh
./build/bin/kareer --db build/dev.sqlite add --company Acme --title Engineer
./build/bin/kareer --db build/dev.sqlite
```

To see the first-run "where should the database live?" dialog, start with
empty config and data directories:

```sh
XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) ./build/bin/kareer
```

## Test

```sh
ctest --test-dir build --output-on-failure
```

Tests live in `autotests/` and link the core logic directly out of `src/`
(see `autotests/CMakeLists.txt`). CI runs the same suite on every pull
request via `.github/workflows/test.yml`, using `flatpak-builder`'s
`run-tests` option so the run happens inside the same KDE SDK sandbox as
release builds.

## Conventions

<!-- REUSE-IgnoreStart -->
- Every source file starts with `SPDX-License-Identifier: GPL-3.0-or-later`
  (in whatever comment syntax fits the file type). Files that can't carry
  an inline header — `.desktop`, `keys/*.asc` — are covered instead by
  `REUSE.toml`.
<!-- REUSE-IgnoreEnd -->
- `src/` is intentionally flat: one class per concern, no `models/`,
  `controllers/`, or `viewmodels/` subfolders.
- C++ backend classes are exposed to QML via `QML_ELEMENT`; QML views are
  meant to stay thin renderers over that state, not hold logic themselves.
- `JobsDatabase` is the only class that touches the SQLite database
  directly — route all persistence changes through it.

## Releasing

`.github/workflows/build.yml` builds, signs, and publishes a Flatpak
bundle whenever a `v*` tag is pushed. To cut a release: bump
`project(... VERSION ...)` in `CMakeLists.txt`, add a matching `<release>`
entry to the metainfo file, update the manifest's pinned `tag:`, then tag
and push.
