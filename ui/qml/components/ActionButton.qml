import QtQuick
import QtQuick.Controls

Button {
    id: root
    property bool primary: false
    property bool destructive: false

    Accessible.name: text
    implicitHeight: 36
    leftPadding: 16
    rightPadding: 16
    topPadding: 8
    bottomPadding: 8

    contentItem: Text {
        text: root.text
        color: !root.enabled ? Theme.textMuted
              : root.primary ? "#FFFFFF"
              : root.destructive ? Theme.negative
              : Theme.text
        font.pixelSize: 13
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 7
        color: !root.enabled ? Theme.surfaceRaised
              : root.primary ? (root.down ? "#2445B7" : Theme.primary)
              : root.down ? Theme.surfaceRaised
              : Theme.surface
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus
                      ? (root.primary ? "#FFFFFF" : Theme.primary)
                      : root.primary ? Theme.primary
                      : root.destructive ? "#E8BFC2"
                      : Theme.borderStrong
    }
}
