/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import io.github.toservetheking.Kareer

Kirigami.ApplicationWindow {
    id: root

    title: i18nc("@title:window", "Kareer")

    minimumWidth: Kirigami.Units.gridUnit * 32
    minimumHeight: Kirigami.Units.gridUnit * 24
    width: Kirigami.Units.gridUnit * 50
    height: Kirigami.Units.gridUnit * 36

    JobsModel {
        id: jobsModel
    }

    // The job currently open in the edit form, so the sidebar can keep it highlighted.
    property int currentJobId: -1

    pageStack.defaultColumnWidth: Kirigami.Units.gridUnit * 16
    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.ToolBar

    globalDrawer: Kirigami.GlobalDrawer {
        isMenu: true
        actions: [
            Kirigami.Action {
                text: i18nc("@action:inmenu", "Preferences…")
                icon.name: "configure"
                onTriggered: root.pageStack.pushDialogLayer(settingsPageComponent, {
                    width: root.width
                }, {
                    width: Kirigami.Units.gridUnit * 24,
                    height: Kirigami.Units.gridUnit * 20,
                    modality: Qt.NonModal
                })
            },
            Kirigami.Action {
                text: i18nc("@action:inmenu", "About %1", root.title)
                icon.name: "help-about"
                onTriggered: root.pageStack.pushDialogLayer(aboutPageComponent, {
                    width: root.width
                }, {
                    width: Kirigami.Units.gridUnit * 30,
                    height: Kirigami.Units.gridUnit * 30,
                    modality: Qt.NonModal
                })
            }
        ]
    }

    Component {
        id: aboutPageComponent
        FormCard.AboutPage {}
    }

    Component {
        id: settingsPageComponent
        SettingsPage {}
    }

    // Two static columns: the applications list (sidebar) and the dashboard.
    // "Add Application" (and clicking a row) replaces just the dashboard
    // column with the edit form - the sidebar and its list are untouched.
    pageStack.initialPage: [applicationsComponent, dashboardComponent]

    function showDashboard(): void {
        root.currentJobId = -1;
        root.pageStack.currentIndex = 1;
        root.pageStack.replace(dashboardComponent);
    }

    function showEditPage(jobId: int): void {
        root.currentJobId = jobId;
        root.pageStack.currentIndex = 1;
        root.pageStack.replace(editPageComponent, {editingJobId: jobId});
    }

    Component {
        id: applicationsComponent
        ApplicationsPage {
            jobsModel: jobsModel
            currentJobId: root.currentJobId
            onEditRequested: jobId => root.showEditPage(jobId)
        }
    }

    Component {
        id: dashboardComponent
        DashboardPage {
            jobsModel: jobsModel
            onAddRequested: root.showEditPage(-1)
        }
    }

    Component {
        id: editPageComponent
        ApplicationEditPage {
            jobsModel: jobsModel
            onDone: root.showDashboard()
        }
    }
}
