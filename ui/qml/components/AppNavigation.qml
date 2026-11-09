pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: navigation
    required property bool locked
    property int currentIndex: 0
    readonly property real contentLeft: navigationRail.width
    readonly property real contentTop: mobileNavigation.height
    readonly property real navigationWidth: 204
    readonly property bool compact: Qt.platform.os === "android" || width < 760

    readonly property var navigationItems: [
        { "title": qsTr("Обзор") },
        { "title": qsTr("История") },
        { "title": qsTr("Инвестиции") },
        { "title": qsTr("Криптокошельки") },
        { "title": qsTr("Подключения") },
        { "title": qsTr("Настройки") },
        { "title": qsTr("Кэшбэк") }
    ]

    ToolBar {
        id: mobileNavigation
        objectName: "compactNavigation"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        visible: navigation.compact
        height: visible ? 56 : 0
        background: Rectangle {
            color: Theme.surface
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            Text {
                text: "WealthTracker"
                color: Theme.text
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            MinimalComboBox {
                objectName: "navigationSelector"
                Layout.preferredWidth: 160
                model: navigation.navigationItems.map(item => item.title)
                currentIndex: navigation.currentIndex
                onActivated: navigation.currentIndex = currentIndex
                enabled: !navigation.locked
            }
        }
    }

    Rectangle {
        id: navigationRail
        objectName: "navigationRail"
        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
        }
        width: navigation.compact ? 0 : navigation.navigationWidth
        visible: !navigation.compact
        color: Theme.surface

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            anchors.topMargin: 22
            anchors.bottomMargin: 18
            spacing: 5

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 10
                Layout.bottomMargin: 24
                text: "WealthTracker"
                color: Theme.text
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            Repeater {
                model: navigation.navigationItems
                delegate: Button {
                    id: navigationItem
                    objectName: "navigationItem_" + index
                    required property int index
                    required property var modelData

                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    flat: true
                    Accessible.name: navigationItem.modelData.title
                    onClicked: navigation.currentIndex = navigationItem.index
                    contentItem: Text {
                        text: navigationItem.modelData.title
                        color: navigation.currentIndex === navigationItem.index ? Theme.text : Theme.textMuted
                        font.pixelSize: 13
                        font.weight: navigation.currentIndex === navigationItem.index ? Font.Medium : Font.Normal
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 12
                        rightPadding: 12
                    }
                    background: Rectangle {
                        radius: 7
                        color: navigation.currentIndex === navigationItem.index
                               ? Theme.surfaceRaised : "transparent"
                        border.color: navigationItem.activeFocus
                                      ? Theme.primary : "transparent"
                    }
                }
            }
            Item { Layout.fillHeight: true }
            Text {
                Layout.fillWidth: true
                text: qsTr("Данные остаются на устройстве")
                color: Theme.textMuted
                font.pixelSize: 10
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignLeft
                Layout.leftMargin: 10
                Layout.rightMargin: 10
            }
        }

        Rectangle {
            width: 1
            color: Theme.border
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
        }
    }
}
