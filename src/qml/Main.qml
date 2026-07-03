// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami
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

    ApplicationEditDialog {
        id: editDialog
        jobsModel: jobsModel
    }

    pageStack.defaultColumnWidth: Kirigami.Units.gridUnit * 22
    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.ToolBar

    // Two static columns (applications + dashboard) - no dynamic page pushing.
    pageStack.initialPage: [applicationsComponent, dashboardComponent]

    Component {
        id: applicationsComponent
        ApplicationsPage {
            jobsModel: jobsModel
            editDialog: editDialog
        }
    }

    Component {
        id: dashboardComponent
        DashboardPage {
            jobsModel: jobsModel
        }
    }
}
