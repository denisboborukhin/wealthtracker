import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "pages"

ApplicationWindow {
    id: window
    width: 1380
    height: 860
    minimumWidth: 980
    minimumHeight: 680
    visible: true
    title: "WealthTracker"
    color: Theme.background

    readonly property var navigationItems: [
        { "title": qsTr("Обзор"), "symbol": "◫" },
        { "title": qsTr("Счета"), "symbol": "◉" },
        { "title": qsTr("Операции"), "symbol": "↕" },
        { "title": qsTr("Импорт"), "symbol": "⇩" },
        { "title": qsTr("Инвестиции"), "symbol": "⌁" },
        { "title": qsTr("Категории"), "symbol": "◈" },
        { "title": qsTr("Подключения"), "symbol": "⌘" },
        { "title": qsTr("Копии"), "symbol": "▣" },
        { "title": qsTr("Настройки"), "symbol": "⚙" }
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 224
            color: "#0E1628"
            border.color: Theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 22
                    Rectangle {
                        implicitWidth: 38
                        implicitHeight: 38
                        radius: 12
                        color: Theme.primary
                        Text { anchors.centerIn: parent; text: "W"; color: "#0B1020"; font.pixelSize: 20; font.bold: true }
                    }
                    Text {
                        text: "WealthTracker"
                        color: Theme.text
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                }

                Repeater {
                    model: window.navigationItems
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        radius: 10
                        color: navigation.currentIndex === index ? Theme.surfaceRaised : "transparent"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            Text {
                                Layout.preferredWidth: 24
                                text: modelData.symbol
                                color: navigation.currentIndex === index ? Theme.primary : Theme.textMuted
                                font.pixelSize: 16
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.title
                                color: navigation.currentIndex === index ? Theme.text : Theme.textMuted
                                font.pixelSize: 13
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: navigation.currentIndex = index
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Локально · Без телеметрии")
                    color: Theme.textMuted
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        StackLayout {
            id: navigation
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: 0

            DashboardPage {}
            AccountsPage {}
            TransactionsPage {}
            ImportsPage {}
            InvestmentsPage {}
            CategoriesPage {}
            ConnectionsPage {}
            BackupsPage {}
            SettingsPage {}
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: App.locked
        z: 100
        color: "#F00B1020"

        Rectangle {
            anchors.centerIn: parent
            width: 390
            height: 280
            radius: 18
            color: Theme.surface
            border.color: Theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 28
                spacing: 14
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Хранилище заблокировано")
                    color: Theme.text
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Ключ будет получен из защищённого хранилища операционной системы")
                    color: Theme.textMuted
                    wrapMode: Text.WordWrap
                }
                Button {
                    Layout.fillWidth: true
                    text: qsTr("Разблокировать")
                    onClicked: App.unlock()
                }
            }
        }
    }
}
