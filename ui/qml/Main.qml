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
    minimumWidth: 1020
    minimumHeight: 680
    visible: true
    title: "WealthTracker"
    color: Theme.background
    property alias currentPage: navigation.currentIndex

    readonly property var navigationItems: [
        { "title": qsTr("Обзор") },
        { "title": qsTr("Счета") },
        { "title": qsTr("Операции") },
        { "title": qsTr("Импорт") },
        { "title": qsTr("Инвестиции") },
        { "title": qsTr("Категории") },
        { "title": qsTr("Подключения") },
        { "title": qsTr("Настройки") }
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 204
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
                    delegate: Rectangle {
                        id: navigationItem
                        required property int index
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        radius: 7
                        color: navigation.currentIndex === navigationItem.index ? Theme.surfaceRaised : "transparent"
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            text: navigationItem.modelData.title
                            color: navigation.currentIndex === navigationItem.index ? Theme.text : Theme.textMuted
                            font.pixelSize: 13
                            font.weight: navigation.currentIndex === navigationItem.index ? Font.Medium : Font.Normal
                            verticalAlignment: Text.AlignVCenter
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: navigation.currentIndex = navigationItem.index
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
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: 0

            DashboardPage { app: window.app }
            AccountsPage { app: window.app }
            TransactionsPage { app: window.app }
            ImportsPage { app: window.app }
            InvestmentsPage { app: window.app }
            CategoriesPage { app: window.app }
            ConnectionsPage { app: window.app }
            SettingsPage { app: window.app }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: window.app.locked
        z: 100
        color: "#D9F5F6F7"

        Rectangle {
            anchors.centerIn: parent
            width: 420
            height: 240
            radius: 12
            color: Theme.surface
            border.color: Theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 28
                spacing: 14
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Финансовые данные скрыты")
                    color: Theme.text
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Активные подключения остановлены. Покажите данные, чтобы продолжить работу.")
                    color: Theme.textMuted
                    wrapMode: Text.WordWrap
                }
                ActionButton {
                    Layout.fillWidth: true
                    text: qsTr("Показать данные")
                    primary: true
                    onClicked: window.app.unlock()
                }
            }
        }
    }
}
