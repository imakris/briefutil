import QtQuick 2.15
import QtQuick.Controls 2.12

ComboBox {
    id: styledCombo

    property bool darkMode: false
    property color fieldBg: palette.base
    property color fieldBorder: darkMode ? "#555555" : "#c0c0c0"
    property color textColor: palette.text
    property real backgroundImplicitHeight: 28

    background: Rectangle {
        implicitHeight: styledCombo.backgroundImplicitHeight
        color: styledCombo.fieldBg
        border.width: 1
        border.color: styledCombo.fieldBorder
        radius: 2
    }

    contentItem: Text {
        leftPadding: 8
        rightPadding: 24
        text: styledCombo.displayText
        color: styledCombo.textColor
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    delegate: ItemDelegate {
        id: optionDelegate

        width: styledCombo.width
        highlighted: styledCombo.highlightedIndex === index

        background: Rectangle {
            color: optionDelegate.highlighted
                ? (styledCombo.darkMode ? "#505050" : "#d0d0d0")
                : (styledCombo.darkMode ? "#2d2d2d" : "#eeeeee")
        }

        contentItem: Text {
            text: styledCombo.textRole ? model[styledCombo.textRole] : modelData
            color: styledCombo.textColor
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
            color: styledCombo.darkMode ? "#2d2d2d" : "#eeeeee"
            border.width: 1
            border.color: styledCombo.fieldBorder
            radius: 2
        }
    }

    indicator: Canvas {
        property color ink: styledCombo.textColor

        x: styledCombo.width - width - 8
        y: (styledCombo.height - height) / 2
        width: 12
        height: 8
        contextType: "2d"
        onInkChanged: requestPaint()

        onPaint: {
            context.reset()
            context.moveTo(0, 0)
            context.lineTo(width, 0)
            context.lineTo(width / 2, height)
            context.closePath()
            context.fillStyle = ink
            context.fill()
        }
    }
}
