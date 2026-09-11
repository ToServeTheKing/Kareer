# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Kareer is a KDE-style job application tracker: C++20 backend, QML/Kirigami frontend, ECM/CMake build, Qt 6 + KDE Frameworks 6. Data is a local SQLite file. Every GUI action is also a `kareer` CLI subcommand so scripts can drive it headlessly. Licensed GPL-3.0-or-later, REUSE-compliant.

## Commands

```sh
# Configure + build (Debug, tests on). A configured build/ dir may already exist.
cmake --preset ninja-dev          # or: cmake -B build -G Ninja
cmake --build build
./build/bin/kareer                # GUI
./build/bin/kareer list --json    # any recognized subcommand runs headless

# Tests (QtTest binaries; appstreamtest is added automatically by KDECMakeSettings)
ctest --test-dir build --output-on-failure
ctest --test-dir build -R sankeylayouttest          # one test binary
./build/bin/sankeylayouttest layoutReflectsTransitionCounts   # one test function

# Point the app at a throwaway DB (GUI or any subcommand; file created if missing)
./build/bin/kareer --db build/dev.sqlite add --company Acme --title Dev
./build/bin/kareer --db build/dev.sqlite
# Autotests use the env var instead: KAREER_DB_PATH=/tmp/x.sqlite

# Formatting: CI runs clang-format (pinned major 22) with the committed .clang-format
find src autotests -name '*.h' -o -name '*.cpp' | xargs clang-format --dry-run --Werror
# REUSE/SPDX check (CI): reuse lint
```

Configuring installs a clang-format git pre-commit hook via ECM if `clang-format` is on PATH. CI builds and tests inside the KDE Flatpak SDK (`flatpak-builder` with `run-tests: true` in the manifest), not a bare CMake build.

## Architecture

`src/` is intentionally flat: one class per concern, no subfolders. Backend classes are exposed to QML with `QML_ELEMENT` via the `io.github.toservetheking.Kareer` QML module declared in `src/CMakeLists.txt`. QML files are meant to be thin renderers; logic lives in C++ so it can be unit-tested.

**Entry point and CLI/GUI split.** `main.cpp` checks whether `argv[1]` is a known subcommand (`Cli::isSubcommand`). If so it builds a `QCoreApplication` and hands off to `Cli::run` in `clicommands.cpp`; otherwise it starts the Kirigami GUI. Adding a subcommand means adding it to both the list in `isSubcommand` and the dispatch in `run`, plus the README usage block.

**Database location.** `JobsDatabase::defaultPath()` resolves `--db` (stripped from argv in `main.cpp` before CLI/GUI routing) > `KAREER_DB_PATH` > the path the user chose in the GUI (`kareerrc` `[Database] Path`, loaded at startup for both GUI and CLI by `DatabaseLocation::loadConfiguredPath`) > `$XDG_DATA_HOME/kareer/kareer.sqlite`. On a GUI first run (nothing forced, file missing) `JobsDatabase::setSelectionPending(true)` makes default-constructed instances open nothing until `DatabaseSetupDialog.qml` picks a location. `DatabaseLocation` (QML singleton) writes the choice and emits `changed`; `Main.qml` then refreshes `JobsModel`, and every model calls `m_db.reopenIfPathChanged()` at the start of its refresh, so switching databases needs no restart.

**Persistence.** `JobsDatabase` is the only class that touches SQLite. Two tables: `jobs` and `stage_history`. Schema is created with `CREATE TABLE IF NOT EXISTS` in `migrate()`; there is no version number, so schema changes need an idempotent migration step there. Each `JobsDatabase` instance opens its own uniquely named `QSqlDatabase` connection, so `JobsModel`, `StatsModel` and `SankeyModel` each hold their own instance on the same file. Consequence: after a write through one model, the others do not know; QML wires this up (`DashboardPage` listens to `JobsModel.countChanged` and calls `statsModel.refresh()` and the Sankey `refresh()`).

