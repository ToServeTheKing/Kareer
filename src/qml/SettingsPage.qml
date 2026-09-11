/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

// Styled after KDE System Settings' Audio page (plasma-pa's kcm/ui/main.qml):
// Kirigami.ListSectionHeader per section, flat rows indented under the
// header, no card/border around them.
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import io.github.toservetheking.Kareer

Kirigami.ScrollablePage {
    id: root

    title: i18nc("@title:window", "Preferences")

    leftPadding: 0
    rightPadding: 0
    // No topPadding: Kirigami.ListSectionHeader (the first thing on the
    // page) already carries its own top padding, and stacking ours on top
    // of that left an oversized gap above "Appearance".
    bottomPadding: Kirigami.Units.gridUnit

    ColumnLayout {
        width: root.width
        spacing: 0

        Kirigami.ListSectionHeader {
            Layout.fillWidth: true
            text: i18nc("@title:group", "Appearance")
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: i18nc("@label:listbox", "Color scheme")
            }
            QQC2.ComboBox {
                id: colorSchemeCombo
                Layout.fillWidth: true

                model: AppColorScheme.colorSchemesModel
                textRole: "display"

                delegate: QQC2.ItemDelegate {
                    id: schemeDelegate
                    required property var model
                    width: colorSchemeCombo.width

                    icon.source: "image://colorScheme/" + schemeDelegate.model.display
                    icon.color: "transparent"
                    text: schemeDelegate.model.display
                    highlighted: schemeDelegate.model.display === AppColorScheme.activeColorSchemeName

                    onClicked: {
                        AppColorScheme.activeColorSchemeName = schemeDelegate.model.display;
                        colorSchemeCombo.popup.close();
                    }
                }

                // Keep the closed-box label in sync without fighting the popup's own selection state.
                displayText: AppColorScheme.activeColorSchemeName
            }
        }

        Kirigami.ListSectionHeader {
            Layout.fillWidth: true
            text: i18nc("@title:group", "Database")
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: i18nc("@label", "Location")
            }
            QQC2.Label {
                Layout.fillWidth: true
                text: DatabaseLocation.path
                wrapMode: Text.WrapAnywhere
                opacity: 0.7
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Information
                text: i18nc("@info", "Set by the --db option or the KAREER_DB_PATH environment variable, so it can't be changed here.")
                visible: DatabaseLocation.overridden
            }
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Error
                text: DatabaseLocation.lastError
                visible: text.length > 0
            }
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Positive
                text: DatabaseLocation.lastNotice
                visible: text.length > 0
            }

            Flow {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                enabled: !DatabaseLocation.overridden

                QQC2.Button {
                    icon.name: "folder-move"
                    text: i18nc("@action:button", "Move to…")
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Copy the database into another folder and use the copy from now on. The current file is left in place.")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: moveFolderDialog.open()
                }
                QQC2.Button {
                    icon.name: "document-open"
                    text: i18nc("@action:button", "Open Existing…")
                    onClicked: openFileDialog.open()
                }
                QQC2.Button {
                    icon.name: "edit-reset"
                    text: i18nc("@action:button", "Use Default Location")
                    visible: DatabaseLocation.path !== DatabaseLocation.standardPath
                    QQC2.ToolTip.text: xi18nc("@info:tooltip", "Switch to <filename>%1</filename>. Nothing is copied.", DatabaseLocation.standardPath)
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: DatabaseLocation.useDefault()
                }
            }
        }
    }

    Component.onCompleted: DatabaseLocation.clearMessages()

    FolderDialog {
        id: moveFolderDialog
        title: i18nc("@title:window", "Move Database To")
        onAccepted: DatabaseLocation.moveTo(selectedFolder)
    }

    FileDialog {
        id: openFileDialog
        title: i18nc("@title:window", "Open Database")
        fileMode: FileDialog.OpenFile
        nameFilters: [i18nc("@item:inlistbox", "Kareer databases (*.sqlite)"), i18nc("@item:inlistbox", "All files (*)")]
        onAccepted: DatabaseLocation.useFile(selectedFile)
    }
}
