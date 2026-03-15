import QtQuick 2.15
import QtQuick.Layouts 1.12
import QtQuick.Controls 2.12
import QtQuick.Window 2.15

Window {
    id: settingsWin

    width: 520
    height: 530
    title: "Settings"

    required property var proxy
    required property bool darkMode

    signal darkModeToggled(bool dark)
    signal windowClosed()

    onClosing: {
        windowClosed()
    }

    onDarkModeChanged: {
        proxy.set_window_dark_mode(settingsWin, darkMode)
    }

    color: darkMode ? "#2d2d2d" : "#eeeeee"

    property color textColor: darkMode ? "#ffffff" : "#000000"
    property color dimTextColor: darkMode ? "#cccccc" : "#333333"
    property color fieldBg: darkMode ? "#1e1e1e" : "#ffffff"
    property color fieldBorder: darkMode ? "#555555" : "#c0c0c0"
    property color buttonBg: darkMode ? "#3d3d3d" : "#e0e0e0"
    property color buttonPressed: darkMode ? "#505050" : "#c0c0c0"

    property string fontSans: ""
    property string fontSansBold: ""
    property string fontSansItalic: ""
    property string fontSansBoldItalic: ""
    property string fontMono: ""
    property double bodySize: 10
    property double bodyLeading: 12
    property string templateDir: ""

    Component.onCompleted: {
        proxy.set_window_dark_mode(settingsWin, darkMode)
        fontSans           = proxy.get_font_sans()
        fontSansBold       = proxy.get_font_sans_bold()
        fontSansItalic     = proxy.get_font_sans_italic()
        fontSansBoldItalic = proxy.get_font_sans_bold_italic()
        fontMono           = proxy.get_font_mono()
        bodySize           = proxy.get_body_size()
        bodyLeading        = proxy.get_body_leading()
        templateDir        = proxy.get_template_dir()
    }

    function applySettings() {
        proxy.set_font_sans(fontSans)
        proxy.set_font_sans_bold(fontSansBold)
        proxy.set_font_sans_italic(fontSansItalic)
        proxy.set_font_sans_bold_italic(fontSansBoldItalic)
        proxy.set_font_mono(fontMono)
        proxy.set_body_size(bodySize)
        proxy.set_body_leading(bodyLeading)
        proxy.set_template_dir(templateDir)
        settingsWin.close()
    }

    // Styled button matching dark/light mode
    component StyledButton: Button {
        id: styledBtn
        background: Rectangle {
            implicitWidth: 80
            implicitHeight: 28
            color: styledBtn.pressed ? settingsWin.buttonPressed : settingsWin.buttonBg
            border.width: 1
            border.color: settingsWin.fieldBorder
            radius: 3
        }
        contentItem: Text {
            text: styledBtn.text
            color: settingsWin.textColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // Styled spin box matching dark/light mode
    component StyledSpinBox: SpinBox {
        id: styledSpin
        editable: true

        background: Rectangle {
            implicitWidth: 100
            implicitHeight: 28
            color: settingsWin.fieldBg
            border.width: 1
            border.color: settingsWin.fieldBorder
        }

        contentItem: TextInput {
            text: styledSpin.textFromValue(styledSpin.value, styledSpin.locale)
            font: styledSpin.font
            color: settingsWin.textColor
            selectionColor: "#0078d4"
            selectedTextColor: "#ffffff"
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            readOnly: !styledSpin.editable
            validator: styledSpin.validator
            inputMethodHints: Qt.ImhFormattedNumbersOnly
        }

        up.indicator: Rectangle {
            x: styledSpin.mirrored ? 0 : parent.width - width
            width: 20
            height: parent.height
            color: styledSpin.up.pressed ? settingsWin.buttonPressed : settingsWin.buttonBg
            border.width: 1
            border.color: settingsWin.fieldBorder

            Text {
                text: "+"
                font.pixelSize: 14
                color: settingsWin.textColor
                anchors.centerIn: parent
            }
        }

        down.indicator: Rectangle {
            x: styledSpin.mirrored ? parent.width - width : 0
            width: 20
            height: parent.height
            color: styledSpin.down.pressed ? settingsWin.buttonPressed : settingsWin.buttonBg
            border.width: 1
            border.color: settingsWin.fieldBorder

            Text {
                text: "-"
                font.pixelSize: 14
                color: settingsWin.textColor
                anchors.centerIn: parent
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 15
        spacing: 8

        RowLayout {
            spacing: 10
            Layout.fillWidth: true

            Label {
                text: "Appearance"
                font.bold: true
                color: settingsWin.textColor
            }

            Item { Layout.fillWidth: true }

            RoundButton {
                id: themeToggle
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28

                background: Rectangle {
                    radius: width / 2
                    color: themeToggle.pressed ? (settingsWin.darkMode ? "#4d4d4d" : "#909090")
                                               : (settingsWin.darkMode ? "#3d3d3d" : "#505050")
                    border.width: 1
                    border.color: settingsWin.darkMode ? "#5d5d5d" : "#404040"
                }

                contentItem: Text {
                    text: settingsWin.darkMode ? "\uD83C\uDF19" : "\u2600"
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: "#ffffff"
                }

                onClicked: {
                    settingsWin.darkModeToggled(!settingsWin.darkMode)
                }
            }
        }

        Item { Layout.preferredHeight: 4 }

        Label {
            text: "Fonts"
            font.bold: true
            color: settingsWin.textColor
        }

        component FontRow: RowLayout {
            property alias label: lbl.text
            property alias value: field.text
            spacing: 8
            Layout.fillWidth: true

            Label {
                id: lbl
                Layout.preferredWidth: 100
                color: settingsWin.dimTextColor
            }
            TextField {
                id: field
                Layout.fillWidth: true
                selectByMouse: true
                placeholderText: "Font name or .ttf/.otf path"
                background: Rectangle {
                    color: settingsWin.fieldBg
                    border.width: 1
                    border.color: settingsWin.fieldBorder
                }
                color: settingsWin.textColor
            }
        }

        FontRow { label: "Sans";             value: settingsWin.fontSans;           onValueChanged: settingsWin.fontSans = value }
        FontRow { label: "Sans Bold";        value: settingsWin.fontSansBold;       onValueChanged: settingsWin.fontSansBold = value }
        FontRow { label: "Sans Italic";      value: settingsWin.fontSansItalic;     onValueChanged: settingsWin.fontSansItalic = value }
        FontRow { label: "Sans Bold Italic"; value: settingsWin.fontSansBoldItalic; onValueChanged: settingsWin.fontSansBoldItalic = value }
        FontRow { label: "Mono";             value: settingsWin.fontMono;           onValueChanged: settingsWin.fontMono = value }

        Label {
            text: "Use either built-in font names for all fields or .ttf/.otf file paths for all fields."
            wrapMode: Text.WordWrap
            color: settingsWin.dimTextColor
            Layout.fillWidth: true
        }

        Item { Layout.preferredHeight: 8 }

        Label {
            text: "Typography"
            font.bold: true
            color: settingsWin.textColor
        }

        RowLayout {
            spacing: 15
            Layout.fillWidth: true

            Label { text: "Body size (pt)"; color: settingsWin.dimTextColor }
            StyledSpinBox {
                from: 6
                to: 24
                value: settingsWin.bodySize
                onValueChanged: settingsWin.bodySize = value
            }

            Label { text: "Leading (pt)"; color: settingsWin.dimTextColor }
            StyledSpinBox {
                from: 6
                to: 36
                value: settingsWin.bodyLeading
                onValueChanged: settingsWin.bodyLeading = value
            }
        }

        Item { Layout.preferredHeight: 8 }

        Label {
            text: "Paths"
            font.bold: true
            color: settingsWin.textColor
        }

        RowLayout {
            spacing: 8
            Layout.fillWidth: true

            Label {
                text: "Template dir"
                Layout.preferredWidth: 100
                color: settingsWin.dimTextColor
            }
            TextField {
                id: templateDirField
                Layout.fillWidth: true
                text: settingsWin.templateDir
                selectByMouse: true
                onTextChanged: settingsWin.templateDir = text
                background: Rectangle {
                    color: settingsWin.fieldBg
                    border.width: 1
                    border.color: settingsWin.fieldBorder
                }
                color: settingsWin.textColor
            }
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }

            StyledButton {
                text: "Cancel"
                onClicked: settingsWin.close()
            }
            StyledButton {
                text: "Apply"
                onClicked: settingsWin.applySettings()
            }
        }
    }
}
