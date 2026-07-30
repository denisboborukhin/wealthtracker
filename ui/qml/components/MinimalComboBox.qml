pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

ComboBox {
    id: root

    implicitHeight: 40
    leftPadding: 12
    rightPadding: 36

    contentItem: Text {
        text: root.displayText
        color: Theme.text
        font.pixelSize: 12
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Item {
        x: root.width - width
        width: 36
        height: root.height

        Rectangle {
            x: 12
            y: 18
            width: 7
            height: 1
            color: Theme.textMuted
            rotation: 40
            transformOrigin: Item.Right
        }

        Rectangle {
            x: 17
            y: 18
            width: 7
            height: 1
            color: Theme.textMuted
            rotation: -40
            transformOrigin: Item.Left
        }
    }

    background: Rectangle {
        color: Theme.surface
        border.color: root.activeFocus ? Theme.primary : Theme.borderStrong
        radius: 7
    }

    delegate: ItemDelegate {
        id: optionDelegate

        required property var modelData
        required property int index

        width: root.width - 8
        height: 36
        leftPadding: 10
        text: modelData
        highlighted: root.highlightedIndex === index

        contentItem: Text {
            text: optionDelegate.text
            color: Theme.text
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            color: optionDelegate.highlighted ? Theme.surfaceRaised : "transparent"
            radius: 5
        }
    }

    popup: Popup {
        y: root.height + 4
        width: root.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 220)
        padding: 4

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
        }

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.borderStrong
            radius: 7
        }
    }
}
