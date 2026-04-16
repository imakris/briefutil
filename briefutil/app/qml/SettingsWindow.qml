import QtQuick 2.15
import QtQuick.Layouts 1.12
import QtQuick.Controls 2.12
import QtQuick.Window 2.15
import QtQuick.Dialogs

Window {
    id: settingsWin

    width: 520
    height: 560
    title: "Settings"

    required property var proxyObj
    required property bool darkMode

    signal darkModeToggled(bool dark)
    signal windowClosed()

    property bool _initialized: false

    onClosing: {
        if (_initialized && proxyObj) {
            fontApplyTimer.stop()
            tryApplyFonts()
            if (templateDirOk) {
                proxyObj.set_template_dir(templateDir)
            }
        }
        windowClosed()
    }

    onDarkModeChanged: {
        if (proxyObj) {
            proxyObj.set_window_dark_mode(settingsWin, darkMode)
        }
    }

    color: darkMode ? "#2d2d2d" : "#eeeeee"

    property color textColor:      darkMode ? "#ffffff" : "#000000"
    property color dimTextColor:    darkMode ? "#cccccc" : "#333333"
    property color fieldBg:         darkMode ? "#1e1e1e" : "#ffffff"
    property color fieldBorder:     darkMode ? "#555555" : "#c0c0c0"
    property color buttonBg:        darkMode ? "#3d3d3d" : "#e0e0e0"
    property color buttonPressed:   darkMode ? "#505050" : "#c0c0c0"
    property color invalidFieldBg:  darkMode ? "#3d2020" : "#ffcccc"

    property string fontSans: ""
    property string fontSansBold: ""
    property string fontSansItalic: ""
    property string fontSansBoldItalic: ""
    property string fontMono: ""
    property double bodySize: 10
    property double bodyLeading: 12
    property string templateDir: ""
    property string layoutPreset: "din_5008_form_b"

    // ====================================================================
    // Font validation
    // ====================================================================

    readonly property bool fontSansOk:           proxyObj ? proxyObj.validate_font_value(fontSans, "sans") : true
    readonly property bool fontSansBoldOk:       proxyObj ? proxyObj.validate_font_value(fontSansBold, "sans_bold") : true
    readonly property bool fontSansItalicOk:     proxyObj ? proxyObj.validate_font_value(fontSansItalic, "sans_italic") : true
    readonly property bool fontSansBoldItalicOk: proxyObj ? proxyObj.validate_font_value(fontSansBoldItalic, "sans_bold_italic") : true
    readonly property bool fontMonoOk:           proxyObj ? proxyObj.validate_font_value(fontMono, "mono") : true

    readonly property bool fontsCanApply: fontSansOk && fontSansBoldOk && fontSansItalicOk
                                        && fontSansBoldItalicOk && fontMonoOk

    function tryApplyFonts() {
        if (!_initialized || !proxyObj || !fontsCanApply) return
        proxyObj.set_font_sans(fontSans)
        proxyObj.set_font_sans_bold(fontSansBold)
        proxyObj.set_font_sans_italic(fontSansItalic)
        proxyObj.set_font_sans_bold_italic(fontSansBoldItalic)
        proxyObj.set_font_mono(fontMono)
    }

    // Debounce font writes so each keystroke doesn't trigger a full
    // QSettings save (which validates and walks the system font registry).
    Timer {
        id: fontApplyTimer
        interval: 250
        repeat: false
        onTriggered: settingsWin.tryApplyFonts()
    }

    function scheduleApplyFonts() {
        if (!_initialized) return
        fontApplyTimer.restart()
    }

    onFontSansChanged:           scheduleApplyFonts()
    onFontSansBoldChanged:       scheduleApplyFonts()
    onFontSansItalicChanged:     scheduleApplyFonts()
    onFontSansBoldItalicChanged: scheduleApplyFonts()
    onFontMonoChanged:           scheduleApplyFonts()

    // ====================================================================
    // Template dir validation
    // ====================================================================

    readonly property bool templateDirOk: proxyObj ? proxyObj.validate_directory(templateDir) : true

    function syncLayoutPresetCombo() {
        for (var i = 0; i < layoutPresetModel.count; i++) {
            if (layoutPresetModel.get(i).value === layoutPreset) {
                layoutPresetCombo.currentIndex = i
                return
            }
        }
        layoutPresetCombo.currentIndex = 0
    }

    onLayoutPresetChanged: {
        if (_initialized && proxyObj) {
            proxyObj.set_layout_preset(layoutPreset)
        }
        syncLayoutPresetCombo()
    }

    // ====================================================================
    // Spinbox values (always valid — range-clamped)
    // ====================================================================

    onBodySizeChanged: {
        if (_initialized && proxyObj) proxyObj.set_body_size(bodySize)
    }
    onBodyLeadingChanged: {
        if (_initialized && proxyObj) proxyObj.set_body_leading(bodyLeading)
    }

    Component.onCompleted: {
        if (!proxyObj) return
        proxyObj.set_window_dark_mode(settingsWin, darkMode)
        fontSans           = proxyObj.get_font_sans()
        fontSansBold       = proxyObj.get_font_sans_bold()
        fontSansItalic     = proxyObj.get_font_sans_italic()
        fontSansBoldItalic = proxyObj.get_font_sans_bold_italic()
        fontMono           = proxyObj.get_font_mono()
        bodySize           = proxyObj.get_body_size()
        bodyLeading        = proxyObj.get_body_leading()
        templateDir        = proxyObj.get_template_dir()
        layoutPreset       = proxyObj.get_layout_preset()
        syncLayoutPresetCombo()
        _initialized = true
    }

    FolderDialog {
        id: folderDialog
        title: "Select template directory"
        onAccepted: {
            var path = selectedFolder.toString()
            path = path.replace(/^file:\/\/\//, "")
            path = decodeURIComponent(path)
            if (path.length > 0 && !path.endsWith("/")) path += "/"
            settingsWin.templateDir = path
            if (settingsWin._initialized && settingsWin.proxyObj) {
                settingsWin.proxyObj.set_template_dir(path)
            }
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

    component StyledComboBox: ComboBox {
        id: styledCombo

        background: Rectangle {
            implicitHeight: 28
            color: settingsWin.fieldBg
            border.width: 1
            border.color: settingsWin.fieldBorder
            radius: 2
        }

        contentItem: Text {
            leftPadding: 8
            rightPadding: 24
            text: styledCombo.displayText
            color: settingsWin.textColor
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        delegate: ItemDelegate {
            width: styledCombo.width
            highlighted: styledCombo.highlightedIndex === index

            background: Rectangle {
                color: highlighted
                    ? (settingsWin.darkMode ? "#505050" : "#d0d0d0")
                    : (settingsWin.darkMode ? "#2d2d2d" : "#eeeeee")
            }

            contentItem: Text {
                text: model[textRole]
                color: settingsWin.textColor
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        popup: Popup {
            y: styledCombo.height
            width: styledCombo.width
            padding: 1

            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: styledCombo.popup.visible ? styledCombo.delegateModel : null
                currentIndex: styledCombo.highlightedIndex
            }

            background: Rectangle {
                color: settingsWin.darkMode ? "#2d2d2d" : "#eeeeee"
                border.width: 1
                border.color: settingsWin.fieldBorder
                radius: 2
            }
        }

        indicator: Canvas {
            x: styledCombo.width - width - 8
            y: (styledCombo.height - height) / 2
            width: 12
            height: 8
            contextType: "2d"

            onPaint: {
                context.reset()
                context.moveTo(0, 0)
                context.lineTo(width, 0)
                context.lineTo(width / 2, height)
                context.closePath()
                context.fillStyle = settingsWin.textColor
                context.fill()
            }
        }
    }

    ListModel {
        id: layoutPresetModel
        ListElement { text: "DIN 5008 Form B"; value: "din_5008_form_b" }
        ListElement { text: "DIN 5008 Form A"; value: "din_5008_form_a" }
        ListElement { text: "US Letter"; value: "us_letter" }
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
            id: fontRow
            property alias label: lbl.text
            property alias value: field.text
            property bool valid: true
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
                placeholderText: "Built-in PDF font, installed font, or .ttf/.otf path"
                background: Rectangle {
                    color: fontRow.valid ? settingsWin.fieldBg : settingsWin.invalidFieldBg
                    border.width: 1
                    border.color: settingsWin.fieldBorder
                    Behavior on color { ColorAnimation { duration: 150 } }
                }
                color: settingsWin.textColor
            }
        }

        FontRow { label: "Sans";             value: settingsWin.fontSans;           valid: settingsWin.fontSansOk;           onValueChanged: settingsWin.fontSans = value }
        FontRow { label: "Sans Bold";        value: settingsWin.fontSansBold;       valid: settingsWin.fontSansBoldOk;       onValueChanged: settingsWin.fontSansBold = value }
        FontRow { label: "Sans Italic";      value: settingsWin.fontSansItalic;     valid: settingsWin.fontSansItalicOk;     onValueChanged: settingsWin.fontSansItalic = value }
        FontRow { label: "Sans Bold Italic"; value: settingsWin.fontSansBoldItalic; valid: settingsWin.fontSansBoldItalicOk; onValueChanged: settingsWin.fontSansBoldItalic = value }
        FontRow { label: "Mono";             value: settingsWin.fontMono;           valid: settingsWin.fontMonoOk;           onValueChanged: settingsWin.fontMono = value }

        Label {
            text: "You can use a built-in PDF font such as Helvetica or Courier, an installed font such as Noto Sans, or an explicit .ttf/.otf font file."
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
            text: "Layout"
            font.bold: true
            color: settingsWin.textColor
        }

        RowLayout {
            spacing: 8
            Layout.fillWidth: true

            Label {
                text: "Page preset"
                Layout.preferredWidth: 100
                color: settingsWin.dimTextColor
            }

            StyledComboBox {
                id: layoutPresetCombo
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                model: layoutPresetModel
                textRole: "text"

                onActivated: {
                    settingsWin.layoutPreset =
                        layoutPresetModel.get(currentIndex).value
                }
            }
        }

        Label {
            text: "Choose the paper size and envelope layout used for generated letters."
            wrapMode: Text.WordWrap
            color: settingsWin.dimTextColor
            Layout.fillWidth: true
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
                onEditingFinished: {
                    if (settingsWin._initialized && settingsWin.proxyObj && settingsWin.templateDirOk) {
                        settingsWin.proxyObj.set_template_dir(settingsWin.templateDir)
                    }
                }
                background: Rectangle {
                    color: settingsWin.templateDirOk ? settingsWin.fieldBg : settingsWin.invalidFieldBg
                    border.width: 1
                    border.color: settingsWin.fieldBorder
                    Behavior on color { ColorAnimation { duration: 150 } }
                }
                color: settingsWin.textColor
            }
            Button {
                id: browseBtn
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                background: Rectangle {
                    color: browseBtn.pressed ? settingsWin.buttonPressed : settingsWin.buttonBg
                    border.width: 1
                    border.color: settingsWin.fieldBorder
                    radius: 2
                }
                contentItem: Text {
                    text: "\uD83D\uDCC1"
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: {
                    if (settingsWin.templateDir.length > 0) {
                        folderDialog.currentFolder = "file:///" + settingsWin.templateDir.replace(/\\/g, "/")
                    }
                    folderDialog.open()
                }
            }
        }
    }
}
