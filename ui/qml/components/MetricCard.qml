import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    required property string title
    required property string value
    property string hint: ""
    property color accent: Theme.primary

    implicitHeight: 132
    radius: Theme.radius
    color: Theme.surface
    border.color: Theme.border

    Rectangle {
        width: 4
        radius: 2
        color: root.accent
        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
            margins: 14
        }
    }

    ColumnLayout {
        anchors {
            fill: parent
            leftMargin: 34
            rightMargin: 20
            topMargin: 18
            bottomMargin: 18
        }
        spacing: 7

        Text {
            text: root.title
            color: Theme.textMuted
            font.pixelSize: 13
        }
        Text {
            text: root.value
            color: Theme.text
            font.pixelSize: 25
            font.weight: Font.DemiBold
        }
        Text {
            visible: root.hint.length > 0
            text: root.hint
            color: root.accent
            font.pixelSize: 12
        }
    }
}
