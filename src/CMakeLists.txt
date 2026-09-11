# SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(kareer
    main.cpp
    jobstage.cpp
    jobsdatabase.cpp
    jobfieldcatalog.cpp
    clicommands.cpp
)

qt_add_qml_module(kareer
    URI io.github.toservetheking.Kareer
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        qml/Main.qml
        qml/ApplicationsPage.qml
        qml/ApplicationEditPage.qml
        qml/DashboardPage.qml
        qml/SankeyDiagram.qml
        qml/SettingsPage.qml
        qml/DatabaseSetupDialog.qml
    SOURCES
        jobsmodel.cpp
        jobsmodel.h
        statsmodel.cpp
        statsmodel.h
        sankeymodel.cpp
        sankeymodel.h
        jobeditmodel.cpp
        jobeditmodel.h
        stagehistorymodel.cpp
        stagehistorymodel.h
        appcolorscheme.cpp
        appcolorscheme.h
        databaselocation.cpp
        databaselocation.h
        autoghost.cpp
        autoghost.h
)

# The app icon, embedded so the window and About page show it even when
# running from the build directory; the installed hicolor icon wins when present.
qt_add_resources(kareer "appicon"
    PREFIX "/icons"
    BASE "${CMAKE_SOURCE_DIR}/icons"
    FILES "${CMAKE_SOURCE_DIR}/icons/sc-apps-io.github.toservetheking.Kareer.svg"
)

target_include_directories(kareer PRIVATE ${CMAKE_BINARY_DIR})

target_link_libraries(kareer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Sql
    KF6::ConfigCore
    KF6::I18n
    KF6::I18nQml
    KF6::CoreAddons
    KF6::IconThemes
    KF6::Crash
    KF6::ColorScheme
    KF6::Kirigami
)

install(TARGETS kareer ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
