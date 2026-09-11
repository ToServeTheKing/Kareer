/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

// Styled after KDE System Settings' Audio page (plasma-pa's kcm/ui/main.qml):
// Kirigami.ListSectionHeader per category, flat rows indented under the
// header and separated by Kirigami.Separator, no card/border around them.
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import Qt.labs.qmlmodels
import org.kde.kirigami as Kirigami
import org.kde.kitemmodels as KItemModels
import org.kde.kirigamiaddons.dateandtime as DateTime
import io.github.toservetheking.Kareer

Kirigami.ScrollablePage {
    id: root

    required property JobsModel jobsModel
    property int editingJobId: -1

    title: root.editingJobId < 0 ? i18nc("@title", "Add Application") : i18nc("@title", "Edit Application")

    signal done()

    leftPadding: 0
    rightPadding: 0
    // No topPadding: Kirigami.ListSectionHeader (the first thing on the
    // page) already carries its own top padding, and stacking ours on top
    // of that left an oversized gap above "Company", the first category.
    bottomPadding: Kirigami.Units.gridUnit

    JobEditModel {
        id: editModel
        jobsModel: root.jobsModel
        editingJobId: root.editingJobId
    }

    actions: [
        Kirigami.Action {
            text: i18nc("@action:button", "Cancel")
            icon.name: "dialog-cancel"
            onTriggered: root.done()
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Delete")
            icon.name: "edit-delete"
            visible: root.editingJobId >= 0
            onTriggered: deleteConfirmDialog.open()
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Save")
            icon.name: "document-save"
            // On failure, editModel.lastError is set and shown by errorLabel.
            onTriggered: {
                if (editModel.save()) {
                    root.done();
                }
            }
        }
    ]

    ColumnLayout {
        spacing: 0

        Kirigami.InlineMessage {
            id: errorLabel
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            type: Kirigami.MessageType.Error
            text: editModel.lastError
            visible: text.length > 0
        }

        Repeater {
            model: editModel.categories

            delegate: ColumnLayout {
                id: section

                required property var modelData

                Layout.fillWidth: true
                spacing: 0

                Kirigami.ListSectionHeader {
                    Layout.fillWidth: true
                    text: section.modelData.title
                }

                KItemModels.KSortFilterProxyModel {
                    id: catModel
                    sourceModel: editModel
                    filterRoleName: "categoryId"
                    // No category id is a prefix of another, so this is an exact match.
                    filterString: section.modelData.id
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    Layout.bottomMargin: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.largeSpacing

                    Repeater {
                        id: rowRepeater
                        model: catModel

                        delegate: DelegateChooser {
                            role: "rowType"

                            // Text
                            DelegateChoice {
                                roleValue: 0
                                delegate: ColumnLayout {
                                    id: textD
                                    required property int index
                                    required property string label
                                    required property string value
                                    required property string placeholder
                                    readonly property int sourceRow: catModel.mapToSource(catModel.index(index, 0)).row
                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.smallSpacing

                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        text: textD.label
                                    }
                                    QQC2.TextField {
                                        Layout.fillWidth: true
                                        text: textD.value
                                        placeholderText: textD.placeholder
                                        onTextEdited: editModel.setValue(textD.sourceRow, text)
                                    }

                                    Kirigami.Separator {
                                        Layout.fillWidth: true
                                        Layout.topMargin: Kirigami.Units.smallSpacing
                                        visible: textD.index !== rowRepeater.count - 1
                                    }
                                }
                            }

                            // Combo box
                            DelegateChoice {
                                roleValue: 1
                                delegate: ColumnLayout {
                                    id: comboD
                                    required property int index
                                    required property string label
                                    required property string value
                                    required property var comboOptions
                                    readonly property int sourceRow: catModel.mapToSource(catModel.index(index, 0)).row
                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.smallSpacing

                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        text: comboD.label
                                    }
                                    QQC2.ComboBox {
                                        Layout.fillWidth: true
                                        model: comboD.comboOptions
                                        currentIndex: comboD.comboOptions.indexOf(comboD.value)
                                        onActivated: editModel.setValue(comboD.sourceRow, currentText)
                                    }

                                    Kirigami.Separator {
                                        Layout.fillWidth: true
                                        Layout.topMargin: Kirigami.Units.smallSpacing
                                        visible: comboD.index !== rowRepeater.count - 1
                                    }
                                }
                            }

                            // Spin box
                            DelegateChoice {
                                roleValue: 2
                                delegate: ColumnLayout {
                                    id: spinD
                                    required property int index
                                    required property string label
                                    required property var model
                                    readonly property int sourceRow: catModel.mapToSource(catModel.index(index, 0)).row
                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.smallSpacing

                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        text: spinD.label
                                    }
                                    QQC2.SpinBox {
                                        id: spinBox
                                        Layout.fillWidth: true
                                        from: spinD.model.spinMin
                                        to: spinD.model.spinMax
                                        stepSize: 1000
                                        // Live binding so values loaded after the delegate is
                                        // created still show up. Writing back only on
                                        // valueModified (user edits) avoids a binding loop.
                                        value: spinD.model.value
                                        onValueModified: editModel.setValue(spinD.sourceRow, value)
                                    }

                                    Kirigami.Separator {
                                        Layout.fillWidth: true
                                        Layout.topMargin: Kirigami.Units.smallSpacing
                                        visible: spinD.index !== rowRepeater.count - 1
                                    }
                                }
                            }

                            // Date
                            DelegateChoice {
                                roleValue: 3
                                delegate: ColumnLayout {
                                    id: dateD
                                    required property int index
                                    required property string label
                                    required property var value
                                    readonly property int sourceRow: catModel.mapToSource(catModel.index(index, 0)).row
                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.smallSpacing

                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        text: dateD.label
                                    }
                                    QQC2.Button {
                                        Layout.fillWidth: true
                                        text: Qt.formatDate(dateD.value, Qt.ISODate)
                                        icon.name: "view-calendar-day"
                                        onClicked: {
                                            datePopup.value = dateD.value;
                                            datePopup.open();
                                        }

                                        DateTime.DatePopup {
                                            id: datePopup
                                            onAccepted: editModel.setValue(dateD.sourceRow, value)
                                        }
                                    }

                                    Kirigami.Separator {
                                        Layout.fillWidth: true
                                        Layout.topMargin: Kirigami.Units.smallSpacing
                                        visible: dateD.index !== rowRepeater.count - 1
                                    }
                                }
                            }

                            // Text area
                            DelegateChoice {
                                roleValue: 4
                                delegate: ColumnLayout {
                                    id: areaD
                                    required property int index
                                    required property string label
                                    required property string value
                                    readonly property int sourceRow: catModel.mapToSource(catModel.index(index, 0)).row
                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.smallSpacing

                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        text: areaD.label
                                    }
                                    QQC2.TextArea {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: Kirigami.Units.gridUnit * 5
                                        wrapMode: TextEdit.Wrap
                                        text: areaD.value
                                        // TextArea has no textEdited signal; skip the echo from
                                        // the model-driven binding and only write real edits.
                                        onTextChanged: {
                                            if (text !== areaD.value) {
                                                editModel.setValue(areaD.sourceRow, text);
                                            }
                                        }
                                    }

                                    Kirigami.Separator {
                                        Layout.fillWidth: true
                                        Layout.topMargin: Kirigami.Units.smallSpacing
                                        visible: areaD.index !== rowRepeater.count - 1
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Kirigami.PromptDialog {
        id: deleteConfirmDialog
        title: i18nc("@title", "Delete Application")
        subtitle: i18nc("@info", "Are you sure you want to delete this application? This cannot be undone.")
        standardButtons: QQC2.Dialog.Cancel

        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    editModel.deleteJob();
                    deleteConfirmDialog.close();
                    root.done();
                }
            }
        ]
    }
}
