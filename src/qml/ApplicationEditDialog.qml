// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import io.github.toservetheking.Kareer

FormCard.FormCardDialog {
    id: root

    required property JobsModel jobsModel
    property int editingJobId: -1

    title: editingJobId < 0 ? i18nc("@title:dialog", "Add Application") : i18nc("@title:dialog", "Edit Application")
    standardButtons: QQC2.Dialog.Save | QQC2.Dialog.Cancel

    function openForAdd(): void {
        editingJobId = -1;
        companyField.text = "";
        titleField.text = "";
        locationField.text = "";
        remoteCombo.currentIndex = 0;
        sourceField.text = "";
        urlField.text = "";
        dateField.value = new Date();
        salaryMinField.value = 0;
        salaryMaxField.value = 0;
        salaryExpectationField.value = 0;
        currencyField.text = "USD";
        contactField.text = "";
        notesField.text = "";
        const stages = root.jobsModel.stages;
        stageCombo.currentIndex = stages.indexOf("Applied");
        errorLabel.text = "";
        root.open();
    }

    function openForEdit(id: int): void {
        editingJobId = id;
        const data = root.jobsModel.jobData(id);
        companyField.text = data.company;
        titleField.text = data.title;
        locationField.text = data.location;
        remoteCombo.currentIndex = Math.max(0, remoteCombo.model.indexOf(data.remoteType));
        sourceField.text = data.source;
        urlField.text = data.url;
        dateField.value = data.dateApplied;
        salaryMinField.value = data.salaryMin > 0 ? data.salaryMin : 0;
        salaryMaxField.value = data.salaryMax > 0 ? data.salaryMax : 0;
        salaryExpectationField.value = data.salaryExpectation > 0 ? data.salaryExpectation : 0;
        currencyField.text = data.currency;
        contactField.text = data.contact;
        notesField.text = data.notes;
        const stages = root.jobsModel.stages;
        stageCombo.currentIndex = Math.max(0, stages.indexOf(data.stage));
        errorLabel.text = "";
        root.open();
    }

    onAccepted: {
        const fields = {
            company: companyField.text,
            title: titleField.text,
            location: locationField.text,
            remoteType: remoteCombo.currentIndex === 0 ? "" : remoteCombo.currentText,
            source: sourceField.text,
            url: urlField.text,
            dateApplied: dateField.value,
            salaryMin: salaryMinField.value > 0 ? salaryMinField.value : -1,
            salaryMax: salaryMaxField.value > 0 ? salaryMaxField.value : -1,
            salaryExpectation: salaryExpectationField.value > 0 ? salaryExpectationField.value : -1,
            currency: currencyField.text,
            contact: contactField.text,
            notes: notesField.text,
            stage: stageCombo.currentText,
        };

        const ok = root.editingJobId < 0 ? root.jobsModel.addJob(fields) : root.jobsModel.updateJob(root.editingJobId, fields);
        if (!ok) {
            errorLabel.text = root.jobsModel.lastError();
            root.open();
        }
    }

    FormCard.FormCard {
        FormCard.FormTextFieldDelegate {
            id: companyField
            label: i18nc("@label:textbox", "Company")
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextFieldDelegate {
            id: titleField
            label: i18nc("@label:textbox", "Job Title")
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextFieldDelegate {
            id: locationField
            label: i18nc("@label:textbox", "Location")
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormComboBoxDelegate {
            id: remoteCombo
            text: i18nc("@label:listbox", "Remote Type")
            model: ["Unspecified", "Onsite", "Hybrid", "Remote"]
        }
    }

    FormCard.FormCard {
        FormCard.FormComboBoxDelegate {
            id: stageCombo
            text: i18nc("@label:listbox", "Stage")
            model: root.jobsModel.stages
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormHeader {
            title: i18nc("@title:group", "Date Applied")
        }
        FormCard.FormDateTimeDelegate {
            id: dateField
            dateTimeDisplay: FormCard.FormDateTimeDelegate.DateTimeDisplay.Date
        }
    }

    FormCard.FormCard {
        FormCard.FormSpinBoxDelegate {
            id: salaryMinField
            label: i18nc("@label:spinbox", "Salary Range Minimum")
            from: 0
            to: 5000000
            stepSize: 1000
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormSpinBoxDelegate {
            id: salaryMaxField
            label: i18nc("@label:spinbox", "Salary Range Maximum")
            from: 0
            to: 5000000
            stepSize: 1000
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormSpinBoxDelegate {
            id: salaryExpectationField
            label: i18nc("@label:spinbox", "Your Salary Expectation")
            from: 0
            to: 5000000
            stepSize: 1000
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextFieldDelegate {
            id: currencyField
            label: i18nc("@label:textbox", "Currency")
        }
    }

    FormCard.FormCard {
        FormCard.FormTextFieldDelegate {
            id: sourceField
            label: i18nc("@label:textbox", "Source")
            placeholderText: i18nc("@info:placeholder", "Referral, LinkedIn, company site...")
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextFieldDelegate {
            id: urlField
            label: i18nc("@label:textbox", "Job Posting URL")
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextFieldDelegate {
            id: contactField
            label: i18nc("@label:textbox", "Contact")
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextAreaDelegate {
            id: notesField
            label: i18nc("@label:textbox", "Notes / Expectations")
        }
    }

    FormCard.FormCard {
        visible: root.editingJobId >= 0
        FormCard.FormButtonDelegate {
            text: i18nc("@action:button", "Delete Application")
            icon.name: "edit-delete"
            onClicked: deleteConfirmDialog.open()
        }
    }

    Kirigami.InlineMessage {
        id: errorLabel
        Layout.fillWidth: true
        Layout.margins: Kirigami.Units.smallSpacing
        type: Kirigami.MessageType.Error
        visible: text.length > 0
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
                    root.jobsModel.removeJob(root.editingJobId);
                    deleteConfirmDialog.close();
                    root.close();
                }
            }
        ]
    }
}
