/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

// Sidebar/detail two-column layout: a search-filtered list of applications
// with a RoundedItemDelegate + SubtitleContentItem delegate, where the
// currently-open entry stays highlighted.
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kitemmodels as KItemModels
import org.kde.kirigamiaddons.delegates as Delegates
import io.github.toservetheking.Kareer

Kirigami.ScrollablePage {
    id: root

    required property JobsModel jobsModel
    property int currentJobId: -1

    signal editRequested(int jobId)

    title: i18nc("@title", "Applications")

    titleDelegate: Kirigami.SearchField {
        Layout.fillWidth: true
        onTextChanged: filteredJobs.filterString = text
    }

    KItemModels.KSortFilterProxyModel {
        id: filteredJobs
        sourceModel: root.jobsModel
        filterRoleName: "company"
        filterCaseSensitivity: Qt.CaseInsensitive
    }

    ListView {
        id: jobList
        model: filteredJobs
        currentIndex: -1

        delegate: Delegates.RoundedItemDelegate {
            id: jobDelegate

            required property int index
            required property int jobId
            required property string company
            required property string title
            required property string stage

            text: jobDelegate.company
            icon.source: "network-workgroup-symbolic"
            highlighted: root.currentJobId === jobDelegate.jobId

            contentItem: Delegates.SubtitleContentItem {
                itemDelegate: jobDelegate
                subtitle: jobDelegate.title + " · " + jobDelegate.stage
            }

            onClicked: root.editRequested(jobDelegate.jobId)
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: jobList.count === 0
            icon.name: "office-address-book-symbolic"
            text: i18n("No applications yet")
            explanation: i18n("Use the Add Application button on the dashboard to log your first one.")
        }
    }
}
