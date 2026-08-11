import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    required property string title
    required property string value
    property string hint: ""
    property color accent: Theme.primary

    implicitHeight: 104
    radius: Theme.radius
    color: Theme.surface
    border.color: Theme.border

    ColumnLayout {
        anchors {
            fill: parent
            leftMargin: 18
            rightMargin: 18
            topMargin: 15
            bottomMargin: 15
        }
        spacing: 5

        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: root.title
            color: Theme.textMuted
            font.pixelSize: 12
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: root.value
            color: Theme.text
            font.pixelSize: 22
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            visible: root.hint.length > 0
            text: root.hint
            color: root.accent
            font.pixelSize: 11
            elide: Text.ElideRight
        }
    }
}
