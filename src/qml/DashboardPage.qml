// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import io.github.toservetheking.Kareer

Kirigami.ScrollablePage {
    id: root

    required property JobsModel jobsModel

    title: i18nc("@title", "Dashboard")

    StatsModel {
        id: statsModel
    }

    Connections {
        target: root.jobsModel
        function onCountChanged() {
            statsModel.refresh();
            sankeyDiagram.refresh();
        }
    }

    Component.onCompleted: statsModel.refresh()

    ColumnLayout {
        width: root.width
        spacing: Kirigami.Units.largeSpacing

        GridLayout {
            Layout.fillWidth: true
            columns: root.width > Kirigami.Units.gridUnit * 30 ? 4 : 2
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing

            Repeater {
                model: [
                    {label: i18n("Total Applications"), value: String(statsModel.totalApplications)},
                    {label: i18n("Active"), value: String(statsModel.activeApplications)},
                    {label: i18n("Offers"), value: String(statsModel.offerCount)},
                    {label: i18n("Response Rate"), value: Math.round(statsModel.responseRate) + "%"},
                ]
                delegate: Kirigami.AbstractCard {
                    id: statCard
                    required property var modelData
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        spacing: Kirigami.Units.smallSpacing
                        Kirigami.Heading {
                            level: 2
                            text: statCard.modelData.value
                        }
                        QQC2.Label {
                            text: statCard.modelData.label
                            opacity: 0.7
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }

        Kirigami.Heading {
            level: 3
            text: i18nc("@title:group", "Pipeline")
        }

        SankeyDiagram {
            id: sankeyDiagram
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 20
        }
    }
}
