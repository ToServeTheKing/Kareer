/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import io.github.toservetheking.Kareer

// All the layout math (columns, stacking, ribbon paths) lives in SankeyModel;
// this component only draws the rectangles and PathSvg shapes it hands back.
Item {
    id: root

    readonly property bool empty: sankeyModel.empty

    SankeyModel {
        id: sankeyModel
    }

    function refresh(): void {
        if (root.width > 0 && root.height > 0) {
            sankeyModel.relayout(root.width, root.height);
        }
    }

    onWidthChanged: refresh()
    onHeightChanged: refresh()
    Component.onCompleted: refresh()

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        visible: root.empty
        icon.name: "office-chart-line-symbolic"
        text: i18n("No applications yet")
        explanation: i18n("Once you add applications, their pipeline will appear here.")
    }

    Repeater {
        model: sankeyModel.links
        delegate: Shape {
            required property var modelData
            asynchronous: true
            ShapePath {
                fillColor: modelData.color
                strokeColor: "transparent"
                PathSvg {
                    path: modelData.pathData
                }
            }
        }
    }

    Repeater {
        model: sankeyModel.nodes
        delegate: Rectangle {
            id: nodeDelegate
            required property var modelData

            x: modelData.x
            y: modelData.y
            width: modelData.width
            height: modelData.height
            radius: 2
            color: modelData.color

            QQC2.ToolTip.visible: nodeMouse.containsMouse
            QQC2.ToolTip.text: `${modelData.label} (${modelData.value})`

            MouseArea {
                id: nodeMouse
                anchors.fill: parent
                hoverEnabled: true
            }

            Kirigami.Heading {
                level: 5
                anchors.left: parent.right
                anchors.leftMargin: Kirigami.Units.smallSpacing
                anchors.verticalCenter: parent.verticalCenter
                width: nodeDelegate.modelData.labelWidth
                text: nodeDelegate.modelData.label
                visible: nodeDelegate.height >= Kirigami.Units.gridUnit
                elide: Text.ElideRight
            }
        }
    }
}