**Stage changes must go through `setStage`.** `updateJob` deliberately does not write the stage column. `addJob` records a synthetic `Start -> stage` transition and `setStage` records every move, and that history is the sole input to the Sankey diagram. Bypassing `setStage` silently corrupts the pipeline view. The same applies to the GUI: `JobEditModel::save()` calls `updateJob` and then `setStage` when the stage combo changed (it once skipped the latter, silently dropping stage edits).

**Stage vocabulary.** `JobStage` (`jobstage.h`) is a closed, fixed list (Applied, Screening, Interview, Onsite, Offer, Accepted, Rejected, Withdrawn, Ghosted, plus the synthetic `Start`). It also owns each stage's Sankey column, in-column stacking order, color, and terminal flag. Input is canonicalized case-insensitively on write. Adding a stage touches this file, the migration's canonicalization loop, and the README.

**Sankey layout.** `SankeyModel::reload()` turns `stage_history` into one left-to-right path per job (Start, the funnel stages it reached in increasing column order, then its current stage if terminal), so backward/sideways moves never become ribbons and node values equal the number of jobs that reached them. `relayout()` computes all geometry, including SVG path strings for `QtQuick.Shapes` `PathSvg`; `SankeyDiagram.qml` only draws the `nodes`/`links` lists. Layout is lane-based: links to Rejected/Withdrawn/Ghosted are drop-offs that travel in under-lanes below each column's node; main-line links that skip a column travel in over-lanes above it; each column stack is top-aligned and the whole block is centered. Every ribbon crossing a given gap between columns uses the same x endpoints, so ribbons can only cross if their vertical order differs at the two ends of a gap; the lane/slot orderings are chosen to keep it equal everywhere except the final gap into the outcome nodes. `reload()` re-reads the DB; `relayout()` recomputes geometry from cached counts (used on resize via a debounce timer). `autotests/sankeylayouttest.cpp` checks this on the drawn geometry (it parses `pathData` and asserts ribbons never overlap) and points `KAREER_DB_PATH` at a temp file because `SankeyModel` always opens the default path.

**Edit form.** The add/edit form is data-driven rather than hand-written per field: `JobFieldCatalog` is the static list of categories and fields (id, label, row type, combo options, spin range); `JobEditModel` is a `QAbstractListModel` with one row per field holding the current values, and `ApplicationEditPage.qml` renders it with a `DelegateChooser` on `rowType`. Field ids must match the keys `JobsModel::mapFromJob` / `jobFromMap` use. Adding a form field means: `Job` struct, `JobsDatabase` columns and `jobFromQuery`, `JobsModel` roles and map conversion, `JobFieldCatalog`, and the CLI options.

**Window layout.** `Main.qml` uses a two-column `pageStack`: `ApplicationsPage` (sidebar list) is fixed, and the second column is swapped between `DashboardPage` and `ApplicationEditPage` with `pageStack.replace`. A single `JobsModel` instance is created in `Main.qml` and passed down as a property.

## Conventions

- Every source file starts with an SPDX header (`GPL-3.0-or-later` for code; docs like README/CONTRIBUTING are CC0). Files that cannot carry one are listed in `REUSE.toml`. CI fails on missing headers.
- Qt string style: `using namespace Qt::Literals::StringLiterals;` with `u"..."_s`; user-visible strings go through `i18n`/`i18nc`.
- `main.cpp` installs a message handler that drops a few known-benign Qt/Kirigami warnings; add to `isBenignFrameworkNoise` rather than silencing categories wholesale.
- Releasing: bump `project(... VERSION ...)` in `CMakeLists.txt`, add a `<release>` entry to the metainfo XML, update `pkgver`/checksum in `dist/arch/PKGBUILD`, then push a `v*` tag; `build.yml` builds, signs and publishes the Flatpak. The Flatpak manifest's `sources` block is rewritten by CI to build from the checkout.
- PRs use `.github/PULL_REQUEST_TEMPLATE.md`: reason for change, test plan, screenshots for UI changes.
