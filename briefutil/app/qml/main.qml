import QtQuick 2.15
import QtQuick.Layouts 1.12
import QtQuick.Controls 2.12
import Proxy 1.0

ApplicationWindow {
    id: root

    width: 640
    height: 480
    visible: true

    property bool darkMode: false
    property bool hasTemplates: false
    property bool isBusy: false
    property var appProxy: proxy

    onDarkModeChanged: {
        proxy.set_window_dark_mode(root, darkMode)
        proxy.save_dark_mode(darkMode)
    }

    Component.onCompleted: {
        darkMode = proxy.load_dark_mode()
        proxy.set_window_dark_mode(root, darkMode)
    }

    palette {
        window: darkMode ? "#2d2d2d" : "#eeeeee"
        windowText: darkMode ? "#ffffff" : "#000000"
        base: darkMode ? "#1e1e1e" : "#ffffff"
        text: darkMode ? "#ffffff" : "#000000"
        button: darkMode ? "#3d3d3d" : "#e0e0e0"
        buttonText: darkMode ? "#ffffff" : "#000000"
        highlight: "#0078d4"
        highlightedText: "#ffffff"
    }

    Proxy{
        id: proxy
    }

    Connections {
        target: proxy

        function onPdf_generated(success, message) {
            root.isBusy = false
            if (!success && message.length > 0) {
                console.warn("PDF generation failed: " + message)
            }
        }

        function onSender_templates_changed() {
            root.refreshSenderTemplates()
        }
    }

    background: Rectangle {
        color: root.palette.window
    }

    function refreshSenderTemplates() {
        var currentText = w_from.currentText
        var options = proxy.get_sender_templates()
        w_from.model.clear()
        for (var i = 0; i < options.length; i++) {
            w_from.model.append({"": options[i]})
        }

        var newIndex = -1
        for (var j = 0; j < options.length; j++) {
            if (options[j] === currentText) {
                newIndex = j
                break
            }
        }
        if (newIndex < 0 && options.length > 0) {
            newIndex = 0
        }
        w_from.currentIndex = newIndex
        root.hasTemplates = options.length > 0
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 5

        Label {
            text: "From"
        }
        ComboBox {
            id: w_from
            background: Rectangle {
                color: root.palette.base
            }
            Layout.fillWidth: true

            model: ListModel {
            }

            Component.onCompleted: {
                root.refreshSenderTemplates()
            }
        }

        Label {
            text: "To"
            Layout.topMargin: 10
        }

        TextArea {
            id: w_to
            selectByMouse: true
            selectByKeyboard: true
            background: Rectangle {
                color: root.palette.base
            }
            Layout.fillWidth: true
        }

        Label {
            text: "Subject"
            Layout.topMargin: 10
        }
        TextField {
            id: w_subject
            selectByMouse: true
            background: Rectangle {
                color: root.palette.base
            }
            Layout.fillWidth: true
        }

        Label {
            text: "Body (supports Markdown)"
            Layout.topMargin: 10
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollBar.horizontal.policy: ScrollBar.AsNeeded
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            TextArea {
                id: w_body
                wrapMode: TextEdit.WordWrap
                selectByMouse: true
                selectByKeyboard: true
                background: Rectangle {
                    color: root.palette.base
                    border.width: 1
                    border.color: root.darkMode ? "#555555" : "#c0c0c0"
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: function(eventPoint) {
                        bodyContextMenu.x = eventPoint.position.x
                        bodyContextMenu.y = eventPoint.position.y
                        bodyContextMenu.open()
                    }
                }

                Menu {
                    id: bodyContextMenu

                    component StyledMenuItem: MenuItem {
                        id: styledItem
                        background: Rectangle {
                            implicitWidth: 150
                            implicitHeight: 30
                            color: styledItem.highlighted
                                ? (root.darkMode ? "#505050" : "#d0d0d0")
                                : (root.darkMode ? "#2d2d2d" : "#eeeeee")
                        }
                        contentItem: Text {
                            text: styledItem.text
                            color: styledItem.enabled
                                ? (root.darkMode ? "#ffffff" : "#000000")
                                : (root.darkMode ? "#888888" : "#999999")
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 8
                        }
                    }

                    background: Rectangle {
                        implicitWidth: 150
                        color: root.darkMode ? "#2d2d2d" : "#eeeeee"
                        border.color: root.darkMode ? "#555555" : "#b0b0b0"
                        border.width: 1
                        radius: 4
                    }

                    StyledMenuItem {
                        text: "Cut"
                        enabled: w_body.selectedText.length > 0
                        onTriggered: w_body.cut()
                    }
                    StyledMenuItem {
                        text: "Copy"
                        enabled: w_body.selectedText.length > 0
                        onTriggered: w_body.copy()
                    }
                    StyledMenuItem {
                        text: "Paste"
                        enabled: w_body.canPaste
                        onTriggered: w_body.paste()
                    }
                    MenuSeparator {
                        contentItem: Rectangle {
                            implicitWidth: 150
                            implicitHeight: 1
                            color: root.darkMode ? "#555555" : "#b0b0b0"
                        }
                    }
                    StyledMenuItem {
                        text: "Select All"
                        onTriggered: w_body.selectAll()
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true

            // Settings button (gear)
            RoundButton {
                id: settingsBtn
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                Layout.alignment: Qt.AlignVCenter

                background: Rectangle {
                    radius: width / 2
                    color: settingsBtn.pressed ? (root.darkMode ? "#4d4d4d" : "#b0b0b0")
                                               : (root.darkMode ? "#3d3d3d" : "#d0d0d0")
                    border.width: 1
                    border.color: root.darkMode ? "#5d5d5d" : "#a0a0a0"
                }

                contentItem: Text {
                    text: "\u2699"
                    font.pixelSize: 15
                    anchors.centerIn: parent
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: root.darkMode ? "#ffffff" : "#333333"
                }

                property var settingsWindow: null

                onClicked: {
                    if (settingsWindow) {
                        settingsWindow.show()
                        settingsWindow.raise()
                        settingsWindow.requestActivate()
                        return
                    }
                    var component = Qt.createComponent("qrc:/SettingsWindow.qml")
                    if (component.status === Component.Ready) {
                        settingsWindow = component.createObject(root, {
                            proxyObj: root.appProxy,
                            darkMode: root.darkMode
                        })
                        if (!settingsWindow) {
                            console.warn("Failed to create SettingsWindow")
                            return
                        }
                        settingsWindow.windowClosed.connect(function() {
                            settingsBtn.settingsWindow = null
                        })
                        settingsWindow.darkModeToggled.connect(function(dark) {
                            root.darkMode = dark
                            settingsWindow.darkMode = dark
                        })
                        settingsWindow.show()
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                id: button
                text: root.isBusy ? "PLEASE WAIT" : "GO"
                enabled: root.hasTemplates && !root.isBusy

                background: Rectangle {
                    implicitWidth: 90
                    implicitHeight: 28
                    color: !button.enabled ? (root.darkMode ? "#2a2a2a" : "#cccccc")
                         : button.pressed  ? (root.darkMode ? "#505050" : "#c0c0c0")
                         :                   (root.darkMode ? "#3d3d3d" : "#e0e0e0")
                    border.width: 1
                    border.color: root.darkMode ? "#555555" : "#c0c0c0"
                    radius: 3
                }
                contentItem: Text {
                    text: button.text
                    color: !button.enabled ? (root.darkMode ? "#666666" : "#999999")
                                           : (root.darkMode ? "#ffffff" : "#000000")
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onReleased: {
                    if (root.isBusy || !root.hasTemplates) {
                        return;
                    }
                    root.isBusy = true
                    proxy.make_pdf(w_from.currentIndex, w_to.text, w_subject.text, w_body.text)
                }
            }
        }
    }
}
