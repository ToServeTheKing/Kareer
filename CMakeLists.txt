# SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>
#
# SPDX-License-Identifier: GPL-3.0-or-later
# Kareer - a Kirigami/Qt job application tracker

cmake_minimum_required(VERSION 3.16)

project(kareer VERSION 0.1.4 LANGUAGES CXX)

set(REQUIRED_QT_VERSION 6.6.0)
set(REQUIRED_KF_VERSION 6.5.0)

find_package(ECM ${REQUIRED_KF_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMSetupVersion)
include(ECMInstallIcons)
include(FeatureSummary)
include(KDEClangFormat)
include(KDEGitCommitHooks)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 ${REQUIRED_QT_VERSION} REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Qml
    Quick
    # QtQuick.Shapes runtime QML module (SankeyDiagram.qml); configure-time
    # guard only, nothing links against it.
    QuickShapesPrivate
    # QtQuick.Dialogs runtime QML module (DatabaseSetupDialog.qml,
    # SettingsPage.qml); configure-time guard only, nothing links against it.
    QuickDialogs2
    QuickControls2
    Sql
    Test
)

find_package(KF6 ${REQUIRED_KF_VERSION} REQUIRED COMPONENTS
    Config
    I18n
    CoreAddons
    IconThemes
    Crash
    ColorScheme
)

find_package(KF6Kirigami ${REQUIRED_KF_VERSION} REQUIRED)
set_package_properties(KF6Kirigami PROPERTIES
    TYPE REQUIRED
    DESCRIPTION "KDE's next-gen UI framework"
)

find_package(KF6KirigamiAddons REQUIRED)
set_package_properties(KF6KirigamiAddons PROPERTIES
    TYPE REQUIRED
    DESCRIPTION "Add-on components for Kirigami"
)

ecm_setup_version(${PROJECT_VERSION}
    VARIABLE_PREFIX KAREER
    VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/kareer-version.h"
)

add_subdirectory(icons)
add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(autotests)
endif()

install(PROGRAMS io.github.toservetheking.Kareer.desktop
        DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES io.github.toservetheking.Kareer.metainfo.xml
        DESTINATION ${KDE_INSTALL_METAINFODIR})
install(FILES LICENSES/GPL-3.0-or-later.txt
        DESTINATION ${KDE_INSTALL_DATAROOTDIR}/licenses/${PROJECT_NAME})

# Translations: add a po/ directory and re-enable ki18n_install(po) once present.

kde_configure_git_pre_commit_hook(CHECKS CLANG-FORMAT)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
