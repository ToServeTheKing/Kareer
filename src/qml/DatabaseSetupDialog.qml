/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

// First-run choice of where the database lives. Shown by Main.qml while
// DatabaseLocation.setupPending is true; it cannot be dismissed until a
// location has been picked, since nothing can be saved before then.
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import io.github.toservetheking.Kareer

Kirigami.Dialog {
    id: root

    title: i18nc("@title:window", "Welcome to Kareer")
    showCloseButton: false
    closePolicy: QQC2.Popup.NoAutoClose
    modal: true
    preferredWidth: Kirigami.Units.gridUnit * 26
    standardButtons: Kirigami.Dialog.NoButton

    onOpened: DatabaseLocation.clearMessages()

    ColumnLayout {
        spacing: 0

        QQC2.Label {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            wrapMode: Text.Wrap
            text: DatabaseLocation.missingPath.length > 0
                ? xi18nc("@info", "The database at <filename>%1</filename> could not be found. Choose where Kareer should keep your applications.", DatabaseLocation.missingPath)
                : i18nc("@info", "Choose where Kareer should keep your applications.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            type: Kirigami.MessageType.Error
            text: DatabaseLocation.lastError
            visible: text.length > 0
        }

        FormCard.FormButtonDelegate {
            Layout.fillWidth: true
            icon.name: "document-new"
            text: i18nc("@action:button", "Create in the default location")
            description: DatabaseLocation.standardPath
            onClicked: {
                if (DatabaseLocation.useDefault()) {
                    root.close();
                }
            }
        }

        FormCard.FormButtonDelegate {
            Layout.fillWidth: true
            icon.name: "folder-new"
            text: i18nc("@action:button", "Choose a folder…")
            description: i18nc("@info", "Create a new database in a folder of your choice, for example a synced folder.")
            onClicked: folderDialog.open()
        }

        FormCard.FormButtonDelegate {
            Layout.fillWidth: true
            icon.name: "document-open"
            text: i18nc("@action:button", "Open an existing database…")
            description: i18nc("@info", "Use a kareer.sqlite file you already have, where it is.")
            onClicked: fileDialog.open()
        }
    }

    FolderDialog {
        id: folderDialog
        title: i18nc("@title:window", "Choose a Folder for the Database")
        onAccepted: {
            if (DatabaseLocation.createIn(selectedFolder)) {
                root.close();
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: i18nc("@title:window", "Open Database")
        fileMode: FileDialog.OpenFile
        nameFilters: [i18nc("@item:inlistbox", "Kareer databases (*.sqlite)"), i18nc("@item:inlistbox", "All files (*)")]
        onAccepted: {
            if (DatabaseLocation.useFile(selectedFile)) {
                root.close();
            }
        }
    }
}
