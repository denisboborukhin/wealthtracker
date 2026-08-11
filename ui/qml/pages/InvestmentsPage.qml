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
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 4
                Text { text: qsTr("Инвестиции"); color: Theme.text; font.pixelSize: 26; font.weight: Font.DemiBold }
                Text { text: page.app.syncStatus; color: Theme.textMuted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Проверить подключение")
                primary: true
                onClicked: page.app.synchronizeInvestments()
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 12
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Стоимость портфеля")
                value: page.app.portfolioValue
                hint: page.app.demoMode ? qsTr("+14,8% за всё время")
                                   : qsTr("Текущая оценка")
                accent: Theme.positive
            }
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Дивиденды и купоны")
                value: page.app.demoMode ? "86 420 ₽" : "0 ₽"
                hint: page.app.demoMode ? qsTr("За 12 месяцев")
                                   : qsTr("Нет данных")
                accent: Theme.positive
            }
            MetricCard {
                Layout.fillWidth: true
                title: "XIRR"
                value: page.app.demoMode ? "17,6%" : "—"
                hint: page.app.demoMode
                      ? qsTr("Не является налоговым расчётом")
                      : qsTr("Недостаточно операций")
                accent: Theme.textMuted
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    Layout.preferredHeight: 42
                    Text { Layout.preferredWidth: 92; text: qsTr("Тикер"); color: Theme.textMuted; font.pixelSize: 11 }
                    Text { Layout.fillWidth: true; text: qsTr("Актив"); color: Theme.textMuted; font.pixelSize: 11 }
                    Text { Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight; text: qsTr("Доля"); color: Theme.textMuted; font.pixelSize: 11 }
                    Text { Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight; text: qsTr("Доходность"); color: Theme.textMuted; font.pixelSize: 11 }
                    Text { Layout.preferredWidth: 125; horizontalAlignment: Text.AlignRight; text: qsTr("Стоимость"); color: Theme.textMuted; font.pixelSize: 11 }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

                ListView {
                    id: holdingList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: count > 0
                    model: page.app.holdings
                    clip: true
                    delegate: Item {
                        id: holdingRow
                        required property string ticker
                        required property string name
                        required property string allocation
                        required property string performance
                        required property string value
                        required property bool positive

                        width: ListView.view.width
                        height: 54
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 18
                            anchors.rightMargin: 18
                            Text { Layout.preferredWidth: 92; text: holdingRow.ticker; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                            Text { Layout.fillWidth: true; text: holdingRow.name; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
                            Text { Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight; text: holdingRow.allocation; color: Theme.textMuted; font.pixelSize: 12 }
                            Text { Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight; text: holdingRow.performance; color: holdingRow.positive ? Theme.positive : Theme.negative; font.pixelSize: 12 }
                            Text { Layout.preferredWidth: 125; horizontalAlignment: Text.AlignRight; text: holdingRow.value; color: Theme.text; font.pixelSize: 12; font.weight: Font.Medium }
                        }
                        Rectangle { width: parent.width; height: 1; anchors.bottom: parent.bottom; color: Theme.border }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: holdingList.count === 0
                    text: qsTr("Активов пока нет. Подключите инвестиционный счёт.")
                    color: Theme.textMuted
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
