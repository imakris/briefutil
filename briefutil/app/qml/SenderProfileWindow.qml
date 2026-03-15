import QtQuick 2.15
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import QtQuick.Window 2.15
import QtQuick.Dialogs

Window {
    id: editorWin

    width: 680
    height: 760
    title: profileId.trim().length > 0 ? "Sender Profile: " + profileId : "Sender Profile"

    required property var proxyObj
    required property bool darkMode
    required property int profileIndex

    signal windowClosed()

    property bool _initialized: false

    property color textColor:      darkMode ? "#ffffff" : "#000000"
    property color dimTextColor:   darkMode ? "#cccccc" : "#333333"
    property color fieldBg:        darkMode ? "#1e1e1e" : "#ffffff"
    property color fieldBorder:    darkMode ? "#555555" : "#c0c0c0"
    property color buttonBg:       darkMode ? "#3d3d3d" : "#e0e0e0"
    property color buttonPressed:  darkMode ? "#505050" : "#c0c0c0"
    property color invalidFieldBg: darkMode ? "#3d2020" : "#ffcccc"

    property string profileId: ""
    property string profileStyle: "simple"
    property string senderLines: ""
    property string email: ""
    property string returnAddressLine: ""
    property string signerName: ""
    property string signerTitle: ""
    property string signatureImage: ""
    property string logoImage: ""
    property string topRuleColor: "#C8C8C8"
    property string footerLines: ""

    readonly property bool isCommercial: profileStyle === "commercial"
    readonly property bool profileIdOk: profileId.trim().length > 0
    readonly property bool signatureImageOk: proxyObj ? proxyObj.validate_profile_image_name(signatureImage) : true
    readonly property bool logoImageOk: proxyObj ? proxyObj.validate_profile_image_name(logoImage) : true
    readonly property bool topRuleColorOk: proxyObj ? proxyObj.validate_hex_color(topRuleColor) : true
    readonly property bool profileCanSave: profileIdOk && signatureImageOk && logoImageOk && topRuleColorOk

    color: darkMode ? "#2d2d2d" : "#eeeeee"

    onDarkModeChanged: {
        if (proxyObj) {
            proxyObj.set_window_dark_mode(editorWin, darkMode)
        }
    }

    onProfileIndexChanged: loadProfile()

    onClosing: {
        saveTimer.stop()
        saveProfile()
        windowClosed()
    }

    Component.onCompleted: {
        if (proxyObj) {
            proxyObj.set_window_dark_mode(editorWin, darkMode)
        }
        loadProfile()
    }

    function loadProfile() {
        _initialized = false
        saveTimer.stop()
        if (!proxyObj || profileIndex < 0) {
            return
        }

        var profile = proxyObj.get_sender_profile(profileIndex)
        profileId = profile.id || ""
        profileStyle = profile.style || "simple"
        senderLines = profile.senderLines || ""
        email = profile.email || ""
        returnAddressLine = profile.returnAddressLine || ""
        signerName = profile.signerName || ""
        signerTitle = profile.signerTitle || ""
        signatureImage = profile.signatureImage || ""
        logoImage = profile.logoImage || ""
        topRuleColor = profile.topRuleColor || "#C8C8C8"
        footerLines = profile.footerLines || ""

        if (idField) idField.text = profileId
        if (styleCombo) styleCombo.currentIndex = profileStyle === "commercial" ? 1 : 0
        if (emailField) emailField.text = email
        if (signerNameField) signerNameField.text = signerName
        if (topRuleColorField) topRuleColorField.text = topRuleColor
        if (senderLinesArea) senderLinesArea.text = senderLines
        if (returnAddressArea) returnAddressArea.text = returnAddressLine
        if (signatureImageField) signatureImageField.text = signatureImage
        if (logoImageField) logoImageField.text = logoImage
        if (signerTitleField) signerTitleField.text = signerTitle
        if (footerLinesArea) footerLinesArea.text = footerLines

        _initialized = true
    }

    function scheduleSave() {
        if (!_initialized) return
        saveTimer.restart()
    }

    function saveProfile() {
        if (!_initialized || !proxyObj || profileIndex < 0 || !profileCanSave) return
        var ok = proxyObj.save_sender_profile(profileIndex, {
            id: profileId,
            style: profileStyle,
            senderLines: senderLines,
            email: email,
            returnAddressLine: returnAddressLine,
            signerName: signerName,
            signerTitle: signerTitle,
            signatureImage: signatureImage,
            logoImage: logoImage,
            topRuleColor: topRuleColor,
            footerLines: footerLines
        })
        if (!ok) {
            console.warn("Failed to save sender profile")
        }
    }

    Timer {
        id: saveTimer
        interval: 250
        repeat: false
        onTriggered: editorWin.saveProfile()
    }

    FileDialog {
        id: imageDialog
        title: "Import PNG into template directory"
        nameFilters: ["PNG files (*.png)"]
        property string targetField: ""
        onAccepted: {
            if (!editorWin.proxyObj) return
            var imported = editorWin.proxyObj.import_template_image(selectedFile)
            if (imported.length === 0) return

            if (targetField === "signature") {
                editorWin.signatureImage = imported
                if (signatureImageField) signatureImageField.text = imported
            } else if (targetField === "logo") {
                editorWin.logoImage = imported
                if (logoImageField) logoImageField.text = imported
            }
            editorWin.scheduleSave()
        }
    }

    component StyledTextField: TextField {
        id: styledField
        property bool valid: true
        selectByMouse: true
        color: editorWin.textColor
        background: Rectangle {
            color: styledField.valid ? editorWin.fieldBg : editorWin.invalidFieldBg
            border.width: 1
            border.color: editorWin.fieldBorder
            Behavior on color { ColorAnimation { duration: 150 } }
        }
    }

    component StyledTextArea: TextArea {
        id: styledArea
        property bool valid: true
        selectByMouse: true
        selectByKeyboard: true
        wrapMode: TextEdit.Wrap
        color: editorWin.textColor
        background: Rectangle {
            color: styledArea.valid ? editorWin.fieldBg : editorWin.invalidFieldBg
            border.width: 1
            border.color: editorWin.fieldBorder
            Behavior on color { ColorAnimation { duration: 150 } }
        }
    }

    component BrowseButton: Button {
        id: browseBtn
        Layout.preferredWidth: 28
        Layout.preferredHeight: 28
        background: Rectangle {
            color: browseBtn.pressed ? editorWin.buttonPressed : editorWin.buttonBg
            border.width: 1
            border.color: editorWin.fieldBorder
            radius: 2
        }
        contentItem: Text {
            text: "\uD83D\uDCC1"
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component StyledComboBox: ComboBox {
        id: styledCombo
        background: Rectangle {
            color: editorWin.fieldBg
            border.width: 1
            border.color: editorWin.fieldBorder
            radius: 2
        }
        contentItem: Text {
            leftPadding: 8
            rightPadding: 24
            text: styledCombo.displayText
            color: editorWin.textColor
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        delegate: ItemDelegate {
            width: styledCombo.width
            highlighted: styledCombo.highlightedIndex === index
            background: Rectangle {
                color: highlighted
                    ? (editorWin.darkMode ? "#505050" : "#d0d0d0")
                    : (editorWin.darkMode ? "#2d2d2d" : "#eeeeee")
            }
            contentItem: Text {
                text: modelData
                color: editorWin.textColor
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
                color: editorWin.darkMode ? "#2d2d2d" : "#eeeeee"
                border.width: 1
                border.color: editorWin.fieldBorder
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
                context.fillStyle = editorWin.textColor
                context.fill()
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 15
        clip: true

        ColumnLayout {
            width: editorWin.width - 40
            spacing: 8

            Label {
                text: "Sender profile"
                font.bold: true
                color: editorWin.textColor
            }

            Label {
                text: "Changes are written directly to the selected profile JSON. Imported PNG files are copied into the template directory."
                wrapMode: Text.WordWrap
                color: editorWin.dimTextColor
                Layout.fillWidth: true
            }

            Item { Layout.preferredHeight: 4 }

            Label {
                text: "General"
                font.bold: true
                color: editorWin.textColor
            }

            GridLayout {
                columns: 2
                columnSpacing: 10
                rowSpacing: 8
                Layout.fillWidth: true

                Label { text: "Display name"; color: editorWin.dimTextColor }
                StyledTextField {
                    id: idField
                    Layout.fillWidth: true
                    valid: editorWin.profileIdOk
                    text: editorWin.profileId
                    onTextChanged: {
                        editorWin.profileId = text
                        editorWin.scheduleSave()
                    }
                }

                Label { text: "Style"; color: editorWin.dimTextColor }
                StyledComboBox {
                    id: styleCombo
                    Layout.fillWidth: true
                    Layout.preferredHeight: idField.implicitHeight
                    model: ["simple", "commercial"]
                    currentIndex: editorWin.profileStyle === "commercial" ? 1 : 0
                    onActivated: {
                        editorWin.profileStyle = currentIndex === 1 ? "commercial" : "simple"
                        editorWin.scheduleSave()
                    }
                }

                Label { text: "Email"; color: editorWin.dimTextColor }
                StyledTextField {
                    id: emailField
                    Layout.fillWidth: true
                    text: editorWin.email
                    onTextChanged: {
                        editorWin.email = text
                        editorWin.scheduleSave()
                    }
                }

                Label { text: "Signer name"; color: editorWin.dimTextColor }
                StyledTextField {
                    id: signerNameField
                    Layout.fillWidth: true
                    text: editorWin.signerName
                    onTextChanged: {
                        editorWin.signerName = text
                        editorWin.scheduleSave()
                    }
                }

                Label {
                    text: "Top rule color"
                    color: editorWin.dimTextColor
                    visible: editorWin.isCommercial
                }
                StyledTextField {
                    id: topRuleColorField
                    Layout.fillWidth: true
                    visible: editorWin.isCommercial
                    valid: editorWin.topRuleColorOk
                    text: editorWin.topRuleColor
                    placeholderText: "#C8C8C8"
                    onTextChanged: {
                        editorWin.topRuleColor = text
                        editorWin.scheduleSave()
                    }
                }
            }

            Label {
                text: "Sender lines"
                font.bold: true
                color: editorWin.textColor
            }
            StyledTextArea {
                id: senderLinesArea
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                text: editorWin.senderLines
                placeholderText: "One line per row"
                onTextChanged: {
                    editorWin.senderLines = text
                    editorWin.scheduleSave()
                }
            }

            Label {
                text: "Return address line"
                font.bold: true
                color: editorWin.textColor
            }
            StyledTextArea {
                id: returnAddressArea
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                text: editorWin.returnAddressLine
                placeholderText: "Shown above the recipient block"
                onTextChanged: {
                    editorWin.returnAddressLine = text
                    editorWin.scheduleSave()
                }
            }

            Label {
                text: "Images"
                font.bold: true
                color: editorWin.textColor
            }

            GridLayout {
                columns: 3
                columnSpacing: 8
                rowSpacing: 8
                Layout.fillWidth: true

                Label { text: "Signature"; color: editorWin.dimTextColor }
                StyledTextField {
                    id: signatureImageField
                    Layout.fillWidth: true
                    valid: editorWin.signatureImageOk
                    text: editorWin.signatureImage
                    placeholderText: "relative PNG filename"
                    onTextChanged: {
                        editorWin.signatureImage = text
                        editorWin.scheduleSave()
                    }
                }
                BrowseButton {
                    onClicked: {
                        imageDialog.targetField = "signature"
                        imageDialog.open()
                    }
                }

                Label {
                    text: "Logo"
                    color: editorWin.dimTextColor
                    visible: editorWin.isCommercial
                }
                StyledTextField {
                    id: logoImageField
                    Layout.fillWidth: true
                    visible: editorWin.isCommercial
                    valid: editorWin.logoImageOk
                    text: editorWin.logoImage
                    placeholderText: "relative PNG filename"
                    onTextChanged: {
                        editorWin.logoImage = text
                        editorWin.scheduleSave()
                    }
                }
                BrowseButton {
                    visible: editorWin.isCommercial
                    onClicked: {
                        imageDialog.targetField = "logo"
                        imageDialog.open()
                    }
                }
            }

            Label {
                text: "Commercial footer"
                font.bold: true
                color: editorWin.textColor
                visible: editorWin.isCommercial
            }
            StyledTextField {
                id: signerTitleField
                Layout.fillWidth: true
                visible: editorWin.isCommercial
                text: editorWin.signerTitle
                placeholderText: "Signer title"
                onTextChanged: {
                    editorWin.signerTitle = text
                    editorWin.scheduleSave()
                }
            }
            StyledTextArea {
                id: footerLinesArea
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                visible: editorWin.isCommercial
                text: editorWin.footerLines
                placeholderText: "One footer line per row"
                onTextChanged: {
                    editorWin.footerLines = text
                    editorWin.scheduleSave()
                }
            }
        }
    }
}
