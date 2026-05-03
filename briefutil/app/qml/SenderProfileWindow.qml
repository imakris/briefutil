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
    property int profileIndex: -1

    signal windowClosed()

    property bool _initialized: false
    property int _newProfileIndex: -1
    property string savedProfileId: ""

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
    property string profileLanguage: "en"
    property string returnAddressLine: ""
    property string closingPhrase: ""
    property string signerName: ""
    property string signerTitle: ""
    property string signatureImage: ""
    property string logoImage: ""
    property string topRuleColor: "#C8C8C8"
    property string footerLines: ""

    readonly property bool isCommercial: profileStyle === "commercial"
    readonly property bool profileIdDuplicate:
        profileNameInUse(profileId.trim(), savedProfileId)
    readonly property bool profileIdOk: profileId.trim().length > 0 && !profileIdDuplicate
    readonly property string profileIdErrorText: profileId.trim().length === 0
        ? "Display name is required."
        : (profileIdDuplicate ? "Another sender profile already uses this display name." : "")
    readonly property bool signatureImageOk: proxyObj ? proxyObj.validate_profile_image_name(signatureImage) : true
    readonly property bool logoImageOk: proxyObj ? proxyObj.validate_profile_image_name(logoImage) : true
    readonly property bool topRuleColorOk: proxyObj ? proxyObj.validate_hex_color(topRuleColor) : true
    readonly property bool profileCanSave: profileIdOk && signatureImageOk
        && (!isCommercial || (logoImageOk && topRuleColorOk))

    color: darkMode ? "#2d2d2d" : "#eeeeee"

    onDarkModeChanged: {
        if (proxyObj) {
            proxyObj.set_window_dark_mode(editorWin, darkMode)
        }
    }

    onProfileIndexChanged: {
        if (profileCombo && profileCombo.currentIndex !== profileIndex) {
            profileCombo.currentIndex = profileIndex
        }
        loadProfile()
    }

    function resetProfileFields() {
        profileId = ""
        savedProfileId = ""
        profileStyle = "simple"
        senderLines = ""
        email = ""
        profileLanguage = "en"
        returnAddressLine = ""
        closingPhrase = ""
        signerName = ""
        signerTitle = ""
        signatureImage = ""
        logoImage = ""
        topRuleColor = "#C8C8C8"
        footerLines = ""

        if (idField) {
            idField.text = profileId
        }
        if (styleCombo) {
            styleCombo.currentIndex = 0
        }
        if (emailField) {
            emailField.text = email
        }
        if (languageCombo) {
            languageCombo.currentIndex = 0
        }
        if (closingPhraseField) {
            closingPhraseField.text = closingPhrase
        }
        if (signerNameField) {
            signerNameField.text = signerName
        }
        if (topRuleColorField) {
            topRuleColorField.text = topRuleColor
        }
        if (senderLinesArea) {
            senderLinesArea.text = senderLines
        }
        if (returnAddressArea) {
            returnAddressArea.text = returnAddressLine
        }
        if (signatureImageField) {
            signatureImageField.text = signatureImage
        }
        if (logoImageField) {
            logoImageField.text = logoImage
        }
        if (signerTitleField) {
            signerTitleField.text = signerTitle
        }
        if (footerLinesArea) {
            footerLinesArea.text = footerLines
        }
    }

    function selectProfile(index, forceReload) {
        if (profileCombo && profileCombo.currentIndex !== index) {
            profileCombo.currentIndex = index
        }
        if (profileIndex !== index) {
            profileIndex = index
        }
        else
        if (forceReload) {
            loadProfile()
        }
    }

    function switchToProfile(index) {
        saveTimer.stop()
        saveProfile()
        selectProfile(index, true)
    }

    onClosing: {
        saveTimer.stop()
        saveProfile()
        cleanupNewProfile()
        windowClosed()
    }

    Component.onCompleted: {
        if (proxyObj) {
            proxyObj.set_window_dark_mode(editorWin, darkMode)
        }
        refreshProfileList()
        if (profileCombo && profileIndex >= 0) {
            profileCombo.currentIndex = profileIndex
        }
        loadProfile()
    }

    Connections {
        target: proxyObj
        function onSender_templates_changed() {
            var lookupId = editorWin.profileIdOk
                ? editorWin.profileId
                : editorWin.savedProfileId
            var selectedIndex = editorWin.refreshProfileList(lookupId)
            if (selectedIndex >= 0 && selectedIndex !== editorWin.profileIndex) {
                editorWin.profileIndex = selectedIndex
            }
        }
    }

    function refreshProfileList(preferredId) {
        if (!proxyObj || !profileCombo) {
            return -1
        }
        var templates = proxyObj.get_sender_templates()
        var items = []
        for (var i = 0; i < templates.length; i++) {
            items.push(templates[i].length > 0 ? templates[i] : "(new profile)")
        }
        profileCombo.model = items
        if (items.length === 0) {
            profileCombo.currentIndex = -1
            return -1
        }

        var selectedIndex = -1
        var wantedId = preferredId ? preferredId.trim() : ""
        if (wantedId.length > 0) {
            for (var j = 0; j < templates.length; j++) {
                if (templates[j] === wantedId) {
                    selectedIndex = j
                    break
                }
            }
        }
        if (selectedIndex < 0) {
            selectedIndex = profileIndex
        }
        if (selectedIndex < 0) {
            selectedIndex = 0
        }
        else
        if (selectedIndex >= items.length) {
            selectedIndex = items.length - 1
        }
        profileCombo.currentIndex = selectedIndex
        return selectedIndex
    }

    function cleanupNewProfile() {
        if (_newProfileIndex < 0 || !proxyObj) {
            return
        }
        var p = proxyObj.get_sender_profile(_newProfileIndex)
        if (!p || !p.id || p.id.trim().length === 0) {
            proxyObj.delete_sender_profile(_newProfileIndex)
        }
        _newProfileIndex = -1
    }

    function loadProfile() {
        _initialized = false
        saveTimer.stop()
        if (!proxyObj || profileIndex < 0) {
            resetProfileFields()
            return
        }

        var profile = proxyObj.get_sender_profile(profileIndex)
        if (!profile
            || (profile.id === undefined
                && profile.style === undefined
                && profile.senderLines === undefined)) {
            resetProfileFields()
            return
        }
        profileId = profile.id || ""
        savedProfileId = profileId.trim()
        profileStyle = profile.style || "simple"
        senderLines = profile.senderLines || ""
        email = profile.email || ""
        profileLanguage = profile.language || "en"
        returnAddressLine = profile.returnAddressLine || ""
        closingPhrase = profile.closingPhrase || ""
        signerName = profile.signerName || ""
        signerTitle = profile.signerTitle || ""
        signatureImage = profile.signatureImage || ""
        logoImage = profile.logoImage || ""
        topRuleColor = profile.topRuleColor || "#C8C8C8"
        footerLines = profile.footerLines || ""

        if (idField) {
            idField.text = profileId
        }
        if (styleCombo) {
            styleCombo.currentIndex = profileStyle === "commercial" ? 1 : 0
        }
        if (emailField) {
            emailField.text = email
        }
        if (languageCombo) {
            languageCombo.currentIndex = profileLanguage === "de" ? 1 : 0
        }
        if (closingPhraseField) {
            closingPhraseField.text = closingPhrase
        }
        if (signerNameField) {
            signerNameField.text = signerName
        }
        if (topRuleColorField) {
            topRuleColorField.text = topRuleColor
        }
        if (senderLinesArea) {
            senderLinesArea.text = senderLines
        }
        if (returnAddressArea) {
            returnAddressArea.text = returnAddressLine
        }
        if (signatureImageField) {
            signatureImageField.text = signatureImage
        }
        if (logoImageField) {
            logoImageField.text = logoImage
        }
        if (signerTitleField) {
            signerTitleField.text = signerTitle
        }
        if (footerLinesArea) {
            footerLinesArea.text = footerLines
        }

        _initialized = true
    }

    function scheduleSave() {
        if (!_initialized) {
            return
        }
        saveTimer.restart()
    }

    function profileNameInUse(name, excludedName) {
        if (!proxyObj || name.length === 0) {
            return false
        }
        var excluded = excludedName ? excludedName.trim() : ""
        var templates = proxyObj.get_sender_templates()
        for (var i = 0; i < templates.length; i++) {
            if (templates[i] === name && templates[i] !== excluded) {
                return true
            }
        }
        return false
    }

    function saveProfile() {
        if (!_initialized || !proxyObj || profileIndex < 0 || !profileCanSave) {
            return
        }
        var ok = proxyObj.save_sender_profile(profileIndex, {
            id: profileId,
            style: profileStyle,
            senderLines: senderLines,
            email: email,
            language: profileLanguage,
            returnAddressLine: returnAddressLine,
            closingPhrase: closingPhrase,
            signerName: signerName,
            signerTitle: signerTitle,
            signatureImage: signatureImage,
            logoImage: logoImage,
            topRuleColor: topRuleColor,
            footerLines: footerLines
        })
        if (!ok) {
            console.warn("Failed to save sender profile")
            return
        }
        var savedId = profileId.trim()
        savedProfileId = savedId
        if (profileId !== savedId) {
            _initialized = false
            profileId = savedId
            if (idField) {
                idField.text = savedId
            }
            _initialized = true
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
            if (!editorWin.proxyObj) {
                return
            }
            var imported = editorWin.proxyObj.import_template_image(selectedFile)
            if (imported.length === 0) {
                return
            }

            if (targetField === "signature") {
                editorWin.signatureImage = imported
                if (signatureImageField) {
                    signatureImageField.text = imported
                }
            }
            else
            if (targetField === "logo") {
                editorWin.logoImage = imported
                if (logoImageField) {
                    logoImageField.text = imported
                }
            }
            editorWin.scheduleSave()
        }
    }

    component StyledTextField: TextField {
        id: styledField
        property bool valid: true
        property string errorText: ""
        hoverEnabled: true
        selectByMouse: true
        color: editorWin.textColor
        ToolTip.visible: !valid && hovered && errorText.length > 0
        ToolTip.text: errorText
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
        property string errorText: ""
        hoverEnabled: true
        selectByMouse: true
        selectByKeyboard: true
        wrapMode: TextEdit.Wrap
        color: editorWin.textColor
        ToolTip.visible: !valid && hovered && errorText.length > 0
        ToolTip.text: errorText
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

    component ActionButton: Button {
        id: actionBtn
        Layout.preferredWidth: 92
        Layout.preferredHeight: idField.implicitHeight
        background: Rectangle {
            color: actionBtn.pressed ? editorWin.buttonPressed : editorWin.buttonBg
            border.width: 1
            border.color: editorWin.fieldBorder
            radius: 2
            opacity: actionBtn.enabled ? 1.0 : 0.6
        }
        contentItem: Text {
            text: actionBtn.text
            color: editorWin.textColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            leftPadding: 8
            rightPadding: 8
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
        id: editorScroll
        anchors.fill: parent
        anchors.margins: 15
        clip: true
        rightPadding: 14
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        Item {
            width: editorScroll.availableWidth
            implicitWidth: editorScroll.availableWidth
            implicitHeight: contentColumn.implicitHeight

            ColumnLayout {
                id: contentColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                spacing: 8

                Label {
                    text: "Sender profile"
                    font.bold: true
                    color: editorWin.textColor
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    StyledComboBox {
                        id: profileCombo
                        Layout.fillWidth: true
                        Layout.preferredHeight: idField.implicitHeight
                        model: []
                        onActivated: function(index) {
                            saveTimer.stop()
                            editorWin.saveProfile()
                            editorWin.selectProfile(index, true)
                        }
                    }

                    ActionButton {
                        id: newProfileBtn
                        text: "New"
                        onClicked: {
                            saveTimer.stop()
                            editorWin.saveProfile()
                            editorWin.cleanupNewProfile()

                            var newIndex = proxyObj.create_new_profile()
                            editorWin._newProfileIndex = newIndex
                            editorWin.selectProfile(newIndex, true)
                        }
                    }

                    ActionButton {
                        text: "Clone"
                        enabled: editorWin.profileIndex >= 0 && editorWin.profileCanSave
                        onClicked: {
                            saveTimer.stop()
                            editorWin.saveProfile()
                            editorWin.cleanupNewProfile()

                            var newIndex = proxyObj.clone_sender_profile(editorWin.profileIndex)
                            if (newIndex >= 0) {
                                editorWin.selectProfile(newIndex, true)
                            }
                        }
                    }

                    ActionButton {
                        text: "Remove"
                        enabled: editorWin.profileIndex >= 0
                        onClicked: {
                            if (!proxyObj || editorWin.profileIndex < 0) {
                                return
                            }

                            saveTimer.stop()
                            var removedIndex = editorWin.profileIndex
                            if (editorWin._newProfileIndex === removedIndex) {
                                editorWin._newProfileIndex = -1
                            }
                            else
                            if (editorWin._newProfileIndex > removedIndex) {
                                editorWin._newProfileIndex -= 1
                            }
                            if (!proxyObj.delete_sender_profile(removedIndex)) {
                                return
                            }

                            editorWin.refreshProfileList()
                            var remaining = proxyObj.get_sender_templates()
                            if (remaining.length === 0) {
                                var newIndex = proxyObj.create_new_profile()
                                editorWin._newProfileIndex = newIndex
                                editorWin.selectProfile(newIndex, true)
                            }
                            else {
                                var nextIndex = Math.min(removedIndex, remaining.length - 1)
                                editorWin.selectProfile(nextIndex, true)
                            }
                        }
                    }
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
                        errorText: editorWin.profileIdErrorText
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

                    Label { text: "Language"; color: editorWin.dimTextColor }
                    StyledComboBox {
                        id: languageCombo
                        Layout.fillWidth: true
                        Layout.preferredHeight: idField.implicitHeight
                        model: ["English", "German"]
                        currentIndex: editorWin.profileLanguage === "de" ? 1 : 0
                        onActivated: {
                            editorWin.profileLanguage = currentIndex === 1 ? "de" : "en"
                            editorWin.scheduleSave()
                        }
                    }

                    Label { text: "Closing phrase"; color: editorWin.dimTextColor }
                    StyledTextField {
                        id: closingPhraseField
                        Layout.fillWidth: true
                        text: editorWin.closingPhrase
                        placeholderText: editorWin.profileLanguage === "de"
                            ? "Defaults to \"Mit freundlichen Grüßen\""
                            : "Defaults to \"Sincerely,\""
                        onTextChanged: {
                            editorWin.closingPhrase = text
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
                        errorText: "Use a color in #RRGGBB format."
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
                        errorText: "Use an existing PNG filename in the template directory, or leave this empty."
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
                        errorText: "Use an existing PNG filename in the template directory, or leave this empty."
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
}
