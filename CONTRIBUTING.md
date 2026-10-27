# Contributing to Kareer

Thanks for helping out. Bug reports, feature ideas and pull requests are all
welcome. Please follow the [Code of Conduct](CODE_OF_CONDUCT.md), and report
security problems privately as described in [SECURITY.md](SECURITY.md).

## License

Kareer is licensed under the GNU General Public License v3.0 or later (see
[LICENSE](LICENSE)). By submitting a contribution, you agree that it is
licensed under the same terms.

## Pull requests

1. Fork the repository and create a branch from `main`.
2. Make your change, with tests where it makes sense.
3. Make sure the build and the tests pass (see below).
4. Open a pull request and fill in the template: the reason for the change,
   how you tested it, and screenshots for anything visible in the UI.

## Build

Kareer is a KDE/Kirigami application (C++20 + QML) built with CMake and
Extra CMake Modules. Install the Qt 6 and KDE Frameworks 6 development
packages that `CMakeLists.txt` asks for, then:

```sh
cmake -B build -G Ninja
cmake --build build
./build/bin/kareer
```

Every GUI action is also available from the `kareer` CLI
(`add|list|show|update|stage|delete|stats|stages|history|sync`). Run
`./build/bin/kareer --help` to see the subcommands.

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
  an inline header, such as `.desktop` and `keys/*.asc`, are covered instead
  by `REUSE.toml`.
<!-- REUSE-IgnoreEnd -->
- `src/` is intentionally flat: one class per concern, no `models/`,
  `controllers/`, or `viewmodels/` subfolders.
- C++ backend classes are exposed to QML via `QML_ELEMENT`; QML views are
  meant to stay thin renderers over that state, not hold logic themselves.
- `JobsDatabase` is the only class that touches the SQLite database
  directly. Route all persistence changes through it.

## Releasing

`.github/workflows/build.yml` builds, signs, and publishes a Flatpak
bundle whenever a `v*` tag is pushed. To cut a release: bump
`project(... VERSION ...)` in `CMakeLists.txt`, add a matching `<release>`
entry to the metainfo file, update the manifest's pinned `tag:`, then tag
and push.
