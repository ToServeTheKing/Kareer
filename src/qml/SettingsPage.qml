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
    }
}
