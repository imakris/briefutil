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

    onDarkModeChanged: {
        proxy.setWindowDarkMode(root, darkMode)
        proxy.saveDarkMode(darkMode)
    }

    Component.onCompleted: {
        darkMode = proxy.loadDarkMode()
        proxy.setWindowDarkMode(root, darkMode)
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

        function onTexify_finished() {
            root.isBusy = false
        }
    }

    background: Rectangle {
        color: root.palette.window
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
                w_from.model.clear();
                var options = proxy.get_sender_templates();
                for (var i=0; i<options.length; i++) {
                    w_from.model.append({"": options[i]});
                }
                if (options.length != 0) {
                    w_from.currentIndex = 0;
                }
                root.hasTemplates = options.length > 0
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
            text: "Body"
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

            // Theme toggle button
            RoundButton {
                id: themeToggle
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                Layout.alignment: Qt.AlignVCenter

                background: Rectangle {
                    radius: width / 2
                    color: themeToggle.pressed ? (root.darkMode ? "#4d4d4d" : "#909090")
                                               : (root.darkMode ? "#3d3d3d" : "#505050")
                    border.width: 1
                    border.color: root.darkMode ? "#5d5d5d" : "#404040"
                }

                contentItem: Text {
                    text: root.darkMode ? "\u2600" : "\uD83C\uDF19"
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: "#ffffff"
                }

                onClicked: {
                    root.darkMode = !root.darkMode
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                id: button
                text: root.isBusy ? "PLEASE WAIT" : "GO"
                enabled: root.hasTemplates && !root.isBusy

                onReleased: {
                    if (root.isBusy || !root.hasTemplates)
                        return;
                    root.isBusy = true
                    proxy.make_pdf(w_from.currentIndex, w_to.text, w_subject.text, w_body.text)
                }
            }
        }
    }
}
