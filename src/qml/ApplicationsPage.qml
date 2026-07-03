// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kitemmodels as KItemModels
import org.kde.kirigamiaddons.delegates as Delegates
import io.github.toservetheking.Kareer

Kirigami.ScrollablePage {
    id: root

    required property JobsModel jobsModel
    required property var editDialog

    title: i18nc("@title", "Applications")

    titleDelegate: Kirigami.SearchField {
        Layout.fillWidth: true
        onTextChanged: filteredJobs.filterString = text
    }

    actions: [
        Kirigami.Action {
            text: i18nc("@action:button", "Add Application")
            icon.name: "list-add"
            onTriggered: root.editDialog.openForAdd()
        }
    ]

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
            required property var dateApplied
            required property int salaryMin
            required property int salaryMax
            required property string currency

            text: jobDelegate.company

            contentItem: Delegates.SubtitleContentItem {
                itemDelegate: jobDelegate
                subtitle: {
                    const parts = [jobDelegate.title, jobDelegate.stage];
                    if (jobDelegate.dateApplied) {
                        parts.push(Qt.formatDate(jobDelegate.dateApplied, "yyyy-MM-dd"));
                    }
                    if (jobDelegate.salaryMin >= 0 || jobDelegate.salaryMax >= 0) {
                        let salary = jobDelegate.currency + " ";
                        salary += jobDelegate.salaryMin >= 0 ? jobDelegate.salaryMin : "?";
                        salary += "–";
                        salary += jobDelegate.salaryMax >= 0 ? jobDelegate.salaryMax : "?";
                        parts.push(salary);
                    }
                    return parts.join(" · ");
                }
            }

            onClicked: root.editDialog.openForEdit(jobDelegate.jobId)
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: jobList.count === 0
            icon.name: "office-address-book-symbolic"
            text: i18n("No applications yet")
            explanation: i18n("Use the Add Application button to log your first one.")
        }
    }
}
