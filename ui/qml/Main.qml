pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"
import "pages"

ApplicationWindow {
    id: window
    required property var app
    width: 1320
    height: 820
    minimumWidth: Qt.platform.os === "android" ? 320 : 1020
    minimumHeight: Qt.platform.os === "android" ? 0 : 680
    visible: true
    title: "WealthTracker"
    color: Theme.background
    property alias currentPage: navigation.currentIndex
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

    Item {
        anchors.fill: parent

        ToolBar {
            id: mobileNavigation
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            visible: window.compact
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
                    Layout.preferredWidth: 160
                    model: window.navigationItems.map(item => item.title)
                    currentIndex: navigation.currentIndex
                    onActivated: navigation.currentIndex = currentIndex
                    enabled: !window.app.locked
                }
            }
        }

        Rectangle {
            id: navigationRail
            anchors {
                left: parent.left
                top: parent.top
                bottom: parent.bottom
            }
            width: window.compact ? 0 : window.navigationWidth
            visible: !window.compact
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
                    model: window.navigationItems
                    delegate: Button {
                        id: navigationItem
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

        StackLayout {
            id: navigation
            anchors {
                left: navigationRail.right
                right: parent.right
                top: mobileNavigation.bottom
                bottom: parent.bottom
            }
            currentIndex: 0

            DashboardPage { app: window.app }
            HistoryPage { app: window.app }
            InvestmentsPage { app: window.app }
            CryptoPage { app: window.app }
            ConnectionsPage { app: window.app }
            SettingsPage { app: window.app }
            CashbackPage { app: window.app }
        }
    }

    PrivacyScreen {
        anchors.fill: parent
        visible: window.app.locked
        onUnlockRequested: window.app.unlock()
    }

}
