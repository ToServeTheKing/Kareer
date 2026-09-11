# Kareer

A job application tracker built the KDE way - C++ with a QML/Kirigami
frontend, ECM/CMake, and KDE Frameworks 6. Every application you log is
stored in a local SQLite database, and every stage change is kept as
history so the whole pipeline can be visualized as a Sankey diagram.

Everything the GUI can do is also available from the command line, so
other tools - a resume generator, a job-search script - can log
applications without ever opening a window.

## Features

- Track company, title, location, remote type, source, salary range,
  salary expectation, notes, contact, and the date applied
- A fixed pipeline of stages (Applied, Screening, Interview, Onsite,
  Offer, Accepted, Rejected, Withdrawn, Ghosted) with full history of
  every transition
- A Sankey diagram of the whole pipeline, showing where applications
  progress and where they drop off
- Dashboard summary stats: total applications, active count, offers,
  response rate
- A full CLI (`kareer add|list|show|update|stage|delete|stats|stages`)
  for scripting

## Command line usage

Running `kareer` with no arguments (or an unrecognized first argument)
starts the GUI. A recognized first argument runs headlessly instead -
handy for other tools to call directly.

```sh
# Add an application (stage defaults to Applied, date defaults to today)
kareer add --company "Acme Corp" --title "Senior Software Engineer" \
    --location "Remote" --remote remote --source "LinkedIn" \
    --salary-min 140000 --salary-max 170000 --salary-expectation 160000 \
    --notes "Great team, async-friendly"

# Machine-readable output for scripting (prints the new record, including its id)
kareer add --company "Acme Corp" --title "Senior Software Engineer" --json

# List / filter
kareer list
kareer list --stage Interview
kareer list --company Acme --json

# Show, update, and move through the pipeline
kareer show 1
kareer update 1 --salary-max 175000
kareer stage 1 Interview

# Delete (requires --yes to actually happen)
kareer delete 1 --yes

# Summary stats and the canonical stage list
kareer stats
kareer stages
```

Run `kareer <command> --help` for the full option list of any
subcommand.

### Where the data lives

On first launch the GUI asks where to keep the database: the default
location, a folder you choose (a synced folder, say), or an existing
`kareer.sqlite` you already have, used where it is. You can move or switch
it later under Preferences. The CLI uses the same file.

The database file is picked in this order:

1. `--db <path>` on the command line (GUI or any subcommand; the file is
   created if it doesn't exist): `kareer --db ~/test.sqlite list`
2. The `KAREER_DB_PATH` environment variable
3. The location chosen in the GUI (stored in `kareerrc`)
4. `$XDG_DATA_HOME/kareer/kareer.sqlite` (under Flatpak, that's sandboxed to
   the app's own data directory)

### Wiring up a resume-builder tool

Any script that generates or sends out a resume can log the
application in the same step:

```sh
kareer add --company "$COMPANY" --title "$TITLE" --url "$POSTING_URL" \
    --source "resume-builder" --json
```

## Install

Tagged releases are built by CI into a single-file bundle (attached to
each [GitHub Release]) and a hosted Flatpak repository on GitHub Pages:

```sh
flatpak remote-add --if-not-exists --user kareer \
  https://toservetheking.github.io/Kareer/kareer.flatpakrepo
flatpak install --user kareer io.github.toservetheking.Kareer
```

The repository and bundle are GPG-signed; the public key is embedded in the
`.flatpakrepo` (and available at [`keys/kareer.asc`](keys/kareer.asc)).

Or install the downloaded bundle directly:

```sh
flatpak install --user ./io.github.toservetheking.Kareer.flatpak
```

[GitHub Release]: https://github.com/toservetheking/Kareer/releases

On Arch, a `PKGBUILD` is maintained in [`dist/arch`](dist/arch/PKGBUILD)
(pending AUR publication - the AUR is not accepting new accounts right
now):

```sh
cd dist/arch && makepkg -si
```

## Building

Requires Qt 6, KDE Frameworks 6, Kirigami, Kirigami Addons and the
CMake toolchain. On Arch/CachyOS:

```sh
sudo pacman -S --needed cmake ninja extra-cmake-modules base-devel \
    qt6-base qt6-declarative vulkan-headers kirigami kirigami-addons \
    ki18n kconfig kcoreaddons kiconthemes kcrash kitemmodels \
    kcolorscheme qqc2-desktop-style
```

On Fedora 44:

```sh
sudo dnf install cmake ninja-build extra-cmake-modules gcc-c++ \
    qt6-qtbase-devel qt6-qtbase-private-devel \
    qt6-qtdeclarative-devel qt6-qtquickcontrols2-devel \
    vulkan-headers kf6-kirigami-devel \
    kf6-kirigami-addons-devel kf6-ki18n-devel kf6-kcoreaddons-devel \
    kf6-kconfig-devel \
    kf6-kiconthemes-devel kf6-kcrash-devel kf6-kitemmodels-devel \
    kf6-kcolorscheme-devel qqc2-desktop-style
```

Then:

```sh
cmake -B build
cmake --build build
./build/bin/kareer
```

(If you have `ninja` installed, add `-G Ninja` to the configure step or
use the `ninja-dev` preset: `cmake --preset ninja-dev`.)

Run the tests with `ctest --test-dir build`.

## Architecture

- `job.h` - the `Job` and `StageTransition` plain data structs.
- `jobstage.{h,cpp}` - the fixed stage vocabulary: ordering, Sankey
  column/stacking position, and color.
- `jobsdatabase.{h,cpp}` - SQLite storage (via QtSql) for applications
  and stage history; the only class that touches the database.
- `jobsmodel.{h,cpp}` - `QAbstractListModel` wrapper over
  `JobsDatabase` for the QML application list and edit dialog.
- `statsmodel.{h,cpp}` - summary counters for the dashboard.
- `sankeymodel.{h,cpp}` - turns stage history into laid-out Sankey
  geometry (node columns/stacking, ribbon SVG path data); QML only
  draws what this hands back.
- `jobfieldcatalog.{h,cpp}` - the static catalog of edit-form fields and categories.
- `jobeditmodel.{h,cpp}` - `QAbstractListModel`-backed edit-form state, built from the field catalog.
- `clicommands.{h,cpp}` - the `add`/`list`/`show`/`update`/`stage`/
  `delete`/`stats`/`stages` subcommands.
- `appcolorscheme.{h,cpp}` - QML-facing wrapper around `KColorSchemeManager` for the Preferences page.
- `qml/` - Kirigami UI: `ApplicationsPage` (list + search),
  `ApplicationEditPage` (add/edit/delete form), `DashboardPage`
  (stat cards + pipeline), `SankeyDiagram` (the renderer),
  `SettingsPage` (the Preferences page).
