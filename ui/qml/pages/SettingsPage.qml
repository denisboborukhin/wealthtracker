import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 18
        Text {
            text: qsTr("Настройки")
            color: Theme.text
            font.pixelSize: 30
            font.weight: Font.DemiBold
        }
        SectionCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 250
            GridLayout {
                anchors.fill: parent
                anchors.margins: 22
                columns: 2
                rowSpacing: 18
                columnSpacing: 30
                Text { text: qsTr("Основная валюта"); color: Theme.text }
                ComboBox { model: ["RUB — ₽", "CNY — ¥", "USD — $", "EUR — €"] }
                Text { text: qsTr("Автоблокировка"); color: Theme.text }
                ComboBox { model: [qsTr("5 минут"), qsTr("15 минут"), qsTr("30 минут"), qsTr("Никогда")] ; currentIndex: 1 }
                Text { text: qsTr("Синхронизация при запуске"); color: Theme.text }
                Switch { checked: false }
                Text { text: qsTr("Демо-режим"); color: Theme.text }
                Switch {
                    checked: App.demoMode
                    onToggled: checked ? App.loadDemoData() : App.clearDemoData()
                }
            }
        }
        Button { text: qsTr("Заблокировать хранилище"); onClicked: App.lock() }
        Item { Layout.fillHeight: true }
    }
}
