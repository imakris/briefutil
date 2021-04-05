import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.12
import QtQuick.Controls 2.12
import Proxy 1.0

ApplicationWindow {

    width: 640
    height: 480
    visible: true

    Proxy{
        id: proxy
    }

    Connections {
        target: proxy

        onTexify_finished: {
            button.enabled = true
            button.text = "GO"
        }
    }

    background: Rectangle {
        color: "#ffeeeeee"
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
                color: "#ffffffff"
            }
            Layout.fillWidth: true
            Layout.fillHeight: false

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
                color: "#ffffffff"
            }
            Layout.fillWidth: true
            Layout.fillHeight: false
        }


        Label {
            text: "Subject"
            Layout.topMargin: 10
        }
        TextField {
            id: w_subject
            selectByMouse: true
            background: Rectangle {
                color: "#ffffffff"
            }

            Layout.fillWidth: true
            Layout.fillHeight: false
        }


        Label {
            text: "Body"
            Layout.topMargin: 10
        }
        TextArea {
            id: w_body
            wrapMode: TextEdit.WordWrap
            selectByMouse: true
            selectByKeyboard: true
            background: Rectangle {
                color: "#ffffffff"
            }
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
        Button {
            id: button
            text: "GO"
            Layout.alignment: Qt.AlignRight

            onReleased: {
                button.enabled = false;
                button.text = "PLEASE WAIT"
                proxy.make_pdf(w_from.currentIndex, w_to.text, w_subject.text, w_body.text)
            }
        }

        Layout.fillWidth: true
        Layout.fillHeight: true
    }
}

