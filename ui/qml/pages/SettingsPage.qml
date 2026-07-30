import QtQuick
import QtQuick.Layouts
import "../components"

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pageMargin
        spacing: 20

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                text: qsTr("Настройки")
                color: Theme.text
                font.pixelSize: 26
                font.weight: Font.DemiBold
            }
            Text {
                text: qsTr("Отображение, безопасность и локальное поведение")
                color: Theme.textMuted
                font.pixelSize: 12
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 300

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 20

                Text {
                    text: qsTr("Основные")
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 24
                    rowSpacing: 20

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 7
                        Text {
                            text: qsTr("Основная валюта")
                            color: Theme.text
                            font.pixelSize: 12
                            font.weight: Font.Medium
                        }
                        MinimalComboBox {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            model: ["RUB — ₽", "CNY — ¥", "USD — $", "EUR — €"]
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 7
                        Text {
                            text: qsTr("Автоблокировка")
                            color: Theme.text
                            font.pixelSize: 12
                            font.weight: Font.Medium
                        }
                        MinimalComboBox {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            model: [qsTr("5 минут"), qsTr("15 минут"), qsTr("30 минут"), qsTr("Никогда")]
                            currentIndex: 1
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.border
                }

                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text { text: qsTr("Синхронизация при запуске"); color: Theme.text; font.pixelSize: 12; font.weight: Font.Medium }
                        Text { text: qsTr("Запускать только подключённые read-only коннекторы"); color: Theme.textMuted; font.pixelSize: 11 }
                    }
                    MinimalSwitch { checked: false }
                }

                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text { text: qsTr("Демо-режим"); color: Theme.text; font.pixelSize: 12; font.weight: Font.Medium }
                        Text { text: qsTr("Показывать пример данных вместо пустого состояния"); color: Theme.textMuted; font.pixelSize: 11 }
                    }
                    MinimalSwitch {
                        checked: App.demoMode
                        onToggled: checked ? App.loadDemoData() : App.clearDemoData()
                    }
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            RowLayout {
                anchors.fill: parent
                anchors.margins: 20
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text { text: qsTr("Заблокировать хранилище"); color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                    Text { text: qsTr("Потребуется системный ключ для повторного доступа"); color: Theme.textMuted; font.pixelSize: 11 }
                }
                ActionButton {
                    text: qsTr("Заблокировать")
                    onClicked: App.lock()
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
