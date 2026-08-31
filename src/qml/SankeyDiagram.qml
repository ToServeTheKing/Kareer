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
    readonly property real nodeWidth: Math.round(Kirigami.Units.gridUnit * 0.75)
    readonly property real rowPadding: Kirigami.Units.gridUnit

    SankeyModel {
        id: sankeyModel
    }

    function refresh(): void {
        sankeyModel.reload(root.width, root.height, root.nodeWidth, root.rowPadding);
    }

    // Resizes only need fresh geometry, not a database re-read, and are
    // debounced so a window drag doesn't rebuild every delegate per pixel.
    onWidthChanged: relayoutTimer.restart()
    onHeightChanged: relayoutTimer.restart()
    Component.onCompleted: refresh()

    Timer {
        id: relayoutTimer
        interval: 150
        onTriggered: sankeyModel.relayout(root.width, root.height, root.nodeWidth, root.rowPadding)
    }

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

            // Subtle backing so the label stays legible where ribbons pass
            // underneath it.
            Rectangle {
                anchors.fill: labelHeading
                anchors.margins: -Kirigami.Units.smallSpacing / 2
                radius: 3
                color: Kirigami.Theme.backgroundColor
                opacity: 0.6
            }

            Kirigami.Heading {
                id: labelHeading
                level: 5
                anchors.left: nodeDelegate.modelData.labelOnRight ? parent.right : undefined
                anchors.right: nodeDelegate.modelData.labelOnRight ? undefined : parent.left
                anchors.leftMargin: Kirigami.Units.smallSpacing
                anchors.rightMargin: Kirigami.Units.smallSpacing
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(nodeDelegate.modelData.labelWidth, implicitWidth)
                horizontalAlignment: nodeDelegate.modelData.labelOnRight ? Text.AlignLeft : Text.AlignRight
                text: nodeDelegate.modelData.label
                elide: Text.ElideRight
            }
        }
    }
}
