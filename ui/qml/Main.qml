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
    readonly property real navigationWidth: 204

    readonly property var navigationItems: [
        { "title": qsTr("Обзор") },
        { "title": qsTr("Счета") },
        { "title": qsTr("История") },
        { "title": qsTr("Операции") },
        { "title": qsTr("Импорт") },
        { "title": qsTr("Инвестиции") },
        { "title": qsTr("Криптокошельки") },
        { "title": qsTr("Категории") },
        { "title": qsTr("Подключения") },
        { "title": qsTr("Настройки") }
    ]

    Item {
        anchors.fill: parent

        Rectangle {
            id: navigationRail
            anchors {
                left: parent.left
                top: parent.top
                bottom: parent.bottom
            }
            width: window.navigationWidth
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
                top: parent.top
                bottom: parent.bottom
            }
            currentIndex: 0

            DashboardPage { app: window.app }
            AccountsPage { app: window.app }
            HistoryPage { app: window.app }
            TransactionsPage { app: window.app }
            ImportsPage { app: window.app }
            InvestmentsPage { app: window.app }
            CryptoPage { app: window.app }
            CategoriesPage { app: window.app }
            ConnectionsPage { app: window.app }
            SettingsPage { app: window.app }
        }
    }

    FocusScope {
        id: privacyScreen
        anchors.fill: parent
        visible: window.app.locked
        focus: visible
        z: 100

        onVisibleChanged: {
            if (visible)
                returnToDataButton.forceActiveFocus()
        }
        Keys.onPressed: function(event) {
            event.accepted = true
        }

        Rectangle {
            anchors.fill: parent
            color: Theme.background
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(440, parent.width - 48)
            height: 276
            radius: 12
            color: Theme.surface
            border.color: Theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 28
                spacing: 14
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Экран приватности включён")
                    color: Theme.text
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Суммы и операции скрыты. Текущие входы и синхронизации остановлены, сохранённые данные не удалены.")
                    color: Theme.textMuted
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Это не блокировка приложения: для возврата пароль не требуется.")
                    color: Theme.textMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
                ActionButton {
                    id: returnToDataButton
                    Layout.fillWidth: true
                    text: qsTr("Вернуться к данным")
                    primary: true
                    onClicked: window.app.unlock()
                }
            }
        }
    }
}
