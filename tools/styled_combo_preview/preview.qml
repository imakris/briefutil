import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../../briefutil/app/qml" as Briefutil

ApplicationWindow {
    id: window

    width: 660
    height: 560
    minimumWidth: 400
    minimumHeight: 520
    visible: true
    title: "Briefutil styled combo preview"
    property bool darkMode: false
    property var senders: ["Northfield Consulting", "Operations"]

    palette.window: darkMode ? "#2d2d2d" : "#eeeeee"
    palette.windowText: darkMode ? "white" : "black"
    palette.base: darkMode ? "#1e1e1e" : "white"
    palette.text: darkMode ? "white" : "black"
    palette.button: darkMode ? "#3d3d3d" : "#e0e0e0"
    palette.buttonText: darkMode ? "white" : "black"

    Shortcut {
        sequence: "Ctrl+T"
        onActivated: window.darkMode = !window.darkMode
    }

    component SampleCombo: Briefutil.StyledComboBox {
        Layout.fillWidth: true
        Layout.preferredHeight: 28
        darkMode: window.darkMode
        fieldBg: window.palette.base
        textColor: window.palette.text
    }

    ListModel {
        id: presets
        ListElement { text: "DIN 5008 Form B"; value: "din_5008_form_b" }
        ListElement { text: "DIN 5008 Form A"; value: "din_5008_form_a" }
        ListElement { text: "US Letter"; value: "us_letter" }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        Label {
            Layout.fillWidth: true
            text: "Actual shared StyledComboBox"
            font.pixelSize: 22
        }

        RowLayout {
            CheckBox {
                text: "Dark theme (Ctrl+T)"
                checked: window.darkMode
                onToggled: window.darkMode = checked
            }
            Button {
                text: window.senders.length ? "Clear sender model" : "Restore sender model"
                onClicked: window.senders = window.senders.length
                    ? [] : ["Northfield Consulting", "Operations"]
            }
        }

        Label { text: "Sender: string-list model, 28-pixel row" }
        SampleCombo {
            id: sender
            model: window.senders
        }

        Label { text: "Page preset: ListModel with textRole" }
        SampleCombo {
            id: preset
            model: presets
            textRole: "text"
        }

        Label { text: "Profile controls: height follows the adjacent text field" }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: nameField
                Layout.fillWidth: true
                text: "Northfield Consulting"
            }
            SampleCombo {
                id: profile
                Layout.preferredHeight: nameField.implicitHeight
                backgroundImplicitHeight: 0
                model: ["Office", "Personal"]
            }
        }
        RowLayout {
            Layout.fillWidth: true
            SampleCombo {
                id: style
                Layout.preferredHeight: nameField.implicitHeight
                backgroundImplicitHeight: 0
                model: ["simple", "commercial"]
            }
            SampleCombo {
                id: language
                Layout.preferredHeight: nameField.implicitHeight
                backgroundImplicitHeight: 0
                model: ["English", "German"]
            }
        }

        Label {
            Layout.fillWidth: true
            text: "Selected: " + sender.currentText + " / " + preset.currentText
                + " / " + profile.currentText + " / " + style.currentText
                + " / " + language.currentText
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            text: "Open each popup, use the arrow keys and Enter, then switch theme. "
                + "Resize to inspect elision and popup alignment."
            wrapMode: Text.Wrap
        }

        Item { Layout.fillHeight: true }
    }
}
