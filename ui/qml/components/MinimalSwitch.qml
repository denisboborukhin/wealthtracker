import QtQuick
import QtQuick.Controls

Switch {
    id: root

    implicitWidth: 40
    implicitHeight: 24
    padding: 0

    indicator: Rectangle {
        implicitWidth: 40
        implicitHeight: 22
        x: 0
        y: 1
        radius: 11
        color: root.checked ? Theme.primary : Theme.borderStrong

        Rectangle {
            x: root.checked ? 20 : 2
            y: 2
            width: 18
            height: 18
            radius: 9
            color: Theme.surface

            Behavior on x {
                NumberAnimation { duration: 120 }
            }
        }
    }

    contentItem: Item {}
}
