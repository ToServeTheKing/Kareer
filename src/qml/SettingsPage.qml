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

        Kirigami.ListSectionHeader {
            Layout.fillWidth: true
            text: i18nc("@title:group", "Sync")
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
                text: i18nc("@label:textbox", "Server")
            }
            QQC2.TextField {
                Layout.fillWidth: true
                text: Sync.serverUrl
                placeholderText: i18nc("@info:placeholder", "https://kareer.example.com")
                enabled: !Sync.signedIn && !Sync.busy
                inputMethodHints: Qt.ImhUrlCharactersOnly
                onEditingFinished: Sync.serverUrl = text
            }

            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.7
                text: {
                    if (Sync.busy) {
                        return i18nc("@info", "Syncing…");
                    }
                    if (Sync.serverUrl.length === 0) {
                        return i18nc("@info", "Sync needs a Kareer server you run yourself. Enter its address to keep your applications the same on every computer you use Kareer on.");
                    }
                    if (!Sync.signedIn) {
                        return i18nc("@info", "Sign in to keep your applications the same on every computer you use Kareer on.");
                    }
                    if (isNaN(Sync.lastSynced.getTime())) {
                        return i18nc("@info", "Signed in. Not synced yet.");
                    }
                    return i18nc("@info %1 is a date and time", "Signed in. Last synced %1.", Qt.formatDateTime(Sync.lastSynced, Locale.ShortFormat));
                }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Error
                text: Sync.lastError
                visible: text.length > 0
            }

            Flow {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                enabled: !Sync.busy

                QQC2.Button {
                    icon.name: "im-user-online"
                    text: i18nc("@action:button", "Sign In…")
                    visible: !Sync.signedIn
                    enabled: Sync.serverUrl.length > 0
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Opens your browser. Applications already on this computer are uploaded and merged with the server's.")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: Sync.signIn()
                }
                QQC2.Button {
                    icon.name: "view-refresh"
                    text: i18nc("@action:button", "Sync Now")
                    visible: Sync.signedIn
                    onClicked: Sync.syncNow()
                }
                QQC2.Button {
                    icon.name: "system-log-out"
                    text: i18nc("@action:button", "Sign Out")
                    visible: Sync.signedIn
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Stops syncing. Your applications stay on this computer.")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: Sync.signOut()
                }
            }
        }

        Kirigami.ListSectionHeader {
            Layout.fillWidth: true
            text: i18nc("@title:group", "Applications")
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            QQC2.Switch {
                id: autoGhostSwitch
                Layout.fillWidth: true
                text: i18nc("@option:check", "Mark applications with no response as Ghosted")
                checked: AutoGhost.enabled
                onToggled: AutoGhost.enabled = checked
            }

            RowLayout {
                Layout.fillWidth: true
                enabled: autoGhostSwitch.checked
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    text: i18nc("@label:spinbox Mark as ghosted after [N] days", "After")
                }
                QQC2.SpinBox {
                    from: 1
                    to: 365
                    value: AutoGhost.days
                    onValueModified: AutoGhost.days = value
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18nc("@label:spinbox Mark as ghosted after [N] days", "days without any activity")
                    wrapMode: Text.Wrap
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: i18nc("@info", "Only applications still at Applied are affected. The move is recorded in their history, and you can change the stage back at any time.")
                wrapMode: Text.Wrap
                font: Kirigami.Theme.smallFont
                opacity: 0.7
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
