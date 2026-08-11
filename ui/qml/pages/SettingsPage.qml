pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import "../components"

Item {
    id: page
    required property var app

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
                text: qsTr("Локальное поведение приложения")
                color: Theme.textMuted
                font.pixelSize: 12
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 142

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 20

                Text {
                    text: qsTr("Данные для знакомства")
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
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
                        checked: page.app.demoMode
                        onToggled: checked ? page.app.loadDemoData() : page.app.clearDemoData()
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
                    Text { text: qsTr("Скрыть финансовые данные"); color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                    Text { text: qsTr("Закрывает вход в банк и останавливает активные синхронизации"); color: Theme.textMuted; font.pixelSize: 11 }
                }
                ActionButton {
                    text: qsTr("Скрыть")
                    onClicked: page.app.lock()
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
