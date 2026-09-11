# SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>
#
# SPDX-License-Identifier: GPL-3.0-or-later

# Named after the app ID, as the desktop file, the window icon and Flatpak
# (which only exports icons prefixed with the app ID) all expect.
# Image loaders (gdk-pixbuf, and so AppStream compose in the Flatpak build)
# recognise an SVG by finding "<svg" in its first 256 bytes; past that the
# file is "Unrecognized image file format" and the Flatpak build fails.
file(READ sc-apps-io.github.toservetheking.Kareer.svg _kareer_svg_head LIMIT 256)
if(NOT _kareer_svg_head MATCHES "<svg")
    message(FATAL_ERROR "icons/sc-apps-io.github.toservetheking.Kareer.svg: the <svg> tag must start within the first 256 bytes; move comments inside the element.")
endif()

ecm_install_icons(ICONS
    sc-apps-io.github.toservetheking.Kareer.svg
    128-apps-io.github.toservetheking.Kareer.png
    64-apps-io.github.toservetheking.Kareer.png
    48-apps-io.github.toservetheking.Kareer.png
    44-apps-io.github.toservetheking.Kareer.png
    32-apps-io.github.toservetheking.Kareer.png
    22-apps-io.github.toservetheking.Kareer.png
    16-apps-io.github.toservetheking.Kareer.png
    DESTINATION ${KDE_INSTALL_ICONDIR}
    THEME hicolor
)
